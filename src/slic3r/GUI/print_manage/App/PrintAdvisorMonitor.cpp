#include "PrintAdvisorMonitor.hpp"

#include "libslic3r/libslic3r.h"


#include "DeviceTelemetryCache.h"
#include "PrinterMgrView.hpp"
#include "WebSocketProxyAdvisor.hpp"
#include "slic3r/GUI/simple/printAdvisor/PrintAdvisorPageBridge.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Widgets/WebView.hpp"
#include "slic3r/GUI/print_manage/RemotePrinterManager.hpp"
#include "slic3r/GUI/simple/printAdvisor/PrintAdvisorInternal.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Utils.hpp"

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>

#include <wx/thread.h>
#include <wx/timer.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace advisor {

using json = nlohmann::json;
namespace fs = boost::filesystem;

namespace {

constexpr long long MINUTE_MS              = 60000;
constexpr int       PREHEAT_MIN_MINUTES    = 5;
constexpr int       PREHEAT_MAX_MINUTES    = 120;
constexpr long long AUTO_OFF_GRACE_MS      = 20 * MINUTE_MS; // after a plateau or the chamber target
constexpr long long AUTO_OFF_VERIFY_MS     = MINUTE_MS;      // wait for targetBedTemp0 0 before resending
constexpr int       AUTO_OFF_MAX_SENDS     = 3;
constexpr long long AUTO_OFF_IDLE_MS       = MINUTE_MS;      // idle on fresh reports this long before switching off
constexpr long long AUTO_OFF_UPLOAD_WAIT_MS = 2 * MINUTE_MS; // re-check after an upload to the printer
constexpr long long STATE_STALE_REQUEST_MS = 30000;          // stale state on an open connection: ask for it
constexpr long long STATUS_REQUEST_EVERY_MS = MINUTE_MS;
constexpr int       SHUTDOWN_FLUSH_MS      = 150;            // lets the I/O thread write the last bed-off
// Plateau: judged from minute 15 over 10 minutes. The chamber sensor has 1 C steps and a cold room
// rises only 2-3 C in 15 min, so the former 5-minute window ended preheats too early.
constexpr long long PLATEAU_AFTER_MS       = 15 * MINUTE_MS;
constexpr long long PLATEAU_WINDOW_MS      = 10 * MINUTE_MS;
constexpr long long RISE_REPORT_WINDOW_MS  = 5 * MINUTE_MS;  // rise_last_5min of the status
constexpr double    PLATEAU_MIN_RISE       = 1.0;
constexpr long long PUSH_INTERVAL_MS       = 30000;
constexpr long long BED_DROP_WINDOW_MS     = 15 * MINUTE_MS;
constexpr double    BED_DROP_C             = 15.0;
constexpr double    DETACH_BED_C           = 40.0;
constexpr long long EXHAUST_RESEND_MS      = 10000;
constexpr long long GUARD_GAP_MS           = 2 * MINUTE_MS;
constexpr double    CHAMBER_CEILING_DEFAULT = 55.0;
constexpr double    CHAMBER_CEILING_MARGIN = 5.0;
constexpr double    CHAMBER_CEILING_RESUME = 3.0;
constexpr long long LOST_AFTER_MS          = 10 * MINUTE_MS;
constexpr long long LOST_FOLLOW_MAX_MS     = 12 * 60 * MINUTE_MS; // a lost preheat or cool-down end is given up after this
constexpr long long UPLOAD_POSTPONE_MAX_MS = 15 * MINUTE_MS;  // an upload postpones the bed switch-off at most this long
constexpr long long UPLOAD_POSTPONE_NOTIFY_MS = 5 * MINUTE_MS;
constexpr int       EXHAUST_IGNORED_SENDS  = 6;               // consecutive fanCase 0 ignored before slowing down
constexpr long long EXHAUST_BACKOFF_MS     = 2 * MINUTE_MS;
constexpr long long VERDICT_FALLBACK_MS    = 30 * MINUTE_MS;  // verdict question without a bed reading
constexpr long long LOST_LOG_MATCH_MS      = 30000;           // a lost log is matched to a completion this long
constexpr long long PREHEAT_SETPOINT_SETTLE_MS = MINUTE_MS;   // a bed setpoint 0 right after arming is not trusted
constexpr long long FRESH_FOR_ACTION_MS    = 60000;
constexpr long long PREHEAT_BEFORE_PRINT   = 3 * 60 * MINUTE_MS;
constexpr int       TICK_MS                = 30000;
constexpr size_t    MAX_READINGS           = 120;
constexpr size_t    MAX_EVENTS             = 30;
constexpr size_t    MAX_PENDING_LINES      = 30;
constexpr size_t    MAX_DEVICES            = 16;
constexpr size_t    MAX_LISTED_LOGS        = 100;
constexpr size_t    MAX_LOG_FILES          = 200;
constexpr std::uintmax_t MAX_LOG_BYTES     = 20u * 1024u * 1024u;
constexpr std::uintmax_t MAX_READ_BYTES    = MAX_LOG_BYTES + 1024u * 1024u;
constexpr std::uintmax_t TAIL_BYTES        = 64u * 1024u;

// True while MentorSlicer uploads a file to the printer at `address` (a print may start right
// after it). Unknown counts as uploading.
bool upload_in_progress(const std::string& address)
{
    try {
        return RemotePrint::RemotePrinterManager::getInstance().hasPendingUpload(address);
    } catch (...) {
        return true;
    }
}

// Wall-clock time (display, logs, history) and monotonic time (intervals and timeouts).
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

struct Now
{
    long long wall;
    long long mono;
};

Now now_time() { return {wall_ms(), mono_ms()}; }

// Telemetry number, or -1 when missing or not a number (none of the fields used here is negative).
double field(const json& device, const char* key)
{
    auto it = device.find(key);
    return it == device.end() ? -1 : json_number(*it);
}

// True when `key` of a cache entry is current: the printer is connected and reported the field
// on the current connection (not a value kept from a previous one).
bool usable(const json& device, const char* key)
{
    if (!device.is_object() || !device.contains(key))
        return false;
    auto connected = device.find("connected");
    if (connected == device.end() || !connected->is_boolean() || !connected->get<bool>())
        return false;
    auto stale = device.find("stale_keys");
    if (stale != device.end() && stale->is_array())
        for (const json& item : *stale)
            if (item.is_string() && item.get_ref<const std::string&>() == key)
                return false;
    return true;
}

// Current value of `key`, or -1 when it is missing or not usable.
double live_field(const json& device, const char* key)
{
    return usable(device, key) ? field(device, key) : -1;
}

json opt_number(double value)
{
    return value >= 0 ? json(value) : json(nullptr);
}

// int of a telemetry number, or -1 when not finite or out of the int range.
int to_int(double value)
{
    if (!std::isfinite(value) || value < double(std::numeric_limits<int>::min()) || value > double(std::numeric_limits<int>::max()))
        return -1;
    return int(value);
}

bool is_printing(int state) { return state == 1 || state == 5 || state == 6; }
bool is_busy(int state) { return is_printing(state) || state == 7 || state == 8; }
bool is_ended(int state) { return state == 2 || state == 3 || state == 4; }
bool is_idle(int state) { return state == 0 || is_ended(state); }

std::string base_name(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// File name part of a print log: letters, digits, '-', '_' and single dots, without the G-code
// extension, leading or trailing dots, or a Windows reserved device name. The result can always
// be reopened by name (existing_log).
std::string sanitized(std::string name)
{
    for (const char* ext : {".gcode", ".GCODE", ".gco"}) {
        const std::string e(ext);
        if (name.size() > e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0) {
            name.resize(name.size() - e.size());
            break;
        }
    }
    std::string out;
    for (unsigned char c : name) {
        if (out.size() >= 60)
            break;
        const char next = c < 128 && (std::isalnum(c) || c == '-' || c == '_' || c == '.') ? char(c) : '_';
        if (next == '.' && !out.empty() && out.back() == '.')
            continue; // runs of dots collapse to one
        out += next;
    }
    while (!out.empty() && out.front() == '.')
        out.erase(out.begin());
    // Windows drops trailing dots and spaces from file names.
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    // Reserved device names (CON, NUL, COM1...), also followed by an extension.
    std::string device = out.substr(0, out.find('.'));
    std::transform(device.begin(), device.end(), device.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    static const std::set<std::string> reserved = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5",
                                                   "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4",
                                                   "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (reserved.count(device))
        out = "_" + out;
    return out.empty() ? std::string("print") : out;
}

std::string local_stamp(long long ms)
{
    const std::time_t time = std::time_t(ms / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d_%H%M", &tm);
    return buffer;
}

fs::path prints_dir()
{
    return fs::path(data_dir()) / "print-advisor" / "prints";
}

std::string bed_setpoint_message(int value)
{
    return json{{"method", "set"}, {"params", {{"bedTempControl", {{"num", 0}, {"val", value}}}}}}.dump();
}

std::string to_text(double value)
{
    return std::isfinite(value) ? std::to_string(std::llround(value)) : std::string("?");
}

// Print a guard is bound to: printStartTime (-1 unknown) and file name (empty unknown).
struct PrintIdentity
{
    double start{-1};
    std::string file;

    bool known() const { return start > 0 || !file.empty(); }
    bool operator==(const PrintIdentity& other) const { return start == other.start && file == other.file; }
};

PrintIdentity print_identity(const json& telemetry)
{
    PrintIdentity identity;
    const double start = live_field(telemetry, "printStartTime");
    identity.start = start > 0 ? start : -1;
    if (usable(telemetry, "printFileName"))
        if (auto it = telemetry.find("printFileName"); it->is_string())
            identity.file = base_name(it->get<std::string>());
    return identity;
}

// Cache entry of the printer at `address`, or null.
json telemetry_of(const std::string& address)
{
    const json snapshot = DeviceTelemetryCache::instance().snapshot();
    auto it = snapshot.find(address);
    return it == snapshot.end() ? json(nullptr) : *it;
}

struct Reading
{
    long long t{0};    // wall
    long long mono{0};
    double chamber{-1};
    double bed{-1};
};

// Switching the bed off after a preheat.
struct AutoOff
{
    bool pending{false};           // still to be switched off once due, if the printer is idle
    bool verifying{false};         // the command was sent: waiting for targetBedTemp0 0
    long long grace_until_mono{0}; // set after a plateau or the target: due 20 min later
    std::string grace_reason;      // idle_after_plateau | idle_after_target
    std::string reason;            // reason of the command sent: timeout | idle_after_* | stopped
    std::string outcome{"none"};   // pending | sent | switched_off_after_reconnect | confirmed | not_needed |
                                   // print_started | cancelled | replaced | stopped | stopped_not_sent |
                                   // telemetry_lost | failed
    int attempts{0};
    long long last_send_mono{0};
    long long sent_ms{0};
    int idle_reports{0};               // consecutive fresh reports of an idle printer
    long long idle_since_mono{0};      // time of the first of them
    long long postponed_until_mono{0}; // an upload to the printer was in progress
    bool print_changed{false};         // printStartTime differs from the one seen at arming
    long long postponed_since_mono{0}; // first postponement for an upload (0: none)
    bool postpone_notified{false};
    bool upload_ignored_logged{false};
    bool lost{false};                  // no telemetry for LOST_AFTER_MS: followed again when it returns
    long long lost_since_mono{0};
    bool after_reconnect{false};       // the telemetry was lost and came back
};

struct Preheat
{
    bool armed_ever{false};
    bool active{false};
    std::string address;
    long long started_ms{0};
    long long started_mono{0};
    long long ended_ms{0};
    int max_minutes{60};
    double baseline_chamber{-1};
    double bed_target{-1};
    double chamber_target{-1};
    double chamber{-1};
    double bed{-1};
    double armed_print_start{-1}; // printStartTime when armed (or first seen after), -1 unknown
    bool setpoint_seen{false};    // a report after arming showed a bed setpoint above 0
    std::string request_id;       // bed command of the device page that armed or cancelled it
    std::string outcome{"none"};
    std::vector<Reading> readings;
    AutoOff off;
};

struct PrintLog
{
    bool open{false};
    std::string name;
    fs::path path;
    long long started_ms{0};
    long long started_mono{0};
    bool joined{false};
    bool header_written{false};
    json header;
    std::vector<std::string> pending;
    long long last_point_mono{0};
    double start_chamber{-1};
    double max_chamber{-1};
    double min_chamber{-1};
    int exhaust_reenables{0};
    int runouts{0};
    bool bed_drop{false};
    double max_bed_target{-1};
    json soak_seconds{nullptr};
    std::string file;
};

struct Device
{
    int state{-1};
    bool connected{false};
    long long last_update_ms{0};
    long long last_update_mono{0};
    json last;
    PrintLog log;
    double prev_material_status{-1};
    int prev_err_key{-1};
    long long last_runout_mono{0};
    bool wait_detach{false};
    json last_complete{nullptr}; // last print that completed with an open log: for the verdict question
    long long complete_mono{0};  // when the last completion was seen
    json lost_log{nullptr};      // last log closed as "lost": adopted if the completion is seen later
    long long lost_log_until_mono{0}; // a completion was seen: the lost log is matched until then
    bool exhaust_on{false};
    json last_push;
    long long last_push_mono{0};
    long long state_stale_since_mono{0}; // connected, but "state" not reported again (0: current)
    long long last_status_request_mono{0};
};

struct Guard
{
    bool armed{false};
    std::string address;
    long long armed_ms{0};
    long long armed_mono{0};
    int reenables{0};
    int sends{0};
    json last{nullptr};
    long long last_send_mono{0};
    std::string ended;
    PrintIdentity print;
    double ceiling{CHAMBER_CEILING_DEFAULT}; // no fanCase 0 while the chamber is at or above it
    bool at_ceiling{false};
    bool ceiling_logged{false};
    int ignored_sends{0};        // fanCase 0 sent while the exhaust stayed on
    bool backoff_logged{false};
};

struct Cooldown
{
    bool active{false};
    std::string address;
    int from{0};
    int to{0};
    int step{10};
    int minutes{5};
    long long started_mono{0};
    int next_setpoint{-1};
    long long next_ms{0}; // wall, for display
    long long next_mono{0};
    int last_sent{-1};
    int prev_sent{-1};  // setpoint sent before last_sent (-1: none)
    long long last_sent_ms{0};
    int failed_sends{0};
    bool resent{false}; // the current setpoint was sent again after a mismatch
    std::string outcome{"none"};
};

// Final setpoint of a cool-down that ended early on lost telemetry (or on a replacement by a
// cool-down of another printer), sent when that printer reports again and is idle. One per
// printer (Impl::cooldown_ends, keyed by address).
struct CooldownEnd
{
    int to{0};
    int last_sent{-1};
    long long since_mono{0};
};

// How end_cooldown treats the bed: leave it at the last setpoint, send the final setpoint `to`
// now when the printer is idle, or now or else once its telemetry returns.
enum class CooldownFinal { Leave, Now, NowOrOnReturn };

enum class FinalResult { Sent, NotNeeded, External, Wait };

struct LogLine
{
    fs::path path;
    std::string text;
    bool summary{false}; // written even past the size cap; closes the log
};

// Side effects collected under the state lock and run after it.
struct Actions
{
    std::vector<LogLine> lines;
    std::vector<std::pair<std::string, std::string>> notifications;
    std::vector<json> history;
    std::vector<json> pushes;
    std::vector<std::pair<std::string, std::string>> context_requests; // address, log name
    std::vector<json> verdict_requests; // request_print_verdict() arguments
    bool prune{false};
};

// The Print Advisor is stopping: history is written directly, notifications are shown even if
// Print Advisor was just switched off, and nothing is pushed to the device page.
enum class RunMode { Normal, Stopping };

// Profiles selected in MentorSlicer when a print starts. GUI thread.
json read_print_context()
{
    json context = {{"filament", nullptr}, {"process", nullptr}, {"printer", nullptr}, {"soak_seconds", nullptr},
                    {"soak_approximate", nullptr}};
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return context;
    context["printer"] = bundle->printers.get_edited_preset().name;
    context["process"] = bundle->prints.get_edited_preset().name;
    json filaments = json::array();
    for (const std::string& name : bundle->filament_presets)
        filaments.push_back(name);
    context["filament"] = filaments.size() == 1 ? filaments.front() : filaments;

    // Same rule as the G-code: the longest heat_soak_time among the filaments the current plate
    // uses, clamped to 0-60 min. Without the plate's filaments, the first slot (approximate).
    std::set<size_t> used;
    if (Plater* plater = wxGetApp().plater())
        if (PartPlate* plate = plater->get_partplate_list().get_curr_plate())
            for (int id : plate->get_extruders(true))
                if (id >= 1 && size_t(id) <= bundle->filament_presets.size())
                    used.insert(size_t(id - 1));
    const bool approximate = used.empty();
    if (approximate && !bundle->filament_presets.empty())
        used.insert(0);
    const Preset& edited = bundle->filaments.get_edited_preset();
    int soak_minutes = 0;
    bool found = false;
    for (size_t index : used) {
        const std::string& name = bundle->filament_presets[index];
        // With its unsaved changes when it is the edited filament.
        const DynamicPrintConfig* config = nullptr;
        if (name == edited.name)
            config = &edited.config;
        else if (const Preset* preset = bundle->filaments.find_preset(name))
            config = &preset->config;
        if (!config)
            continue;
        if (const auto* soak = config->option<ConfigOptionInts>("heat_soak_time"); soak && !soak->values.empty()) {
            soak_minutes = std::max(soak_minutes, soak->values.front());
            found = true;
        }
    }
    if (found) {
        context["soak_seconds"] = std::clamp(soak_minutes, 0, 60) * 60;
        context["soak_approximate"] = approximate;
    }
    return context;
}

// Removes the oldest print logs beyond MAX_LOG_FILES, never an open one. Names start with the
// local start time, so the name order is the age order. Caller holds the file lock.
void prune_logs(const std::set<std::string>& open_names)
{
    std::vector<fs::path> logs;
    boost::system::error_code ec;
    for (fs::directory_iterator it(prints_dir(), ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".jsonl" && fs::is_regular_file(it->path(), ec))
            logs.push_back(it->path());
    if (logs.size() <= MAX_LOG_FILES)
        return;
    std::sort(logs.begin(), logs.end(), [](const fs::path& a, const fs::path& b) { return a.filename().string() < b.filename().string(); });
    size_t excess = logs.size() - MAX_LOG_FILES;
    for (const fs::path& path : logs) {
        if (excess == 0)
            break;
        if (open_names.count(path.filename().string()))
            continue;
        boost::system::error_code remove_ec;
        fs::remove(path, remove_ec);
        --excess;
    }
}

} // namespace

struct PrintAdvisorMonitor::Impl
{
    mutable std::mutex          mutex;      // state below
    std::mutex                  file_mutex; // appends to print logs; taken after `mutex`, never before
    std::set<std::string>       truncated_logs; // logs past MAX_LOG_BYTES; guarded by file_mutex
    std::atomic<bool>           enabled{false}; // the Print Advisor server is running (set_server_running)
    std::map<std::string, Device> devices;
    Preheat                     preheat;
    std::map<std::string, json> baselines;
    Guard                       guard;
    Cooldown                    cooldown;
    std::map<std::string, CooldownEnd> cooldown_ends; // pending final setpoints, by address
    std::deque<json>            events;

    // --- helpers, called with `mutex` held ---------------------------------------------------

    void add_event(Actions& actions, const std::string& address, const std::string& type, json detail,
                   const std::string& title = {}, const std::string& message = {}, bool history = true)
    {
        json event = {{"time_ms", wall_ms()}, {"address", address}, {"type", type}};
        if (detail.is_object())
            event.update(detail);
        events.push_back(event);
        while (events.size() > MAX_EVENTS)
            events.pop_front();
        if (history) {
            json entry = {{"action", "monitor_event"}, {"event", type}, {"address", address}};
            if (detail.is_object())
                entry["detail"] = detail;
            actions.history.push_back(entry);
        }
        if (!title.empty())
            actions.notifications.emplace_back(title, message);
    }

    void log_line(Actions& actions, Device& device, const json& line)
    {
        PrintLog& log = device.log;
        if (!log.open)
            return;
        if (log.header_written)
            actions.lines.push_back({log.path, line.dump(), false});
        else if (log.pending.size() < MAX_PENDING_LINES)
            log.pending.push_back(line.dump());
    }

    void flush_header(Actions& actions, PrintLog& log)
    {
        if (log.header_written)
            return;
        actions.lines.push_back({log.path, log.header.dump(), false});
        for (std::string& line : log.pending)
            actions.lines.push_back({log.path, std::move(line), false});
        log.pending.clear();
        log.header_written = true;
    }

    void start_log(Actions& actions, const std::string& address, Device& device, const json& telemetry, const Now& now, bool joined)
    {
        PrintLog log;
        log.open = true;
        log.started_ms = now.wall;
        log.started_mono = now.mono;
        log.joined = joined;
        if (auto it = telemetry.find("printFileName"); it != telemetry.end() && it->is_string())
            log.file = base_name(it->get<std::string>());

        // Unique name, also among the logs still waiting for their header.
        const std::string stem = local_stamp(now.wall) + "_" + sanitized(log.file);
        std::set<std::string> taken;
        for (const auto& [host, other] : devices)
            if (other.log.open)
                taken.insert(other.log.name);
        for (int i = 1; i < 100; ++i) {
            const std::string name = stem + (i == 1 ? std::string() : "_" + std::to_string(i)) + ".jsonl";
            boost::system::error_code ec;
            if (!taken.count(name) && !fs::exists(prints_dir() / name, ec)) {
                log.name = name;
                break;
            }
        }
        if (log.name.empty())
            return;
        log.path = prints_dir() / log.name;
        const double print_start = field(telemetry, "printStartTime");
        log.header = {{"header", true}, {"file", log.file.empty() ? json(nullptr) : json(log.file)}, {"address", address},
                      {"started_ms", now.wall}, {"print_start_s", opt_number(print_start > 0 ? print_start : -1)},
                      {"joined_mid_print", joined}, {"filament", nullptr}, {"process", nullptr}, {"printer", nullptr},
                      {"profiles_note", "Profiles selected in MentorSlicer when the log started; the print may come from another source"}};
        device.log = std::move(log);
        device.lost_log = nullptr; // a new print: an earlier lost log is no longer completed
        device.lost_log_until_mono = 0;
        actions.context_requests.emplace_back(address, device.log.name);
        actions.prune = true;
        add_event(actions, address, "print_log_started", {{"log", device.log.name}, {"joined_mid_print", joined}}, {}, {}, false);
    }

    void close_log(Actions& actions, const std::string& address, Device& device, const std::string& result, const Now& now)
    {
        PrintLog& log = device.log;
        if (!log.open)
            return;
        flush_header(actions, log);
        const json summary = {{"summary", true}, {"result", result}, {"ended_ms", now.wall},
                              {"duration_s", (now.mono - log.started_mono) / 1000}, {"joined_mid_print", log.joined},
                              {"max_chamber", opt_number(log.max_chamber)}, {"min_chamber_after_start", opt_number(log.min_chamber)},
                              {"start_chamber", opt_number(log.start_chamber)}, {"exhaust_reenables", log.exhaust_reenables},
                              {"bed_drop", log.bed_drop}, {"runout", log.runouts}};
        actions.lines.push_back({log.path, summary.dump(), true});
        add_event(actions, address, "print_log_closed", {{"log", log.name}, {"result", result}}, {}, {}, false);
        // Kept for the verdict question, asked once the bed has cooled (bed_cooled below).
        const json info = {{"log", log.name}, {"file", log.file.empty() ? json(nullptr) : json(log.file)}, {"address", address},
                           {"started_ms", log.started_ms}, {"ended_ms", now.wall}, {"printer", log.header.value("printer", json(nullptr))},
                           {"filament", log.header.value("filament", json(nullptr))}, {"process", log.header.value("process", json(nullptr))}};
        device.last_complete = result == "complete" ? info : json(nullptr);
        // A log lost to a telemetry gap is adopted if the printer later reports this print as
        // completed (adopt_lost_log).
        device.lost_log = nullptr;
        device.lost_log_until_mono = 0;
        if (result == "lost") {
            device.lost_log = info;
            device.lost_log["path"] = log.path.string();
            device.lost_log["print_start_s"] = log.header.value("print_start_s", json(nullptr));
        }
        log = PrintLog();
    }

    // The printer reported a completed print after a log was closed as "lost" (lost_log_until_mono
    // set). The log is adopted only when the print is identified as the same one on current
    // (non-stale) data: same start time, else same file name. While the current identity is not
    // known yet (right after a reconnection) the match is retried on the following reports until
    // LOST_LOG_MATCH_MS; an unknown identity is never accepted. When adopted, a second summary
    // marks the log complete and the verdict question is asked for it.
    void adopt_lost_log(Actions& actions, Device& device, const json& telemetry, const Now& now)
    {
        if (device.lost_log_until_mono == 0)
            return;
        if (!device.lost_log.is_object() || device.last_complete.is_object() || now.mono > device.lost_log_until_mono) {
            device.lost_log = nullptr;
            device.lost_log_until_mono = 0;
            return;
        }
        const json& pending = device.lost_log;
        const PrintIdentity current = print_identity(telemetry);
        const double lost_start = pending.value("print_start_s", json(nullptr)).is_number() ? pending["print_start_s"].get<double>() : -1;
        const std::string lost_file = pending.value("file", json(nullptr)).is_string() ? pending["file"].get<std::string>() : std::string();
        bool same = false;
        if (lost_start > 0 && current.start > 0)
            same = lost_start == current.start;
        else if (!lost_file.empty() && !current.file.empty())
            same = lost_file == current.file;
        else if ((lost_start > 0 || !lost_file.empty()) && (current.start > 0 || !current.file.empty()))
            same = false; // both known but not comparable (start time on one side, file on the other)
        else
            return; // not known yet: retry on the next report
        json lost = std::move(device.lost_log);
        device.lost_log = nullptr;
        device.lost_log_until_mono = 0;
        if (!same)
            return;
        const fs::path path = fs::path(lost.value("path", std::string()));
        if (!path.empty())
            actions.lines.push_back({path, json{{"summary", true}, {"result", "complete"}, {"ended_ms", now.wall},
                                                {"completion_seen_after_loss", true}}.dump(), true});
        lost.erase("path");
        lost.erase("print_start_s");
        lost["ended_ms"] = now.wall;
        device.last_complete = std::move(lost);
    }

    // Chamber rise against the newest reading at least `window_ms` old; NaN without such a reading.
    double rise_over(long long now_mono, long long window_ms) const
    {
        const Reading* reference = nullptr;
        for (const Reading& reading : preheat.readings)
            if (reading.mono <= now_mono - window_ms && reading.chamber >= 0)
                reference = &reading;
        if (!reference || preheat.chamber < 0)
            return std::nan("");
        return preheat.chamber - reference->chamber;
    }

    // --- preheat and bed auto-off ----------------------------------------------------------

    long long timeout_mono() const { return preheat.started_mono + preheat.max_minutes * MINUTE_MS; }

    long long due_mono() const
    {
        const AutoOff& off = preheat.off;
        return off.grace_until_mono > 0 ? std::min(off.grace_until_mono, timeout_mono()) : timeout_mono();
    }

    // Reason of the switch-off when it becomes due.
    std::string due_reason() const
    {
        const AutoOff& off = preheat.off;
        return off.grace_until_mono > 0 && off.grace_until_mono <= timeout_mono() ? off.grace_reason : std::string("timeout");
    }

    bool follows_preheat() const { return preheat.active || preheat.off.pending || preheat.off.verifying; }

    // True when the preheated printer reports, on fresh data received after arming, a bed
    // setpoint of 0. Right after arming the device page may not have applied the preheat yet, so
    // a 0 counts only once a report showed a setpoint above 0 or PREHEAT_SETPOINT_SETTLE_MS passed.
    bool preheat_bed_cleared(const Now& now) const
    {
        auto it = devices.find(preheat.address);
        if (it == devices.end() || preheat.off.lost)
            return false;
        const Device& device = it->second;
        return device.connected && now.mono - device.last_update_mono <= FRESH_FOR_ACTION_MS &&
               device.last_update_mono > preheat.started_mono &&
               (preheat.setpoint_seen || now.mono - preheat.started_mono >= PREHEAT_SETPOINT_SETTLE_MS) &&
               live_field(device.last, "targetBedTemp0") == 0;
    }

    // Refusal of a new preheat on `address` while the preheat of another printer is followed and
    // its bed may still be hot (running, switch-off pending, also with lost telemetry, or sent but
    // not confirmed): the monitor follows one preheat at a time, and replacing it would leave that
    // bed on. Not a conflict when that printer reports its bed setpoint at 0 (preheat_bed_cleared).
    // Empty when the preheat may start.
    std::string preheat_conflict_locked(const std::string& address, const Now& now) const
    {
        const AutoOff& off = preheat.off;
        if (preheat.address.empty() || preheat.address == address || !(preheat.active || off.pending || off.verifying))
            return {};
        if (preheat_bed_cleared(now))
            return {};
        std::string message = "Print Advisor sta ancora seguendo il preriscaldamento di " + preheat.address +
                              " e il suo piatto potrebbe essere caldo: segue un solo preriscaldamento alla volta. "
                              "Prima annullalo (seleziona " + preheat.address +
                              " in MentorSlicer e chiedi il preriscaldamento con bed_temp 0), poi riprova.";
        if (off.lost)
            message += " Se " + preheat.address + " non è raggiungibile, spegni il piatto dalla stampante e disattiva e "
                       "riattiva Print Advisor (Integrazione AI → Print Advisor MCP).";
        return message;
    }

    // Fresh, current report of an idle printer (within FRESH_FOR_ACTION_MS) to which MentorSlicer
    // is not uploading a file.
    bool idle_now(const std::string& address, const Now& now) const
    {
        auto it = devices.find(address);
        if (it == devices.end())
            return false;
        const Device& device = it->second;
        return device.connected && usable(device.last, "state") && is_idle(device.state) &&
               now.mono - device.last_update_mono <= FRESH_FOR_ACTION_MS && !upload_in_progress(address);
    }

    std::string auto_off_text(const std::string& reason) const
    {
        if (reason == "idle_after_target")
            return "camera a temperatura da 20 min e nessuna stampa avviata su " + preheat.address + ".";
        if (reason == "idle_after_plateau")
            return "camera stabile da 20 min e nessuna stampa avviata su " + preheat.address + ".";
        if (reason == "stopped")
            return "Print Advisor disattivato durante il preriscaldamento di " + preheat.address + ".";
        if (reason == "switch_off_failed")
            return "lo spegnimento dalla pagina Dispositivo non è riuscito su " + preheat.address +
                   ", quindi Print Advisor lo ha inviato direttamente.";
        return "nessuna stampa dopo " + std::to_string(preheat.max_minutes) + " min di preriscaldamento su " + preheat.address + ".";
    }

    void end_preheat(Actions& actions, const std::string& outcome, const Now& now, bool notify)
    {
        if (!preheat.active)
            return;
        preheat.active = false;
        preheat.outcome = outcome;
        preheat.ended_ms = now.wall;
        const long long minutes = (now.mono - preheat.started_mono) / MINUTE_MS;
        const std::string chamber = preheat.chamber >= 0 ? to_text(preheat.chamber) + " °C" : std::string("n/d");
        std::string title, message;
        if (notify) {
            if (outcome == "target") {
                title = "Camera a temperatura";
                message = "Camera a " + chamber + " (obiettivo " + to_text(preheat.chamber_target) + " °C) dopo " +
                          std::to_string(minutes) + " min: puoi avviare la stampa. Il piatto resta caldo; se entro 20 min "
                          "non parte una stampa, Print Advisor lo spegne.";
            } else if (outcome == "plateau") {
                title = "Camera stabile";
                message = "La camera non sale più: " + chamber + " dopo " + std::to_string(minutes) +
                          " min. Puoi avviare la stampa. Il piatto resta caldo; se entro 20 min non parte una stampa, "
                          "Print Advisor lo spegne.";
            } else if (outcome == "print_started") {
                title = "Preriscaldamento concluso";
                message = "La stampa è partita dopo " + std::to_string(minutes) + " min di preriscaldamento (camera " + chamber + ").";
            }
        }
        add_event(actions, preheat.address, "preheat_" + outcome,
                  {{"minutes", minutes}, {"chamber", opt_number(preheat.chamber)}, {"baseline_chamber", opt_number(preheat.baseline_chamber)},
                   {"bed_target", opt_number(preheat.bed_target)}, {"chamber_target", opt_number(preheat.chamber_target)},
                   {"max_minutes", preheat.max_minutes}},
                  title, message);
    }

    // Ends the auto-off follow-up without sending anything. A command already sent keeps its
    // outcome ("sent"); only the verification stops.
    void close_auto_off(Actions& actions, const std::string& outcome)
    {
        AutoOff& off = preheat.off;
        if (!off.pending && !off.verifying)
            return;
        const bool was_pending = off.pending;
        off.pending = false;
        off.verifying = false;
        off.lost = false;
        if (was_pending) {
            off.outcome = outcome;
            add_event(actions, preheat.address, "preheat_auto_off_" + outcome, json::object(), {}, {}, false);
        }
    }

    // Sends the bed switch-off. True when queued on an open connection.
    bool send_bed_off(Actions& actions, const std::string& reason, const Now& now, bool notify)
    {
        AutoOff& off = preheat.off;
        if (!WebSocketProxy::SendToDevice(preheat.address, bed_setpoint_message(0)))
            return false;
        ++off.attempts;
        off.last_send_mono = now.mono;
        if (!off.verifying) {
            off.pending = false;
            off.verifying = true;
            off.reason = reason;
            off.outcome = off.after_reconnect ? "switched_off_after_reconnect" : "sent";
            off.sent_ms = now.wall;
            const long long minutes = (now.mono - preheat.started_mono) / MINUTE_MS;
            actions.history.push_back({{"action", "preheat_auto_off"}, {"address", preheat.address}, {"reason", reason},
                                       {"minutes", minutes}, {"max_minutes", preheat.max_minutes},
                                       {"bed_target", opt_number(preheat.bed_target)}, {"chamber", opt_number(preheat.chamber)},
                                       {"outcome", off.outcome}});
            add_event(actions, preheat.address, "preheat_auto_off",
                      {{"reason", reason}, {"minutes", minutes}, {"after_reconnect", off.after_reconnect}},
                      notify ? std::string("Piatto spento") : std::string(),
                      "Piatto spento dal Print Advisor: " + auto_off_text(reason) +
                          (off.after_reconnect ? std::string(" (la stampante è tornata raggiungibile dopo un'interruzione dei dati)")
                                               : std::string()),
                      false);
        }
        return true;
    }

    void update_preheat(Actions& actions, const Device& device, const Now& now)
    {
        const json& telemetry = device.last;
        const double chamber = live_field(telemetry, "boxTemp");
        const double bed = live_field(telemetry, "bedTemp0");
        if (preheat.active) {
            if (chamber >= 0)
                preheat.chamber = chamber;
            if (bed >= 0)
                preheat.bed = bed;
            if (preheat.readings.empty() || now.mono - preheat.readings.back().mono >= MINUTE_MS) {
                preheat.readings.push_back({now.wall, now.mono, preheat.chamber, preheat.bed});
                if (preheat.readings.size() > MAX_READINGS)
                    preheat.readings.erase(preheat.readings.begin());
            }
        }
        if (usable(telemetry, "state") && is_busy(device.state)) {
            end_preheat(actions, "print_started", now, true);
            close_auto_off(actions, "print_started");
            return;
        }
        if (!preheat.active)
            return;
        const double rise = rise_over(now.mono, PLATEAU_WINDOW_MS);
        AutoOff& off = preheat.off;
        if (preheat.chamber_target > 0 && chamber >= 0 && chamber >= preheat.chamber_target) {
            end_preheat(actions, "target", now, true);
            off.grace_until_mono = now.mono + AUTO_OFF_GRACE_MS;
            off.grace_reason = "idle_after_target";
        } else if (now.mono - preheat.started_mono >= PLATEAU_AFTER_MS && !std::isnan(rise) && rise < PLATEAU_MIN_RISE) {
            end_preheat(actions, "plateau", now, true);
            off.grace_until_mono = now.mono + AUTO_OFF_GRACE_MS;
            off.grace_reason = "idle_after_plateau";
        }
    }

    // Switches the bed off when due and the printer is idle; verifies it and resends if needed.
    void check_auto_off(Actions& actions, const Now& now)
    {
        AutoOff& off = preheat.off;
        if (!off.pending && !off.verifying)
            return;
        auto it = devices.find(preheat.address);
        const Device* device = it == devices.end() ? nullptr : &it->second;
        // A command is sent only on a current report received within FRESH_FOR_ACTION_MS.
        const bool live = device && device->connected && usable(device->last, "state") &&
                          now.mono - device->last_update_mono <= FRESH_FOR_ACTION_MS;
        // Never switch off once a print started.
        if ((device && device->connected && usable(device->last, "state") && is_busy(device->state)) || off.print_changed) {
            end_preheat(actions, "print_started", now, true);
            close_auto_off(actions, "print_started");
            return;
        }
        // Lost telemetry (check_stale): wait until the printer reports again, then follow the
        // normal path (idle on two fresh reports over a minute, upload check).
        if (off.lost) {
            if (!live)
                return;
            off.lost = false;
            off.after_reconnect = true;
            add_event(actions, preheat.address, "preheat_telemetry_back",
                      {{"lost_minutes", (now.mono - off.lost_since_mono) / MINUTE_MS}, {"auto_off", off.pending ? "pending" : "verifying"}},
                      {}, {}, false);
        }
        if (preheat.active && now.mono >= timeout_mono())
            end_preheat(actions, "timeout", now, false); // the switch-off below notifies
        const double bed_target = device ? live_field(device->last, "targetBedTemp0") : -1;
        if (off.verifying) {
            if (bed_target == 0) {
                off.verifying = false;
                off.outcome = "confirmed";
                add_event(actions, preheat.address, "preheat_auto_off_confirmed", {{"reason", off.reason}}, {}, {}, false);
                return;
            }
            if (now.mono - off.last_send_mono < AUTO_OFF_VERIFY_MS)
                return;
            if (off.attempts >= AUTO_OFF_MAX_SENDS) {
                off.verifying = false;
                off.outcome = "failed";
                add_event(actions, preheat.address, "preheat_auto_off_failed", {{"attempts", off.attempts}},
                          "Piatto forse ancora acceso",
                          "Print Advisor ha chiesto di spegnere il piatto di " + preheat.address +
                              " ma la stampante non lo conferma: controlla e spegnilo dalla stampante.");
                return;
            }
            if (live && is_idle(device->state) && may_switch_off(actions, now))
                send_bed_off(actions, off.reason, now, true);
            return;
        }
        if (now.mono < due_mono())
            return;
        // Due: only on current data of an idle printer. Otherwise wait (a lost connection is
        // followed in check_stale, and the switch-off resumes when the printer reports again).
        if (!live || !is_idle(device->state))
            return;
        if (bed_target == 0) {
            off.pending = false;
            off.outcome = "not_needed";
            add_event(actions, preheat.address, "preheat_auto_off_not_needed", {{"reason", due_reason()}}, {}, {}, false);
            return;
        }
        if (may_switch_off(actions, now))
            send_bed_off(actions, due_reason(), now, true);
    }

    // Follows the auto-off conditions on each report of the preheated printer: consecutive
    // fresh reports of an idle printer, and a new print seen through printStartTime.
    void track_auto_off(const Device& device, const Now& now)
    {
        AutoOff& off = preheat.off;
        if (!off.pending && !off.verifying)
            return;
        if (device.connected && usable(device.last, "state") && is_idle(device.state)) {
            if (off.idle_reports++ == 0)
                off.idle_since_mono = now.mono;
        } else {
            off.idle_reports = 0;
            off.idle_since_mono = 0;
        }
        const double start = live_field(device.last, "printStartTime");
        if (start > 0) {
            if (preheat.armed_print_start <= 0)
                preheat.armed_print_start = start;
            else if (start != preheat.armed_print_start)
                off.print_changed = true;
        }
    }

    // The bed may be switched off: the printer was idle on at least two fresh reports spanning
    // AUTO_OFF_IDLE_MS, and MentorSlicer is not uploading a file to it (re-checked later). An
    // upload postpones the switch-off by at most UPLOAD_POSTPONE_MAX_MS in total (an upload that
    // never ends, or a failing check, must not keep the bed hot); the user is told after
    // UPLOAD_POSTPONE_NOTIFY_MS.
    bool may_switch_off(Actions& actions, const Now& now)
    {
        AutoOff& off = preheat.off;
        if (off.idle_reports < 2 || now.mono - off.idle_since_mono < AUTO_OFF_IDLE_MS || now.mono < off.postponed_until_mono)
            return false;
        if (upload_in_progress(preheat.address)) {
            const bool first = off.postponed_since_mono == 0;
            if (first)
                off.postponed_since_mono = now.mono;
            const long long postponed = now.mono - off.postponed_since_mono;
            if (postponed >= UPLOAD_POSTPONE_MAX_MS) {
                if (!off.upload_ignored_logged) {
                    off.upload_ignored_logged = true;
                    add_event(actions, preheat.address, "preheat_auto_off_upload_ignored",
                              {{"postponed_minutes", postponed / MINUTE_MS}}, {}, {}, false);
                }
                return true;
            }
            off.postponed_until_mono = now.mono + AUTO_OFF_UPLOAD_WAIT_MS;
            if (first)
                add_event(actions, preheat.address, "preheat_auto_off_postponed", {{"reason", "upload_in_progress"}}, {}, {}, false);
            if (postponed >= UPLOAD_POSTPONE_NOTIFY_MS && !off.postpone_notified) {
                off.postpone_notified = true;
                add_event(actions, preheat.address, "preheat_auto_off_postponed_long", {{"postponed_minutes", postponed / MINUTE_MS}},
                          "Spegnimento del piatto rimandato",
                          "MentorSlicer sta ancora inviando un file a " + preheat.address +
                              ": lo spegnimento automatico del piatto è rimandato. Se l'invio non finisce entro " +
                              std::to_string(UPLOAD_POSTPONE_MAX_MS / MINUTE_MS) +
                              " min e la stampante resta inattiva, Print Advisor spegne comunque il piatto.");
            }
            return false;
        }
        return true;
    }

    // Asks a connected printer for its full status (read-only request), at most once per
    // STATUS_REQUEST_EVERY_MS.
    void request_status(const std::string& address, Device& device, const Now& now)
    {
        if (!device.connected)
            return;
        if (device.last_status_request_mono != 0 && now.mono - device.last_status_request_mono < STATUS_REQUEST_EVERY_MS)
            return;
        device.last_status_request_mono = now.mono;
        WebSocketProxy::SendToDevice(address, json{{"method", "get"}, {"params", {{"reqPrinterPara", 1}}}}.dump());
    }

    // A connected printer whose state stayed stale after a reconnection.
    void request_status_if_stale(const std::string& address, Device& device, const Now& now)
    {
        if (device.state_stale_since_mono == 0 || now.mono - device.state_stale_since_mono < STATE_STALE_REQUEST_MS)
            return;
        request_status(address, device, now);
    }

    // --- slow cool-down and exhaust guard --------------------------------------------------

    // Sends `to`, the final setpoint of a cool-down that ended early, to an idle printer on fresh
    // data. Never heats: `to` must be below the last setpoint sent (true for every running
    // cool-down: to < from and the steps never go below `to`) and the printer must still report
    // that setpoint; a lower setpoint needs nothing, any other was set outside Print Advisor.
    FinalResult send_cooldown_final(Actions& actions, const std::string& address, int to, int last_sent, const Now& now)
    {
        if (to < 0 || last_sent < 0)
            return FinalResult::NotNeeded;
        if (!idle_now(address, now))
            return FinalResult::Wait;
        const double target = live_field(devices.at(address).last, "targetBedTemp0");
        if (target < 0)
            return FinalResult::Wait;
        if (target <= to + 1 || to >= last_sent)
            return FinalResult::NotNeeded;
        if (std::abs(target - last_sent) > 1)
            return FinalResult::External;
        if (!WebSocketProxy::SendToDevice(address, bed_setpoint_message(to)))
            return FinalResult::Wait;
        actions.history.push_back({{"action", "slow_cooldown_final_setpoint"}, {"address", address}, {"to", to}, {"from_setpoint", last_sent}});
        return FinalResult::Sent;
    }

    // Stops an active slow cool-down before it reached `to`. With `final_mode` other than Leave,
    // the final setpoint `to` is sent now when the printer is idle on fresh data, or (NowOrOnReturn)
    // once it reports again (cooldown_ends). Otherwise the bed keeps the last setpoint sent: when
    // `notify` and the bed is still above DETACH_BED_C, the user is told so. True when `to` was
    // sent now.
    bool end_cooldown(Actions& actions, const std::string& outcome, bool notify, const Now& now,
                      CooldownFinal final_mode = CooldownFinal::Leave)
    {
        if (!cooldown.active)
            return false;
        cooldown.active = false;
        cooldown.outcome = outcome;
        double bed = -1;
        if (auto it = devices.find(cooldown.address); it != devices.end())
            bed = field(it->second.last, "bedTemp0");
        FinalResult final_result = FinalResult::NotNeeded;
        if (final_mode != CooldownFinal::Leave)
            final_result = send_cooldown_final(actions, cooldown.address, cooldown.to, cooldown.last_sent, now);
        const bool on_return = final_mode == CooldownFinal::NowOrOnReturn && final_result == FinalResult::Wait;
        // One pending final setpoint per printer: a newer one of the same printer replaces it.
        if (on_return)
            cooldown_ends[cooldown.address] = {cooldown.to, cooldown.last_sent, now.mono};
        const std::string final_text = cooldown.to == 0 ? std::string("ha spento il piatto")
                                                        : "ha portato il piatto al valore finale di " + std::to_string(cooldown.to) + " °C";
        std::string title, message;
        if (notify) {
            title = outcome == "replaced" ? "Raffreddamento lento sostituito" : "Raffreddamento lento interrotto";
            const std::string head = outcome == "replaced"
                ? "Il raffreddamento lento di " + cooldown.address + " è stato sostituito da una nuova richiesta"
                : "Il raffreddamento lento di " + cooldown.address + " si è fermato";
            if (final_mode != CooldownFinal::Leave && final_result == FinalResult::Sent)
                message = head + ": Print Advisor " + final_text + ".";
            else if (on_return)
                message = head + " (nessun dato recente dalla stampante): il piatto resta impostato a " +
                          std::to_string(cooldown.last_sent) + " °C. Print Advisor " +
                          (cooldown.to == 0 ? std::string("lo spegnerà") : "lo porterà a " + std::to_string(cooldown.to) + " °C") +
                          " quando la stampante tornerà a inviare dati e sarà inattiva.";
            else if (outcome == "replaced")
                message = head + (cooldown.last_sent > 0 ? " (ultimo valore inviato " + std::to_string(cooldown.last_sent) + " °C)." : std::string("."));
            else if (bed > DETACH_BED_C && cooldown.last_sent > 0 &&
                     (final_mode == CooldownFinal::Leave || final_result == FinalResult::Wait))
                message = "Il piatto di " + cooldown.address + " è a " + to_text(bed) + " °C e resta impostato a " +
                          std::to_string(cooldown.last_sent) + " °C (ultimo valore inviato). Spegnilo dalla stampante se serve.";
            // Without a bed reading the bed may still hold the last setpoint: say so.
            else if (bed < 0 && cooldown.last_sent > cooldown.to &&
                     (final_mode == CooldownFinal::Leave || final_result == FinalResult::Wait))
                message = "Nessun dato recente dal piatto di " + cooldown.address + ": potrebbe essere ancora impostato a " +
                          std::to_string(cooldown.last_sent) + " °C (ultimo valore inviato). Controlla la stampante.";
            if (message.empty())
                title.clear();
        }
        add_event(actions, cooldown.address, "slow_cooldown_cancelled",
                  {{"reason", outcome}, {"bed", opt_number(bed)}, {"last_sent", cooldown.last_sent},
                   {"final_setpoint", final_mode == CooldownFinal::Leave ? json(nullptr)
                                      : final_result == FinalResult::Sent ? json("sent")
                                      : on_return ? json("on_return")
                                      : final_result == FinalResult::NotNeeded ? json("not_needed")
                                      : final_result == FinalResult::External ? json("external_change") : json("not_sent")}},
                  title, message);
        return final_result == FinalResult::Sent;
    }

    // Final setpoint of a cool-down that ended on lost telemetry, once the printer reports again:
    // dropped when a print started (it sets the bed itself), after LOST_FOLLOW_MAX_MS, or when
    // the setpoint was changed outside Print Advisor.
    void follow_cooldown_end(Actions& actions, const std::string& address, const Device& device, const Now& now)
    {
        auto found = cooldown_ends.find(address);
        if (found == cooldown_ends.end())
            return;
        if (usable(device.last, "state") && is_busy(device.state)) {
            cooldown_ends.erase(found);
            add_event(actions, address, "slow_cooldown_final_dropped", {{"reason", "print_started"}}, {}, {}, false);
            return;
        }
        const FinalResult result = send_cooldown_final(actions, address, found->second.to, found->second.last_sent, now);
        if (result == FinalResult::Wait)
            return;
        const int to = found->second.to;
        cooldown_ends.erase(found);
        if (result == FinalResult::Sent)
            add_event(actions, address, "slow_cooldown_final_sent", {{"to", to}}, "Raffreddamento lento concluso",
                      "La stampante " + address + " è tornata raggiungibile: Print Advisor " +
                          (to == 0 ? std::string("ha spento il piatto.") : "ha portato il piatto a " + std::to_string(to) + " °C."));
        else
            add_event(actions, address, "slow_cooldown_final_dropped",
                      {{"reason", result == FinalResult::External ? "external_change" : "not_needed"}}, {}, {}, false);
    }

    void end_guard(Actions& actions, const std::string& reason, bool notify)
    {
        if (!guard.armed)
            return;
        guard.armed = false;
        guard.ended = reason;
        std::string title, message;
        if (notify && reason == "replaced") {
            title = "Guardia ventola sostituita";
            message = "La guardia della ventola di estrazione di " + guard.address + " è stata sostituita da una nuova richiesta.";
        } else if (notify && reason == "telemetry_gap") {
            title = "Guardia ventola disattivata";
            message = "Nessun dato da " + guard.address + " da oltre 2 minuti: la guardia della ventola di estrazione si è fermata.";
        }
        add_event(actions, guard.address, "exhaust_guard_disarmed", {{"reason", reason}, {"reenables", guard.reenables}}, title, message);
    }

    // Automations of printers that stopped reporting, print logs of silent printers, and the
    // time-based preheat checks. Called before new telemetry is applied, so a gap is measured
    // against the previous update; then without the auto-off, which waits for the new data, and
    // without the periodic status requests (timer only).
    void check_stale(Actions& actions, const Now& now, bool auto_off = true)
    {
        auto last_update = [this](const std::string& address) {
            auto it = devices.find(address);
            return it == devices.end() ? 0LL : it->second.last_update_mono;
        };
        for (auto& [address, device] : devices) {
            if (device.log.open && now.mono - device.last_update_mono > LOST_AFTER_MS)
                close_log(actions, address, device, "lost", now);
            request_status_if_stale(address, device, now);
            // Verdict question without a bed reading: asked VERDICT_FALLBACK_MS after the completion.
            if (device.wait_detach && device.last_complete.is_object() && device.complete_mono > 0 &&
                now.mono - device.complete_mono >= VERDICT_FALLBACK_MS && live_field(device.last, "bedTemp0") < 0) {
                device.wait_detach = false;
                device.last_complete["bed"] = nullptr;
                actions.verdict_requests.push_back(std::move(device.last_complete));
                device.last_complete = nullptr;
            }
        }
        // An idle printer may report nothing for minutes: while an automation follows it, its
        // status is requested periodically (from the timer, not while a report is being applied)
        // so that silence really means a lost printer.
        std::set<std::string> followed;
        if (auto_off) {
            if (follows_preheat())
                followed.insert(preheat.address);
            if (cooldown.active)
                followed.insert(cooldown.address);
            for (const auto& [end_address, end] : cooldown_ends)
                followed.insert(end_address);
            if (guard.armed)
                followed.insert(guard.address);
        }
        for (const std::string& address : followed)
            if (auto it = devices.find(address); it != devices.end() && now.mono - it->second.last_update_mono >= STATUS_REQUEST_EVERY_MS / 2)
                request_status(address, it->second, now);

        if (cooldown.active && now.mono - std::max(cooldown.started_mono, last_update(cooldown.address)) > LOST_AFTER_MS)
            end_cooldown(actions, "telemetry_lost", true, now, CooldownFinal::NowOrOnReturn);
        for (auto it = cooldown_ends.begin(); it != cooldown_ends.end();) {
            if (now.mono - it->second.since_mono > LOST_FOLLOW_MAX_MS) {
                add_event(actions, it->first, "slow_cooldown_final_dropped", {{"reason", "telemetry_lost"}}, {}, {});
                it = cooldown_ends.erase(it);
            } else
                ++it;
        }
        if (guard.armed && now.mono - std::max(guard.armed_mono, last_update(guard.address)) > GUARD_GAP_MS)
            end_guard(actions, "telemetry_gap", true);
        // Lost telemetry: the preheat ends, but the switch-off stays pending (off.lost) and runs
        // when the printer reports again idle (check_auto_off), for at most LOST_FOLLOW_MAX_MS.
        AutoOff& off = preheat.off;
        if (follows_preheat() && !off.lost && now.mono - std::max(preheat.started_mono, last_update(preheat.address)) > LOST_AFTER_MS) {
            end_preheat(actions, "telemetry_lost", now, false);
            off.lost = true;
            off.lost_since_mono = now.mono;
            off.idle_reports = 0;
            off.idle_since_mono = 0;
            const bool sent = off.verifying;
            add_event(actions, preheat.address, "preheat_telemetry_lost", {{"auto_off", sent ? "sent" : "pending"}},
                      "Preriscaldamento: stampante non raggiungibile",
                      "Nessun dato da " + preheat.address + " da oltre 10 minuti: il piatto potrebbe essere ancora caldo. "
                      "Controlla la stampante. Se torna raggiungibile ed è inattiva, Print Advisor " +
                          (sent ? std::string("ricontrolla lo spegnimento del piatto") : std::string("spegne il piatto")) + ".");
        }
        if (off.lost && now.mono - off.lost_since_mono > LOST_FOLLOW_MAX_MS) {
            const bool sent = off.verifying;
            off.lost = false;
            off.pending = false;
            off.verifying = false;
            if (!sent)
                off.outcome = "telemetry_lost";
            add_event(actions, preheat.address, "preheat_follow_up_abandoned", {{"auto_off", off.outcome}},
                      "Preriscaldamento non più seguito",
                      "Nessun dato da " + preheat.address + " da oltre " + std::to_string(LOST_FOLLOW_MAX_MS / (60 * MINUTE_MS)) +
                          " ore: Print Advisor smette di seguire il preriscaldamento. Controlla il piatto dalla stampante.");
        }
        if (auto_off)
            check_auto_off(actions, now);
    }

    json push_message(const std::string& address, const Device& device) const
    {
        const PrintLog& log = device.log;
        const bool preheat_shown = preheat.armed_ever && preheat.address == address && preheat.outcome != "cancelled" &&
            (preheat.active || (log.open && !log.joined && preheat.started_mono <= log.started_mono &&
                                log.started_mono - preheat.started_mono < PREHEAT_BEFORE_PRINT));
        json message = {{"command", "advisor_chamber"}, {"address", address}, {"state", device.state}};
        message["preheat_started_ms"] = preheat_shown ? json(preheat.started_ms) : json(nullptr);
        message["preheat_bed"] = preheat_shown ? opt_number(preheat.bed_target) : json(nullptr);
        // Chamber target of the running preheat, shown next to its timer (web patch 6).
        message["preheat_chamber_target"] = preheat_shown && preheat.active && preheat.chamber_target > 0
            ? json(preheat.chamber_target) : json(nullptr);
        // Preheat still followed for this printer (running, or bed kept hot until the automatic
        // switch-off): the chamber-card button offers "Stop" (web patch 17).
        message["preheat_followed"] = preheat.address == address && follows_preheat();
        // The K1C repeats boxTemp only now and then, so the page may never have seen it: the
        // chamber card of web patch 1 shows this value instead.
        const double chamber = field(device.last, "boxTemp");
        message["chamber"] = chamber >= 0 ? json(chamber) : json(nullptr);
        message["soak_seconds"] = log.open ? log.soak_seconds : json(nullptr);
        const double print_start = field(device.last, "printStartTime");
        message["print_start_s"] = log.open && is_printing(device.state) && print_start > 0 ? json(print_start) : json(nullptr);
        message["file"] = log.open && !log.file.empty() ? json(log.file) : json(nullptr);
        return message;
    }

    void maybe_push(Actions& actions, const std::string& address, Device& device, const Now& now, bool force = false)
    {
        const json message = push_message(address, device);
        const bool active = (preheat.active && preheat.address == address) || device.log.open;
        if (force || message != device.last_push || (active && now.mono - device.last_push_mono >= PUSH_INTERVAL_MS)) {
            device.last_push = message;
            device.last_push_mono = now.mono;
            actions.pushes.push_back(message);
        }
    }

    json guard_status() const
    {
        return {{"armed", guard.armed}, {"address", guard.address.empty() ? json(nullptr) : json(guard.address)},
                {"armed_ms", guard.armed_ms > 0 ? json(guard.armed_ms) : json(nullptr)}, {"reenables", guard.reenables},
                {"sends", guard.sends}, {"last", guard.last}, {"ended", guard.ended.empty() ? json(nullptr) : json(guard.ended)},
                {"chamber_ceiling", guard.ceiling}, {"at_ceiling", guard.at_ceiling},
                {"ignored_sends", guard.ignored_sends}, {"slowed_down", guard.ignored_sends >= EXHAUST_IGNORED_SENDS},
                {"print_start_s", opt_number(guard.print.start)},
                {"print_file", guard.print.file.empty() ? json(nullptr) : json(guard.print.file)}};
    }

    json cooldown_status() const
    {
        const Cooldown& c = cooldown;
        return {{"active", c.active}, {"address", c.address.empty() ? json(nullptr) : json(c.address)},
                {"from", c.from}, {"to", c.to}, {"step_c", c.step}, {"minutes_per_step", c.minutes},
                {"next_setpoint", c.active ? json(c.next_setpoint) : json(nullptr)},
                {"next_at_ms", c.active ? json(c.next_ms) : json(nullptr)},
                {"last_sent", c.last_sent >= 0 ? json(c.last_sent) : json(nullptr)},
                {"last_sent_ms", c.last_sent_ms > 0 ? json(c.last_sent_ms) : json(nullptr)},
                {"failed_sends", c.failed_sends}, {"outcome", c.outcome},
                {"final_setpoint_pending", final_setpoints_pending()}};
    }

    // Pending final setpoints of cool-downs that ended early: [{address, to}], or null when none.
    json final_setpoints_pending() const
    {
        if (cooldown_ends.empty())
            return nullptr;
        json pending = json::array();
        for (const auto& [address, end] : cooldown_ends)
            pending.push_back({{"address", address}, {"to", end.to}});
        return pending;
    }

    json preheat_status_locked(const Now& now) const
    {
        json readings = json::array();
        for (const Reading& reading : preheat.readings)
            readings.push_back({{"t", reading.t}, {"chamber", opt_number(reading.chamber)}, {"bed", opt_number(reading.bed)}});
        const double rise = preheat.active ? rise_over(now.mono, RISE_REPORT_WINDOW_MS) : std::nan("");
        const bool any = preheat.armed_ever;
        const AutoOff& off = preheat.off;
        const auto to_wall = [&now](long long mono) { return now.wall + (mono - now.mono); };
        json auto_off = nullptr;
        if (any)
            auto_off = {{"pending", off.pending || off.verifying},
                        {"reason", off.pending ? json(due_reason()) : off.reason.empty() ? json(nullptr) : json(off.reason)},
                        {"due_at_ms", off.pending ? json(to_wall(due_mono())) : json(nullptr)},
                        {"outcome", off.outcome}, {"attempts", off.attempts},
                        {"sent_ms", off.sent_ms > 0 ? json(off.sent_ms) : json(nullptr)},
                        {"confirmed", off.outcome == "confirmed"}, {"telemetry_lost", off.lost},
                        {"after_reconnect", off.after_reconnect}};
        return {{"active", preheat.active}, {"address", any ? json(preheat.address) : json(nullptr)},
                {"started_ms", any ? json(preheat.started_ms) : json(nullptr)},
                {"ended_ms", any && !preheat.active ? json(preheat.ended_ms) : json(nullptr)},
                {"max_minutes", any ? json(preheat.max_minutes) : json(nullptr)},
                {"timeout_at_ms", any ? json(preheat.started_ms + preheat.max_minutes * MINUTE_MS) : json(nullptr)},
                {"baseline_chamber", any ? opt_number(preheat.baseline_chamber) : json(nullptr)},
                {"bed_target", any ? opt_number(preheat.bed_target) : json(nullptr)},
                {"chamber_target", any ? opt_number(preheat.chamber_target) : json(nullptr)},
                {"chamber", any ? opt_number(preheat.chamber) : json(nullptr)}, {"bed", any ? opt_number(preheat.bed) : json(nullptr)},
                {"rise_last_5min", std::isnan(rise) ? json(nullptr) : json(std::round(rise * 10) / 10)},
                {"outcome", preheat.active ? std::string("running") : preheat.outcome}, {"readings", readings},
                {"auto_off", auto_off}};
    }

    // --- side effects ------------------------------------------------------------------------

    // Appends the log lines in order. A log past MAX_LOG_BYTES gets one truncation marker and
    // no further lines except its summary. Caller holds file_mutex.
    void write_lines(const std::vector<LogLine>& lines)
    {
        for (const LogLine& item : lines) {
            try {
                const std::string key = item.path.string();
                if (!item.summary && truncated_logs.count(key))
                    continue;
                fs::create_directories(item.path.parent_path());
                boost::system::error_code ec;
                std::uintmax_t size = fs::file_size(item.path, ec);
                if (ec)
                    size = 0;
                fs::ofstream file(item.path, std::ios::app | std::ios::binary);
                if (!item.summary && size >= MAX_LOG_BYTES) {
                    file << json{{"event", "log_truncated"}, {"t", wall_ms()}, {"max_bytes", MAX_LOG_BYTES}}.dump() << "\n";
                    truncated_logs.insert(key);
                    continue;
                }
                file << item.text << "\n";
                if (item.summary)
                    truncated_logs.erase(key);
            } catch (...) {
                // The print log is informational.
            }
        }
    }

    // Writes the log lines in order (the file lock is taken before the state lock is released),
    // then queues notifications, history entries, page pushes and context requests.
    void run(Actions& actions, std::unique_lock<std::mutex>& state_lock, RunMode mode = RunMode::Normal)
    {
        std::set<std::string> open_names;
        if (actions.prune)
            for (const auto& [address, device] : devices)
                if (device.log.open)
                    open_names.insert(device.log.name);
        {
            std::lock_guard<std::mutex> file_lock(file_mutex);
            state_lock.unlock();
            write_lines(actions.lines);
            if (actions.prune) {
                try {
                    prune_logs(open_names);
                } catch (...) {
                }
            }
        }
        if (mode == RunMode::Stopping) {
            for (json& entry : actions.history)
                append_history(std::move(entry));
            // The notification is queued on the GUI thread, which may not run it at exit: the
            // log keeps the warning.
            for (const auto& [title, message] : actions.notifications) {
                BOOST_LOG_TRIVIAL(warning) << "Print Advisor: " << title << ": " << message;
                print_advisor_notify(title, message, true);
            }
            return;
        }
        if (!wxTheApp)
            return;
        for (const auto& [title, message] : actions.notifications)
            print_advisor_notify(title, message);
        // History is written even if Print Advisor stopped meanwhile: it records what was done.
        if (!actions.history.empty())
            wxTheApp->CallAfter([history = std::move(actions.history)]() {
                for (const json& entry : history)
                    append_history(entry);
            });
        if (!actions.pushes.empty())
            wxTheApp->CallAfter([pushes = std::move(actions.pushes)]() {
                if (!PrintAdvisorMonitor::instance().impl().enabled.load())
                    return;
                for (const json& message : pushes)
                    advisor_push_device_page(message);
            });
        for (const auto& [address, name] : actions.context_requests)
            wxTheApp->CallAfter([address = address, name = name]() {
                Impl& impl = PrintAdvisorMonitor::instance().impl();
                if (impl.enabled.load())
                    impl.apply_context(address, name, read_print_context());
            });
        for (const json& request : actions.verdict_requests)
            request_print_verdict(request);
    }

    void apply_context(const std::string& address, const std::string& name, const json& context)
    {
        std::unique_lock<std::mutex> lock(mutex);
        if (!enabled.load())
            return;
        auto it = devices.find(address);
        if (it == devices.end() || !it->second.log.open || it->second.log.name != name)
            return;
        Actions actions;
        PrintLog& log = it->second.log;
        for (const char* key : {"filament", "process", "printer"})
            log.header[key] = context.value(key, json(nullptr));
        // The soak time matters only for a print whose start was seen.
        log.soak_seconds = log.joined ? json(nullptr) : context.value("soak_seconds", json(nullptr));
        log.header["soak_seconds"] = log.soak_seconds;
        log.header["soak_approximate"] = log.joined ? json(nullptr) : context.value("soak_approximate", json(nullptr));
        flush_header(actions, log);
        maybe_push(actions, address, it->second, now_time(), true);
        run(actions, lock);
    }

    void tick()
    {
        if (!enabled.load())
            return;
        std::unique_lock<std::mutex> lock(mutex);
        if (!enabled.load())
            return;
        Actions actions;
        check_stale(actions, now_time());
        run(actions, lock);
    }

    void on_disconnected(const std::string& address)
    {
        if (!enabled.load())
            return;
        std::lock_guard<std::mutex> lock(mutex);
        if (auto it = devices.find(address); it != devices.end()) {
            it->second.connected = false;
            it->second.state_stale_since_mono = 0;
        }
        if (preheat.address == address) {
            preheat.off.idle_reports = 0;
            preheat.off.idle_since_mono = 0;
        }
    }

    // --- telemetry -----------------------------------------------------------------------------

    void on_telemetry(const std::string& address, const json& telemetry)
    {
        if (!enabled.load() || !telemetry.is_object())
            return;

        std::unique_lock<std::mutex> lock(mutex);
        if (!enabled.load()) // the server stopped meanwhile (disarm_all)
            return;
        const Now now = now_time();
        Actions actions;
        // Gaps are measured before this update: what depended on a printer that went silent
        // ends before its new (merged, possibly partial) data is used.
        check_stale(actions, now, false);

        auto found = devices.find(address);
        if (found == devices.end()) {
            if (devices.size() >= MAX_DEVICES) {
                run(actions, lock);
                return;
            }
            found = devices.emplace(address, Device()).first;
        }
        Device& device = found->second;

        const int previous = device.state;
        const int reported = usable(telemetry, "state") ? telemetry_state(telemetry) : -1;
        const int state = reported >= 0 ? reported : previous;
        device.state = state;
        device.connected = telemetry.value("connected", false);
        device.last = telemetry;
        device.last_update_ms = now.wall;
        device.last_update_mono = now.mono;
        if (device.connected && !usable(telemetry, "state")) {
            if (device.state_stale_since_mono == 0)
                device.state_stale_since_mono = now.mono;
        } else
            device.state_stale_since_mono = 0;
        request_status_if_stale(address, device, now);

        const double chamber = live_field(telemetry, "boxTemp");
        const double bed = live_field(telemetry, "bedTemp0");
        const double bed_target = live_field(telemetry, "targetBedTemp0");
        const double layer = field(telemetry, "layer");

        // Print log: open on a print start (or when a print is already running), close at the end.
        if (device.log.open && (is_ended(state) || state == 0))
            close_log(actions, address, device, state == 2 ? "complete" : state == 3 ? "failed" : state == 4 ? "aborted" : "lost", now);
        if (is_printing(state) && !device.log.open)
            start_log(actions, address, device, telemetry, now, !(state == 1 && previous >= 0 && !is_busy(previous)));

        if (state != previous) {
            if (state == 2 && previous >= 0) {
                device.wait_detach = true;
                device.complete_mono = now.mono;
                // A log lost to a gap is matched to this completion on this and the next reports.
                if (device.lost_log.is_object())
                    device.lost_log_until_mono = now.mono + LOST_LOG_MATCH_MS;
            }
            if (is_busy(state))
                device.wait_detach = false;
            if (guard.armed && guard.address == address && is_busy(previous) && (is_ended(state) || state == 0))
                end_guard(actions, "print_end", false);
            if (cooldown.active && cooldown.address == address && is_busy(state))
                end_cooldown(actions, "print_started", false, now); // the print sets the bed
        }

        PrintLog& log = device.log;
        if (log.open) {
            if (chamber >= 0) {
                if (log.start_chamber < 0)
                    log.start_chamber = chamber;
                log.max_chamber = std::max(log.max_chamber, chamber);
                log.min_chamber = log.min_chamber < 0 ? chamber : std::min(log.min_chamber, chamber);
            }
            if (is_printing(state) && now.mono - log.last_point_mono >= MINUTE_MS) {
                log.last_point_mono = now.mono;
                log_line(actions, device,
                         {{"t", now.wall}, {"layer", opt_number(layer)}, {"total_layer", opt_number(field(telemetry, "TotalLayer"))},
                          {"progress", opt_number(field(telemetry, "printProgress"))}, {"nozzle", opt_number(field(telemetry, "nozzleTemp"))},
                          {"nozzle_target", opt_number(field(telemetry, "targetNozzleTemp"))}, {"bed", opt_number(bed)},
                          {"bed_target", opt_number(bed_target)}, {"chamber", opt_number(chamber)},
                          {"fan_part", opt_number(field(telemetry, "modelFanPct"))}, {"fan_side", opt_number(field(telemetry, "auxiliaryFanPct"))},
                          {"fan_exhaust", opt_number(field(telemetry, "caseFanPct"))}, {"feedrate", opt_number(field(telemetry, "curFeedratePct"))},
                          {"state", state}});
            }
            // Bed setpoint lowered after the heat soak (e.g. by START_PRINT), in the first minutes.
            if (!log.joined && is_printing(state) && now.mono - log.started_mono <= BED_DROP_WINDOW_MS && bed_target >= 0) {
                if (bed_target > log.max_bed_target)
                    log.max_bed_target = bed_target;
                else if (!log.bed_drop && bed_target > 0 && bed_target < log.max_bed_target - BED_DROP_C) {
                    log.bed_drop = true;
                    const json detail = {{"from", log.max_bed_target}, {"to", bed_target}, {"layer", opt_number(layer)},
                                         {"minutes", (now.mono - log.started_mono) / MINUTE_MS}};
                    json line = detail;
                    line["event"] = "bed_drop_after_soak";
                    line["t"] = now.wall;
                    log_line(actions, device, line);
                    add_event(actions, address, "bed_drop_after_soak", detail, "Piatto abbassato dopo il soak",
                              "La stampante ha portato il piatto da " + to_text(log.max_bed_target) + " a " + to_text(bed_target) +
                                  " °C all'inizio della stampa: il soak potrebbe essere vanificato.");
                }
            }
        }

        // Filament runout.
        const double material_status = live_field(telemetry, "materialStatus");
        int err_key = -1;
        if (auto err = telemetry.find("err"); usable(telemetry, "err") && err->is_object() && err->contains("key"))
            err_key = to_int(json_number((*err)["key"]));
        if (is_printing(state)) {
            const bool runout = (device.prev_material_status == 0 && material_status == 1) ||
                                (err_key == 2839 && device.prev_err_key != 2839);
            if (runout && (device.last_runout_mono == 0 || now.mono - device.last_runout_mono > MINUTE_MS)) {
                device.last_runout_mono = now.mono;
                if (log.open)
                    ++log.runouts;
                const json detail = {{"layer", opt_number(layer)}, {"progress", opt_number(field(telemetry, "printProgress"))}};
                log_line(actions, device, json{{"event", "filament_runout"}, {"t", now.wall}, {"layer", opt_number(layer)}});
                add_event(actions, address, "filament_runout", detail, "Filamento esaurito",
                          "Filamento esaurito: cambia la bobina e riprendi");
            }
        }
        if (material_status >= 0)
            device.prev_material_status = material_status;
        device.prev_err_key = err_key;

        // Part ready to be removed once the bed has cooled after a completed print.
        if (device.wait_detach && !is_busy(state) && bed >= 0 && bed < DETACH_BED_C) {
            device.wait_detach = false;
            add_event(actions, address, "bed_cooled", {{"bed", bed}}, "Pezzo staccabile",
                      "Il piatto è sceso a " + to_text(bed) + " °C: puoi staccare il pezzo.", false);
            // Completed print with a log: ask the user how it turned out (PrintAdvisorVerdict.cpp).
            if (device.last_complete.is_object()) {
                device.last_complete["bed"] = bed;
                actions.verdict_requests.push_back(std::move(device.last_complete));
                device.last_complete = nullptr;
            }
        }

        // Exhaust guard: only for the print it was armed for.
        if (guard.armed && guard.address == address) {
            const PrintIdentity current = print_identity(telemetry);
            if ((guard.print.start > 0 && current.start > 0 && current.start != guard.print.start) ||
                (!guard.print.file.empty() && !current.file.empty() && current.file != guard.print.file))
                end_guard(actions, "new_print", false);
        }
        // Exhaust guard: the firmware switches the exhaust on at about 37 °C in the chamber. It
        // acts only on current data, and stands down at the chamber ceiling (the exhaust is then
        // left as the firmware wants it) until the chamber is CHAMBER_CEILING_RESUME below it.
        if (guard.armed && guard.address == address && state == 1 && usable(telemetry, "state")) {
            const bool fan_known = usable(telemetry, "fanCase") || usable(telemetry, "caseFanPct");
            const bool on = live_field(telemetry, "fanCase") == 1 || live_field(telemetry, "caseFanPct") > 0;
            if (chamber >= guard.ceiling) {
                if (!guard.at_ceiling) {
                    guard.at_ceiling = true;
                    if (!guard.ceiling_logged) {
                        guard.ceiling_logged = true;
                        const json detail = {{"chamber", chamber}, {"ceiling", guard.ceiling}, {"layer", opt_number(layer)}};
                        json line = detail;
                        line["event"] = "ceiling_reached";
                        line["t"] = now.wall;
                        log_line(actions, device, line);
                        add_event(actions, address, "ceiling_reached", detail, "Guardia ventola in pausa",
                                  "Camera a " + to_text(chamber) + " °C (limite " + to_text(guard.ceiling) +
                                      " °C): Print Advisor non spegne più l'estrazione finché la camera non scende sotto " +
                                      to_text(guard.ceiling - CHAMBER_CEILING_RESUME) + " °C.");
                    }
                }
            } else if (guard.at_ceiling && chamber >= 0 && chamber < guard.ceiling - CHAMBER_CEILING_RESUME)
                guard.at_ceiling = false;
            if (on) {
                if (!device.exhaust_on) {
                    device.exhaust_on = true;
                    ++guard.reenables;
                    if (log.open)
                        ++log.exhaust_reenables;
                    const json detail = {{"chamber", opt_number(chamber)}, {"layer", opt_number(layer)},
                                         {"case_fan_pct", opt_number(live_field(telemetry, "caseFanPct"))}};
                    guard.last = detail;
                    guard.last["t"] = now.wall;
                    json line = detail;
                    line["event"] = "exhaust_reenabled";
                    line["t"] = now.wall;
                    log_line(actions, device, line);
                    add_event(actions, address, "exhaust_reenabled", detail);
                }
                // Without a chamber reading the ceiling cannot be checked: nothing is sent. Some
                // firmwares (K1C) ignore fanCase 0 while the chamber is above their threshold:
                // after EXHAUST_IGNORED_SENDS ignored sends the guard resends only every
                // EXHAUST_BACKOFF_MS, until the exhaust really goes off.
                const long long interval = guard.ignored_sends >= EXHAUST_IGNORED_SENDS ? EXHAUST_BACKOFF_MS : EXHAUST_RESEND_MS;
                if (!guard.at_ceiling && chamber >= 0 &&
                    (guard.last_send_mono == 0 || now.mono - guard.last_send_mono >= interval)) {
                    guard.last_send_mono = now.mono;
                    if (WebSocketProxy::SendToDevice(address, json{{"method", "set"}, {"params", {{"fanCase", 0}}}}.dump())) {
                        ++guard.sends;
                        ++guard.ignored_sends;
                    }
                    if (guard.ignored_sends == EXHAUST_IGNORED_SENDS && !guard.backoff_logged) {
                        guard.backoff_logged = true;
                        const json detail = {{"chamber", chamber}, {"ignored_sends", guard.ignored_sends}, {"layer", opt_number(layer)}};
                        json line = detail;
                        line["event"] = "exhaust_guard_slowed_down";
                        line["t"] = now.wall;
                        log_line(actions, device, line);
                        add_event(actions, address, "exhaust_guard_slowed_down", detail, "Guardia ventola rallentata",
                                  "La stampante " + address + " ignora lo spegnimento della ventola di estrazione (camera a " +
                                      to_text(chamber) + " °C): Print Advisor ora riprova solo ogni " +
                                      std::to_string(EXHAUST_BACKOFF_MS / MINUTE_MS) + " minuti.");
                    }
                }
            } else if (fan_known) {
                device.exhaust_on = false;
                guard.ignored_sends = 0; // the exhaust went off: back to the normal rate
            }
        }

        // Slow cool-down: next bed setpoint when its time has come, on current data only.
        // The printer must still have the last setpoint sent; otherwise someone else changed it.
        // Without a reported setpoint the step waits. The last step sends `to` (0 switches the bed off).
        if (cooldown.active && cooldown.address == address && usable(telemetry, "state") && !is_busy(state) &&
            now.mono >= cooldown.next_mono && bed_target >= 0) {
            const bool mismatch = std::abs(bed_target - cooldown.last_sent) > 1;
            // A step the printer did not apply leaves the setpoint sent before it: then, once, the
            // current one is sent again (lower than what the printer reports, so it never heats)
            // and held for a full step. Any other value (a deliberate change, a lower setpoint) or
            // a second mismatch is an external change.
            if (mismatch && !cooldown.resent && cooldown.prev_sent > cooldown.last_sent &&
                std::abs(bed_target - cooldown.prev_sent) <= 1) {
                if (WebSocketProxy::SendToDevice(address, bed_setpoint_message(cooldown.last_sent))) {
                    cooldown.resent = true;
                    cooldown.last_sent_ms = now.wall;
                    cooldown.next_mono = now.mono + cooldown.minutes * MINUTE_MS;
                    cooldown.next_ms = now.wall + cooldown.minutes * MINUTE_MS;
                    add_event(actions, address, "slow_cooldown_resent", {{"bed_target", bed_target}, {"setpoint", cooldown.last_sent}},
                              {}, {}, false);
                } else
                    ++cooldown.failed_sends;
            } else if (mismatch) {
                cooldown.active = false;
                cooldown.outcome = "external_change";
                add_event(actions, address, "slow_cooldown_cancelled",
                          {{"reason", "external_change"}, {"bed_target", bed_target}, {"last_sent", cooldown.last_sent}},
                          "Raffreddamento lento annullato",
                          "Il piatto è stato impostato a " + to_text(bed_target) + " °C fuori da Print Advisor (ultimo valore inviato " +
                              std::to_string(cooldown.last_sent) + " °C): MentorSlicer non invia altri valori.");
            } else if (WebSocketProxy::SendToDevice(address, bed_setpoint_message(cooldown.next_setpoint))) {
                cooldown.resent = false;
                cooldown.prev_sent = cooldown.last_sent;
                cooldown.last_sent = cooldown.next_setpoint;
                cooldown.last_sent_ms = now.wall;
                if (cooldown.next_setpoint <= cooldown.to) {
                    cooldown.active = false;
                    cooldown.outcome = "done";
                    add_event(actions, address, "slow_cooldown_done", {{"to", cooldown.to}}, "Raffreddamento lento finito",
                              cooldown.to == 0 ? std::string("Raffreddamento lento finito: piatto spento")
                                               : "Raffreddamento lento finito: piatto a " + std::to_string(cooldown.to) + " °C");
                } else {
                    cooldown.next_setpoint = std::max(cooldown.to, cooldown.next_setpoint - cooldown.step);
                    cooldown.next_mono = now.mono + cooldown.minutes * MINUTE_MS;
                    cooldown.next_ms = now.wall + cooldown.minutes * MINUTE_MS;
                }
            } else
                ++cooldown.failed_sends;
        }

        adopt_lost_log(actions, device, telemetry, now);
        follow_cooldown_end(actions, address, device, now);
        if (preheat.address == address && live_field(telemetry, "targetBedTemp0") > 0)
            preheat.setpoint_seen = true;
        if (preheat.address == address)
            track_auto_off(device, now);
        if (preheat.address == address && follows_preheat())
            update_preheat(actions, device, now);
        check_auto_off(actions, now);

        maybe_push(actions, address, device, now);
        run(actions, lock);
    }
};

namespace {

// Runs the time-based checks every TICK_MS while the server runs, even without telemetry.
// The timer lives on the GUI thread and, like the monitor, is never destroyed.
void set_tick_timer(bool run)
{
    auto apply = [run]() {
        static wxTimer* timer = nullptr;
        if (!timer) {
            if (!run)
                return;
            timer = new wxTimer();
            timer->Bind(wxEVT_TIMER, [](wxTimerEvent&) { PrintAdvisorMonitor::instance().impl().tick(); });
        }
        if (run && !timer->IsRunning())
            timer->Start(TICK_MS);
        else if (!run && timer->IsRunning())
            timer->Stop();
    };
    if (wxThread::IsMain())
        apply();
    else if (wxTheApp)
        wxTheApp->CallAfter(apply);
}

} // namespace

PrintAdvisorMonitor& PrintAdvisorMonitor::instance()
{
    // Never destroyed: the proxy thread may still deliver telemetry while the process exits.
    static PrintAdvisorMonitor* monitor = new PrintAdvisorMonitor();
    return *monitor;
}

PrintAdvisorMonitor::PrintAdvisorMonitor() : m_impl(std::make_unique<Impl>()) {}

PrintAdvisorMonitor::~PrintAdvisorMonitor() = default;

void PrintAdvisorMonitor::on_telemetry(const std::string& address, const nlohmann::json& device)
{
    m_impl->on_telemetry(address, device);
}

void PrintAdvisorMonitor::on_disconnected(const std::string& address)
{
    m_impl->on_disconnected(address);
}

void PrintAdvisorMonitor::arm_preheat(const std::string& address, double bed_target, double chamber_target, int max_minutes,
                                      const std::string& request_id)
{
    const json telemetry = telemetry_of(address);
    const double chamber = live_field(telemetry, "boxTemp");
    const double bed = live_field(telemetry, "bedTemp0");

    Impl& impl = *m_impl;
    std::unique_lock<std::mutex> lock(impl.mutex);
    Actions actions;
    const Now now = now_time();
    Preheat& current = impl.preheat;
    if (bed_target > 0) {
        // Refused before the dialog by the server (preheat_conflict); checked again here because
        // following this preheat would drop the follow-up of a bed that may still be hot.
        if (!impl.enabled.load())
            throw std::runtime_error("Print Advisor si sta fermando: il preriscaldamento di " + address +
                                     " non è seguito. Spegni il piatto dalla stampante se serve.");
        if (const std::string conflict = impl.preheat_conflict_locked(address, now); !conflict.empty())
            throw std::runtime_error("Il piatto di " + address + " ha ricevuto il comando ma il preriscaldamento non è seguito "
                                     "(spegnilo dalla stampante se serve). " + conflict);
    }
    if (bed_target <= 0) {
        // The request switched the bed off itself: nothing left to switch off.
        if (current.address == address && impl.follows_preheat()) {
            impl.end_preheat(actions, "cancelled", now, false);
            impl.close_auto_off(actions, "cancelled");
        }
        if (current.address == address)
            current.request_id = request_id;
    } else {
        // Same printer, or another one that reports its bed setpoint at 0 (preheat_conflict_locked).
        if (impl.follows_preheat()) {
            const std::string previous = current.address;
            impl.end_preheat(actions, "replaced", now, false);
            impl.close_auto_off(actions, "replaced");
            if (previous != address)
                impl.add_event(actions, previous, "preheat_not_followed", {{"replaced_by", address}},
                               "Preriscaldamento non più seguito",
                               "Il preriscaldamento di " + previous + " non è più seguito da Print Advisor (nuovo preriscaldamento su " +
                                   address + "). La stampante riportava il piatto spento: controllala se serve.");
        }
        // A slow cool-down of the same printer, or its pending final setpoint, would fight the
        // new setpoint.
        if (impl.cooldown.active && impl.cooldown.address == address)
            impl.end_cooldown(actions, "replaced", true, now);
        impl.cooldown_ends.erase(address);
        Preheat preheat;
        preheat.armed_ever = true;
        preheat.active = true;
        preheat.address = address;
        preheat.started_ms = now.wall;
        preheat.started_mono = now.mono;
        preheat.max_minutes = std::clamp(max_minutes, PREHEAT_MIN_MINUTES, PREHEAT_MAX_MINUTES);
        preheat.baseline_chamber = chamber;
        preheat.bed_target = bed_target;
        preheat.chamber_target = chamber_target > 0 ? chamber_target : -1;
        preheat.chamber = chamber;
        preheat.bed = bed;
        preheat.armed_print_start = live_field(telemetry, "printStartTime");
        preheat.request_id = request_id;
        preheat.outcome = "running";
        preheat.readings.push_back({now.wall, now.mono, chamber, bed});
        preheat.off.pending = true;
        preheat.off.outcome = "pending";
        current = std::move(preheat);
        impl.baselines[address] = {{"chamber", opt_number(chamber)}, {"at_ms", now.wall}};
        impl.add_event(actions, address, "preheat_armed",
                       {{"bed_target", bed_target}, {"chamber_target", opt_number(current.chamber_target)},
                        {"baseline_chamber", opt_number(chamber)}, {"max_minutes", current.max_minutes}}, {}, {}, false);
    }
    if (auto it = impl.devices.find(address); it != impl.devices.end())
        impl.maybe_push(actions, address, it->second, now, true);
    impl.run(actions, lock);
}

void PrintAdvisorMonitor::on_bed_command_failed(const std::string& address, double bed_value, const std::string& request_id)
{
    Impl& impl = *m_impl;
    std::unique_lock<std::mutex> lock(impl.mutex);
    Actions actions;
    const Now now = now_time();
    Preheat& preheat = impl.preheat;

    if (bed_value > 0) {
        // Only the preheat armed by this request: a later one on the same printer is not touched.
        if (preheat.address != address || preheat.request_id != request_id || !impl.follows_preheat()) {
            impl.add_event(actions, address, "preheat_command_failed", {{"request_id", request_id}, {"followed", false}});
            impl.run(actions, lock);
            return;
        }
        // The printer may still report a setpoint above 0 (set before, or applied after all):
        // then the automatic switch-off keeps following the bed.
        auto it = impl.devices.find(address);
        const Device* device = it == impl.devices.end() ? nullptr : &it->second;
        const bool bed_set = device && device->connected && now.mono - device->last_update_mono <= FRESH_FOR_ACTION_MS &&
                             live_field(device->last, "targetBedTemp0") > 0;
        impl.end_preheat(actions, "command_failed", now, false);
        if (!bed_set)
            impl.close_auto_off(actions, "command_failed");
        impl.add_event(actions, address, "preheat_command_failed",
                       {{"request_id", request_id}, {"followed", true}, {"auto_off_kept", bed_set}}, {},
                       {}, true);
        if (device)
            impl.maybe_push(actions, address, it->second, now, true);
        impl.run(actions, lock);
        return;
    }

    // Switch-off failed. A preheat of this printer that is still followed (armed again after the
    // failed request) switches the bed off by itself.
    if (impl.follows_preheat() && preheat.address == address) {
        impl.add_event(actions, address, "bed_off_command_failed", {{"request_id", request_id}, {"handled_by", "preheat_follow_up"}});
        impl.run(actions, lock);
        return;
    }
    // Monitor stopped, or following the preheat of another printer: one direct attempt.
    if (!impl.enabled.load() || impl.follows_preheat()) {
        const bool sent = impl.idle_now(address, now) && WebSocketProxy::SendToDevice(address, bed_setpoint_message(0));
        actions.history.push_back({{"action", "bed_off_retry"}, {"address", address}, {"request_id", request_id}, {"sent", sent}});
        impl.add_event(actions, address, "bed_off_command_failed", {{"request_id", request_id}, {"direct_retry", sent}},
                       "Piatto forse ancora acceso",
                       sent ? "Lo spegnimento del piatto di " + address + " dalla pagina Dispositivo non è riuscito: Print Advisor "
                              "lo ha inviato direttamente. Controlla la stampante."
                            : "Lo spegnimento del piatto di " + address + " non è riuscito: spegnilo dalla stampante.",
                       false);
        impl.run(actions, lock);
        return;
    }
    // Follow the switch-off like an automatic one: due now, verified, resent, and kept pending
    // while the printer is unreachable; dropped if a print starts (the print needs the bed).
    // A new follow-up: the timeout of an earlier preheat must not become the reason.
    Preheat followed;
    followed.armed_ever = true;
    followed.address = address;
    followed.started_ms = now.wall;
    followed.started_mono = now.mono;
    followed.bed_target = 0;
    followed.outcome = "cancelled";
    followed.request_id = request_id;
    preheat = std::move(followed);
    preheat.off.grace_until_mono = now.mono;
    preheat.off.grace_reason = "switch_off_failed";
    if (impl.idle_now(address, now) && impl.send_bed_off(actions, "switch_off_failed", now, true)) {
        impl.add_event(actions, address, "bed_off_command_failed", {{"request_id", request_id}, {"direct_retry", true}}, {}, {}, false);
    } else {
        preheat.off.pending = true;
        preheat.off.outcome = "pending";
        impl.add_event(actions, address, "bed_off_command_failed", {{"request_id", request_id}, {"direct_retry", false}},
                       "Piatto forse ancora acceso",
                       "Lo spegnimento del piatto di " + address + " dalla pagina Dispositivo non è riuscito. Print Advisor lo "
                       "spegne appena la stampante risulta inattiva; nel frattempo controlla la stampante.");
    }
    if (auto it = impl.devices.find(address); it != impl.devices.end())
        impl.maybe_push(actions, address, it->second, now, true);
    impl.run(actions, lock);
}

void PrintAdvisorMonitor::disarm_all()
{
    set_tick_timer(false);
    Impl& impl = *m_impl;
    std::unique_lock<std::mutex> lock(impl.mutex);
    impl.enabled = false;
    Actions actions;
    const Now now = now_time();

    // Preheat: switch the bed off when the printer is idle (best effort: the command is queued
    // on the open connection, which may itself be closing).
    Preheat& preheat = impl.preheat;
    bool bed_off_sent = false;
    if (preheat.active || preheat.off.pending) {
        auto it = impl.devices.find(preheat.address);
        const Device* device = it == impl.devices.end() ? nullptr : &it->second;
        const bool live = device && device->connected && usable(device->last, "state");
        const bool busy = (live && is_busy(device->state)) || preheat.off.print_changed;
        impl.end_preheat(actions, busy ? "print_started" : "stopped", now, false);
        // The switch-off at stop needs a fresh report (within FRESH_FOR_ACTION_MS) of an idle
        // printer received after arming, not lost. One report is enough here, unlike the automatic
        // switch-off: the user is quitting, possibly right after arming, and nobody follows later.
        const bool fresh_idle = live && is_idle(device->state) && !preheat.off.lost &&
                                now.mono - device->last_update_mono <= FRESH_FOR_ACTION_MS;
        const bool idle_confirmed = fresh_idle && preheat.off.idle_reports >= 1;
        if (busy)
            impl.close_auto_off(actions, "print_started");
        else if (fresh_idle && live_field(device->last, "targetBedTemp0") == 0)
            impl.close_auto_off(actions, "not_needed");
        else if (idle_confirmed && !upload_in_progress(preheat.address) && impl.send_bed_off(actions, "stopped", now, true)) {
            preheat.off.verifying = false; // nobody follows the confirmation any more
            bed_off_sent = true;
        } else {
            impl.close_auto_off(actions, "stopped_not_sent");
            impl.add_event(actions, preheat.address, "preheat_stopped_bed_on", json::object(), "Print Advisor disattivato",
                           "Il preriscaldamento di " + preheat.address + " non è più seguito e Print Advisor non ha potuto "
                           "spegnere il piatto: potrebbe essere ancora caldo. Controlla la stampante.");
        }
    }
    preheat.off.verifying = false;
    preheat.off.lost = false;
    impl.end_guard(actions, "stopped", false);
    // The final setpoint is sent now when the printer is idle; nobody follows it afterwards.
    if (impl.end_cooldown(actions, "stopped", true, now, CooldownFinal::Now))
        bed_off_sent = true;
    const std::map<std::string, CooldownEnd> ends = std::move(impl.cooldown_ends);
    impl.cooldown_ends.clear();
    for (const auto& [end_address, end] : ends) {
        const FinalResult result = impl.send_cooldown_final(actions, end_address, end.to, end.last_sent, now);
        if (result == FinalResult::Sent)
            bed_off_sent = true;
        else if (result == FinalResult::Wait)
            impl.add_event(actions, end_address, "slow_cooldown_final_dropped", {{"reason", "stopped"}}, "Print Advisor disattivato",
                           "Print Advisor non ha potuto portare il piatto di " + end_address + " al valore finale del "
                           "raffreddamento lento (" + std::to_string(end.to) + " °C): controlla la stampante.");
    }
    std::vector<std::pair<std::string, int>> known; // address, state: their device page shows no preheat any more
    for (auto& [address, device] : impl.devices) {
        impl.close_log(actions, address, device, "monitor_stopped", now);
        known.emplace_back(address, device.state);
    }
    // Telemetry state starts again from the next start of the server.
    impl.devices.clear();
    // Called while the server stops (possibly at shutdown): history is written directly.
    impl.run(actions, lock, RunMode::Stopping);
    // Clears the preheat and soak shown by the device page, unless the windows are closing.
    if (wxThread::IsMain() && !WebView::IsShuttingDown())
        for (const auto& [address, state] : known)
            advisor_push_device_page({{"command", "advisor_chamber"}, {"address", address}, {"state", state},
                                      {"preheat_started_ms", nullptr}, {"preheat_bed", nullptr},
                                      {"preheat_chamber_target", nullptr}, {"soak_seconds", nullptr},
                                      {"print_start_s", nullptr}, {"file", nullptr}});
    // SendToDevice only queues the bed-off on the I/O thread: at exit, give it a moment to write
    // it before the connection is torn down.
    if (bed_off_sent)
        std::this_thread::sleep_for(std::chrono::milliseconds(SHUTDOWN_FLUSH_MS));
}

bool PrintAdvisorMonitor::slow_cooldown_active(const std::string& address) const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->cooldown.active && m_impl->cooldown.address == address;
}

std::string PrintAdvisorMonitor::preheat_conflict(const std::string& address) const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->preheat_conflict_locked(address, now_time());
}

void PrintAdvisorMonitor::set_server_running(bool running)
{
    m_impl->enabled = running;
    set_tick_timer(running);
}

nlohmann::json PrintAdvisorMonitor::preheat_status() const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->preheat_status_locked(now_time());
}

nlohmann::json PrintAdvisorMonitor::chamber_baseline(const std::string& address) const
{
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    auto it = m_impl->baselines.find(address);
    return it == m_impl->baselines.end() ? json(nullptr) : it->second;
}

// --- routes ------------------------------------------------------------------------------------

namespace {

// Name of an existing print log; the directory listing is the only accepted source.
fs::path existing_log(const json& body)
{
    if (!body.contains("name") || !body["name"].is_string())
        throw std::runtime_error("name is required");
    const std::string name = body["name"].get<std::string>();
    // Without separators the name cannot leave the directory, and it must match one of its
    // entries. Dots inside the name are accepted: logs written before the dot runs were
    // collapsed (sanitized) may contain "..".
    if (name.empty() || name.size() > 200 || name.find_first_of("/\\:") != std::string::npos || name == "." || name == "..")
        throw std::runtime_error("Invalid log name");
    boost::system::error_code ec;
    for (fs::directory_iterator it(prints_dir(), ec), end; !ec && it != end; it.increment(ec))
        if (it->path().filename().string() == name && fs::is_regular_file(it->path(), ec))
            return it->path();
    throw std::runtime_error("Unknown print log: " + name);
}

json parse_line(std::string& line)
{
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return json::parse(line, nullptr, false);
}

std::vector<json> read_log(const fs::path& path)
{
    std::vector<json> lines;
    boost::system::error_code ec;
    if (fs::file_size(path, ec) > MAX_READ_BYTES || ec)
        return lines;
    fs::ifstream file(path, std::ios::binary);
    std::string line;
    while (std::getline(file, line)) {
        json value = parse_line(line);
        if (value.is_object())
            lines.push_back(std::move(value));
    }
    return lines;
}

bool flag(const json& line, const char* key)
{
    auto it = line.find(key);
    return it != line.end() && it->is_boolean() && it->get<bool>();
}

// Header (first line) and the last summary and verdict of a print log, reading only the first
// line and the last TAIL_BYTES of the file.
void read_log_ends(const fs::path& path, json& header, json& summary, json& verdict)
{
    boost::system::error_code ec;
    const std::uintmax_t size = fs::file_size(path, ec);
    if (ec)
        return;
    fs::ifstream file(path, std::ios::binary);
    std::string line;
    if (std::getline(file, line)) {
        json value = parse_line(line);
        if (value.is_object() && flag(value, "header"))
            header = std::move(value);
    }
    const std::uintmax_t start = size > TAIL_BYTES ? size - TAIL_BYTES : 0;
    file.clear();
    file.seekg(std::streamoff(start));
    if (start > 0)
        std::getline(file, line); // partial line
    while (std::getline(file, line)) {
        json value = parse_line(line);
        if (!value.is_object())
            continue;
        if (flag(value, "summary"))
            summary = std::move(value);
        else if (flag(value, "verdict"))
            verdict = std::move(value);
    }
}

using Impl = PrintAdvisorMonitor::Impl;

json open_log_names()
{
    Impl& impl = PrintAdvisorMonitor::instance().impl();
    std::lock_guard<std::mutex> lock(impl.mutex);
    json names = json::array();
    for (const auto& [address, device] : impl.devices)
        if (device.log.open)
            names.push_back(device.log.name);
    return names;
}

json print_logs()
{
    std::vector<std::string> names;
    boost::system::error_code ec;
    for (fs::directory_iterator it(prints_dir(), ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".jsonl" && fs::is_regular_file(it->path(), ec))
            names.push_back(it->path().filename().string());
    std::sort(names.rbegin(), names.rend()); // names start with the local start time
    if (names.size() > MAX_LISTED_LOGS)
        names.resize(MAX_LISTED_LOGS);
    const json open = open_log_names();
    json logs = json::array();
    for (const std::string& name : names) {
        json header, summary, verdict;
        read_log_ends(prints_dir() / name, header, summary, verdict);
        const bool running = std::find(open.begin(), open.end(), json(name)) != open.end();
        logs.push_back({{"name", name},
                        {"started", header.is_object() ? header.value("started_ms", json(nullptr)) : json(nullptr)},
                        {"result", summary.is_object() ? summary.value("result", json("unknown")) : json(running ? "running" : "unknown")},
                        {"file", header.is_object() ? header.value("file", json(nullptr)) : json(nullptr)},
                        {"address", header.is_object() ? header.value("address", json(nullptr)) : json(nullptr)},
                        {"verdict", verdict.is_object() ? verdict.value("value", json(nullptr)) : json(nullptr)}});
    }
    return {{"success", true}, {"logs", logs}};
}

json print_log(const json& body)
{
    const fs::path path = existing_log(body);
    int max_points = 200;
    if (body.contains("max_points")) {
        if (!body["max_points"].is_number_integer())
            throw std::runtime_error("max_points must be an integer");
        max_points = int(std::clamp<long long>(body["max_points"].get<long long>(), 2, 2000));
    }
    json header, summary, points = json::array(), events = json::array(), verdicts = json::array();
    for (json& line : read_log(path)) {
        if (flag(line, "header"))
            header = std::move(line);
        else if (flag(line, "summary"))
            summary = std::move(line);
        else if (flag(line, "verdict"))
            verdicts.push_back(std::move(line));
        else if (line.contains("event"))
            events.push_back(std::move(line));
        else if (line.contains("t"))
            points.push_back(std::move(line));
    }
    const size_t total = points.size();
    if (total > size_t(max_points)) {
        // Evenly spaced points, keeping the first and the last.
        json sampled = json::array();
        for (int i = 0; i < max_points; ++i)
            sampled.push_back(points[size_t((unsigned long long)(total - 1) * i / (max_points - 1))]);
        points = std::move(sampled);
    }
    const json open = open_log_names();
    const std::string name = path.filename().string();
    return {{"success", true}, {"name", name}, {"running", std::find(open.begin(), open.end(), json(name)) != open.end()},
            {"header", header}, {"points", points}, {"total_points", total}, {"events", events},
            {"summary", summary}, {"verdicts", verdicts}};
}

} // namespace

// Also used by the end-of-print verdict dialog (PrintAdvisorVerdict.cpp): one writer, one format.
json record_print_verdict(const json& body)
{
    const fs::path path = existing_log(body);
    static const std::set<std::string> allowed = {"ok", "warp", "detach", "stringing", "under_extrusion", "other"};
    if (!body.contains("verdict") || !body["verdict"].is_string() || !allowed.count(body["verdict"].get<std::string>()))
        throw std::runtime_error("verdict must be ok, warp, detach, stringing, under_extrusion or other");
    std::string note;
    if (body.contains("note") && !body["note"].is_null()) {
        if (!body["note"].is_string() || body["note"].get<std::string>().size() > 500)
            throw std::runtime_error("note must be a string of at most 500 characters");
        note = body["note"].get<std::string>();
    }
    const std::string verdict = body["verdict"].get<std::string>();
    const json line = {{"verdict", true}, {"value", verdict}, {"note", note}, {"t", wall_ms()}};
    {
        Impl& impl = PrintAdvisorMonitor::instance().impl();
        std::lock_guard<std::mutex> lock(impl.file_mutex);
        fs::ofstream file(path, std::ios::app | std::ios::binary);
        file << line.dump() << "\n";
        if (!file)
            throw std::runtime_error("Could not write the print log");
    }
    append_history({{"action", "print_verdict"}, {"log", path.filename().string()}, {"verdict", verdict}, {"note", note}});
    return {{"success", true}, {"name", path.filename().string()}, {"verdict", verdict}};
}

namespace {

json monitor_status()
{
    Impl& impl = PrintAdvisorMonitor::instance().impl();
    impl.tick();
    std::lock_guard<std::mutex> lock(impl.mutex);
    const Now now = now_time();
    json prints = json::array();
    for (const auto& [address, device] : impl.devices)
        prints.push_back({{"address", address}, {"state", device.state}, {"state_name", state_name(device.state)},
                          {"connected", device.connected}, {"updated_ms", device.last_update_ms},
                          {"log", device.log.open ? json(device.log.name) : json(nullptr)},
                          {"log_started_ms", device.log.open ? json(device.log.started_ms) : json(nullptr)},
                          {"waiting_bed_cool", device.wait_detach}});
    json events = json::array();
    for (const json& event : impl.events)
        events.push_back(event);
    return {{"success", true}, {"enabled", impl.enabled.load()}, {"exhaust_guard", impl.guard_status()},
            {"slow_cooldown", impl.cooldown_status()}, {"preheat", impl.preheat_status_locked(now)},
            {"printers", prints}, {"last_events", events}};
}

int integer_field(const json& body, const char* key, int fallback, bool required)
{
    if (!body.contains(key) || body[key].is_null()) {
        if (required)
            throw std::runtime_error(std::string(key) + " is required");
        return fallback;
    }
    if (!body[key].is_number())
        throw std::runtime_error(std::string(key) + " must be a number");
    const double value = body[key].get<double>();
    if (!std::isfinite(value) || value != std::floor(value) || std::abs(value) > 1e6)
        throw std::runtime_error(std::string(key) + " must be a whole number");
    return int(value);
}

// Throws unless the bed of `address` can be cooled from `from`: current telemetry of a connected
// printer (less than 60 s old), and `from` never above the current setpoint; with the heater off
// (setpoint 0) or no setpoint reported, never above the bed temperature + 1 °C. A cool-down must
// never heat the bed.
void check_cooldown_bed(const std::string& address, int from)
{
    const json telemetry = telemetry_of(address);
    if (!telemetry.is_object() || !telemetry.value("connected", false))
        throw std::runtime_error("The printer connection is not open: open the Device tab in MentorSlicer and retry");
    // Monotonic age: a wall-clock change must not make old data look recent.
    const long long age = DeviceTelemetryCache::instance().age_ms(address);
    if (age < 0 || age > FRESH_FOR_ACTION_MS)
        throw std::runtime_error("No recent data from the printer: open the Device tab in MentorSlicer and retry");
    const double bed = live_field(telemetry, "bedTemp0");
    const double target = live_field(telemetry, "targetBedTemp0");
    if (bed < 0 && target < 0)
        throw std::runtime_error("La stampante non riporta la temperatura del piatto: impossibile verificare il raffreddamento");
    if (target > 0) {
        if (from > target)
            throw std::runtime_error("Il raffreddamento non può scaldare il piatto: from (" + std::to_string(from) +
                                     " °C) supera la temperatura impostata ora (" + to_text(target) + " °C)");
    } else {
        if (bed < 0)
            throw std::runtime_error("La stampante non riporta la temperatura del piatto: impossibile verificare il raffreddamento");
        if (from > bed + 1)
            throw std::runtime_error("Il raffreddamento non può scaldare il piatto: from (" + std::to_string(from) +
                                     " °C) supera la temperatura attuale del piatto (" + to_text(bed) + " °C, riscaldamento spento)");
    }
}

json slow_cooldown(const json& body, const std::atomic<bool>& stopping)
{
    Impl& impl = PrintAdvisorMonitor::instance().impl();
    if (body.contains("cancel") && body["cancel"].is_boolean() && body["cancel"].get<bool>()) {
        std::unique_lock<std::mutex> lock(impl.mutex);
        const bool was_active = impl.cooldown.active;
        Actions actions;
        if (was_active) {
            impl.end_cooldown(actions, "cancelled", true, now_time());
            actions.history.push_back({{"action", "slow_cooldown_cancel"}, {"address", impl.cooldown.address}});
        }
        const json status = impl.cooldown_status();
        impl.run(actions, lock);
        return {{"success", true}, {"cancelled", was_active}, {"slow_cooldown", status}};
    }

    const int from = integer_field(body, "from", 0, true);
    const int to = integer_field(body, "to", 0, true);
    const int step = integer_field(body, "step_c", 10, false);
    const int minutes = integer_field(body, "minutes_per_step", 5, false);
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        throw std::runtime_error("PresetBundle is unavailable");
    const MachineLimits limits = machine_limits(bundle->printers.get_edited_preset().config);
    const int max_bed = limits.max_bed_temp > 0 ? int(limits.max_bed_temp) : 120;
    if (to < 0 || from > max_bed || from <= to)
        throw std::runtime_error("from and to must satisfy 0 <= to < from <= " + std::to_string(max_bed));
    if (step < 1 || step > 50)
        throw std::runtime_error("step_c must be between 1 and 50");
    if (minutes < 1 || minutes > 60)
        throw std::runtime_error("minutes_per_step must be between 1 and 60");

    const DeviceSnapshot device = selected_device_snapshot();
    if (device.state != 0 && !is_ended(device.state))
        throw std::runtime_error(std::string("Slow cool-down is allowed only while the printer is idle (state: ") +
                                 state_name(device.state) + ")");
    check_cooldown_bed(device.address, from);
    const int steps = (from - to + step - 1) / step;
    const std::string question =
        "Raffreddare lentamente il piatto di " + device.address + " da " + std::to_string(from) + " °C a " + std::to_string(to) +
        " °C, " + std::to_string(step) + " °C ogni " + std::to_string(minutes) + " minuti (circa " + std::to_string(steps * minutes) +
        " min)?\n\nCon questa conferma MentorSlicer invierà da solo alla stampante i valori successivi del piatto, "
        "finché la scheda Dispositivo resta aperta. Si annulla se parte una stampa, se il piatto viene impostato da altri "
        "o se la stampante non invia dati per 10 minuti.\n\n" +
        (to == 0 ? std::string("Alla fine il piatto viene spento.")
                 : "Alla fine il piatto resta acceso a " + std::to_string(to) + " °C.");
    if (!ask_user("Raffreddamento lento del piatto", question, stopping,
                  {{"Piatto", std::to_string(from) + " °C", std::to_string(to) + " °C"}}))
        return {{"success", false}, {"cancelled", true}};
    check_device_unchanged(device);
    // Again after the dialog: the bed may have cooled or been changed while it was open.
    check_cooldown_bed(device.address, from);

    std::unique_lock<std::mutex> lock(impl.mutex);
    if (!impl.enabled.load())
        throw std::runtime_error("Print Advisor is stopping");
    // Last check right before the first setpoint (the monitor lock is taken before the cache
    // lock, never the opposite): `from` is never above the current setpoint.
    check_cooldown_bed(device.address, from);
    // Sent under the state lock, so the monitor cannot send a step of the cool-down replaced below.
    if (!WebSocketProxy::SendToDevice(device.address, bed_setpoint_message(from)))
        throw std::runtime_error("The printer connection is not open: open the Device tab in MentorSlicer and retry");
    Actions actions;
    const Now now = now_time();
    // A cool-down of another printer ends with its final setpoint (now, or when it reports
    // again); on this printer the new cool-down takes over from `from`.
    const bool other_printer = impl.cooldown.active && impl.cooldown.address != device.address;
    impl.end_cooldown(actions, "replaced", true, now, other_printer ? CooldownFinal::NowOrOnReturn : CooldownFinal::Leave);
    impl.cooldown_ends.erase(device.address);
    // A preheat follow-up of this printer would switch the bed off in the middle of the cool-down.
    if (impl.preheat.address == device.address && impl.follows_preheat()) {
        impl.end_preheat(actions, "replaced", now, false);
        impl.close_auto_off(actions, "replaced");
    }
    Cooldown& c = impl.cooldown;
    c = Cooldown();
    c.active = true;
    c.started_mono = now.mono;
    c.address = device.address;
    c.from = from;
    c.to = to;
    c.step = step;
    c.minutes = minutes;
    c.last_sent = from;
    c.last_sent_ms = now.wall;
    c.next_setpoint = std::max(to, from - step);
    c.next_mono = now.mono + minutes * MINUTE_MS;
    c.next_ms = now.wall + minutes * MINUTE_MS;
    c.outcome = "running";
    actions.history.push_back({{"action", "slow_cooldown"}, {"address", device.address}, {"from", from}, {"to", to},
                               {"step_c", step}, {"minutes_per_step", minutes}});
    const json status = impl.cooldown_status();
    impl.run(actions, lock);
    return {{"success", true}, {"slow_cooldown", status}};
}

json exhaust_guard(const json& body, const std::atomic<bool>& stopping)
{
    Impl& impl = PrintAdvisorMonitor::instance().impl();
    if (!body.contains("enabled") || !body["enabled"].is_boolean())
        throw std::runtime_error("enabled (boolean) is required");
    if (!body["enabled"].get<bool>()) {
        std::unique_lock<std::mutex> lock(impl.mutex);
        Actions actions;
        if (impl.guard.armed) {
            impl.end_guard(actions, "disabled", false);
            actions.history.push_back({{"action", "exhaust_guard"}, {"enabled", false}, {"address", impl.guard.address},
                                       {"reenables", impl.guard.reenables}});
        }
        const json status = impl.guard_status();
        impl.run(actions, lock);
        return {{"success", true}, {"exhaust_guard", status}};
    }

    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        throw std::runtime_error("PresetBundle is unavailable");
    const Preset& filament = bundle->filaments.get_edited_preset();
    const auto* filtration = filament.config.option<ConfigOptionBools>("activate_air_filtration");
    const auto* exhaust = filament.config.option<ConfigOptionInts>("during_print_exhaust_fan_speed");
    if (!filtration || filtration->values.empty() || !filtration->get_at(0) || !exhaust || exhaust->values.empty() ||
        exhaust->get_at(0) != 0)
        throw std::runtime_error("The exhaust guard needs the active filament (" + filament.name +
                                 ") to have activate_air_filtration=1 and during_print_exhaust_fan_speed=0");
    const std::string filament_name = filament.name;

    const DeviceSnapshot device = selected_device_snapshot();
    if (!is_printing(device.state))
        throw std::runtime_error(std::string("The exhaust guard can be armed only during a print (state: ") +
                                 state_name(device.state) + ")");
    // The guard follows only the print running now: its identity is taken before the dialog.
    const json before = telemetry_of(device.address);
    const PrintIdentity print = print_identity(before);
    if (!print.known())
        throw std::runtime_error("The printer reports neither the print start time nor the file name: the guard cannot be "
                                 "bound to the running print");
    const double max_box = live_field(before, "maxBoxTemp");
    const double ceiling = max_box > 0 ? max_box - CHAMBER_CEILING_MARGIN : CHAMBER_CEILING_DEFAULT;
    const std::string question =
        "Attivare la guardia della ventola di estrazione posteriore su " + device.address + "?\n\n"
        "Durante la stampa il firmware riaccende l'estrazione quando la camera arriva a circa 37 °C. "
        "Con questa conferma MentorSlicer la spegnerà da sola (fanCase 0) ogni volta che si riaccende, al massimo una "
        "volta ogni 10 secondi, fino alla fine della stampa in corso" +
        (print.file.empty() ? std::string() : " (" + print.file + ")") + ". Funziona solo con la scheda Dispositivo aperta "
        "e si disattiva se la stampante non invia dati per 2 minuti.\n\n"
        "Limite camera: " + to_text(ceiling) + " °C. Da quella temperatura la guardia lascia l'estrazione accesa, "
        "finché la camera non scende sotto " + to_text(ceiling - CHAMBER_CEILING_RESUME) + " °C.\n\n"
        "Filamento: " + filament_name;
    if (!ask_user("Guardia ventola di estrazione", question, stopping))
        return {{"success", false}, {"cancelled", true}};
    check_device_unchanged(device);
    if (!(print_identity(telemetry_of(device.address)) == print))
        throw std::runtime_error("The print changed while the dialog was open; the guard was not armed");

    std::unique_lock<std::mutex> lock(impl.mutex);
    if (!impl.enabled.load())
        throw std::runtime_error("Print Advisor is stopping");
    Actions actions;
    const Now now = now_time();
    impl.end_guard(actions, "replaced", true);
    impl.guard = Guard();
    impl.guard.armed = true;
    impl.guard.address = device.address;
    impl.guard.armed_ms = now.wall;
    impl.guard.armed_mono = now.mono;
    impl.guard.print = print;
    impl.guard.ceiling = ceiling;
    if (auto it = impl.devices.find(device.address); it != impl.devices.end())
        it->second.exhaust_on = false;
    actions.history.push_back({{"action", "exhaust_guard"}, {"enabled", true}, {"address", device.address},
                               {"filament", filament_name}, {"chamber_ceiling", ceiling}});
    const json status = impl.guard_status();
    impl.run(actions, lock);
    return {{"success", true}, {"exhaust_guard", status}};
}

} // namespace

std::vector<AdvisorRoute> monitor_routes()
{
    std::vector<AdvisorRoute> routes;
    routes.push_back({"/v1/preheat_status", "GET", true, [](const json&, const std::atomic<bool>&) {
        PrintAdvisorMonitor::instance().impl().tick();
        json status = PrintAdvisorMonitor::instance().preheat_status();
        status["success"] = true;
        return status;
    }});
    routes.push_back({"/v1/monitor_status", "GET", true, [](const json&, const std::atomic<bool>&) { return monitor_status(); }});
    // File reads only: they run on the server thread.
    routes.push_back({"/v1/print_logs", "GET", false, [](const json&, const std::atomic<bool>&) { return print_logs(); }});
    routes.push_back({"/v1/print_log", "POST", false, [](const json& body, const std::atomic<bool>&) { return print_log(body); }});
    routes.push_back({"/v1/print_verdict", "POST", true, [](const json& body, const std::atomic<bool>&) { return record_print_verdict(body); }});
    routes.push_back({"/v1/slow_cooldown", "POST", true, [](const json& body, const std::atomic<bool>& stopping) {
        return slow_cooldown(body, stopping);
    }});
    routes.push_back({"/v1/exhaust_guard", "POST", true, [](const json& body, const std::atomic<bool>& stopping) {
        return exhaust_guard(body, stopping);
    }});
    return routes;
}

} // namespace advisor
} // namespace GUI
} // namespace Slic3r

