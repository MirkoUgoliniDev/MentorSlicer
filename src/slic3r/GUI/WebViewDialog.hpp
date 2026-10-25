#ifndef slic3r_WebViewDialog_hpp_
#define slic3r_WebViewDialog_hpp_


#include "ImGuiWrapper.hpp"
#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include <wx/webview.h>

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>


namespace Slic3r {

namespace GUI {


class WebViewPanel : public wxPanel
{
public:
    WebViewPanel(wxWindow *parent);
    virtual ~WebViewPanel();

    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent &evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);
    void OnScriptResponseMessage(wxCommandEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void RunScript(const wxString& javascript);
    void OnClose(wxCloseEvent& evt);

    bool m_preferences_sent{false}; // preferences are sent to the page once, on the first script message

public:
    void SendRecentList(int images);

    void update_mode();
    void sync_preferences(const std::vector<wxString>& prefs);
    void Reload();
    wxString GetURL();
	wxWebView* Browse(){ return m_browser;};
private:

    wxWebView*  m_browser = nullptr;
    wxInfoBar *m_info;

    // Last executed JavaScript snippet, for convenience.
    wxString m_javascript;
    wxString m_response_js;

#ifdef __WXGTK__
    // When using GTK, there may be a problem of synthetic dirty area failure, so perform a low-frequency refresh
    wxTimer* m_freshTimer;
#endif

};


} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
