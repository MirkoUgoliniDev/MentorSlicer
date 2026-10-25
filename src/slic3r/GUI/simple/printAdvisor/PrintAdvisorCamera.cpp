#include "PrintAdvisorInternal.hpp"

#include "PrintAdvisorPageBridge.hpp"
#include "slic3r/GUI/print_manage/App/PrinterMgrView.hpp"

#include <boost/log/trivial.hpp>
#include <wx/app.h>
#include <wx/notifmsg.h>

#include <chrono>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace Slic3r {
namespace GUI {

using nlohmann::json;

namespace {

// Requests sent to the device page and waiting for "advisor_snapshot_result". The server
// thread starts and waits; the GUI thread (OnScriptMessage) delivers. Entries are always
// removed by the waiter, so a late or unknown reply is dropped.
class SnapshotRegistry
{
public:
    static SnapshotRegistry& instance()
    {
        static SnapshotRegistry registry;
        return registry;
    }

    std::string start()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::system_clock::now().time_since_epoch()).count();
        std::string id = "advisor-snapshot-" + std::to_string(now) + "-" + std::to_string(++m_seq);
        m_pending.emplace(id, std::nullopt);
        return id;
    }

    void deliver(json&& result)
    {
        const auto it_id = result.find("request_id");
        if (it_id == result.end() || !it_id->is_string())
            return;
        const std::string id = it_id->get<std::string>();
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_pending.find(id);
            if (it == m_pending.end() || it->second.has_value())
                return;
            it->second = std::move(result);
        }
        m_cv.notify_all();
    }

    // Reply for id, or nullopt on timeout / stop. Removes the entry in every case.
    std::optional<json> wait(const std::string& id, std::chrono::milliseconds timeout, const std::atomic<bool>& stopping)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        std::unique_lock<std::mutex> lock(m_mutex);
        std::optional<json> reply;
        while (true) {
            auto it = m_pending.find(id);
            if (it == m_pending.end())
                break;
            if (it->second.has_value()) {
                reply = std::move(it->second);
                break;
            }
            if (stopping.load() || std::chrono::steady_clock::now() >= deadline)
                break;
            m_cv.wait_for(lock, std::chrono::milliseconds(100));
        }
        m_pending.erase(id);
        return reply;
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    unsigned long long m_seq{0};
    std::unordered_map<std::string, std::optional<json>> m_pending;
};

const char* const k_snapshot_hint =
    "Apri la scheda Dispositivo in MentorSlicer, entra nella pagina di dettaglio della stampante "
    "e assicurati che il video sia in riproduzione, poi riprova.";

constexpr size_t k_max_image_bytes = 3 * 1024 * 1024;

// Visible sign that the printer camera was read: a short Windows notification on every
// snapshot (no dialog), plus a log line. Called on the server thread.
void announce_snapshot()
{
    BOOST_LOG_TRIVIAL(info) << "Print Advisor: foto dalla telecamera acquisita";
    if (wxTheApp == nullptr)
        return;
    wxTheApp->CallAfter([] {
        // wxNotificationMessage must stay alive after Show(): keep the last one until the next
        // replaces it. Like print_advisor_notify (DeviceTelemetryCache.cpp), the last one is
        // deliberately not destroyed at exit: a static destructor would run after wx shut down.
        static wxNotificationMessage* notification = nullptr;
        delete notification;
        notification = new wxNotificationMessage(wxString::FromUTF8("MCP Advisor"),
            wxString::FromUTF8("Print Advisor: foto dalla telecamera acquisita"));
        notification->Show(wxNotificationMessage::Timeout_Auto);
    });
}

json snapshot_error(const std::string& error)
{
    return {{"success", false}, {"error", error + " " + k_snapshot_hint}};
}

json camera_snapshot(const json&, const std::atomic<bool>& stopping)
{
    if (wxTheApp == nullptr)
        return {{"success", false}, {"error", "MentorSlicer is closing"}};

    const std::string id = SnapshotRegistry::instance().start();
    wxTheApp->CallAfter([id] {
        if (!advisor_device_page_loaded()) {
            advisor_deliver_snapshot_result(
                json{{"request_id", id}, {"ok", false}, {"error", "La pagina Dispositivo non e' caricata."}});
            return;
        }
        // The printer Print Advisor acts on: the page reports whether its video shows that one.
        std::string address;
        try {
            address = advisor::target_device_address();
        } catch (...) {
        }
        advisor_push_device_page(json{{"command", "advisor_snapshot"}, {"request_id", id},
                                      {"address", address.empty() ? json(nullptr) : json(address)}});
    });

    std::optional<json> reply = SnapshotRegistry::instance().wait(id, std::chrono::seconds(10), stopping);
    if (stopping.load())
        return {{"success", false}, {"error", "Print Advisor is stopping"}};
    if (!reply)
        return snapshot_error("Nessun fotogramma ricevuto entro 10 s.");
    if (!reply->value("ok", false)) {
        std::string error = "La pagina Dispositivo non ha catturato il fotogramma";
        const auto it = reply->find("error");
        // Text of the web page: made safe and short before it reaches the MCP client.
        if (it != reply->end() && it->is_string() && !it->get_ref<const std::string&>().empty())
            error += ": " + advisor::sanitized_text(it->get<std::string>(), 128);
        return snapshot_error(error + ".");
    }

    auto it_url = reply->find("data_url");
    if (it_url == reply->end() || !it_url->is_string())
        return snapshot_error("Risposta della pagina senza immagine.");
    std::string& data_url = it_url->get_ref<std::string&>();
    // "data:<mime>;base64,<data>"
    const size_t comma = data_url.find(',');
    const std::string prefix = comma == std::string::npos ? std::string() : data_url.substr(0, comma);
    const std::string base64_tag = ";base64";
    if (prefix.rfind("data:image/", 0) != 0 || prefix.size() <= base64_tag.size() ||
        prefix.compare(prefix.size() - base64_tag.size(), base64_tag.size(), base64_tag) != 0)
        return snapshot_error("Formato dell'immagine non riconosciuto.");
    const std::string mime = prefix.substr(5, prefix.size() - 5 - base64_tag.size());
    const size_t base64_size = data_url.size() - comma - 1;
    if (base64_size == 0)
        return snapshot_error("Immagine vuota (il video e' in pausa o non ancora avviato?).");
    if (base64_size / 4 * 3 > k_max_image_bytes)
        return {{"success", false}, {"error", "Immagine troppo grande (oltre 3 MB)."}};

    data_url.erase(0, comma + 1); // in place: avoids another copy of the image
    announce_snapshot();
    json result = {{"success", true}, {"mime", mime}, {"width", reply->value("width", 0)},
                   {"height", reply->value("height", 0)}};
    // Printer shown by the page, and whether it is the one that was asked for.
    if (const auto it = reply->find("address"); it != reply->end() && it->is_string())
        result["address"] = advisor::sanitized_text(it->get<std::string>());
    if (const auto it = reply->find("address_matched"); it != reply->end() && it->is_boolean()) {
        result["address_matched"] = it->get<bool>();
        if (!it->get<bool>())
            result["warning"] = "Il fotogramma viene dal video aperto nella scheda Dispositivo, che non e' la stampante "
                                "selezionata in MentorSlicer: controlla quale stampante mostra.";
    }
    result["data_base64"] = std::move(data_url);
    return result;
}

} // namespace

void advisor_deliver_snapshot_result(json&& result)
{
    try {
        SnapshotRegistry::instance().deliver(std::move(result));
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "advisor_deliver_snapshot_result failed: " << e.what();
    }
}

namespace advisor {

json camera_snapshot_now(const std::atomic<bool>& stopping)
{
    return camera_snapshot(json::object(), stopping);
}

// Camera routes: one frame of the printer video, captured by the device manager page.
// Read-only, so no confirmation dialog. Runs on the server thread: it waits for the page reply
// without blocking the GUI thread.
std::vector<AdvisorRoute> camera_routes()
{
    return {{"/v1/camera_snapshot", "GET", false, camera_snapshot}};
}

} // namespace advisor
} // namespace GUI
} // namespace Slic3r
