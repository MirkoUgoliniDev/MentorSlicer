#ifndef slic3r_GUI_ReleaseNote_hpp_
#define slic3r_GUI_ReleaseNote_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
#include <wx/dataview.h>
#include <wx/gdicmn.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/settings.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/hyperlink.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/popupwin.h>
#include <wx/spinctrl.h>
#include <wx/artprov.h>
#include <wx/wrapsizer.h>
#include <wx/event.h>
#include <wx/hyperlink.h>
#include <wx/richtext/richtextctrl.h>

// F5: the Bambu AMS mapping popup header was removed; generic headers it used to bring in.
#include "Plater.hpp"
#include "Widgets/PopupWindow.hpp"
#include "Widgets/TextInput.hpp"
#include "Jobs/Worker.hpp"
#include <wx/simplebook.h>
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/ScrolledWindow.hpp"

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_SECONDARY_CHECK_CONFIRM, wxCommandEvent);

struct ConfirmBeforeSendInfo
{
    enum InfoLevel {
        Normal = 0,
        Warning = 1
    };
    InfoLevel level;
    wxString text;
    ConfirmBeforeSendInfo(wxString txt, InfoLevel lev = Normal) : text(txt), level(lev) {}
};

class ConfirmBeforeSendDialog : public DPIDialog
{
public:
    enum ButtonStyle {
        ONLY_CONFIRM = 0,
        CONFIRM_AND_CANCEL = 1,
        MAX_STYLE_NUM = 2
    };
    ConfirmBeforeSendDialog(
        wxWindow* parent,
        wxWindowID      id = wxID_ANY,
        const wxString& title = wxEmptyString,
        enum ButtonStyle btn_style = CONFIRM_AND_CANCEL,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long            style = wxCLOSE_BOX | wxCAPTION,
        bool not_show_again_check = false
    );
    void update_text(wxString text);
    void update_text(std::vector<ConfirmBeforeSendInfo> texts);
    void on_show();
    void on_hide();
    void update_btn_label(wxString ok_btn_text, wxString cancel_btn_text);
    void rescale();
    void on_dpi_changed(const wxRect& suggested_rect);
    void hide_button_ok();
    void edit_cancel_button_txt(wxString txt);
    void disable_button_ok();
    void enable_button_ok();
    wxString format_text(wxString str, int warp);

    ~ConfirmBeforeSendDialog();

    wxBoxSizer* m_sizer_main;
    wxScrolledWindow* m_vebview_release_note{ nullptr };
    Label* m_staticText_release_note{ nullptr };
    Button* m_button_ok;
    Button* m_button_cancel;
    wxCheckBox* m_show_again_checkbox;
    bool not_show_again = false;
    std::string show_again_config_text = "";
};




}} // namespace Slic3r::GUI

#endif
