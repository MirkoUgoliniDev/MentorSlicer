#ifndef slic3r_GUI_PrintAdvisorMonitor_hpp_
#define slic3r_GUI_PrintAdvisorMonitor_hpp_

// Print Advisor print monitor. It follows the LAN telemetry of the printers (DeviceTelemetryCache)
// and, only while Print Advisor is enabled:
// - follows a chamber preheat started by Print Advisor, reports its outcome and switches the bed
//   off when the printer stays idle (timeout, or 20 min after the chamber settled), also when its
//   telemetry returns after a loss (for up to 12 h); one preheat at a time (preheat_conflict);
// - keeps a per-print log (<data_dir>/print-advisor/prints/*.jsonl, newest 200 files, 20 MB each);
// - reports filament runout, a bed setpoint drop after the heat soak, a cooled bed after a print;
// - runs the actions the user confirmed in Creality Print: exhaust guard and slow bed cool-down;
// - pushes the preheat and print timing to the device manager page.
// Telemetry arrives on the WebSocket proxy thread; every member is thread-safe. GUI and preset
// state is read only on the GUI thread (CallAfter). Automations act only on telemetry of a
// connected printer, from the current connection, without a gap.

#include <nlohmann/json.hpp>

#include <memory>
#include <string>

namespace Slic3r {
namespace GUI {
namespace advisor {

class PrintAdvisorMonitor
{
public:
    static PrintAdvisorMonitor& instance();

    // Merged telemetry of the printer at `address` after each update (DeviceTelemetryCache
    // entry, with "connected" and "stale_keys"). Any thread; never blocks long.
    void on_telemetry(const std::string& address, const nlohmann::json& device);

    // The last device connection to `address` closed. Any thread.
    void on_disconnected(const std::string& address);

    // Follows the chamber of `address` after a confirmed preheat. bed_target <= 0 means the user
    // switched the preheat off (outcome "cancelled"). chamber_target <= 0 means no target.
    // max_minutes (clamped to 5..120) caps the preheat: when it elapses and the printer is still
    // idle the monitor switches the bed off. After a chamber plateau or the chamber target the
    // bed is switched off 20 min later unless a print started (or earlier, at max_minutes).
    // Throws when Print Advisor is stopping, or (bed_target > 0) when preheat_conflict(address)
    // is not empty: the server must check preheat_conflict before the dialog and again before it
    // sends the bed setpoint, so this throw is only a last guard. request_id: id of the bed
    // command sent to the device page (send_device_control), matched by on_bed_command_failed.
    void arm_preheat(const std::string& address, double bed_target, double chamber_target, int max_minutes,
                     const std::string& request_id = {});

    // The device page answered that the bed command `request_id` (setpoint bed_value) sent to
    // `address` failed. GUI thread.
    // - bed_value > 0 (preheat start): the preheat armed by that request ends (outcome
    //   command_failed) so it neither shows as running nor blocks other printers; its automatic
    //   switch-off stays armed only if the printer reports a bed setpoint above 0.
    // - bed_value 0 (switch-off): the bed may still be hot. The switch-off is sent directly over
    //   the LAN connection, like the automatic switch-off, now if the printer is idle, otherwise
    //   kept pending until it is; when the monitor follows another printer, one direct attempt.
    // Both cases are logged, written to the history and notified.
    void on_bed_command_failed(const std::string& address, double bed_value, const std::string& request_id);

    // Message refusing a preheat of `address` (to report as a conflict), or empty when it may
    // start. A preheat of another printer that is still followed and whose bed may be hot is
    // never replaced: the user cancels it first (preheat with bed_temp 0 on that printer).
    std::string preheat_conflict(const std::string& address) const;

    // True while a slow cool-down of `address` is running: a new preheat would end it.
    bool slow_cooldown_active(const std::string& address) const;

    // Stops the preheat follow-up (switching the bed off when the printer is idle, best effort),
    // the exhaust guard and the slow cool-down, and closes the open print logs. Called when the
    // Print Advisor server stops.
    void disarm_all();

    // The monitor runs only while the Print Advisor server runs. Set by PrintAdvisorServer after a
    // successful start (true) and when it stops (false). Starts and stops a 30 s GUI timer that
    // runs the time-based checks (preheat timeout, telemetry loss) without new telemetry.
    void set_server_running(bool running);

    // {active, address, started_ms, ended_ms, max_minutes, timeout_at_ms, baseline_chamber,
    //  bed_target, chamber_target, chamber, bed, rise_last_5min, outcome, readings,
    //  auto_off: {pending, reason, due_at_ms, outcome, attempts, sent_ms, confirmed}}
    nlohmann::json preheat_status() const;

    // {chamber, at_ms} recorded when the last preheat of `address` was armed, or null.
    nlohmann::json chamber_baseline(const std::string& address) const;

    // Used by the monitor routes (PrintAdvisorMonitor.cpp).
    struct Impl;
    Impl& impl() const { return *m_impl; }

private:
    PrintAdvisorMonitor();
    ~PrintAdvisorMonitor();
    PrintAdvisorMonitor(const PrintAdvisorMonitor&) = delete;
    PrintAdvisorMonitor& operator=(const PrintAdvisorMonitor&) = delete;

    std::unique_ptr<Impl> m_impl;
};

} // namespace advisor
} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_PrintAdvisorMonitor_hpp_
