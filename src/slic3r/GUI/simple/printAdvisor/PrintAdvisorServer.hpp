#ifndef slic3r_GUI_PrintAdvisorServer_hpp_
#define slic3r_GUI_PrintAdvisorServer_hpp_

#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include "libslic3r/Preset.hpp"
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace Slic3r {
namespace GUI {

// Thrown by the constructor when another running MentorSlicer already serves the MCP
// endpoint: this instance keeps the server off without changing the shared setting.
struct PrintAdvisorOtherInstance : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Local authenticated bridge used by the Print Advisor MCP sidecar.
// MCP itself lives in the sidecar; profile mutations use approved, allowlisted copies.
class PrintAdvisorServer
{
public:
    PrintAdvisorServer();
    ~PrintAdvisorServer();

    PrintAdvisorServer(const PrintAdvisorServer&) = delete;
    PrintAdvisorServer& operator=(const PrintAdvisorServer&) = delete;

    void stop();

    // Flag set when the server stops: dialogs opened on its behalf must not apply anything
    // afterwards (ask_user checks it). Shared with the device-page preheat button.
    std::shared_ptr<std::atomic<bool>> stop_flag() const { return m_callback_stopping; }

private:
    void run();
    // One endpoint of the local API. Handlers of the server's own routes take the raw body;
    // routes of the other Print Advisor modules (advisor::AdvisorRoute) are wrapped to parse it.
    struct Route {
        std::string path;
        std::string method; // "GET" | "POST"
        bool runs_on_gui{true};
        // Changes something (usually after a confirmation dialog): refused while another modal
        // window of MentorSlicer is open.
        bool mutating{false};
        std::function<nlohmann::json(const std::string& body, const std::atomic<bool>& stopping)> handler;
        // Takes the single GUI request slot (refused with 409 while another such request runs).
        // False for read-only routes, which may also run while a confirmation dialog is open.
        bool exclusive{true};
    };

    void build_routes();
    // Serves one connection on its own worker thread (see run()).
    void handle_connection(boost::asio::ip::tcp::socket socket);
    // Joins the workers that finished (all of them when `all`, after the stop request).
    void reap_workers(bool all);
    // `client` is the connection's socket, polled while waiting so a request whose client went
    // away is cancelled before anything is applied.
    std::string dispatch_gui_request(const Route& route, const std::string& body, boost::asio::detail::socket_type client);
    nlohmann::json context();
    nlohmann::json create_proposal(const std::string& body);
    nlohmann::json approve_proposal(const std::string& body);
    nlohmann::json rollback_operation(const std::string& body);
    nlohmann::json apply_layer_schedule(const std::string& body);
    nlohmann::json slice_result();
    nlohmann::json rename_copy(const std::string& body);
    nlohmann::json printer_status();
    nlohmann::json print_control(const std::string& body);
    nlohmann::json preheat(const std::string& body);
    nlohmann::json start_calibration(const std::string& body);
    nlohmann::json preflight();
    nlohmann::json model_analysis();
    nlohmann::json arrange_or_orient(const std::string& body);
    nlohmann::json object_settings(const std::string& body);
    nlohmann::json list_profiles();
    nlohmann::json profile_diff(const std::string& body);
    nlohmann::json delete_profile(const std::string& body);
    nlohmann::json export_profile(const std::string& body);
    nlohmann::json history();
    // Throws when another running MentorSlicer already published a live endpoint.
    void check_no_other_instance();
    void write_descriptor();
    void remove_descriptor();
    // Drops proposals older than their 10-minute expiry.
    void prune_proposals();

    struct PendingProposal {
        Preset::Type type{Preset::TYPE_INVALID};
        std::string class_name;
        std::string source_name;
        std::string new_name; // empty: generated "(Advisor <time>)" name
        std::string reason;
        DynamicPrintConfig source_config;
        std::string printer_name;
        DynamicPrintConfig printer_config;
        std::string other_name;
        DynamicPrintConfig other_config;
        Semver source_version;
        std::string source_custom_defined;
        std::vector<std::pair<std::string, std::string>> changes;
        std::chrono::steady_clock::time_point created_at{std::chrono::steady_clock::now()};
    };
    struct AppliedOperation {
        Preset::Type type{Preset::TYPE_INVALID};
        std::string class_name;
        std::string created_name;
        DynamicPrintConfig expected_config;
        size_t disk_fingerprint{0};
    };
    // Built in the constructor, read-only afterwards (read by every worker thread).
    std::vector<Route> m_routes;
    // Touched only by route handlers, which all run on the GUI thread (CallAfter): no lock.
    std::map<std::string, PendingProposal> m_pending_proposals;
    std::map<std::string, AppliedOperation> m_applied_operations;
    std::shared_ptr<std::atomic<bool>> m_callback_stopping{std::make_shared<std::atomic<bool>>(false)};
    // Set while a GUI request is queued or running on the GUI thread (cleared by the GUI
    // callback, also after a timeout): new GUI requests get HTTP 409 instead of stacking
    // inside an open confirmation dialog.
    std::shared_ptr<std::atomic<bool>> m_gui_busy{std::make_shared<std::atomic<bool>>(false)};
    // Connections being served, so stop() can shut them down to unblock pending reads and
    // writes, and the worker threads serving them. Both guarded by m_connections_mutex.
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex m_connections_mutex;
    std::set<boost::asio::detail::socket_type> m_active_sockets;
    std::list<Worker> m_workers;

    boost::asio::io_context m_io;
    boost::asio::ip::tcp::acceptor m_acceptor;
    boost::thread m_thread;
    boost::asio::ip::port_type m_port{0};
    std::string m_token;
    std::string m_descriptor_path;
    std::atomic<bool> m_stopping{false};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_PrintAdvisorServer_hpp_
