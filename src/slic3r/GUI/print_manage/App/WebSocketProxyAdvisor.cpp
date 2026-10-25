#include "WebSocketProxyAdvisor.hpp"
#include "DeviceTelemetryCache.h"

#include <algorithm>

namespace Slic3r {
namespace GUI {
namespace WebSocketProxy {
namespace advisor_link {

namespace {

// Live proxies that SendToDevice can reach. Entries expire with their proxy.
struct DeviceSenderRegistry
{
    std::mutex                               mutex;
    std::vector<std::weak_ptr<DeviceSender>> senders;
};

// Never destroyed, like DeviceTelemetryCache and the Print Advisor monitor: SendToDevice may be
// called from another thread while the process exits, after static destructors have started.
DeviceSenderRegistry& device_senders()
{
    static DeviceSenderRegistry* registry = new DeviceSenderRegistry();
    return *registry;
}

} // namespace

void register_device_sender(const std::shared_ptr<DeviceSender>& sender)
{
    DeviceSenderRegistry& registry = device_senders();
    std::lock_guard<std::mutex> lock(registry.mutex);
    registry.senders.erase(std::remove_if(registry.senders.begin(), registry.senders.end(),
                               [](const std::weak_ptr<DeviceSender>& item) { return item.expired(); }),
                           registry.senders.end());
    registry.senders.push_back(sender);
}

bool DeviceHosts::contains(const std::string& host) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return std::find(m_hosts.begin(), m_hosts.end(), host) != m_hosts.end();
}

void DeviceHosts::publish(std::vector<std::string> hosts)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_hosts = std::move(hosts);
}

void on_device_message(const std::string& host, std::string_view payload)
{
    DeviceTelemetryCache::instance().update(host, payload);
}

void on_device_disconnected(const std::string& host)
{
    DeviceTelemetryCache::instance().mark_disconnected(host);
}

} // namespace advisor_link

bool SendToDevice(const std::string& host, const std::string& text)
{
    if (host.empty())
        return false;
    std::vector<std::shared_ptr<advisor_link::DeviceSender>> senders;
    {
        advisor_link::DeviceSenderRegistry& registry = advisor_link::device_senders();
        std::lock_guard<std::mutex> lock(registry.mutex);
        for (const std::weak_ptr<advisor_link::DeviceSender>& item : registry.senders)
            if (auto sender = item.lock())
                senders.push_back(std::move(sender));
    }
    // Outside the lock: the last reference may be released here, destroying a proxy.
    for (const auto& sender : senders)
        if (sender->SendToDeviceHost(host, text))
            return true;
    return false;
}

} // namespace WebSocketProxy
} // namespace GUI
} // namespace Slic3r
