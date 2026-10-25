#include "DeviceTelemetryCache.h"
#include "PrintAdvisorMonitor.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"

#include <wx/notifmsg.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>

namespace Slic3r {
namespace GUI {

namespace {

// Status fields kept for each printer; identifiers and network data are not stored. The name of
// the printing file is kept for the Print Advisor print log.
constexpr std::array<const char*, 40> TELEMETRY_KEYS{
    "model", "deviceState", "state", "printProgress", "layer", "TotalLayer",
    "printJobTime", "printLeftTime", "err",
    "nozzleTemp", "targetNozzleTemp", "maxNozzleTemp",
    "bedTemp0", "targetBedTemp0", "maxBedTemp",
    "boxTemp", "targetBoxTemp", "maxBoxTemp",
    "modelFanPct", "auxiliaryFanPct", "caseFanPct", "fan", "fanAuxiliary", "fanCase",
    "curFeedratePct", "nozzleDiameter", "lightSw",
    "printStartTime", "printFileName", "materialDetect", "materialStatus", "materialDetector1",
    "aiSw", "aiDetection", "aiFirstFloor", "aiPausePrint", "curFlowratePct", "curZOffset",
    "speedMode", "sleepMode"
};

constexpr size_t MAX_DEVICES    = 16;
constexpr size_t MAX_EVENTS     = 20;
constexpr size_t MAX_TEXT_CHARS = 128;
// A reconnection within this time keeps the previous state current for the finish events.
constexpr long long SHORT_DISCONNECT_MS = 30000;
// Telemetry numbers are temperatures, percentages, counters and Unix times: anything larger is
// garbage and is dropped, so later integer conversions stay in range.
constexpr double MAX_ABS_NUMBER = 1e12;

// Integer value of a JSON number or numeric string, or -1 when it is not a finite number in the
// int range (a huge float converted with get<int> is undefined behaviour).
int number_of(const nlohmann::json& value)
{
    double number = std::numeric_limits<double>::quiet_NaN();
    if (value.is_number())
        number = value.get<double>();
    else if (value.is_string()) {
        std::istringstream stream(value.get<std::string>());
        stream.imbue(std::locale::classic());
        double parsed = 0;
        if (stream >> parsed)
            number = parsed;
    }
    if (!std::isfinite(number) || number < double(std::numeric_limits<int>::min()) ||
        number > double(std::numeric_limits<int>::max()))
        return -1;
    return int(number);
}

// Characters never kept from a printer string: C0/C1 controls become spaces; zero-width,
// bidirectional and line/paragraph separator characters are dropped.
bool is_format_char(std::uint32_t cp)
{
    return cp == 0x061C || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E) ||
           (cp >= 0x2060 && cp <= 0x2069) || cp == 0xFEFF;
}

// UTF-8 text safe to store and pass on: invalid sequences removed, controls replaced with
// spaces, formatting characters dropped, at most MAX_TEXT_CHARS code points.
std::string sanitized_text(const std::string& text)
{
    std::string out;
    size_t chars = 0;
    size_t i = 0;
    while (i < text.size() && chars < MAX_TEXT_CHARS) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        const size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 0;
        if (length == 0 || i + length > text.size()) {
            ++i;
            continue;
        }
        std::uint32_t cp = length == 1 ? lead : length == 2 ? (lead & 0x1Fu) : length == 3 ? (lead & 0x0Fu) : (lead & 0x07u);
        bool valid = true;
        for (size_t k = 1; k < length; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            cp = (cp << 6) | (next & 0x3Fu);
        }
        if (!valid) {
            ++i;
            continue;
        }
        const bool overlong = (length == 2 && cp < 0x80) || (length == 3 && cp < 0x800) || (length == 4 && cp < 0x10000);
        if (overlong || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF) || is_format_char(cp)) {
            i += length;
            continue;
        }
        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F))
            out += ' ';
        else
            out.append(text, i, length);
        ++chars;
        i += length;
    }
    return out;
}

// Value of a telemetry field as stored in the cache, or null to drop it.
nlohmann::json sanitized_field(const std::string& key, const nlohmann::json& value)
{
    if (key == "err") {
        if (!value.is_object())
            return nullptr;
        // Only the numbers: the error text ("value") is not passed on.
        return {{"errcode", number_of(value.value("errcode", nlohmann::json(-1)))},
                {"key", number_of(value.value("key", nlohmann::json(-1)))}};
    }
    if (value.is_object() || value.is_array() || value.is_null())
        return nullptr;
    if (key == "state" || key == "deviceState") {
        const int state = number_of(value);
        return state >= 0 ? nlohmann::json(state) : nlohmann::json(nullptr);
    }
    if (value.is_number()) {
        const double number = value.get<double>();
        return std::isfinite(number) && std::abs(number) <= MAX_ABS_NUMBER ? value : nlohmann::json(nullptr);
    }
    if (value.is_string()) {
        std::string text = value.get<std::string>();
        // Only the file name is kept, never the directories on the printer.
        if (key == "printFileName") {
            const size_t slash = text.find_last_of("/\\");
            if (slash != std::string::npos)
                text = text.substr(slash + 1);
        }
        return sanitized_text(text);
    }
    return value; // boolean
}

bool is_active_state(int state)
{
    return state == 1 || state == 5 || state == 6 || state == 7 || state == 8;
}

long long wall_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

long long mono_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

// Windows notification on the GUI thread, only while Print Advisor is enabled. Any thread.
void print_advisor_notify(const std::string& title, const std::string& message, bool even_when_disabled)
{
    if (!wxTheApp)
        return;
    wxTheApp->CallAfter([title, message, even_when_disabled]() {
        auto* config = wxGetApp().app_config;
        if (!even_when_disabled && (!config || !config->get_bool("print_advisor_mcp_enabled")))
            return;
        // The notification must outlive Show(); keep the last one until the next replaces it.
        // The last one is deliberately not destroyed at exit, after wx has shut down.
        static wxNotificationMessage* notification = nullptr;
        delete notification;
        notification = new wxNotificationMessage(wxString::FromUTF8(title), wxString::FromUTF8(message));
        notification->Show(wxNotificationMessage::Timeout_Auto);
    });
}

DeviceTelemetryCache& DeviceTelemetryCache::instance()
{
    // Never destroyed, like the monitor: the proxy thread may still deliver telemetry or a
    // session close while the process exits, after static destructors have started.
    static DeviceTelemetryCache* cache = new DeviceTelemetryCache();
    return *cache;
}

void DeviceTelemetryCache::update(const std::string& host, std::string_view payload)
{
    if (host.empty() || payload.empty() || payload.front() != '{')
        return;
    nlohmann::json message = nlohmann::json::parse(payload.begin(), payload.end(), nullptr, false);
    if (!message.is_object())
        return;

    nlohmann::json fields = nlohmann::json::object();
    for (const char* key : TELEMETRY_KEYS) {
        auto it = message.find(key);
        if (it == message.end())
            continue;
        nlohmann::json value = sanitized_field(key, *it);
        if (!value.is_null())
            fields[key] = std::move(value);
    }
    if (fields.empty())
        return;

    const long long now = wall_ms();
    const long long now_mono = mono_ms();
    std::string event_type, event_detail;
    nlohmann::json device_snapshot;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto device = m_devices.find(host);
        if (device == m_devices.end()) {
            if (m_devices.size() >= MAX_DEVICES)
                return;
            device = m_devices.emplace(host, nlohmann::json::object()).first;
        }
        const int previous_state = device->second.contains("state") ? number_of(device->second["state"]) : -1;
        // The previous state describes the printer only when it was reported on the connection
        // still open, or on the one that closed less than SHORT_DISCONNECT_MS ago (a finish during
        // a short reconnection is still reported). After a longer gap it may be hours old: the
        // new state is adopted silently instead of reporting a late finish.
        bool previous_state_current = false;
        if (device->second.value("connected", false)) {
            auto stale = m_stale.find(host);
            previous_state_current = stale == m_stale.end() || !stale->second.count("state");
        } else if (auto gap = m_disconnects.find(host); gap != m_disconnects.end())
            previous_state_current = gap->second.state_current && now_mono - gap->second.at_mono < SHORT_DISCONNECT_MS;
        m_disconnects.erase(host);
        int previous_error = -1;
        if (device->second.contains("err") && device->second["err"].is_object())
            previous_error = number_of(device->second["err"].value("errcode", nlohmann::json(-1)));
        // The printer resets the layer counters together with the final state: keep the last ones.
        const nlohmann::json previous_layer = device->second.value("layer", nlohmann::json(0));
        const nlohmann::json previous_total = device->second.value("TotalLayer", nlohmann::json(0));
        device->second.update(fields);
        device->second["updated_at_ms"] = now;
        m_updated_mono[host] = now_mono;
        device->second["connected"] = true;
        device->second.erase("disconnected_at_ms");
        // Fields reported again are current; the others stay stale.
        auto stale = m_stale.find(host);
        if (stale != m_stale.end()) {
            for (const auto& item : fields.items())
                stale->second.erase(item.key());
            if (stale->second.empty()) {
                m_stale.erase(stale);
                device->second.erase("stale_keys");
            } else
                device->second["stale_keys"] = stale->second;
        }
        device_snapshot = device->second;

        // A print leaving an active state is a finish, failure or abort.
        if (fields.contains("state")) {
            const int state = number_of(fields["state"]);
            if (previous_state_current && is_active_state(previous_state) && (state == 2 || state == 3 || state == 4)) {
                event_type = state == 2 ? "print_complete" : state == 3 ? "print_failed" : "print_aborted";
                event_detail = "layer " + previous_layer.dump() + "/" + previous_total.dump();
            }
        }
        // Error codes 0, 30000 and 30010 are not shown as errors by the device page either.
        if (event_type.empty() && fields.contains("err") && fields["err"].is_object()) {
            const int code = number_of(fields["err"].value("errcode", nlohmann::json(-1)));
            const int key = number_of(fields["err"].value("key", nlohmann::json(-1)));
            if (code > 0 && code != previous_error && key != 30000 && key != 30010) {
                event_type = "printer_error";
                event_detail = "errcode " + std::to_string(code) + ", key " + std::to_string(key);
            }
        }
    }
    if (!event_type.empty())
        record_event(host, event_type, event_detail);
    // Outside the lock: the monitor reads the cache from other threads.
    advisor::PrintAdvisorMonitor::instance().on_telemetry(host, device_snapshot);
}

void DeviceTelemetryCache::mark_disconnected(const std::string& host)
{
    bool known = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto device = m_devices.find(host);
        if (device != m_devices.end()) {
            known = true;
            if (device->second.value("connected", false)) {
                device->second["connected"] = false;
                device->second["disconnected_at_ms"] = wall_ms();
                // Whether the state was current when the connection closed (update()).
                auto previous = m_stale.find(host);
                const bool state_current = device->second.contains("state") &&
                                           (previous == m_stale.end() || !previous->second.count("state"));
                m_disconnects[host] = {mono_ms(), state_current};
            }
            std::set<std::string>& stale = m_stale[host];
            for (const auto& item : device->second.items())
                if (item.key() != "updated_at_ms" && item.key() != "connected" && item.key() != "disconnected_at_ms" &&
                    item.key() != "stale_keys")
                    stale.insert(item.key());
            if (stale.empty())
                m_stale.erase(host);
            else
                device->second["stale_keys"] = stale;
        }
    }
    if (known)
        advisor::PrintAdvisorMonitor::instance().on_disconnected(host);
}

void DeviceTelemetryCache::record_event(const std::string& host, const std::string& type, const std::string& detail)
{
    const long long now = wall_ms();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_events.push_back({{"time_ms", now}, {"address", host}, {"type", type}, {"detail", detail}});
        while (m_events.size() > MAX_EVENTS)
            m_events.pop_front();
    }
    const std::string title = type == "print_complete" ? "Stampa completata" :
                              type == "print_failed"   ? "Stampa fallita" :
                              type == "print_aborted"  ? "Stampa interrotta" : "Errore della stampante";
    print_advisor_notify(title, host + " - " + detail);
}

nlohmann::json DeviceTelemetryCache::events() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json result = nlohmann::json::array();
    for (const auto& event : m_events)
        result.push_back(event);
    return result;
}

long long DeviceTelemetryCache::age_ms(const std::string& host) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_updated_mono.find(host);
    return it == m_updated_mono.end() ? -1 : std::max(0LL, mono_ms() - it->second);
}

nlohmann::json DeviceTelemetryCache::snapshot() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    nlohmann::json result = nlohmann::json::object();
    for (const auto& [host, fields] : m_devices)
        result[host] = fields;
    return result;
}

} // namespace GUI
} // namespace Slic3r
