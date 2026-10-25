#include "WebView.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/MacDarkMode.hpp"

#include <boost/log/trivial.hpp>

#include <wx/webviewarchivehandler.h>
#include <wx/webviewfshandler.h>
#include <wx/textfile.h>
#include <wx/utils.h>
#include <wx/dir.h>
#include <wx/file.h>
#if wxUSE_WEBVIEW_EDGE
#include <wx/msw/webview_edge.h>
#elif defined(__WXMAC__)
#include <wx/osx/webview_webkit.h>
#endif
#include <wx/uri.h>
#if defined(__WIN32__) || defined(__WXMAC__)
#include "wx/private/jsscriptwrapper.h"
#endif

#ifdef __WIN32__
#include <WebView2.h>
#include <Shellapi.h>
#include <slic3r/Utils/Http.hpp>
#elif defined __linux__
#include <gtk/gtk.h>
#define WEBKIT_API
struct WebKitWebView;
struct WebKitJavascriptResult;
struct WebKitSettings;
typedef enum {
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ON_DEMAND,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS,
    WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER
} WebKitHardwareAccelerationPolicy;
extern "C" {
WEBKIT_API WebKitSettings *webkit_web_view_get_settings(WebKitWebView *web_view);
WEBKIT_API void webkit_settings_set_hardware_acceleration_policy(WebKitSettings *settings, WebKitHardwareAccelerationPolicy policy);
WEBKIT_API guint webkit_get_major_version(void);
WEBKIT_API guint webkit_get_minor_version(void);
WEBKIT_API guint webkit_get_micro_version(void);
WEBKIT_API void
webkit_web_view_run_javascript                       (WebKitWebView             *web_view,
                                                      const gchar               *script,
                                                      GCancellable              *cancellable,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data);
WEBKIT_API WebKitJavascriptResult *
webkit_web_view_run_javascript_finish                (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
						      GError                    **error);
WEBKIT_API void
webkit_javascript_result_unref              (WebKitJavascriptResult *js_result);
}
#endif

#include <cstdlib>
#include <set>
#ifdef __WIN32__
class WebViewEdge : public wxWebViewEdge
{
public:
    WebViewEdge(const wxWebViewConfiguration& config, bool defer_navigation)
        : wxWebViewEdge(config)
        , m_defer_navigation(defer_navigation)
    {
        Bind(wxEVT_WEBVIEW_CREATED, &WebViewEdge::OnWebViewCreated, this);
    }

    ~WebViewEdge() override
    {
        pendingUrl.clear();
        Unbind(wxEVT_WEBVIEW_CREATED, &WebViewEdge::OnWebViewCreated, this);
    }

    void LoadURL(const wxString& url) override
    {
        if (WebView::IsShuttingDown()) {
            pendingUrl.clear();
            return;
        }

        if (m_defer_navigation && GetNativeBackend() == nullptr) {
            // WebView2 initialization is asynchronous. During GUI recreation,
            // the old controls may still be shutting down while new controls
            // are created. Navigating before wxEVT_WEBVIEW_CREATED can fail
            // with ERROR_INVALID_STATE.
            pendingUrl = url;
            BOOST_LOG_TRIVIAL(warning)
                << __FUNCTION__ << ": WebView2 backend is not ready; deferring URL: "
                << url.ToStdString();
            return;
        }

        pendingUrl.clear();
        wxWebViewEdge::LoadURL(url);
    }

    bool SetUserAgent(const wxString &userAgent)
    {
        bool dark = userAgent.Contains("dark");
        SetColorScheme(dark ? COREWEBVIEW2_PREFERRED_COLOR_SCHEME_DARK : COREWEBVIEW2_PREFERRED_COLOR_SCHEME_LIGHT);

        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2) {
            ICoreWebView2Settings *settings;
            HRESULT                hr = webView2->get_Settings(&settings);
            if (hr == S_OK) {
                ICoreWebView2Settings2 *settings2;
                hr = settings->QueryInterface(&settings2);
                if (hr == S_OK) {
                    settings2->put_UserAgent(userAgent.wc_str());
                    settings2->Release();
                    return true;
                }
            }
            settings->Release();
            return false;
        }
        pendingUserAgent = userAgent;
        return true;
    }

    bool SetColorScheme(COREWEBVIEW2_PREFERRED_COLOR_SCHEME colorScheme)
    {
        ICoreWebView2 *webView2 = (ICoreWebView2 *) GetNativeBackend();
        if (webView2) {
            ICoreWebView2_13 * webView2_13;
            HRESULT           hr = webView2->QueryInterface(&webView2_13);
            if (hr == S_OK) {
                ICoreWebView2Profile *profile;
                hr = webView2_13->get_Profile(&profile);
                if (hr == S_OK) {
                    profile->put_PreferredColorScheme(colorScheme);
                    profile->Release();
                    return true;
                }
                webView2_13->Release();
            }
            return false;
        }
        pendingColorScheme = colorScheme;
        return true;
    }

    void DoGetClientSize(int *x, int *y) const override
    {
        if (!pendingUserAgent.empty()) {
            auto thiz = const_cast<WebViewEdge *>(this);
            auto userAgent = std::move(thiz->pendingUserAgent);
            thiz->pendingUserAgent.clear();
            thiz->SetUserAgent(userAgent);
        }
        if (pendingColorScheme) {
            auto thiz      = const_cast<WebViewEdge *>(this);
            auto colorScheme = pendingColorScheme;
            thiz->pendingColorScheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
            thiz->SetColorScheme(colorScheme);
        }
        wxWebViewEdge::DoGetClientSize(x, y);
    };
private:
    void OnWebViewCreated(wxWebViewEvent& event)
    {
        if (WebView::IsShuttingDown()) {
            // The backend may have become available after the first shutdown
            // scan. Capture its browser PID before this control is destroyed.
            WebView::BeginShutdown();
            pendingUrl.clear();
            event.Skip();
            return;
        }

        void* backend = GetNativeBackend();
        if (backend == nullptr) {
            BOOST_LOG_TRIVIAL(warning)
                << __FUNCTION__ << ": WebView2 created event received without a backend. webView="
                << (void*) this;
            event.Skip();
            return;
        }

        BOOST_LOG_TRIVIAL(warning)
            << __FUNCTION__ << ": WebView2 backend ready. webView=" << (void*) this
            << ", backend=" << backend;

        if (!pendingUrl.empty()) {
            const wxString url = std::move(pendingUrl);
            pendingUrl.clear();
            BOOST_LOG_TRIVIAL(warning)
                << __FUNCTION__ << ": loading deferred URL: " << url.ToStdString();
            wxWebViewEdge::LoadURL(url);
        }

        event.Skip();
    }

    const bool m_defer_navigation;
    wxString pendingUrl;
    wxString pendingUserAgent;
    COREWEBVIEW2_PREFERRED_COLOR_SCHEME pendingColorScheme = COREWEBVIEW2_PREFERRED_COLOR_SCHEME_AUTO;
};

#elif defined __WXOSX__

class WebViewWebKit : public wxWebViewWebKit
{
public:
#if wxCHECK_VERSION(3, 3, 0)
    WebViewWebKit() : wxWebViewWebKit(wxWebView::NewConfiguration(wxWebViewBackendWebKit)) {}
#endif

    void LoadURL(const wxString& url) override
    {
        if (!m_wx_bridge_ready) {
            // Registration runs via CallAfter. Keep only the latest navigation
            // until the document-start bridge script has been installed.
            m_pending_url = url;
            m_has_pending_url = true;
            return;
        }
        wxWebViewWebKit::LoadURL(url);
    }

    bool AddScriptMessageHandler(const wxString& name) override
    {
        const bool added = wxWebViewWebKit::AddScriptMessageHandler(name);
        if (added && name == "wx") {
            m_wx_bridge_ready = true;
            if (m_has_pending_url) {
                const wxString url = m_pending_url;
                m_pending_url.clear();
                m_has_pending_url = false;
                wxWebViewWebKit::LoadURL(url);
            }
        }
        return added;
    }

    ~WebViewWebKit() override
    {
        RemoveScriptMessageHandler("wx");
    }

private:
    bool m_wx_bridge_ready{false};
    bool m_has_pending_url{false};
    wxString m_pending_url;
};

#elif defined __linux__

    // TODO: release webview script message handle in Destructor
    // class WebViewWebGTK : public wxWebview
    // {
    //     ~WebViewWebGTK() override
    //     {
    //         RemoveScriptMessageHandler("wx");
    //     }
    // };

#endif

class FakeWebView : public wxWebView
{
public:
    bool Create(wxWindow* parent, wxWindowID id, const wxString& url, const wxPoint& pos,
                const wxSize& size, long style, const wxString& name) override
    {
        return wxControl::Create(parent, id, pos, size, style, wxDefaultValidator, name);
    }
    virtual wxString GetCurrentTitle() const override { return wxString(); }
    virtual wxString GetCurrentURL() const override { return wxString(); }
    virtual bool IsBusy() const override { return false; }
    virtual bool IsEditable() const override { return false; }
    virtual void LoadURL(const wxString& url) override { }
    virtual void Print() override { }
    virtual void RegisterHandler(wxSharedPtr<wxWebViewHandler> handler) override { }
    virtual void Reload(wxWebViewReloadFlags flags = wxWEBVIEW_RELOAD_DEFAULT) override { }
    virtual bool RunScript(const wxString& javascript, wxString* output = NULL) const override { return false; }
    virtual void SetEditable(bool enable = true) override { }
    virtual void Stop() override { }
    virtual bool CanGoBack() const override { return false; }
    virtual bool CanGoForward() const override { return false; }
    virtual void GoBack() override { }
    virtual void GoForward() override { }
    virtual void ClearHistory() override { }
    virtual void EnableHistory(bool enable = true) override { }
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem>> GetBackwardHistory() override { return {}; }
    virtual wxVector<wxSharedPtr<wxWebViewHistoryItem>> GetForwardHistory() override { return {}; }
    virtual void LoadHistoryItem(wxSharedPtr<wxWebViewHistoryItem> item) override { }
    virtual bool CanSetZoomType(wxWebViewZoomType type) const override { return false; }
    virtual float GetZoomFactor() const override { return 0.0f; }
    virtual wxWebViewZoomType GetZoomType() const override { return wxWebViewZoomType(); }
    virtual void SetZoomFactor(float zoom) override { }
    virtual void SetZoomType(wxWebViewZoomType zoomType) override { }
    virtual bool CanUndo() const override { return false; }
    virtual bool CanRedo() const override { return false; }
    virtual void Undo() override { }
    virtual void Redo() override { }
    virtual void* GetNativeBackend() const override { return nullptr; }
    virtual void DoSetPage(const wxString& html, const wxString& baseUrl) override { }
};

static std::vector<wxWebView*> g_webviews;
static std::vector<wxWebView*> g_delay_webviews;
static bool g_webview_shutting_down = false;
#ifdef __WIN32__
static std::set<DWORD> g_webview_browser_pids;
static bool g_webview_atexit_registered = false;
static wxString g_webview_userdata_dir;
static wxString g_webview_userdata_marker;
static bool g_webview_userdata_ephemeral = false;
static std::unique_ptr<wxWebViewConfiguration> g_webview_configuration;
static bool g_force_single_process = false;
#endif
#if defined __linux__
static bool g_linux_webview_schemes_registered = false;
#endif

class WebViewRef : public wxObjectRefData
{
public:
    WebViewRef(wxWebView *webView) : m_webView(webView) {}
    ~WebViewRef() {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " wxWebView address: " << (void*) m_webView;
        auto iter = std::find(g_webviews.begin(), g_webviews.end(), m_webView);
        if (iter != g_webviews.end())
            g_webviews.erase(iter);
        g_delay_webviews.erase(std::remove(g_delay_webviews.begin(), g_delay_webviews.end(), m_webView),
                               g_delay_webviews.end());
    }
    wxWebView *m_webView;
};

#ifdef __WIN32__
static bool is_valid_webview_profile_name(const wxString& name)
{
    return name.StartsWith("profile") && name != "." && name != ".." &&
           !name.Contains("/") && !name.Contains("\\") && !name.Contains(":");
}

// True when a process with this PID still exists. Errors other than "no such process" count as
// alive, so a profile is never deleted or reclaimed by mistake (PID reuse also errs on this side).
static bool is_process_alive(unsigned long pid)
{
    if (pid == 0)
        return false;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (process == nullptr)
        return GetLastError() != ERROR_INVALID_PARAMETER;
    DWORD exit_code = 0;
    const bool alive = !GetExitCodeProcess(process, &exit_code) || exit_code == STILL_ACTIVE;
    CloseHandle(process);
    return alive;
}

// Reads the PID written in a profile's ".running" marker; 0 when it cannot be read.
static unsigned long read_webview_marker_pid(const wxString& marker_path)
{
    wxLogNull no_log; // a marker being written by another instance cannot be opened: not an error
    wxFile marker;
    if (!marker.Open(marker_path, wxFile::read))
        return 0;
    char buffer[32] = {};
    const ssize_t read = marker.Read(buffer, sizeof(buffer) - 1);
    if (read <= 0)
        return 0;
    unsigned long pid = 0;
    return wxString::FromUTF8(buffer, size_t(read)).Trim().Trim(false).ToULong(&pid) ? pid : 0;
}

// True while a WebView2 browser process uses the profile. Chromium keeps
// "<profile>\EBWebView\lockfile" open without write sharing and deletes it on a clean exit;
// msedgewebview2.exe processes can outlive the app that started them, so a dead app PID
// alone does not prove the profile is free. Errors other than "not found" count as locked.
static bool is_webview_profile_locked(const wxString& profile_dir)
{
    wxFileName lock_file(profile_dir, "lockfile");
    lock_file.AppendDir("EBWebView");
    HANDLE handle = CreateFileW(lock_file.GetFullPath().wc_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
        return false;
    }
    const DWORD error = GetLastError();
    return error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND;
}

// True when the file or folder was modified less than 10 minutes ago, or its time is unknown.
static bool modified_recently(const wxFileName& path)
{
    wxDateTime modified;
    if (!path.GetTimes(nullptr, &modified, nullptr))
        return true;
    return wxDateTime::Now().GetTicks() - modified.GetTicks() < 10 * 60;
}

// Deletes the ".running" marker of a crashed or killed instance. The marker is opened
// exclusively and its PID is read through that handle, so only the marker that was checked
// can be deleted: a marker just rewritten by another instance holds a live PID, or is still
// open and cannot be opened here.
static bool remove_stale_webview_marker(const wxString& profile_dir, const wxString& marker_path, unsigned long& owner_pid)
{
    owner_pid = 0;
    HANDLE handle = CreateFileW(marker_path.wc_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;

    char buffer[32] = {};
    DWORD read = 0;
    if (ReadFile(handle, buffer, sizeof(buffer) - 1, &read, nullptr) && read > 0 &&
        !wxString::FromUTF8(buffer, size_t(read)).Trim().Trim(false).ToULong(&owner_pid))
        owner_pid = 0;

    bool removed = false;
    if (owner_pid != 0 && !is_process_alive(owner_pid) && !is_webview_profile_locked(profile_dir)) {
        FILE_DISPOSITION_INFO disposition{TRUE};
        removed = SetFileInformationByHandle(handle, FileDispositionInfo, &disposition, sizeof(disposition)) != 0;
    }
    CloseHandle(handle);
    return removed;
}

static bool claim_webview_profile(const wxString& profile_dir, wxString& marker_path)
{
    if (!wxFileName::Mkdir(profile_dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) &&
        !wxFileName::DirExists(profile_dir)) {
        return false;
    }

    wxFileName marker(profile_dir, ".running");
    HANDLE marker_handle = CreateFileW(marker.GetFullPath().wc_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    unsigned long owner_pid = 0;
    if (marker_handle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_FILE_EXISTS &&
        remove_stale_webview_marker(profile_dir, marker.GetFullPath(), owner_pid)) {
        // The marker of a crashed or killed instance stays behind: reclaim the profile. If another
        // instance creates its marker first, CREATE_NEW fails and the profile is left to it.
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": reclaiming WebView2 profile left by process " << owner_pid;
        marker_handle = CreateFileW(marker.GetFullPath().wc_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (marker_handle == INVALID_HANDLE_VALUE)
        return false;

    const wxString pid = wxString::Format("%lu", static_cast<unsigned long>(wxGetProcessId()));
    const wxScopedCharBuffer pid_utf8 = pid.utf8_str();
    DWORD written = 0;
    WriteFile(marker_handle, pid_utf8.data(), static_cast<DWORD>(pid_utf8.length()), &written, nullptr);
    CloseHandle(marker_handle);
    marker_path = marker.GetFullPath();
    return true;
}

static void save_active_webview_profile(const wxString& active_file_path, const wxString& profile_name)
{
    wxTextFile active_file;
    if (wxFileName::FileExists(active_file_path)) {
        if (!active_file.Open(active_file_path))
            return;
        active_file.Clear();
    } else if (!active_file.Create(active_file_path)) {
        return;
    }

    active_file.AddLine(profile_name);
    active_file.Write();
    active_file.Close();
}

// Deletes the profiles under root_path that no running instance uses: no ".running" marker,
// or a marker whose process is gone, and no WebView2 process holding the profile lock.
// The profile in use, the one named in active_profile.txt (possibly just written by another
// instance) and folders or markers touched in the last 10 minutes are kept.
static void remove_stale_webview_profiles(const wxString& root_path, const wxString& profile_in_use)
{
    wxDir root_dir(root_path);
    if (!root_dir.IsOpened())
        return;

    wxString active_profile;
    wxTextFile active_file;
    const wxString active_file_path = wxFileName(root_path, "active_profile.txt").GetFullPath();
    if (wxFileName::FileExists(active_file_path) && active_file.Open(active_file_path)) {
        if (active_file.GetLineCount() > 0)
            active_profile = active_file.GetLine(0);
        active_file.Close();
    }

    std::vector<wxString> stale;
    wxString name;
    for (bool found = root_dir.GetFirst(&name, wxEmptyString, wxDIR_DIRS); found; found = root_dir.GetNext(&name)) {
        if (!is_valid_webview_profile_name(name) || name == profile_in_use || name == active_profile)
            continue;
        const wxString dir_path = wxFileName(root_path, name).GetFullPath();
        // Another instance may be creating or using this profile right now.
        if (modified_recently(wxFileName::DirName(dir_path)))
            continue;
        const wxFileName marker(dir_path, ".running");
        if (marker.FileExists()) {
            if (modified_recently(marker))
                continue;
            const unsigned long pid = read_webview_marker_pid(marker.GetFullPath());
            if (pid == 0 || is_process_alive(pid))
                continue;
        }
        if (is_webview_profile_locked(dir_path))
            continue;
        stale.push_back(dir_path);
    }

    for (const wxString& dir_path : stale) {
        if (wxFileName::Rmdir(dir_path, wxPATH_RMDIR_RECURSIVE))
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": removed stale WebView2 profile " << dir_path.ToUTF8().data();
        else
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": could not remove stale WebView2 profile " << dir_path.ToUTF8().data();
    }
}

static void initialize_webview_userdata_dir()
{
    wxFileName root(wxStandardPaths::Get().GetTempDir(), "");
    root.AppendDir("MentorSlicer_webview");

    const wxString root_path = root.GetFullPath();
    if (!wxFileName::Mkdir(root_path, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL) &&
        !wxFileName::DirExists(root_path)) {
        wxFileName fallback(wxStandardPaths::Get().GetTempDir(), "");
        fallback.AppendDir(wxString::Format("MentorSlicer_webview_%lu",
                                           static_cast<unsigned long>(wxGetProcessId())));
        wxFileName::Mkdir(fallback.GetFullPath(), wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
        g_webview_userdata_dir = fallback.GetFullPath();
        g_webview_userdata_ephemeral = true;
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << ": failed to create reusable profile root; using per-run folder: "
                                   << g_webview_userdata_dir.ToStdString();
        wxSetEnv("WEBVIEW2_USER_DATA_FOLDER", g_webview_userdata_dir);
        return;
    }

    const wxString active_file_path = wxFileName(root_path, "active_profile.txt").GetFullPath();
    wxString profile_name = "profile";
    if (wxFileName::FileExists(active_file_path)) {
        wxTextFile active_file;
        if (active_file.Open(active_file_path) && active_file.GetLineCount() > 0 &&
            is_valid_webview_profile_name(active_file.GetLine(0))) {
            profile_name = active_file.GetLine(0);
        }
        if (active_file.IsOpened())
            active_file.Close();
    }

    wxFileName profile(root_path, "");
    profile.AppendDir(profile_name);
    const bool reused_profile = claim_webview_profile(profile.GetFullPath(), g_webview_userdata_marker);
    if (!reused_profile) {
        profile_name = wxString::Format("profile_%lu_%llu",
                                        static_cast<unsigned long>(wxGetProcessId()),
                                        static_cast<unsigned long long>(GetTickCount64()));
        profile.AssignDir(root_path);
        profile.AppendDir(profile_name);
        if (!claim_webview_profile(profile.GetFullPath(), g_webview_userdata_marker)) {
            g_webview_userdata_ephemeral = true;
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << ": failed to claim reusable and fallback profiles; folder will be removed on exit.";
        }
    }

    g_webview_userdata_dir = profile.GetFullPath();
    save_active_webview_profile(active_file_path, profile_name);
    try {
        remove_stale_webview_profiles(root_path, profile_name);
    } catch (const std::exception& ex) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": stale WebView2 profile cleanup failed: " << ex.what();
    }
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                               << (reused_profile ? ": using reusable WebView2 profile: "
                                                  : ": active profile is occupied; using fallback profile: ")
                               << g_webview_userdata_dir.ToStdString();
    wxSetEnv("WEBVIEW2_USER_DATA_FOLDER", g_webview_userdata_dir);
}
#endif

wxWebView* WebView::CreateWebView(wxWindow * parent, wxString const & url)
{
    if (g_webview_shutting_down) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": using inert view during application shutdown";
        auto* webView = new FakeWebView;
        webView->Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition,
                        wxDefaultSize, wxBORDER_NONE, wxWebViewNameStr);
        return webView;
    }

    bool using_edge_fixed = false;
#if wxUSE_WEBVIEW_EDGE
    // Check if a fixed version of edge is present in
    // $executable_path/edge_fixed and use it
    wxFileName edgeFixedDir(wxStandardPaths::Get().GetExecutablePath());
    edgeFixedDir.SetFullName("");
    edgeFixedDir.AppendDir("edge_fixed");
    using_edge_fixed = edgeFixedDir.DirExists();
    if (using_edge_fixed) {
        wxWebViewEdge::MSWSetBrowserExecutableDir(edgeFixedDir.GetFullPath());
        wxLogMessage("Using fixed edge version");
    }
    // parepare for single process mode, but edge_fixed2 is not used now, so we don't set it.
    wxFileName edgeFixedDir2(wxStandardPaths::Get().GetExecutablePath());
    edgeFixedDir2.SetFullName("");
    edgeFixedDir2.AppendDir("edge_fixed2");
    if (edgeFixedDir2.DirExists()) {
        wxWebViewEdge::MSWSetBrowserExecutableDir(edgeFixedDir2.GetFullPath());
        wxLogMessage("Using fixed edge version");
    }
    if (g_webview_userdata_dir.empty())
        initialize_webview_userdata_dir();
#endif
    auto url2  = url;
#ifdef __WIN32__
    url2.Replace("\\", "/");
#endif
    if (!url2.empty()) { url2 = wxURI(url2).BuildURI(); }
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ": " << url2.ToUTF8();

#ifdef __WIN32__
    wxString previous_browser_args;
    const bool had_previous_args = wxGetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", &previous_browser_args);
    const bool use_single_process = g_force_single_process && !using_edge_fixed;
    if (g_force_single_process && using_edge_fixed) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": edge_fixed detected; ignoring WebView2 single-process mode.";
    }
    if (use_single_process) {
        wxSetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", wxS("--single-process"));
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": enabling WebView2 single-process mode for creation.";
    }
    // Keep one Edge configuration alive for the whole process. Reusing its
    // underlying WebView2 environment prevents rapid GUI recreation from
    // closing one environment while the next controls are being initialized.
    if (!g_webview_configuration) {
        wxWebViewConfiguration config = wxWebView::NewConfiguration(wxWebViewBackendEdge);
        config.SetDataPath(g_webview_userdata_dir);
        g_webview_configuration = std::make_unique<wxWebViewConfiguration>(config);
    }
    // Initial startup keeps the old direct-navigation behaviour. GUI recreation
    // snapshots a deferred-navigation policy because m_is_recreating_gui may be
    // reset before the asynchronous WebView2 creation event is delivered.
    const bool defer_navigation = Slic3r::GUI::wxGetApp().is_recreating_gui();
    wxWebView* webView = new WebViewEdge(*g_webview_configuration, defer_navigation);
    BOOST_LOG_TRIVIAL(warning)
        << __FUNCTION__ << ": WebView2 navigation mode="
        << (defer_navigation ? "deferred (GUI recreation)" : "direct (initial startup)");
#elif defined(__WXOSX__)
    wxWebView *webView = new WebViewWebKit;
#else
    auto webView = wxWebView::New();
#endif
    if (webView) {
        webView->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
#ifdef __WIN32__
        webView->SetUserAgent(wxString::Format("Creality-Slicer/v%s (%s) Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
            "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/107.0.0.0 Safari/537.36 Edg/107.0.1418.52", SLIC3R_VERSION, 
            Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
        if (defer_navigation) {
            // During GUI recreation, wait for wxEVT_WEBVIEW_CREATED before
            // navigating to avoid ERROR_INVALID_STATE while old controls close.
            webView->Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
            if (!url2.empty())
                webView->LoadURL(url2);
        } else {
            // On initial startup let wxWebView2 handle the initial URL as part
            // of Create(), matching the pre-10ece62d62 behaviour.
            webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        }
        // We register the wxfs:// protocol for testing purposes
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("bbl")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
#elif defined __linux__
        if (webView) {
            std::ifstream osRelease("/etc/os-release");
            std::string   line;
            bool          isUOS = false;
            while (std::getline(osRelease, line)) {
                if (line.find("UOS") != std::string::npos || line.find("UnionTech") != std::string::npos) {
                    isUOS = true;
                    break;
                }
            }
            if (isUOS) {
                webView->SetUserAgent(
                    wxString::Format("Creality-Slicer/v%s (%s; UOS)", SLIC3R_VERSION, Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
                setenv("WEBKIT_DISABLE_TLS_VERIFICATION", "0", 0);
                setenv("G_TLS_GNUTLS_PRIORITY", "NORMAL:-VERS-ALL:+VERS-TLS1.2", 1);
                setenv("WEBKIT_TLS_ERRORS_POLICY", "ignore-tls-errors", 0);
                BOOST_LOG_TRIVIAL(info) << "Set TLS 1.2 as default for WebView on Linux";
            } else {
                webView->SetUserAgent(
                    wxString::Format("Creality-Slicer/v%s (%s; Linux) Mozilla/5.0 (X11; Linux) AppleWebKit/537.36 (KHTML, like Gecko)",
                                     SLIC3R_VERSION, Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
            }
            if (!g_linux_webview_schemes_registered) {
                webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
                webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
                g_linux_webview_schemes_registered = true;
            }
            webView->Create(parent, wxID_ANY, url2, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        }
#else
        // With WKWebView handlers need to be registered before creation
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewArchiveHandler("wxfs")));
        // And the memory: file system
        webView->RegisterHandler(wxSharedPtr<wxWebViewHandler>(new wxWebViewFSHandler("memory")));
        // Create only the empty document; business pages must wait for "wx".
        webView->Create(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
        webView->SetUserAgent(wxString::Format("Creality-Slicer/v%s (%s) Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko)", SLIC3R_VERSION,
                                               Slic3r::GUI::wxGetApp().dark_mode() ? "dark" : "light"));
        if (!url2.empty())
            webView->LoadURL(url2);
#endif
#ifdef __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        Slic3r::GUI::WKWebView_setTransparentBackground(wkWebView);
#endif

        auto addScriptMessageHandler = [] (wxWebView *webView) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": begin to add script message handler for wx.";
            Slic3r::GUI::wxGetApp().set_adding_script_handler(true);
            if (!webView->AddScriptMessageHandler("wx"))
                wxLogError("Could not add script message handler");
            Slic3r::GUI::wxGetApp().set_adding_script_handler(false);
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": finished add script message handler for wx.";
        };
#ifndef __WIN32__
        webView->CallAfter([webView, addScriptMessageHandler] {
#endif
            if (Slic3r::GUI::wxGetApp().is_adding_script_handler()) {
                g_delay_webviews.push_back(webView);
            } else {
                addScriptMessageHandler(webView);
                while (!g_delay_webviews.empty()) {
                    auto views = std::move(g_delay_webviews);
                    for (auto wv : views)
                        addScriptMessageHandler(wv);
                }
            }
#ifndef __WIN32__
        });
#endif
        webView->EnableContextMenu(true);
    } else {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": failed. Use fake web view.";
        webView = new FakeWebView;
    }
    webView->SetRefData(new WebViewRef(webView));
    g_webviews.push_back(webView);
#ifdef __WIN32__
    if (use_single_process) {
        if (had_previous_args)
            wxSetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", previous_browser_args);
        else
            wxUnsetEnv("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS");
    }
    if (!g_webview_atexit_registered) {
        g_webview_atexit_registered = true;
        std::atexit([] {
            BOOST_LOG_TRIVIAL(warning)
                << "[WebView][atexit] triggered, g_webviews.size()=" << g_webviews.size();
            WebView::BeginShutdown();
            WebView::FinalizeShutdown();
        });
    }
#endif
    return webView;
}
#if wxUSE_WEBVIEW_EDGE
void WebView::SetForceSingleProcess(bool force_single_process)
{
#ifdef __WIN32__
    g_force_single_process = force_single_process;
#else
    (void)force_single_process;
#endif
}
#endif
#if wxUSE_WEBVIEW_EDGE
bool WebView::CheckWebViewRuntime()
{
    wxWebViewFactoryEdge factory;
    auto wxVersion = factory.GetVersionInfo(wxVersionContext::RunTime);
    return wxVersion.GetMajor() != 0;
}
#endif

bool WebView::ConfigureHardwareAccelerationForMjpeg(wxWebView *webView)
{
#if defined(__linux__)
    if (webView == nullptr)
        return false;

    const guint major = webkit_get_major_version();
    const guint minor = webkit_get_minor_version();
    const guint micro = webkit_get_micro_version();

    bool disable = major < 2 || (major == 2 && minor < 42);

    wxString override_value;
    if (wxGetEnv("CREALITY_WEBVIEW_HARDWARE_ACCELERATION", &override_value)) {
        override_value.Trim(true).Trim(false).MakeLower();
        if (override_value == "never")
            disable = true;
        else if (override_value == "default")
            disable = false;
        else if (!override_value.empty() && override_value != "auto") {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": ignoring invalid CREALITY_WEBVIEW_HARDWARE_ACCELERATION="
                                       << override_value.ToStdString() << " (expected auto, never, or default)";
        }
    }

    if (!disable) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": WebKitGTK " << major << "." << minor << "." << micro
                                << "; keeping default hardware acceleration";
        return true;
    }

    auto *native = static_cast<WebKitWebView *>(webView->GetNativeBackend());
    if (native == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": WebKit backend is not ready";
        return false;
    }

    WebKitSettings *settings = webkit_web_view_get_settings(native);
    if (settings == nullptr) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": WebKit settings are unavailable";
        return false;
    }

    webkit_settings_set_hardware_acceleration_policy(settings, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": WebKitGTK " << major << "." << minor << "." << micro
                            << "; hardware acceleration policy set to NEVER for MJPEG WebView";
    return true;
#else
    return false;
#endif
}

void WebView::LoadUrl(wxWebView * webView, wxString const &url)
{
    if (g_webview_shutting_down || webView == nullptr)
        return;

    auto url2  = url;
#ifdef __WIN32__
    url2.Replace("\\", "/");
#endif
    if (!url2.empty()) { url2 = wxURI(url2).BuildURI(); }
    BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << url2.ToUTF8();
    webView->LoadURL(url2);
}

bool WebView::RunScript(wxWebView *webView, wxString const &javascript)
{
    if (g_webview_shutting_down || webView == nullptr)
        return false;

    if (Slic3r::GUI::wxGetApp().app_config->get("internal_developer_mode") == "true"
            && javascript.find("studio_userlogin") == wxString::npos)
        wxLogMessage("Running JavaScript:\n%s\n", javascript);

    try {
#ifdef __WIN32__
        ICoreWebView2 *   webView2 = (ICoreWebView2 *) webView->GetNativeBackend();
        if (webView2 == nullptr)
            return false;
        return webView2->ExecuteScript(javascript, NULL) == 0;
#elif defined __WXMAC__
        WKWebView * wkWebView = (WKWebView *) webView->GetNativeBackend();
        Slic3r::GUI::WKWebView_evaluateJavaScript(wkWebView, javascript, nullptr);
        return true;
#else
        WebKitWebView *wkWebView = (WebKitWebView *) webView->GetNativeBackend();
        webkit_web_view_run_javascript(
            wkWebView, javascript.utf8_str(), NULL,
            [](GObject *wkWebView, GAsyncResult *res, void *) {
                GError * error = NULL;
                auto result = webkit_web_view_run_javascript_finish((WebKitWebView*)wkWebView, res, &error);
                if (!result)
                    g_error_free (error);
                else
                    webkit_javascript_result_unref (result);
        }, NULL);
        return true;
#endif
    } catch (std::exception &e) {
        return false;
    }
}

bool WebView::IsShuttingDown()
{
    return g_webview_shutting_down;
}

void WebView::BeginShutdown()
{
    if (!g_webview_shutting_down) {
        g_webview_shutting_down = true;
        g_delay_webviews.clear();
    }

#ifdef __WIN32__
    // WebView2 initialization is asynchronous, so scan on every call and add
    // PIDs that were not available when shutdown first began. Do not Stop(),
    // navigate, clear g_webviews, wait, or terminate while controls are alive.
    for (auto* webView : g_webviews) {
        if (!webView)
            continue;
        auto* webView2 = reinterpret_cast<ICoreWebView2*>(webView->GetNativeBackend());
        if (!webView2)
            continue;

        UINT32 pid = 0;
        if (SUCCEEDED(webView2->get_BrowserProcessId(&pid)) && pid != 0)
            g_webview_browser_pids.insert(static_cast<DWORD>(pid));
    }
#endif

    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": webviews=" << g_webviews.size()
#ifdef __WIN32__
                               << ", browser_pids=" << g_webview_browser_pids.size()
#endif
        ;
}

static bool wait_for_webview_browser_processes()
{
#ifdef __WIN32__
    if (g_webview_browser_pids.empty())
        return true;

    const ULONGLONG deadline = GetTickCount64() + 5000;
    while (!g_webview_browser_pids.empty() && GetTickCount64() < deadline) {
        for (auto iter = g_webview_browser_pids.begin(); iter != g_webview_browser_pids.end();) {
            const DWORD pid = *iter;
            HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, pid);
            if (handle) {
                const DWORD wait_result = WaitForSingleObject(handle, 0);
                CloseHandle(handle);
                if (wait_result == WAIT_OBJECT_0) {
                    iter = g_webview_browser_pids.erase(iter);
                    continue;
                }
            } else if (GetLastError() == ERROR_INVALID_PARAMETER) {
                // The process no longer exists.
                iter = g_webview_browser_pids.erase(iter);
                continue;
            }
            ++iter;
        }
        if (!g_webview_browser_pids.empty())
            Sleep(50);
    }

    for (auto iter = g_webview_browser_pids.begin(); iter != g_webview_browser_pids.end();) {
        const DWORD pid = *iter;
        HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!handle) {
            const DWORD error = GetLastError();
            if (error == ERROR_INVALID_PARAMETER) {
                iter = g_webview_browser_pids.erase(iter);
                continue;
            }
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << ": cannot terminate WebView2 browser process " << pid
                                       << ", error=" << error;
            ++iter;
            continue;
        }

        if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                       << ": terminating WebView2 browser process " << pid;
            if (!TerminateProcess(handle, 1))
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                           << ": TerminateProcess failed for " << pid
                                           << ", error=" << GetLastError();
        }
        const bool stopped = WaitForSingleObject(handle, 2000) == WAIT_OBJECT_0;
        CloseHandle(handle);
        if (stopped) {
            iter = g_webview_browser_pids.erase(iter);
            continue;
        }
        ++iter;
    }

    return g_webview_browser_pids.empty();
#else
    return true;
#endif
}

void WebView::FinalizeShutdown()
{
    // Always rescan before finalization so a backend that completed after the
    // first shutdown request is still captured.
    BeginShutdown();

    // Releasing the shared environment or killing WebView2 while a wxWebView
    // still exists recreates the shutdown race this split is intended to fix.
    if (!g_webviews.empty()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << ": deferred because "
                                   << g_webviews.size() << " webview(s) are still alive";
        return;
    }

#if wxUSE_WEBVIEW_EDGE
#ifdef __WIN32__
    // Drop the last shared environment reference before waiting for the
    // browser processes, but keep the profile marker until they are gone.
    g_webview_configuration.reset();
#endif
#endif
    const bool browser_processes_stopped = wait_for_webview_browser_processes();

#if wxUSE_WEBVIEW_EDGE
    if (browser_processes_stopped) {
        ReleaseConfiguration();
    } else {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__
                                   << ": preserving WebView2 profile marker because "
                                   << g_webview_browser_pids.size() << " process(es) remain";
    }
#endif
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " done";
}

#if wxUSE_WEBVIEW_EDGE
void WebView::ReleaseConfiguration()
{
#ifdef __WIN32__
    // Release the shared WebView2 environment before wxWidgets unloads its
    // WebView module. Leaving this to the CRT static destructors is too late.
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " releasing shared WebView2 configuration";
    g_webview_configuration.reset();

    // Only mark the profile reusable after the shared WebView2 environment is released, and
    // only when no WebView2 process holds it any more. A kept marker names this (dead) process:
    // the next start reclaims the profile once the lock is gone.
    const bool profile_locked = !g_webview_userdata_dir.empty() && is_webview_profile_locked(g_webview_userdata_dir);
    if (profile_locked)
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " WebView2 profile still locked; keeping it: "
                                   << g_webview_userdata_dir.ToStdString();
    if (!g_webview_userdata_marker.empty()) {
        if (!profile_locked && !wxRemoveFile(g_webview_userdata_marker))
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " failed to remove WebView2 profile marker: "
                                       << g_webview_userdata_marker.ToStdString();
        g_webview_userdata_marker.clear();
    }
    if (g_webview_userdata_ephemeral && !g_webview_userdata_dir.empty() && !profile_locked)
        wxFileName::Rmdir(g_webview_userdata_dir, wxPATH_RMDIR_RECURSIVE);
    g_webview_userdata_dir.clear();
    g_webview_userdata_ephemeral = false;
#endif
}
#endif

