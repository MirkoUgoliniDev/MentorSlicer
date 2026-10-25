#include "WebViewDialog.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/print_manage/Utils.hpp"
#include "libslic3r_version.h"
#include "../Utils/Http.hpp"

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include <wx/url.h>

#include <slic3r/GUI/Widgets/WebView.hpp>
#include "print_manage/AppMgr.hpp"
#include "libslic3r/common_header/common_header.h"
namespace pt = boost::property_tree;

namespace Slic3r { namespace GUI {
wxDECLARE_EVENT(EVT_RESPONSE_MESSAGE, wxCommandEvent);

wxDEFINE_EVENT(EVT_RESPONSE_MESSAGE, wxCommandEvent);

WebViewPanel::WebViewPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Address: " << (void*) this;

#if CUSTOM_COMMUNITY_ENABLE
    wxString url = GetURL();
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    // Create the info panel
    m_info = new wxInfoBar(this);
    topsizer->Add(m_info, wxSizerFlags().Expand());
    // Create the webview
    m_browser = WebView::CreateWebView(this, url);
    m_browser->EnableAccessToDevTools(); // for debug:
    if (m_browser == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " m_browser is null!!! ";
        wxLogError("Could not init m_browser");
        return;
    }
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Address: " << (void*) m_browser;
    // "Connect via IP" reads the old WiFi printers through the local /proxy, which needs the session token.
    m_browser->AddUserScript(wxString::FromUTF8(wxGetApp().get_server()->proxy_token_user_script()));

    // m_browser->Hide();
    SetSizer(topsizer);

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Log backend information
    /* m_browser->GetUserAgent() may lead crash
    if (wxGetApp().get_mode() == comDevelop) {
        wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("Backend: %s Version: %s", m_browser->GetClassInfo()->GetClassName(),
            wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("User Agent: %s", m_browser->GetUserAgent());
    }
    */

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &WebViewPanel::OnNavigationRequest, this);
    Bind(wxEVT_WEBVIEW_NAVIGATED, &WebViewPanel::OnNavigationComplete, this);
    Bind(wxEVT_WEBVIEW_LOADED, &WebViewPanel::OnDocumentLoaded, this);
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &WebViewPanel::OnTitleChanged, this);
    Bind(wxEVT_WEBVIEW_ERROR, &WebViewPanel::OnError, this);
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &WebViewPanel::OnNewWindow, this);
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewPanel::OnScriptMessage, this);
    Bind(EVT_RESPONSE_MESSAGE, &WebViewPanel::OnScriptResponseMessage, this);

    Bind(wxEVT_CLOSE_WINDOW, &WebViewPanel::OnClose, this);

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
    DM::AppMgr::Ins().Register(m_browser);
#endif
}

wxString WebViewPanel::GetURL()
{
    // alpha/beta/dev 对社区的影响：社区使用不同的创想云服务环境
    // alpha：预发布环境（pre）+ cpp 启动的本地 http 服务
    // beta：生产环境 + c++ 启动的本地 http 服务
    // dev：根据开发需求决定用哪个环境 + 前端开发启动的局域网 http 服务

    // alpha & beta: c++ 启动了一个 http server
    // 原因：file 协议加载 html 跨域问题难以处理，尤其是大文件（font 文件，10M+）
    // http server 信息：
    // 1. 运行在 http://127.0.0.1:%d（端口动态获取）
    // 2. 将 resources_dir 的 web/homepage 作为静态目录，社区的 html 等打包放在这里
    // 3. c++ webview 加载 http://127.0.0.1:%d/#/；添加 # 是因为使用了 hash 路由
    // wxString url     = wxString::Format("http://127.0.0.1:%d/#/",wxGetApp().get_server_port()); // 127.0.0.1 访问创想云 cdn 图片会
    // 403，因此使用 localhost
    wxString lang = wxGetApp().current_frontend_language_code();

    std::string type = std::string(PROJECT_VERSION_EXTRA);
    type.erase(std::remove(type.begin(), type.end(), ' '), type.end());

    std::string version = std::string(CREALITYPRINT_VERSION);
    version.erase(std::remove(version.begin(), version.end(), ' '), version.end());

    std::string region = wxGetApp().app_config->get("region");
    if(region== "")
    {
         wxString strlang = wxGetApp().current_language_code_safe();
        if (strlang == "zh_CN") {
                region = "China";
        } else {
                region = "North America";
            }
    }
    //替换region中的空格为下划线
    std::replace(region.begin(), region.end(), ' ', '_');
    std::string use_inches = wxGetApp().app_config->get("use_inches");

    int port = wxGetApp().get_server_port();

    std::string os_description = Slic3r::Http::url_encode(wxGetOsDescription().ToStdString());

    wxString url = wxString::Format(
        "http://localhost:%d/homepage/index.html?lang=%s&version=%s&type=%s&region=%s&use_inches=%s&debug=false&ai=true&time=%d&privacy=%d&os=%s#/Community/Home Page",
        port,
        lang, version, type, region, use_inches, std::time(0), 0, os_description);
    return url;
}
WebViewPanel::~WebViewPanel()
{
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Address: " << (void*) this;

    Unbind(wxEVT_WEBVIEW_NAVIGATED, &WebViewPanel::OnNavigationComplete, this);
    Unbind(wxEVT_WEBVIEW_NAVIGATING, &WebViewPanel::OnNavigationRequest, this);
    Unbind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebViewPanel::OnScriptMessage, this);


#ifdef __WXGTK__
    m_freshTimer->Stop();
    m_browser->Stop();
    m_browser->RemoveScriptMessageHandler("wx");
#endif
#if CUSTOM_COMMUNITY_ENABLE
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    DM::AppMgr::Ins().UnRegister(m_browser);
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << " End";
#endif
}
void WebViewPanel::Reload()
{
#if CUSTOM_COMMUNITY_ENABLE
    void* backend_before = m_browser->GetNativeBackend();
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << "[LOAD_URL_ACTION] START. webView=" << (void*) m_browser
                               << ", Backend Ptr BEFORE: " << backend_before;
    m_browser->LoadURL(GetURL());
#endif
    //m_browser->SetFocus();
    void* backend_after = m_browser->GetNativeBackend();
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << "[LOAD_URL_ACTION] END (call returned). webView=" << (void*) m_browser
                               << ", Backend Ptr AFTER: " << backend_after;
}
void WebViewPanel::OnClose(wxCloseEvent& evt) { this->Hide(); }

void WebViewPanel::SendRecentList(int images)
{
    boost::property_tree::wptree req;
    boost::property_tree::wptree data;
    wxGetApp().mainframe->get_recent_projects(data, images);
    req.put(L"sequence_id", "");
    req.put(L"command", L"get_recent_projects");
    req.put_child(L"response", data);
    std::wostringstream oss;
    pt::write_json(oss, req, false);

    // 通过WebSocket发送消息


    // 保留原有的RunScript调用作为备用
    RunScript(wxString::Format("window.handleStudioCmd(%s)", oss.str()));
}

void WebViewPanel::sync_preferences(const std::vector<wxString>& prefs)
{
    if (prefs.size() < 4)
        return;

    boost::property_tree::wptree req;
    req.put(L"command", L"sync_preference");

    boost::property_tree::wptree preferences;
    preferences.put(L"lang", prefs[0].ToStdWstring());
    preferences.put(L"region", prefs[1].ToStdWstring());
    preferences.put(L"use_inches", prefs[2].ToStdWstring());
    preferences.put(L"syncPresetEnabled", prefs[3].ToStdWstring());
    req.add_child(L"preference", preferences);

    std::wostringstream oss;
    pt::write_json(oss, req, false);

    RunScript(wxString::Format("window.handleStudioCmd(%s)", oss.str()));
}

void WebViewPanel::update_mode()
{
#if CUSTOM_COMMUNITY_ENABLE
    GetSizer()->Show(size_t(0), wxGetApp().app_config->get("internal_developer_mode") == "true");
    GetSizer()->Layout();
#endif
}

/**
 * Callback invoked when there is a request to load a new page (for instance
 * when the user clicks a link)
 */
void WebViewPanel::OnNavigationRequest(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    const wxString& url = evt.GetURL();
    if (url.StartsWith("File://") || url.StartsWith("file://")) {
        if (!url.Contains("/web/homepage/index.html")) {
            auto file = wxURL::Unescape(wxURL(url).GetPath());
#ifdef _WIN32
            if (file.StartsWith('/'))
                file = file.Mid(1);
#endif
            if (wxGetApp().app_config->get("is_first_install") == "1" ||
                (wxGetApp().app_config->get("is_first_install") != "1" && !wxGetApp().preset_bundle->printers.only_default_printers())) {
                wxGetApp().plater()->load_files(wxArrayString{1, &file});
            }
            evt.Veto();
            return;
        }
    }

    if (m_info->IsShown()) {
        m_info->Dismiss();
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Navigation request to '" + evt.GetURL() + "' (target='" + evt.GetTarget() + "')");

}

/**
 * Callback invoked when a navigation request was accepted
 */
void WebViewPanel::OnNavigationComplete(wxWebViewEvent& evt)
{
    m_browser->Show();
    Layout();
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");
}

/**
 * Callback invoked when a page is finished loading
 */
void WebViewPanel::OnDocumentLoaded(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    // Only notify if the document is the main frame, not a subframe
    if (evt.GetURL() == m_browser->GetCurrentURL()) {
        if (wxGetApp().get_mode() == comDevelop)
            wxLogMessage("%s", "Document loaded; url='" + evt.GetURL() + "'");
    }
}

void WebViewPanel::OnTitleChanged(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetString().ToUTF8().data();
    // wxGetApp().CallAfter([this] { SendRecentList(); });
}

/**
 * On new window: pages of the local server (and local files) open in this single-window
 * browser, web links (target=_blank) in the default browser.
 */
void WebViewPanel::OnNewWindow(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetURL().ToUTF8().data();
    wxString flag = " (other)";

    if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER) {
        flag = " (user)";
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "New window; url='" + evt.GetURL() + "'" + flag);

    const wxString url   = evt.GetURL();
    const wxString lower = url.Lower();
    const wxString port  = wxString::Format(":%d", wxGetApp().get_server_port());
    const auto local_server = [&lower, &port](const wxString& host) {
        const wxString origin = "http://" + host + port;
        return lower == origin || lower.StartsWith(origin + "/") || lower.StartsWith(origin + "?") || lower.StartsWith(origin + "#");
    };
    if (lower.StartsWith("file://") || local_server("localhost") || local_server("127.0.0.1"))
        m_browser->LoadURL(url);
    else if (lower.StartsWith("http://") || lower.StartsWith("https://"))
        wxLaunchDefaultBrowser(url);
    else
        BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ": new window ignored";
}

void WebViewPanel::OnScriptMessage(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << evt.GetString().ToUTF8().data();
    // Send the preferences once, on the first script message of this panel.
    if (!m_preferences_sent) {
        if (Slic3r::GUI::wxGetApp().mainframe->m_webview) {
            std::vector<wxString> prefs;

            wxString    strlang           = wxGetApp().current_frontend_language_code();
            std::string country_code      = wxGetApp().app_config->get("region");
            std::string use_inches        = wxGetApp().app_config->get("use_inches");
            std::string syncPresetEnabled = wxGetApp().app_config->get("sync_user_preset") == "true" ? "1" : "0";

            prefs.push_back(strlang);
            prefs.push_back(wxString(country_code));
            prefs.push_back(wxString(use_inches));
            prefs.push_back(wxString(syncPresetEnabled));
            Slic3r::GUI::wxGetApp().mainframe->m_webview->sync_preferences(prefs);
        }
        m_preferences_sent = true;
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("Script message received; value = %s, handler = %s", evt.GetString(), evt.GetMessageHandler());

    if (DM::AppMgr::Ins().Invoke(m_browser, evt.GetString().ToUTF8().data())) {
        return;
    }

    std::string response = wxGetApp().handle_web_request(evt.GetString().ToUTF8().data());
    if (response.empty())
        return;

    /* remove \n in response string */
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    //boost::replace_all(response, "\\n", "");
    if (!response.empty()) {
        // m_response_js = wxString::Format("window.handleStudioCmd('%s')", response);
        wxString        wxResponse = from_u8(response.c_str());
        wxCommandEvent* event      = new wxCommandEvent(EVT_RESPONSE_MESSAGE, this->GetId());
        event->SetClientData(new wxString(
            wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(std::string(wxResponse.utf8_str())))));
        wxQueueEvent(this, event);
    } else {
        m_response_js.clear();
    }
}

void WebViewPanel::OnScriptResponseMessage(wxCommandEvent& evt)
{
    wxString* wxResponse = static_cast<wxString*>(evt.GetClientData());
    if (wxResponse) {
        if (!wxResponse->empty()) {
            RunScript(*wxResponse);
        }
        delete wxResponse;
    }
}

void WebViewPanel::RunScript(const wxString& javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;

    if (!m_browser)
        return;
    void* backend_after = m_browser->GetNativeBackend();

    WebView::RunScript(m_browser, javascript);
}

/**
 * Callback invoked when a loading error occurs
 */
void WebViewPanel::OnError(wxWebViewEvent& evt)
{
#define WX_ERROR_CASE(type) \
    case type: category = #type; break;

    wxString category;
    switch (evt.GetInt()) {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": [" << category << "] " << evt.GetString().ToUTF8().data();
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": [" << category << "] " << evt.GetString().ToUTF8().data();

    if (wxGetApp().get_mode() == comDevelop) {
        wxLogMessage("%s", "Error; url='" + evt.GetURL() + "', error='" + category + " (" + evt.GetString() + ")'");

        // Show the info bar with an error
        m_info->ShowMessage(_L("An error occurred loading ") + evt.GetURL() + "\n" + "'" + category + "'", wxICON_ERROR);
    }
}

}} // namespace Slic3r::GUI
