#ifndef slic3r_GUI_KBShortcutsDialog_hpp_
#define slic3r_GUI_KBShortcutsDialog_hpp_

#include <wx/wx.h>
#include <map>
#include <vector>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include <wx/simplebook.h>

namespace Slic3r {
namespace GUI {

class Select
{
public:
    int       m_index;
    wxWindow *m_tab_button;
    wxWindow *m_tab_text;
};
WX_DECLARE_HASH_MAP(int, Select *, wxIntegerHash, wxIntegerEqual, SelectHash);
class SelectableCard;
class KBShortcutsDialog : public DPIDialog
{
    typedef std::pair<std::string, std::string> Shortcut;
    typedef std::vector<Shortcut> Shortcuts;
    typedef std::pair<std::pair<wxString, wxString>, Shortcuts> ShortcutsItem;
    typedef std::vector<ShortcutsItem> ShortcutsVec;

    ShortcutsVec    m_full_shortcuts;
    ScalableBitmap  m_logo_bmp;
    wxStaticBitmap* m_header_bitmap;
    std::vector<wxPanel*> m_pages;

public:
    KBShortcutsDialog();
    wxWindow* create_button(int id, wxString text);
    void          OnSelectTabel(wxCommandEvent &event);
    wxPanel *m_panel_selects;
    wxBoxSizer *m_sizer_right;
    wxSimplebook *m_simplebook;
    wxBoxSizer *  m_sizer_body;
    SelectHash  m_hash_selector;

protected:
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    void fill_shortcuts();
    wxPanel* create_header(wxWindow* parent, const wxFont& bold_font);
    wxPanel* create_page(wxWindow* parent, const ShortcutsItem& shortcuts, const wxFont& font, const wxFont& bold_font);

    // Mouse scheme cards
    wxPanel*        m_mouse_cards_host = nullptr;
    SelectableCard * m_card_scheme_a    = nullptr;
    SelectableCard * m_card_scheme_b    = nullptr; 
    bool            m_is_a_selected    = true;

    void create_mouse_scheme_cards(wxWindow* parent);
    void select_scheme(bool choose_a);

    static wxSizer* make_kv_row(wxWindow* parent, const wxString& key, const wxString& desc);
};

class SelectableCard : public wxPanel
{
public:
    explicit SelectableCard(wxWindow* parent, wxString title);
    void SetSelected(bool sel);
    bool IsSelected() const;
    int  HeaderHeight() const { return FromDIP(32); };
    void SetHover(bool hov) { if (m_hover != hov) { m_hover = hov; Refresh(); } }
protected:
    void OnPaint(wxPaintEvent&);

private:
    bool     m_selected = false;
    bool     m_hover = false;
    wxString m_title;
};

} // namespace GUI
} // namespace Slic3r

#endif
