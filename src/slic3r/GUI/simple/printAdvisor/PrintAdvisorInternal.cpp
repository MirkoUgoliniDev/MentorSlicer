#include "PrintAdvisorInternal.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/print_manage/App/DeviceTelemetryCache.h"
#include "slic3r/GUI/print_manage/App/PrintAdvisorMonitor.hpp"
#include "slic3r/GUI/print_manage/App/PrinterMgrView.hpp"
#include "slic3r/GUI/print_manage/App/SendToPrinter.hpp"
#include "slic3r/GUI/print_manage/data/DataCenter.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <wx/dialog.h>
#include <wx/evtloop.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <deque>
#include <locale>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>

namespace Slic3r {
namespace GUI {
namespace advisor {

using json = nlohmann::json;

json MachineLimits::to_json() const
{
    const auto value = [](double limit) { return limit >= 0 ? json(limit) : json(nullptr); };
    return {{"max_bed_temp", value(max_bed_temp)}, {"max_nozzle_temp", value(max_nozzle_temp)},
            {"max_chamber_temp", value(max_chamber_temp)}, {"source", source}};
}

std::string normalized_model(std::string model)
{
    boost::algorithm::trim(model);
    if (model.rfind("Creality ", 0) == 0)
        model = model.substr(9);
    std::transform(model.begin(), model.end(), model.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return model;
}

double json_number(const json& value)
{
    if (value.is_number())
        return value.get<double>();
    if (value.is_string()) {
        std::istringstream stream(value.get<std::string>());
        stream.imbue(std::locale::classic());
        double number = -1;
        stream >> number;
        if (!stream.fail())
            return number;
    }
    return -1;
}

namespace {
// Known models (bed, nozzle), from the values these printers report themselves.
const std::map<std::string, std::array<double, 2>>& known_model_limits()
{
    static const std::map<std::string, std::array<double, 2>> known = {
        {"k1c", {100, 300}}, {"k1", {100, 300}}, {"k1 max", {100, 300}}, {"k1 se", {100, 300}}
    };
    return known;
}
} // namespace

bool modal_ui_active()
{
    for (wxWindowList::compatibility_iterator node = wxTopLevelWindows.GetFirst(); node; node = node->GetNext()) {
        const wxWindow* window = node->GetData();
        const auto* dialog = dynamic_cast<const wxDialog*>(window);
        if (dialog && dialog->IsShown() && dialog->IsModal())
            return true;
        // The send dialog is modeless (Plater shows it with Show()): it reads the plate, the
        // slice result and the profiles while it is open, so nothing may change underneath.
        if (dynamic_cast<const CxSentToPrinterDialog*>(window) && window->IsShown())
            return true;
    }
    // Native modal dialogs (file dialogs, message boxes) are not in wxTopLevelWindows: inside
    // them the active event loop is not the main loop.
    const wxEventLoopBase* active = wxEventLoopBase::GetActive();
    const wxEventLoopBase* main_loop = wxTheApp ? wxTheApp->GetMainLoop() : nullptr;
    return active && main_loop && active != main_loop;
}

std::string format_number(double value)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(6) << value;
    return text.str();
}

std::string truncate_utf8_bytes(const std::string& text, size_t max_bytes)
{
    if (text.size() <= max_bytes)
        return text;
    size_t end = max_bytes;
    // Step back over continuation bytes to the start of the sequence that does not fit.
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
        --end;
    return text.substr(0, end);
}

std::string data_relative_path(const boost::filesystem::path& path)
{
    const std::string root = boost::filesystem::path(data_dir()).lexically_normal().generic_string();
    const std::string full = path.lexically_normal().generic_string();
    if (!root.empty() && full.size() > root.size() + 1 && full.compare(0, root.size(), root) == 0 && full[root.size()] == '/')
        return full.substr(root.size() + 1);
    return path.filename().string();
}

MachineLimits machine_limits(const DynamicPrintConfig& printer_config)
{
    MachineLimits limits;
    std::vector<std::string> sources;
    // Each limit is the lowest known value: a report or a table can lower it, never raise it.
    const auto lower = [](double& current, double value) {
        if (std::isfinite(value) && value > 0 && (current < 0 || value < current))
            current = value;
    };
    const auto& known = known_model_limits();
    bool from_table = false;
    const auto apply_table = [&](const std::string& model) {
        const auto it = model.empty() ? known.end() : known.find(model);
        if (it == known.end())
            return;
        lower(limits.max_bed_temp, it->second[0]);
        lower(limits.max_nozzle_temp, it->second[1]);
        from_table = true;
    };
    const ConfigOption* model_option = printer_config.option("printer_model");
    apply_table(model_option ? normalized_model(model_option->serialize()) : std::string());

    // Only the printer Print Advisor acts on: never another LAN printer of the same model.
    const std::string address = target_device_address();
    if (!address.empty()) {
        const json devices = DeviceTelemetryCache::instance().snapshot();
        // An entry of a disconnected printer (connected == false) is not used.
        if (devices.contains(address) && devices[address].is_object() &&
            !(devices[address].contains("connected") && devices[address]["connected"].is_boolean() &&
              !devices[address]["connected"].get<bool>())) {
            const json& device = devices[address];
            const auto reported = [&device](const char* key) { return device.contains(key) ? json_number(device[key]) : -1.0; };
            const double bed = reported("maxBedTemp");
            const double nozzle = reported("maxNozzleTemp");
            const double chamber = reported("maxBoxTemp");
            if (bed > 0 || nozzle > 0 || chamber > 0) {
                lower(limits.max_bed_temp, bed);
                lower(limits.max_nozzle_temp, nozzle);
                lower(limits.max_chamber_temp, chamber);
                sources.push_back("printer");
            }
            if (device.contains("model") && device["model"].is_string())
                apply_table(normalized_model(device["model"].get<std::string>()));
        }
    }
    if (from_table)
        sources.push_back("known_model_table");
    if (!sources.empty()) {
        limits.source.clear();
        for (const std::string& source : sources)
            limits.source += (limits.source.empty() ? "" : "+") + source;
    }
    return limits;
}

// Append-only log of what Print Advisor changed, kept across restarts in the data directory.
boost::filesystem::path history_path()
{
    return boost::filesystem::path(data_dir()) / "print-advisor" / "history.jsonl";
}

boost::filesystem::path history_backup_path()
{
    return boost::filesystem::path(data_dir()) / "print-advisor" / "history.jsonl.1";
}

void append_history(json entry)
{
    static std::mutex mutex; // the monitor may log from another thread
    try {
        std::lock_guard<std::mutex> lock(mutex);
        boost::filesystem::create_directories(history_path().parent_path());
        entry["time_ms"] = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        // Rotate above 5 MB, keeping one backup: ownership checks read both files.
        boost::system::error_code error;
        const auto size = boost::filesystem::file_size(history_path(), error);
        if (!error && size > 5u * 1024u * 1024u) {
            boost::filesystem::remove(history_backup_path(), error);
            boost::filesystem::rename(history_path(), history_backup_path(), error);
        }
        boost::filesystem::ofstream file(history_path(), std::ios::app | std::ios::binary);
        file << entry.dump() << "\n";
    } catch (...) {
        // The log is informational; a write failure must not undo a confirmed change.
    }
}

const char* state_name(int state)
{
    switch (state) {
    case 0: return "idle";
    case 1: return "printing";
    case 2: return "complete";
    case 3: return "failed";
    case 4: return "aborted";
    case 5: return "paused";
    case 6: return "pausing";
    case 7: return "stopping";
    case 8: return "restoring";
    default: return "unknown";
    }
}

// Address of the printer Print Advisor acts on: the device manager's current device or, when
// none is set, the only LAN printer reporting the model of the selected printer profile.
std::string target_device_address()
{
    const ::DM::Device& device = ::DM::DataCenter::Ins().get_current_device_data();
    if (device.valid && !device.address.empty())
        return device.address;
    auto* bundle = wxGetApp().preset_bundle;
    const ConfigOption* model_option = bundle ? bundle->printers.get_edited_preset().config.option("printer_model") : nullptr;
    const std::string model = model_option ? normalized_model(model_option->serialize()) : std::string();
    if (model.empty())
        return {};
    std::vector<std::string> matches;
    const json devices = DeviceTelemetryCache::instance().snapshot();
    for (const auto& [host, fields] : devices.items())
        if (fields.contains("model") && fields["model"].is_string() && normalized_model(fields["model"].get<std::string>()) == model)
            matches.push_back(host);
    return matches.size() == 1 ? matches.front() : std::string();
}

// LAN telemetry of the printer Print Advisor acts on.
json current_device_telemetry()
{
    const std::string address = target_device_address();
    if (address.empty())
        return nullptr;
    const json snapshot = DeviceTelemetryCache::instance().snapshot();
    return snapshot.contains(address) ? snapshot[address] : json(nullptr);
}

// True when "state" is a value kept from a previous connection (listed in "stale_keys").
static bool state_is_stale(const json& device)
{
    if (!device.is_object())
        return false;
    auto stale = device.find("stale_keys");
    if (stale == device.end() || !stale->is_array())
        return false;
    for (const json& item : *stale)
        if (item.is_string() && item.get_ref<const std::string&>() == "state")
            return true;
    return false;
}

int telemetry_state(const json& device)
{
    if (!device.is_object() || !device.contains("state") || state_is_stale(device))
        return -1;
    const double state = json_number(device["state"]);
    return state >= 0 ? int(state) : -1;
}

DeviceSnapshot selected_device_snapshot(bool require_state)
{
    DeviceSnapshot snapshot;
    snapshot.address = target_device_address();
    if (snapshot.address.empty())
        throw std::runtime_error("No printer selected: select it in MentorSlicer, or keep only one LAN printer of this model "
                                 "connected (Device tab open)");
    const json telemetry = current_device_telemetry();
    // The cache keeps the last values after the session ends; they no longer describe the printer.
    if (telemetry.is_object() && telemetry.contains("connected") && telemetry["connected"].is_boolean() &&
        !telemetry["connected"].get<bool>())
        throw std::runtime_error("La stampante non è collegata: apri la scheda Dispositivo");
    // After a reconnection the old state is kept until the printer reports it again.
    if (require_state && state_is_stale(telemetry))
        throw std::runtime_error("Stato della stampante non aggiornato: attendi qualche secondo con la scheda Dispositivo aperta");
    snapshot.state = telemetry_state(telemetry);
    if (telemetry.is_object())
        snapshot.updated_at_ms = telemetry.value("updated_at_ms", -1LL);
    // Age from the monotonic clock: a wall-clock jump must not make old data look fresh.
    const long long age = DeviceTelemetryCache::instance().age_ms(snapshot.address);
    if (age < 0 || age > 60000)
        throw std::runtime_error("No recent data from the printer: open the Device tab in MentorSlicer and retry");
    return snapshot;
}

void check_device_unchanged(const DeviceSnapshot& before)
{
    const DeviceSnapshot after = selected_device_snapshot(before.state >= 0);
    if (after.address != before.address || after.state != before.state)
        throw std::runtime_error("The selected printer or its state changed while the dialog was open; nothing was sent");
}

namespace {
// print_control request handed to the device page, waiting for its print_control_result.
struct PendingControl {
    std::string request_id;
    std::string action;
    std::string address;
    json value; // "value" of the request (bed setpoint of set_bed_temp), or null
    long long requested_at{-1};
};

// Requests waiting for the answer of the device page and the results it sent back
// (advisor_on_print_control_result). The page answers asynchronously.
struct PrintControlTracker {
    static constexpr size_t MAX_PENDING = 16;
    std::mutex mutex;
    unsigned long long counter{0};
    std::deque<PendingControl> pending; // oldest first, at most MAX_PENDING
    json results = json::object();      // last result by action
    json last_result;
};

PrintControlTracker& print_control_tracker()
{
    static PrintControlTracker tracker;
    return tracker;
}

long long wall_clock_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

json last_print_control_result()
{
    PrintControlTracker& tracker = print_control_tracker();
    std::lock_guard<std::mutex> lock(tracker.mutex);
    return tracker.last_result;
}

json print_control_results()
{
    PrintControlTracker& tracker = print_control_tracker();
    std::lock_guard<std::mutex> lock(tracker.mutex);
    return tracker.results;
}

// print_control payload for the device manager page (pause, resume, stop, set_bed_temp).
std::string send_device_control(const std::string& expected_address, const std::string& action, const json& extra)
{
    auto* mainframe = wxGetApp().mainframe;
    auto* view = mainframe ? mainframe->get_printer_mgr_view() : nullptr;
    const std::string address = target_device_address();
    if (!view || address.empty())
        throw std::runtime_error("No printer is selected in MentorSlicer");
    if (address != expected_address)
        throw std::runtime_error("The selected printer changed; nothing was sent");
    // The device page resolves the printer by address first; send the full identity when known.
    const ::DM::Device& device = ::DM::DataCenter::Ins().get_current_device_data();
    json identity = {{"address", address}};
    if (device.valid && device.address == address)
        identity = {{"name", device.name}, {"address", device.address}, {"mac", device.mac},
                    {"tb_id", device.tbId}, {"tbId", device.tbId}, {"device_type", device.deviceType},
                    {"deviceType", device.deviceType}, {"old_printer", device.oldPrinter},
                    {"oldPrinter", device.oldPrinter}, {"model_name", device.modelName},
                    {"modelName", device.modelName}, {"online", device.online}};
    json payload = {{"action", action}, {"device", identity}};
    payload.update(extra);
    PendingControl request;
    request.action = action;
    request.address = address;
    request.value = extra.is_object() ? extra.value("value", json(nullptr)) : json(nullptr);
    request.requested_at = wall_clock_ms();
    PrintControlTracker& tracker = print_control_tracker();
    {
        std::lock_guard<std::mutex> lock(tracker.mutex);
        request.request_id = "pa-" + std::to_string(request.requested_at) + "-" + std::to_string(++tracker.counter);
        // Recorded before sending: the page echoes the id in its print_control_result.
        tracker.pending.push_back(request);
        while (tracker.pending.size() > PrintControlTracker::MAX_PENDING)
            tracker.pending.pop_front();
    }
    payload["request_id"] = request.request_id;
    if (view->request_print_control(payload))
        return request.request_id;
    std::lock_guard<std::mutex> lock(tracker.mutex);
    tracker.pending.erase(std::remove_if(tracker.pending.begin(), tracker.pending.end(),
                                         [&request](const PendingControl& item) { return item.request_id == request.request_id; }),
                          tracker.pending.end());
    return {};
}

namespace {
// Cancellation flag of the GUI request being handled (GUI thread only).
std::shared_ptr<std::atomic<bool>>& request_cancel_slot()
{
    static std::shared_ptr<std::atomic<bool>> slot;
    return slot;
}
} // namespace

std::shared_ptr<std::atomic<bool>> current_request_cancel_flag()
{
    return request_cancel_slot();
}

ScopedRequestFlag::ScopedRequestFlag(std::shared_ptr<std::atomic<bool>> cancelled)
    : m_previous(std::move(request_cancel_slot()))
{
    request_cancel_slot() = std::move(cancelled);
}

ScopedRequestFlag::~ScopedRequestFlag()
{
    request_cancel_slot() = std::move(m_previous);
}

void throw_if_request_cancelled(const std::shared_ptr<std::atomic<bool>>& cancelled)
{
    if (cancelled && cancelled->load())
        throw std::runtime_error("Richiesta scaduta: nessuna modifica applicata");
}

// Shows the MCP Advisor confirmation dialog. It runs a nested event loop, so the server may be
// stopped or destroyed meanwhile: the stop flag and the request flag are held by value.
bool ask_user(const std::string& action, const std::string& question, std::shared_ptr<std::atomic<bool>> stopping,
              const std::vector<AdvisorChangeRow>& changes, bool destructive)
{
    const std::shared_ptr<std::atomic<bool>> cancelled = current_request_cancel_flag();
    // The client may have gone (disconnect, timeout) while the handler prepared the question.
    if (stopping && stopping->load())
        throw std::runtime_error("Print Advisor is stopping");
    throw_if_request_cancelled(cancelled);
    const bool confirmed = advisor_confirm(action, question, changes, destructive);
    if (stopping && stopping->load())
        throw std::runtime_error("Print Advisor is stopping");
    throw_if_request_cancelled(cancelled);
    return confirmed;
}

bool ask_user(const std::string& action, const std::string& question, const std::atomic<bool>& stopping,
              const std::vector<AdvisorChangeRow>& changes, bool destructive)
{
    const std::shared_ptr<std::atomic<bool>> cancelled = current_request_cancel_flag();
    if (stopping.load())
        throw std::runtime_error("Print Advisor is stopping");
    throw_if_request_cancelled(cancelled);
    const bool confirmed = advisor_confirm(action, question, changes, destructive);
    if (stopping.load())
        throw std::runtime_error("Print Advisor is stopping");
    throw_if_request_cancelled(cancelled);
    return confirmed;
}

namespace {
// Length of the well-formed UTF-8 sequence starting at text[i] (RFC 3629: no overlong forms,
// no surrogates, nothing above U+10FFFF) and its code point, or 0 when it is malformed.
size_t utf8_sequence(const std::string& text, size_t i, char32_t& code_point)
{
    const auto byte = [&text](size_t at) { return static_cast<unsigned char>(text[at]); };
    const unsigned char c = byte(i);
    if (c < 0x80) {
        code_point = c;
        return 1;
    }
    size_t len = 0;
    unsigned char low = 0x80, high = 0xBF; // allowed range of the second byte
    if (c >= 0xC2 && c <= 0xDF)
        len = 2;
    else if (c >= 0xE0 && c <= 0xEF) {
        len = 3;
        if (c == 0xE0) low = 0xA0;  // overlong
        if (c == 0xED) high = 0x9F; // surrogates U+D800-U+DFFF
    } else if (c >= 0xF0 && c <= 0xF4) {
        len = 4;
        if (c == 0xF0) low = 0x90;  // overlong
        if (c == 0xF4) high = 0x8F; // above U+10FFFF
    } else
        return 0; // continuation byte, overlong lead (C0, C1) or F5-FF
    if (i + len > text.size())
        return 0;
    if (byte(i + 1) < low || byte(i + 1) > high)
        return 0;
    for (size_t k = 2; k < len; ++k)
        if ((byte(i + k) & 0xC0) != 0x80)
            return 0;
    code_point = c & (len == 2 ? 0x1F : len == 3 ? 0x0F : 0x07);
    for (size_t k = 1; k < len; ++k)
        code_point = (code_point << 6) | (byte(i + k) & 0x3F);
    return len;
}

// Control characters, line separators, bidi controls, the byte order mark and malformed UTF-8
// become single spaces; at most max_chars characters are kept. *cut is set when text was left out.
std::string sanitize_utf8(const std::string& text, size_t max_chars, bool* cut)
{
    std::string out;
    size_t chars = 0;
    size_t i = 0;
    for (; i < text.size() && chars < max_chars;) {
        char32_t cp = 0;
        size_t len = utf8_sequence(text, i, cp);
        bool blank = false;
        if (len == 0) {
            len = 1; // malformed: the byte becomes a space
            blank = true;
        } else {
            blank = cp < 0x20 || cp == 0x7F ||
                    (cp >= 0x80 && cp <= 0x9F) ||     // C1 controls
                    cp == 0x061C ||                   // Arabic letter mark
                    cp == 0x200E || cp == 0x200F ||   // left-to-right / right-to-left marks
                    (cp >= 0x2028 && cp <= 0x202E) || // line/paragraph separators, bidi embeddings
                    (cp >= 0x2066 && cp <= 0x2069) || // bidi isolates
                    cp == 0xFEFF;                     // byte order mark (zero width no-break space)
        }
        if (blank) {
            if (!out.empty() && out.back() != ' ')
                out += ' ';
        } else
            out.append(text, i, len);
        i += len;
        ++chars;
    }
    if (cut)
        *cut = i < text.size();
    boost::algorithm::trim(out);
    return out;
}
} // namespace

std::string sanitized_reason(const json& value)
{
    if (!value.is_string())
        return {};
    return sanitize_utf8(value.get_ref<const std::string&>(), 300, nullptr);
}

std::string sanitized_text(const std::string& text, size_t max_chars)
{
    bool cut = false;
    std::string out = sanitize_utf8(text, max_chars, &cut);
    if (out.empty())
        return "(senza nome)";
    return cut ? out + "..." : out;
}

std::string reason_text(const json& value)
{
    const std::string reason = sanitized_reason(value);
    return reason.empty() ? std::string() : "Motivo indicato dall'assistente: " + reason;
}

} // namespace advisor

void advisor_on_print_control_result(const nlohmann::json& result)
{
    using json = nlohmann::json;
    using advisor::print_control_tracker;
    const json payload = result.is_object() ? result : json::object();
    const bool success = payload.contains("success") && payload["success"].is_boolean() && payload["success"].get<bool>();
    // Text from the web page: kept short and free of control characters.
    const auto page_text = [&payload](const char* key, size_t max_chars) {
        return payload.contains(key) && payload[key].is_string()
            ? advisor::sanitize_utf8(payload[key].get<std::string>(), max_chars, nullptr) : std::string();
    };
    std::string action = page_text("action", 40);
    const std::string code = page_text("code", 80);
    const std::string request_id = page_text("request_id", 64);
    const std::string message = advisor::sanitized_reason(payload.value("message", json()));

    // The request this result answers: by request_id, or (no id echoed) the newest one with the
    // same action.
    advisor::PendingControl request;
    bool matched = false;
    {
        advisor::PrintControlTracker& tracker = print_control_tracker();
        std::lock_guard<std::mutex> lock(tracker.mutex);
        auto found = tracker.pending.end();
        if (!request_id.empty()) {
            found = std::find_if(tracker.pending.begin(), tracker.pending.end(),
                                 [&request_id](const advisor::PendingControl& item) { return item.request_id == request_id; });
        } else if (!action.empty()) {
            for (auto it = tracker.pending.begin(); it != tracker.pending.end(); ++it)
                if (it->action == action)
                    found = it;
        }
        if (found != tracker.pending.end()) {
            request = *found;
            tracker.pending.erase(found);
            matched = true;
            if (action.empty())
                action = request.action;
        }
    }

    json stored = {{"success", success}, {"action", action.empty() ? json(nullptr) : json(action)},
                   {"request_id", matched ? json(request.request_id) : request_id.empty() ? json(nullptr) : json(request_id)},
                   {"matched_request", matched}, {"received_at", advisor::wall_clock_ms()}};
    if (matched) {
        stored["requested_at"] = request.requested_at;
        stored["address"] = request.address;
        if (!request.value.is_null())
            stored["value"] = request.value;
    }
    if (!code.empty())
        stored["code"] = code;
    if (!message.empty())
        stored["message"] = message;
    if (action == "set_bed_temp")
        stored["tool"] = "preheat_chamber";
    else if (action == "pause" || action == "resume" || action == "stop")
        stored["tool"] = "control_print";
    {
        advisor::PrintControlTracker& tracker = print_control_tracker();
        std::lock_guard<std::mutex> lock(tracker.mutex);
        if (!action.empty())
            tracker.results[action] = stored;
        tracker.last_result = stored;
    }

    json entry = {{"action", "print_control_result"}, {"control", stored["action"]}, {"success", success},
                  {"request_id", stored["request_id"]}};
    if (matched)
        entry["address"] = request.address;
    if (!code.empty())
        entry["code"] = code;
    if (!message.empty())
        entry["message"] = message;
    advisor::append_history(std::move(entry));

    if (success) {
        BOOST_LOG_TRIVIAL(debug) << "Print Advisor: device page accepted print control " << action << " " << request_id;
        return;
    }
    BOOST_LOG_TRIVIAL(warning) << "Print Advisor: device page rejected print control " << action << " " << request_id
                               << " (" << code << "): " << message << (matched ? "" : " [no matching request]");
    print_advisor_notify("MCP Advisor",
                         "Comando alla stampante non riuscito (" + (action.empty() ? std::string("?") : action) + ")" +
                             (message.empty() ? std::string() : ": " + message));
    // A failed bed command changes what the preheat monitor must do: a switch-off that did not
    // reach the printer may leave the bed hot, a preheat that did not start must not be followed.
    if (matched && request.action == "set_bed_temp" && request.value.is_number()) {
        try {
            advisor::PrintAdvisorMonitor::instance().on_bed_command_failed(request.address, request.value.get<double>(),
                                                                            request.request_id);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Print Advisor: handling of the failed bed command failed: " << e.what();
        }
    }
}

} // namespace GUI
} // namespace Slic3r
