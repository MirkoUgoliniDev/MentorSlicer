#include "ReleaseNote.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"
#include "Widgets/WebView.hpp"
#include "Jobs/BoostThreadWorker.hpp"
#include "Jobs/PlaterWorker.hpp"

#include "slic3r/GUI/Widgets/AmsColors.hpp" // F5: AMS_CONTROL_* colours

#include <wx/regex.h>
#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <wx/stdpaths.h>
#include <wx/filename.h>
#include <wx/dir.h>
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/common_header/common_header.h"
#include <boost/log/trivial.hpp>
namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SECONDARY_CHECK_CONFIRM, wxCommandEvent);

ConfirmBeforeSendDialog::ConfirmBeforeSendDialog(wxWindow* parent, wxWindowID id, const wxString& title, enum ButtonStyle btn_style, const wxPoint& pos, const wxSize& size, long style, bool not_show_again_check)
    :DPIDialog(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/%2%.ico") % resources_dir() % Slic3r::CxBuildInfo::getIconName()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(400), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    m_vebview_release_note = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_vebview_release_note->SetScrollRate(0, 5);
    m_vebview_release_note->SetBackgroundColour(*wxWHITE);
    m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    m_sizer_right->Add(m_vebview_release_note, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(15));


    auto bottom_sizer = new wxBoxSizer(wxVERTICAL);
    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(21, 191, 89), StateColor::Pressed), 
        std::pair<wxColour, int>(wxColour(21, 191, 89), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(21, 191, 89), StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));


    if (not_show_again_check) {
        auto checkbox_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_show_again_checkbox = new wxCheckBox(this, wxID_ANY, _L("Don't show again"), wxDefaultPosition, wxDefaultSize, 0);
        m_show_again_checkbox->Bind(wxEVT_COMMAND_CHECKBOX_CLICKED, [this](wxCommandEvent& e) {
            not_show_again = !not_show_again;
            m_show_again_checkbox->SetValue(not_show_again);
        });
        checkbox_sizer->Add(FromDIP(15), 0, 0, 0);
        checkbox_sizer->Add(m_show_again_checkbox, 0, wxALL, FromDIP(5));
        bottom_sizer->Add(checkbox_sizer, 0, wxBOTTOM | wxEXPAND, 0);
    }
    m_button_ok = new Button(this, _L("Confirm"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(wxColour(255,255,255));
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(-1, FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SECONDARY_CHECK_CONFIRM, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        this->on_hide();
    });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(-1, FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        this->on_hide();
        });

    if (btn_style != CONFIRM_AND_CANCEL)
        m_button_cancel->Hide();
    else
        m_button_cancel->Show();
    
    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    sizer_button->Add(FromDIP(5),0, 0, 0);
    bottom_sizer->Add(sizer_button, 0, wxEXPAND | wxRIGHT | wxLEFT, 0);


    m_sizer_right->Add(bottom_sizer, 0, wxEXPAND | wxRIGHT | wxLEFT, FromDIP(20));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(10));

    m_sizer_main->Add(m_sizer_right, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    Bind(wxEVT_CLOSE_WINDOW, [this](auto& e) {this->on_hide(); });

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

void ConfirmBeforeSendDialog::update_text(wxString text)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    if (!m_staticText_release_note){
        m_staticText_release_note = new Label(m_vebview_release_note, text, LB_AUTO_WRAP);
        wxBoxSizer* top_blank_sizer = new wxBoxSizer(wxVERTICAL);
        wxBoxSizer* bottom_blank_sizer = new wxBoxSizer(wxVERTICAL);
        top_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        bottom_blank_sizer->Add(FromDIP(5), 0, wxALIGN_CENTER | wxALL, FromDIP(5));

        sizer_text_release_note->Add(top_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        sizer_text_release_note->Add(m_staticText_release_note, 0, wxALIGN_CENTER, FromDIP(5));
        sizer_text_release_note->Add(bottom_blank_sizer, 0, wxALIGN_CENTER | wxALL, FromDIP(5));
        m_vebview_release_note->SetSizer(sizer_text_release_note);
    }
    m_staticText_release_note->SetMaxSize(wxSize(FromDIP(380), -1));
    m_staticText_release_note->SetMinSize(wxSize(FromDIP(380), -1));
    m_staticText_release_note->SetLabelText(text);
    m_vebview_release_note->Layout();

    auto text_size = m_staticText_release_note->GetBestSize();
    if (text_size.y < FromDIP(380))
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), text_size.y + FromDIP(25)));
    else {
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    }

    Layout();
    Fit();
}

void ConfirmBeforeSendDialog::update_text(std::vector<ConfirmBeforeSendInfo> texts)
{
    wxBoxSizer* sizer_text_release_note = new wxBoxSizer(wxVERTICAL);
    m_vebview_release_note->SetSizer(sizer_text_release_note);

    auto height = 0;
    for (auto text : texts) {
        auto label_item = new Label(m_vebview_release_note, text.text, LB_AUTO_WRAP);
        if (text.level == ConfirmBeforeSendInfo::InfoLevel::Warning) {
            label_item->SetForegroundColour(wxColour(0xFF, 0x6F, 0x00));
        }
        label_item->SetMaxSize(wxSize(FromDIP(380), -1));
        label_item->SetMinSize(wxSize(FromDIP(380), -1));
        label_item->Wrap(FromDIP(380));
        label_item->Layout();
        sizer_text_release_note->Add(label_item, 0, wxALIGN_CENTER | wxALL, FromDIP(3));
        height += label_item->GetSize().y;
    }
    
    m_vebview_release_note->Layout();
    if (height < FromDIP(380))
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), height + FromDIP(25)));
    else {
        m_vebview_release_note->SetMinSize(wxSize(FromDIP(400), FromDIP(380)));
    }

    Layout();
    Fit();
}

void ConfirmBeforeSendDialog::on_show()
{
    wxGetApp().UpdateDlgDarkUI(this);
    // recover button color
    wxMouseEvent evt_ok(wxEVT_LEFT_UP);
    m_button_ok->GetEventHandler()->ProcessEvent(evt_ok);
    wxMouseEvent evt_cancel(wxEVT_LEFT_UP);
    m_button_cancel->GetEventHandler()->ProcessEvent(evt_cancel);
    this->ShowModal();
}

void ConfirmBeforeSendDialog::on_hide()
{
    if (m_show_again_checkbox != nullptr && not_show_again && show_again_config_text != "")
        wxGetApp().app_config->set(show_again_config_text, "1");
    EndModal(wxID_OK);
}

void ConfirmBeforeSendDialog::update_btn_label(wxString ok_btn_text, wxString cancel_btn_text)
{
    m_button_ok->SetLabel(ok_btn_text);
    m_button_cancel->SetLabel(cancel_btn_text);
    rescale();
}

wxString ConfirmBeforeSendDialog::format_text(wxString str, int warp)
{
    Label st (this, str);
    wxString out_txt      = str;
    wxString count_txt    = "";
    int      new_line_pos = 0;

    for (int i = 0; i < str.length(); i++) {
        auto text_size = st.GetTextExtent(count_txt);
        if (text_size.x < warp) {
            count_txt += str[i];
        } else {
            out_txt.insert(i - 1, '\n');
            count_txt = "";
        }
    }
    return out_txt;
}

ConfirmBeforeSendDialog::~ConfirmBeforeSendDialog()
{

}

void ConfirmBeforeSendDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    rescale();
}

void ConfirmBeforeSendDialog::hide_button_ok()
{
    m_button_ok->Hide();
}

void ConfirmBeforeSendDialog::edit_cancel_button_txt(wxString txt)
{
    m_button_cancel->SetLabel(txt);
}

void ConfirmBeforeSendDialog::disable_button_ok()
{
    m_button_ok->Disable();
    m_button_ok->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
    m_button_ok->SetBorderColor(wxColour(0x90, 0x90, 0x90));
}

void ConfirmBeforeSendDialog::enable_button_ok()
{
    m_button_ok->Enable();
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(btn_bg_green);
}

void ConfirmBeforeSendDialog::rescale()
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}


 }} // namespace Slic3r::GUI
