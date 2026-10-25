#include "AdvisorConfirmDialog.hpp"
#include "AdvisorBranding.hpp"
#include "PrintAdvisorInternal.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/listctrl.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>

namespace Slic3r {
namespace GUI {

namespace {

const wxColour& advisor_green = advisor_branding::green;
const wxColour& advisor_red = advisor_branding::red;

// Table cells may hold untrusted names (profiles, objects): one line, bounded length.
wxString table_cell(const std::string& text)
{
    return text.empty() ? wxString() : wxString::FromUTF8(advisor::sanitized_text(text, 160));
}

class AdvisorConfirmDialog : public wxDialog
{
public:
    AdvisorConfirmDialog(wxWindow* parent, const std::string& action, const std::string& message,
                         const std::vector<AdvisorChangeRow>& changes, bool destructive)
        : wxDialog(parent, wxID_ANY, wxString::FromUTF8("MCP Advisor - " + action), wxDefaultPosition, wxDefaultSize,
                   wxDEFAULT_DIALOG_STYLE)
    {
        const bool dark = wxGetApp().dark_mode();
        const wxColour background = dark ? wxColour(0x2D, 0x2D, 0x31) : *wxWHITE;
        const wxColour foreground = dark ? wxColour(0xEE, 0xEE, 0xEE) : wxColour(0x22, 0x22, 0x22);
        const wxColour muted = dark ? wxColour(0xA8, 0xA8, 0xA8) : wxColour(0x66, 0x66, 0x66);
        auto* root = new wxBoxSizer(wxVERTICAL);

        // Header: logo, product name and who asked.
        auto* header = new wxBoxSizer(wxHORIZONTAL);
        header->Add(new wxStaticBitmap(this, wxID_ANY, advisor_branding::logo(FromDIP(44))), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
        auto* titles = new wxBoxSizer(wxVERTICAL);
        auto* brand = new wxStaticText(this, wxID_ANY, "MCP Advisor");
        brand->SetFont(brand->GetFont().Bold().Scaled(1.35f));
        brand->SetForegroundColour(advisor_green);
        titles->Add(brand);
        auto* action_text = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(action));
        action_text->SetFont(action_text->GetFont().Bold().Scaled(1.1f));
        titles->Add(action_text, 0, wxTOP, FromDIP(2));
        auto* origin = new wxStaticText(this, wxID_ANY, wxString::FromUTF8("Richiesta da MCP Advisor (Claude): nulla cambia senza la tua conferma"));
        origin->SetForegroundColour(muted);
        titles->Add(origin, 0, wxTOP, FromDIP(2));
        header->Add(titles, 1, wxALIGN_CENTER_VERTICAL);
        root->Add(header, 0, wxEXPAND | wxALL, FromDIP(16));
        root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

        // Message.
        auto* text = new wxStaticText(this, wxID_ANY, wxString::FromUTF8(message));
        text->Wrap(FromDIP(560));
        root->Add(text, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

        // Change table: parameter, before, after.
        wxListCtrl* table = nullptr;
        if (!changes.empty()) {
            table = new wxListCtrl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                         wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_SIMPLE);
            table->AppendColumn(wxString::FromUTF8("Parametro"), wxLIST_FORMAT_LEFT, FromDIP(280));
            table->AppendColumn(wxString::FromUTF8("Prima"), wxLIST_FORMAT_LEFT, FromDIP(140));
            table->AppendColumn(wxString::FromUTF8("Dopo"), wxLIST_FORMAT_LEFT, FromDIP(140));
            for (size_t i = 0; i < changes.size(); ++i) {
                const long row = table->InsertItem(long(i), table_cell(changes[i].parameter));
                table->SetItem(row, 1, table_cell(changes[i].before));
                table->SetItem(row, 2, table_cell(changes[i].after));
            }
            const int visible_rows = int(std::min<size_t>(changes.size(), 10));
            table->SetMinSize(wxSize(FromDIP(570), FromDIP(30 + 24 * visible_rows)));
            root->Add(table, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));
        }

        // Buttons: Cancel is the default and the Escape action.
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer();
        auto* cancel = new wxButton(this, wxID_CANCEL, wxString::FromUTF8("Annulla"));
        auto* confirm = new wxButton(this, wxID_OK, wxString::FromUTF8("Conferma"));
        confirm->SetBackgroundColour(destructive ? advisor_red : advisor_green);
        confirm->SetForegroundColour(*wxWHITE);
        buttons->Add(cancel, 0, wxRIGHT, FromDIP(8));
        buttons->Add(confirm, 0);
        root->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(16));

        SetEscapeId(wxID_CANCEL);
        SetAffirmativeId(wxID_OK);
        cancel->SetDefault();
        cancel->SetFocus();
        SetSizerAndFit(root);
        wxGetApp().UpdateDlgDarkUI(this);
        // Explicit colours after the theme update: the default dialog colours gave grey text on
        // a light background in the dark theme.
        SetBackgroundColour(background);
        for (wxStaticText* label : {action_text, text})
            label->SetForegroundColour(foreground);
        if (table) {
            table->SetBackgroundColour(background);
            table->SetTextColour(foreground);
        }
        cancel->SetBackgroundColour(dark ? wxColour(0x45, 0x45, 0x4A) : wxColour(0xE6, 0xE6, 0xE6));
        cancel->SetForegroundColour(foreground);
        // Keep the brand colours after the theme update.
        brand->SetForegroundColour(advisor_green);
        origin->SetForegroundColour(muted);
        confirm->SetBackgroundColour(destructive ? advisor_red : advisor_green);
        confirm->SetForegroundColour(*wxWHITE);
        CentreOnParent();
    }
};

} // namespace

bool advisor_confirm(const std::string& action, const std::string& message,
                     const std::vector<AdvisorChangeRow>& changes, bool destructive)
{
    wxWindow* parent = wxGetApp().mainframe;
    AdvisorConfirmDialog dialog(parent, action, message, changes, destructive);
    if (parent)
        parent->Raise();
    advisor::AdvisorUserWait user_wait;
    return dialog.ShowModal() == wxID_OK;
}

int& advisor::advisor_user_wait_depth()
{
    static int depth = 0;
    return depth;
}

} // namespace GUI
} // namespace Slic3r
