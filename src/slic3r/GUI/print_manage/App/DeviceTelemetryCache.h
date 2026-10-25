#ifndef slic3r_DeviceTelemetryCache_h_
#define slic3r_DeviceTelemetryCache_h_

#include "nlohmann/json.hpp"

#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>

namespace Slic3r {
namespace GUI {

// Last state reported by each LAN printer (temperatures, limits, fans, progress), taken from
// the messages that already pass through the device WebSocket proxy. Updated on the proxy
// thread and read on the GUI thread, so every access is guarded by a mutex.
//
// The printer sends partial updates that are merged per printer, so a merged value is current
// only while the connection that reported it stays open. Each entry therefore carries:
// - "connected": false once the last device connection to that printer closed;
// - "stale_keys": fields kept from a previous connection and not reported again since
//   (present only when not empty);
// - "updated_at_ms": wall-clock time of the last message (display only; use age_ms() for
//   freshness decisions, it is not affected by clock changes).
// Printer strings are untrusted: they are cut to 128 characters without control or
// bidirectional formatting characters, "err" keeps only the numbers errcode and key, and
// printFileName keeps only the file name.
class DeviceTelemetryCache
{
public:
    static DeviceTelemetryCache& instance();

    void update(const std::string& host, std::string_view payload);

    // The last device connection to `host` closed (proxy thread). The entry is kept, marked
    // disconnected, and every field becomes stale until the printer reports it again.
    void mark_disconnected(const std::string& host);

    // { "<host>": { <reported fields>, "updated_at_ms": <time>, "connected": <bool> }, ... }
    nlohmann::json snapshot() const;

    // Most recent print finished/failed/aborted and error events, oldest first.
    nlohmann::json events() const;

    // Milliseconds since the last message from `host`, measured on the monotonic clock, or -1
    // when nothing was received from it.
    long long age_ms(const std::string& host) const;

private:
    DeviceTelemetryCache() = default;

    void record_event(const std::string& host, const std::string& type, const std::string& detail);

    mutable std::mutex                             m_mutex;
    std::map<std::string, nlohmann::json>          m_devices;
    std::map<std::string, std::set<std::string>>   m_stale;
    std::map<std::string, long long>               m_updated_mono; // steady clock, ms
    struct Disconnect
    {
        long long at_mono{0};       // steady clock, ms
        bool state_current{false};  // "state" was current on the connection that closed
    };
    std::map<std::string, Disconnect>              m_disconnects;
    std::deque<nlohmann::json>                     m_events;
};

// Windows notification for Print Advisor events, shown only while Print Advisor is enabled
// (print_advisor_mcp_enabled), or always when `even_when_disabled` (used for heater safety
// messages sent while Print Advisor is being switched off). Any thread: the notification is
// shown on the GUI thread.
void print_advisor_notify(const std::string& title, const std::string& message, bool even_when_disabled = false);

} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_DeviceTelemetryCache_h_ */
