#ifndef slic3r_GUI_EnableLiteModeDialog_hpp_
#define slic3r_GUI_EnableLiteModeDialog_hpp_

#include "GUI_Utils.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/WebView.hpp"
#include <wx/webview.h>
#include <wx/progdlg.h>
#include <wx/simplebook.h>
#include <wx/html/htmlwin.h>

namespace Slic3r { namespace GUI {


class EnableLiteModeDialog : public DPIDialog
{
public:
    enum ButtonStyle {
        ONLY_CONFIRM = 0,
        CONFIRM_AND_CANCEL = 1,
        MAX_STYLE_NUM = 2
    };
    EnableLiteModeDialog(
        wxWindow* parent,
        wxWindowID      id = wxID_ANY,
        const wxString& title = wxEmptyString,
        enum ButtonStyle btn_style = CONFIRM_AND_CANCEL,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxPD_APP_MODAL| wxCAPTION
    );
    void onLinkClicked(wxHtmlLinkEvent& event);
    wxWebView* CreateTipView(wxWindow* parent);
    void OnNavigating(wxWebViewEvent& event);
    bool ShowReleaseNote(std::string content);
    void RunScript(std::string script);
    void set_text(std::string str) { m_mkdown_text = str; };
    void on_show();
    void on_hide();
    void update_btn_label(wxString ok_btn_text, wxString cancel_btn_text);
    void rescale();
    ~EnableLiteModeDialog();
    void on_dpi_changed(const wxRect& suggested_rect);

    wxBoxSizer* m_sizer_main;
    wxWebView* m_vebview_release_note{ nullptr };
    Label* m_staticText_release_note{ nullptr };
    Button* m_button_ok;
    Button* m_button_cancel;
    std::string m_mkdown_text;
    std::string m_host_url;
};

}} // namespace Slic3r::GUI

#endif
