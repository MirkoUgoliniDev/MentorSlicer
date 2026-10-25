#include "PrinterMgrView.hpp"
#include "../Device/LanDeviceProbe.hpp"

#include "../../I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/MsgDialog.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Notebook.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/PrinterCoverRoute.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/miniz_extension.hpp"
#include "libslic3r_version.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <chrono>
#include <mutex>
#include <regex>
#include <string>
#include <thread>
#include <utility>
#include <wx/sizer.h>
#include <wx/mstream.h>
#include <wx/string.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include "wx/evtloop.h"
#include <wx/thread.h>
#include <wx/window.h>

#include <slic3r/GUI/Widgets/WebView.hpp>
#include <wx/webview.h>
#include "slic3r/GUI/print_manage/RemotePrinterManager.hpp"
#include "slic3r/GUI/simple/printAdvisor/PrintAdvisorPageBridge.hpp" // PRINT-ADVISOR advisor-hook
#include <boost/algorithm/string/predicate.hpp>
#include <boost/beast/core/detail/base64.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <wx/strconv.h>
#include <sstream>
#include <vector>

#include <wx/stdpaths.h>
#include "../utils/cxmdns.h"
#include "slic3r/GUI/print_manage/Utils.hpp"
#include "wx/event.h"
#include "../data/DataCenter.hpp"
#include "../AppMgr.hpp"
#include "../PrinterMgr.hpp"
#include "../TypeDefine.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#if defined(__linux__) || defined(__LINUX__)
#include "video/WebRTCDecoder.h"
#endif
#include <slic3r/GUI/print_manage/AppUtils.hpp>
#if ENABLE_FFMPEG
#include "video/RTSPDecoder.h"
#endif
#include "buildinfo.h"
#include <cmath>
#include "../AppUtils.hpp"

namespace pt = boost::property_tree;

namespace Slic3r {
namespace GUI {

static std::string json_string_value(const nlohmann::json& value, const char* key)
{
    const auto it = value.find(key);
    return it != value.end() && it->is_string() ? it->get<std::string>() : std::string();
}

static nlohmann::json gcode_file_size_bytes(const std::string& file_path)
{
    if (file_path.empty())
        return nullptr;

    boost::system::error_code ec;
    const boost::filesystem::path path(file_path);
    if (!boost::filesystem::is_regular_file(path, ec) || ec)
        return nullptr;

    const auto file_size = boost::filesystem::file_size(path, ec);
    if (ec)
        return nullptr;

    return static_cast<std::uint64_t>(file_size);
}

static std::string normalize_device_mac(const std::string& mac)
{
    std::string normalized;
    normalized.reserve(mac.size());
    for (unsigned char ch : mac) {
        if (ch == ':' || ch == '-' || ch == '.' || std::isspace(ch))
            continue;
        if (!std::isxdigit(ch))
            return {};
        normalized.push_back(static_cast<char>(std::toupper(ch)));
    }
    return normalized.size() == 12 ? normalized : std::string();
}

static std::string json_identifier_value(const nlohmann::json& value, const char* key)
{
    const auto it = value.find(key);
    if (it == value.end())
        return {};
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_number_unsigned())
        return std::to_string(it->get<std::uint64_t>());
    if (it->is_number_integer())
        return std::to_string(it->get<std::int64_t>());
    return {};
}

struct DeviceWebViewHandle {
    wxWindowID id {wxID_NONE};
    std::uintptr_t address {0};
};

static DeviceWebViewHandle make_device_webview_handle(wxWebView* browser)
{
    if (!wxIsMainThread() || browser == nullptr)
        return {};

    return {browser->GetId(), reinterpret_cast<std::uintptr_t>(static_cast<wxWindow*>(browser))};
}

static wxWebView* resolve_device_webview(const DeviceWebViewHandle& handle)
{
    if (!wxIsMainThread() || handle.address == 0)
        return nullptr;

    wxWindow* window = wxWindow::FindWindowById(handle.id);
    if (window == nullptr || reinterpret_cast<std::uintptr_t>(window) != handle.address)
        return nullptr;

    wxWebView* browser = dynamic_cast<wxWebView*>(window);
    return browser != nullptr && !browser->IsBeingDeleted() ? browser : nullptr;
}

static void post_device_webview_command(const DeviceWebViewHandle& weak_browser,
                                        const std::string& command,
                                        nlohmann::json data)
{
    if (wxTheApp == nullptr)
        return;

    nlohmann::json message;
    message["command"] = command;
    message["data"] = std::move(data);
    const std::string encoded = RemotePrint::Utils::url_encode(message.dump());
    wxTheApp->CallAfter([weak_browser, encoded] {
        wxWebView* browser = resolve_device_webview(weak_browser);
        if (browser == nullptr)
            return;
        DM::AppUtils::PostMsg(
            browser, wxString::Format("window.handleStudioCmd('%s');", encoded).ToStdString());
    });
}

static bool has_printable_gcode_in_3mf(const std::string& file_path)
{
    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    if (!Slic3r::open_zip_reader(&archive, file_path))
        return false;

    bool found = false;
    const mz_uint file_count = mz_zip_reader_get_num_files(&archive);
    for (mz_uint index = 0; index < file_count; ++index) {
        mz_zip_archive_file_stat stat;
        if (!mz_zip_reader_file_stat(&archive, index, &stat) || stat.m_is_directory)
            continue;

        const std::string name = stat.m_filename;
        if (boost::istarts_with(name, "Metadata/") && boost::iends_with(name, ".gcode")) {
            found = true;
            break;
        }
    }

    Slic3r::close_zip_reader(&archive);
    return found;
}

/*
* 1.获取ip后,先验证tcp能否连上,httpsOpen/httpOpen
* 2.再连接对应的https 或 http
*/

PrinterMgrView::PrinterMgrView(wxWindow *parent)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
 {
    BOOST_LOG_TRIVIAL(warning) <<__FUNCTION__ << " Address: " << (void*) this;
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

      // Create the webview
    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " m_browser is null!!! ";
        wxLogError("Could not init m_browser");
        return;
    }
    const DeviceWebViewHandle browser_handle = make_device_webview_handle(m_browser);
    m_browser_handle_id.store(browser_handle.id, std::memory_order_release);
    m_browser_handle_address.store(browser_handle.address, std::memory_order_release);
#if defined(__linux__)
    WebView::ConfigureHardwareAccelerationForMjpeg(m_browser);
#endif
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " m_browser address: " << (void*) m_browser;

    m_ws_proxy = std::make_unique<WebSocketProxy::Manager>(
        [this](const std::string& script) {
            if (m_browser)
                run_script(script);
        });
    wxString ws_proxy_script = WebSocketProxy::GetWebSocketProxyScript();
    if (!ws_proxy_script.IsEmpty())
        m_browser->AddUserScript(ws_proxy_script);
    else
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " ws_proxy script empty, skip";
    // The page reads the old WiFi printers through the local /proxy, which needs the session token.
    m_browser->AddUserScript(wxString::FromUTF8(wxGetApp().get_server()->proxy_token_user_script()));

    m_browser->Bind(wxEVT_WEBVIEW_ERROR, &PrinterMgrView::OnError, this);
    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &PrinterMgrView::OnLoaded, this);
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterMgrView::OnScriptMessage, this, m_browser->GetId());

    SetSizer(topsizer);

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    //Zoom
    m_zoomFactor = 100;

    //Connect the idle events
    Bind(wxEVT_CLOSE_WINDOW, &PrinterMgrView::OnClose, this);

    RegisterHandler("save_user_operation_state", [this](const nlohmann::json& json_data) {
        this->handle_save_user_operation_state(json_data);
    });

    RegisterHandler("request_user_operation_state", [this](const nlohmann::json& json_data) {
        this->handle_request_user_operation_state(json_data);
    });

    RegisterHandler("request_device_address_correction", [this](const nlohmann::json&) {
        this->correct_device();
    });

    RegisterHandler("get_user_custom_color_list", [this](const nlohmann::json& json_data) {
        this->handle_get_user_custom_color_list(json_data);
    });

    RegisterHandler("set_user_custom_color_list", [this](const nlohmann::json& json_data) {
        this->handle_set_user_custom_color_list(json_data);
    });
    std::string version = std::string(CREALITYPRINT_VERSION);
    std::string os = wxGetOsDescription().ToStdString();
    std::string type = std::string(PROJECT_VERSION_EXTRA);
    int port = wxGetApp().get_server_port();
    int customized = 0;
    #ifdef CUSTOMIZED
        customized = 1;
    #endif
        wxString url = wxString::Format("%s/web/deviceMgr/index.html?version=%s&port=%d&os=%s&customized=%d&type=%s", from_u8(resources_dir()),
                                    version, port, os, customized, type);
        url.Replace(wxT("\\"), wxT("/"));
        url.Replace(wxT("#"), wxT("%23"));
        wxURI uri(url);
        wxString encodedUrl = uri.BuildURI();
        encodedUrl = wxT("file://")+encodedUrl;
        this->load_url(encodedUrl);
        m_browser->EnableAccessToDevTools();
    
    #ifdef __WXGTK__
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetDoubleBuffered(true);
    m_freshTimer = new wxTimer();
    m_freshTimer->SetOwner(this);
    m_freshTimer->Start(500);
    Bind(wxEVT_TIMER, [this](wxTimerEvent&){
        this->Refresh();
    });
    #endif
    DM::AppMgr::Ins().Register(m_browser, "PrinterMgrView");
    DM::AppMgr::Ins().RegisterEvents(m_browser, std::vector<std::string>{DM::EVENT_SET_CURRENT_DEVICE, DM::EVENT_FORWARD_DEVICE_DETAIL});
 }
PrinterMgrView::~PrinterMgrView()
{
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Address: " << (void*) this;
    m_browser_handle_address.store(0, std::memory_order_release);
    m_browser_handle_id.store(wxID_NONE, std::memory_order_release);
    SetEvtHandlerEnabled(false);
    m_scanExit.store(true, std::memory_order_release);
    if (m_browser) {
        m_browser->Unbind(wxEVT_WEBVIEW_ERROR, &PrinterMgrView::OnError, this);
        m_browser->Unbind(wxEVT_WEBVIEW_LOADED, &PrinterMgrView::OnLoaded, this);
        Unbind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrinterMgrView::OnScriptMessage, this, m_browser->GetId());
    }
    UnregisterHandler("save_user_operation_state");
    UnregisterHandler("request_user_operation_state");
    UnregisterHandler("get_user_custom_color_list");
    UnregisterHandler("set_user_custom_color_list");
    if (m_ws_proxy) {
        m_ws_proxy->Shutdown();
        m_ws_proxy.reset();
    }
#ifdef __WXGTK__
    m_freshTimer->Stop();
    m_browser->Stop();
    m_browser->RemoveScriptMessageHandler("wx");
#endif
    DM::AppMgr::Ins().UnRegister(m_browser);
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Start";
    if (m_scanPoolThread.joinable())
        m_scanPoolThread.join();
    if (m_deviceScanThread.joinable())
        m_deviceScanThread.join();
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " End";
}

void PrinterMgrView::load_url(const wxString& url)
{
    if (m_browser == nullptr)
        return;
    void* backend_before = m_browser->GetNativeBackend();
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << "[LOAD_URL_ACTION] START. webView=" << (void*) m_browser
                               << ", Backend Ptr BEFORE: " << backend_before
                               << ", URL: " << url.ToStdString();

    m_browser->LoadURL(url);

    void* backend_after = m_browser->GetNativeBackend();
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << "[LOAD_URL_ACTION] END (call returned). webView=" << (void*) m_browser
                               << ", Backend Ptr AFTER: " << backend_after;
    //m_browser->SetFocus();
    UpdateState();
}

void PrinterMgrView::on_switch_to_device_page()
{
    //update_which_device_is_current();

    // Switching to the device page no longer auto-opens the current device detail page.
    advisor_push_device_page({{"command", "advisor_open_single"}}); // PRINT-ADVISOR advisor-hook: web patch 19 opens the only printer
}

void PrinterMgrView::reload()
{
    void* backend_before = m_browser->GetNativeBackend();
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << "[LOAD_URL_ACTION] START. webView=" << (void*) m_browser
                               << ", Backend Ptr BEFORE: " << backend_before ;
    m_browser->Reload();
    void* backend_after = m_browser->GetNativeBackend();
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << "[LOAD_URL_ACTION] END (call returned). webView=" << (void*) m_browser
                               << ", Backend Ptr AFTER: " << backend_after;
}
/**
 * Method that retrieves the current state from the web control and updates the
 * GUI the reflect this current state.
 */
void PrinterMgrView::UpdateState() {
  // SetTitle(m_browser->GetCurrentTitle());

}

void PrinterMgrView::OnClose(wxCloseEvent& evt)
{
    this->Hide();
}

void PrinterMgrView::OnError(wxWebViewEvent &evt)
{
    auto e = "unknown error";
    switch (evt.GetInt()) {
      case wxWEBVIEW_NAV_ERR_CONNECTION:
        e = "wxWEBVIEW_NAV_ERR_CONNECTION";
#if wxUSE_WEBVIEW_EDGE
        #ifdef __WIN32__
        if (!wxGetApp().app_config->get_bool("webview_single_process") && !m_webview_loaded_successfully) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": WebView connection error, switch to single-process and restart.";
            wxGetApp().app_config->set_bool("webview_single_process", true);
            wxGetApp().app_config->save();
            wxMessageBox(_L("WebView failed to start. The application will switch to single-process mode and restart."),
                         _L("WebView"), wxOK | wxICON_INFORMATION);
            wxString exe_path = wxStandardPaths::Get().GetExecutablePath();
            wxString restart_cmd = wxString::Format("\"%s\"", exe_path);
            long pid = wxExecute(restart_cmd, wxEXEC_ASYNC);
            if (pid <= 0)
                BOOST_LOG_TRIVIAL(error) << "[WebViewRuntime] Failed to relaunch Creality Print after enabling single-process.";
            if (Slic3r::GUI::wxGetApp().mainframe)
                Slic3r::GUI::wxGetApp().mainframe->Close(true);
            else
                wxGetApp().ExitMainLoop();
            return;
        }
        #endif
        if (!m_webview_loaded_successfully && !m_bHasError)
        {
            m_bHasError = true;
            if (Slic3r::GUI::wxGetApp().mark_webview_runtime_repair_prompted())
                Slic3r::GUI::wxGetApp().reinstall_webview_runtime();
        }
        else if (m_webview_loaded_successfully)
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": WebView had loaded successfully before, skip repair prompt.";
        }
            
#endif
        break;
      case wxWEBVIEW_NAV_ERR_CERTIFICATE:
        e = "wxWEBVIEW_NAV_ERR_CERTIFICATE";
        break;
      case wxWEBVIEW_NAV_ERR_AUTH:
        e = "wxWEBVIEW_NAV_ERR_AUTH";
        break;
      case wxWEBVIEW_NAV_ERR_SECURITY:
        e = "wxWEBVIEW_NAV_ERR_SECURITY";
        break;
      case wxWEBVIEW_NAV_ERR_NOT_FOUND:
        e = "wxWEBVIEW_NAV_ERR_NOT_FOUND";
        break;
      case wxWEBVIEW_NAV_ERR_REQUEST:
        e = "wxWEBVIEW_NAV_ERR_REQUEST";
        break;
      case wxWEBVIEW_NAV_ERR_USER_CANCELLED:
        e = "wxWEBVIEW_NAV_ERR_USER_CANCELLED";
        break;
      case wxWEBVIEW_NAV_ERR_OTHER:
        e = "wxWEBVIEW_NAV_ERR_OTHER";
        break;
      }
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__<< boost::format(": error loading page %1% %2% %3% %4%") %evt.GetURL() %evt.GetTarget() %e %evt.GetString();
}

void PrinterMgrView::OnLoaded(wxWebViewEvent &evt)
{
    if (evt.GetURL().IsEmpty())
        return;

    DM::DeviceMgr::Ins().Load();
}

void PrinterMgrView::sendAllProgressWithRateLimit()
{
    std::lock_guard<std::mutex> lock(sendMutex);
    auto now = std::chrono::steady_clock::now();
    if (now - lastSendTime < std::chrono::milliseconds(500)) {
        return;
    }
    lastSendTime = now;

    nlohmann::json all_json;
    nlohmann::json items = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lk(m_uploadProgressMutex);
        for (const auto& kv : m_uploadProgressMap) {
            nlohmann::json item;
            item["ip"] = kv.first;
            item["progress"] = kv.second.progress;
            item["speed"] = std::round(kv.second.speed);
            items.push_back(item);
        }
    }
    all_json["items"] = items;

    std::string json_str = all_json.dump();
    nlohmann::json commandJson;
    commandJson["command"] = "upload_progress";
    commandJson["data"]    = RemotePrint::Utils::url_encode(json_str);

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));

    wxTheApp->CallAfter([this, strJS]() {
        try
        {
            if (!m_browser->IsBusy()) {
                run_script(strJS.ToStdString());
            }
        }
        catch (...)
        {
        }
    });
}

bool PrinterMgrView::request_check_upload_file_ready(const std::string& printer_ip,
                                                     const std::string& file_name,
                                                     std::uint64_t file_size,
                                                     int timeout_ms)
{
    if (printer_ip.empty() || file_name.empty() || file_size == 0)
        return false;

    if (wxIsMainThread()) {
        BOOST_LOG_TRIVIAL(error) << "PrinterMgrView::request_check_upload_file_ready cannot wait on UI thread.";
        return false;
    }

    const std::string request_id = "upload-file-ready-" + std::to_string(m_upload_file_ready_seq.fetch_add(1) + 1);
    {
        std::lock_guard<std::mutex> lock(m_upload_file_ready_mutex);
        m_upload_file_ready_results[request_id] = UploadFileReadyCheckResult{};
    }

    nlohmann::json payload;
    payload["request_id"] = request_id;
    payload["printer_ip"] = printer_ip;
    payload["file_name"] = file_name;
    payload["file_size"] = file_size;

    nlohmann::json commandJson;
    commandJson["command"] = "check_upload_file_ready";
    commandJson["data"] = payload;

    const wxString strJS = wxString::Format(
        "window.handleStudioCmd('%s');",
        RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));

    const std::uintptr_t browser_address = m_browser_handle_address.load(std::memory_order_acquire);
    const DeviceWebViewHandle weak_browser {
        m_browser_handle_id.load(std::memory_order_acquire),
        browser_address
    };
    wxGetApp().CallAfter([weak_browser, strJS, request_id]() {
        try {
            wxWebView* browser = resolve_device_webview(weak_browser);
            if (browser == nullptr) {
                BOOST_LOG_TRIVIAL(error) << "[UPLOAD_FILE_READY][dispatch_failed]"
                                         << " request_id=" << request_id
                                         << ", reason=device_webview_unavailable";
                return;
            }
            WebView::RunScript(browser, strJS.ToStdString());
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "[UPLOAD_FILE_READY][dispatch_failed]"
                                     << " request_id=" << request_id
                                     << ", exception=" << e.what();
        } catch (...) {
            BOOST_LOG_TRIVIAL(error) << "[UPLOAD_FILE_READY][dispatch_failed]"
                                     << " request_id=" << request_id
                                     << ", exception=unknown";
        }
    });

    std::unique_lock<std::mutex> lock(m_upload_file_ready_mutex);
    const bool completed = m_upload_file_ready_cv.wait_for(
        lock,
        std::chrono::milliseconds(std::max(timeout_ms, 1)),
        [&]() {
            auto it = m_upload_file_ready_results.find(request_id);
            return it != m_upload_file_ready_results.end() && it->second.completed;
        });

    bool ready = false;
    std::string message;
    auto it = m_upload_file_ready_results.find(request_id);
    if (it != m_upload_file_ready_results.end()) {
        ready = completed && it->second.ready;
        message = it->second.message;
        m_upload_file_ready_results.erase(it);
    }

    BOOST_LOG_TRIVIAL(info) << "PrinterMgrView::request_check_upload_file_ready"
                            << " request_id=" << request_id
                            << ", printer_ip=" << printer_ip
                            << ", file_name=" << file_name
                            << ", file_size=" << file_size
                            << ", completed=" << completed
                            << ", ready=" << ready
                            << ", message=" << message;
    return ready;
}

std::string filterInvalidFileNameChars(const std::string& input) {
    std::string result = input;
    for (char& c : result) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '-';
        }
    }
    return result;
}

void PrinterMgrView::OnScriptMessage(wxWebViewEvent& evt)
{
    try
    {

        auto incoming = json::parse(evt.GetString().ToUTF8().data()); // PRINT-ADVISOR advisor-hook: upstream is `const auto`, non-const so the snapshot payload can be moved
        if (advisor_handle_page_message(incoming)) return;             // PRINT-ADVISOR advisor-hook
        if (DM::AppMgr::Ins().Invoke(m_browser, evt.GetString().ToUTF8().data()))
        {
            return;
        }

        wxString strInput = evt.GetString();
        BOOST_LOG_TRIVIAL(trace) << "DeviceDialog::OnScriptMessage;OnRecv:" << strInput.c_str();
        json     j = json::parse(strInput);

        wxString strCmd = j["command"].get<std::string>();
        BOOST_LOG_TRIVIAL(trace) << "DeviceDialog::OnScriptMessage;Command:" << strCmd;
        
        if (strCmd == "ws_proxy") {
            if (j.contains("payload")) {
                if (m_ws_proxy)
                    m_ws_proxy->HandleCommand(j["payload"]);
                return;
            }
        }
        if (strCmd == "set_device_detail_state") {
            if (m_ws_proxy) {
                WebSocketProxy::DeviceDetailState state;
                state.visible = j.value("visible", false);
                state.source = j.value("source", std::string("none"));
                state.address = j.value("address", std::string());
                state.mac = j.value("mac", std::string());
                m_ws_proxy->SetDeviceDetailState(std::move(state));
            }
            return;
        }
        if (strCmd == "start_time_lapse_share") {
            // Fork: sharing time-lapse videos to Creality Cloud was removed (F4). Answer with an
            // error so the share dialog of the page leaves its waiting state.
            nlohmann::json data = {{"requestId", json_identifier_value(j, "requestId")},
                                   {"stage", "error"},
                                   {"code", "not_available"},
                                   {"message", "Sharing time-lapse videos to Creality Cloud is not available in this build"}};
            nlohmann::json command_json = {{"command", "time_lapse_share_error"}, {"data", std::move(data)}};
            run_script(wxString::Format("window.handleStudioCmd('%s');",
                RemotePrint::Utils::url_encode(command_json.dump(-1, ' ', true, nlohmann::json::error_handler_t::replace)))
                .ToStdString());
            return;
        }
        if (strCmd == "cancel_time_lapse_share")
            return; // nothing is ever running
        if (strCmd == "get_printer_progress")
        {
            //get all uploading progress
            sendAllProgressWithRateLimit();
        }
        if (strCmd == "send_gcode")
        {
            int plateIndex = j["plateIndex"];
            std::string ipAddress = j["ipAddress"].get<std::string>();
            std::string uploadName = j["uploadName"].get<std::string>();
            bool oldPrinter = j["oldPrinter"];
            int  moonrakerPort = j["moonrakerPort"];
            const bool secureConnection = j.value("secureConnection", false);

            RemotePrint::RemotePrinterManager::getInstance().setSecureConnectionMap(ipAddress, secureConnection);

            if (oldPrinter)
            {
                std::string strIpAddr = ipAddress;
                RemotePrint::RemotePrinterManager::getInstance().setOldPrinterMap(strIpAddr);
            }
            if (moonrakerPort > 0)
            {
                std::string strIpAddr = ipAddress;
                if (strIpAddr.find('(') != std::string::npos)
                {
                    RemotePrint::RemotePrinterManager::getInstance().setKlipperPrinterMap(strIpAddr, moonrakerPort);
                }
            }

            PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_plate(plateIndex);
            if (plate)
            {
                std::string gcodeFilePath;
                if (wxGetApp().plater()->only_gcode_mode())
                {
                   GCodeProcessorResult* plate_gcode_result = plate->get_slice_result();
                    if (plate_gcode_result)
                    {
                        gcodeFilePath = plate_gcode_result->filename;
                    }

                    if (gcodeFilePath.empty())
                        return;

                }else{
                    gcodeFilePath = _L(plate->get_tmp_gcode_path()).ToUTF8();
                }

                // Release GCodeViewer's mapped_file_source (same as export_3mf flow)
                wxGetApp().plater()->get_preview_canvas3D()->get_gcode_viewer().release_gcode_file_mapping();

                RemotePrint::RemotePrinterManager::getInstance().pushUploadMultTasks(ipAddress, uploadName, gcodeFilePath,
                    [this, j](std::string ip, float progress, double speed) {
                        // ????????????????????????????????????j????????
                        {
                            std::lock_guard<std::mutex> lk(m_uploadProgressMutex);
                            m_uploadProgressMap[ip] = ProgressInfo{progress, speed};
                        }
                        sendAllProgressWithRateLimit();
                    },
                    [this, j](std::string ip, int statusCode) {
                        nlohmann::json top_level_json;
                        top_level_json["ip"] = ip;
                        top_level_json["statusCode"]  = statusCode;
                        std::string json_str = top_level_json.dump();

                        nlohmann::json commandJson;
                        commandJson["command"] = "upload_status";
                        commandJson["data"] = RemotePrint::Utils::url_encode(json_str);
                        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));

                        wxTheApp->CallAfter([this, strJS, ip, statusCode]() {
                            try
                            {
                                if (!m_browser->IsBusy()) {
                                    run_script(strJS.ToStdString());
                                }
                            }
                            catch (...)
                            {
                            }
                        });
                    },
                    [this, j](std::string ip, std::string body){
                        int deviceType = 0;//local device
                        int statusCode = 1;
                        json jBody = json::parse(body);
                        if (jBody.contains("code") && jBody["code"].is_number_integer()) {
                            statusCode = jBody["code"];
                        }

                        nlohmann::json top_level_json;
                        top_level_json["ip"] = ip;
                        top_level_json["statusCode"] = statusCode;
                        top_level_json["id"] = "";
                        top_level_json["name"] = "";
                        top_level_json["type"] = "";
                        top_level_json["filekey"] = "";
                        if(jBody.contains("result") && jBody["result"].contains("list") &&jBody["result"]["list"].size()>=0){
                            deviceType = 1;//CX device
                            if(jBody["result"]["list"][0].contains("id"))top_level_json["id"]=jBody["result"]["list"][0]["id"];
                            if(jBody["result"]["list"][0].contains("name"))top_level_json["name"]=jBody["result"]["list"][0]["name"];
                            if(jBody["result"]["list"][0].contains("type"))top_level_json["type"]=jBody["result"]["list"][0]["type"];
                            if(jBody["result"]["list"][0].contains("filekey"))top_level_json["filekey"]=jBody["result"]["list"][0]["filekey"];
                        }

                        std::string json_str;
                        json_str = top_level_json.dump(-1, ' ', true);

                        nlohmann::json commandJson;
                        commandJson["command"] = "upload_complete";
                        commandJson["data"] = RemotePrint::Utils::url_encode(json_str);

                        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));

                        wxTheApp->CallAfter([this, strJS, ip]() {
                            try
                            {
                                if (!m_browser->IsBusy()) {
                                    run_script(strJS.ToStdString());
                                }
                            }
                            catch (...)
                            {
                            }
                        });
                        // ???????????????? IP ?L??????
                        {
                            std::lock_guard<std::mutex> lk(m_uploadProgressMutex);
                            m_uploadProgressMap.erase(ip);
                        }

              });
            }
        }else if(strCmd == "send_start_print_cmd")
        {
            nlohmann::json commandJson;
            commandJson["command"] = "send_print_cmd";
            commandJson["data"] = j["data"].dump(-1, ' ', true);
            ExecuteScriptCommand(RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
        }else if(strCmd == "down_files")
        {
            std::string address = j.value("address", "");
            bool secureConnection = j.value("secureConnection", false);
            std::string path_type = j["file_type"];

            if (address.empty() || !j.contains("files") || !j["files"].is_array()) {
                return;
            }

            wxDirDialog dlg(this, _L("Please Select"), "", wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);

            if (dlg.ShowModal() != wxID_OK) {
                return;
            }
            wxString path = dlg.GetPath();
            std::vector<DownloadItem> download_items;
            for (const auto& file : j["files"])
            {
                if (!file.is_object() || !file.contains("name") || !file.contains("path")) {
                    continue;
                }

                DownloadItem item;
                item.name = file["name"].get<std::string>();
                item.path = file["path"].get<std::string>();
                download_items.push_back(item);
            }

            if (download_items.empty()) {
                return;
            }

            const std::string save_path = path.ToUTF8().data();
            std::vector<DownloadItem> confirmed_items;
            confirmed_items.reserve(download_items.size());

            for (const auto& item : download_items)
            {
                const std::string safe_name = filterInvalidFileNameChars(item.name);
                boost::filesystem::path target_path = save_path;
                target_path /= safe_name;

                if (boost::filesystem::exists(target_path))
                {
                    MessageDialog overwrite_dialog(
                        this,
                        wxString::Format(
                            _L("A file exists with the same name: %s, do you want to override it."),
                            from_u8(safe_name)),
                        _L("Overwrite file"),
                        wxYES_NO | wxNO_DEFAULT);

                    if (overwrite_dialog.ShowModal() != wxID_YES) {
                        continue;
                    }
                }

                confirmed_items.push_back(item);
            }

            if (confirmed_items.empty()) {
                return;
            }

            this->down_files(address, secureConnection, confirmed_items, save_path, path_type);


        }
        else if (strCmd == "scan_device")
        {
            scan_device(json_string_value(j, "requestId"));
        }
        else if (m_commandHandlers.find(strCmd.ToStdString()) != m_commandHandlers.end())
        {
            m_commandHandlers[strCmd.ToStdString()](j);
        }
        else if (strCmd == "req_device_move_direction")
        {
            std::string presetName = j["preset_name"];
            std::string address = j["address"];
            int direction = 0;
            int machine_LED_light_exist = 0;
            int auxiliary_fan = 0;
            int support_air_filtration = 0;
            int machine_ptc_exist = 0;
            Preset*     preset = Slic3r::GUI::wxGetApp().preset_bundle->printers.find_preset(presetName);
            if (preset != nullptr) {
                if(preset->config.has("machine_platform_motion_enable"))
                {
                    const ConfigOption* option = preset->config.option("machine_platform_motion_enable");
                    bool b = option->getBool();
                    direction = b ? 1 : 0;
                }
                if (preset->config.has("support_air_filtration"))
                {
                    const ConfigOption* option = preset->config.option("support_air_filtration");
                    bool b = option->getBool();
                    support_air_filtration = b ? 1 : 0;
                }
                if (preset->config.has("machine_LED_light_exist"))
                {
                    const ConfigOption* option = preset->config.option("machine_LED_light_exist");
                    bool b = option->getBool();
                    machine_LED_light_exist = b ? 1 : 0;
                }
                if (preset->config.has("auxiliary_fan"))
                {
                    const ConfigOption* option = preset->config.option("auxiliary_fan");
                    bool b = option->getBool();
                    auxiliary_fan = b ? 1 : 0;
                }
                if(preset->config.has("machine_ptc_exist"))
                {
                    const ConfigOption* option = preset->config.option("machine_ptc_exist");
                    bool b = option->getBool();
                    machine_ptc_exist     = b ? 1 : 0;
                }
                // PRINT-ADVISOR BEGIN chamber-ui: the Creality K1 family (K1, K1C, K1 Max, K1 SE...) has no
                // chamber heater although its profiles ship machine_ptc_exist = 1; same rule as the filament
                // tab (Tab.cpp). Without the PTC flag the page shows the Advisor chamber card (web patches
                // 1, 7 and 17: preheat state and the Preriscalda/Stop button) instead of a target field.
                if (machine_ptc_exist == 1 && preset->config.has("printer_model")) {
                    const std::string printer_model = preset->config.opt_string("printer_model");
                    const std::string k1_prefix     = "Creality K1";
                    const bool        k1_family     = boost::algorithm::starts_with(printer_model, k1_prefix) &&
                        (printer_model.size() == k1_prefix.size() || printer_model[k1_prefix.size()] < '0' || printer_model[k1_prefix.size()] > '9');
                    const bool heater = preset->config.has("support_chamber_temp_control") && preset->config.opt_bool("support_chamber_temp_control");
                    if (k1_family && !heater)
                        machine_ptc_exist = 0;
                }
                // PRINT-ADVISOR END
                nlohmann::json commandJson;
                nlohmann::json  dataJson;
                commandJson["command"] = "req_device_move_direction";
                dataJson["direction"]  = direction;
                dataJson["machine_LED_light_exist"]  = machine_LED_light_exist;
                dataJson["auxiliary_fan"]  = auxiliary_fan;
                dataJson["support_air_filtration"]  = support_air_filtration;
                dataJson["machine_ptc_exist"]  = machine_ptc_exist;
                
                dataJson["address"]    = address;
                commandJson["data"]    = dataJson;
                wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandJson.dump());
                run_script(strJS.ToStdString());
            }
        }
        else if (strCmd == "get_machine_list")
        {
            m_webview_loaded_successfully = true;
            load_machine_preset_data();
        }else if(strCmd == "switch_webrtc_source")
        {
            std::string ip = j["ip"];
            std::string video_url = (boost::format("http://%1%:8000/call/webrtc_local") % ip).str();
            bool isOrderPrinter = j["isOrderPrinter"].get<bool>();
            if(isOrderPrinter)
            {
                #if ENABLE_FFMPEG
                // For order printer, we use the webrtc local url
                video_url = (boost::format("rtsp://%1%/ch0_0") % ip).str();
                RTSPDecoder::GetInstance()->startPlay(video_url); 
                #endif
            }
            else
            {
                #if defined(__linux__) || defined(__LINUX__)
                    WebRTCDecoder::GetInstance()->startPlay(video_url); 
                #endif
            }

        }
        else if (strCmd == "get_file_List_from_lan_device")
        {
            std::string strIp = j["url"];
            getFileListFromLanDevice(strIp);
        }
        else if (strCmd == "delete_file_from_lan_device")
        {
            std::string strIp = j["url"];
            std::string strName = j["name"];

            deleteFileListFromLanDevice(strIp,strName);
        }
        else if (strCmd == "uploade_file_oldPrinter")
        {
            std::string strIp = j["url"];

            int num = uploadeFileLanDevice(strIp);
            nlohmann::json commandJson;
            commandJson["data"] = num;
            commandJson["command"] = "uploade_file_oldPrinter";
            wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandJson.dump());
            run_script(strJS.ToStdString());
        }
        else if (strCmd == "upload_device_file")
        {
            uploadDeviceFile(j.value("address", std::string()),
                             j.value("requestId", std::string()));
        }
        else if (strCmd == "browse_fluidd_ca_file")
        {
            wxString path = openCAFile();
            if (path.IsEmpty())
                return ;
            nlohmann::json commandJson;
            commandJson["data"] = path.ToUTF8().data();
            commandJson["command"] = "browse_fluidd_ca_file";
            wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
            run_script(strJS.ToStdString());
        }
        else if (strCmd == "cancel_upload")
        {
            wxString ipAddress  = j["ipAddress"].get<std::string>();
            std::string ip = ipAddress.ToStdString();
            RemotePrint::RemotePrinterManager::getInstance().cancelUpload(ip);
        }
        else if (strCmd == "check_upload_file_ready_result")
        {
            const std::string request_id = j.value("request_id", std::string());
            const bool ready = j.value("ready", false);
            const std::string message = j.value("message", std::string());
            {
                std::lock_guard<std::mutex> lock(m_upload_file_ready_mutex);
                auto it = m_upload_file_ready_results.find(request_id);
                if (it != m_upload_file_ready_results.end()) {
                    it->second.completed = true;
                    it->second.ready = ready;
                    it->second.message = message;
                }
            }
            m_upload_file_ready_cv.notify_all();
        }
        else if (strCmd == "diagnosis_lan_connect")
        {
            const std::string ip = j.value("ip", std::string());
            const bool secure_connection = j.value("secureConnection", false);
            const int wss_port = j.value("wssPort", 0);
            Slic3r::create_thread([this, ip, secure_connection, wss_port] {
                int result = DM::LANConnectCheck::checkLan(
                    ip, secure_connection, wss_port, _ctrl);
                nlohmann::json commandJson;
                nlohmann::json resultJson;
                commandJson["command"] = "diagnosis_lan_connect_result";
                resultJson["ip"] = ip;
                resultJson["errorcode"] = result;
                commandJson["data"] = resultJson;
                wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandJson.dump());
                wxGetApp().CallAfter([this, strJS] { run_script(strJS.ToStdString()); });
            });
        }
        else if (strCmd == "retry_upload") {
            wxString ipAddress = j["ipAddress"].get<std::string>();
            RemotePrint::RemotePrinterManager::getInstance().retryUpload(ipAddress.ToStdString());
        }
        else if (strCmd == "diagnosis_close_cmd")
        {
            _ctrl.requestStop();
            _ctrl.reset();
        }
        else {
            BOOST_LOG_TRIVIAL(trace) << "PrinterMgrView::OnScriptMessage;Unknown Command:" << strCmd;
        }
    }
    catch (std::exception& e)
    {
        // wxMessageBox(e.what(), "json Exception", MB_OK);
        BOOST_LOG_TRIVIAL(trace) << "DeviceDialog::OnScriptMessage;Error:" << e.what();
    }

}

std::string PrinterMgrView::get_plate_data_on_show()
{
    try {
    nlohmann::json json_array = nlohmann::json::array();

    std::vector<std::string> extruder_colors = Slic3r::GUI::wxGetApp().plater()->get_extruder_colors_from_plater_config();
    nlohmann::json           colors_json     = nlohmann::json::array();
    for (const auto& color : extruder_colors) {
        colors_json.push_back(color);
    }

    std::vector<std::string> filament_presets = wxGetApp().preset_bundle->filament_presets;

    std::vector<std::string> filament_types;
    nlohmann::json filament_types_json = nlohmann::json::array();

    for (const auto& preset_name : filament_presets) {
        std::string     filament_type;
        Slic3r::Preset* preset = wxGetApp().preset_bundle->filaments.find_preset(preset_name);
        if (preset) {
            preset->get_filament_type(filament_type);
            filament_types_json.push_back(filament_type);
            filament_types.emplace_back(filament_type);
        }
    }

    // Append types for mixed (virtual) filaments to stay in sync with
    // extruder_colors which already includes mixed display colors.
    {
        const auto& mixed        = wxGetApp().preset_bundle->mixed_filaments.mixed_filaments();
        size_t      num_physical = filament_types.size();
        for (const auto& mf : mixed) {
            if (!mf.enabled || mf.deleted)
                continue;
            std::string ft;
            if (mf.component_a >= 1 && mf.component_a <= num_physical) {
                ft = filament_types[mf.component_a - 1];
            } else {
                ft = "PLA";
            }
            filament_types.emplace_back(ft);
            filament_types_json.push_back(ft);
        }
    }

    for (int i = 0; i < wxGetApp().plater()->get_partplate_list().get_plate_count(); i++) {
        PartPlate* plate = wxGetApp().plater()->get_partplate_list().get_plate(i);
        if (plate && !plate->empty() && plate->is_slice_result_valid() && plate->is_slice_result_ready_for_print() && plate->thumbnail_data.is_valid()) {
            wxImage image(plate->thumbnail_data.width, plate->thumbnail_data.height);
            image.InitAlpha();
            for (unsigned int r = 0; r < plate->thumbnail_data.height; ++r) {
                unsigned int rr = (plate->thumbnail_data.height - 1 - r) * plate->thumbnail_data.width;
                for (unsigned int c = 0; c < plate->thumbnail_data.width; ++c) {
                    unsigned char* px = (unsigned char*) plate->thumbnail_data.pixels.data() + 4 * (rr + c);
                    image.SetRGB((int) c, (int) r, px[0], px[1], px[2]);
                    image.SetAlpha((int) c, (int) r, px[3]);
                }
            }

            wxImage resized_image = image.Rescale(50, 50, wxIMAGE_QUALITY_HIGH);

            wxMemoryOutputStream mem_stream;
            if (!resized_image.SaveFile(mem_stream, wxBITMAP_TYPE_PNG)) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": thumbnail SaveFile PNG->memory failed"
                                         << " | plate_index=" << plate->get_index()
                                         << " | src_w=" << image.GetWidth()
                                         << " | src_h=" << image.GetHeight()
                                         << " | dst_w=" << resized_image.GetWidth()
                                         << " | dst_h=" << resized_image.GetHeight()
                                         << " | slice_ready=" << plate->is_slice_result_ready_for_print()
                                         << " | thumb_valid=" << plate->thumbnail_data.is_valid();
                boost::log::core::get()->flush();
            }

            auto size = mem_stream.GetSize();
            // '?????????/????????????????�
            std::vector<unsigned char> imgdata(size);
            if (size > 0) {
                mem_stream.CopyTo(imgdata.data(), size);
            } else {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": thumbnail memory stream size is zero after SaveFile"
                                         << " | plate_index=" << plate->get_index()
                                         << " | dst_w=" << resized_image.GetWidth()
                                         << " | dst_h=" << resized_image.GetHeight()
                                         << " | png_type=wxBITMAP_TYPE_PNG";
                boost::log::core::get()->flush();
            }

            std::size_t encoded_size = boost::beast::detail::base64::encoded_size(size);
            std::string img_base64_data(encoded_size, '\0');
            if (size > 0) {
                boost::beast::detail::base64::encode(&img_base64_data[0], imgdata.data(), size);
            }

            std::string default_gcode_name = "";

            std::vector<int> plate_extruders = plate->get_extruders(true);

            if (Slic3r::GUI::wxGetApp().plater()->only_gcode_mode()) {
                wxString   last_loaded_gcode = Slic3r::GUI::wxGetApp().plater()->get_last_loaded_gcode();
                wxFileName fileName(last_loaded_gcode);
                default_gcode_name = std::string(fileName.GetName().ToUTF8().data()) + ".gcode";
                plate_extruders    = Slic3r::GUI::wxGetApp().plater()->get_gcode_extruders_in_only_gcode_mode();
            }
            else {
                // {m_pathname=L"Cone_PLA_10m45s.gcode" }
                ModelObjectPtrs plate_objects = plate->get_objects_on_this_plate();
                std::string     obj0_name     = ""; // the first object's name
                if (plate_objects.size() > 0 && nullptr != plate_objects[0]) {
                    obj0_name = plate_objects[0]->name;
                }

                auto                                  plate_print_statistics = plate->get_slice_result()->print_statistics;
                const PrintEstimatedStatistics::Mode& plate_time_mode =
                    plate_print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)];

                if (plate_extruders.size() > 0) {
                    // ??????????? plate_extruders[0]-1 ??????? filament_types
                    int extruder_index = plate_extruders[0] - 1;
                    if (extruder_index >= 0 && extruder_index < static_cast<int>(filament_types.size())) {
                        default_gcode_name = obj0_name + "_" + filament_types[extruder_index] + "_" +
                                             get_bbl_time_dhms(plate_time_mode.model_time_s()) + ".gcode";
                    } else {
                        BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                                                 << ": invalid extruder index when composing default_gcode_name"
                                                 << " | plate_index=" << plate->get_index()
                                                 << " | obj0_name=" << obj0_name
                                                 << " | print_time=" << get_bbl_time_dhms(plate_time_mode.model_time_s())
                                                 << " | extruder_index=" << extruder_index
                                                 << " | filament_types_size=" << filament_types.size();
                        default_gcode_name = obj0_name + "_" + get_bbl_time_dhms(plate_time_mode.model_time_s()) + ".gcode";
                        boost::log::core::get()->flush();
                    }
                } else {
                    default_gcode_name = "plate" + std::to_string(i + 1) + ".gcode";
                }
            }

            nlohmann::json json_data;
            json_data["image"]              = "data:image/png;base64," + std::move(img_base64_data);
            json_data["plate_index"]        = plate->get_index();
            json_data["upload_gcode__name"] = std::move(default_gcode_name);

            std::string gcode_path;
            if (Slic3r::GUI::wxGetApp().plater()->only_gcode_mode()) {
                if (const GCodeProcessorResult* slice_result = plate->get_slice_result())
                    gcode_path = slice_result->filename;
            } else {
                gcode_path = plate->get_tmp_gcode_path();
            }
            json_data["gcode_size"] = gcode_file_size_bytes(gcode_path);

            nlohmann::json extruders_json = nlohmann::json::array();
            for (const auto& extruder : plate_extruders) {
                extruders_json.push_back(extruder);
            }
            json_data["plate_extruders"] = extruders_json;

            json_array.push_back(json_data);
        }
    }

    nlohmann::json top_level_json;
    top_level_json["extruder_colors"] = std::move(colors_json);
    top_level_json["filament_types"]    = std::move(filament_types_json);
    top_level_json["plates"]          = std::move(json_array);

    std::string json_str         = top_level_json.dump(-1, ' ', true);

    // create command to send to the webview
    nlohmann::json commandJson;
    commandJson["command"] = "update_plate_data";
    commandJson["data"]    = RemotePrint::Utils::url_encode(json_str);

    std::string commandStr = commandJson.dump(-1, ' ', true);
    return RemotePrint::Utils::url_encode(commandStr);
    }
    catch (const std::bad_alloc& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": OOM while building plate data. " << e.what();
        boost::log::core::get()->flush();
        wxMessageBox(_L("Out of memory. Please save the current project and exit the application."), _L("Out of memory"), wxOK | wxICON_ERROR);
        nlohmann::json commandJson;
        commandJson["command"] = "update_plate_data";
        commandJson["data"]    = RemotePrint::Utils::url_encode("{\"plates\":[],\"extruder_colors\":[],\"filament_types\":[]}");
        return RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true));
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": Exception while building plate data. " << e.what();
        boost::log::core::get()->flush();
        wxMessageBox(_L("An error occurred: Failed to generate plate data."), _L("Error"), wxOK | wxICON_ERROR);
        nlohmann::json commandJson;
        commandJson["command"] = "update_plate_data";
        commandJson["data"]    = RemotePrint::Utils::url_encode("{\"plates\":[],\"extruder_colors\":[],\"filament_types\":[]}");
        return RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true));
    }
}

bool PrinterMgrView::Show(bool show) 
{
    bool result = wxPanel::Show(show);

    if (show && !m_plate_data_sent_on_show) {
        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", get_plate_data_on_show());
        run_script(strJS.ToStdString());
        m_plate_data_sent_on_show = true;
    }

    return result;
}

void PrinterMgrView::run_script(std::string content)
{
    if (m_browser == nullptr || m_browser->IsBeingDeleted())
        return;
    WebView::RunScript(m_browser, content);
}

void PrinterMgrView::down_files(const std::string& address, bool secureConnection, const std::vector<DownloadItem>& download_items, std::string savePath, std::string path_type )
{
        boost::thread import_thread = Slic3r::create_thread([savePath, address, secureConnection, download_items, this] {
            for (const auto& item : download_items)
            {
                boost::filesystem::path target_path = savePath;
                target_path = target_path / filterInvalidFileNameChars(item.name);
                const std::string scheme = secureConnection ? "https" : "http";
                const std::string download_info = scheme + "://" + address + item.path;
                wxString download_url(download_info.c_str());
                fs::path tmp_path = target_path;
                tmp_path += wxSecretString::Format("%s", ".download");

                auto filesize = 0;
                bool size_limit = false;
                auto http = Http::get(download_url.ToStdString());
                if (secureConnection) {
                    http.ca_file(Slic3r::resources_dir() + "/cert/ca.crt")
                        .ssl_verify_peer(true)
                        .ssl_verify_host(false).ssl_ignore_certificate_time(true);
                }

                http.on_progress([filesize, size_limit, this](Http::Progress progress, bool& cancel) {
                        if (progress.dltotal != 0) {
                            int percent = progress.dlnow * 100 / progress.dltotal;

                            nlohmann::json commandJson;
                            commandJson["command"] = "update_download_progress";
                            commandJson["data"]    = percent;

                            wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump()));
                            wxGetApp().CallAfter([this, strJS] { run_script(strJS.ToStdString()); });
                        }
                    })
                    .on_error([this](std::string body, std::string error, unsigned http_status) {
                        (void) body;
                        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format("Error getting: HTTP %1%, %2%")%http_status%error;
                        nlohmann::json commandJson;
                        commandJson["command"] = "update_download_progress";
                    
                        commandJson["data"]    = -2;

                        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump()));
                        wxGetApp().CallAfter([this, strJS] { run_script(strJS.ToStdString()); });
                        return;
                    }).on_complete([tmp_path, target_path](std::string body, unsigned http_status) {
                        if(http_status!=200){
                            return ;
                        }
                        fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
                        file.write(body.c_str(), body.size());
                        file.close();
                        if(fs::exists(target_path)){
                            fs::remove(target_path);
                        }
                        fs::rename(tmp_path, target_path);
                    })
                    .perform_sync();
            }
        });
    
}
void PrinterMgrView::correct_device()
{
    const DeviceWebViewHandle weak_browser = make_device_webview_handle(m_browser);
    if (m_scanPoolThread.joinable() && !m_scanPoolThread.try_join_for(boost::chrono::seconds(0))) {
        post_device_webview_command(weak_browser, "device_address_correction_result", {
            {"accepted", false},
            {"completed", false},
            {"busy", true}
        });
        return;
    }

    struct StoredDeviceSnapshot {
        std::string mac;
        std::string address;
        std::size_t count {0};
    };

    std::unordered_map<std::string, StoredDeviceSnapshot> stored_devices;
    const nlohmann::json persisted_devices = DM::DeviceMgr::Ins().GetData();
    const auto groups = persisted_devices.find("groups");
    if (groups != persisted_devices.end() && groups->is_array()) {
        for (const auto& group : *groups) {
            if (!group.is_object() || !group.contains("list") || !group["list"].is_array())
                continue;
            for (const auto& item : group["list"]) {
                if (!item.is_object())
                    continue;
                const std::string mac = item.value("mac", std::string());
                const std::string normalized_mac = normalize_device_mac(mac);
                if (normalized_mac.empty())
                    continue;

                auto existing = stored_devices.find(normalized_mac);
                if (existing != stored_devices.end()) {
                    ++existing->second.count;
                    continue;
                }

                StoredDeviceSnapshot snapshot;
                snapshot.mac = mac;
                snapshot.address = item.value("address", std::string());
                snapshot.count = 1;
                stored_devices.emplace(normalized_mac, std::move(snapshot));
            }
        }
    }

    post_device_webview_command(weak_browser, "device_address_correction_result", {
        {"accepted", true},
        {"completed", false},
        {"busy", false}
    });
    m_scanPoolThread = Slic3r::create_thread(
        [this, weak_browser, stored_devices = std::move(stored_devices)] {
            int correction_count = 0;
            // std::this_thread::sleep_for(std::chrono::milliseconds(1500));  不再需要，此函数改成由前端控制触发时机
            std::vector<std::string> prefix;
            prefix.push_back("CXSWBox");
            prefix.push_back("creality");
            prefix.push_back("Creality");
            std::vector<std::string> vtIp,vtBoxIp;
            if (m_scanExit.load(std::memory_order_acquire))
            {
                    return 0;
                }
            std::vector<cxnet::machine_info> vtDevice;
            try {
                vtDevice = cxnet::syncDiscoveryService(prefix);
            } catch (const std::exception& e) {
                post_device_webview_command(weak_browser, "device_address_correction_result", {
                    {"accepted", true},
                    {"completed", true},
                    {"busy", false},
                    {"corrections", correction_count},
                    {"error", e.what()}
                });
                return 0;
            } catch (...) {
                post_device_webview_command(weak_browser, "device_address_correction_result", {
                    {"accepted", true},
                    {"completed", true},
                    {"busy", false},
                    {"corrections", correction_count},
                    {"error", "device discovery failed"}
                });
                return 0;
            }
            //cxnet::machine_info info;
            //info.answer = "1";
            //info.machineIp = "172.23.215.56";
            //vtDevice.push_back(info);
            for (auto& item : vtDevice) {
                if (m_scanExit.load(std::memory_order_acquire))
                {
                    return 0;
                }
                std::string answer = item.answer;
                if (answer.substr(0, 8) == "_CXSWBox")
                {
                    continue; // Skip the box devices for correction
                }
                else
                {
                std::regex legacyAnswerRegex("_creality(\\d{2})(\\d{4}).+");
                std::smatch legacyMatches;
                if (!std::regex_match(answer, legacyMatches, legacyAnswerRegex)) {
                    RemotePrint::ProbeOptions probeOptions;
                    probeOptions.cancelled = [this] {
                        return m_scanExit.load(std::memory_order_acquire);
                    };
                    const auto probeResult = RemotePrint::LanDeviceProbe().probe(
                        item.machineIp, RemotePrint::ProbePolicy::PreferSecure, probeOptions);
                    if (probeResult.ok()) {
                        nlohmann::json profile = probeResult.profile->to_json();
                        const std::string normalized_mac =
                            normalize_device_mac(probeResult.profile->mac);
                        const auto stored = stored_devices.find(normalized_mac);
                        BOOST_LOG_TRIVIAL(debug)
                            << "[address-correction] probed: ip=" << item.machineIp
                            << ", mac=" << probeResult.profile->mac
                            << ", model=" << probeResult.profile->model
                            << ", name=" << answer
                            << ", stored=" << (stored != stored_devices.end())
                            << ", unique="
                            << (stored != stored_devices.end() && stored->second.count == 1);
                        if (stored == stored_devices.end() || stored->second.count != 1)
                            continue;


                        const std::string& address = item.machineIp;
                        if (stored->second.address != address) {
                            profile["mac"] = stored->second.mac;
                            profile["ip"] = address;
                            post_device_webview_command(weak_browser, "correct_device", std::move(profile));
                            ++correction_count;
                        }
                    }
                    continue;
                }

                std::string url = (boost::format("http://%1%/info") % item.machineIp).str();
                Slic3r::Http http_url = Slic3r::Http::get(url);    
                http_url.timeout_connect(1)
                .timeout_max(5)
                .on_complete(
                [item, weak_browser, &stored_devices, &correction_count](std::string body, unsigned status) {
                    try {
                        json j = json::parse(body);
                        std::string mac = j["mac"].get<std::string>();
                        std::string vtIp = item.machineIp;
                        const auto stored = stored_devices.find(normalize_device_mac(mac));
                        const std::string model = j.value("model", std::string());
                        const std::string name = j.value("name", model);
                        BOOST_LOG_TRIVIAL(debug)
                            << "[address-correction] probed legacy: ip=" << vtIp
                            << ", mac=" << mac
                            << ", model=" << model
                            << ", name=" << name
                            << ", stored=" << (stored != stored_devices.end())
                            << ", unique="
                            << (stored != stored_devices.end() && stored->second.count == 1);
                        if (stored == stored_devices.end() || stored->second.count != 1 ||
                            stored->second.address == vtIp)
                            return;

                        nlohmann::json dataJson;
                        dataJson["mac"] = stored->second.mac;
                        dataJson["ip"] = vtIp;
                        post_device_webview_command(weak_browser, "correct_device", std::move(dataJson));
                        ++correction_count;
                    }
                    catch (std::exception& e) {
                        BOOST_LOG_TRIVIAL(error) << "Error parsing JSON: " << e.what();
                        return;
                    }
                }).on_error(
                [this](std::string body, std::string error, unsigned int status) {
                    
                    }).perform_sync();
                }
            }

            post_device_webview_command(weak_browser, "device_address_correction_result", {
                {"accepted", true},
                {"completed", true},
                {"busy", false},
                {"corrections", correction_count}
            });
            return 0;
        });
}
void PrinterMgrView::scan_device(const std::string& request_id)
{
    const DeviceWebViewHandle weak_browser = make_device_webview_handle(m_browser);
    if (m_deviceScanThread.joinable() &&
        !m_deviceScanThread.try_join_for(boost::chrono::seconds(0))) {
        if (!request_id.empty()) {
            nlohmann::json busyResult = {
                {"requestId", request_id},
                {"status", 1},
                {"errorType", "scan_busy"},
                {"servers", nlohmann::json::array()},
                {"serverInfos", nlohmann::json::array()},
                {"boxs", nlohmann::json::array()},
                {"klipper", nlohmann::json::array()}
            };
            post_device_webview_command(weak_browser, "scan_device", std::move(busyResult));
        }
        return;
    }

    m_deviceScanThread = Slic3r::create_thread(
        [this, weak_browser, request_id] {
            if (m_scanExit.load(std::memory_order_acquire))
                return;

             
            std::vector<std::string> prefix;
            prefix.push_back("CXSWBox");
            prefix.push_back("creality");
            prefix.push_back("Creality");
            std::vector<std::string> vtIp,vtBoxIp,vtKlipperIp;
            // A printer reached through several network interfaces (e.g. Wi-Fi and a VPN) or
            // announcing several services is listed once per address.
            auto push_unique = [](std::vector<std::string>& list, const std::string& address) {
                if (std::find(list.begin(), list.end(), address) == list.end())
                    list.push_back(address);
            };
            auto vtDevice = cxnet::syncDiscoveryService(prefix);
            for (auto& item : vtDevice) {
                if (m_scanExit.load(std::memory_order_acquire))
                    return;

                std::string answer = item.answer;
                if (answer.substr(0, 8) == "_CXSWBox")
                {
                    push_unique(vtBoxIp, item.machineIp);
                }
                else
                {
                    std::regex answerRegex("_creality(\\d{2})(\\d{4}).+");
                    std::smatch matches;
                    if (std::regex_match(answer, matches, answerRegex))
                    {
                        string strMachineType = matches[1];
                        string strMoonrakerPort = matches[2];
                        if (strMachineType == "00")
                        {
                            //??????
                            push_unique(vtKlipperIp, item.machineIp + ":"+strMoonrakerPort);
                        }
                    }
                    else
                    {
                        //if (vtIp.size() < 50)
                            push_unique(vtIp, item.machineIp);
                    }
                }
            }

            {   //发送"校验中"状态
                nlohmann::json verifyingData = request_id.empty()
                    ? nlohmann::json("")
                    : nlohmann::json{{"requestId", request_id}};
                post_device_webview_command(weak_browser, "scan_device_verifying", std::move(verifyingData));
            }

            RemotePrint::ProbeOptions probe_options;
            probe_options.preflight_ports = true;
            probe_options.https_timeout_seconds = 2;
            probe_options.http_timeout_seconds = 3;
            probe_options.cancelled = [this] {
                return m_scanExit.load(std::memory_order_acquire);
            };
            const auto probe_results = RemotePrint::LanDeviceProbe().probe_many(
                vtIp, RemotePrint::ProbePolicy::PreferSecure, probe_options);

            nlohmann::json serverInfos = nlohmann::json::array();
            for (const auto& result : probe_results) {
                if (result.ok())
                    serverInfos.push_back(result.profile->to_json());
            }

            nlohmann::json dataJson;
            dataJson["status"] = 0;
            dataJson["servers"] = vtIp;  
            dataJson["serverInfos"] = serverInfos;  //返回打印机/info信息,前端不必再连接获取
            dataJson["boxs"] = vtBoxIp;   
            dataJson["klipper"] = vtKlipperIp;   
            if (!request_id.empty())
                dataJson["requestId"] = request_id;

            if (m_scanExit.load(std::memory_order_acquire))
                return;
            post_device_webview_command(weak_browser, "scan_device", std::move(dataJson));
        });
}

void     PrinterMgrView::RequestDeviceListFromDB() 
{
    // create command to send to the webview
    nlohmann::json commandJson;
    commandJson["command"] = "req_device_list";
    commandJson["data"]    = "";

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandJson.dump());

    run_script(strJS.ToStdString());
}

void PrinterMgrView::RegisterHandler(const std::string& command, std::function<void(const nlohmann::json&)> handler)
{
    m_commandHandlers[command] = handler;
}
void PrinterMgrView::UnregisterHandler(const std::string& command)
{
    m_commandHandlers.erase(command);
}

void PrinterMgrView::ExecuteScriptCommand(const std::string& commandInfo, bool async)
{
    if (commandInfo.empty())
        return;
    
    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandInfo);

    if (async)
    {
        wxGetApp().CallAfter([this, strJS] { run_script(strJS.ToStdString()); });
    }
    else 
    {
        run_script(strJS.ToStdString());
    }

}

void PrinterMgrView::request_refresh_all_device()
{
    // Create command JSON object
    nlohmann::json commandJson = {
        {"command", "refresh_all_device"},
        {"data", ""}
    };

    ExecuteScriptCommand(commandJson.dump());
}

void PrinterMgrView::update_current_cxy_device_filament(const std::string& mac)
{
    nlohmann::json commandJson = {
        {"command", "update_current_cxy_device_filament"}, {"data", mac}
    };

    ExecuteScriptCommand(commandJson.dump(), true);
}

std::string PrinterMgrView::get_user_operation_state_file_path() const
{
    return (fs::path(data_dir()) / "cache" / "send_to_printer" / "user_operation_state.json").make_preferred().string();
}

bool PrinterMgrView::save_user_operation_state(const nlohmann::json& state_data)
{
    try {
        const auto path = fs::path(get_user_operation_state_file_path());
        const auto dir  = path.parent_path();
        if (!dir.empty() && !fs::exists(dir))
            fs::create_directories(dir);

        boost::nowide::ofstream out(path.string(), std::ios::out | std::ios::trunc);
        if (!out.is_open()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to open file: " << path.string();
            return false;
        }

        out << state_data.dump(-1, ' ', true);
        return true;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to save state, error=" << e.what();
        return false;
    }
}

nlohmann::json PrinterMgrView::load_user_operation_state() const
{
    nlohmann::json default_state = {
        {"deviceLayout", "card"},
        {"customColorList", nlohmann::json::array()}
    };

    try {
        const auto path = fs::path(get_user_operation_state_file_path());
        if (!fs::exists(path))
            return default_state;

        boost::nowide::ifstream in(path.string());
        if (!in.is_open()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to open file: " << path.string();
            return default_state;
        }

        std::stringstream buffer;
        buffer << in.rdbuf();
        if (buffer.str().empty())
            return default_state;

        auto parsed = nlohmann::json::parse(buffer.str(), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": invalid state file: " << path.string();
            return default_state;
        }

        std::string device_layout = "card";
        try {
            if (parsed.contains("deviceLayout") && parsed["deviceLayout"].is_string())
                device_layout = parsed["deviceLayout"].get<std::string>();
        } catch (...) {
            device_layout = "card";
        }

        if (device_layout != "list")
            device_layout = "card";

        parsed["deviceLayout"] = device_layout;

        nlohmann::json custom_color_list = nlohmann::json::array();
        try {
            if (parsed.contains("customColorList") && parsed["customColorList"].is_array()) {
                const auto is_valid_slot_value = [](const nlohmann::json& value) {
                    return value.is_string() || value.is_number_integer() || value.is_number_unsigned();
                };
                for (const auto& item : parsed["customColorList"]) {
                    if (!item.is_object())
                        continue;
                    if (!item.contains("address") || !item["address"].is_string())
                        continue;
                    if (!item.contains("boxType") || !is_valid_slot_value(item["boxType"]))
                        continue;
                    if (!item.contains("boxId") || !is_valid_slot_value(item["boxId"]))
                        continue;
                    if (!item.contains("materialId") || !is_valid_slot_value(item["materialId"]))
                        continue;
                    if (!item.contains("customColor") || !item["customColor"].is_string())
                        continue;

                    const auto address      = item["address"].get<std::string>();
                    const auto custom_color = item["customColor"].get<std::string>();
                    if (address.empty() || custom_color.empty())
                        continue;

                    custom_color_list.push_back({
                        {"address", address},
                        {"boxType", item["boxType"]},
                        {"boxId", item["boxId"]},
                        {"materialId", item["materialId"]},
                        {"customColor", custom_color}
                    });
                }
            }
        } catch (...) {
            custom_color_list = nlohmann::json::array();
        }

        parsed["customColorList"] = custom_color_list;
        return parsed;
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": failed to load state, error=" << e.what();
        return default_state;
    }
}

void PrinterMgrView::handle_save_user_operation_state(const nlohmann::json& json_data)
{
    const nlohmann::json payload = json_data.contains("data") ? json_data["data"] : json_data;

    std::string device_layout = "card";
    try {
        if (payload.is_object() && payload.contains("deviceLayout") && payload["deviceLayout"].is_string())
            device_layout = payload["deviceLayout"].get<std::string>();
        else if (payload.is_string())
            device_layout = payload.get<std::string>();
    } catch (...) {
        device_layout = "card";
    }

    if (device_layout != "list")
        device_layout = "card";

    nlohmann::json state_data = load_user_operation_state();
    if (!state_data.is_object())
        state_data = nlohmann::json::object();
    state_data["deviceLayout"] = device_layout;

    const bool ok = save_user_operation_state(state_data);

    nlohmann::json commandJson;
    commandJson["command"] = "save_user_operation_state";
    commandJson["data"]    = {
        {"success", ok},
        {"deviceLayout", state_data["deviceLayout"]}
    };

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
    run_script(strJS.ToStdString());
}

void PrinterMgrView::handle_request_user_operation_state(const nlohmann::json& json_data)
{
    (void)json_data;

    nlohmann::json commandJson;
    commandJson["command"] = "request_user_operation_state";
    commandJson["data"]    = load_user_operation_state();

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
    run_script(strJS.ToStdString());
}


void PrinterMgrView::handle_get_user_custom_color_list(const nlohmann::json& json_data)
{
    (void)json_data;

    nlohmann::json state_data = load_user_operation_state();
    if (!state_data.is_object())
        state_data = nlohmann::json::object();

    nlohmann::json commandJson;
    commandJson["command"] = "get_user_custom_color_list";
    commandJson["data"]    = {
        {"customColorList", state_data.value("customColorList", nlohmann::json::array())}
    };

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
    run_script(strJS.ToStdString());
}

void PrinterMgrView::handle_set_user_custom_color_list(const nlohmann::json& json_data)
{
    const nlohmann::json payload = json_data.contains("data") ? json_data["data"] : json_data;

    const auto slot_value_to_string = [](const nlohmann::json& item, const char* key) -> std::string {
        if (!item.is_object() || !item.contains(key))
            return "";

        const auto& value = item[key];
        try {
            if (value.is_string())
                return value.get<std::string>();
            if (value.is_number_integer())
                return std::to_string(value.get<long long>());
            if (value.is_number_unsigned())
                return std::to_string(value.get<unsigned long long>());
        } catch (...) {
        }

        return "";
    };

    const auto normalize_custom_color_item = [&](const nlohmann::json& item) -> nlohmann::json {
        if (!item.is_object())
            return nlohmann::json();

        if (!item.contains("address") || !item["address"].is_string())
            return nlohmann::json();
        if (!item.contains("customColor") || !item["customColor"].is_string())
            return nlohmann::json();

        const auto address      = item["address"].get<std::string>();
        const auto box_type     = slot_value_to_string(item, "boxType");
        const auto box_id       = slot_value_to_string(item, "boxId");
        const auto material_id  = slot_value_to_string(item, "materialId");
        const auto custom_color = item["customColor"].get<std::string>();
        if (address.empty() || box_type.empty() || box_id.empty() || material_id.empty() || custom_color.empty())
            return nlohmann::json();

        return {
            {"address", address},
            {"boxType", item["boxType"]},
            {"boxId", item["boxId"]},
            {"materialId", item["materialId"]},
            {"customColor", custom_color}
        };
    };

    const auto is_same_slot = [&](const nlohmann::json& lhs, const nlohmann::json& rhs) {
        return lhs.is_object() && rhs.is_object() &&
               slot_value_to_string(lhs, "address") == slot_value_to_string(rhs, "address") &&
               slot_value_to_string(lhs, "boxType") == slot_value_to_string(rhs, "boxType") &&
               slot_value_to_string(lhs, "boxId") == slot_value_to_string(rhs, "boxId") &&
               slot_value_to_string(lhs, "materialId") == slot_value_to_string(rhs, "materialId");
    };

    nlohmann::json incoming_custom_color_list = nlohmann::json::array();
    try {
        const auto* custom_color_data = payload.is_array()
            ? &payload
            : ((payload.is_object() && payload.contains("customColorList") && payload["customColorList"].is_array())
                ? &payload["customColorList"]
                : nullptr);

        if (custom_color_data != nullptr) {
            for (const auto& item : *custom_color_data) {
                const auto normalized_item = normalize_custom_color_item(item);
                if (!normalized_item.is_null())
                    incoming_custom_color_list.push_back(normalized_item);
            }
        }
    } catch (...) {
        incoming_custom_color_list = nlohmann::json::array();
    }

    nlohmann::json state_data = load_user_operation_state();
    if (!state_data.is_object())
        state_data = nlohmann::json::object();

    nlohmann::json custom_color_list = nlohmann::json::array();
    if (state_data.contains("customColorList") && state_data["customColorList"].is_array())
        custom_color_list = state_data["customColorList"];

    bool ok = false;
    if (!incoming_custom_color_list.empty()) {
        for (const auto& incoming_item : incoming_custom_color_list) {
            bool updated = false;
            for (auto& item : custom_color_list) {
                if (!is_same_slot(item, incoming_item))
                    continue;

                item = incoming_item;
                updated = true;
                break;
            }

            if (!updated)
                custom_color_list.push_back(incoming_item);
        }

        state_data["customColorList"] = custom_color_list;
        ok = save_user_operation_state(state_data);
    } else {
        state_data["customColorList"] = custom_color_list;
    }

    nlohmann::json commandJson;
    commandJson["command"] = "set_user_custom_color_list";
    commandJson["data"]    = {
        {"success", ok},
        {"customColorList", state_data["customColorList"]}
    };

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
    run_script(strJS.ToStdString());
}

bool PrinterMgrView::LoadFile(std::string jPath, std::string &sContent)
{
    try {
        boost::nowide::ifstream t(jPath);
        std::stringstream buffer;
        buffer << t.rdbuf();
        sContent=buffer.str();
        BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << boost::format(", load %1% into buffer")% jPath;
    }
    catch (std::exception &e)
    {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",  got exception: "<<e.what();
        return false;
    }

    return true;
}

void PrinterMgrView::request_close_detail_page()
{
    nlohmann::json commandJson;
    commandJson["command"] = "req_close_detail_page_video";
    commandJson["data"] = "";

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandJson.dump());

    run_script(strJS.ToStdString());
}

void PrinterMgrView::request_reopen_detail_video()
{
    nlohmann::json commandJson;
    commandJson["command"] = "req_reopen_detail_page_video";
    commandJson["data"] = "";

    wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandJson.dump());

    run_script(strJS.ToStdString());
}

int PrinterMgrView::load_machine_preset_data()
{
    // Fork: the printer catalogue is read only from the bundled resources, never downloaded.
    const std::string vendor_preset_path = (boost::filesystem::path(Slic3r::resources_dir()) / PRESET_PROFILES_DIR /
                                            "Creality" / "machineList.json").make_preferred().string();
    if (!boost::filesystem::exists(vendor_preset_path)) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": missing " << vendor_preset_path;
        return -1;
    }
    try {
        std::string contents;
        LoadFile(vendor_preset_path, contents);
        json jLocal = json::parse(contents);
        localize_printer_list_images(jLocal["printerList"],
                                     wxGetApp().get_server()->is_started() ? wxGetApp().get_server_port() : 0);
        nlohmann::json commandJson;
        commandJson["command"] = "get_machine_list";
        commandJson["data"]    = jLocal["printerList"];
        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
        run_script(strJS.ToStdString());
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << vendor_preset_path << " failed: " << e.what();
        return -1;
    }
    return 0;
}

int PrinterMgrView::getFileListFromLanDevice(const std::string strIp)
{
    if (strIp.empty())
    {
        return -1;
    }
    const DeviceWebViewHandle weak_browser = make_device_webview_handle(m_browser);
    Slic3r::create_thread([weak_browser, strIp] {
        CURL* curl = curl_easy_init();
        if (!curl)
        {
            return -1;
        }

        // RAII helper for CURL handle
        struct CurlGuard
        {
            CURL* handle;
            ~CurlGuard()
            {
                if (handle) curl_easy_cleanup(handle);
            }
        } guard{ curl };

        std::string ftpUrl = "ftp://" + strIp + "/mmcblk0p1/creality/gztemp/";
        curl_easy_setopt(curl, CURLOPT_URL, ftpUrl.c_str());
        curl_easy_setopt(curl, CURLOPT_USERPWD, "anonymous:");
        curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_NONE);

        // Store FTP response in a temporary file
        std::string tempFilePath = "/tmp/ftp_listing_temp"; // Temp path on Linux/Mac
    #if defined(_WIN32)
        tempFilePath = std::tmpnam(nullptr); // Temp file path on Windows
    #endif

        FILE* fd = nullptr;
    #if defined(_MSC_VER) || defined(__MINGW64__)
        fd = boost::nowide::fopen(tempFilePath.c_str(), "wb+");
    #elif defined(__GNUC__) && defined(_LARGEFILE64_SOURCE)
        fd = fopen64(tempFilePath.c_str(), "wb+");
    #else
        fd = fopen(tempFilePath.c_str(), "wb+");
    #endif

        if (fd == nullptr)
        {
            return -1;
        }

        // RAII helper for file handle
        struct FileGuard
        {
            FILE* fd;
            std::string filePath;
            ~FileGuard()
            {
                if (fd) fclose(fd);
                // Delete temporary file
                remove(filePath.c_str());
            }
        } fileGuard{ fd, tempFilePath };

        // Write FTP response into file
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, fd);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // ??????5??
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);       // ????????15??

        // Execute request synchronously
        CURLcode res = curl_easy_perform(curl);

        std::vector<std::string> FileInfoList;

        if (res == CURLE_OK)
        {
            // Rewind file pointer to beginning
            rewind(fd);

            // Read file line by line and parse
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), fd))
            {
                std::string line(buffer);

                // Remove newline and carriage return characters
                line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
                line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

                FileInfoList.push_back(line);
            }

            nlohmann::json commandJson;
            nlohmann::json dataJson;

            dataJson["address"] = strIp;
            if(FileInfoList.empty())
                dataJson["fileList"] = nlohmann::json::array();
            else
                dataJson["fileList"] = FileInfoList;
            commandJson["command"] = "get_file_List_from_lan_device";
            commandJson["data"] = dataJson;

            wxString strJS = wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)));
            wxGetApp().CallAfter([weak_browser, strJS] {
                wxWebView* browser = resolve_device_webview(weak_browser);
                if (browser == nullptr)
                    return;
                WebView::RunScript(browser, strJS.ToStdString());
            });
            //run_script(strJS.ToStdString());
        }
        return 0;
    });
    return 0;
}

int PrinterMgrView::deleteFileListFromLanDevice(const std::string strIp, const std::string strName)
{
    if (strIp.empty() || strName.empty())
    {
        return -1;
    }

    CURL* curl = curl_easy_init();
    if (!curl)
    {
        return -1;
    }

    // RAII helper for CURL handle
    struct CurlGuard
    {
        CURL* handle;
        ~CurlGuard()
        {
            if (handle) curl_easy_cleanup(handle);
        }
    } guard{ curl };

    std::string fullPath = "ftp://" + strIp + "/mmcblk0p1/creality/gztemp/";
    curl_easy_setopt(curl, CURLOPT_URL, fullPath.c_str());

    std::string deleteCmd = "DELE /mmcblk0p1/creality/gztemp/" + strName;
    struct curl_slist *CMDlist = nullptr;
    CMDlist = curl_slist_append(CMDlist, deleteCmd.c_str()); 
    curl_easy_setopt(curl, CURLOPT_POSTQUOTE, CMDlist);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);  // ??????5??
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);       // ????????15??

    // Execute FTP delete command synchronously
    CURLcode res = curl_easy_perform(curl);

    curl_slist_free_all(CMDlist);

    if (res != CURLE_OK)
    {
       
        return -1;
    }


    return 0;
}

int PrinterMgrView::uploadeFileLanDevice(const std::string strIp)
{
    wxString input_file;

    input_file.Clear();
    wxFileDialog dialog(this,
        _L("Choose files"),
        "", "",
        file_wildcards(FT_ONLY_GCODE), wxFD_OPEN);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
    else
        return 0;

    // Check whether it is a gcode file
    if (!is_gcode_file(into_u8(input_file)))
    {
        return -1;
    }

    {
        // Notify front-end about upload progress
        nlohmann::json commandJson;
        commandJson["data"] = 30;
        commandJson["command"] = "uploade_file_oldPrinter_progress";
        wxString strJS = wxString::Format("window.handleStudioCmd('%s');", commandJson.dump());
        run_script(strJS.ToStdString());
    }

    fs::path path = into_u8(input_file);
    fs::path name = path.filename();

   float outProgress = 0;
    RemotePrint::RemotePrinterManager::getInstance().uploadFileByLan(strIp,name.string(),path.string(),
        [&outProgress](float progress,double speed) {
                outProgress  = progress;
            });

    if ((outProgress<101) && (outProgress>99))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

void PrinterMgrView::uploadDeviceFile(const std::string& address, const std::string& requestId)
{
    const DeviceWebViewHandle weak_browser = make_device_webview_handle(m_browser);
    auto send_failure = [weak_browser, address, requestId](const std::string& error_code) {
        nlohmann::json data;
        data["requestId"] = requestId;
        data["address"] = address;
        data["status"] = "failed";
        data["errorCode"] = error_code;
        data["statusCode"] = 0;
        post_device_webview_command(weak_browser, "upload_device_file_result", std::move(data));
    };

    if (address.empty() || requestId.empty()) {
        send_failure("invalid_request");
        return;
    }

    const auto device = DM::DeviceMgr::Ins().FindByAddress(address);
    if (!device) {
        send_failure("device_not_found");
        return;
    }
    const bool is_local_creality_device = device->connectType == 3;
    if (!is_local_creality_device || device->oldPrinter || device->moonrakerPort > 0) {
        send_failure("unsupported_device");
        return;
    }

    startDeviceFileUpload(address, device->secureConnection, requestId);
}

void PrinterMgrView::startDeviceFileUpload(const std::string& address,
                                           bool secureConnection,
                                           const std::string& requestId)
{
    const DeviceWebViewHandle weak_browser = make_device_webview_handle(m_browser);
    const auto terminal_sent = std::make_shared<std::atomic<bool>>(false);
    auto send_progress = [weak_browser, address, requestId, terminal_sent](int progress) {
        if (terminal_sent->load(std::memory_order_acquire))
            return;

        nlohmann::json data;
        data["requestId"] = requestId;
        data["address"] = address;
        data["progress"] = progress;
        post_device_webview_command(weak_browser, "upload_device_file_progress", std::move(data));
    };
    auto send_result = [weak_browser, address, requestId, terminal_sent](
                           const std::string& status, const std::string& error_code, int status_code) {
        if (terminal_sent->exchange(true, std::memory_order_acq_rel))
            return;

        nlohmann::json data;
        data["requestId"] = requestId;
        data["address"] = address;
        data["status"] = status;
        data["errorCode"] = error_code;
        data["statusCode"] = status_code;
        post_device_webview_command(weak_browser, "upload_device_file_result", std::move(data));
    };

    wxString input_file;
    input_file.Clear();
    wxFileDialog dialog(this,
        _L("Choose files"),
        "", "",
        _L("Printable files (*.gcode;*.3mf)|*.gcode;*.3mf|All files|*.*"),
        wxFD_OPEN | wxFD_FILE_MUST_EXIST);

    if (dialog.ShowModal() == wxID_OK)
        input_file = dialog.GetPath();
    else
    {
        send_result("cancelled", std::string(), 0);
        return;
    }

    const std::string input_file_path = into_u8(input_file);
    fs::path path = into_path(input_file);
    wxString lower_file = input_file.Lower();
    const bool is_gcode = is_gcode_file(input_file_path);
    const bool is_3mf = lower_file.EndsWith(".3mf");
    if (!is_gcode && !is_3mf) {
        send_result("failed", "invalid_file", 0);
        return;
    }
    if (is_3mf && !has_printable_gcode_in_3mf(input_file_path)) {
        send_result("failed", "invalid_3mf", 0);
        return;
    }

    send_progress(1);

    fs::path name = path.filename();

    auto& printer_mgr = RemotePrint::RemotePrinterManager::getInstance();
    printer_mgr.pushUploadTasksWithProfile(
        address,
        name.string(),
        path.string(),
        secureConnection,
        [send_progress](const std::string& ipAddress, float progress, double speed) {
            (void)ipAddress;
            (void)speed;
            send_progress(static_cast<int>(progress));
        },
        [send_result](const std::string& ipAddress, int statusCode) {
            (void)ipAddress;
            if (statusCode == CURLE_OK)
                return;
            if (statusCode == 601)
                send_result("cancelled", std::string(), statusCode);
            else
                send_result("failed", "upload_failed", statusCode);
        },
        [send_result](const std::string& ipAddress, std::string body) {
            (void)ipAddress;
            wxString lower_body = wxString::FromUTF8(body.c_str()).Lower();
            if (lower_body.Find("ok") != wxNOT_FOUND)
                send_result("success", std::string(), CURLE_OK);
            else
                send_result("failed", "invalid_response", CURLE_HTTP_RETURNED_ERROR);
        });
}
wxString PrinterMgrView::openCAFile()
{
    static const auto filemasks = _L("Certificate files (*.crt, *.pem)|*.crt;*.pem|All files|*.*");
    wxFileDialog      openFileDialog(this, _L("Open CA certificate file"), "", "", filemasks, wxFD_OPEN | wxFD_FILE_MUST_EXIST);
    if (openFileDialog.ShowModal() != wxID_CANCEL) {
        wxString path = openFileDialog.GetPath();
        return path;
        //m_optgroup->set_value("printhost_cafile", openFileDialog.GetPath(), true);
        //m_optgroup->get_field("printhost_cafile")->field_changed();
    }
    return "";
}

bool PrinterMgrView::request_print_control(const nlohmann::json& payload)
{
    try {
        nlohmann::json command_json = payload;
        command_json["command"] = "print_control";
        auto json_str = RemotePrint::Utils::url_encode(command_json.dump(-1, ' ', true));
        ExecuteScriptCommand(json_str, true);
        return true;
    }
    catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "PrinterMgrView::request_print_control failed: " << e.what();
        return false;
    }
}
} // GUI
} // Slic3r
