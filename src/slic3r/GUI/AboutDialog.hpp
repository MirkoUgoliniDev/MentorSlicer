#ifndef slic3r_GUI_AboutDialog_hpp_
#define slic3r_GUI_AboutDialog_hpp_

#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/html/htmlwin.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "slic3r/GUI/ParamsDialog.hpp"
#include "ThirdPartyLibraries.hpp"

#include <atomic>
#include <memory>

namespace Slic3r { 
namespace GUI {

class AboutDialogLogo : public wxPanel
{
public:
    AboutDialogLogo(wxWindow* parent);
    
private:
    ScalableBitmap logo;
    void onRepaint(wxEvent &event);
};



class CopyrightsDialog : public DPIDialog
{
public:
    CopyrightsDialog();
    ~CopyrightsDialog();

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    // Shared with the worker thread of the online check; `cancelled` is set by the
    // destructor, so callbacks queued after the dialog closed do nothing.
    struct CheckState {
        std::atomic<bool> cancelled { false };
    };

    wxHtmlWindow*               m_html;
    wxStaticText*               m_check_label { nullptr };
    wxButton*                   m_check_button { nullptr };
    LibraryCheckCache           m_cache;
    std::shared_ptr<CheckState> m_check_state;

    void onLinkClicked(wxHtmlLinkEvent &event);
    void onCloseDialog(wxEvent &);

    void start_check();
    void on_check_finished(const std::map<std::string, LibraryCheckResult> &results);
    void update_check_label();
    wxString get_html_text();
};



class AboutDialog : public DPIDialog
{
    ScalableBitmap  m_logo_bitmap;
    wxStaticBitmap* m_logo;
    wxStaticText*   m_website_html;
    int             m_copy_version_btn_id { wxID_ANY };
public:
    AboutDialog();
    void setTitle(const wxString& title);

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;
    
private:
    void onLinkClicked(wxHtmlLinkEvent &event);
    void onCloseDialog(wxEvent &);
    void onCopyToClipboard(wxEvent&);
};

} // namespace GUI
} // namespace Slic3r

#endif
