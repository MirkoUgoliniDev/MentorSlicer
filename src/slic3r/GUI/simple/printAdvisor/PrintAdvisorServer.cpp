#include "PrintAdvisorServer.hpp"
#include "AdvisorConfirmDialog.hpp"
#include "PrintAdvisorInternal.hpp"
#include "PrintAdvisorHooks.hpp"
#include "PrintAdvisorPageBridge.hpp"
#include "slic3r/GUI/I18N.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/simple/bridge/SlicerAction.hpp"
#include "slic3r/GUI/simple/bridge/SlicerBridge.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/print_manage/App/DeviceTelemetryCache.h"
#include "slic3r/GUI/print_manage/App/PrintAdvisorMonitor.hpp"
#include "slic3r/GUI/print_manage/App/PrinterMgrView.hpp"
#include "slic3r/GUI/print_manage/data/DataCenter.hpp"
#include "slic3r/GUI/GUI_Factories.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ParamsPanel.hpp"
#include "slic3r/GUI/Jobs/Job.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "libslic3r/calib.hpp"
#include <wx/filedlg.h>
#include <wx/filename.h>
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r/CustomGCode.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/GCode/GCodeProcessor.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/DataDirectoryMigration.hpp" // LEGACY_CREALITY_* (PRINT-ADVISOR F8)

#include <boost/algorithm/string/trim.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>
#include <wx/dialog.h>
#include <wx/evtloop.h>
#include <wx/msgdlg.h>
#include <wx/stdpaths.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <climits>
#include <cmath>
#include <future>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifndef _WIN32
#include <cerrno>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Slic3r {
namespace GUI {

using namespace advisor;

namespace {

namespace beast = boost::beast;
namespace http = beast::http;
using tcp = boost::asio::ip::tcp;
using json = nlohmann::json;

std::string make_session_token()
{
    std::random_device random;
    std::ostringstream token;
    for (int i = 0; i < 4; ++i)
        token << std::hex << std::setw(8) << std::setfill('0') << random();
    return token.str();
}

bool constant_time_equal(const std::string& left, const std::string& right)
{
    if (left.size() != right.size())
        return false;
    unsigned char difference = 0;
    for (size_t i = 0; i < left.size(); ++i)
        difference |= static_cast<unsigned char>(left[i] ^ right[i]);
    return difference == 0;
}

std::string bearer_token(const http::request<http::string_body>& request)
{
    const auto it = request.find(http::field::authorization);
    if (it == request.end())
        return {};
    const std::string value(it->value().data(), it->value().size());
    static const std::string prefix = "Bearer ";
    return value.compare(0, prefix.size(), prefix) == 0 ? value.substr(prefix.size()) : std::string();
}

std::string request_path(const std::string& target)
{
    const size_t query = target.find('?');
    return target.substr(0, query);
}

std::string make_operation_id()
{
    return make_session_token();
}

// Refusal reported as HTTP 409 Conflict: the request may succeed later, unchanged.
struct ConflictError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

// Connections served at the same time; more get HTTP 503 at once.
constexpr size_t max_connections = 8;

// GUI thread: number of exclusive route handlers running (nested only through event loops).
int& exclusive_handler_depth()
{
    static int depth = 0;
    return depth;
}

class ExclusiveHandlerScope
{
public:
    explicit ExclusiveHandlerScope(bool exclusive) : m_exclusive(exclusive)
    {
        if (m_exclusive)
            ++exclusive_handler_depth();
    }
    ~ExclusiveHandlerScope()
    {
        if (m_exclusive)
            --exclusive_handler_depth();
    }
    ExclusiveHandlerScope(const ExclusiveHandlerScope&) = delete;
    ExclusiveHandlerScope& operator=(const ExclusiveHandlerScope&) = delete;

private:
    bool m_exclusive;
};

// Response bodies: text from profiles, 3MF names or printer pages may hold invalid UTF-8, which
// would make dump() throw (and an exception thrown while reporting an error terminates).
std::string dump_body(const json& value)
{
    return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

using socket_handle = boost::asio::detail::socket_type;

// tcp_stream::expires_after applies only to asynchronous operations: synchronous reads and
// writes are bounded with socket timeouts.
void set_socket_timeout(socket_handle handle, int option, long long milliseconds)
{
#ifdef _WIN32
    const DWORD value = DWORD(std::max<long long>(milliseconds, 1));
    ::setsockopt(handle, SOL_SOCKET, option, reinterpret_cast<const char*>(&value), sizeof(value));
#else
    const timeval value{time_t(milliseconds / 1000), suseconds_t((milliseconds % 1000) * 1000)};
    ::setsockopt(handle, SOL_SOCKET, option, &value, sizeof(value));
#endif
}

// True when the client closed the connection or it failed: a non-blocking peek reports the end
// of the stream (0) or a hard error. Pending data or nothing to read mean the client is still
// waiting for the reply (the sidecar never half-closes its side).
bool client_disconnected(socket_handle handle)
{
    char byte = 0;
#ifdef _WIN32
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(handle, &readable);
    timeval zero{0, 0};
    const int ready = ::select(0, &readable, nullptr, nullptr, &zero);
    if (ready == SOCKET_ERROR)
        return ::WSAGetLastError() != WSAEINTR && ::WSAGetLastError() != WSAEINPROGRESS;
    if (ready == 0)
        return false;
    const int received = ::recv(handle, &byte, 1, MSG_PEEK);
    if (received > 0)
        return false;
    if (received == 0)
        return true;
    const int error = ::WSAGetLastError();
    return error != WSAEWOULDBLOCK && error != WSAETIMEDOUT && error != WSAEINTR && error != WSAEINPROGRESS;
#else
    pollfd descriptor{handle, POLLIN, 0};
    const int ready = ::poll(&descriptor, 1, 0);
    if (ready < 0)
        return errno != EINTR;
    if (ready == 0)
        return false;
    if (descriptor.revents & (POLLERR | POLLNVAL))
        return true;
    const ssize_t received = ::recv(handle, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
    if (received > 0)
        return false;
    if (received == 0)
        return true;
    return errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR;
#endif
}

// Answers HTTP 503 to a connection beyond max_connections without reading its request, then
// drains what the client sent for a moment (closing with unread data would reset the
// connection and lose the reply). Runs on the accept thread: every wait is short.
void reject_busy(tcp::socket& socket)
{
    static const std::string body = R"({"success":false,"error":"Print Advisor is busy with other requests: retry in a few seconds"})";
    const std::string response = "HTTP/1.1 503 Service Unavailable\r\nContent-Type: application/json; charset=utf-8\r\n"
                                 "Cache-Control: no-store\r\nRetry-After: 2\r\nConnection: close\r\nContent-Length: " +
                                 std::to_string(body.size()) + "\r\n\r\n" + body;
    boost::system::error_code error;
    set_socket_timeout(socket.native_handle(), SO_SNDTIMEO, 1000);
    set_socket_timeout(socket.native_handle(), SO_RCVTIMEO, 100);
    boost::asio::write(socket, boost::asio::buffer(response), error);
    socket.shutdown(tcp::socket::shutdown_send, error);
    char sink[4096];
    for (int i = 0; i < 16 && !error; ++i)
        socket.read_some(boost::asio::buffer(sink), error);
    socket.close(error);
}

unsigned long current_process_id()
{
#ifdef _WIN32
    return ::GetCurrentProcessId();
#else
    return static_cast<unsigned long>(::getpid());
#endif
}

// True when a process with this id is running (best effort).
bool process_alive(unsigned long process_id)
{
#ifdef _WIN32
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(process_id));
    if (!process)
        return ::GetLastError() == ERROR_ACCESS_DENIED;
    DWORD exit_code = 0;
    const bool alive = ::GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
    ::CloseHandle(process);
    return alive;
#else
    return ::kill(pid_t(process_id), 0) == 0 || errno == EPERM;
#endif
}

// True when something accepts a TCP connection on 127.0.0.1:port within 300 ms.
bool loopback_port_answers(unsigned short port)
{
    try {
        boost::asio::io_context io;
        tcp::socket socket(io);
        bool connected = false;
        socket.async_connect(tcp::endpoint(boost::asio::ip::address_v4::loopback(), port),
                             [&connected](const boost::system::error_code& error) { connected = !error; });
        io.run_for(std::chrono::milliseconds(300));
        boost::system::error_code ignored;
        socket.close(ignored);
        return connected;
    } catch (...) {
        return false;
    }
}

// True when the process runs the same executable as this one (file name compared without
// case). False when that cannot be told (no access, process gone, unsupported platform): a
// process id reused after a crash must not block the server.
#ifdef _WIN32
// Executable file name of a running process; empty when it cannot be told.
std::wstring process_image_name(DWORD id)
{
    HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, id);
    if (!process)
        return {};
    wchar_t buffer[4096];
    DWORD size = DWORD(sizeof(buffer) / sizeof(buffer[0]));
    std::wstring name;
    if (::QueryFullProcessImageNameW(process, 0, buffer, &size))
        name = boost::filesystem::path(std::wstring(buffer, size)).filename().wstring();
    ::CloseHandle(process);
    return name;
}
#endif

bool runs_same_executable(unsigned long process_id)
{
#ifdef _WIN32
    const std::wstring other = process_image_name(DWORD(process_id));
    const std::wstring own = process_image_name(::GetCurrentProcessId());
    return !other.empty() && !own.empty() && ::_wcsicmp(other.c_str(), own.c_str()) == 0;
#else
    const auto command_name = [](unsigned long id) {
        std::ifstream comm("/proc/" + std::to_string(id) + "/comm");
        std::string name;
        std::getline(comm, name);
        return name;
    };
    const std::string other = command_name(process_id);
    return !other.empty() && other == command_name(static_cast<unsigned long>(::getpid()));
#endif
}

// Print host credentials and anything that looks like a secret never leave MentorSlicer.
bool is_secret_key(std::string key)
{
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (key.rfind("printhost_", 0) == 0 || key.rfind("print_host", 0) == 0)
        return true;
    for (const char* word : {"password", "apikey", "api_key", "token", "secret"})
        if (key.find(word) != std::string::npos)
            return true;
    return false;
}

// Removes secret keys at any depth, also from {"key": <name>, ...} entries of arrays.
void redact_secrets(json& value)
{
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end();) {
            if (is_secret_key(it.key()))
                it = value.erase(it);
            else {
                redact_secrets(it.value());
                ++it;
            }
        }
    } else if (value.is_array()) {
        for (auto it = value.begin(); it != value.end();) {
            if (it->is_object() && it->contains("key") && (*it)["key"].is_string() &&
                is_secret_key((*it)["key"].get<std::string>()))
                it = value.erase(it);
            else {
                redact_secrets(*it);
                ++it;
            }
        }
    }
}

// modal_ui_active() (PrintAdvisorInternal.cpp) tells whether a modal window or the "Send to
// printer" dialog is open: a mutating request would otherwise run inside its event loop
// (CallAfter is also processed by nested loops) or change what that dialog is sending.

// The current request was cancelled (timeout) or the server is stopping: nothing may be applied.
void check_still_wanted(const std::shared_ptr<std::atomic<bool>>& stopping, const std::shared_ptr<std::atomic<bool>>& cancelled)
{
    if (stopping && stopping->load())
        throw std::runtime_error("Print Advisor is stopping");
    throw_if_request_cancelled(cancelled);
}

bool preset_config_equal(const DynamicPrintConfig& left, const DynamicPrintConfig& right)
{
    return left.diff(right).empty();
}

// Any numeric, percent, boolean or enum setting of a filament/process profile can be tuned.
// Free text (G-code, notes, names, compatibility conditions, inherits) and points are never
// writable, nor are keys that describe the material identity or reassign extruders.
bool advisor_key_writable(const std::string& key)
{
    static const std::unordered_set<std::string> denied = {
        "filament_diameter", "filament_is_support", "filament_soluble",
        "nozzle_temperature_range_low", "nozzle_temperature_range_high",
        "required_nozzle_HRC", "filament_adhesiveness_category", "temperature_vitrification",
        "wall_filament", "sparse_infill_filament", "solid_infill_filament",
        "support_filament", "support_interface_filament", "wipe_tower_filament", "print_extruder_id",
        "wiping_volumes_extruders", "filament_minimal_purge_on_wipe_tower",
        "wipe_tower_x", "wipe_tower_y", "wipe_tower_rotation_angle",
        // Printing object by object can make the gantry hit printed parts.
        "print_sequence",
        // Makes the G-code set the nozzle temperature per layer from a flow/temperature graph,
        // outside the filament range check of nozzle_temperature.
        "material_flow_dependent_temperature", "material_flow_temp_graph"
    };
    if (denied.count(key))
        return false;
    const ConfigOptionDef* def = print_config_def.get(key);
    if (!def || def->readonly || def->mode == comDevelop || def->printer_technology == ptSLA)
        return false;
    switch (def->type) {
    case coFloat: case coFloats:
    case coInt: case coInts:
    case coPercent: case coPercents:
    case coFloatOrPercent: case coFloatsOrPercents:
    case coBool: case coBools:
    case coEnum:
        return true;
    default:
        return false;
    }
}

// The schema limits are UI limits (nozzle temperature up to 1500 °C, many keys unbounded),
// so absolute values are also checked against physically plausible FFF bounds.
// Relative (percent) values are only checked against the schema limits.
void check_advisor_safety(const std::string& key, double value, const DynamicPrintConfig& source, const MachineLimits& limits)
{
    struct Bounds { double min; double max; };
    static const std::unordered_map<std::string, Bounds> exact = {
        {"nozzle_temperature", {150, 350}}, {"nozzle_temperature_initial_layer", {150, 350}},
        {"chamber_temperature", {0, 80}}, {"idle_temperature", {0, 300}},
        {"filament_flow_ratio", {0.5, 1.5}}, {"pressure_advance", {0, 2}},
        {"filament_retraction_length", {0, 10}}, {"filament_max_volumetric_speed", {0.5, 60}},
        {"layer_height", {0.04, 0.6}}, {"initial_layer_print_height", {0.04, 0.6}},
        {"heat_soak_time", {0, 60}}, {"heat_soak_bed_temp", {0, 120}}, {"brim_layers", {1, 10}},
        {"brim_layers_object_gap", {0, 2}}
    };
    const auto ends_with = [&key](const std::string& suffix) {
        return key.size() >= suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    Bounds bounds{-1, -1};
    if (auto it = exact.find(key); it != exact.end())
        bounds = it->second;
    else if (ends_with("plate_temp") || ends_with("plate_temp_initial_layer"))
        bounds = {0, 120};
    else if (ends_with("_speed") && key.find("fan") == std::string::npos)
        bounds = {0, 1000};
    else if (ends_with("_acceleration"))
        bounds = {0, 50000};
    else if (ends_with("_jerk"))
        bounds = {0, 100};
    if (bounds.max >= 0 && (value < bounds.min || value > bounds.max))
        throw std::runtime_error("Value " + format_number(value) + " is outside the Print Advisor safety bounds [" +
            format_number(bounds.min) + ", " + format_number(bounds.max) + "]: " + key);

    const auto ends_with_key = [&key](const std::string& suffix) {
        return key.size() >= suffix.size() && key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    const auto check_limit = [&](double limit, const char* what) {
        if (limit >= 0 && value > limit)
            throw std::runtime_error(std::string("The selected printer reaches at most ") + std::to_string(int(limit)) +
                " C for the " + what + " (" + limits.source + "): " + key);
    };
    if (ends_with_key("plate_temp") || ends_with_key("plate_temp_initial_layer") || key == "heat_soak_bed_temp")
        check_limit(limits.max_bed_temp, "bed");
    if (key == "nozzle_temperature" || key == "nozzle_temperature_initial_layer")
        check_limit(limits.max_nozzle_temp, "nozzle");
    if (key == "chamber_temperature")
        check_limit(limits.max_chamber_temp, "chamber");

    // Nozzle temperatures must stay inside the range declared by the filament profile itself.
    if (key == "nozzle_temperature" || key == "nozzle_temperature_initial_layer") {
        const auto* low = source.option<ConfigOptionInts>("nozzle_temperature_range_low");
        const auto* high = source.option<ConfigOptionInts>("nozzle_temperature_range_high");
        if (low && high && !low->values.empty() && !high->values.empty() &&
            low->values.front() > 0 && high->values.front() > low->values.front() &&
            (value < low->values.front() || value > high->values.front()))
            throw std::runtime_error("Nozzle temperature must stay within the filament range " +
                std::to_string(low->values.front()) + "-" + std::to_string(high->values.front()) + " °C: " + key);
    }
}

// Read-only printer limits and capabilities given to the advisor, so recommendations
// respect the machine (speeds, accelerations, nozzle, fans, chamber).
const char* const advisor_machine_keys[] = {
    "printer_model", "printer_variant", "nozzle_diameter", "nozzle_type", "printable_height",
    "min_layer_height", "max_layer_height",
    "machine_max_speed_x", "machine_max_speed_y", "machine_max_speed_z", "machine_max_speed_e",
    "machine_max_acceleration_x", "machine_max_acceleration_y", "machine_max_acceleration_z",
    "machine_max_acceleration_e", "machine_max_acceleration_extruding",
    "machine_max_acceleration_retracting", "machine_max_acceleration_travel",
    "machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z", "machine_max_jerk_e",
    "retraction_length", "retraction_speed", "deretraction_speed", "z_hop", "z_hop_types",
    "auxiliary_fan", "support_chamber_temp_control", "support_air_filtration", "bed_temperature_mode",
    "single_extruder_multi_material", "gcode_flavor"
};

// First line of every custom G-code block created by Print Advisor, so its own schedule
// can be replaced without touching blocks added by the user.
const std::string advisor_schedule_marker = "; Print Advisor schedule";

json advisor_profile_values(const DynamicPrintConfig& config, const std::vector<std::string>& keys,
    const std::vector<std::string>& other_class_keys)
{
    json values = json::object();
    for (const std::string& key : keys) {
        // Keys shared by both profile classes have no single owner and stay read-only.
        if (std::find(other_class_keys.begin(), other_class_keys.end(), key) != other_class_keys.end() || !advisor_key_writable(key))
            continue;
        const ConfigOption* option = config.option(key);
        if (!option)
            continue;
        if (const auto* vector = dynamic_cast<const ConfigOptionVectorBase*>(option)) {
            const auto components = vector->vserialize();
            json array = json::array();
            for (size_t i = 0; i < components.size(); ++i)
                array.push_back(vector->is_nil(i) ? json(nullptr) : json(components[i]));
            values[key] = std::move(array);
        } else {
            values[key] = option->serialize();
        }
    }
    return values;
}

// Profile names become file names: keep them short and free of path characters.
std::string validate_profile_name(std::string name)
{
    boost::algorithm::trim(name);
    if (name.size() < 3 || name.size() > 80)
        throw std::runtime_error("Profile name must be between 3 and 80 characters");
    for (const unsigned char c : name)
        if (c < 32 || std::string("<>:\"/\\|?*").find(char(c)) != std::string::npos)
            throw std::runtime_error("Profile name contains a character that is not allowed in file names");
    if (name.back() == '.')
        throw std::runtime_error("Profile name cannot end with a dot");
    // Windows device names cannot be used as file names, with or without an extension.
    std::string stem = name.substr(0, name.find('.'));
    boost::algorithm::trim(stem);
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    static const std::unordered_set<std::string> reserved = {
        "CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
        "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    if (reserved.count(stem))
        throw std::runtime_error("Profile name is a reserved Windows device name");
    return name;
}

// PresetCollection::delete_preset erases the preset without moving the selected index, so a
// preset stored before the selected one would shift the selection; keep it on the same preset.
bool delete_preset_keep_selection(PresetCollection& collection, const std::string& name)
{
    const auto& presets = collection();
    size_t deleted = presets.size();
    for (size_t i = 0; i < presets.size(); ++i)
        if (presets[i].name == name) {
            deleted = i;
            break;
        }
    const size_t selected = collection.get_selected_idx();
    if (deleted == presets.size() || !collection.delete_preset(name))
        return false;
    if (selected != size_t(-1) && deleted < selected)
        collection.set_idx_selected(selected - 1);
    return true;
}

size_t file_fingerprint(const boost::filesystem::path& path)
{
    boost::filesystem::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot read saved profile for verification");
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (contents.empty())
        throw std::runtime_error("Saved profile is empty");
    return std::hash<std::string>{}(contents);
}

// Entries of the rotated backup first, then of the current file (oldest first).
std::vector<json> read_history(size_t limit)
{
    std::vector<json> entries;
    for (const boost::filesystem::path& path : {history_backup_path(), history_path()}) {
        boost::filesystem::ifstream file(path, std::ios::binary);
        std::string line;
        while (std::getline(file, line)) {
            json entry = json::parse(line, nullptr, false);
            if (entry.is_object())
                entries.push_back(std::move(entry));
        }
    }
    if (entries.size() > limit)
        entries.erase(entries.begin(), entries.end() - limit);
    return entries;
}

// Names the history shows as created or renamed by Print Advisor and not deleted since.
std::unordered_set<std::string> advisor_history_names(const std::string& class_name)
{
    std::unordered_set<std::string> names;
    for (const json& entry : read_history(size_t(-1))) {
        if (entry.value("profile_class", "") != class_name)
            continue;
        const std::string action = entry.value("action", "");
        if (action == "created" || action == "renamed")
            names.insert(entry.value("profile", ""));
        if (action == "deleted")
            names.erase(entry.value("profile", ""));
        if (action == "renamed")
            names.erase(entry.value("from", ""));
    }
    return names;
}

// A profile counts as created by Print Advisor when its name still has the generated suffix,
// or when the history shows it was created or renamed to that name and not deleted since.
bool advisor_created(const std::string& name, const std::unordered_set<std::string>& history_names)
{
    return name.find("(Advisor ") != std::string::npos || history_names.count(name) > 0;
}

bool advisor_created(const std::string& class_name, const std::string& name)
{
    return advisor_created(name, advisor_history_names(class_name));
}

// Identity of the objects in the project, to tell whether a calibration generator ran.
std::vector<size_t> model_object_ids(Plater& plater)
{
    std::vector<size_t> ids;
    for (const ModelObject* object : plater.model().objects)
        ids.push_back(object ? object->id().id : 0);
    return ids;
}

// Throws when the profile is selected or assigned to an extruder (filament slots of the plate).
void check_profile_unused(const PresetBundle& bundle, Preset::Type type, const std::string& name)
{
    const PresetCollection& collection = type == Preset::TYPE_FILAMENT ? bundle.filaments : bundle.prints;
    if (collection.get_selected_preset_name() == name)
        throw std::runtime_error("The profile is selected; select another one first");
    if (type == Preset::TYPE_FILAMENT &&
        std::find(bundle.filament_presets.begin(), bundle.filament_presets.end(), name) != bundle.filament_presets.end())
        throw std::runtime_error("The profile is used by an extruder; select another one first");
}

// Z of every layer of the current plate's last slice, in G-code order: object and support
// layers merged like GCode::collect_layers_to_print (print_z closer than EPSILON is one layer).
// Throws when the plate is not sliced or the layers do not match the G-code layer count.
std::vector<double> sliced_layer_z(Plater& plater)
{
    auto& plate_list = plater.get_partplate_list();
    PartPlate* plate = plate_list.get_curr_plate();
    const GCodeProcessorResult* gcode = plate_list.get_current_slice_result();
    if (!plate || !plate->is_slice_result_valid() || !gcode)
        throw std::runtime_error("Il piatto non è sezionato o lo slicing non è aggiornato: esegui lo slicing e riprova "
                                 "(servono le quote Z reali dei layer)");
    std::vector<double> zs;
    for (const PrintObject* object : plate_list.get_current_fff_print().objects()) {
        if (!object)
            continue;
        for (const Layer* layer : object->layers())
            zs.push_back(layer->print_z);
        for (const SupportLayer* layer : object->support_layers())
            zs.push_back(layer->print_z);
    }
    std::sort(zs.begin(), zs.end());
    std::vector<double> merged;
    for (const double z : zs)
        if (merged.empty() || z > merged.back() + EPSILON)
            merged.push_back(z);
    const size_t gcode_layers =
        gcode->print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].layers_times.size();
    if (merged.empty() || (gcode_layers > 0 && gcode_layers != merged.size()))
        throw std::runtime_error("Non riesco a ricavare con certezza la quota Z di ogni layer dall'ultimo slicing: "
                                 "esegui di nuovo lo slicing e riprova");
    return merged;
}

// 0-based index of the layer a custom G-code at print_z runs on: the first layer at or above it.
size_t layer_index_at(const std::vector<double>& zs, double print_z)
{
    const auto it = std::lower_bound(zs.begin(), zs.end(), print_z - EPSILON);
    return size_t(it - zs.begin());
}

// Per-object overrides as text, to tell whether they changed while a dialog was open.
std::map<std::string, std::string> object_overrides(const ModelObject& object)
{
    std::map<std::string, std::string> values;
    for (const std::string& key : object.config.keys())
        if (const ConfigOption* option = object.config.option(key))
            values[key] = option->serialize();
    return values;
}

struct SavedCopy {
    DynamicPrintConfig config;
    size_t fingerprint{0};
};

// Saves a new, unselected user profile like "Save as" does: it inherits from parent_name and
// the file holds only the differences from that parent.
// parent_name and version are taken by value: callers may pass fields of a preset stored in
// the collection, which load_preset can move.
SavedCopy save_user_copy(PresetCollection& collection, Preset::Type type, const std::string& name,
    DynamicPrintConfig config, std::string parent_name, Semver version, bool custom_defined)
{
    if (collection.find_preset(name, false))
        throw std::runtime_error("A profile with this name already exists: " + name);
    config.set_key_value("inherits", new ConfigOptionString(parent_name));
    if (type == Preset::TYPE_FILAMENT)
        config.option<ConfigOptionStrings>("filament_settings_id", true)->values[0] = name;
    else
        config.option<ConfigOptionString>("print_settings_id", true)->value = name;

    const std::string path = collection.path_from_name(name);
    if (boost::filesystem::exists(path))
        throw std::runtime_error("A profile file with this name already exists: " + name);
    Preset& created = collection.load_preset(path, name, std::move(config), false, version, custom_defined);
    created.vendor = nullptr;
    created.alias.clear();
    created.renamed_from.clear();
    created.setting_id.clear();
    created.filament_id.clear();
    created.user_id.clear();
    created.sync_info.clear();
    created.base_id.clear();
    created.is_default = false;
    created.is_system = false;
    created.is_external = false;
    created.is_project_embedded = false;
    created.is_visible = true;
    created.is_dirty = false;
    Preset* parent = created.inherits().empty() ? nullptr : collection.find_preset(created.inherits(), false, true);
    if (!created.inherits().empty() && !parent) {
        delete_preset_keep_selection(collection, name);
        throw std::runtime_error("Parent profile not found: " + parent_name);
    }
    if (parent) {
        created.base_id = parent->setting_id;
        // Inheriting presets take the material id from their parent, as on load.
        created.filament_id = parent->filament_id;
    }
    try {
        created.save(parent ? &parent->config : nullptr);
    } catch (...) {
        delete_preset_keep_selection(collection, name);
        throw;
    }
    SavedCopy saved;
    try {
        if (!boost::filesystem::exists(path))
            throw std::runtime_error("The user profile copy could not be verified on disk");
        saved.fingerprint = file_fingerprint(path);
    } catch (...) {
        delete_preset_keep_selection(collection, name);
        throw;
    }
    saved.config = collection.find_preset(name, false)->config;
    return saved;
}

} // namespace

PrintAdvisorServer::PrintAdvisorServer()
    : m_acceptor(m_io)
    , m_token(make_session_token())
{
    check_no_other_instance();
    build_routes();
    boost::system::error_code error;
    const tcp::endpoint endpoint(boost::asio::ip::address_v4::loopback(), 0);
    m_acceptor.open(endpoint.protocol(), error);
    if (error) throw boost::system::system_error(error);
    m_acceptor.bind(endpoint, error);
    if (error) throw boost::system::system_error(error);
    m_acceptor.listen(boost::asio::socket_base::max_listen_connections, error);
    if (error) throw boost::system::system_error(error);
    m_port = m_acceptor.local_endpoint().port();

    write_descriptor();
    m_thread = boost::thread([this] { run(); });
    PrintAdvisorMonitor::instance().set_server_running(true);
}

PrintAdvisorServer::~PrintAdvisorServer()
{
    stop();
}

void PrintAdvisorServer::stop()
{
    if (m_stopping.exchange(true))
        return;
    m_callback_stopping->store(true);
    PrintAdvisorMonitor::instance().set_server_running(false);
    PrintAdvisorMonitor::instance().disarm_all();
    {
        // Unblock the reads and writes in progress on the worker threads. Only shutdown here:
        // each worker owns its socket and closes it, after removing the handle under this mutex.
        // A worker that registers after this sees m_stopping (set above) and gives up.
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        for (const socket_handle handle : m_active_sockets) {
#ifdef _WIN32
            ::shutdown(handle, SD_BOTH);
#else
            ::shutdown(handle, SHUT_RDWR);
#endif
        }
    }
    boost::system::error_code ignored;
    m_acceptor.cancel(ignored);
    m_acceptor.close(ignored);
    if (m_thread.joinable())
        m_thread.join();
    // No worker is started after the accept thread ended. Workers waiting for the GUI thread
    // poll the stop flag every 50 ms; the others are bounded by the socket timeouts.
    reap_workers(true);
    remove_descriptor();
}

void PrintAdvisorServer::reap_workers(bool all)
{
    std::list<Worker> finished;
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        for (auto it = m_workers.begin(); it != m_workers.end();) {
            auto next = std::next(it);
            if (all || it->done->load())
                finished.splice(finished.end(), m_workers, it);
            it = next;
        }
    }
    for (Worker& worker : finished)
        if (worker.thread.joinable())
            worker.thread.join();
}

// Accept thread: every connection is served on its own worker thread, so a confirmation dialog
// that keeps one request waiting does not hold the others. A second changing request then gets
// HTTP 409 at once (m_gui_busy), and read-only routes keep answering while a dialog is open.
void PrintAdvisorServer::run()
{
    while (!m_stopping.load()) {
        boost::system::error_code error;
        tcp::socket socket(m_io);
        m_acceptor.accept(socket, error);
        if (m_stopping.load())
            break;
        if (error) {
            // A persistent error (for example no free handles) must not spin a core.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }
        try {
            reap_workers(false);
            bool full = false;
            {
                std::lock_guard<std::mutex> lock(m_connections_mutex);
                if (m_stopping.load())
                    break;
                full = m_workers.size() >= max_connections;
                if (!full) {
                    // The slot exists before the thread starts, so a running thread is never
                    // left unowned (a joinable std::thread destroyed unjoined terminates).
                    m_workers.emplace_back();
                    Worker& worker = m_workers.back();
                    worker.done = std::make_shared<std::atomic<bool>>(false);
                    try {
                        worker.thread = std::thread([this, done = worker.done, connection = std::move(socket)]() mutable {
                            try {
                                handle_connection(std::move(connection));
                            } catch (const std::exception& exception) {
                                BOOST_LOG_TRIVIAL(error) << "Print Advisor connection failed: " << exception.what();
                            } catch (...) {
                                BOOST_LOG_TRIVIAL(error) << "Print Advisor connection failed";
                            }
                            done->store(true);
                        });
                    } catch (...) {
                        m_workers.pop_back();
                        throw;
                    }
                }
            }
            if (full)
                reject_busy(socket);
        } catch (const std::exception& exception) {
            // For example no thread could be started: the connection is closed unanswered.
            BOOST_LOG_TRIVIAL(error) << "Print Advisor could not serve a connection: " << exception.what();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (...) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void PrintAdvisorServer::handle_connection(tcp::socket socket)
{
    // SO_RCVTIMEO bounds each recv only, so the read loop below also enforces a total deadline:
    // a client sending a byte at a time cannot hold a worker (and app shutdown, which joins it).
    const socket_handle handle = socket.native_handle();
    set_socket_timeout(handle, SO_SNDTIMEO, 4000);
    beast::tcp_stream stream(std::move(socket));
    {
        std::lock_guard<std::mutex> lock(m_connections_mutex);
        if (m_stopping.load())
            return;
        m_active_sockets.insert(handle);
    }
    // Declared after the stream: forgets the handle before the stream closes the socket, so
    // stop() never shuts down a handle the system may have reused.
    struct ActiveSocketGuard {
        PrintAdvisorServer* server;
        socket_handle handle;
        ~ActiveSocketGuard()
        {
            std::lock_guard<std::mutex> lock(server->m_connections_mutex);
            server->m_active_sockets.erase(handle);
        }
    } active_socket_guard{this, handle};

    beast::flat_buffer buffer;
    http::request_parser<http::string_body> parser;
    parser.header_limit(4096);
    // Sized for the largest proposal the sidecar schema allows (40 changes of 32 components).
    parser.body_limit(131072);
    boost::system::error_code error;
    const auto read_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!parser.is_done()) {
        if (m_stopping.load())
            return;
        const long long remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            read_deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0)
            return;
        // Short per-recv timeout too, so a stop request is noticed even if shutdown() does not
        // wake the blocked recv. A local client sends a request in one go.
        set_socket_timeout(handle, SO_RCVTIMEO, std::min<long long>(remaining, 2000));
        http::read_some(stream, buffer, parser, error);
        if (error)
            return;
    }

    const auto request = parser.release();
    http::response<http::string_body> response;
    response.version(request.version());
    response.keep_alive(false);
    response.set(http::field::content_type, "application/json; charset=utf-8");
    response.set(http::field::cache_control, "no-store");

    const auto host = request.find(http::field::host);
    const std::string expected_host = "127.0.0.1:" + std::to_string(m_port);
    if (host == request.end() || std::string(host->value().data(), host->value().size()) != expected_host) {
        response.result(http::status::forbidden);
        response.body() = R"({"success":false,"error":"invalid_host"})";
    } else if (!constant_time_equal(bearer_token(request), m_token)) {
        response.result(http::status::unauthorized);
        response.body() = R"({"success":false,"error":"unauthorized"})";
    } else {
        const std::string path = request_path(std::string(request.target().data(), request.target().size()));
        // GET routes take no body, POST routes require one.
        const char* method = request.method() == http::verb::get && request.body().empty() ? "GET" :
                             request.method() == http::verb::post && !request.body().empty() ? "POST" : nullptr;
        const auto route = method == nullptr ? m_routes.end() :
            std::find_if(m_routes.begin(), m_routes.end(), [&](const Route& item) { return item.path == path && item.method == method; });
        if (route == m_routes.end()) {
            response.result(http::status::method_not_allowed);
            response.body() = R"({"success":false,"error":"unsupported_method_or_route"})";
        } else {
            try {
                response.result(http::status::ok);
                if (route->runs_on_gui) {
                    response.body() = dispatch_gui_request(*route, request.body(), handle);
                } else {
                    // Worker thread: the handler polls the stop flag while it waits. Such
                    // handlers may run on several workers at once (file reads, camera frames).
                    if (m_stopping.load())
                        throw std::runtime_error("Print Advisor is stopping");
                    const auto callback_stopping = m_callback_stopping;
                    response.body() = dump_body(route->handler(request.body(), *callback_stopping));
                }
            } catch (const ConflictError& exception) {
                response.result(http::status::conflict);
                response.body() = dump_body(json{{"success", false}, {"error", exception.what()}});
            } catch (const std::exception& exception) {
                response.result(http::status::bad_request);
                response.body() = dump_body(json{{"success", false}, {"error", exception.what()}});
            } catch (...) {
                response.result(http::status::internal_server_error);
                response.body() = R"({"success":false,"error":"internal_error"})";
            }
        }
    }

    response.prepare_payload();
    http::write(stream, response, error);
    stream.socket().shutdown(tcp::socket::shutdown_both, error);
}

void PrintAdvisorServer::build_routes()
{
    // POST routes on the GUI thread change something, except these read-only ones.
    // /v1/print_verdict writes the print log: it is refused while a modal window (such as the
    // end-of-print verdict dialog, which writes the same log) is open.
    static const std::unordered_set<std::string> read_only_posts = {
        "/v1/proposals", "/v1/profile_diff", "/v1/preflight", "/v1/overhang_regions", "/v1/support_volumes"};
    const auto is_mutating = [](const std::string& path, const std::string& method) {
        return method == "POST" && !read_only_posts.count(path);
    };
    // Routes that only read: they do not take the single GUI request slot, so they are not
    // refused while a confirmation dialog of another request is open. GET routes, and these POSTs.
    // /v1/proposals only validates and stores a proposal in memory (no preset is touched), so
    // parallel previews are not refused.
    static const std::unordered_set<std::string> shared_posts = {
        "/v1/profile_diff", "/v1/preflight", "/v1/overhang_regions", "/v1/support_volumes", "/v1/print_log",
        "/v1/proposals"};
    const auto is_exclusive = [](const std::string& path, const std::string& method) {
        return method == "POST" && !shared_posts.count(path);
    };
    const auto add = [this, is_mutating, is_exclusive](const char* path, const char* method, std::function<json(const std::string&)> handler) {
        m_routes.push_back({path, method, true, is_mutating(path, method),
                            [handler](const std::string& body, const std::atomic<bool>&) { return handler(body); }});
        m_routes.back().exclusive = is_exclusive(path, method);
    };
    add("/v1/context", "GET", [this](const std::string&) { return context(); });
    add("/v1/schema", "GET", [](const std::string&) {
        json payload;
        payload["success"] = true;
        payload["options"] = Bridge::SlicerBridge::BuildConfigSchemaArray();
        return payload;
    });
    add("/v1/slice_result", "GET", [this](const std::string&) { return slice_result(); });
    add("/v1/printer_status", "GET", [this](const std::string&) { return printer_status(); });
    add("/v1/model_analysis", "GET", [this](const std::string&) { return model_analysis(); });
    add("/v1/profiles", "GET", [this](const std::string&) { return list_profiles(); });
    add("/v1/history", "GET", [this](const std::string&) { return history(); });
    add("/v1/proposals", "POST", [this](const std::string& body) { return create_proposal(body); });
    add("/v1/approve", "POST", [this](const std::string& body) { return approve_proposal(body); });
    add("/v1/rollback", "POST", [this](const std::string& body) { return rollback_operation(body); });
    add("/v1/layer_schedule", "POST", [this](const std::string& body) { return apply_layer_schedule(body); });
    // Slices the current plate in the background with the active settings.
    // The result is read from /v1/slice_result.
    add("/v1/slice", "POST", [](const std::string&) {
        return Bridge::SlicerBridge::Instance().Execute(Bridge::ActionID::START_SLICE, json::object());
    });
    add("/v1/rename", "POST", [this](const std::string& body) { return rename_copy(body); });
    add("/v1/print_control", "POST", [this](const std::string& body) { return print_control(body); });
    add("/v1/preheat", "POST", [this](const std::string& body) { return preheat(body); });
    add("/v1/calibration", "POST", [this](const std::string& body) { return start_calibration(body); });
    add("/v1/preflight", "POST", [this](const std::string&) { return preflight(); });
    add("/v1/arrange", "POST", [this](const std::string& body) { return arrange_or_orient(body); });
    add("/v1/object_settings", "POST", [this](const std::string& body) { return object_settings(body); });
    add("/v1/profile_diff", "POST", [this](const std::string& body) { return profile_diff(body); });
    add("/v1/delete_profile", "POST", [this](const std::string& body) { return delete_profile(body); });
    add("/v1/export_profile", "POST", [this](const std::string& body) { return export_profile(body); });

    // Routes of the other Print Advisor modules. Their handlers receive the parsed body (an
    // empty object for GET routes).
    const std::vector<std::vector<AdvisorRoute>> groups = {
        printer_control_routes(), monitor_routes(), supports_routes(), camera_routes(), plate_routes(),
        archive_routes()};
    for (const std::vector<AdvisorRoute>& group : groups) {
        for (const AdvisorRoute& route : group) {
            if ((route.method != "GET" && route.method != "POST") || route.path.rfind("/v1/", 0) != 0 || !route.handler)
                throw std::logic_error("Invalid Print Advisor route: " + route.method + " " + route.path);
            for (const Route& existing : m_routes)
                if (existing.path == route.path && existing.method == route.method)
                    throw std::logic_error("Duplicate Print Advisor route: " + route.method + " " + route.path);
            auto handler = route.handler;
            if (route.path == "/v1/print_verdict" && route.method == "POST") {
                // One verdict per print: a second one (MCP or the end-of-print dialog already
                // answered) is refused instead of adding a contradicting line.
                handler = [inner = route.handler](const json& body, const std::atomic<bool>& stopping) {
                    if (body.is_object() && body.contains("name") && body["name"].is_string() &&
                        print_log_has_verdict(body["name"].get<std::string>()))
                        throw std::runtime_error("A verdict is already recorded for this print log; it cannot be changed from here");
                    json result = inner(body, stopping);
                    // The end-of-print question about this print is answered now.
                    if (result.value("success", false))
                        verdict_recorded_by_client(body["name"].get<std::string>());
                    return result;
                };
            }
            m_routes.push_back({route.path, route.method, route.runs_on_gui, route.runs_on_gui && is_mutating(route.path, route.method),
                [handler](const std::string& body, const std::atomic<bool>& stopping) {
                    return handler(body.empty() ? json::object() : json::parse(body), stopping);
                }});
            m_routes.back().exclusive = is_exclusive(route.path, route.method);
        }
    }
}

// Snapshot of the active printer, filament and process profiles and of the current plate.
nlohmann::json PrintAdvisorServer::context()
{
    auto& bridge = Bridge::SlicerBridge::Instance();
    json payload;
    const auto captured_at = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    payload["context_id"] = "cp-" + std::to_string(captured_at);
    payload["captured_at"] = captured_at;
    payload["presets"] = bridge.Execute(Bridge::ActionID::GET_PRESETS, json::object());
    payload["edited_config"] = bridge.Execute(Bridge::ActionID::GET_EDITED_CONFIG, json::object());
    if (wxGetApp().preset_bundle) {
        auto* bundle = wxGetApp().preset_bundle;
        const auto& printer = bundle->printers.get_edited_preset();
        const auto& filament = bundle->filaments.get_edited_preset();
        const auto& process = bundle->prints.get_edited_preset();
        payload["profile_identity"] = {
            {"printer", printer.name},
            {"filament", filament.name},
            {"process", process.name}
        };
        const ConfigOption* material_type = filament.config.option("filament_type");
        if (material_type)
            payload["profile_identity"]["filament_type"] = material_type->serialize();
        payload["profile_identity"]["filament_parent"] = filament.inherits();
        payload["profile_identity"]["process_parent"] = process.inherits();
        json machine = json::object();
        for (const char* key : advisor_machine_keys)
            if (const ConfigOption* option = printer.config.option(key))
                machine[key] = option->serialize();
        payload["machine"] = std::move(machine);
        payload["machine_limits"] = machine_limits(printer.config).to_json();
        if (Plater* plater = wxGetApp().plater()) {
            if (PartPlate* plate = plater->get_partplate_list().get_curr_plate()) {
                const BedType bed_type = plate->get_bed_type(true);
                payload["bed"] = {{"plate_type", bed_type_to_gcode_string(bed_type)},
                                  {"temp_key", get_bed_temp_key(bed_type)},
                                  {"initial_layer_temp_key", get_bed_temp_1st_layer_key(bed_type)}};
                payload["plate_number"] = plater->get_partplate_list().get_curr_plate_index() + 1;
            }
            json items = json::array();
            for (const CustomGCode::Item& item : plater->model().get_curr_plate_custom_gcodes().gcodes)
                items.push_back({{"print_z", item.print_z}, {"type", int(item.type)},
                                 {"advisor", item.extra.rfind(advisor_schedule_marker, 0) == 0},
                                 {"gcode", truncate_utf8_bytes(item.extra, 300)}});
            payload["plate_layer_gcodes"] = std::move(items);
        }
        payload["editable_profile_values"] = {
            {"filament", advisor_profile_values(filament.config, Preset::filament_options(), Preset::print_options())},
            {"process", advisor_profile_values(process.config, Preset::print_options(), Preset::filament_options())}
        };
    }
    payload["success"] = payload["presets"].value("success", false) &&
        payload["edited_config"].value("success", false);
    // The context goes to the LLM: print host credentials and secrets stay in MentorSlicer.
    redact_secrets(payload);
    return payload;
}

std::string PrintAdvisorServer::dispatch_gui_request(const Route& route, const std::string& body, socket_handle client)
{
    if (m_stopping.load() || !wxTheApp)
        throw std::runtime_error("Print Advisor is stopping");
    // A client that already gave up must not have its request queued at all.
    if (client_disconnected(client))
        throw std::runtime_error("The client closed the connection: request not executed");

    // One changing GUI request at a time. While a dialog keeps one open (or after a timeout,
    // until that dialog ends) a new one is refused at once: it would run inside the dialog's
    // event loop. Read-only routes do not take the slot: running inside a dialog's event loop is
    // harmless for them, and each connection has its own worker, so they are answered meanwhile.
    const bool exclusive = route.exclusive;
    if (exclusive && m_gui_busy->exchange(true))
        throw ConflictError("Un'altra richiesta è in corso in MentorSlicer");

    const auto callback_stopping = m_callback_stopping;
    const auto busy = m_gui_busy;
    // Set when this server stops waiting for the reply: the handler must not apply anything.
    const auto cancelled = std::make_shared<std::atomic<bool>>(false);
    const auto handler = route.handler;
    const bool mutating = route.mutating;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(600);
    const auto stop_waiting_if_needed = [&] {
        if (m_callback_stopping->load()) {
            cancelled->store(true);
            throw std::runtime_error("Print Advisor is stopping");
        }
        // The client gave up (its own timeout, or it was closed): it will never see the reply,
        // so nothing may be applied any more. A dialog already open stays open; confirming it
        // then applies nothing (ask_user checks the flag).
        if (client_disconnected(client)) {
            cancelled->store(true);
            throw std::runtime_error("The client closed the connection: request cancelled, nothing applied");
        }
    };

    // A read-only request may be dispatched by a nested event loop (a dialog, or YieldFor while
    // a project loads) in the middle of an exclusive handler. It runs there only while that
    // handler waits for the user with nothing half-applied (advisor_user_wait_depth); otherwise
    // the callback reports `deferred` and the request is posted again 50 ms later.
    for (;;) {
        auto result = std::make_shared<std::promise<std::string>>();
        auto future = result->get_future();
        const auto deferred = std::make_shared<std::atomic<bool>>(false);
        wxGetApp().CallAfter([callback_stopping, busy, cancelled, deferred, result, handler, body, mutating, exclusive] {
            std::string value;
            std::exception_ptr error;
            if (callback_stopping->load())
                error = std::make_exception_ptr(std::runtime_error("Print Advisor is stopping"));
            else if (cancelled->load())
                error = std::make_exception_ptr(std::runtime_error("Richiesta scaduta: nessuna modifica applicata"));
            else if (mutating && modal_ui_active())
                error = std::make_exception_ptr(ConflictError("Chiudi le finestre aperte in MentorSlicer e riprova"));
            else if (!exclusive && exclusive_handler_depth() > 0 && advisor_user_wait_depth() == 0)
                deferred->store(true);
            else {
                try {
                    ScopedRequestFlag request_flag(cancelled);
                    ExclusiveHandlerScope exclusive_scope(exclusive);
                    value = dump_body(handler(body, *callback_stopping));
                } catch (...) {
                    error = std::current_exception();
                }
            }
            // Free the slot before replying, so the client's next request is not refused.
            if (exclusive)
                busy->store(false);
            try {
                if (error)
                    result->set_exception(error);
                else
                    result->set_value(std::move(value));
            } catch (...) {}
        });

        while (future.wait_for(std::chrono::milliseconds(50)) != std::future_status::ready) {
            stop_waiting_if_needed();
            if (std::chrono::steady_clock::now() >= deadline) {
                cancelled->store(true);
                // A handler that passed its last check just before the flag was set finishes
                // synchronously: give it a moment, so its result is not reported as a timeout.
                if (future.wait_for(std::chrono::seconds(2)) == std::future_status::ready && !deferred->load())
                    return future.get();
                throw std::runtime_error("MentorSlicer non ha risposto entro 10 minuti (finestra di conferma ancora aperta?): "
                                         "richiesta scaduta, nessuna modifica verrà applicata");
            }
        }
        std::string value = future.get(); // rethrows the handler's error
        if (!deferred->load())
            return value;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        stop_waiting_if_needed();
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("MentorSlicer è rimasto occupato da un'altra richiesta per 10 minuti: lettura non eseguita");
    }
}

void PrintAdvisorServer::prune_proposals()
{
    const auto now = std::chrono::steady_clock::now();
    for (auto it = m_pending_proposals.begin(); it != m_pending_proposals.end();)
        it = now - it->second.created_at > std::chrono::minutes(10) ? m_pending_proposals.erase(it) : std::next(it);
}

nlohmann::json PrintAdvisorServer::create_proposal(const std::string& body)
{
    prune_proposals();
    if (m_pending_proposals.size() >= 32)
        throw std::runtime_error("Too many pending proposals; apply them or wait up to 10 minutes until they expire");
    const json request = json::parse(body);
    const std::string class_name = request.at("profile_class").get<std::string>();
    if (class_name != "filament" && class_name != "process")
        throw std::runtime_error("Only filament and process profiles can be updated");
    if (!request.contains("changes") || !request["changes"].is_array() || request["changes"].empty() || request["changes"].size() > 40)
        throw std::runtime_error("A proposal must contain between 1 and 40 changes");
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        throw std::runtime_error("PresetBundle is unavailable");

    PresetCollection& collection = class_name == "filament" ? bundle->filaments : bundle->prints;
    const Preset& source = collection.get_edited_preset();
    if (source.is_default || source.is_project_embedded)
        throw std::runtime_error("Choose a saved profile before proposing an update");
    // Unsaved edits (for example left by a calibration test) would end up in the copy.
    if (source.is_dirty)
        throw std::runtime_error("The selected profile has unsaved changes in MentorSlicer (a calibration test also changes it); "
                                 "select the profile again discarding the changes, then retry");
    // An external profile (loaded from a 3MF or config file) is not kept across restarts,
    // so a copy inheriting from it would lose its parent.
    if (source.is_external)
        throw std::runtime_error("The selected profile was loaded from a file; save it as a user profile in MentorSlicer first");

    std::unordered_set<std::string> allowed;
    const auto& keys = class_name == "filament" ? Preset::filament_options() : Preset::print_options();
    const auto& other_keys = class_name == "filament" ? Preset::print_options() : Preset::filament_options();
    allowed.insert(keys.begin(), keys.end());
    for (const std::string& key : other_keys)
        allowed.erase(key);
    PendingProposal proposal;
    proposal.type = class_name == "filament" ? Preset::TYPE_FILAMENT : Preset::TYPE_PRINT;
    proposal.class_name = class_name;
    proposal.source_name = source.name;
    if (request.contains("reason")) {
        if (!request["reason"].is_string() || request["reason"].get<std::string>().size() > 500)
            throw std::runtime_error("Proposal reason must be plain text up to 500 characters");
        proposal.reason = request["reason"].get<std::string>();
    }
    if (request.contains("profile_name") && !request["profile_name"].is_null()) {
        if (!request["profile_name"].is_string())
            throw std::runtime_error("profile_name must be a string");
        proposal.new_name = validate_profile_name(request["profile_name"].get<std::string>());
        if (collection.find_preset(proposal.new_name, false))
            throw std::runtime_error("A profile with this name already exists: " + proposal.new_name);
    }
    proposal.source_config = source.config;
    proposal.source_version = source.version;
    proposal.source_custom_defined = source.custom_defined;
    const Preset& active_printer = bundle->printers.get_edited_preset();
    const Preset& other = class_name == "filament" ? bundle->prints.get_edited_preset() : bundle->filaments.get_edited_preset();
    proposal.printer_name = active_printer.name;
    proposal.printer_config = active_printer.config;
    const MachineLimits limits = machine_limits(active_printer.config);
    proposal.other_name = other.name;
    proposal.other_config = other.config;

    json changes = json::array();
    std::unordered_set<std::string> seen_keys;
    for (const auto& change : request["changes"]) {
        const std::string key = change.at("key").get<std::string>();
        if (!seen_keys.insert(key).second)
            throw std::runtime_error("A parameter may appear only once: " + key);
        if (!allowed.count(key))
            throw std::runtime_error("Parameter does not belong to the selected profile class: " + key);
        if (!advisor_key_writable(key))
            throw std::runtime_error("Parameter is not writable by Print Advisor (free text, identity or extruder assignment): " + key);
        const ConfigOptionDef* def = print_config_def.get(key);
        const ConfigOption* source_option = source.config.option(key);
        const auto* source_vector = source_option ? dynamic_cast<const ConfigOptionVectorBase*>(source_option) : nullptr;
        if (!def || def->mode == comDevelop || def->printer_technology == ptSLA)
            throw std::runtime_error("Parameter is not an editable FFF setting: " + key);
        if (!source_option)
            throw std::runtime_error("Parameter is not present in the selected profile: " + key);
        if (bool(source_vector) == def->is_scalar())
            throw std::runtime_error("Profile parameter shape does not match the schema: " + key);
        const auto& input = change.at("value");
        std::string serialized;
        if (input.is_string()) serialized = input.get<std::string>();
        else if (input.is_boolean()) serialized = input.get<bool>() ? "1" : "0";
        else if (input.is_number()) serialized = input.dump();
        else if (input.is_array()) {
            for (const auto& component : input) {
                if (!serialized.empty()) serialized += ',';
                if (component.is_null()) serialized += "nil";
                else if (component.is_string()) {
                    const std::string component_text = component.get<std::string>();
                    if (component_text.empty() || component_text.find(',') != std::string::npos)
                        throw std::runtime_error("Vector components cannot be empty or contain commas: " + key);
                    serialized += component_text;
                }
                else if (component.is_boolean()) serialized += component.get<bool>() ? "1" : "0";
                else if (component.is_number()) serialized += component.dump();
                else throw std::runtime_error("Vector components must be scalar values or null: " + key);
            }
        }
        else throw std::runtime_error("Parameter value must be a scalar: " + key);
        if (serialized.empty() || serialized.size() > 256)
            throw std::runtime_error("Parameter value is empty or too long: " + key);

        DynamicPrintConfig parsed;
        parsed.set_deserialize_strict(key, serialized);
        const ConfigOption* option = parsed.option(key);
        if (!option)
            throw std::runtime_error("Could not parse parameter: " + key);
        const auto* parsed_vector = dynamic_cast<const ConfigOptionVectorBase*>(option);
        if (source_vector) {
            if (!parsed_vector)
                throw std::runtime_error("A vector value is required for this profile parameter: " + key);
            const auto old_components = source_vector->vserialize();
            const auto new_components = parsed_vector->vserialize();
            if (old_components.size() != new_components.size())
                throw std::runtime_error("Vector length must match the source profile (extruder count cannot be changed): " + key);
            for (size_t index = 0; index < old_components.size(); ++index) {
                if (source_vector->is_nil(index) != parsed_vector->is_nil(index))
                    throw std::runtime_error("Vector null slots must be preserved: " + key);
                if (parsed_vector->is_nil(index) || def->type == coBools)
                    continue;
                std::string component_text = new_components[index];
                const bool is_percent = def->type == coPercents ||
                    (def->type == coFloatsOrPercents && !component_text.empty() && component_text.back() == '%');
                if (!component_text.empty() && component_text.back() == '%')
                    component_text.pop_back();
                // Config values always use '.'; std::stod follows the GUI locale (',' in Italian).
                std::istringstream component_stream(component_text);
                component_stream.imbue(std::locale::classic());
                double numeric = 0;
                component_stream >> numeric;
                if (component_stream.fail() || !component_stream.eof() || !std::isfinite(numeric) ||
                    (!(def->min == INT_MIN && def->max == INT_MAX) && (numeric < def->min || numeric > def->max)))
                    throw std::runtime_error("Vector component is outside the allowed range: " + key);
                if (!is_percent)
                    check_advisor_safety(key, numeric, source.config, limits);
            }
        } else if (parsed_vector) {
            throw std::runtime_error("A scalar value is required for this profile parameter: " + key);
        } else if (def->type == coInt || def->type == coFloat || def->type == coPercent || def->type == coFloatOrPercent || def->type == coEnum) {
            try {
                const double numeric = (def->type == coInt || def->type == coEnum) ? option->getInt() : option->getFloat();
                if (!std::isfinite(numeric) || numeric < def->min || numeric > def->max)
                    throw std::runtime_error("Parameter is outside its allowed range: " + key);
                const auto* float_or_percent = dynamic_cast<const ConfigOptionFloatOrPercent*>(option);
                const bool is_percent = def->type == coPercent || (float_or_percent && float_or_percent->percent);
                if (def->type != coEnum && !is_percent)
                    check_advisor_safety(key, numeric, source.config, limits);
            } catch (const BadOptionTypeException&) {
                // Strict deserialization is the validator for non-numeric scalar types.
            }
        }
        const std::string old_value = source_option->serialize();
        const std::string new_value = option->serialize();
        if (old_value == new_value)
            continue;
        proposal.changes.emplace_back(key, new_value);
        changes.push_back({{"key", key}, {"old_value", old_value}, {"new_value", new_value}});
    }
    if (proposal.changes.empty())
        throw std::runtime_error("All requested values are already set");

    const std::string id = make_operation_id();
    const std::string reason = proposal.reason;
    const std::string profile_name = proposal.new_name;
    m_pending_proposals.emplace(id, std::move(proposal));
    return {{"success", true}, {"proposal_id", id}, {"profile_class", class_name},
            {"source_profile", source.name}, {"changes", changes}, {"reason", reason},
            {"profile_name", profile_name.empty() ? json(nullptr) : json(profile_name)},
            {"requires_in_app_approval", true}, {"expires_in_seconds", 600}};
}

nlohmann::json PrintAdvisorServer::approve_proposal(const std::string& body)
{
    const std::string id = json::parse(body).at("proposal_id").get<std::string>();
    prune_proposals();
    auto it = m_pending_proposals.find(id);
    if (it == m_pending_proposals.end())
        throw std::runtime_error("Proposal is unknown, expired (10 minutes) or already consumed; create a fresh preview");
    PendingProposal proposal = std::move(it->second);
    m_pending_proposals.erase(it);
    if (std::chrono::steady_clock::now() - proposal.created_at > std::chrono::minutes(10))
        throw std::runtime_error("Proposal expired; create a fresh preview");
    if (m_applied_operations.size() >= 32)
        throw std::runtime_error("Too many tracked profile copies; restart Print Advisor before creating another");
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        throw std::runtime_error("PresetBundle is unavailable");
    PresetCollection& collection = proposal.type == Preset::TYPE_FILAMENT ? bundle->filaments : bundle->prints;
    Preset& source = collection.get_edited_preset();
    const Preset& active_printer = bundle->printers.get_edited_preset();
    const Preset& other = proposal.type == Preset::TYPE_FILAMENT ? bundle->prints.get_edited_preset() : bundle->filaments.get_edited_preset();
    if (source.name != proposal.source_name || !preset_config_equal(source.config, proposal.source_config) ||
        active_printer.name != proposal.printer_name || !preset_config_equal(active_printer.config, proposal.printer_config) ||
        other.name != proposal.other_name || !preset_config_equal(other.config, proposal.other_config))
        throw std::runtime_error("The active source profile changed after preview; create a new proposal");

    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string new_name = proposal.new_name.empty()
        ? proposal.source_name + " (Advisor " + std::to_string(now) + ")"
        : proposal.new_name;
    std::vector<AdvisorChangeRow> rows;
    for (const auto& change : proposal.changes) {
        const ConfigOptionDef* def = print_config_def.get(change.first);
        const std::string unit = def && !def->sidetext.empty() ? " " + def->sidetext : std::string();
        // Translated full name, as in the settings tabs, followed by the key.
        std::string name = def ? (!def->full_label.empty() ? def->full_label : def->label) : std::string();
        if (!name.empty())
            name = _(wxString::FromUTF8(name)).ToUTF8().data();
        const std::string label = name.empty() ? change.first : name + " (" + change.first + ")";
        rows.push_back({label, proposal.source_config.option(change.first)->serialize() + unit, change.second + unit});
    }
    std::string summary = "Salvare una copia del profilo " + proposal.class_name + "?\n\n" +
                          "Profilo di partenza: " + sanitized_text(proposal.source_name) + "\nNuova copia: " + sanitized_text(new_name) + "\n";
    {
        const Preset& source_now = collection.get_edited_preset();
        const std::string parent_label = collection.is_base_preset(source_now) ? source_now.name : source_now.inherits();
        summary += "Profilo padre: " + (parent_label.empty() ? std::string("nessuno") : sanitized_text(parent_label)) + "\n";
    }
    if (const std::string reason = reason_text(proposal.reason); !reason.empty())
        summary += "\n" + reason + "\n";
    summary += "\nIl profilo originale resta invariato e la copia non viene selezionata.";
    if (!ask_user("Salva copia del profilo", summary, m_callback_stopping, rows))
        return {{"success", false}, {"cancelled", true}, {"message", "User cancelled in MentorSlicer"}};
    const Preset& confirmed_source = collection.get_edited_preset();
    const Preset& confirmed_printer = bundle->printers.get_edited_preset();
    const Preset& confirmed_other = proposal.type == Preset::TYPE_FILAMENT ? bundle->prints.get_edited_preset() : bundle->filaments.get_edited_preset();
    if (confirmed_source.name != proposal.source_name || !preset_config_equal(confirmed_source.config, proposal.source_config) ||
        confirmed_printer.name != proposal.printer_name || !preset_config_equal(confirmed_printer.config, proposal.printer_config) ||
        confirmed_other.name != proposal.other_name || !preset_config_equal(confirmed_other.config, proposal.other_config))
        throw std::runtime_error("The active profiles changed while the approval dialog was open; create a fresh preview");

    DynamicPrintConfig new_config = proposal.source_config;
    for (const auto& change : proposal.changes)
        new_config.set_deserialize_strict(change.first, change.second);
    // Mirror PresetCollection::save_current_preset: a copy of a base profile (system, or a
    // user profile without parent) inherits from it; any other copy keeps the source's parent.
    // This keeps the copy linked to the system profile, so untouched values follow its updates.
    const std::string parent_name = collection.is_base_preset(confirmed_source) ? confirmed_source.name : confirmed_source.inherits();
    SavedCopy saved = save_user_copy(collection, proposal.type, new_name, std::move(new_config), parent_name,
        proposal.source_version, proposal.source_custom_defined == "1");

    const std::string operation_id = make_operation_id();
    AppliedOperation operation;
    operation.type = proposal.type;
    operation.class_name = proposal.class_name;
    operation.created_name = new_name;
    operation.expected_config = std::move(saved.config);
    operation.disk_fingerprint = saved.fingerprint;
    m_applied_operations.emplace(operation_id, std::move(operation));
    {
        json changed = json::object();
        for (const auto& change : proposal.changes)
            changed[change.first] = change.second;
        append_history({{"action", "created"}, {"profile_class", proposal.class_name}, {"profile", new_name},
                        {"source", proposal.source_name}, {"parent", parent_name}, {"changes", changed},
                        {"reason", proposal.reason}});
    }
    if (Tab* tab = wxGetApp().get_tab(proposal.type))
        tab->update_tab_ui();
    if (wxGetApp().plater())
        wxGetApp().plater()->sidebar().update_presets(proposal.type);
    return {{"success", true}, {"operation_id", operation_id}, {"created_profile", new_name},
            {"selected", false}, {"source_profile", proposal.source_name}};
}

nlohmann::json PrintAdvisorServer::rollback_operation(const std::string& body)
{
    const std::string id = json::parse(body).at("operation_id").get<std::string>();
    auto it = m_applied_operations.find(id);
    if (it == m_applied_operations.end())
        throw std::runtime_error("Operation is unknown or already rolled back");
    const AppliedOperation operation = it->second;
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        throw std::runtime_error("PresetBundle is unavailable");
    PresetCollection& collection = operation.type == Preset::TYPE_FILAMENT ? bundle->filaments : bundle->prints;
    Preset* target = collection.find_preset(operation.created_name);
    if (!target || target->is_system || target->is_default || target->is_project_embedded ||
        collection.get_selected_preset_name() == target->name || !preset_config_equal(target->config, operation.expected_config))
        throw std::runtime_error("Rollback refused: profile is missing, active, protected, or changed after creation");
    if (file_fingerprint(target->file) != operation.disk_fingerprint)
        throw std::runtime_error("Rollback refused: profile file changed after creation");
    for (const Preset& preset : collection())
        if (preset.name != target->name && preset.inherits() == target->name)
            throw std::runtime_error("Rollback refused: another profile now inherits from this copy");
    check_profile_unused(*bundle, operation.type, operation.created_name);
    const std::string question = "Eliminare la copia '" + sanitized_text(operation.created_name) +
                                 "'?\n\nIl profilo di partenza non viene toccato.";
    if (!ask_user("Annulla modifica", question, m_callback_stopping, {}, true))
        return {{"success", false}, {"cancelled", true}};
    target = collection.find_preset(operation.created_name);
    if (!target || target->is_system || target->is_default || target->is_project_embedded ||
        collection.get_selected_preset_name() == target->name || !preset_config_equal(target->config, operation.expected_config) ||
        file_fingerprint(target->file) != operation.disk_fingerprint)
        throw std::runtime_error("Rollback refused: profile changed during the confirmation dialog");
    for (const Preset& preset : collection())
        if (preset.name != target->name && preset.inherits() == target->name)
            throw std::runtime_error("Rollback refused: another profile now inherits from this copy");
    check_profile_unused(*bundle, operation.type, operation.created_name);
    if (!delete_preset_keep_selection(collection, operation.created_name))
        throw std::runtime_error("MentorSlicer could not remove the copied profile");
    m_applied_operations.erase(id);
    append_history({{"action", "deleted"}, {"profile_class", operation.class_name}, {"profile", operation.created_name},
                    {"via", "rollback"}});
    if (Tab* tab = wxGetApp().get_tab(operation.type))
        tab->update_tab_ui();
    if (wxGetApp().plater())
        wxGetApp().plater()->sidebar().update_presets(operation.type);
    return {{"success", true}, {"rolled_back_profile", operation.created_name}};
}

// Programs temperature and case-fan changes at specific layers through the plate's custom
// G-code (the same data as "Add custom G-code" on the preview layer slider). Part and
// auxiliary fans are recomputed by the cooling logic on every layer, so they are set via
// the filament profile instead.
nlohmann::json PrintAdvisorServer::apply_layer_schedule(const std::string& body)
{
    const json request = json::parse(body);
    if (!request.contains("entries") || !request["entries"].is_array() || request["entries"].size() > 30)
        throw std::runtime_error("A schedule must contain between 0 and 30 entries");
    auto* bundle = wxGetApp().preset_bundle;
    Plater* plater = wxGetApp().plater();
    if (!bundle || !plater)
        throw std::runtime_error("MentorSlicer is not ready");
    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    if (!plate)
        throw std::runtime_error("No plate is selected");
    if (plate->get_real_print_seq() == PrintSequence::ByObject)
        throw std::runtime_error("Layer commands are ignored when printing object by object");

    const DynamicPrintConfig& filament = bundle->filaments.get_edited_preset().config;
    // Real layer heights of the last slice (variable layer height, adaptive layers, rafts and
    // several objects make a constant-height formula wrong).
    const std::vector<double> layer_z = sliced_layer_z(*plater);
    const std::string case_fan = bundle->is_cx_vendor() ? "P1" : "P3";
    const MachineLimits limits = machine_limits(bundle->printers.get_edited_preset().config);

    const auto read_value = [](const json& entry, const char* key, double min, double max, double& value) {
        if (!entry.contains(key) || entry[key].is_null())
            return false;
        if (!entry[key].is_number())
            throw std::runtime_error(std::string("Value must be a number: ") + key);
        value = entry[key].get<double>();
        if (!std::isfinite(value) || value < min || value > max)
            throw std::runtime_error(std::string("Value is outside the Print Advisor safety bounds: ") + key);
        return true;
    };

    std::map<int, std::string> schedule;
    std::string summary;
    for (const auto& entry : request["entries"]) {
        if (!entry.is_object() || !entry.contains("layer") || !entry["layer"].is_number_integer())
            throw std::runtime_error("Every entry needs an integer layer number");
        const long long requested_layer = entry["layer"].get<long long>();
        // Layer 1 is driven by the *_initial_layer settings of the profiles.
        if (requested_layer < 2 || requested_layer > (long long)layer_z.size())
            throw std::runtime_error("Il layer deve essere tra 2 e " + std::to_string(layer_z.size()) +
                                     " (layer dell'ultimo slicing); il layer 1 usa le impostazioni del primo layer dei profili");
        const int layer = int(requested_layer);
        if (schedule.count(layer))
            throw std::runtime_error("A layer may appear only once: " + std::to_string(layer));
        std::string gcode;
        std::string label;
        double value = 0;
        if (read_value(entry, "bed_temp", 0, 120, value)) {
            check_advisor_safety("heat_soak_bed_temp", value, filament, limits);
            gcode += "M140 S" + std::to_string(int(std::lround(value))) + "\n";
            label += " piatto " + std::to_string(int(std::lround(value))) + "°C";
        }
        if (read_value(entry, "nozzle_temp", 150, 350, value)) {
            check_advisor_safety("nozzle_temperature", value, filament, limits);
            gcode += "M104 S" + std::to_string(int(std::lround(value))) + "\n";
            label += " ugello " + std::to_string(int(std::lround(value))) + "°C";
        }
        if (read_value(entry, "chamber_temp", 0, 80, value)) {
            // M141 only for printers that heat the chamber: the profile says so, or it is a
            // Creality K2 (its profiles ship support_chamber_temp_control = 0 although it has a
            // chamber heater). Other printers would ignore or reject the command.
            const DynamicPrintConfig& printer_config = bundle->printers.get_edited_preset().config;
            const auto* chamber_control = printer_config.option<ConfigOptionBool>("support_chamber_temp_control");
            const ConfigOption* model_option = printer_config.option("printer_model");
            // "Creality K2 Plus" and "K2 Plus" alike (normalized_model drops "Creality ").
            const bool k2_family = model_option && normalized_model(model_option->serialize()).rfind("k2", 0) == 0;
            if (!(chamber_control && chamber_control->value) && !k2_family)
                throw std::runtime_error("The selected printer cannot heat the chamber (support_chamber_temp_control is off and it is "
                                         "not a Creality K2); use heat_soak_time to warm the chamber with the bed");
            check_advisor_safety("chamber_temperature", value, filament, limits);
            gcode += "M141 S" + std::to_string(int(std::lround(value))) + "\n";
            label += " camera " + std::to_string(int(std::lround(value))) + "°C";
        }
        if (read_value(entry, "case_fan", 0, 100, value)) {
            gcode += "M106 " + case_fan + " S" + std::to_string(int(std::lround(value * 255.0 / 100.0))) + "\n";
            label += " ventola case " + std::to_string(int(std::lround(value))) + "%";
        }
        if (gcode.empty())
            throw std::runtime_error("Entry for layer " + std::to_string(layer) + " has no command");
        schedule.emplace(layer, gcode);
        const double print_z = layer_z[size_t(layer - 1)];
        std::ostringstream z_text;
        z_text.imbue(std::locale::classic());
        z_text << std::fixed << std::setprecision(2) << print_z;
        summary += "Layer " + std::to_string(layer) + " (Z " + z_text.str() + " mm):" + label + "\n";
    }

    Model& model = plater->model();
    const CustomGCode::Info original = model.get_curr_plate_custom_gcodes();
    CustomGCode::Info info = original;
    const size_t before = info.gcodes.size();
    info.gcodes.erase(std::remove_if(info.gcodes.begin(), info.gcodes.end(), [](const CustomGCode::Item& item) {
        return item.extra.rfind(advisor_schedule_marker, 0) == 0;
    }), info.gcodes.end());
    const bool replaces_previous = info.gcodes.size() != before;
    if (schedule.empty() && !replaces_previous)
        throw std::runtime_error("There is no Print Advisor schedule to remove on this plate");
    for (const auto& [layer, gcode] : schedule) {
        const double print_z = layer_z[size_t(layer - 1)];
        // The slicer runs one custom G-code block per layer.
        for (const CustomGCode::Item& item : info.gcodes)
            if (layer_index_at(layer_z, item.print_z) == size_t(layer - 1))
                throw std::runtime_error("Layer " + std::to_string(layer) + " already has a custom G-code added in MentorSlicer");
        CustomGCode::Item item;
        item.print_z = print_z;
        item.type = CustomGCode::Custom;
        item.extruder = 1;
        item.extra = advisor_schedule_marker + "\n" + gcode;
        info.gcodes.push_back(std::move(item));
    }
    std::sort(info.gcodes.begin(), info.gcodes.end());
    if (info.mode == CustomGCode::Undef)
        info.mode = CustomGCode::SingleExtruder;

    std::string question = schedule.empty() ? "Rimuovere la programmazione per layer di Print Advisor dal piatto corrente?\n"
                                            : "Programmazione per layer sul piatto corrente:\n\n" + summary;
    if (!schedule.empty() && replaces_previous)
        question += "\nSostituisce la programmazione precedente di Print Advisor.";
    if (const std::string reason = reason_text(request.value("reason", json())); !reason.empty())
        question += "\n\n" + reason;
    question += "\n\nI profili non vengono modificati.";
    if (!ask_user("Comandi per layer", question, m_callback_stopping))
        return {{"success", false}, {"cancelled", true}};
    if (model.get_curr_plate_custom_gcodes() != original)
        throw std::runtime_error("The plate custom G-code changed while the dialog was open; try again");
    if (sliced_layer_z(*plater) != layer_z)
        throw std::runtime_error("I layer sono cambiati mentre la finestra era aperta: esegui di nuovo lo slicing e riprova");

    model.plates_custom_gcodes[model.curr_plate_index] = info;
    {
        json logged = json::array();
        for (const auto& [layer, gcode] : schedule)
            logged.push_back({{"layer", layer}, {"gcode", gcode}});
        append_history({{"action", "layer_schedule"}, {"plate", model.curr_plate_index + 1}, {"entries", logged}});
    }
    plate->update_slice_result_valid_state(false);
    plater->set_plater_dirty(true);
    if (wxGetApp().mainframe)
        wxGetApp().mainframe->update_slice_print_status(MainFrame::SlicePrintEventType::eEventSliceUpdate, true, false);

    json applied = json::array();
    for (const auto& [layer, gcode] : schedule)
        applied.push_back({{"layer", layer}, {"print_z", layer_z[size_t(layer - 1)]}, {"gcode", gcode}});
    return {{"success", true}, {"applied", applied}, {"replaced_previous", replaces_previous},
            {"note", "Slice the plate again to include the commands in the G-code"}};
}

// Summary of the current plate's slice: time, material, layers, time per feature and the
// slicer's own warnings, so the effect of a change can be measured.
nlohmann::json PrintAdvisorServer::slice_result()
{
    Plater* plater = wxGetApp().plater();
    if (!plater)
        throw std::runtime_error("MentorSlicer is not ready");
    auto& plate_list = plater->get_partplate_list();
    PartPlate* plate = plate_list.get_curr_plate();
    json result = {{"success", true}, {"plate_number", plate_list.get_curr_plate_index() + 1}};
    const bool valid = plate && plate->is_slice_result_valid();
    result["sliced"] = valid;
    result["slicing_in_progress"] = plater->is_background_process_update_scheduled() || plater->is_background_process_slicing();
    const GCodeProcessorResult* gcode = plate_list.get_current_slice_result();
    if (!valid || !gcode)
        return result;

    const auto& statistics = plate_list.get_current_fff_print().print_statistics();
    const auto& normal = gcode->print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];
    result["estimated_time_s"] = normal.time;
    result["layer_count"] = normal.layers_times.size();
    result["filament_used_mm"] = statistics.total_used_filament;
    result["filament_used_g"] = statistics.total_weight;
    result["toolpath_outside_plate"] = gcode->toolpath_outside;
    try {
        result["critical_warning"] = plate->get_print().has_critical_warning_status();
    } catch (...) {
        result["critical_warning"] = nullptr;
    }

    json roles = json::array();
    for (const auto& [role, seconds] : normal.roles_times)
        if (seconds > 0)
            roles.push_back({{"feature", ExtrusionEntity::role_to_string(role)}, {"time_s", seconds}});
    result["time_per_feature"] = std::move(roles);

    json warnings = json::array();
    for (const auto& warning : gcode->warnings) {
        json item = {{"level", warning.level}, {"message", warning.msg}, {"code", warning.error_code}};
        if (!warning.params.empty())
            item["params"] = warning.params;
        warnings.push_back(std::move(item));
    }
    result["warnings"] = std::move(warnings);
    return result;
}

// Renames a profile copy created by Print Advisor: saves it under the new name with the same
// parent, moves the selection to it when it was in use, then deletes the old copy.
nlohmann::json PrintAdvisorServer::rename_copy(const std::string& body)
{
    const json request = json::parse(body);
    const std::string class_name = request.at("profile_class").get<std::string>();
    if (class_name != "filament" && class_name != "process")
        throw std::runtime_error("Only filament and process profiles can be renamed");
    const std::string current_name = request.at("current_name").get<std::string>();
    const std::string new_name = validate_profile_name(request.at("new_name").get<std::string>());
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        throw std::runtime_error("PresetBundle is unavailable");
    const Preset::Type type = class_name == "filament" ? Preset::TYPE_FILAMENT : Preset::TYPE_PRINT;
    PresetCollection& collection = type == Preset::TYPE_FILAMENT ? bundle->filaments : bundle->prints;

    const auto check = [&]() -> Preset& {
        Preset* target = collection.find_preset(current_name, false);
        if (!target || target->is_system || target->is_default || target->is_project_embedded || target->is_external)
            throw std::runtime_error("Profile not found or not a user profile: " + current_name);
        if (!advisor_created(class_name, target->name))
            throw std::runtime_error("Only profile copies created by Print Advisor can be renamed");
        for (const Preset& preset : collection())
            if (preset.inherits() == current_name)
                throw std::runtime_error("Another profile inherits from this copy");
        if (collection.find_preset(new_name, false))
            throw std::runtime_error("A profile with this name already exists: " + new_name);
        if (collection.get_edited_preset().name == current_name && collection.get_edited_preset().is_dirty)
            throw std::runtime_error("The profile has unsaved changes in MentorSlicer; save or discard them first");
        return *target;
    };
    check();
    const std::string question = "Rinominare il profilo " + class_name + "\n'" + sanitized_text(current_name) + "'\nin\n'" +
        sanitized_text(new_name) + "'?\n\nIl profilo padre e i valori restano gli stessi.";
    if (!ask_user("Rinomina profilo", question, m_callback_stopping))
        return {{"success", false}, {"cancelled", true}};

    Preset& target = check();
    const bool was_selected = collection.get_selected_preset_name() == current_name;
    SavedCopy saved = save_user_copy(collection, type, new_name, target.config, target.inherits(), target.version,
        target.custom_defined == "1");
    bool extruder_renamed = false;
    if (type == Preset::TYPE_FILAMENT)
        for (std::string& name : bundle->filament_presets)
            if (name == current_name) {
                name = new_name;
                extruder_renamed = true;
            }
    if (was_selected) {
        if (Tab* tab = wxGetApp().get_tab(type))
            tab->select_preset(new_name);
        else
            collection.select_preset_by_name(new_name, true);
    }
    if (collection.get_selected_preset_name() == current_name || !delete_preset_keep_selection(collection, current_name))
        throw std::runtime_error("The renamed copy was saved as '" + new_name + "' but the old copy could not be removed");
    // Extruder slots that used the old name (also when the copy is not the selected filament)
    // are saved like a selection made in the filament panel, so a restart keeps them.
    if (extruder_renamed && wxGetApp().app_config)
        bundle->export_selections(*wxGetApp().app_config);

    // Keep rollback working for copies created in this session.
    for (auto& [id, operation] : m_applied_operations)
        if (operation.type == type && operation.created_name == current_name) {
            operation.created_name = new_name;
            operation.expected_config = saved.config;
            operation.disk_fingerprint = saved.fingerprint;
        }
    append_history({{"action", "renamed"}, {"profile_class", class_name}, {"from", current_name}, {"profile", new_name}});
    if (Tab* tab = wxGetApp().get_tab(type))
        tab->update_tab_ui();
    if (wxGetApp().plater())
        wxGetApp().plater()->sidebar().update_presets(type);
    return {{"success", true}, {"renamed_from", current_name}, {"renamed_to", new_name}, {"selected", was_selected}};
}

// Live state of the LAN printers known to the device manager (temperatures, fans, progress),
// plus recent finish/error events. Read-only.
nlohmann::json PrintAdvisorServer::printer_status()
{
    const std::string target = target_device_address();
    json devices = json::array();
    const json snapshot = DeviceTelemetryCache::instance().snapshot();
    for (const auto& [host, fields] : snapshot.items()) {
        json device = fields;
        device["address"] = host;
        device["is_selected_device"] = !target.empty() && target == host;
        const int state = telemetry_state(fields);
        if (state >= 0)
            device["state_name"] = state_name(state);
        devices.push_back(std::move(device));
    }
    json result = {{"success", true}, {"devices", devices}, {"recent_events", DeviceTelemetryCache::instance().events()},
                   {"last_print_control_result", last_print_control_result()},
                   {"print_control_results", print_control_results()}};
    if (devices.empty())
        result["note"] = "No LAN printer data yet: open the Device tab in MentorSlicer so it connects to the printer.";
    if (auto* bundle = wxGetApp().preset_bundle)
        result["machine_limits"] = machine_limits(bundle->printers.get_edited_preset().config).to_json();
    return result;
}

// Pause, resume or stop the print on the selected printer, through the device manager page.
// Never starts a print. Each action needs a confirmation in MentorSlicer.
nlohmann::json PrintAdvisorServer::print_control(const std::string& body)
{
    const json request = json::parse(body);
    const std::string action = request.at("action").get<std::string>();
    if (action != "pause" && action != "resume" && action != "stop")
        throw std::runtime_error("action must be pause, resume or stop");
    const DeviceSnapshot device = selected_device_snapshot();
    const int state = device.state;
    if (action == "pause" && state != 1)
        throw std::runtime_error(std::string("The printer is not printing (state: ") + state_name(state) + ")");
    if (action == "resume" && state != 5)
        throw std::runtime_error(std::string("The printer is not paused (state: ") + state_name(state) + ")");
    if (action == "stop" && state != 1 && state != 5)
        throw std::runtime_error(std::string("There is no print to stop (state: ") + state_name(state) + ")");

    std::string question = action == "pause" ? "Mettere in pausa la stampa in corso?" :
                           action == "resume" ? "Riprendere la stampa in pausa?" :
                           "FERMARE la stampa in corso?\n\nL'operazione non si può annullare: la stampa andrà ricominciata da capo.";
    if (const std::string reason = reason_text(request.value("reason", json())); !reason.empty())
        question += "\n\n" + reason;
    if (!ask_user(action == "stop" ? "Ferma la stampa" : action == "pause" ? "Pausa stampa" : "Riprendi stampa", question,
                  m_callback_stopping, {}, action == "stop"))
        return {{"success", false}, {"cancelled", true}};
    check_device_unchanged(device);
    const std::string request_id = send_device_control(device.address, action);
    if (request_id.empty())
        throw std::runtime_error("MentorSlicer could not send the command to the printer");
    append_history({{"action", "print_control"}, {"control", action}, {"request_id", request_id}});
    return {{"success", true}, {"action", action}, {"request_id", request_id},
            {"note", "Command handed to the Device page asynchronously; it is not confirmed yet: check print_control_results." + action +
                     " with this request_id and the new state with get_printer_status"}};
}

// Warms the chamber of an enclosed printer by heating the bed while the printer is idle, so the
// user can start the print once get_printer_status shows the wanted chamber temperature.
// bed_temp 0 switches the bed off.
namespace {
// Preheat of the chamber through the bed, shared by the /v1/preheat route and the button of the
// device page (advisor_preheat_from_page). `request`: bed_temp (0 ends the preheat), optional
// target_chamber_temp and max_minutes. Asks the user, sends the bed setpoint and arms the
// monitor, which switches the bed off by itself when the printer stays idle.
json run_preheat(const json& request, std::shared_ptr<std::atomic<bool>> stopping, const char* source)
{
    if (!request.contains("bed_temp") || !request["bed_temp"].is_number())
        throw std::runtime_error("bed_temp is required");
    const double bed_temp = request["bed_temp"].get<double>();
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        throw std::runtime_error("PresetBundle is unavailable");
    const MachineLimits limits = machine_limits(bundle->printers.get_edited_preset().config);
    if (!std::isfinite(bed_temp) || bed_temp < 0 || bed_temp > 120 || (limits.max_bed_temp >= 0 && bed_temp > limits.max_bed_temp))
        throw std::runtime_error("bed_temp is outside the printer limits");
    // Optional chamber temperature the monitor follows while the bed preheats (-1: none).
    double chamber_target = -1;
    if (request.contains("target_chamber_temp") && !request["target_chamber_temp"].is_null()) {
        if (!request["target_chamber_temp"].is_number())
            throw std::runtime_error("target_chamber_temp must be a number");
        chamber_target = request["target_chamber_temp"].get<double>();
        if (!std::isfinite(chamber_target) || chamber_target < 20 || chamber_target > 70 ||
            (limits.max_chamber_temp >= 0 && chamber_target > limits.max_chamber_temp))
            throw std::runtime_error("target_chamber_temp is outside the printer limits");
        if (bed_temp <= 0)
            chamber_target = -1;
    }
    // Longest time the bed may stay hot without a print: the monitor switches it off afterwards.
    int max_minutes = 60;
    if (request.contains("max_minutes") && !request["max_minutes"].is_null()) {
        const json& value = request["max_minutes"];
        if (!value.is_number() || !std::isfinite(value.get<double>()) || value.get<double>() != std::floor(value.get<double>()) ||
            value.get<double>() < 5 || value.get<double>() > 120)
            throw std::runtime_error("max_minutes deve essere un numero intero di minuti tra 5 e 120");
        max_minutes = int(value.get<double>());
    }
    const DeviceSnapshot device = selected_device_snapshot();
    const int state = device.state;
    if (state != 0 && state != 2 && state != 3 && state != 4)
        throw std::runtime_error(std::string("Preheating is allowed only while the printer is idle (state: ") + state_name(state) + ")");
    // Only one preheat is followed at a time: a preheat on another printer whose bed may still be
    // hot must be cancelled first, otherwise nobody would switch that bed off.
    if (bed_temp > 0)
        if (const std::string conflict = PrintAdvisorMonitor::instance().preheat_conflict(device.address); !conflict.empty())
            throw ConflictError(conflict);

    std::string question = bed_temp > 0
        ? "Scaldare il piatto a " + std::to_string(int(bed_temp)) + " °C per preriscaldare la camera?\n\n"
          "Chiudi il coperchio. Il piatto si spegne da solo dopo " + std::to_string(max_minutes) +
          " min se la stampa non parte, o 20 min dopo che la camera smette di salire."
        : "Spegnere il riscaldamento del piatto?";
    if (chamber_target > 0)
        question += "\n\nObiettivo camera: " + std::to_string(int(chamber_target)) + " °C.";
    if (bed_temp > 0 && PrintAdvisorMonitor::instance().slow_cooldown_active(device.address))
        question += "\n\nAttenzione: è in corso un raffreddamento lento del piatto su questa stampante, che verrà interrotto.";
    if (!ask_user("Preriscaldamento camera", question, std::move(stopping)))
        return {{"success", false}, {"cancelled", true}};
    check_device_unchanged(device);
    // Checked again after the dialog: another preheat may have been armed meanwhile.
    if (bed_temp > 0)
        if (const std::string conflict = PrintAdvisorMonitor::instance().preheat_conflict(device.address); !conflict.empty())
            throw ConflictError(conflict);
    const std::string request_id = send_device_control(device.address, "set_bed_temp", {{"value", bed_temp}});
    if (request_id.empty())
        throw std::runtime_error("MentorSlicer could not send the command to the printer");
    // bed_temp 0 tells the monitor that the preheat of this printer ended. A failure reported
    // later by the page for request_id reaches the monitor (on_bed_command_failed).
    PrintAdvisorMonitor::instance().arm_preheat(device.address, bed_temp, chamber_target, max_minutes, request_id);
    append_history({{"action", "preheat"}, {"bed_temp", bed_temp}, {"max_minutes", max_minutes}, {"source", source},
                    {"request_id", request_id}});
    return {{"success", true}, {"bed_temp", bed_temp}, {"max_minutes", bed_temp > 0 ? json(max_minutes) : json(nullptr)},
            {"request_id", request_id},
            {"note", "Command handed to the Device page asynchronously, not confirmed yet: check print_control_results.set_bed_temp "
                     "with this request_id and targetBedTemp0 with get_printer_status. "
                     "Poll get_printer_status (boxTemp) to follow the chamber temperature; the K1-series chamber sensor can read 5-10 C high."}};
}
} // namespace

nlohmann::json PrintAdvisorServer::preheat(const std::string& body)
{
    return run_preheat(json::parse(body), m_callback_stopping, "mcp");
}

// "Preriscalda" / "Stop" button of the chamber card in the device page (web patch 17):
// {"command":"advisor_preheat","address":"<ip[:port]>","start":true|false[,"bed_temp":n]
//  [,"target_chamber_temp":"<text typed in the card>"]}.
// Same confirmation, limits and automatic switch-off as the MCP tool; refused with a message
// box when the local API is off, another Print Advisor request is running, a modal window is
// open, or the printer of the card is not the one Print Advisor acts on. GUI thread.
void advisor_preheat_from_page(const nlohmann::json& msg)
{
    const auto host = [](const std::string& address) { return address.substr(0, address.find(':')); };
    std::string error;
    try {
        PrintAdvisorServer* server = advisor_running_server();
        if (!server)
            throw std::runtime_error("Il preriscaldo dalla scheda Dispositivo usa il Print Advisor: attivalo da Integrazione AI → Print Advisor MCP.");
        if (exclusive_handler_depth() > 0 || modal_ui_active())
            throw std::runtime_error("Un'altra richiesta e' in corso in MentorSlicer: riprova tra poco.");
        const std::string address = msg.value("address", std::string());
        const std::string target = target_device_address();
        if (target.empty())
            throw std::runtime_error("Nessuna stampante selezionata in MentorSlicer.");
        if (!address.empty() && host(address) != host(target))
            throw std::runtime_error("La stampante della scheda (" + host(address) + ") non e' quella su cui agisce il Print Advisor (" + host(target) + ").");
        // The page always sends "start"; anything else is refused rather than read as "heat".
        if (!msg.contains("start") || !msg["start"].is_boolean())
            throw std::runtime_error("Richiesta di preriscaldo non valida dalla scheda Dispositivo.");
        json request;
        if (msg["start"].get<bool>()) {
            // Default: the bed maximum of the printer, capped at 100 C (the K1C maximum).
            double bed_temp = 100;
            if (auto* bundle = wxGetApp().preset_bundle) {
                const MachineLimits limits = machine_limits(bundle->printers.get_edited_preset().config);
                if (limits.max_bed_temp > 0)
                    bed_temp = std::min(bed_temp, limits.max_bed_temp);
            }
            if (msg.contains("bed_temp") && msg["bed_temp"].is_number())
                bed_temp = msg["bed_temp"].get<double>();
            request = {{"bed_temp", bed_temp}, {"max_minutes", 60}};
            // Optional chamber target typed in the card, as text ("" = none, "35" or "35,5").
            if (msg.contains("target_chamber_temp") && msg["target_chamber_temp"].is_string()) {
                std::string text = msg["target_chamber_temp"].get<std::string>();
                std::replace(text.begin(), text.end(), ',', '.');
                boost::algorithm::trim(text);
                if (!text.empty()) {
                    std::istringstream stream(text);
                    stream.imbue(std::locale::classic());
                    double target = 0;
                    stream >> target;
                    double ceiling = 70;
                    if (auto* bundle = wxGetApp().preset_bundle) {
                        const MachineLimits limits = machine_limits(bundle->printers.get_edited_preset().config);
                        if (limits.max_chamber_temp >= 0)
                            ceiling = std::min(ceiling, limits.max_chamber_temp);
                    }
                    if (stream.fail() || !stream.eof() || !std::isfinite(target) || target < 20 || target > ceiling)
                        throw std::runtime_error("Obiettivo camera non valido: \"" + msg["target_chamber_temp"].get<std::string>() +
                                                 "\". Scrivi una temperatura tra 20 e " + std::to_string(int(ceiling)) +
                                                 " °C, oppure lascia il campo vuoto.");
                    const json telemetry = current_device_telemetry();
                    if (telemetry.is_object() && telemetry.contains("boxTemp") && telemetry["boxTemp"].is_number() &&
                        telemetry["boxTemp"].get<double>() >= target)
                        throw std::runtime_error("La camera è già a " + std::to_string(int(std::lround(telemetry["boxTemp"].get<double>()))) +
                                                 " °C, non sotto l'obiettivo di " + std::to_string(int(std::lround(target))) + " °C.");
                    request["target_chamber_temp"] = target;
                }
            }
        } else {
            request = {{"bed_temp", 0}};
        }
        ExclusiveHandlerScope exclusive_scope(true);
        const json result = run_preheat(request, server->stop_flag(), "device_page");
        BOOST_LOG_TRIVIAL(info) << "Print Advisor: device-page preheat " << result.dump();
    } catch (const std::exception& e) {
        error = e.what();
    }
    if (error.empty())
        return;
    wxWindow* parent = wxGetApp().mainframe;
    if (parent && (!wxTopLevelWindows.Find(parent) || parent->IsBeingDeleted()))
        parent = nullptr;
    wxMessageBox(wxString::FromUTF8(error), "MCP Advisor", wxOK | wxICON_WARNING, parent);
}

// Generates one of MentorSlicer's calibration tests on a new project, like the Calibration
// menu does. The test changes the edited presets; reselect the profile before saving a result.
nlohmann::json PrintAdvisorServer::start_calibration(const std::string& body)
{
    const json request = json::parse(body);
    const std::string test = request.at("test").get<std::string>();
    Plater* plater = wxGetApp().plater();
    auto* bundle = wxGetApp().preset_bundle;
    if (!plater || !bundle)
        throw std::runtime_error("MentorSlicer is not ready");
    const auto number = [&request](const char* key, double fallback) {
        const double value = request.contains(key) && request[key].is_number() ? request[key].get<double>() : fallback;
        if (!std::isfinite(value) || std::abs(value) > 1e6)
            throw std::runtime_error(std::string("Il valore non è un numero valido: ") + key);
        return value;
    };
    const DynamicPrintConfig& printer_config = bundle->printers.get_edited_preset().config;
    const MachineLimits limits = machine_limits(printer_config);
    // Height of a tower with one step per mm (plus a base), against the printable height.
    const auto check_tower_height = [&printer_config](double steps, double base_mm) {
        double printable = 250;
        if (const auto* height = printer_config.option<ConfigOptionFloat>("printable_height"); height && height->value > 0)
            printable = height->value;
        if (steps + base_mm > printable)
            throw std::runtime_error("La torre sarebbe alta " + std::to_string(int(std::ceil(steps + base_mm))) +
                                     " mm, oltre l'altezza stampabile di " + std::to_string(int(printable)) +
                                     " mm: riduci l'intervallo o aumenta il passo");
    };

    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    const BedType bed_type = plate ? plate->get_bed_type(true) : btDefault;
    const DynamicPrintConfig& filament = bundle->filaments.get_edited_preset().config;
    double bed_temp = 0;
    const std::string bed_key = get_bed_temp_key(bed_type);
    if (const auto* option = bed_key.empty() ? nullptr : filament.option<ConfigOptionInts>(bed_key))
        if (!option->values.empty())
            bed_temp = option->values.front();

    Calib_Params params;
    params.print_numbers = true;
    params.bed_type = bed_type;
    params.bed_temp = bed_temp;
    params.acc_speed = 0;
    params.print_speed = 0;
    params.high_step = 0;
    params.start = number("start", 0);
    params.end = number("end", 0);
    params.step = number("step", 0);
    std::string description;
    json reading;
    if (test == "temperature") {
        params.mode = CalibMode::Calib_Temp_Tower;
        params.step = 5;
        if (params.start > 350 || params.end < 170 || params.end > params.start - 5 ||
            std::fmod(params.start, 5.0) != 0 || std::fmod(params.end, 5.0) != 0)
            throw std::runtime_error("La torre di temperatura richiede multipli di 5 con inizio <= 350, fine >= 170 e fine <= inizio - 5");
        // Both ends against the printer limits and the active filament's nozzle range.
        const DynamicPrintConfig& tower_filament = bundle->filaments.get_edited_preset().config;
        for (const auto& [value, label] : {std::pair<double, const char*>{params.start, "inizio"}, {params.end, "fine"}}) {
            try {
                check_advisor_safety("nozzle_temperature", value, tower_filament, limits);
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string("Torre di temperatura: ") + label + " " + std::to_string(int(value)) +
                                         " °C fuori dai limiti della stampante o del filamento attivo (" + error.what() + ")");
            }
        }
        // Plater::calib_temp keeps one 10 mm block per 5 °C from start down to end.
        check_tower_height(10.0 * ((params.start - params.end) / 5.0 + 1.0), 0);
        description = "Torre di temperatura da " + std::to_string(int(params.start)) + " a " + std::to_string(int(params.end)) + " °C";
        reading = {{"block_height_mm", 10}, {"formula", "temperature = start - 5 * block_index (block 0 at the bottom)"},
                   {"start", params.start}, {"step", -5}};
    } else if (test == "flow") {
        const double pass_value = number("pass", 1);
        if (pass_value != 1 && pass_value != 2)
            throw std::runtime_error("Flow test pass must be 1 or 2");
        const int pass = int(pass_value);
        // Pass 1 always uses flowrate 1.0; pass 2 starts from the pass-1 result.
        params.start = pass == 1 ? 1.0 : number("flowrate", 1.0);
        double profile_flow = 1.0;
        if (const auto* ratios = filament.option<ConfigOptionFloats>("filament_flow_ratio"); ratios && !ratios->values.empty())
            profile_flow = ratios->values.front();
        if (pass == 2 && (params.start < 0.8 || params.start > 1.2))
            throw std::runtime_error("Flow pass 2 needs flowrate between 0.8 and 1.2 (the result of pass 1)");
        description = pass == 1 ? "Test del flusso, passo 1 (piastrine da -20 a +20)" : "Test del flusso, passo 2 (piastrine da -9 a 0)";
        reading = {{"formula", "new_flow_ratio = base_flow_ratio * (1 + N / 100), N = number printed on the best patch"},
                   {"base_flow_ratio", profile_flow * params.start},
                   {"note", "the test multiplies the profile flow; compute against the flow_ratio of the profile used for the test"}};
        if (!ask_user("Calibrazione", description + "\n\nVerrà creato un nuovo progetto con il test: MentorSlicer chiederà cosa fare del progetto aperto.", m_callback_stopping))
            return {{"success", false}, {"cancelled", true}};
        const std::vector<size_t> before = model_object_ids(*plater);
        plater->calib_flowrate(pass, float(params.start));
        if (model_object_ids(*plater) == before)
            return {{"success", false}, {"cancelled", true}, {"message", "The calibration was not generated (new project cancelled?)"}};
        append_history({{"action", "calibration"}, {"test", test}, {"pass", pass}});
        return {{"success", true}, {"test", test}, {"description", description}, {"reading", reading}};
    } else if (test == "pressure_advance") {
        params.mode = CalibMode::Calib_PA_Tower;
        if (params.start < 0 || params.step < 0.001 || params.step > 0.5 || params.end < params.start + params.step || params.end > 2.0)
            throw std::runtime_error("Pressure advance: servono inizio >= 0, passo tra 0.001 e 0.5, fine >= inizio + passo e fine <= 2.0");
        check_tower_height((params.end - params.start) / params.step, 1);
        description = "Torre di pressure advance";
        // The tower G-code sets the value per whole millimetre: int(print_z).
        reading = {{"formula", "pressure_advance = start + step * floor(height_mm)"}, {"start", params.start}, {"step", params.step}};
    } else if (test == "retraction") {
        params.mode = CalibMode::Calib_Retraction_tower;
        if (params.start < 0 || params.step < 0.01 || params.step > 2 || params.end <= params.start || params.end > 10)
            throw std::runtime_error("Test di retrazione: servono inizio >= 0, passo tra 0.01 e 2 mm, fine > inizio e fine <= 10 mm");
        check_tower_height((params.end - params.start) / params.step, 1.6);
        description = "Torre di retrazione";
        reading = {{"formula", "retraction_length = start + step * floor(height_mm - 0.6)"}, {"start", params.start}, {"step", params.step}};
    } else if (test == "max_volumetric_speed") {
        params.mode = CalibMode::Calib_Vol_speed_Tower;
        if (params.start < 0.5 || params.step < 0.1 || params.step > 10 || params.end <= params.start || params.end > 60)
            throw std::runtime_error("Test di velocità volumetrica: servono inizio >= 0.5, passo tra 0.1 e 10, fine > inizio e "
                                     "fine <= 60 mm³/s");
        check_tower_height((params.end - params.start) / params.step, 1);
        description = "Torre di velocità volumetrica massima";
        reading = {{"formula", "max_volumetric_speed = start + step * height_mm (mm3/s); save about 10% less than where the wall degrades"},
                   {"start", params.start}, {"step", params.step},
                   {"note", "this test selects the first filament preset of the list: reselect your filament profile afterwards"}};
    } else {
        throw std::runtime_error("test must be temperature, flow, pressure_advance, retraction or max_volumetric_speed");
    }
    if (!ask_user("Calibrazione", description + "\n\nVerrà creato un nuovo progetto con il test: MentorSlicer chiederà cosa fare del progetto aperto.", m_callback_stopping))
        return {{"success", false}, {"cancelled", true}};
    const std::vector<size_t> before = model_object_ids(*plater);
    switch (params.mode) {
    case CalibMode::Calib_Temp_Tower: plater->calib_temp(params); break;
    case CalibMode::Calib_PA_Tower: plater->calib_pa(params); break;
    case CalibMode::Calib_Retraction_tower: plater->calib_retraction(params); break;
    default: plater->calib_max_vol_speed(params); break;
    }
    // The generators return nothing and stop silently if the new project is cancelled.
    if (model_object_ids(*plater) == before)
        return {{"success", false}, {"cancelled", true}, {"message", "The calibration was not generated (new project cancelled?)"}};
    append_history({{"action", "calibration"}, {"test", test}, {"start", params.start}, {"end", params.end}, {"step", params.step}});
    return {{"success", true}, {"test", test}, {"description", description}, {"reading", reading},
            {"note", "Slice and print the test; afterwards reselect the profile (discard the test changes) before saving the result."}};
}

// Checks before printing: printer limits, material/plate/nozzle compatibility and the G-code of
// the current slice. Read-only; findings have severity error, warning or info.
nlohmann::json PrintAdvisorServer::preflight()
{
    auto* bundle = wxGetApp().preset_bundle;
    Plater* plater = wxGetApp().plater();
    if (!bundle || !plater)
        throw std::runtime_error("MentorSlicer is not ready");
    const DynamicPrintConfig& printer = bundle->printers.get_edited_preset().config;
    const DynamicPrintConfig& filament = bundle->filaments.get_edited_preset().config;
    const DynamicPrintConfig& process = bundle->prints.get_edited_preset().config;
    const MachineLimits limits = machine_limits(printer);
    json findings = json::array();
    const auto add = [&findings](const char* severity, const std::string& code, const std::string& message) {
        findings.push_back({{"severity", severity}, {"code", code}, {"message", message}});
    };
    const auto first_int = [](const DynamicPrintConfig& config, const char* key) -> double {
        if (const auto* ints = config.option<ConfigOptionInts>(key))
            return ints->values.empty() ? -1 : ints->values.front();
        if (const auto* floats = config.option<ConfigOptionFloats>(key))
            return floats->values.empty() ? -1 : floats->values.front();
        try {
            if (const ConfigOption* option = config.option(key))
                return option->getFloat();
        } catch (...) {
        }
        return -1;
    };
    std::string type;
    if (const auto* types = filament.option<ConfigOptionStrings>("filament_type"); types && !types->values.empty())
        type = types->values.front();
    std::string upper_type = type;
    std::transform(upper_type.begin(), upper_type.end(), upper_type.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    // Match whole tokens: "PCTG" must not count as "PC".
    const auto tokens_of = [](const std::string& text) {
        std::unordered_set<std::string> tokens;
        std::string token;
        for (const char c : text + " ") {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                token += c;
            } else if (!token.empty()) {
                tokens.insert(token);
                token.clear();
            }
        }
        return tokens;
    };
    const auto has_any = [](const std::unordered_set<std::string>& tokens, std::initializer_list<const char*> wanted) {
        for (const char* word : wanted)
            if (tokens.count(word))
                return true;
        return false;
    };
    const auto type_tokens = tokens_of(upper_type);
    const bool shrinking = has_any(type_tokens, {"ASA", "ABS", "PC", "PA", "PA6", "PA12", "PAHT", "PPA"});
    const bool low_temp = has_any(type_tokens, {"PLA", "PETG", "PCTG", "TPU"});

    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    const BedType bed_type = plate ? plate->get_bed_type(true) : btDefault;
    const std::string bed_key = get_bed_temp_key(bed_type);
    const double bed = bed_key.empty() ? -1 : first_int(filament, bed_key.c_str());
    const double bed_first = bed_key.empty() ? -1 : first_int(filament, get_bed_temp_1st_layer_key(bed_type).c_str());
    const double nozzle = first_int(filament, "nozzle_temperature");
    const double nozzle_first = first_int(filament, "nozzle_temperature_initial_layer");
    const double range_low = first_int(filament, "nozzle_temperature_range_low");
    const double range_high = first_int(filament, "nozzle_temperature_range_high");
    const double soak = first_int(filament, "heat_soak_time");
    const double soak_bed = first_int(filament, "heat_soak_bed_temp");

    // Printer limits.
    for (const auto& [value, what] : {std::pair<double, const char*>{bed, "bed"}, {bed_first, "initial layer bed"}, {soak_bed, "heat soak bed"}})
        if (limits.max_bed_temp >= 0 && value > limits.max_bed_temp)
            add("error", "bed_over_limit", std::string(what) + " temperature " + std::to_string(int(value)) + " C is above the printer maximum " +
                std::to_string(int(limits.max_bed_temp)) + " C");
    for (const double value : {nozzle, nozzle_first})
        if (limits.max_nozzle_temp >= 0 && value > limits.max_nozzle_temp)
            add("error", "nozzle_over_limit", "nozzle temperature " + std::to_string(int(value)) + " C is above the printer maximum " +
                std::to_string(int(limits.max_nozzle_temp)) + " C");
    if (range_low > 0 && range_high > range_low)
        for (const double value : {nozzle, nozzle_first})
            if (value > 0 && (value < range_low || value > range_high))
                add("warning", "nozzle_out_of_filament_range", "nozzle temperature " + std::to_string(int(value)) + " C is outside the filament range " +
                    std::to_string(int(range_low)) + "-" + std::to_string(int(range_high)) + " C");

    // Material, plate and nozzle compatibility.
    if (shrinking && bed_type == btPC)
        add("error", "plate_material", type + " does not stick to the cool plate: use a PEI or engineering plate");
    if (low_temp && bed > 80)
        add("warning", "bed_too_hot_for_material", type + " with the bed at " + std::to_string(int(bed)) + " C can soften the first layers");
    std::string filament_name = bundle->filaments.get_edited_preset().name;
    std::transform(filament_name.begin(), filament_name.end(), filament_name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    const bool abrasive = has_any(type_tokens, {"CF", "GF"}) || has_any(tokens_of(filament_name), {"CF", "GF"});
    if (const ConfigOption* nozzle_type = printer.option("nozzle_type"); abrasive && nozzle_type && nozzle_type->serialize() == "brass")
        add("warning", "abrasive_on_brass", "fiber-filled filament wears a brass nozzle quickly: use a hardened steel nozzle");
    if (soak > 0 && low_temp)
        add("warning", "soak_low_temp_material", "heat soak with " + type + " can soften the filament in the hot end (heat creep)");
    if (shrinking) {
        if (soak <= 0)
            add("info", "no_heat_soak", type + " warps less in a warm chamber: consider heat_soak_time on an enclosed printer");
        const double exhaust = first_int(filament, "during_print_exhaust_fan_speed");
        const ConfigOption* air = filament.option("activate_air_filtration");
        if (air && air->serialize() == "1" && exhaust > 30)
            add("warning", "exhaust_fan_cools_chamber", "back fan at " + std::to_string(int(exhaust)) + "% during the print cools the chamber");
        if (!air || air->serialize() != "1")
            add("info", "exhaust_fan_uncontrolled", "the slicer does not control the back fan during the print, so the printer default applies");
        const double fan_max = first_int(filament, "fan_max_speed");
        if (fan_max > 50)
            add("warning", "part_fan_high", "part fan up to " + std::to_string(int(fan_max)) + "% is high for " + type);
    }

    // Live state of the selected printer (LAN telemetry, Device tab open). Fields are optional.
    const std::string device_address = target_device_address();
    const json telemetry = current_device_telemetry();
    bool printer_data_stale = false;
    if (telemetry.is_object()) {
        // Age from the monotonic clock: a wall-clock jump must not make old data look fresh.
        const long long age = device_address.empty() ? -1 : DeviceTelemetryCache::instance().age_ms(device_address);
        if (age < 0 || age > 120000) {
            printer_data_stale = true;
            add("info", "printer_data_stale", "dati della stampante non recenti: apri la scheda Dispositivo per i controlli su filamento e camera");
        } else {
            if (telemetry.contains("materialDetect")) {
                const double detect = json_number(telemetry["materialDetect"]);
                if (detect >= 0 && detect != 1)
                    add("warning", "filament_sensor_off", "sensore filamento disattivato: la stampa non si ferma se il filamento finisce");
            }
            if (telemetry.contains("materialStatus") && json_number(telemetry["materialStatus"]) == 1)
                add("error", "filament_missing", "filamento assente/esaurito secondo il sensore della stampante");
            const int printer_state = telemetry_state(telemetry);
            const bool printer_idle = printer_state == 0 || printer_state == 2 || printer_state == 3 || printer_state == 4;
            if (shrinking && printer_idle) {
                // Starting chamber: the monitor baseline (reading before any preheat) when known.
                double chamber = -1;
                std::string chamber_source;
                const json baseline = PrintAdvisorMonitor::instance().chamber_baseline(device_address);
                if (baseline.is_object() && baseline.contains("chamber")) {
                    chamber = json_number(baseline["chamber"]);
                    chamber_source = "baseline del monitor";
                }
                if (chamber < 0 && telemetry.contains("boxTemp")) {
                    chamber = json_number(telemetry["boxTemp"]);
                    chamber_source = "sensore camera";
                }
                // Soak figures shared with chamber_cold_preheat (PrintAdvisorPlate.cpp): below
                // 20 °C at least 30 min, 20-30 °C 15 min.
                const std::string reading = std::to_string(int(std::lround(chamber))) + " °C, " + chamber_source;
                if (chamber >= 0 && chamber < 20)
                    add("warning", "cold_room", "stanza fredda (camera " + reading + "): il solo piatto porta la camera circa +16 °C "
                        "in 30–35 min; soak di almeno 30 min. Stima euristica da 2 misure sulla K1C.");
                else if (chamber >= 20 && chamber < 30)
                    add("info", "cool_room", "camera a " + reading + ": il solo piatto la scalda di circa +16 °C in 30–35 min; "
                        "soak di 15 min prima di " + type + ". Stima euristica da 2 misure sulla K1C.");
            }
        }
    }

    // Plate and chamber advice for ASA/ABS (glue on smooth PEI with a large part, cold chamber
    // on an idle printer, or no telemetry to tell). Advisory only. PrintAdvisorPlate.cpp.
    // chamber_unknown is skipped when printer_data_stale already said the telemetry is old.
    for (json& finding : plate_material_findings(type, has_any(type_tokens, {"ASA", "ABS"}), bed_type, telemetry, printer_data_stale))
        findings.push_back(std::move(finding));

    // Speeds and accelerations against the machine limits.
    const double max_speed = std::min(first_int(printer, "machine_max_speed_x"), first_int(printer, "machine_max_speed_y"));
    for (const char* key : {"outer_wall_speed", "inner_wall_speed", "sparse_infill_speed", "internal_solid_infill_speed", "travel_speed"}) {
        const double speed = first_int(process, key);
        if (max_speed > 0 && speed > max_speed)
            add("warning", "speed_over_machine", std::string(key) + " " + std::to_string(int(speed)) + " mm/s is above the machine limit " +
                std::to_string(int(max_speed)) + " mm/s");
    }
    const double max_acc = first_int(printer, "machine_max_acceleration_extruding");
    const double acc = first_int(process, "default_acceleration");
    if (max_acc > 0 && acc > max_acc)
        add("warning", "acceleration_over_machine", "default_acceleration " + std::to_string(int(acc)) + " is above the machine limit " +
            std::to_string(int(max_acc)));

    // G-code of the current slice.
    json gcode_summary = nullptr;
    const GCodeProcessorResult* result = plater->get_partplate_list().get_current_slice_result();
    if (!plate || !plate->is_slice_result_valid() || !result || result->filename.empty()) {
        add("info", "not_sliced", "slice the plate to also check the G-code");
    } else {
        boost::filesystem::ifstream file(result->filename, std::ios::binary);
        std::string line;
        double max_bed_cmd = 0, max_nozzle_cmd = 0, dwell_s = 0;
        bool soak_marker = false;
        size_t lines = 0;
        const auto read_s = [](const std::string& text, char letter) -> double {
            const size_t at = text.find(std::string(" ") + letter);
            if (at == std::string::npos)
                return -1;
            std::istringstream stream(text.substr(at + 2));
            stream.imbue(std::locale::classic());
            double value = -1;
            stream >> value;
            return stream.fail() ? -1 : value;
        };
        while (std::getline(file, line) && lines < 20000000) {
            ++lines;
            if (line.rfind("; HEAT_SOAK", 0) == 0)
                soak_marker = true;
            if (line.rfind("M140", 0) == 0 || line.rfind("M190", 0) == 0)
                max_bed_cmd = std::max(max_bed_cmd, read_s(line, 'S'));
            else if (line.rfind("M104", 0) == 0 || line.rfind("M109", 0) == 0)
                max_nozzle_cmd = std::max(max_nozzle_cmd, read_s(line, 'S'));
            else if (line.rfind("G4 ", 0) == 0) {
                const double p = read_s(line, 'P');
                const double s = read_s(line, 'S');
                dwell_s += p > 0 ? p / 1000.0 : (s > 0 ? s : 0);
            }
        }
        gcode_summary = {{"max_bed_command", max_bed_cmd}, {"max_nozzle_command", max_nozzle_cmd},
                         {"dwell_seconds", dwell_s}, {"heat_soak_block", soak_marker}};
        if (soak > 0 && !soak_marker)
            add("error", "heat_soak_missing", "heat_soak_time is set but the G-code has no heat soak block: slice again");
        if (limits.max_bed_temp >= 0 && max_bed_cmd > limits.max_bed_temp)
            add("error", "gcode_bed_over_limit", "the G-code sets the bed to " + std::to_string(int(max_bed_cmd)) + " C");
        if (limits.max_nozzle_temp >= 0 && max_nozzle_cmd > limits.max_nozzle_temp)
            add("error", "gcode_nozzle_over_limit", "the G-code sets the nozzle to " + std::to_string(int(max_nozzle_cmd)) + " C");
        if (result->toolpath_outside)
            add("error", "toolpath_outside", "part of the toolpath is outside the plate");
        for (const auto& warning : result->warnings)
            add(warning.level >= 2 ? "error" : "warning", "slicer_" + warning.error_code, warning.msg);
    }
    bool ok = true;
    for (const json& finding : findings)
        if (finding["severity"] == "error")
            ok = false;
    return {{"success", true}, {"ok_to_print", ok}, {"findings", findings}, {"gcode", gcode_summary},
            {"machine_limits", limits.to_json()}, {"filament_type", type}, {"plate_type", bed_type_to_gcode_string(bed_type)}};
}

// Objects on the current plate with size, mesh health, an overhang estimate from the mesh
// normals, their per-object overrides and the slicer's support hint from the last slice.
nlohmann::json PrintAdvisorServer::model_analysis()
{
    Plater* plater = wxGetApp().plater();
    if (!plater)
        throw std::runtime_error("MentorSlicer is not ready");
    PartPlate* plate = plater->get_partplate_list().get_curr_plate();
    const Model& model = plater->model();
    json objects = json::array();
    for (size_t index = 0; index < model.objects.size(); ++index) {
        const ModelObject* object = model.objects[index];
        if (!object || object->instances.empty())
            continue;
        bool on_plate = false;
        for (size_t instance = 0; instance < object->instances.size() && !on_plate; ++instance)
            on_plate = plate && plate->contain_instance(int(index), int(instance));
        if (!on_plate)
            continue;
        const BoundingBoxf3 box = object->instance_bounding_box(0);
        const Vec3d size = box.size();
        TriangleMesh mesh = object->raw_mesh();
        const auto& stats = mesh.stats();
        const Bridge::OverhangResult overhang = Bridge::SlicerBridge::AnalyzeOverhang(*object, 45.0);
        json overrides = json::object();
        for (const std::string& key : object->config.keys())
            if (const ConfigOption* option = object->config.option(key))
                overrides[key] = option->serialize();
        objects.push_back({
            {"object_index", index}, {"name", object->name}, {"instances", object->instances.size()},
            {"size_mm", {size.x(), size.y(), size.z()}}, {"volume_mm3", mesh.volume()},
            {"triangles", mesh.its.indices.size()},
            {"mesh", {{"manifold", stats.manifold()}, {"open_edges", stats.open_edges}, {"repaired", stats.repaired()}}},
            {"overhang", {{"unsupported_area_ratio", overhang.unsupported_area_ratio},
                          {"downward_area_ratio", overhang.downward_area_ratio},
                          {"bottom_contact_area_mm2", overhang.bottom_contact_area_mm2},
                          {"bottom_contact_ratio", overhang.bottom_contact_ratio},
                          {"max_overhang_angle_deg", overhang.max_overhang_angle}}},
            {"per_object_overrides", overrides}});
    }
    json support_hints = json::array();
    if (plate && plate->is_slice_result_valid()) {
        const Print& print = plater->get_partplate_list().get_current_fff_print();
        for (const PrintObject* object : print.objects())
            if (object && object->model_object())
                support_hints.push_back({{"name", object->model_object()->name},
                                         {"support_suggested", const_cast<PrintObject*>(object)->has_support_message()}});
    }
    return {{"success", true}, {"plate_number", plater->get_partplate_list().get_curr_plate_index() + 1},
            {"objects", objects}, {"support_hints_from_last_slice", support_hints},
            {"notes", "overhang ratios are mesh estimates (45 degree threshold); support_suggested needs a slice with supports off"}};
}

// Runs the app's auto-orient or auto-arrange on the current plate, after confirmation.
nlohmann::json PrintAdvisorServer::arrange_or_orient(const std::string& body)
{
    const json request = json::parse(body);
    const std::string action = request.at("action").get<std::string>();
    if (action != "orient" && action != "arrange")
        throw std::runtime_error("action must be orient or arrange");
    Plater* plater = wxGetApp().plater();
    if (!plater)
        throw std::runtime_error("MentorSlicer is not ready");
    // Another arrange, orient or similar job is running: the new one would be queued behind it
    // (or dropped) and the history would record an action that did not happen.
    const auto check_jobs_idle = [](Plater& target) {
        if (!target.get_ui_job_worker().is_idle())
            throw ConflictError("Un'altra operazione (disposizione, orientamento...) è in corso in MentorSlicer: attendi e riprova");
    };
    check_jobs_idle(*plater);
    const std::string question = action == "orient"
        ? "Orientare automaticamente gli oggetti del piatto corrente?\n\nPuoi annullare con Ctrl+Z."
        : "Disporre automaticamente gli oggetti del piatto corrente?\n\nPuoi annullare con Ctrl+Z.";
    if (!ask_user(action == "orient" ? "Orientamento automatico" : "Disposizione automatica", question, m_callback_stopping))
        return {{"success", false}, {"cancelled", true}};
    plater = wxGetApp().plater();
    if (!plater)
        throw std::runtime_error("MentorSlicer is not ready");
    check_jobs_idle(*plater);
    plater->set_prepare_state(Job::PREPARE_STATE_MENU);
    if (action == "orient")
        plater->orient();
    else
        plater->arrange();
    append_history({{"action", action}});
    return {{"success", true}, {"action", action}, {"note", "The job runs in the background; slice again afterwards"}};
}

// Sets or removes setting overrides on one object of the plate (like the object settings in the
// object list), validated like profile changes, after confirmation. Undo with Ctrl+Z.
nlohmann::json PrintAdvisorServer::object_settings(const std::string& body)
{
    const json request = json::parse(body);
    Plater* plater = wxGetApp().plater();
    auto* bundle = wxGetApp().preset_bundle;
    if (!plater || !bundle)
        throw std::runtime_error("MentorSlicer is not ready");
    const json& index_value = request.at("object_index");
    if (!index_value.is_number_integer() || index_value.get<long long>() < 0 || index_value.get<long long>() > 100000)
        throw std::runtime_error("object_index must be a non-negative integer");
    const size_t index = size_t(index_value.get<long long>());
    Model& model = plater->model();
    if (index >= model.objects.size() || !model.objects[index])
        throw std::runtime_error("object_index is not valid");
    ModelObject* object = model.objects[index];
    // Identity and overrides before the dialog: undo/redo may recreate the object at another
    // address, or the user may edit its settings meanwhile.
    const ObjectID object_id = object->id();
    const std::map<std::string, std::string> overrides_before = object_overrides(*object);

    const std::vector<std::string> allowed_keys = SettingsFactory::get_options(false);
    const std::unordered_set<std::string> allowed(allowed_keys.begin(), allowed_keys.end());
    const MachineLimits limits = machine_limits(bundle->printers.get_edited_preset().config);
    const DynamicPrintConfig& filament = bundle->filaments.get_edited_preset().config;
    const DynamicPrintConfig& process = bundle->prints.get_edited_preset().config;

    std::vector<std::pair<std::string, std::string>> sets;
    std::vector<std::string> removes;
    std::string summary;
    if (request.contains("changes") && request["changes"].is_array()) {
        if (request["changes"].size() > 20)
            throw std::runtime_error("At most 20 object setting changes");
        for (const auto& change : request["changes"]) {
            const std::string key = change.at("key").get<std::string>();
            if (!allowed.count(key) || !advisor_key_writable(key))
                throw std::runtime_error("Setting cannot be overridden per object: " + key);
            const ConfigOptionDef* def = print_config_def.get(key);
            const auto& value = change.at("value");
            std::string serialized = value.is_string() ? value.get<std::string>() :
                                     value.is_boolean() ? (value.get<bool>() ? "1" : "0") : value.dump();
            DynamicPrintConfig parsed;
            parsed.set_deserialize_strict(key, serialized);
            const ConfigOption* option = parsed.option(key);
            if (!option || dynamic_cast<const ConfigOptionVectorBase*>(option))
                throw std::runtime_error("Only scalar per-object settings are supported: " + key);
            if (def->type == coInt || def->type == coFloat || def->type == coFloatOrPercent || def->type == coPercent) {
                const double numeric = def->type == coInt ? option->getInt() : option->getFloat();
                if (!std::isfinite(numeric) || numeric < def->min || numeric > def->max)
                    throw std::runtime_error("Value is outside its allowed range: " + key);
                const auto* float_or_percent = dynamic_cast<const ConfigOptionFloatOrPercent*>(option);
                if (def->type != coPercent && !(float_or_percent && float_or_percent->percent))
                    check_advisor_safety(key, numeric, filament, limits);
            }
            const ConfigOption* current = object->config.option(key);
            const ConfigOption* global = process.option(key);
            summary += key + ": " + (current ? current->serialize() : (global ? global->serialize() + " (profilo)" : std::string("-"))) +
                       "  →  " + option->serialize() + "\n";
            sets.emplace_back(key, option->serialize());
        }
    }
    if (request.contains("remove") && request["remove"].is_array()) {
        if (request["remove"].size() > 20)
            throw std::runtime_error("At most 20 object settings can be removed at once");
        for (const auto& key : request["remove"]) {
            const std::string name = key.get<std::string>();
            // Same allowlist as the changes: removing an override (for example an extruder
            // assignment) changes the print as much as setting it.
            if (!allowed.count(name) || !advisor_key_writable(name))
                throw std::runtime_error("Setting cannot be changed per object by Print Advisor: " + name);
            if (!object->config.has(name))
                throw std::runtime_error("The object has no override for: " + name);
            summary += name + ": rimuovi l'impostazione dell'oggetto (torna al profilo)\n";
            removes.push_back(name);
        }
    }
    if (sets.empty() && removes.empty())
        throw std::runtime_error("Nothing to change");
    std::string question = "Impostazioni solo per l'oggetto '" + sanitized_text(object->name) + "':\n\n" + summary;
    if (const std::string reason = reason_text(request.value("reason", json())); !reason.empty())
        question += "\n" + reason + "\n";
    question += "\nI profili non cambiano. Puoi annullare con Ctrl+Z. Confermi?";
    if (!ask_user("Impostazioni per oggetto", question, m_callback_stopping))
        return {{"success", false}, {"cancelled", true}};
    if (index >= model.objects.size() || !model.objects[index] || model.objects[index]->id() != object_id ||
        object_overrides(*model.objects[index]) != overrides_before)
        throw std::runtime_error("The object or its settings changed while the dialog was open; try again");
    object = model.objects[index];

    plater->take_snapshot("Print Advisor: object settings");
    for (const auto& [key, value] : sets) {
        DynamicPrintConfig parsed;
        parsed.set_deserialize_strict(key, value);
        object->config.set_key_value(key, parsed.option(key)->clone());
    }
    for (const std::string& key : removes)
        object->config.erase(key);
    wxGetApp().obj_list()->object_config_options_changed({object, nullptr});
    plater->update();
    wxGetApp().obj_list()->update_and_show_object_settings_item();
    if (auto* panel = wxGetApp().params_panel()) {
        panel->notify_object_config_changed();
        panel->switch_to_object_if_has_object_configs();
    }
    json logged = json::object();
    for (const auto& [key, value] : sets)
        logged[key] = value;
    append_history({{"action", "object_settings"}, {"object", object->name}, {"set", logged}, {"removed", removes}});
    return {{"success", true}, {"object", object->name}, {"set", logged}, {"removed", removes}};
}

// User filament and process profiles, with parent, selection and whether Print Advisor made them.
nlohmann::json PrintAdvisorServer::list_profiles()
{
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle)
        throw std::runtime_error("PresetBundle is unavailable");
    json result = {{"success", true}};
    for (const auto& [class_name, collection] : {std::pair<const char*, PresetCollection*>{"filament", &bundle->filaments},
                                                  {"process", &bundle->prints}}) {
        json list = json::array();
        const std::string selected = collection->get_selected_preset_name();
        const std::unordered_set<std::string> history_names = advisor_history_names(class_name);
        for (const Preset& preset : (*collection)()) {
            if (preset.is_system || preset.is_default || !preset.is_visible)
                continue;
            bool has_children = false;
            for (const Preset& other : (*collection)())
                if (other.inherits() == preset.name)
                    has_children = true;
            list.push_back({{"name", preset.name}, {"parent", preset.inherits()}, {"selected", preset.name == selected},
                            {"created_by_advisor", advisor_created(preset.name, history_names)},
                            {"has_children", has_children},
                            {"compatible_with_selected_printer", preset.is_compatible}});
        }
        result[class_name] = std::move(list);
    }
    return result;
}

// Differences between two filament or process profiles, on the resolved values.
nlohmann::json PrintAdvisorServer::profile_diff(const std::string& body)
{
    const json request = json::parse(body);
    const std::string class_name = request.at("profile_class").get<std::string>();
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle || (class_name != "filament" && class_name != "process"))
        throw std::runtime_error("profile_class must be filament or process");
    PresetCollection& collection = class_name == "filament" ? bundle->filaments : bundle->prints;
    const std::string a = request.at("a").get<std::string>(), b = request.at("b").get<std::string>();
    const Preset* left = collection.find_preset(a, false);
    const Preset* right = collection.find_preset(b, false);
    if (!left || !right)
        throw std::runtime_error("Profile not found: " + std::string(left ? b : a));
    const auto& keys = class_name == "filament" ? Preset::filament_options() : Preset::print_options();
    json differences = json::array();
    for (const std::string& key : keys) {
        if (key == "inherits" || key == "filament_settings_id" || key == "print_settings_id" || key == "compatible_printers" ||
            key == "compatible_prints" || key == "compatible_printers_condition" || key == "compatible_prints_condition")
            continue;
        const ConfigOption* l = left->config.option(key);
        const ConfigOption* r = right->config.option(key);
        const std::string lv = l ? l->serialize() : std::string();
        const std::string rv = r ? r->serialize() : std::string();
        if (lv == rv || lv.size() > 400 || rv.size() > 400)
            continue;
        const ConfigOptionDef* def = print_config_def.get(key);
        differences.push_back({{"key", key}, {"label", def ? def->label : std::string()}, {"a", lv}, {"b", rv}});
        if (differences.size() >= 200)
            break;
    }
    return {{"success", true}, {"profile_class", class_name}, {"a", a}, {"b", b}, {"count", differences.size()},
            {"differences", differences}};
}

// Deletes an unused profile copy created by Print Advisor (also after a restart, via the
// history), after confirmation.
nlohmann::json PrintAdvisorServer::delete_profile(const std::string& body)
{
    const json request = json::parse(body);
    const std::string class_name = request.at("profile_class").get<std::string>();
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle || (class_name != "filament" && class_name != "process"))
        throw std::runtime_error("profile_class must be filament or process");
    const Preset::Type type = class_name == "filament" ? Preset::TYPE_FILAMENT : Preset::TYPE_PRINT;
    PresetCollection& collection = type == Preset::TYPE_FILAMENT ? bundle->filaments : bundle->prints;
    const std::string name = request.at("name").get<std::string>();
    const auto check = [&]() {
        const Preset* target = collection.find_preset(name, false);
        if (!target || target->is_system || target->is_default || target->is_project_embedded || target->is_external)
            throw std::runtime_error("Profile not found or not a user profile: " + name);
        if (!advisor_created(class_name, name))
            throw std::runtime_error("Only profile copies created by Print Advisor can be deleted from here");
        check_profile_unused(*bundle, type, name);
        for (const Preset& preset : collection())
            if (preset.inherits() == name)
                throw std::runtime_error("Another profile inherits from this copy");
    };
    check();
    if (!ask_user("Elimina profilo", "Eliminare il profilo " + class_name + " '" + sanitized_text(name) + "'?\n\nL'operazione non si può annullare.",
                  m_callback_stopping, {}, true))
        return {{"success", false}, {"cancelled", true}};
    check();
    if (!delete_preset_keep_selection(collection, name))
        throw std::runtime_error("MentorSlicer could not delete the profile");
    for (auto it = m_applied_operations.begin(); it != m_applied_operations.end();)
        it = it->second.type == type && it->second.created_name == name ? m_applied_operations.erase(it) : std::next(it);
    append_history({{"action", "deleted"}, {"profile_class", class_name}, {"profile", name}});
    if (Tab* tab = wxGetApp().get_tab(type))
        tab->update_tab_ui();
    if (wxGetApp().plater())
        wxGetApp().plater()->sidebar().update_presets(type);
    return {{"success", true}, {"deleted", name}};
}

// Saves a profile with its resolved values to a JSON file chosen by the user in a save dialog.
nlohmann::json PrintAdvisorServer::export_profile(const std::string& body)
{
    const json request = json::parse(body);
    const std::string class_name = request.at("profile_class").get<std::string>();
    auto* bundle = wxGetApp().preset_bundle;
    if (!bundle || (class_name != "filament" && class_name != "process"))
        throw std::runtime_error("profile_class must be filament or process");
    PresetCollection& collection = class_name == "filament" ? bundle->filaments : bundle->prints;
    const std::string name = request.at("name").get<std::string>();
    const Preset* preset = collection.find_preset(name, false);
    if (!preset)
        throw std::runtime_error("Profile not found: " + name);
    const auto& keys = class_name == "filament" ? Preset::filament_options() : Preset::print_options();
    json values = json::object();
    for (const std::string& key : keys)
        if (const ConfigOption* option = preset->config.option(key))
            values[key] = option->serialize();
    values["name"] = name;
    values["inherits"] = preset->inherits();
    values["from"] = "Print Advisor export";

    std::string file_name = sanitized_text(name, 80);
    for (char& c : file_name)
        if (std::string("<>:\"/\\|?*").find(c) != std::string::npos)
            c = '_';
    // Held by value: the server may be stopped and destroyed while the dialog is open.
    const auto stopping = m_callback_stopping;
    const auto cancelled = current_request_cancel_flag();
    wxFileDialog dialog(wxGetApp().mainframe, wxString::FromUTF8("Esporta profilo " + class_name), wxEmptyString,
                        wxString::FromUTF8(file_name + ".json"), "JSON (*.json)|*.json", wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    bool chosen = false;
    {
        // Nothing is half-applied while the user picks the file: read-only requests may run.
        AdvisorUserWait user_wait;
        chosen = dialog.ShowModal() == wxID_OK;
    }
    check_still_wanted(stopping, cancelled);
    if (!chosen)
        return {{"success", false}, {"cancelled", true}};
    // File name only in the reply: the folder the user chose is not reported to the MCP client.
    const std::string exported_file = wxFileName(dialog.GetPath()).GetFullName().ToUTF8().data();
    boost::filesystem::ofstream file(boost::filesystem::path(dialog.GetPath().ToStdWstring()), std::ios::binary | std::ios::trunc);
    file << values.dump(4, ' ', false, json::error_handler_t::replace);
    if (!file)
        throw std::runtime_error("Could not write the file");
    append_history({{"action", "exported"}, {"profile_class", class_name}, {"profile", name}});
    return {{"success", true}, {"exported", name}, {"file", exported_file}};
}

nlohmann::json PrintAdvisorServer::history()
{
    json entries = json::array();
    for (json& entry : read_history(200))
        entries.push_back(std::move(entry));
    // Relative to the MentorSlicer data folder.
    return {{"success", true}, {"entries", entries}, {"file", data_relative_path(history_path())}};
}

namespace {
// Process id and loopback port of an endpoint.json written by write_descriptor (by this app or,
// for the legacy probe, by Creality Print). False when the file is missing or unreadable.
bool read_endpoint_descriptor(const boost::filesystem::path& descriptor, unsigned long& pid, unsigned long& port)
{
    pid = 0;
    port = 0;
    try {
        boost::filesystem::ifstream input(descriptor, std::ios::binary);
        if (!input)
            return false;
        const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        const json existing = json::parse(contents, nullptr, false);
        if (!existing.is_object() || !existing.contains("pid") || !existing["pid"].is_number_unsigned())
            return false;
        pid = existing["pid"].get<unsigned long>();
        // "http://127.0.0.1:<port>", as written by write_descriptor.
        const std::string base_url = existing.value("base_url", std::string());
        static const std::string prefix = "http://127.0.0.1:";
        if (base_url.rfind(prefix, 0) == 0 && base_url.size() > prefix.size() && base_url.size() <= prefix.size() + 5 &&
            base_url.find_first_not_of("0123456789", prefix.size()) == std::string::npos)
            port = std::stoul(base_url.substr(prefix.size()));
    } catch (...) {
        return false;
    }
    return true;
}

// Tells the user why the server stays off (from the GUI thread) and throws PrintAdvisorOtherInstance.
[[noreturn]] void refuse_other_instance(const std::string& message, const std::string& reason)
{
    if (wxTheApp)
        wxTheApp->CallAfter([message] {
            wxWindow* parent = wxGetApp().mainframe;
            if (parent && (!wxTopLevelWindows.Find(parent) || parent->IsBeingDeleted()))
                parent = nullptr;
            wxMessageBox(wxString::FromUTF8(message), "MCP Advisor", wxOK | wxICON_INFORMATION, parent);
        });
    throw PrintAdvisorOtherInstance(reason);
}
} // namespace

void PrintAdvisorServer::check_no_other_instance()
{
    const boost::filesystem::path descriptor = boost::filesystem::path(data_dir()) / "cache" / "print-advisor" / "endpoint.json";
    unsigned long other = 0;
    unsigned long other_port = 0;
    // A descriptor left by a crash, or holding a process id now used by another program, is
    // replaced. One of another running MentorSlicer is kept: the instances share the data
    // folder, so the MCP sidecar can reach only one of them.
    // That MentorSlicer must also still serve the endpoint: its server may be off (the pid of
    // a crashed instance reused by the other copy) while its descriptor was not removed.
    if (read_endpoint_descriptor(descriptor, other, other_port) && other != 0 && other != current_process_id() &&
        process_alive(other) && runs_same_executable(other) && other_port != 0 && other_port <= 65535 &&
        loopback_port_answers(static_cast<unsigned short>(other_port))) {
        BOOST_LOG_TRIVIAL(warning) << "Print Advisor: another running MentorSlicer (pid " << other
                                   << ") serves the MCP endpoint; not starting a second server";
        refuse_other_instance("Print Advisor MCP è già attivo in un'altra finestra di MentorSlicer (PID " +
                                  std::to_string(other) + ").\n\nIn questa finestra resta spento: usa quella, oppure disattivalo "
                                  "lì (Integrazione AI → Print Advisor MCP → Disattiva) e attivalo qui.",
                              "Print Advisor MCP is already active in another MentorSlicer window (pid " + std::to_string(other) + ")");
    }
#ifdef _WIN32
    // PRINT-ADVISOR F8: the old Creality Print copy keeps its own data folder (the one MentorSlicer
    // imported once), so its descriptor is not the one above. While that copy still exists, refuse
    // to serve MCP next to it: both would run printer monitors. Remove with the old copy.
    const boost::filesystem::path legacy_descriptor =
        boost::filesystem::path(wxStandardPaths::Get().GetUserConfigDir().ToStdWstring()) /
        boost::filesystem::path(wxString::FromUTF8(DataMigration::LEGACY_CREALITY_DATA_RELATIVE).ToStdWstring()) / "cache" /
        "print-advisor" / "endpoint.json";
    if (legacy_descriptor.lexically_normal() != descriptor.lexically_normal() &&
        read_endpoint_descriptor(legacy_descriptor, other, other_port) && other != 0 && other != current_process_id() &&
        process_alive(other) && ::_wcsicmp(process_image_name(DWORD(other)).c_str(), DataMigration::LEGACY_CREALITY_EXE) == 0 &&
        other_port != 0 && other_port <= 65535 && loopback_port_answers(static_cast<unsigned short>(other_port))) {
        BOOST_LOG_TRIVIAL(warning) << "Print Advisor: the old Creality Print (pid " << other
                                   << ") serves the MCP endpoint; not starting a second server";
        refuse_other_instance("Print Advisor MCP è attivo nella vecchia Creality Print (PID " + std::to_string(other) +
                                  ").\n\nIn MentorSlicer resta spento: chiudi la vecchia copia, oppure disattivalo lì "
                                  "(Vista > Enable Print Advisor MCP), poi attivalo qui da Integrazione AI → Print Advisor MCP.",
                              "Print Advisor MCP is active in the old Creality Print (pid " + std::to_string(other) + ")");
    }
#endif
}

void PrintAdvisorServer::write_descriptor()
{
    const boost::filesystem::path directory = boost::filesystem::path(data_dir()) / "cache" / "print-advisor";
    boost::filesystem::create_directories(directory);
#ifndef _WIN32
    ::chmod(directory.string().c_str(), S_IRWXU);
#endif
    const boost::filesystem::path descriptor = directory / "endpoint.json";
    const boost::filesystem::path temporary = directory / "endpoint.json.tmp";
    const unsigned long process_id = current_process_id();
    // The rename below replaces an existing descriptor left by a crash or by a program that
    // reuses its process id; check_no_other_instance() refused to start when another running
    // MentorSlicer holds it. Nothing is removed beforehand.
    json value = {{"base_url", "http://127.0.0.1:" + std::to_string(m_port)}, {"token", m_token}, {"api_version", 1},
                  {"pid", process_id}};
    {
        boost::filesystem::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            throw std::runtime_error("Cannot create Print Advisor descriptor");
        output << value.dump();
        output.close();
        if (!output)
            throw std::runtime_error("Cannot write Print Advisor descriptor");
    }
#ifndef _WIN32
    ::chmod(temporary.string().c_str(), S_IRUSR | S_IWUSR);
#endif
    boost::system::error_code error;
    boost::filesystem::rename(temporary, descriptor, error);
    if (error) {
        boost::filesystem::remove(temporary);
        throw boost::system::system_error(error);
    }
    m_descriptor_path = descriptor.string();
}

void PrintAdvisorServer::remove_descriptor()
{
    if (m_descriptor_path.empty())
        return;
    // Another MentorSlicer instance may have replaced the descriptor since: remove it only
    // when it still holds this server's pid and token.
    try {
        boost::filesystem::ifstream input(boost::filesystem::path(m_descriptor_path), std::ios::binary);
        if (!input)
            return;
        const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        input.close();
        const json value = json::parse(contents, nullptr, false);
        if (!value.is_object() || !value.contains("pid") || !value["pid"].is_number_unsigned() ||
            value["pid"].get<unsigned long>() != current_process_id() || !value.contains("token") || !value["token"].is_string() ||
            !constant_time_equal(value["token"].get<std::string>(), m_token))
            return;
    } catch (...) {
        return;
    }
    boost::system::error_code ignored;
    boost::filesystem::remove(m_descriptor_path, ignored);
}

} // namespace GUI
} // namespace Slic3r
