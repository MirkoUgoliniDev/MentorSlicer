#include "AdvisorInfoDialog.hpp"
#include "AdvisorBranding.hpp"
#include "PrintAdvisorHooks.hpp"

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MainFrame.hpp"

#include <wx/button.h>
#include <wx/clipbrd.h>
#include <wx/filename.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/stdpaths.h>
#include <wx/textctrl.h>

#include <algorithm>
#include <vector>

namespace Slic3r {
namespace GUI {

namespace {

// <data_dir>\cache\print-advisor\endpoint.json, with native separators.
wxString descriptor_path()
{
    wxFileName file(wxString::FromUTF8(data_dir()), "endpoint.json");
    file.AppendDir("cache");
    file.AppendDir("print-advisor");
    file.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return file.GetFullPath();
}

// True when the data folder is the default one (no --datadir): the sidecar then finds the
// descriptor without MENTORSLICER_ADVISOR_DESCRIPTOR.
bool descriptor_is_default()
{
    wxFileName standard(wxStandardPaths::Get().GetUserDataDir(), wxEmptyString);
    wxFileName current(wxString::FromUTF8(data_dir()), wxEmptyString);
    standard.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    current.Normalize(wxPATH_NORM_DOTS | wxPATH_NORM_ABSOLUTE);
    return standard.SameAs(current);
}

// mentorslicer-mcp.exe next to MentorSlicer.exe (built by tools\print-advisor-mcp\scripts\build-exe.mjs),
// or empty when this build does not have it.
wxString packaged_sidecar_path()
{
#ifdef _WIN32
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.SetFullName("mentorslicer-mcp.exe");
#else
    wxFileName exe(wxStandardPaths::Get().GetExecutablePath());
    exe.SetFullName("mentorslicer-mcp");
#endif
    return exe.FileExists() ? exe.GetFullPath() : wxString();
}

// The MCP sidecar folder of the source tree this executable was built from: development builds
// live in <repo>\build\src\Release, so walk up from the executable folder looking for
// tools\print-advisor-mcp\package.json. Empty when not found (installed copy).
wxString detect_sidecar_folder()
{
    wxFileName folder(wxStandardPaths::Get().GetExecutablePath());
    folder.SetFullName(wxEmptyString);
    for (int depth = 0; depth < 8; ++depth) {
        wxFileName candidate(folder.GetPath(), "package.json");
        candidate.AppendDir("tools");
        candidate.AppendDir("print-advisor-mcp");
        if (candidate.FileExists())
            return candidate.GetPath();
        if (folder.GetDirCount() == 0)
            break;
        folder.RemoveLastDir();
    }
    return wxEmptyString;
}

class AdvisorInfoDialog : public DPIDialog
{
public:
    explicit AdvisorInfoDialog(wxWindow* parent)
        : DPIDialog(parent, wxID_ANY, "Print Advisor MCP", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
        , m_palette(advisor_branding::palette())
    {
        const int width = FromDIP(560);
        const int wrap = width - FromDIP(24); // room for the vertical scroll bar
        auto* root = new wxBoxSizer(wxVERTICAL);

        // Header: logo, product name and title, like the confirmation dialogs.
        auto* header = new wxBoxSizer(wxHORIZONTAL);
        header->Add(new wxStaticBitmap(this, wxID_ANY, advisor_branding::logo(FromDIP(44))), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(12));
        auto* titles = new wxBoxSizer(wxVERTICAL);
        m_brand = new wxStaticText(this, wxID_ANY, "MCP Advisor");
        m_brand->SetFont(m_brand->GetFont().Bold().Scaled(1.35f));
        titles->Add(m_brand);
        auto* title = new wxStaticText(this, wxID_ANY, "Print Advisor MCP");
        title->SetFont(title->GetFont().Bold().Scaled(1.1f));
        m_labels.push_back(title);
        titles->Add(title, 0, wxTOP, FromDIP(2));
        auto* subtitle = new wxStaticText(this, wxID_ANY, _L("Connect an AI assistant to MentorSlicer"));
        m_muted.push_back(subtitle);
        titles->Add(subtitle, 0, wxTOP, FromDIP(2));
        header->Add(titles, 1, wxALIGN_CENTER_VERTICAL);
        root->Add(header, 0, wxEXPAND | wxALL, FromDIP(16));
        root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));

        // Body: scrolls when the screen is too short.
        m_body = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
        m_body->SetScrollRate(0, FromDIP(10));
        auto* body = new wxBoxSizer(wxVERTICAL);

        auto add_heading = [&](const wxString& text) {
            auto* label = new wxStaticText(m_body, wxID_ANY, text);
            label->SetFont(label->GetFont().Bold());
            m_labels.push_back(label);
            body->Add(label, 0, wxTOP, FromDIP(14));
        };
        auto add_text = [&](const wxString& text, bool bold = false, bool muted = false) {
            auto* label = new wxStaticText(m_body, wxID_ANY, text);
            if (bold)
                label->SetFont(label->GetFont().Bold());
            label->Wrap(wrap);
            (muted ? m_muted : m_labels).push_back(label);
            body->Add(label, 0, wxTOP, FromDIP(8));
        };
        auto add_bullet = [&](const wxString& text) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            auto* dot = new wxStaticText(m_body, wxID_ANY, wxString::FromUTF8("\xE2\x80\xA2"));
            m_labels.push_back(dot);
            auto* label = new wxStaticText(m_body, wxID_ANY, text);
            label->Wrap(wrap - FromDIP(18));
            m_labels.push_back(label);
            row->Add(dot, 0, wxLEFT | wxRIGHT, FromDIP(6));
            row->Add(label, 1);
            body->Add(row, 0, wxTOP, FromDIP(4));
        };
        auto add_step = [&](int number, const wxString& text) {
            auto* row = new wxBoxSizer(wxHORIZONTAL);
            auto* index = new wxStaticText(m_body, wxID_ANY, wxString::Format("%d.", number));
            index->SetMinSize(wxSize(FromDIP(18), -1));
            m_labels.push_back(index);
            auto* label = new wxStaticText(m_body, wxID_ANY, text);
            label->Wrap(wrap - FromDIP(24));
            m_labels.push_back(label);
            row->Add(index, 0, wxLEFT, FromDIP(4));
            row->Add(label, 1);
            body->Add(row, 0, wxTOP, FromDIP(6));
        };

        // What it is.
        add_heading(_L("What it is"));
        add_text(_L("A print assistant that connects an AI client (Claude Code or Codex) to MentorSlicer through MCP (Model Context Protocol). "
                    "The AI runs in the client: MentorSlicer only offers a local service on 127.0.0.1, protected by a session token "
                    "and active only while it is enabled here."));

        // What it allows.
        add_heading(_L("What it allows"));
        add_bullet(_L("Read the printer, filament and process profiles and their settings."));
        add_bullet(_L("Diagnose print defects and suggest changes, saved only as new copies of the profiles after your confirmation."));
        add_bullet(_L("Slice the current plate and measure print time and filament."));
        add_bullet(_L("Run checks before printing."));
        add_bullet(_L("With the Device tab open: printer status, camera snapshot, fans, temperatures, speed and light, "
                      "pause/resume/stop, chamber preheat and slow cooldown."));
        add_bullet(_L("Generate calibration tests and turn their result into profile values."));
        add_bullet(_L("Keep a diary of the prints and an archive of the successful ones."));
        add_text(_L("Every change asks for your confirmation in MentorSlicer. The Print Advisor never starts a print."), true);

        // How to install. The packaged sidecar (mentorslicer-mcp.exe, no Node.js needed) sits next to
        // MentorSlicer.exe; a build without it falls back to running the sources with Node.js.
        const wxString mcp_exe = packaged_sidecar_path();
        const bool     packaged = !mcp_exe.empty();
        const wxString found_folder = packaged ? wxString() : detect_sidecar_folder();
        wxString sidecar = found_folder;
        if (!packaged && sidecar.empty()) {
            wxFileName placeholder(_L("<MentorSlicer source>"), wxEmptyString);
            placeholder.AppendDir("tools");
            placeholder.AppendDir("print-advisor-mcp");
            sidecar = placeholder.GetPath();
        }
        // The sidecar finds the descriptor of the default data folder by itself: pass it only for
        // a copy started with --datadir.
        const wxString descriptor_env = descriptor_is_default() ? wxString() :
            " -e \"MENTORSLICER_ADVISOR_DESCRIPTOR=" + descriptor_path() + "\"";
        m_command = "claude mcp add mentorslicer-advisor --scope user --transport stdio" + descriptor_env + " -- ";
        if (packaged) {
            m_command += "\"" + mcp_exe + "\"";
        } else {
            wxFileName tsx(sidecar, "cli.mjs");
            tsx.AppendDir("node_modules");
            tsx.AppendDir("tsx");
            tsx.AppendDir("dist");
            wxFileName index_ts(sidecar, "index.ts");
            index_ts.AppendDir("src");
            m_command += "node \"" + tsx.GetFullPath() + "\" \"" + index_ts.GetFullPath() + "\"";
        }

        add_heading(_L("How to install"));
        int step = 1;
        if (!packaged) {
            add_text(_L("mentorslicer-mcp.exe was not found next to MentorSlicer.exe: this build runs the sidecar from its sources with Node.js."), false, true);
            add_step(step++, _L("Install Node.js 20 or later."));
            add_step(step++, wxString::Format(_L("Open a terminal in %s and run npm install."), sidecar));
        }
        add_step(step++, _L("Register it in Claude Code with this command (once: it is valid in every folder). "
                            "Then restart Claude Code or use /mcp."));
        m_command_text = new wxTextCtrl(m_body, wxID_ANY, m_command, wxDefaultPosition, wxSize(wrap - FromDIP(22), FromDIP(84)),
                                        wxTE_MULTILINE | wxTE_READONLY | wxTE_BESTWRAP | wxBORDER_SIMPLE);
        m_command_text->SetFont(wxFont(wxFontInfo(GetFont().GetPointSize()).Family(wxFONTFAMILY_TELETYPE)));
        m_copy = new wxButton(m_body, wxID_ANY, _L("Copy command"));
        m_copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { copy_command(); });
        auto* command_box = new wxBoxSizer(wxVERTICAL);
        command_box->Add(m_command_text, 0, wxEXPAND);
        command_box->Add(m_copy, 0, wxTOP, FromDIP(6));
        auto* command_row = new wxBoxSizer(wxHORIZONTAL);
        command_row->AddSpacer(FromDIP(22));
        command_row->Add(command_box, 1);
        body->Add(command_row, 0, wxEXPAND | wxTOP, FromDIP(6));
        add_step(step++, _L("Enable the service with the button below."));
        if (!packaged && found_folder.empty())
            add_text(_L("Replace the placeholder with the folder of the MentorSlicer sources."), false, true);
        add_text(_L("Codex is configured the same way: see the README in the sidecar folder (Codex section)."));

        m_body->SetSizer(body);
        body->Add(0, FromDIP(8));
        m_body->FitInside();
        const wxSize best = body->GetMinSize();
        m_body->SetMinSize(wxSize(width, std::min(best.y, FromDIP(400))));
        root->Add(m_body, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(16));
        root->Add(new wxStaticLine(this), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

        // Status of the local API.
        auto* status_row = new wxBoxSizer(wxHORIZONTAL);
        auto* status_label = new wxStaticText(this, wxID_ANY, _L("Status:"));
        status_label->SetFont(status_label->GetFont().Bold());
        m_labels.push_back(status_label);
        m_status = new wxStaticText(this, wxID_ANY, wxEmptyString);
        m_status->SetFont(m_status->GetFont().Bold());
        status_row->Add(status_label, 0, wxRIGHT, FromDIP(6));
        status_row->Add(m_status, 1);
        // The descriptor path has no spaces to wrap at: shorten it in the middle, full path in
        // the tooltip.
        m_detail = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(width, -1),
                                    wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);
        auto* status_box = new wxBoxSizer(wxVERTICAL);
        status_box->Add(status_row, 0, wxEXPAND);
        status_box->Add(m_detail, 0, wxEXPAND | wxTOP, FromDIP(4));
        root->Add(status_box, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(16));

        // Buttons: the switch and Close (default, Escape).
        auto* buttons = new wxBoxSizer(wxHORIZONTAL);
        buttons->AddStretchSpacer();
        m_toggle = new wxButton(this, wxID_ANY, _L("Disable"));
        m_toggle->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            advisor_set_enabled(!enabled_setting());
            refresh_status();
        });
        m_close = new wxButton(this, wxID_CLOSE, _L("Close"));
        m_close->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { EndModal(wxID_CLOSE); });
        buttons->Add(m_toggle, 0, wxRIGHT, FromDIP(8));
        buttons->Add(m_close, 0);
        root->Add(buttons, 0, wxEXPAND | wxALL, FromDIP(16));

        SetEscapeId(wxID_CLOSE);
        m_close->SetDefault();
        m_close->SetFocus();
        SetSizerAndFit(root);
        wxGetApp().UpdateDlgDarkUI(this);
        apply_colours();
        refresh_status();
        CentreOnParent();
    }

protected:
    void on_dpi_changed(const wxRect& /*suggested_rect*/) override
    {
        Layout();
        Fit();
        Refresh();
    }

private:
    static bool enabled_setting()
    {
        const AppConfig* config = wxGetApp().app_config;
        return config && config->get_bool("print_advisor_mcp_enabled");
    }

    // Explicit colours after the theme update, as in AdvisorConfirmDialog: the default dialog
    // colours gave grey text on a light background in the dark theme.
    void apply_colours()
    {
        SetBackgroundColour(m_palette.background);
        m_body->SetBackgroundColour(m_palette.background);
        m_brand->SetForegroundColour(advisor_branding::green);
        for (wxStaticText* label : m_labels)
            label->SetForegroundColour(m_palette.foreground);
        for (wxStaticText* label : m_muted)
            label->SetForegroundColour(m_palette.muted);
        m_detail->SetForegroundColour(m_palette.muted);
        m_command_text->SetBackgroundColour(m_palette.dark ? wxColour(0x22, 0x22, 0x26) : wxColour(0xF4, 0xF4, 0xF4));
        m_command_text->SetForegroundColour(m_palette.foreground);
        for (wxButton* button : {m_copy, m_close}) {
            button->SetBackgroundColour(m_palette.neutral_button);
            button->SetForegroundColour(m_palette.foreground);
        }
    }

    // Reads the live state: the setting and whether this window runs the local API.
    void refresh_status()
    {
        const bool enabled = enabled_setting();
        const bool running = advisor_running_server() != nullptr;
        wxString detail;
        if (running) {
            m_status->SetLabel(_L("Active"));
            m_status->SetForegroundColour(advisor_branding::green);
            detail = wxString::Format(_L("Descriptor: %s"), descriptor_path());
        } else if (enabled) {
            m_status->SetLabel(_L("Enabled, but the service runs in another MentorSlicer window"));
            m_status->SetForegroundColour(m_palette.muted);
        } else {
            m_status->SetLabel(_L("Off"));
            m_status->SetForegroundColour(m_palette.muted);
        }
        m_status->Wrap(FromDIP(480));
        m_detail->SetLabel(detail);
        m_detail->SetToolTip(detail);
        m_detail->Show(!detail.empty());
        // The switch follows the setting: "Disable" also clears a setting that is on while
        // another window owns the service.
        m_toggle->SetLabel(enabled ? _L("Disable") : _L("Enable"));
        m_toggle->SetBackgroundColour(enabled ? m_palette.neutral_button : advisor_branding::green);
        m_toggle->SetForegroundColour(enabled ? m_palette.foreground : *wxWHITE);
        Layout();
        Fit();
        Refresh();
    }

    void copy_command()
    {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(m_command));
            wxTheClipboard->Close();
            m_copy->SetLabel(_L("Copied"));
            m_body->Layout();
        }
    }

    advisor_branding::Palette  m_palette;
    wxStaticText*              m_brand = nullptr;
    std::vector<wxStaticText*> m_labels;
    std::vector<wxStaticText*> m_muted;
    wxScrolledWindow*          m_body = nullptr;
    wxTextCtrl*                m_command_text = nullptr;
    wxString                   m_command;
    wxButton*                  m_copy = nullptr;
    wxStaticText*              m_status = nullptr;
    wxStaticText*              m_detail = nullptr;
    wxButton*                  m_toggle = nullptr;
    wxButton*                  m_close = nullptr;
};

} // namespace

void advisor_show_info_dialog()
{
    AdvisorInfoDialog dialog(wxGetApp().mainframe);
    dialog.ShowModal();
}

} // namespace GUI
} // namespace Slic3r
