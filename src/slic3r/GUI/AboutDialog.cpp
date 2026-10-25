#include "AboutDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Color.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/Button.hpp"

#include <string>
#include <thread>
#include <wx/clipbrd.h>
#include <wx/datetime.h>
#include <wx/gdicmn.h>
#include <wx/string.h>
#include "libslic3r/common_header/common_header.h"
#include "MentorSlicerVersion.hpp"
namespace Slic3r {
namespace GUI {

AboutDialogLogo::AboutDialogLogo(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    this->SetBackgroundColour(*wxWHITE);
    this->logo = ScalableBitmap(this, Slic3r::var(Slic3r::CxBuildInfo::getIconName() +"_192px.png"), wxBITMAP_TYPE_PNG);
    this->SetMinSize(this->logo.GetBmpSize());

    this->Bind(wxEVT_PAINT, &AboutDialogLogo::onRepaint, this);
}

void AboutDialogLogo::onRepaint(wxEvent &event)
{
    wxPaintDC dc(this);
    dc.SetBackgroundMode(wxTRANSPARENT);

    wxSize size = this->GetSize();
    int logo_w = this->logo.GetBmpWidth();
    int logo_h = this->logo.GetBmpHeight();
    dc.DrawBitmap(this->logo.bmp(), (size.GetWidth() - logo_w)/2, (size.GetHeight() - logo_h)/2, true);

    event.Skip();
}


// -----------------------------------------
// CopyrightsDialog
// -----------------------------------------
namespace {

enum class LibraryState { Unknown, UpToDate, ToUpdate, Kept, ToCheck, Newer };

struct LibraryStatus
{
    std::string  latest;
    std::string  error;
    LibraryState state = LibraryState::Unknown;
};

LibraryStatus library_status(const ThirdPartyLibrary &lib, const LibraryCheckCache &cache)
{
    LibraryStatus status;
    status.latest = lib.reference_latest;
    if (auto it = cache.results.find(lib.name); it != cache.results.end()) {
        if (!it->second.latest.empty())
            status.latest = it->second.latest;
        status.error = it->second.error;
    }
    if (status.latest.empty() || lib.version.empty())
        return status;
    if (compare_library_versions(lib.version, status.latest) >= 0) {
        status.state = LibraryState::UpToDate;
        return status;
    }
    switch (lib.policy) {
    case LibraryPolicy::Update: status.state = LibraryState::ToUpdate; break;
    case LibraryPolicy::Hold: status.state = LibraryState::Kept; break;
    case LibraryPolicy::Watch: status.state = LibraryState::ToCheck; break;
    case LibraryPolicy::Info: status.state = LibraryState::Newer; break;
    }
    return status;
}

// wxGetTranslation("") returns the catalogue header: never translate an empty string.
wxString translate_text(const std::string &s) { return s.empty() ? wxString() : _(s); }

wxString html_escape(const wxString &s)
{
    wxString out = s;
    out.Replace("&", "&amp;");
    out.Replace("<", "&lt;");
    out.Replace(">", "&gt;");
    return out;
}

} // namespace

CopyrightsDialog::CopyrightsDialog()
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, from_u8((boost::format("%1% - %2%")
        % (wxGetApp().is_editor() ? SLIC3R_APP_FULL_NAME : GCODEVIEWER_APP_NAME)
        % _utf8(L("Third-party libraries"))).str()),
        wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_cache(load_library_check_cache())
{
    this->SetFont(wxGetApp().normal_font());
	this->SetBackgroundColour(*wxWHITE);
    // crealiyprint.ico
    std::string icon_path = (boost::format("%1%/images/%2%.ico") % resources_dir() % Slic3r::CxBuildInfo::getIconName()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

	auto sizer = new wxBoxSizer(wxVERTICAL);

    m_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition,
                              wxSize(40 * em_unit(), 20 * em_unit()), wxHW_SCROLLBAR_AUTO);
    m_html->SetMinSize(wxSize(FromDIP(1050),FromDIP(560)));
    m_html->SetBackgroundColour(*wxWHITE);
    wxFont font = get_default_font(this);
    const int fs = font.GetPointSize();
    const int fs2 = static_cast<int>(1.2f*fs);
    int size[] = { fs, fs, fs, fs, fs2, fs2, fs2 };

    m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
    m_html->SetBorders(2);
    m_html->SetPage(get_html_text());

    sizer->Add(m_html, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 15);
    m_html->Bind(wxEVT_HTML_LINK_CLICKED, &CopyrightsDialog::onLinkClicked, this);

    // Online check of the latest releases, only on request.
    auto check_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_check_label    = new wxStaticText(this, wxID_ANY, wxEmptyString);
    m_check_button   = new wxButton(this, wxID_ANY, _L("Check for updates"));
    m_check_button->SetToolTip(_L("Asks GitHub, GitLab and NuGet for the latest release of each library. "
                                  "Nothing is sent besides the requests; the result is kept for the next time."));
    m_check_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { start_check(); });
    check_sizer->Add(m_check_label, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10));
    check_sizer->Add(m_check_button, 0, wxALIGN_CENTER_VERTICAL);
    sizer->Add(check_sizer, 0, wxEXPAND | wxALL, 15);
    update_check_label();

    SetSizer(sizer);
    sizer->SetSizeHints(this);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

CopyrightsDialog::~CopyrightsDialog()
{
    if (m_check_state)
        m_check_state->cancelled = true;
}

void CopyrightsDialog::start_check()
{
    if (m_check_state)
        return;

    std::vector<std::pair<std::string, LibraryVersionSource>> sources;
    for (const ThirdPartyLibrary &lib : third_party_library_list())
        if (lib.source.kind != LibraryVersionSource::Kind::None)
            sources.emplace_back(lib.name, lib.source);

    auto state    = std::make_shared<CheckState>();
    m_check_state = state;
    m_check_button->Disable();
    m_check_label->SetLabel(wxString::Format(_L("Checking %d of %d..."), 0, int(sources.size())));

    // The worker never touches the dialog: it only queues callbacks on the GUI thread,
    // which do nothing once the dialog has been closed.
    std::thread([this, state, sources]() {
        std::map<std::string, LibraryCheckResult> results;
        bool                                      github_limited = false;
        const int                                 total          = int(sources.size());
        for (int i = 0; i < total; ++i) {
            if (state->cancelled)
                return;
            results[sources[i].first] = check_latest_library_version(sources[i].second, github_limited);
            wxGetApp().CallAfter([this, state, done = i + 1, total]() {
                if (!state->cancelled)
                    m_check_label->SetLabel(wxString::Format(_L("Checking %d of %d..."), done, total));
            });
        }
        wxGetApp().CallAfter([this, state, results]() {
            if (!state->cancelled)
                on_check_finished(results);
        });
    }).detach();
}

void CopyrightsDialog::on_check_finished(const std::map<std::string, LibraryCheckResult> &results)
{
    bool any_success = false;
    for (const auto &[name, result] : results) {
        LibraryCheckResult &cached = m_cache.results[name];
        if (!result.latest.empty()) {
            cached      = result;
            any_success = true;
        } else {
            // Keep the last version found, remember why this check failed.
            cached.error = result.error;
        }
    }
    if (any_success)
        m_cache.checked_at = into_u8(wxDateTime::Now().Format("%Y-%m-%d %H:%M"));
    save_library_check_cache(m_cache);

    m_check_state.reset();
    m_check_button->Enable();
    m_html->SetPage(get_html_text());
    update_check_label();
}

void CopyrightsDialog::update_check_label()
{
    int to_update = 0;
    int failed    = 0;
    for (const ThirdPartyLibrary &lib : third_party_library_list()) {
        const LibraryStatus status = library_status(lib, m_cache);
        if (status.state == LibraryState::ToUpdate)
            ++to_update;
        if (!status.error.empty())
            ++failed;
    }

    wxString text = m_cache.checked_at.empty() ?
        wxString::Format(_L("Latest versions as of %s (reference check)."), THIRD_PARTY_REFERENCE_DATE) :
        wxString::Format(_L("Last online check: %s."), from_u8(m_cache.checked_at));
    text += " ";
    text += to_update == 0 ? _L("No library to update.") : wxString::Format(_L("Libraries to update: %d."), to_update);
    if (failed > 0)
        text += " " + wxString::Format(_L("Checks failed: %d."), failed);
    m_check_label->SetLabel(text);
    m_check_label->Wrap(FromDIP(800));
    Layout();
}

wxString CopyrightsDialog::get_html_text()
{
    wxColour bgr_clr = wxGetApp().get_window_default_clr();//wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);

    const auto text_clr = wxGetApp().get_label_clr_default();// wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    const auto text_clr_str = encode_color(ColorRGB(text_clr.Red(), text_clr.Green(), text_clr.Blue()));
    const auto bgr_clr_str = encode_color(ColorRGB(bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue()));

    const wxString copyright_str = _L("Copyright") + "&copy; ";

    wxString text = wxString::Format(
        "<html>"
            "<body bgcolor= %s link= %s>"
            "<font color=%s>"
                "<font size=\"5\">%s</font><br/>"
                "<font size=\"5\">%s</font>"
                "<a href=\"%s\">%s.</a><br/>"
                "<font size=\"5\">%s.</font><br/>"
                "<br /><br />"
                "<font size=\"5\">%s</font><br/>"
                "<font size=\"5\">%s:</font><br/>"
                "<br />"
                "<font size=\"3\">",
         bgr_clr_str, text_clr_str, text_clr_str,
        _L("License"),
        _L("MentorSlicer is licensed under "),
        "https://www.gnu.org/licenses/agpl-3.0.html",_L("GNU Affero General Public License, version 3"),
        _L("MentorSlicer is based on Creality Print, OrcaSlicer, BambuStudio and PrusaSlicer"),
        _L("Libraries"),
        _L("This software uses open source components whose copyright and other proprietary rights belong to their respective owners. Versions refer to the Windows build"));

    const wxString dash = wxString::FromUTF8("\xE2\x80\x94");
    text += wxString::Format(
        "<table width=\"100%%\" cellpadding=\"4\" cellspacing=\"0\">"
        "<tr><td width=\"17%%\"><b>%s</b></td><td width=\"33%%\"><b>%s</b></td><td width=\"9%%\"><b>%s</b></td>"
        "<td width=\"9%%\"><b>%s</b></td><td width=\"32%%\"><b>%s</b></td></tr>",
        _L("Library"), _L("Used for"), _L("In use"), _L("Latest"), _L("Status"));

    for (const ThirdPartyLibrary &lib : third_party_library_list()) {
        const LibraryStatus status = library_status(lib, m_cache);
        const wxString      reason = translate_text(lib.reason);

        wxString state;
        switch (status.state) {
        case LibraryState::UpToDate: state = "<font color=\"#3BA55C\">" + _L("Up to date") + "</font>"; break;
        case LibraryState::ToUpdate: state = "<font color=\"#E8912D\"><b>" + _L("Update recommended") + "</b></font>"; break;
        case LibraryState::Kept: state = _L("Newer version available, kept on purpose."); break;
        case LibraryState::ToCheck: state = "<font color=\"#4A90D9\">" + _L("Newer version available, to re-check.") + "</font>"; break;
        case LibraryState::Newer: state = _L("Newer version available."); break;
        case LibraryState::Unknown:
            state = lib.source.kind == LibraryVersionSource::Kind::None && !reason.empty() ? _L("Not checked online.") : dash;
            break;
        }
        if (!reason.empty() && status.state != LibraryState::UpToDate)
            state += "<br/><font color=\"#888888\">" + html_escape(reason) + "</font>";
        if (!status.error.empty())
            state += "<br/><font color=\"#888888\">" + _L("Last check failed:") + " " + html_escape(translate_text(status.error)) + "</font>";

        text += wxString::Format(
            "<tr><td valign=\"top\"><a href=\"%s\">%s</a></td><td valign=\"top\">%s</td><td valign=\"top\">%s</td>"
            "<td valign=\"top\">%s</td><td valign=\"top\">%s</td></tr>",
            from_u8(lib.link), html_escape(from_u8(lib.name)), html_escape(translate_text(lib.purpose)),
            lib.version.empty() ? dash : from_u8(lib.version), status.latest.empty() ? dash : from_u8(status.latest), state);
    }

    text += wxString(
                "</table>"
                "</font>"
            "</font>"
            "</body>"
        "</html>");

    return text;
}

void CopyrightsDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    const wxFont& font = GetFont();
    const int fs = font.GetPointSize();
    const int fs2 = static_cast<int>(1.2f*fs);
    int font_size[] = { fs, fs, fs, fs, fs2, fs2, fs2 };

    m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), font_size);

    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CLOSE });

    const wxSize& size = wxSize(40 * em, 20 * em);

    m_html->SetMinSize(size);
    m_html->Refresh();

    SetMinSize(size);
    Fit();

    Refresh();
}

void CopyrightsDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref());
    event.Skip(false);
}

void CopyrightsDialog::onCloseDialog(wxEvent &)
{
     this->EndModal(wxID_CLOSE);
}

AboutDialog::AboutDialog()
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe),wxID_ANY,wxString::Format(_L("About %s"), SLIC3R_APP_NAME),wxDefaultPosition,
        wxDefaultSize, /*wxCAPTION*/wxDEFAULT_DIALOG_STYLE)
{
    SetFont(wxGetApp().normal_font());
    const wxColour font_bg = wxGetApp().dark_mode() ? wxColour("#4B4B4D") : wxColour("#FFFFFF");
    SetBackgroundColour(font_bg);

    std::string icon_path = (boost::format("%1%/images/%2%.ico") % resources_dir() % Slic3r::CxBuildInfo::getIconName()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    // wxPanel *m_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(560), FromDIP(237)), wxTAB_TRAVERSAL);
    wxPanel *m_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(800), -1), wxTAB_TRAVERSAL);
    m_panel->SetBackgroundColour(font_bg);

    wxBoxSizer *panel_versizer = new wxBoxSizer(wxVERTICAL);

    m_panel->SetSizer(panel_versizer);

    wxBoxSizer *ver_sizer = new wxBoxSizer(wxVERTICAL);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->Add(m_panel, 0, wxEXPAND | wxALL, 0);
    main_sizer->Add(ver_sizer, 0, wxEXPAND | wxALL, 0);

    //const wxColour font_text_clr = wxGetApp().dark_mode() ?  wxColour("#FFFFFF") : wxColour("#000000");

    // logo and creality_name text
    m_logo_bitmap = ScalableBitmap(m_panel, Slic3r::CxBuildInfo::getIconName(), 32);
    m_logo = new wxStaticBitmap(m_panel, wxID_ANY, m_logo_bitmap.bmp(), wxDefaultPosition,wxDefaultSize, 0);
    // m_logo->SetSizer(vesizer);

    // vesizer->Add(0, FromDIP(80), 1, wxEXPAND, FromDIP(5));
    std::string   name_text     = SLIC3R_APP_FULL_NAME;
    wxStaticText* creality_name = new wxStaticText(m_panel, wxID_ANY, name_text.c_str(), wxDefaultPosition, wxDefaultSize);
    creality_name->SetFont(Label::Body_14);
    creality_name->SetBackgroundColour(font_bg);
    // vesizer->Add(creality_name, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, FromDIP(5));

    wxBoxSizer* top_sizer = new wxBoxSizer(wxHORIZONTAL);
    // Add horizontal spacer to move m_logo to the right
    top_sizer->AddSpacer(FromDIP(30));
    top_sizer->Add(m_logo, 0, wxALL | wxALIGN_LEFT, FromDIP(5));
    top_sizer->Add(creality_name, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));

    // Add vertical spacer to move down m_logo a little bit
    panel_versizer->AddSpacer(FromDIP(18));
    panel_versizer->Add(top_sizer, 0, wxALL | wxEXPAND, 0);

    panel_versizer->AddSpacer(FromDIP(10));

    // version
    {
        wxString version_string = wxString::FromUTF8(SLIC3R_APP_NAME) + " V" + MENTORSLICER_VERSION;
        wxStaticText* version_text = new wxStaticText(m_panel, wxID_ANY, version_string, wxDefaultPosition, wxDefaultSize);
        version_text->SetFont(Label::Body_12);
        version_text->SetBackgroundColour(font_bg);
        panel_versizer->Add(version_text, 0, wxLEFT, FromDIP(10));
    }

    wxBoxSizer *text_sizer_horiz = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *text_sizer = new wxBoxSizer(wxVERTICAL);
    text_sizer_horiz->Add( 0, 0, 0, wxLEFT, FromDIP(0));

    // Derivation chain of the fork (kept for attribution, as the AGPL requires).
    const wxString pre_empty_space = "    ";
    const std::vector<wxString> text_list = {
        wxString::Format(_L("MentorSlicer is based on Creality Print %s by Creality."), CREALITYPRINT_VERSION),
        _L("Creality Print is based on OrcaSlicer and on Ultimaker's CuraEngine."),
        _L("OrcaSlicer is based on BambuStudio, PrusaSlicer and SuperSlicer."),
        _L("BambuStudio is based on PrusaSlicer by Prusa Research and SuperSlicer by Merill."),
        _L("PrusaSlicer was originally based on Slic3r by Alessandro Ranellucci and the RepRap community."),
    };

    text_sizer->Add( 0, 0, 0, wxTOP, FromDIP(20));
    for (const wxString& line : text_list) {
        auto staticText = new wxStaticText(this, wxID_ANY, pre_empty_space + line, wxDefaultPosition, wxSize(FromDIP(650), -1), wxALIGN_LEFT);
        staticText->SetBackgroundColour(font_bg);
        staticText->SetMinSize(wxSize(FromDIP(640), -1));
        staticText->SetFont(Label::Body_12);
        staticText->Wrap(FromDIP(580));
        text_sizer->Add(staticText, 0, wxUP | wxDOWN, FromDIP(3));
    }

    // Author and repository link.
    wxBoxSizer *info_sizer = new wxBoxSizer(wxHORIZONTAL);
    info_sizer->AddSpacer(FromDIP(15));
    wxStaticText *author_text = new wxStaticText(this, wxID_ANY, _L("Author:") + " Mirko Ugolini   ", wxDefaultPosition, wxDefaultSize);
    author_text->SetBackgroundColour(font_bg);
    info_sizer->Add(author_text, 0, wxALL | wxALIGN_LEFT, 0);
    m_website_html = new wxStaticText(this, wxID_ANY, "");
    {
        const bool     is_dark     = Slic3r::GUI::wxGetApp().dark_mode();
        const wxColour color       = is_dark ? wxColor("#4287FF") : wxColour("#387DFF");
        const wxString website_url = "https://github.com/MirkoUgoliniDev/MentorSlicer";
        m_website_html->SetForegroundColour(color);
        m_website_html->SetLabel(website_url);
        m_website_html->SetCursor(wxCURSOR_HAND);
        m_website_html->Bind(wxEVT_LEFT_DOWN, [website_url](wxMouseEvent&) {
            wxGetApp().open_browser_with_warning_dialog(website_url);
        });
        info_sizer->Add(m_website_html, 1, wxALL | wxALIGN_LEFT, 0);
    }

    text_sizer_horiz->Add(text_sizer, 1, wxALL,0);
    ver_sizer->Add(text_sizer_horiz, 0, wxALL,0);
    ver_sizer->Add( 0, 0, 0, wxTOP, FromDIP(25));

    ver_sizer->Add(info_sizer, 0, wxALL | wxEXPAND, FromDIP(0));

    ver_sizer->Add( 0, 0, 0, wxTOP, FromDIP(30));

    wxGetApp().UpdateDlgDarkUI(this);
	SetSizer(main_sizer);
    Layout();
    Fit();
    CenterOnParent();
}

void AboutDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_logo_bitmap.msw_rescale();
    m_logo->SetBitmap(m_logo_bitmap.bmp());

    const wxFont& font = GetFont();
    const int fs = font.GetPointSize() - 1;
    int font_size[] = { fs, fs, fs, fs, fs, fs, fs };
    //m_website_html->SetFonts(font.GetFaceName(), font.GetFaceName(), font_size);

    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CLOSE });

    m_website_html->SetMinSize(wxSize(-1, 16 * em));
    m_website_html->Refresh();

    const wxSize& size = wxSize(65 * em, 30 * em);

    SetMinSize(size);
    Fit();
    Refresh();
}

void AboutDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref());
    event.Skip(false);
}

void AboutDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
}

void AboutDialog::onCopyToClipboard(wxEvent&)
{
    wxTheClipboard->Open();
    wxTheClipboard->SetData(new wxTextDataObject(_L("Version") + " " + MENTORSLICER_VERSION + " (Creality Print " +
                                                 GUI_App::format_display_version() + ")"));
    wxTheClipboard->Close();
}

} // namespace GUI
} // namespace Slic3r
