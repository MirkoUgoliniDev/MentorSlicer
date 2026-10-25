#ifndef slic3r_GUI_PrintAdvisorInternal_hpp_
#define slic3r_GUI_PrintAdvisorInternal_hpp_

// Helpers shared by the Print Advisor modules (server, printer control, monitor, supports,
// camera). Internal to Print Advisor: not an API for the rest of the application.

#include "AdvisorConfirmDialog.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <boost/filesystem/path.hpp>
#include <nlohmann/json.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace advisor {

// One endpoint of the local Print Advisor API.
// - path: "/v1/..."; method: "GET" (request body must be empty) or "POST" (body must be a
//   non-empty JSON document). GET handlers receive an empty JSON object.
// - runs_on_gui: true runs the handler on the GUI thread through CallAfter, like every existing
//   route; false runs it on the worker thread of the connection (for handlers that wait for
//   something the GUI thread must deliver, such as a reply from a web page). Each connection
//   has its own worker, so such a handler may run on several threads at once: it must be
//   thread-safe, must not touch GUI or preset state directly and must poll `stopping` while
//   it waits.
// - stopping: set when the server is shutting down; check it after any wait or dialog.
// A thrown std::exception becomes HTTP 400 {"success":false,"error":what()}.
struct AdvisorRoute {
    std::string path;
    std::string method; // "GET" | "POST"
    bool runs_on_gui{true};
    std::function<nlohmann::json(const nlohmann::json& body, const std::atomic<bool>& stopping)> handler;
};

// Route groups of the Print Advisor modules, registered once when the server starts.
std::vector<AdvisorRoute> printer_control_routes(); // PrintAdvisorPrinterControl.cpp
std::vector<AdvisorRoute> monitor_routes();         // print_manage/App/PrintAdvisorMonitor.cpp
std::vector<AdvisorRoute> supports_routes();        // PrintAdvisorSupports.cpp
std::vector<AdvisorRoute> camera_routes();          // PrintAdvisorCamera.cpp
std::vector<AdvisorRoute> plate_routes();           // PrintAdvisorPlate.cpp (GET/POST /v1/bed_type)
std::vector<AdvisorRoute> archive_routes();         // PrintAdvisorVerdict.cpp (GET /v1/archive, POST /v1/archive/open)

// Advisory pre-print findings (never errors) for ASA/ABS on the current plate: glue stick on a
// smooth PEI plate with a large part, and a cold chamber (LAN telemetry) on an idle printer.
// `material` is the filament type as shown to the user; `telemetry` is current_device_telemetry()
// or null; `telemetry_stale_reported` is true when the caller already emitted printer_data_stale
// (chamber_unknown is then not repeated). Each finding has severity, code, message and data.
// GUI thread. PrintAdvisorPlate.cpp.
std::vector<nlohmann::json> plate_material_findings(const std::string& material, bool asa_abs, BedType bed_type,
                                                    const nlohmann::json& telemetry, bool telemetry_stale_reported);

// Number from a JSON number or a numeric string, read with the classic locale (the GUI locale
// may use a decimal comma). Returns -1 when the value is not a number.
double json_number(const nlohmann::json& value);

// Printer model name without the "Creality " prefix, trimmed and lower case.
std::string normalized_model(std::string model);

// Physical limits of the printer Print Advisor acts on. Each limit is the lowest of what the
// target printer (target_device_address) reports over LAN (maxBedTemp, maxNozzleTemp,
// maxBoxTemp) and the built-in table of known models (profile model and reported model): the
// report can lower the table, never raise it. A negative value means unknown. GUI thread.
struct MachineLimits {
    double max_bed_temp{-1};
    double max_nozzle_temp{-1};
    double max_chamber_temp{-1};
    std::string source{"unknown"};

    nlohmann::json to_json() const;
};

MachineLimits machine_limits(const DynamicPrintConfig& printer_config);

// True when a window that a mutating request must not run inside is open: a shown modal
// wxDialog, a native modal dialog (its own event loop is active), or the modeless "Send to
// printer" dialog (CxSentToPrinterDialog). GUI thread.
bool modal_ui_active();

// Number as text with the classic locale ("0.5", never "0,5") and up to 6 significant digits.
std::string format_number(double value);

// `text` cut to at most max_bytes bytes without splitting a UTF-8 sequence.
std::string truncate_utf8_bytes(const std::string& text, size_t max_bytes);

// `path` relative to the data directory ("print-advisor/history.jsonl"), or its file name when
// it is outside it. Paths given to the MCP client never reveal the user's folders.
std::string data_relative_path(const boost::filesystem::path& path);

// Append-only log of what Print Advisor changed: <data_dir>/print-advisor/history.jsonl.
// Above 5 MB it is moved to history_backup_path() (one backup kept) before the next append;
// readers must read the backup first, then the current file.
boost::filesystem::path history_path();
boost::filesystem::path history_backup_path();
// Adds "time_ms" and appends the entry as one JSON line. Never throws.
void append_history(nlohmann::json entry);

// Name of a printer state code (0 idle, 1 printing, 2 complete, 3 failed, 4 aborted,
// 5 paused, 6 pausing, 7 stopping, 8 restoring); "unknown" otherwise.
const char* state_name(int state);

// Address of the printer Print Advisor acts on: the device manager's current device or, when
// none is set, the only LAN printer reporting the model of the selected printer profile.
// Empty when there is none. GUI thread.
std::string target_device_address();

// LAN telemetry (DeviceTelemetryCache fields) of that printer, or null. GUI thread.
nlohmann::json current_device_telemetry();

// "state" field of a telemetry object, or -1 (also when it is listed in "stale_keys").
int telemetry_state(const nlohmann::json& device);

// Selected printer and its last reported state, to check that nothing changed while a
// confirmation dialog was open.
struct DeviceSnapshot {
    std::string address;
    int state{-1};
    long long updated_at_ms{-1};
};

// Throws when no printer is selected or its telemetry is older than 60 s, and when
// `require_state` and the state is stale (kept from a previous connection). GUI thread.
DeviceSnapshot selected_device_snapshot(bool require_state = true);

// Throws when the selected printer or its state differs from `before`. GUI thread.
void check_device_unchanged(const DeviceSnapshot& before);

// Sends {"action": action, "device": {...identity...}, "request_id": id, ...extra} to the device
// manager page (PrinterMgrView::request_print_control). Throws when the selected printer is not
// expected_address. Returns the new request id, or empty when request_print_control failed.
// The request (action, address, extra "value") is kept until the page answers with
// print_control_result echoing the id (advisor_on_print_control_result). GUI thread.
std::string send_device_control(const std::string& expected_address, const std::string& action,
                                const nlohmann::json& extra = nlohmann::json::object());

// Last print_control_result received from the device page: {success, action, request_id, tool,
// address?, value?, code?, message?, received_at, requested_at?} (times in ms since the epoch),
// or null when none arrived since the start. Any thread.
nlohmann::json last_print_control_result();
// Last result of each action ({"pause": {...}, "set_bed_temp": {...}}, same fields), so a late
// answer to one command does not hide the answer to another. Empty object when none. Any thread.
nlohmann::json print_control_results();

// Cancellation flag of the GUI request being handled, or null outside a request. The server
// sets the request's flag when it stops waiting for the reply (timeout): the client has been
// told the request failed, so nothing may be applied afterwards. The server installs it around
// each GUI handler (ScopedRequestFlag). GUI thread.
std::shared_ptr<std::atomic<bool>> current_request_cancel_flag();
class ScopedRequestFlag
{
public:
    explicit ScopedRequestFlag(std::shared_ptr<std::atomic<bool>> cancelled);
    ~ScopedRequestFlag();
    ScopedRequestFlag(const ScopedRequestFlag&) = delete;
    ScopedRequestFlag& operator=(const ScopedRequestFlag&) = delete;

private:
    std::shared_ptr<std::atomic<bool>> m_previous;
};
// Throws "Richiesta scaduta: nessuna modifica applicata" when `cancelled` is set.
void throw_if_request_cancelled(const std::shared_ptr<std::atomic<bool>>& cancelled);

// Shows the MCP Advisor confirmation dialog (advisor_confirm): optional before -> after table
// and destructive styling. It runs a nested event loop, so the server may be stopped or the
// request may time out (or its client disconnect) meanwhile: throws "Print Advisor is stopping"
// if the stop flag is set, and "Richiesta scaduta..." if the current request was cancelled,
// both before the dialog is shown and after it closes.
// Returns true only when the user confirmed. GUI thread.
// The flag is taken by value: the dialog may outlive the object that owns the caller's copy.
bool ask_user(const std::string& action, const std::string& question, std::shared_ptr<std::atomic<bool>> stopping,
              const std::vector<AdvisorChangeRow>& changes = {}, bool destructive = false);
// GUI thread. Depth of the points where a Print Advisor request waits for the user with nothing
// half-applied (ask_user/advisor_confirm, the export file dialog). Read-only requests that a
// dialog's event loop dispatches may run there; anywhere else inside a changing request the
// server postpones them. Defined in AdvisorConfirmDialog.cpp.
int& advisor_user_wait_depth();

class AdvisorUserWait
{
public:
    AdvisorUserWait() { ++advisor_user_wait_depth(); }
    ~AdvisorUserWait() { --advisor_user_wait_depth(); }
    AdvisorUserWait(const AdvisorUserWait&) = delete;
    AdvisorUserWait& operator=(const AdvisorUserWait&) = delete;
};

// Same, for route handlers that receive the stop flag by reference: that flag is owned by the
// server's dispatch callback and stays alive until the handler returns.
bool ask_user(const std::string& action, const std::string& question, const std::atomic<bool>& stopping,
              const std::vector<AdvisorChangeRow>& changes = {}, bool destructive = false);

// Reason sent by the MCP client, made safe to show in a confirmation dialog: control characters
// (CR and LF included), line separators and bidirectional overrides become spaces, and the text
// is cut to 300 characters. Empty when `value` is not a string or holds only blanks.
std::string sanitized_reason(const nlohmann::json& value);
// Untrusted text (3MF object/volume names, preset and file names) made safe for a dialog the
// same way, cut to max_chars characters ("..." appended when cut). "(senza nome)" when empty.
std::string sanitized_text(const std::string& text, size_t max_chars = 120);
// "Motivo indicato dall'assistente: <reason>" for sanitized_reason(value), or empty.
std::string reason_text(const nlohmann::json& value);

// --- End-of-print verdict and archive (PrintAdvisorVerdict.cpp) -----------------------------

// Appends the user's verdict to a print log, same format as POST /v1/print_verdict:
// body {name, verdict: ok|warp|detach|stringing|other, note?}. Defined in PrintAdvisorMonitor.cpp.
nlohmann::json record_print_verdict(const nlohmann::json& body);

// A print completed and its bed cooled below 40 C: asks the user how it turned out (dialog on
// the GUI thread, plus a notification). `print` holds {log, file, address, ended_ms, printer,
// filament, process}. Only one question is pending at a time (a new one replaces the old) and
// the dialog waits while another modal window is open. Any thread.
void request_print_verdict(const nlohmann::json& print);
// Server lifecycle: re-asks a verdict left pending ("Later" or app closed) a few seconds after
// the start; stops the pending timers and the photo worker. GUI thread.
void verdict_on_server_started();
void verdict_on_server_stopped();
// True when the print log `log_name` (a file name in <data_dir>/print-advisor/prints) already
// holds a verdict line. False for an unknown log. Any thread.
bool print_log_has_verdict(const std::string& log_name);
// The MCP client recorded a verdict for `log_name` (POST /v1/print_verdict succeeded): the
// end-of-print question about that print, if pending and not on screen, is cleared. GUI thread.
void verdict_recorded_by_client(const std::string& log_name);

// One camera frame through the device page, like GET /v1/camera_snapshot: {success, mime,
// data_base64, ...} or {success: false, error}. Waits up to 10 s: never call it on the GUI
// thread. Defined in PrintAdvisorCamera.cpp.
nlohmann::json camera_snapshot_now(const std::atomic<bool>& stopping);

} // namespace advisor

// Result posted by the device page (print_control_result) for a pause/resume/stop or a
// set_bed_temp sent through send_device_control: {success, action, request_id, code?, message?}.
// Matched to its request by request_id (by action when the page sends no id), stored for
// last_print_control_result/print_control_results, appended to the history; a failure is logged
// at warning level and notified. A failed set_bed_temp is handed to the preheat monitor
// (PrintAdvisorMonitor::on_bed_command_failed): a failed switch-off is retried, a failed preheat
// start is no longer followed. Called by advisor_handle_page_message. GUI thread.
void advisor_on_print_control_result(const nlohmann::json& result);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_PrintAdvisorInternal_hpp_
