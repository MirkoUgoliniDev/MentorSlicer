#ifndef slic3r_WebSocketProxyAdvisor_hpp_
#define slic3r_WebSocketProxyAdvisor_hpp_

// Print Advisor side of the device WebSocket proxy (WebSocketProxySession.cpp): the application
// sends its own messages to a LAN printer through the connection opened by the device page, and
// the proxied device messages feed DeviceTelemetryCache. Creality's proxy calls into this file
// through a few lines marked PRINT-ADVISOR; the logic lives here.

#include <boost/log/trivial.hpp>

#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Slic3r {
namespace GUI {
namespace WebSocketProxy {

// Queues a text frame on the proxied WebSocket of the LAN printer at `host` (the address used
// as key by DeviceTelemetryCache). Returns false when no live proxy has an OPEN device
// connection to that host (a connection still being established does not count).
// Asynchronous and thread-safe: it never waits for the I/O thread, so true means "handed to
// the I/O thread", not "delivered". The I/O thread drops the message (and logs it) when the
// connection closed meanwhile or its send queue is full; it never closes the page's
// connection because of a message sent here.
bool SendToDevice(const std::string& host, const std::string& text);

namespace advisor_link {

// A live proxy that SendToDevice can reach (implemented by Manager::Impl).
class DeviceSender
{
public:
    virtual ~DeviceSender() = default;
    // Any thread. Queues the text on the I/O thread when an open device session to host exists.
    virtual bool SendToDeviceHost(const std::string& host, const std::string& text) = 0;
};

// Adds a proxy to the registry read by SendToDevice. Entries expire with their proxy.
void register_device_sender(const std::shared_ptr<DeviceSender>& sender);

// Hosts with an open device session of one proxy: published on the I/O thread when a session
// opens or ends, read by SendToDeviceHost from other threads.
class DeviceHosts
{
public:
    bool contains(const std::string& host) const;
    void publish(std::vector<std::string> hosts);

private:
    mutable std::mutex       m_mutex;
    std::vector<std::string> m_hosts;
};

// Proxy hooks (I/O thread).
// A device message passed through the proxy: keep the last reported state (limits,
// temperatures, fans) for Print Advisor, before the filter drops background telemetry.
void on_device_message(const std::string& host, std::string_view payload);
// The last open device connection to host ended: its merged telemetry is no longer current.
void on_device_disconnected(const std::string& host);

// The helpers below take the proxy's session map (id -> shared_ptr<Session>), whose types are
// private to WebSocketProxySession.cpp; they use only the public Session queries
// is_device_connection(), host(), is_open() and TryEnqueueText().

template<class Sessions>
std::vector<std::string> open_device_hosts(const Sessions& sessions)
{
    std::vector<std::string> hosts;
    for (const auto& item : sessions)
        if (item.second && item.second->is_device_connection() && item.second->is_open())
            hosts.push_back(item.second->host());
    return hosts;
}

// True when another open device session to `host` than `except` exists.
template<class Sessions, class Session>
bool has_open_device_session(const Sessions& sessions, const std::string& host, const Session* except)
{
    for (const auto& item : sessions)
        if (item.second && item.second.get() != except && item.second->is_device_connection() &&
            item.second->host() == host && item.second->is_open())
            return true;
    return false;
}

// One message per printer, only on an open connection: a message queued on a session still
// connecting could be sent much later, after the conditions were checked. TryEnqueueText drops
// the message instead of failing the page's connection.
template<class Sessions>
void send_on_open_session(const Sessions& sessions, const std::string& host, std::string text)
{
    for (const auto& item : sessions) {
        const auto& session = item.second;
        if (session && session->is_device_connection() && session->host() == host && session->is_open()) {
            if (!session->TryEnqueueText(std::move(text)))
                BOOST_LOG_TRIVIAL(warning) << "WebSocket proxy: application message to " << host << " not queued";
            return;
        }
    }
    BOOST_LOG_TRIVIAL(warning) << "WebSocket proxy: no open connection to " << host << ", application message dropped";
}

// A session ended and was removed from `sessions`: mark the printer's telemetry disconnected
// when no other open connection to the same printer remains.
template<class Sessions, class SessionPtr>
void on_session_finished(const Sessions& sessions, const SessionPtr& source)
{
    if (source->is_device_connection() && !has_open_device_session(sessions, source->host(), source.get()))
        on_device_disconnected(source->host());
}

} // namespace advisor_link
} // namespace WebSocketProxy
} // namespace GUI
} // namespace Slic3r

#endif /* slic3r_WebSocketProxyAdvisor_hpp_ */
