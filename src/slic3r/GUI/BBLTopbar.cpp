#include "BBLTopbar.hpp"

#ifdef __WXGTK__
#include <gtk/gtk.h>
#endif
#include "wx/artprov.h"
#include "wx/aui/framemanager.h"
#include "wx/display.h"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "GUI.hpp"
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "WebViewDialog.hpp"
#include "PartPlate.hpp"

#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <wx/dcgraph.h>
#include <wx/utils.h>
#include "Notebook.hpp"
#include "libslic3r/common_header/common_header.h"
#define TOPBAR_ICON_SIZE  17

#if defined(__WIN32__) || defined(__WXGTK__)
#define BBL_TOPBAR_HAS_WINDOW_BUTTONS 1
#endif

// The project name floats in the free gap between the mode tabs and right-side actions.
#define TOPBAR_TITLE_MIN_VISIBLE_WIDTH 16
#define TOPBAR_TITLE_SIDE_PADDING 8

static long HOME_BTN_CODE_CHECKED = 12124;
static long HOME_BTN_CODE_UNCHECKED = 12125;

using namespace Slic3r;

#ifdef __WXGTK__
static bool topbar_is_wayland_session()
{
    const char* gdk_backend     = ::getenv("GDK_BACKEND");
    const char* wayland_display = ::getenv("WAYLAND_DISPLAY");
    const char* session_type    = ::getenv("XDG_SESSION_TYPE");
    return (gdk_backend != nullptr && ::strstr(gdk_backend, "wayland") != nullptr) ||
        ((gdk_backend == nullptr || *gdk_backend == '\0') &&
         ((wayland_display && *wayland_display) ||
          (session_type && ::strcmp(session_type, "wayland") == 0)));
}
#endif

#if defined(__WIN32__)
static bool topbar_should_draw_window_buttons() { return true; }
#elif defined(__WXGTK__)
static bool topbar_should_draw_window_buttons() { return topbar_is_wayland_session(); }
#endif

#ifdef __WXGTK__
static bool begin_wayland_frame_move(wxFrame* frame, wxMouseEvent& event)
{
    if (!topbar_is_wayland_session() || frame == nullptr || frame->GetHandle() == nullptr)
        return false;

    GtkWidget* widget = GTK_WIDGET(frame->GetHandle());
    GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
    if (!GTK_IS_WINDOW(toplevel))
        return false;

    const wxPoint mouse_pos = ::wxGetMousePosition();
    gtk_window_begin_move_drag(GTK_WINDOW(toplevel), 1, mouse_pos.x, mouse_pos.y,
                               static_cast<guint32>(event.GetTimestamp()));
    return true;
}
#endif

class ButtonsCtrl : public wxControl
{
public:
    // BBS
    ButtonsCtrl(wxWindow* parent, wxBoxSizer* side_tools = NULL);
    ~ButtonsCtrl() {}

    void SetSelection(int sel);
    void SuppressHoverUntilReenter(int index);
    int GetSelection();
    int GetButtonsRight() const;
    bool InsertPage(size_t n, const wxString& text, bool bSelect = false, const std::string& bmp_name = "", const std::string& inactive_bmp_name = "");
    void RefreshColor();
    void reLayout(bool compact = false);
private:
    void ApplyButtonStyle(int index, bool selected);
    StateColor DefaultTextColor(bool selected) const;

    wxBoxSizer*      m_sizer;
    std::map<int,Button*> m_mapPageButtons;
    int                  m_selection{-1};
    int                  m_btn_margin;
    int                  m_line_margin;
    std::set<int>        m_suppressed_hover;
    // ModeSizer*                      m_mode_sizer {nullptr};
};

wxDECLARE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, wxCommandEvent);

ButtonsCtrl::ButtonsCtrl(wxWindow* parent, wxBoxSizer* side_tools)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxTAB_TRAVERSAL)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    wxColour default_btn_bg;
#ifdef __APPLE__
    default_btn_bg = Slic3r::GUI::wxGetApp().dark_mode() ? wxColour("#010101") : wxColour(214, 214, 220); // Gradient #414B4E
#else
    default_btn_bg = Slic3r::GUI::wxGetApp().dark_mode() ? wxColour("#010101") : wxColour(214, 214, 220); // Gradient #414B4E

#endif

    SetBackgroundColour(default_btn_bg);

    int em = em_unit(this); // Slic3r::GUI::wxGetApp().em_unit();
    // BBS: no gap
    m_btn_margin  = FromDIP(5); // std::lround(0.3 * em);
    m_line_margin = FromDIP(1);

    m_sizer = new wxBoxSizer(wxHORIZONTAL);
    this->SetSizer(m_sizer);

    if (side_tools != NULL) {
        m_sizer->AddStretchSpacer(1);
        for (size_t idx = 0; idx < side_tools->GetItemCount(); idx++) {
            wxSizerItem* item     = side_tools->GetItem(idx);
            wxWindow*    item_win = item->GetWindow();
            if (item_win) {
                item_win->Reparent(this);
            }
        }
        m_sizer->Add(side_tools, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT | wxBOTTOM, m_btn_margin);
    }

    // BBS: disable custom paint
    // this->Bind(wxEVT_PAINT, &ButtonsCtrl::OnPaint, this);
    Bind(wxEVT_SYS_COLOUR_CHANGED, [this](auto& e) {});
}
int  ButtonsCtrl::GetSelection() { return m_selection; }
int ButtonsCtrl::GetButtonsRight() const
{
    int right = 0;
    for (const auto& entry : m_mapPageButtons) {
        const Button* button = entry.second;
        if (button && button->IsShown())
            right = std::max(right, button->GetRect().GetRight());
    }
    return right;
}
StateColor ButtonsCtrl::DefaultTextColor(bool selected) const
{
    const bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    const wxColour active = is_dark ? wxColour(254, 254, 254) : wxColour(255, 255, 255);
    const wxColour normal = selected ? active : (is_dark ? wxColour(254, 254, 254) : wxColour(0, 0, 0));
    return StateColor(
        std::pair{active, (int) StateColor::Pressed},
        std::pair{active, (int) StateColor::Hovered},
        std::pair{active, (int) StateColor::Focused},
        std::pair{active, (int) StateColor::Checked},
        std::pair{normal, (int) StateColor::Normal});
}

void ButtonsCtrl::ApplyButtonStyle(int index, bool selected)
{
    auto it = m_mapPageButtons.find(index);
    if (it == m_mapPageButtons.end())
        return;

    Button* button = it->second;
    if (!button)
        return;

    button->SetTextColor(DefaultTextColor(selected));
}

void ButtonsCtrl::SetSelection(int sel)
{
    if (m_selection == sel)
        return;
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    if(-1!=sel && m_mapPageButtons.end() == m_mapPageButtons.find(sel))//not found
    {
        if (m_selection >= 0) {
            wxColour   hover_bg = is_dark ? wxColour(76, 213, 130) : wxColour(68, 205, 122);
            StateColor bg_color = StateColor(std::pair{hover_bg, (int) StateColor::Hovered},
                                             std::pair{is_dark ? wxColour(1, 1, 1) : wxColour(214, 214, 220), (int) StateColor::Normal});
            m_mapPageButtons[m_selection]->SetBackgroundColor(bg_color);
            m_mapPageButtons[m_selection]->SetSelected(false);
            ApplyButtonStyle(m_selection, false);
        }

        m_selection = -1;
        return;
    }

    if (-1 == sel) 
    {
        if (m_selection >= 0) {
            wxColour   hover_bg = is_dark ? wxColour(76, 213, 130) : wxColour(68, 205, 122);
            StateColor bg_color = StateColor(std::pair{hover_bg, (int) StateColor::Hovered},
                                             std::pair{is_dark ? wxColour(1, 1, 1) : wxColour(214, 214, 220), (int) StateColor::Normal});
            m_mapPageButtons[m_selection]->SetBackgroundColor(bg_color);
            m_mapPageButtons[m_selection]->SetSelected(false);
            ApplyButtonStyle(m_selection, false);
        }

        m_selection = sel;
        return;
    }

    // BBS: change button color
    wxColour selected_btn_bg("#009688"); // Gradient #009688
    if (m_selection >= 0) {
        wxColour   hover_bg = is_dark ? wxColour(76, 213, 130) : wxColour(68, 205, 122);
        StateColor bg_color = StateColor(std::pair{hover_bg, (int) StateColor::Hovered},
                                         std::pair{is_dark ? wxColour(1, 1, 1) : wxColour(214, 214, 220), (int) StateColor::Normal});
        m_mapPageButtons[m_selection]->SetBackgroundColor(bg_color);
        m_mapPageButtons[m_selection]->SetSelected(false);
        ApplyButtonStyle(m_selection, false);
        
    }
    m_selection = sel;


    m_mapPageButtons[m_selection]->SetSelected(true);
    ApplyButtonStyle(m_selection, true);

    StateColor bg_color = StateColor(std::pair{ wxColour(68, 205, 122), (int)StateColor::Hovered },
                                     std::pair{is_dark ? wxColour(31, 202, 99) : wxColour(21, 192, 89), (int) StateColor::Normal});
    m_mapPageButtons[m_selection]->SetBackgroundColor(bg_color);
    m_mapPageButtons[m_selection]->SetFocus();

    Refresh();
}
void ButtonsCtrl::SuppressHoverUntilReenter(int index)
{
    auto it = m_mapPageButtons.find(index);
    if (it == m_mapPageButtons.end())
        return;

    Button* button = it->second;
    if (button->HasCapture())
        button->ReleaseMouse();

    m_suppressed_hover.insert(index);
    const wxColour normal_bg = Slic3r::GUI::wxGetApp().dark_mode()
        ? wxColour(1, 1, 1)
        : wxColour(214, 214, 220);
    button->SetBackgroundColor(StateColor(std::pair{normal_bg, (int) StateColor::Hovered},
                                          std::pair{normal_bg, (int) StateColor::Normal}));
    button->Refresh();
    button->Update();
}

void ButtonsCtrl::RefreshColor()
{
	//for (auto& it : m_mapPageButtons)
	//{
	//	Slic3r::GUI::wxGetApp().UpdateDarkUI(it.second);
	//}
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour default_btn_bg = Slic3r::GUI::wxGetApp().dark_mode() ? wxColour("#010101") : wxColour(214, 214, 220); // Gradient #414B4E
    SetBackgroundColour(default_btn_bg);
    for (auto& [index, button] : m_mapPageButtons) {
        wxColour   hover_bg = is_dark ? wxColour(76, 213, 130) : wxColour(68, 205, 122);
        StateColor bg_color = StateColor(std::pair{hover_bg, (int) StateColor::Hovered},
                                         std::pair{is_dark ? wxColour(1, 1, 1) : wxColour(214, 214, 220), (int) StateColor::Normal});
        button->SetCornerRadius(FromDIP(3));
        button->SetFontBold(true);
        button->SetBackgroundColor(bg_color);     
        button->SetBackgroundColour(default_btn_bg);
        button->SetTextColor(DefaultTextColor(false));
        if (m_selection == index)
        {
            button->SetSelected(true);
            bg_color = StateColor(std::pair{wxColour(68, 205, 122), (int) StateColor::Hovered},
                std::pair{is_dark ? wxColour(31, 202, 99) : wxColour(21, 192, 89), (int) StateColor::Normal});
            button->SetBackgroundColor(bg_color);
            button->SetTextColor(DefaultTextColor(true));
        }
        ApplyButtonStyle(index, m_selection == index);
        button->Refresh();
        button->Update();
        //Slic3r::GUI::wxGetApp().UpdateDarkUI(button);
    }
    Refresh();
}
void ButtonsCtrl::reLayout(bool compact)
{
    // Recompute DIP-aware metrics so buttons adapt to new DPI.
    m_btn_margin  = FromDIP(compact ? 3 : 5);
    m_line_margin = FromDIP(1);

    // Update sizer item borders to the new margin.
    for (unsigned int idx = 0; idx < m_sizer->GetItemCount(); ++idx) {
        if (auto* item = m_sizer->GetItem(idx)) {
            item->SetBorder(m_btn_margin);
        }
    }

    for (auto& it : m_mapPageButtons)
    {
        Button* btn = it.second;
        if (!btn) continue;

        // Reset DIP-based visuals.
        btn->SetCornerRadius(FromDIP(3));

        // Measure the bold font actually used for painting, including translated labels.
        // Button adds its icon and padding to this minimum during SetMinSize().
        btn->SetFont(Label::Body_14.Bold());
        btn->SetPaddingSize(wxSize(FromDIP(14), FromDIP(5)));
        const bool has_text = !btn->GetLabel().IsEmpty();
        btn->SetMinSize(wxSize(has_text ? (compact ? 0 : FromDIP(100)) : FromDIP(40), FromDIP(30)));
        btn->Rescale();
    }

    const wxSize content_size = m_sizer->GetMinSize();
    SetMinSize(wxDefaultSize);
    SetSize(content_size);
    SetMinSize(content_size);
    m_sizer->SetDimension(0, 0, content_size.x, content_size.y);
    InvalidateBestSize();
    Refresh();
}
bool ButtonsCtrl::InsertPage(
    size_t index, const wxString& text, bool bSelect /* = false*/, const std::string& bmp_name /* = ""*/, const std::string& inactive_bmp_name)
{
    Button* btn = new Button(this, text.empty() ? text : " " + text, bmp_name, wxNO_BORDER, 0, index);
    btn->SetCornerRadius(FromDIP(3));
    btn->SetFontBold(true);

    int em = em_unit(this);
    // BBS set size for button
    btn->SetMinSize({(text.empty() ? FromDIP(40) : FromDIP(100)), FromDIP(30)});
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour   hover_bg = is_dark ? wxColour(76, 213, 130) : wxColour(68, 205, 122);
    StateColor bg_color = StateColor(std::pair{hover_bg, (int) StateColor::Hovered},
                                     std::pair{is_dark ? wxColour("1, 1, 1") : wxColour(214, 214, 220), (int) StateColor::Normal});

    btn->SetBackgroundColor(bg_color);
    btn->SetTextColor(DefaultTextColor(false));
    //btn->SetInactiveIcon(inactive_bmp_name);
    auto activate_page = [this, btn]() {
        int id = btn->GetId();

        wxCommandEvent evt = wxCommandEvent(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED);
        evt.SetId(id);
        wxPostEvent(this->GetParent(), evt);
    };
    if (index == MainFrame::tpPreview || index == MainFrame::tp3DEditor) {
        // Both pages can open modal dialogs. Button releases mouse capture
        // before wxEVT_BUTTON; opening on mouse-down can retain capture and
        // route dialog clicks back to the tab button on macOS.
        btn->Bind(wxEVT_BUTTON, [activate_page](wxCommandEvent&) { activate_page(); });
    } else {
        // Other pages keep activating on press.
        btn->Bind(wxEVT_LEFT_DOWN, [activate_page](wxMouseEvent& event) {
            activate_page();
            event.Skip();
        });
        // Consume the release event to avoid a second switch.
        btn->Bind(wxEVT_BUTTON, [](wxCommandEvent&) {});
    }
    btn->Bind(wxEVT_ENTER_WINDOW, [this, index, btn](wxMouseEvent& event) {
        if (m_suppressed_hover.erase(static_cast<int>(index)) > 0) {
            const bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
            const bool selected = m_selection == static_cast<int>(index);
            const wxColour normal_bg = selected
                ? (is_dark ? wxColour(31, 202, 99) : wxColour(21, 192, 89))
                : (is_dark ? wxColour(1, 1, 1) : wxColour(214, 214, 220));
            const wxColour hover_bg = selected
                ? wxColour(68, 205, 122)
                : (is_dark ? wxColour(76, 213, 130) : wxColour(68, 205, 122));
            btn->SetBackgroundColor(StateColor(std::pair{hover_bg, (int) StateColor::Hovered},
                                                std::pair{normal_bg, (int) StateColor::Normal}));
            btn->Refresh();
        }
        event.Skip();
    });
    Slic3r::GUI::wxGetApp().UpdateDarkUI(btn);
    m_mapPageButtons[index] = btn;
    ApplyButtonStyle(index, false);

    m_sizer->Add(btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT , m_btn_margin);

    m_sizer->Layout();
  
    if (bSelect)
    {
        this->SetSelection(index);
    }

    return true;
}




enum CUSTOM_ID
{
    ID_TOP_MENU_TOOL = 3100,
    ID_LOGO,
    ID_TOP_FILE_MENU,
    ID_TOP_EDIT_MENU,
    ID_TOP_VIEW_MENU,
    ID_TOP_CALIB_MENU,
    ID_TOP_AI_MENU,
    ID_TOP_HELP_MENU,
ID_TITLE,
    ID_CALIB,
    ID_PREFERENCES,
    ID_CONFIG_RELATE,
    ID_TOOL_BAR = 3200,
    ID_MINBTN
};

class BBLTopbarArt : public wxAuiDefaultToolBarArt
{
public:
    enum BTNSTYPE {
        NORMAL,
        CHECKED,
    };

public:
    virtual void DrawLabel(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect) wxOVERRIDE;
    virtual void DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect) wxOVERRIDE;
    virtual void DrawButton(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect) wxOVERRIDE;
    virtual void DrawSeparator(wxDC& dc, wxWindow* wnd, const wxRect& _rect) wxOVERRIDE;
};

void BBLTopbarArt::DrawLabel(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect)
{
    dc.SetFont(m_font);
#ifdef __WINDOWS__
    dc.SetTextForeground(Slic3r::GUI::wxGetApp().dark_mode() ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT) : wxColor(0,0,0));
#elif __linux__
    dc.SetTextForeground(Slic3r::GUI::wxGetApp().dark_mode() ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT) : wxColor(0,0,0));
#else
    dc.SetTextForeground(*wxWHITE);
#endif

    int textWidth = 0, textHeight = 0;
    dc.GetTextExtent(item.GetLabel(), &textWidth, &textHeight);

    wxRect clipRect = rect;
    clipRect.width -= wnd->FromDIP(1);
    dc.SetClippingRegion(clipRect);

    int textX, textY;
    if (textWidth < rect.GetWidth()) {
        textX = rect.x + wnd->FromDIP(1) + (rect.width - textWidth) / 2;
    }
    else {
        textX = rect.x + wnd->FromDIP(1);
    }
    textY = rect.y + (rect.height - textHeight) / 2;
    dc.DrawText(item.GetLabel(), textX, textY);
    dc.DestroyClippingRegion();
}

void BBLTopbarArt::DrawBackground(wxDC& dc, wxWindow* wnd, const wxRect& rect)
{
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    dc.SetBrush(wxBrush(is_dark ? wxColour(1, 1, 1) : wxColour(214, 214, 220)));
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetClippingRegion(rect);
    dc.DrawRectangle(rect);
    dc.DestroyClippingRegion();
}

void BBLTopbarArt::DrawButton(wxDC& dc, wxWindow* wnd, const wxAuiToolBarItem& item, const wxRect& rect)
{
    int textWidth = 0, textHeight = 0;

    if (m_flags & wxAUI_TB_TEXT)
    {
        dc.SetFont(m_font);
        int tx, ty;

        dc.GetTextExtent(wxT("ABCDHgj"), &tx, &textHeight);
        textWidth = 0;
        dc.GetTextExtent(item.GetLabel(), &textWidth, &ty);
    }

    int bmpX = 0, bmpY = 0;
    int textX = 0, textY = 0;

    wxBitmap bmp;
    if (HOME_BTN_CODE_CHECKED == item.GetUserData()) {
        bmp = item.GetState() == wxAUI_BUTTON_STATE_NORMAL
                  ? item.GetBitmapFor(wnd)
                  : item.GetHoverBitmapBundle().GetBitmapFor(wnd);
    } 
    else {
        // Resolve the bitmap against the window currently being painted. With
        // wxWidgets 3.3, the legacy getters may use a stale DPI context and
        // return an invalid bitmap while a toolbar item is pressed.
        bmp = item.GetCurrentBitmapFor(wnd);
    }
   
    const wxSize bmpSize = bmp.IsOk() ? bmp.GetScaledSize() : wxSize(0, 0);

    if (m_textOrientation == wxAUI_TBTOOL_TEXT_BOTTOM)
    {
        bmpX = rect.x +
            (rect.width / 2) -
            (bmpSize.x / 2);

        bmpY = rect.y +
            ((rect.height - textHeight) / 2) -
            (bmpSize.y / 2);

        textX = rect.x + (rect.width / 2) - (textWidth / 2) + wnd->FromDIP(1);
        textY = rect.y + rect.height - textHeight - wnd->FromDIP(1);
    }
    else if (m_textOrientation == wxAUI_TBTOOL_TEXT_RIGHT)
    {
        bmpX = rect.x + wnd->FromDIP(3);

        bmpY = rect.y +
            (rect.height / 2) -
            (bmpSize.y / 2);

        textX = bmpX + wnd->FromDIP(3) + bmpSize.x;
        textY = rect.y +
            (rect.height / 2) -
            (textHeight / 2);
    }

    if (!(item.GetState() & wxAUI_BUTTON_STATE_DISABLED))
    {
        if (item.GetState() & wxAUI_BUTTON_STATE_PRESSED)
        {
            dc.SetPen(wxPen(StateColor::darkModeColorFor("#15BF59"))); // ORCA
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRoundedRectangle(rect, wnd->FromDIP(5));
        }
        else if ((item.GetState() & wxAUI_BUTTON_STATE_HOVER) || item.IsSticky())
        {
            dc.SetPen(wxPen(StateColor::darkModeColorFor("#15BF59"))); // ORCA
            //dc.SetBrush(wxBrush(StateColor::darkModeColorFor("#009688"))); // ORCA

            // draw an even lighter background for checked item hovers (since
            // the hover background is the same color as the check background)
            if (item.GetState() & wxAUI_BUTTON_STATE_CHECKED)
                dc.SetBrush(wxBrush(StateColor::darkModeColorFor("#009688"))); // ORCA

            // dc.DrawRoundedRectangle(rect, 3);
            wxGraphicsContext* gc = wxGraphicsContext::CreateFromUnknownDC(dc);
            if(gc)
            {
                // Create a path for the rectangle
                wxGraphicsPath path = gc->CreatePath();
                // path.AddRectangle(rect.x, rect.y, rect.width, rect.height);
                path.AddRoundedRectangle(rect.x, rect.y, rect.width, rect.height, wnd->FromDIP(2));

                // gc->SetBrush(*wxGREEN_BRUSH);
                gc->SetPen(*wxGREEN_PEN);
                gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
                // gc->DrawRoundedRectangle(rect.x, rect.y, rect.width, rect.height, 3);

                // Draw the border
                gc->StrokePath(path);

                // Destroy the graphics context to free resources
                delete gc;
            }
            else
            {
                dc.DrawRoundedRectangle(rect, wnd->FromDIP(5));
            }
        }
        else if (item.GetState() & wxAUI_BUTTON_STATE_CHECKED)
        {
            if (HOME_BTN_CODE_CHECKED == item.GetUserData()) {
                dc.SetPen(wxPen(StateColor::darkModeColorFor("#15BF59")));     // ORCA
                dc.SetBrush(wxBrush(StateColor::darkModeColorFor("#15BF59"))); // ORCA
                dc.DrawRoundedRectangle(rect, wnd->FromDIP(5));
            } else {
                // it's important to put this code in an else statement after the
                // hover, otherwise hovers won't draw properly for checked items
                dc.SetPen(wxPen(StateColor::darkModeColorFor("#009688"))); // ORCA
                // dc.SetBrush(wxBrush(StateColor::darkModeColorFor("#009688"))); // ORCA
                dc.DrawRectangle(rect);
            }
        }
    }

    if (bmp.IsOk())
        dc.DrawBitmap(bmp, bmpX, bmpY, true);

    // set the item's text color based on if it is disabled
#ifdef __WINDOWS__
    dc.SetTextForeground(Slic3r::GUI::wxGetApp().dark_mode() ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT) : wxColor(0,0,0));
#elif __linux__
    dc.SetTextForeground(Slic3r::GUI::wxGetApp().dark_mode() ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT) : wxColor(0,0,0));
#else
    dc.SetTextForeground(*wxWHITE);
#endif
    if (item.GetState() & wxAUI_BUTTON_STATE_DISABLED)
    {
        dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
    }

    if ((m_flags & wxAUI_TB_TEXT) && !item.GetLabel().empty())
    {
        dc.DrawText(item.GetLabel(), textX, textY);
    }
}

void BBLTopbarArt::DrawSeparator(wxDC& dc, wxWindow* wnd, const wxRect& _rect)
{
    bool horizontal = true;
    if (m_flags & wxAUI_TB_VERTICAL)
        horizontal = false;

    wxRect rect = _rect;
    rect.height = wnd->FromDIP(30);

    if (horizontal) {
        rect.x += (rect.width / 2);
        rect.width     = wnd->FromDIP(1);
        int new_height = (rect.height * 3) / 4;
        rect.y += wnd->FromDIP(3);
        //((_rect.height - rect.height));
        rect.height = new_height;
    } else {
        rect.y += (rect.height / 2);
        rect.height   = wnd->FromDIP(1);
        int new_width = (rect.width * 3) / 4;
        rect.x += (rect.width / 2) - (new_width / 2);
        rect.width = new_width;
    }

     bool     is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    wxColour startColour = is_dark ? ("#454548") : ("#AAAAB0");
    wxColour endColour   = is_dark ? ("#454548") : ("#AAAAB0");
    dc.GradientFillLinear(rect, startColour, endColour, horizontal ? wxSOUTH : wxEAST);
}

BBLTopbar::BBLTopbar(wxFrame* parent) 
    : wxAuiToolBar(parent, ID_TOOL_BAR, wxDefaultPosition, wxSize(-1, 40), wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT | wxAUI_TB_NO_AUTORESIZE)
{ 
    Init(parent);
}

static wxFrame* topbarParent = NULL;

wxBEGIN_EVENT_TABLE(BBLTopbar, wxAuiToolBar)
    EVT_MOTION(BBLTopbar::OnMouseMotion)
    EVT_LEAVE_WINDOW(BBLTopbar::OnMouseLeave)
wxEND_EVENT_TABLE()

BBLTopbar::BBLTopbar(wxWindow* pwin, wxFrame* parent)
    : wxAuiToolBar(pwin, ID_TOOL_BAR, wxDefaultPosition, wxSize(-1, 40), wxAUI_TB_TEXT | wxAUI_TB_HORZ_TEXT | wxAUI_TB_NO_AUTORESIZE)
{
    Init(parent);
    topbarParent = parent;
}

void BBLTopbar::Init(wxFrame* parent) 
{
    m_title_item = nullptr;
    m_calib_item = nullptr;
    m_tabCtrol = nullptr;

    const bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    const wxColour topbar_bg = is_dark ? wxColour("#010101") : wxColour(214, 214, 220);
    SetBackgroundColour(topbar_bg);
    SetArtProvider(new BBLTopbarArt());
#ifdef __WXGTK__
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetDoubleBuffered(true);
    Bind(wxEVT_ERASE_BACKGROUND, [](wxEraseEvent&) {});
#endif
    m_frame = parent;
m_skip_popup_calib_menu    = false;

    wxInitAllImageHandlers();
    //auto  = [=](int x) {return x * em_unit(this) / 10; };
    //this->SetMargins(wxSize(10, 10));
    m_spacer_items.clear();
    auto addDipSpacer = [this](int logical_px) {
        wxAuiToolBarItem* item = this->AddSpacer(FromDIP(logical_px));
        if (item) m_spacer_items.emplace_back(item, logical_px);
        return item;
    };
    addDipSpacer(5);

    wxBitmap logo_bitmap = create_scaled_bitmap("logo", this, (20));
    wxBitmap logo_bitmap_checked = create_scaled_bitmap("logo_checked", this, (20));
    logo_item   = this->AddTool(ID_LOGO, "", logo_bitmap);
    logo_item->SetHoverBitmap(logo_bitmap_checked);

    addDipSpacer(10);
    this->AddSeparator(); 
#ifndef __APPLE__
    wxFont basic_font = this->GetFont();
    basic_font.SetPointSize(10);
    this->SetFont(basic_font);

    this->SetForegroundColour(is_dark ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT) : wxColour(214, 214, 220));

    // MentorSlicer: a classic menu bar, one text button per menu. The transparent 1 px bitmap
    // only gives the button the toolbar's icon height, so all the labels line up.
    wxImage strut(1, FromDIP(TOPBAR_ICON_SIZE));
    strut.InitAlpha();
    for (int y = 0; y < strut.GetHeight(); ++y)
        strut.SetAlpha(0, y, 0);
    const wxBitmap strut_bitmap(strut);
    const std::pair<int, wxString> menu_buttons[] = {{ID_TOP_FILE_MENU, _L("File")},
                                                     {ID_TOP_EDIT_MENU, _L("Edit")},
                                                     {ID_TOP_VIEW_MENU, _L("View")},
                                                     {ID_TOP_CALIB_MENU, _L("Calibration")},
                                                     {ID_TOP_AI_MENU, _L("AI Integration")},
                                                     {ID_TOP_HELP_MENU, _L("Help")}};
    for (const auto& [id, label] : menu_buttons) {
        wxAuiToolBarItem* item = this->AddTool(id, label, strut_bitmap, wxEmptyString, wxITEM_NORMAL);
        m_menu_tools.push_back(item);
        if (id == ID_TOP_FILE_MENU)
            m_file_menu_item = item;
        this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnMenuToolItem, this, id);
    }
 
    addDipSpacer(5);
    this->AddSeparator();
    addDipSpacer(5);
#endif
/*   wxBitmap open_bitmap = create_scaled_bitmap(is_dark ? "open_file" : "open_file_light" , this, (TOPBAR_ICON_SIZE));
    tool_item            = this->AddTool(wxID_OPEN, "", open_bitmap, _L("Open Project"));*/

    addDipSpacer(10);

    wxBitmap save_bitmap = create_scaled_bitmap(is_dark ? "topbar_save" : "topbar_save_light", this, (TOPBAR_ICON_SIZE));
    wxBitmap save_inactive_bitmap = create_scaled_bitmap(is_dark ? "topbar_save_disabled" : "topbar_save_disabled_light", this, (TOPBAR_ICON_SIZE));
    m_save_project_item = this->AddTool(wxID_SAVE, "", save_bitmap, _L("Save the project file"));
    m_save_project_item->SetDisabledBitmap(save_inactive_bitmap);

    addDipSpacer(10);

    //m_preference_item = this->AddTool(ID_PREFERENCES, "", create_scaled_bitmap(is_dark ? "preferences" : "preferences_light", this, (TOPBAR_ICON_SIZE)), _L("Preferences"));

#ifdef __APPLE__
    addDipSpacer(10);
    this->AddTool(ID_CONFIG_RELATE, "", create_scaled_bitmap(is_dark ? "config_relate" : "config_relate_light", nullptr, TOPBAR_ICON_SIZE), _L("Relations"));
    auto item = this->FindTool(ID_CONFIG_RELATE);
    if (item)
    {
        wxBitmap bitmap("");
        item->SetDisabledBitmap(bitmap);
    }
    addDipSpacer(10);
#endif

    wxBitmap undo_bitmap = create_scaled_bitmap(is_dark ? "topbar_undo" : "topbar_undo_light", this, (TOPBAR_ICON_SIZE));
    wxBitmap undo_inactive_bitmap = create_scaled_bitmap(is_dark ? "topbar_undo_disabled" : "topbar_undo_disabled_light", this, (TOPBAR_ICON_SIZE));
    m_undo_item                   = this->AddTool(wxID_UNDO, "", undo_bitmap, _L("Undo"));   
    m_undo_item->SetDisabledBitmap(undo_inactive_bitmap);

    addDipSpacer(10);

    wxBitmap redo_bitmap = create_scaled_bitmap(is_dark ? "topbar_redo" : "topbar_redo_light", this, (TOPBAR_ICON_SIZE));
    wxBitmap redo_inactive_bitmap = create_scaled_bitmap(is_dark ? "topbar_redo_disabled" : "topbar_redo_disabled_light", this, (TOPBAR_ICON_SIZE));
    m_redo_item                   = this->AddTool(wxID_REDO, "", redo_bitmap, _L("Redo"));
    m_redo_item->SetDisabledBitmap(redo_inactive_bitmap);
    /*
    addDipSpacer(10);

    
    wxBitmap calib_bitmap          = create_scaled_bitmap("calib_sf", nullptr, TOPBAR_ICON_SIZE);
    wxBitmap calib_bitmap_inactive = create_scaled_bitmap("calib_sf_inactive", nullptr, TOPBAR_ICON_SIZE);
    m_calib_item                   = this->AddTool(ID_CALIB, _L("Calibration"), calib_bitmap);
    m_calib_item->SetDisabledBitmap(calib_bitmap_inactive);*/

    // Share spare width equally on both sides of the tabs in every layout.
    addDipSpacer(10);
    m_tabs_leading_spacer_item = AddStretchSpacer(1);
    //CX
    ButtonsCtrl* pCtr = new ButtonsCtrl(this);
    pCtr->InsertPage(MainFrame::tp3DEditor, _L("Prepare"), 0);
    pCtr->InsertPage(MainFrame::tpPreview, _L("Preview"), 0);
    pCtr->InsertPage(MainFrame::tpDeviceMgr, _L("Device"), 0);
    //pCtr->InsertPage(3, _L("Project"), 0);
    pCtr->reLayout();
    m_tabCtrol = (wxControl*)pCtr;
    item_ctrl = this->AddControl( m_tabCtrol);
    item_ctrl->SetAlignment(wxALIGN_CENTRE);
    BindWindowDragEvents(m_tabCtrol);
 
    this->Bind(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, [this](wxCommandEvent& evt) {
        //         wxGetApp().mainframe->select_tab(evt.GetId());
        logo_item->SetUserData(HOME_BTN_CODE_UNCHECKED);
        logo_item->SetState(wxAUI_BUTTON_STATE_NORMAL);

        if (evt.GetId() == MainFrame::tpPreview &&
            wxGetApp().tab_panel()->GetSelection() != MainFrame::tpPreview) {
            Plater* plater = wxGetApp().plater();
            PartPlate* current_plate = plater == nullptr ? nullptr : plater->get_partplate_list().get_curr_plate();
            if (current_plate != nullptr && !current_plate->is_slice_result_valid() &&
                !plater->sidebar().prepare_filament_nozzle_mapping_for_slice(false, false)) {
                if (auto* tabs = dynamic_cast<ButtonsCtrl*>(m_tabCtrol)) {
                    tabs->SetSelection(wxGetApp().tab_panel()->GetSelection());
                    tabs->SuppressHoverUntilReenter(MainFrame::tpPreview);
                }
                return;
            }
        }

        if (nullptr != m_tabCtrol) 
        {
            ButtonsCtrl* pCtr = dynamic_cast<ButtonsCtrl*>(m_tabCtrol);
            pCtr->SetSelection(evt.GetId());
        }
        
        wxCommandEvent e = wxCommandEvent(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED);
        e.SetId(evt.GetId());
        
        wxPostEvent(wxGetApp().tab_panel(), e);
        });
    //CX END

    m_title_spacer_item = this->AddStretchSpacer(1);

    addDipSpacer(10);
    m_feedback_separator_item = this->AddSeparator();
    addDipSpacer(10);

#ifdef BBL_TOPBAR_HAS_WINDOW_BUTTONS
    if (topbar_should_draw_window_buttons()) {
        wxBitmap iconize_bitmap = create_scaled_bitmap(is_dark ? "topbar_min" : "topbar_min_light", this, (TOPBAR_ICON_SIZE));
        wxAuiToolBarItem* iconize_btn    = this->AddTool(ID_MINBTN, "", iconize_bitmap);

        maximize_bitmap = create_scaled_bitmap(is_dark ? "topbar_max" : "topbar_max_light", this, (TOPBAR_ICON_SIZE));
        window_bitmap = create_scaled_bitmap(is_dark ? "topbar_win" : "topbar_win_light", this, (TOPBAR_ICON_SIZE));
        if (m_frame->IsMaximized()) {
            maximize_btn = this->AddTool(wxID_MAXIMIZE_FRAME, "", window_bitmap);
        }
        else {
            maximize_btn = this->AddTool(wxID_MAXIMIZE_FRAME, "", maximize_bitmap);
        }

        wxBitmap close_bitmap = create_scaled_bitmap(is_dark ? "topbar_close" : "topbar_close_light", this, (TOPBAR_ICON_SIZE));
        wxAuiToolBarItem* close_btn    = this->AddTool(wxID_CLOSE_FRAME, "", close_bitmap, wxString("Models"));
        //AddDipSpacer(this, 5);
    }
#endif

    m_toolbar_h = parent->FromDIP(40);
    const int client_w = parent->GetClientSize().GetWidth();
    UpdateResponsiveLayout(client_w, true);
    SetSize(client_w, m_toolbar_h);
    
    this->Bind(wxEVT_MOUSE_CAPTURE_LOST, &BBLTopbar::OnMouseCaptureLost, this);
    this->Bind(wxEVT_MENU_CLOSE, &BBLTopbar::OnMenuClose, this);
this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnCalibToolItem, this, ID_CALIB);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnIconize, this, ID_MINBTN);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnFullScreen, this, wxID_MAXIMIZE_FRAME);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnCloseFrame, this, wxID_CLOSE_FRAME);
    this->Bind(wxEVT_LEFT_DCLICK, &BBLTopbar::OnMouseLeftDClock, this);
    #if defined(WIN32) || defined(__WXGTK__)
    this->Bind(wxEVT_LEFT_DOWN, &BBLTopbar::OnMouseLeftDown, this);
    this->Bind(wxEVT_LEFT_UP, &BBLTopbar::OnMouseLeftUp, this);
    #endif
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnSaveProject, this, wxID_SAVE);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnRedo, this, wxID_REDO);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnUndo, this, wxID_UNDO);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnConfigRelate, this, ID_CONFIG_RELATE);
    this->Bind(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, &BBLTopbar::OnLogo, this, ID_LOGO);

    this->Bind(wxEVT_SIZE, &BBLTopbar::OnWindowResize, this);

    //Creality: relations
    // int mode = wxGetApp().app_config->get("role_type") != "0";
    // update_mode(mode);

    ScheduleFileNameDisplayUpdate();
}

BBLTopbar::~BBLTopbar()
{
    m_file_menu_item = nullptr;
    m_file_menu = nullptr;
    // The Edit/View/Calibration/Help menus were owned by m_top_menu before they got their own buttons.
    for (wxMenu* menu : m_owned_menus)
        delete menu;
    m_owned_menus.clear();
    m_menus.clear();
}

void BBLTopbar::OnSaveProject(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->save_project(false, FT_PROJECT);
    EnableSaveItem(false);
}

void BBLTopbar::EnableSaveItem(bool enable)
{
    if (m_save_project_item && GetToolEnabled(m_save_project_item->GetId()) != enable) {
        this->EnableTool(m_save_project_item->GetId(), enable);
        Refresh();
    }
}
void BBLTopbar::EnableUndoItem(bool enable)
{
    if (m_undo_item && GetToolEnabled(m_undo_item->GetId()) != enable) {
        this->EnableTool(m_undo_item->GetId(), enable);
        Refresh();
    }
}

void BBLTopbar::EnableRedoItem(bool enable)
{
    if (m_redo_item && GetToolEnabled(m_redo_item->GetId()) != enable) {
        this->EnableTool(m_redo_item->GetId(), enable);
        Refresh();
    }
}
void BBLTopbar::OnUndo(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->undo();
}

void BBLTopbar::OnRedo(wxAuiToolBarEvent& event)
{
    MainFrame* main_frame = dynamic_cast<MainFrame*>(m_frame);
    Plater* plater = main_frame->plater();
    plater->redo();
}

void BBLTopbar::DisableGuideModeItems()
{
    bool res = this->GetToolEnabled(logo_item->GetId());
    if (!res) {
        logo_item->SetUserData(0);   
    }
       
    for (wxAuiToolBarItem* menu_tool : m_menu_tools) {
        res = this->GetToolEnabled(menu_tool->GetId());
        if (!res)
            menu_tool->SetUserData(0);
    }
    if(tool_item)
    {
        res = this->GetToolEnabled(tool_item->GetId());
        if (!res) {
            tool_item->SetUserData(0);
        }
    }

    res = this->GetToolEnabled(m_save_project_item->GetId());
    if (!res) {
        m_save_project_item->SetUserData(0);
    }

    //res = this->GetToolEnabled(m_preference_item->GetId());
    //if (!res) {
    //    m_preference_item->SetUserData(0);
    //}

    res = this->GetToolEnabled(m_undo_item->GetId());
    if (!res) {
        m_undo_item->SetUserData(0);
    }
    
    res = this->GetToolEnabled(m_redo_item->GetId());
    if (!res) {
        m_redo_item->SetUserData(0);
    }

    this->EnableTool(logo_item->GetId(), false);
    for (wxAuiToolBarItem* menu_tool : m_menu_tools)
        this->EnableTool(menu_tool->GetId(), false);
    if(tool_item)
        this->EnableTool(tool_item->GetId(), false);
    this->EnableTool(m_save_project_item->GetId(), false);
    //this->EnableTool(m_preference_item->GetId(), false);
    this->EnableTool(m_undo_item->GetId(), false);
    this->EnableTool(m_redo_item->GetId(), false);
    m_tabCtrol->Enable(false);
    m_title_enabled = false;

    Refresh();
}


#ifdef __APPLE__
void BBLTopbar::DisableGuideModeItemsMac()
{
    // Only touch the row buttons and tab control to avoid mac-specific crashes.
    if (tool_item)
        this->EnableTool(tool_item->GetId(), false); // Open project
    if (m_save_project_item)
        this->EnableTool(m_save_project_item->GetId(), false); // Save project
    if (m_preference_item)
        this->EnableTool(m_preference_item->GetId(), false); // Preferences
    if (m_undo_item)
        this->EnableTool(m_undo_item->GetId(), false); // Undo
    if (m_redo_item)
        this->EnableTool(m_redo_item->GetId(), false); // Redo
    if (m_tabCtrol)
        m_tabCtrol->Enable(false); // Tabs: Prepare/Preview/Device
    Refresh();
}

void BBLTopbar::EnableGuideModeItemsMac()
{
    if (tool_item)
        this->EnableTool(tool_item->GetId(), true);
    if (m_save_project_item)
        this->EnableTool(m_save_project_item->GetId(), true);
    if (m_preference_item)
        this->EnableTool(m_preference_item->GetId(), true);
    if (m_undo_item)
        this->EnableTool(m_undo_item->GetId(), true);
    if (m_redo_item)
        this->EnableTool(m_redo_item->GetId(), true);
    if (m_tabCtrol)
        m_tabCtrol->Enable(true);
    Refresh();
}
#endif

void BBLTopbar::EnableGuideModeItems()
{
    if (logo_item->GetUserData() != 0)
        logo_item->SetUserData(-1);
    for (wxAuiToolBarItem* menu_tool : m_menu_tools)
        if (menu_tool->GetUserData() != 0)
            menu_tool->SetUserData(-1);
    if (tool_item&&tool_item->GetUserData() != 0)
        tool_item->SetUserData(-1);
    if (m_save_project_item->GetUserData() != 0)
        m_save_project_item->SetUserData(-1);
    //if (m_preference_item->GetUserData() != 0)
    //    m_preference_item->SetUserData(-1);
    if (m_undo_item->GetUserData() != 0)
        m_undo_item->SetUserData(-1);
    if (m_redo_item->GetUserData() != 0)
        m_redo_item->SetUserData(-1);

    this->EnableTool(logo_item->GetId(), true);
    for (wxAuiToolBarItem* menu_tool : m_menu_tools)
        this->EnableTool(menu_tool->GetId(), true);
    if(tool_item)
        this->EnableTool(tool_item->GetId(), true);
    this->EnableTool(m_save_project_item->GetId(), true);
    //this->EnableTool(m_preference_item->GetId(), true);
    this->EnableTool(m_undo_item->GetId(), true);
    this->EnableTool(m_redo_item->GetId(), true);

    m_tabCtrol->Enable(true);
    m_title_enabled = true;

    Refresh();
}

void BBLTopbar::SaveNormalRect()
{
    m_normalRect = m_frame->GetRect();
}

void BBLTopbar::SetSelection(size_t index)
{
    if (index == MainFrame::tpHome)
    {
        wxAuiToolBarEvent evt;
        OnLogo(evt);
    }
    else if (nullptr != m_tabCtrol)
    {
        wxAuiToolBarItem* item = this->FindTool(ID_LOGO);
        item->SetState(wxAUI_BUTTON_STATE_NORMAL);
        ButtonsCtrl* pCtr = dynamic_cast<ButtonsCtrl*>(m_tabCtrol);
        pCtr->SetSelection(index);
    }
}

void BBLTopbar::update_mode(int mode)
{
    if (mode == 0) 
    {
#ifdef __APPLE__
        this->EnableTool(ID_CONFIG_RELATE, false);
        this->GetParent()->Layout();
        return;
#endif
        if (wxMenu* view = m_menus.count(ID_TOP_VIEW_MENU) ? m_menus[ID_TOP_VIEW_MENU] : nullptr; view && view->FindItem(ID_CONFIG_RELATE))
            view->Destroy(ID_CONFIG_RELATE);
        m_relationsItem = NULL;
    } 
    else if (mode == 1) 
    {
#ifdef __APPLE__
        this->EnableTool(ID_CONFIG_RELATE, true);
        this->GetParent()->Layout();
        return;
#endif
        if (m_relationsItem)
            return;

        wxMenu* view = m_menus.count(ID_TOP_VIEW_MENU) ? m_menus[ID_TOP_VIEW_MENU] : nullptr;
        if (!view)
            return;
        if (view->FindItem(ID_CONFIG_RELATE))
            view->Destroy(ID_CONFIG_RELATE);
        m_relationsItem = view->Append(ID_CONFIG_RELATE, _L("Relations"));
        if (!m_relations_bound) {
            view->Bind(
                wxEVT_MENU,
                [](wxCommandEvent&) {
                    wxGetApp().open_config_relate();
                    wxGetApp().plater()->get_current_canvas3D()->force_set_focus();
                },
                ID_CONFIG_RELATE);
            m_relations_bound = true;
        }
        //m_relationsItem->SetBitmap(create_scaled_bitmap(is_dark ? "config_relate" : "config_relate_light", this, TOPBAR_ICON_SIZE));   
    }
}

void BBLTopbar::OnConfigRelate(wxAuiToolBarEvent& evt)
{
    wxGetApp().open_config_relate();
    wxGetApp().plater()->get_current_canvas3D()->force_set_focus();
}
    
void BBLTopbar::OnLogo(wxAuiToolBarEvent& evt) 
{ 
    wxAuiToolBarItem* item = this->FindTool(ID_LOGO);
    item->SetUserData(HOME_BTN_CODE_CHECKED);
    item->SetState(wxAUI_BUTTON_STATE_CHECKED);
    wxGetApp().mainframe->select_tab(size_t(MainFrame::tpHome));
    if (nullptr != m_tabCtrol)
    {
        ButtonsCtrl* pCtr = dynamic_cast<ButtonsCtrl*>(m_tabCtrol);
        pCtr->SetSelection(-1);
    }
}


void BBLTopbar::SetFileMenu(wxMenu* file_menu)
{
    m_file_menu = file_menu;
    m_menus[ID_TOP_FILE_MENU] = file_menu;
}

// Each menu gets its own button (created in the constructor); an unknown title still ends up in
// the hidden aggregate menu, as before.
void BBLTopbar::AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title)
{
    int id = 0;
    if (title == _L("Edit"))
        id = ID_TOP_EDIT_MENU;
    else if (title == _L("View"))
        id = ID_TOP_VIEW_MENU;
    else if (title == _L("Calibration"))
        id = ID_TOP_CALIB_MENU;
    else if (title == _L("AI Integration"))
        id = ID_TOP_AI_MENU;
    else if (title == _L("Help"))
        id = ID_TOP_HELP_MENU;
    if (id == 0) {
        m_top_menu.AppendSubMenu(sub_menu, title);
        return;
    }
    m_menus[id] = sub_menu;
    m_owned_menus.push_back(sub_menu);
}

// The menu that used to collect loose items: now the Edit menu once it is registered.
wxMenu* BBLTopbar::GetTopMenu()
{
    auto it = m_menus.find(ID_TOP_EDIT_MENU);
    return it != m_menus.end() && it->second ? it->second : &m_top_menu;
}

void BBLTopbar::SetTitle(wxString title)
{
    UpdateFileNameDisplay(title);
    Update();
}

void BBLTopbar::SetMaximizedSize()
{
#ifndef __APPLE__
    int count = ++m_set_maximized_size_count;
    
    if (count > 1) {
        BOOST_LOG_TRIVIAL(error) << "SetMaximizedSize() REENTRANCY DETECTED! count=" << count;
        boost::log::core::get()->flush();
    }
    
    if (maximize_btn && maximize_bitmap.IsOk())
        maximize_btn->SetBitmap(maximize_bitmap);
    
    ScheduleFileNameDisplayUpdate();
    --m_set_maximized_size_count;
#endif
}

void BBLTopbar::SetWindowSize()
{
#ifndef __APPLE__
if (maximize_btn && window_bitmap.IsOk())
    maximize_btn->SetBitmap(window_bitmap);
	ScheduleFileNameDisplayUpdate();
#endif
}

int BBLTopbar::GetMinimumToolbarWidth(int screen_client_width) const
{
    return m_layout_widths.minimum_for_screen(screen_client_width);
}

void BBLTopbar::SetActionsCollapsed(bool collapsed)
{
    // The AI/Pro switch and the "More" button were removed, so there is nothing
    // left to collapse: the overflow layout equals the compact one. The function
    // is kept so TopbarLayout and its tests stay valid.
    (void)collapsed;
}

void BBLTopbar::UpdateResponsiveLayout(int width, bool rebuild_metrics)
{
    if (m_responsive_layout_busy || !m_tabCtrol)
        return;

    m_responsive_layout_busy = true;
    auto* buttons = static_cast<ButtonsCtrl*>(m_tabCtrol);
    if (rebuild_metrics || m_layout_widths.normal == 0) {
        // With NO_AUTORESIZE, GetMinSize() can retain the window's old size hint.
        // Measure the actual item sizer, including spacers and window buttons.
        SetActionsCollapsed(false);
        buttons->reLayout(false);
        item_ctrl->SetMinSize(buttons->GetMinSize());
        Realize();
        m_layout_widths.normal = m_sizer->GetMinSize().GetWidth();

        buttons->reLayout(true);
        item_ctrl->SetMinSize(buttons->GetMinSize());
        Realize();
        m_layout_widths.compact = m_sizer->GetMinSize().GetWidth();

        SetActionsCollapsed(true);
        Realize();
        m_layout_widths.overflow = m_sizer->GetMinSize().GetWidth();
        rebuild_metrics = true;
    }

    const TopbarLayout layout = m_layout_widths.layout_for(width);
    if (rebuild_metrics || layout != m_toolbar_layout) {
        buttons->reLayout(layout != TopbarLayout::Normal);
        item_ctrl->SetMinSize(buttons->GetMinSize());
        SetActionsCollapsed(layout == TopbarLayout::Overflow);
        Realize();
        m_toolbar_layout = layout;
        InvalidateBestSize();
        // The parent enforces the screen-dependent minimum. Do not let a normal
        // layout's minimum prevent the toolbar from reaching its compact layout.
        SetMinSize(wxSize(m_layout_widths.overflow, m_toolbar_h));
        Refresh();
    }
    m_responsive_layout_busy = false;
}

void BBLTopbar::Rescale(bool isResize) {
    int em = em_unit(this);
    //auto  = [=](int x) {return x * em_unit(this) / 10; };
    wxAuiToolBarItem* item;
    bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
    const wxColour topbar_bg = is_dark ? wxColour("#010101") : wxColour(214, 214, 220);
    SetBackgroundColour(topbar_bg);
    // wxWidgets 3.3 caches the toolbar background bitmap. The Preferences
    // dark-mode toggle does not send a system colour event to this toolbar,
    // so Refresh()/Realize() alone would keep painting the previous theme.
    wxSysColourChangedEvent colour_event;
    wxAuiToolBar::OnSysColourChanged(colour_event);
    if (isResize)
        m_toolbar_h = m_frame->FromDIP(40);
#ifndef __APPLE__
    item = this->FindTool(ID_LOGO);
    item->SetBitmap(create_scaled_bitmap("logo", this, (20)));
    item->SetHoverBitmap(create_scaled_bitmap("logo_checked", this, (20)));
#endif
    //item = this->FindTool(wxID_OPEN);
    //item->SetBitmap(create_scaled_bitmap(is_dark ? "open_file" : "open_file_light", this, (TOPBAR_ICON_SIZE)));

    item = this->FindTool(wxID_SAVE);
    item->SetBitmap(create_scaled_bitmap(is_dark ? "topbar_save" : "topbar_save_light", this, (TOPBAR_ICON_SIZE)));
    item->SetDisabledBitmap(create_scaled_bitmap(is_dark ? "topbar_save_disabled" : "topbar_save_disabled_light", this, (TOPBAR_ICON_SIZE)));

    //item = this->FindTool(ID_PREFERENCES);
    //item->SetBitmap(create_scaled_bitmap(is_dark ? "preferences" : "preferences_light", this, (TOPBAR_ICON_SIZE)));

#ifdef __APPLE__
     item = this->FindTool(ID_CONFIG_RELATE);
     if (item)
         item->SetBitmap(create_scaled_bitmap(is_dark ? "config_relate" : "config_relate_light", this, TOPBAR_ICON_SIZE));
     if (m_relationsItem)
         m_relationsItem->SetBitmap(create_scaled_bitmap(is_dark ? "config_relate" : "config_relate_light", this, (TOPBAR_ICON_SIZE)));
#endif
    item = this->FindTool(wxID_UNDO);
    item->SetBitmap(create_scaled_bitmap(is_dark ? "topbar_undo" : "topbar_undo_light", this, (TOPBAR_ICON_SIZE)));
    item->SetDisabledBitmap(create_scaled_bitmap(is_dark ? "topbar_undo_disabled" : "topbar_undo_disabled_light", this, (TOPBAR_ICON_SIZE)));

    item = this->FindTool(wxID_REDO);
    item->SetBitmap(create_scaled_bitmap(is_dark ? "topbar_redo" : "topbar_redo_light", this, (TOPBAR_ICON_SIZE)));
    item->SetDisabledBitmap(create_scaled_bitmap(is_dark ? "topbar_redo_disabled" : "topbar_redo_disabled_light", this, (TOPBAR_ICON_SIZE)));

//     item = this->FindTool(ID_CALIB);
//     item->SetBitmap(create_scaled_bitmap("calib_sf", nullptr, TOPBAR_ICON_SIZE));
//     item->SetDisabledBitmap(create_scaled_bitmap("calib_sf_inactive", nullptr, TOPBAR_ICON_SIZE));

    //item = this->FindTool(ID_TITLE);

#ifdef BBL_TOPBAR_HAS_WINDOW_BUTTONS
    if (topbar_should_draw_window_buttons()) {
        item = this->FindTool(ID_MINBTN);
        if (item)
            item->SetBitmap(create_scaled_bitmap(is_dark ? "topbar_min" : "topbar_min_light", this, (TOPBAR_ICON_SIZE)));
        item = this->FindTool(wxID_MAXIMIZE_FRAME);
        maximize_bitmap = create_scaled_bitmap(is_dark ? "topbar_max" : "topbar_max_light", this, (TOPBAR_ICON_SIZE));
        window_bitmap   = create_scaled_bitmap(is_dark ? "topbar_win" : "topbar_win_light", this, (TOPBAR_ICON_SIZE));
        if (item && m_frame->IsMaximized()) {
            item->SetBitmap(window_bitmap);
        }
        else if (item) {
            item->SetBitmap(maximize_bitmap);
        }

        item = this->FindTool(wxID_CLOSE_FRAME);
        if (item)
            item->SetBitmap(create_scaled_bitmap(is_dark ? "topbar_close" : "topbar_close_light", this, (TOPBAR_ICON_SIZE)));
    }
#endif
    if (m_tabCtrol) {
        ButtonsCtrl* pCtr = dynamic_cast<ButtonsCtrl*>(m_tabCtrol);
        pCtr->RefreshColor();
    }

    //refresh layout
    if (isResize)
    {
        // Update spacer sizes based on stored logical DIP values.
        for (auto& pair : m_spacer_items) {
            if (pair.first) {
                pair.first->SetSpacerPixels(FromDIP(pair.second));
            }
        }
    }

    // Rebuild metrics after font, bitmap or DPI changes, preserving the available width.
    const int width = GetClientSize().GetWidth();
    UpdateResponsiveLayout(width, true);
    SetSize(width, m_toolbar_h);
    Layout();
    Refresh();
    UpdateFileNameDisplay();
    Update();
}

void BBLTopbar::OnIconize(wxAuiToolBarEvent& event)
{
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " start";  
#ifdef __WXMSW__
    ::SendMessage((HWND) m_frame->GetHandle(), WM_SYSCOMMAND, SC_MINIMIZE, 0);
#else
    m_frame->Iconize();
#endif
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " end";

    boost::log::core::get()->flush();
}



void BBLTopbar::OnFullScreen(wxAuiToolBarEvent& event)
{
    if (m_frame->IsMaximized()) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Restore";   
        m_frame->Maximize(false);
    }
    else {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Maximize";   
        m_normalRect = m_frame->GetRect();
        m_frame->Maximize();
    }

    boost::log::core::get()->flush();
}

void BBLTopbar::OnCloseFrame(wxAuiToolBarEvent& event)
{
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " start";   
    m_frame->Close();
    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " end";

    boost::log::core::get()->flush();
}

void BBLTopbar::OnMouseLeftDClock(wxMouseEvent& mouse)
{
    wxPoint mouse_pos = ::wxGetMousePosition();
    // check whether mouse is not on any tool item
    if (this->FindToolByCurrentPosition() != NULL &&
        this->FindToolByCurrentPosition() != m_title_item) {
        mouse.Skip();
        return;
    }
#ifdef __W1XMSW__
    ::PostMessage((HWND) m_frame->GetHandle(), WM_NCLBUTTONDBLCLK, HTCAPTION, MAKELPARAM(mouse_pos.x, mouse_pos.y));
    return;
#endif //  __WXMSW__

    wxAuiToolBarEvent evt;
    OnFullScreen(evt);
}

void BBLTopbar::OnMenuToolItem(wxAuiToolBarEvent& evt)
{
    const int id = evt.GetId();
    auto it = m_menus.find(id);
    wxMenu* menu = it != m_menus.end() ? it->second : nullptr;
    // A click on the button of the menu that was just closed only closes it (see OnMenuClose).
    if (m_skip_popup_menu_id == id || menu == nullptr) {
        m_skip_popup_menu_id = 0;
        return;
    }
    m_skip_popup_menu_id = 0;

    wxAuiToolBar* tb = static_cast<wxAuiToolBar*>(evt.GetEventObject());
    tb->SetToolSticky(id, true);
    const wxRect rect = this->GetToolRect(id);
    GetParent()->PopupMenu(menu, wxPoint(rect.GetLeft(), this->GetSize().GetHeight() - 2));
    // make sure the button is "un-stuck"
    tb->SetToolSticky(id, false);
}

void BBLTopbar::OnCalibToolItem(wxAuiToolBarEvent &evt)
{
    wxAuiToolBar *tb = static_cast<wxAuiToolBar *>(evt.GetEventObject());

    tb->SetToolSticky(evt.GetId(), true);

    if (!m_skip_popup_calib_menu) {
        auto rec = this->GetToolRect(ID_CALIB);
        GetParent()->PopupMenu(&m_calib_menu, wxPoint(rec.GetLeft(), this->GetSize().GetHeight() - 2));
    } else {
        m_skip_popup_calib_menu = false;
    }

    // make sure the button is "un-stuck"
    tb->SetToolSticky(evt.GetId(), false);
}

void BBLTopbar::BindWindowDragEvents(wxWindow* window)
{
    // Page buttons handle their own mouse capture and must not participate in frame dragging.
    if (!window || dynamic_cast<Button*>(window) != nullptr)
        return;

    window->Bind(wxEVT_LEFT_DOWN, &BBLTopbar::OnChildDragLeftDown, this);
    window->Bind(wxEVT_LEFT_UP, &BBLTopbar::OnChildDragLeftUp, this);
    window->Bind(wxEVT_MOTION, &BBLTopbar::OnChildDragMotion, this);
    for (wxWindow* child : window->GetChildren())
        BindWindowDragEvents(child);
}

void BBLTopbar::OnChildDragLeftDown(wxMouseEvent& event)
{
    m_child_drag_source = dynamic_cast<wxWindow*>(event.GetEventObject());
    m_child_drag_start_screen = ::wxGetMousePosition();
    m_child_drag_candidate = m_child_drag_source != nullptr;
    if (m_child_drag_source && !m_child_drag_source->HasCapture())
        m_child_drag_source->CaptureMouse();
    event.Skip();
}

void BBLTopbar::OnChildDragLeftUp(wxMouseEvent& event)
{
    wxWindow* source = dynamic_cast<wxWindow*>(event.GetEventObject());
    if (source && source->HasCapture())
        source->ReleaseMouse();
    m_child_drag_candidate = false;
    m_child_drag_source = nullptr;
    event.Skip();
}

void BBLTopbar::OnChildDragMotion(wxMouseEvent& event)
{
    if (!m_child_drag_candidate || !m_child_drag_source || !event.LeftIsDown()) {
        if (!event.LeftIsDown()) {
            m_child_drag_candidate = false;
            m_child_drag_source = nullptr;
        }
        event.Skip();
        return;
    }

    const wxPoint mouse_pos = ::wxGetMousePosition();
    const int drag_x = wxMax(1, wxSystemSettings::GetMetric(wxSYS_DRAG_X, m_frame));
    const int drag_y = wxMax(1, wxSystemSettings::GetMetric(wxSYS_DRAG_Y, m_frame));
    if (std::abs(mouse_pos.x - m_child_drag_start_screen.x) < drag_x &&
        std::abs(mouse_pos.y - m_child_drag_start_screen.y) < drag_y) {
        event.Skip();
        return;
    }

    wxWindow* source = m_child_drag_source;
    m_child_drag_candidate = false;
    m_child_drag_source = nullptr;

    // Cancel the pending control click before handing the gesture to the frame.
    wxMouseEvent cancel_event(wxEVT_LEFT_UP);
    cancel_event.SetEventObject(source);
    cancel_event.SetPosition(wxPoint(-1, -1));
    source->GetEventHandler()->ProcessEvent(cancel_event);

#ifdef __WXGTK__
    if (begin_wayland_frame_move(m_frame, event))
        return;
#endif
#ifdef __WXMSW__
    ::PostMessage((HWND)m_frame->GetHandle(), WM_NCLBUTTONDOWN, HTCAPTION,
                  MAKELPARAM(mouse_pos.x, mouse_pos.y));
    return;
#else
    m_delta = mouse_pos - m_frame->GetScreenPosition();
    if (!HasCapture())
        CaptureMouse();
#endif
}
void BBLTopbar::OnMouseLeftDown(wxMouseEvent& event)
{
    const wxPoint mouse_pos = ::wxGetMousePosition();
    const wxPoint frame_pos = m_frame->GetScreenPosition();
    m_delta = mouse_pos - frame_pos;

    wxAuiToolBarItem* item = FindToolByCurrentPosition();
    const bool is_action_tool = item && item != m_title_item && item->GetId() > 0;
    if (!is_action_tool)
    {
#ifdef __WXGTK__
        if (begin_wayland_frame_move(m_frame, event))
            return;
#endif
        CaptureMouse();
#ifdef __WXMSW__
        ReleaseMouse();
        ::PostMessage((HWND) m_frame->GetHandle(), WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(mouse_pos.x, mouse_pos.y));
        return;
#endif //  __WXMSW__
        event.Skip();
        return;
    }

    // wxAuiToolBar dispatches wxEVT_AUITOOLBAR_TOOL_DROPDOWN on LEFT_DOWN.
    // Consume that down event and defer the action until a matching LEFT_UP,
    // so the same gesture can still become a window drag.
    m_child_drag_candidate = true;
    m_child_drag_source = this;
    m_child_drag_start_screen = mouse_pos;
    if (!HasCapture())
        CaptureMouse();
}
void BBLTopbar::OnMouseLeftUp(wxMouseEvent& event)
{
    if (m_child_drag_source == this) {
        const wxPoint release_pos = event.GetPosition();
        const wxPoint press_pos = ScreenToClient(m_child_drag_start_screen);
        wxAuiToolBarItem* pressed_item = FindToolByPosition(press_pos.x, press_pos.y);
        wxAuiToolBarItem* released_item = FindToolByPosition(release_pos.x, release_pos.y);
        const bool activate = m_child_drag_candidate && pressed_item && released_item &&
                              pressed_item->GetId() == released_item->GetId() &&
                              GetToolEnabled(pressed_item->GetId());

        m_child_drag_candidate = false;
        m_child_drag_source = nullptr;
        if (HasCapture())
            ReleaseMouse();

        if (activate) {
            const int tool_id = pressed_item->GetId();
            wxAuiToolBarEvent tool_event(wxEVT_AUITOOLBAR_TOOL_DROPDOWN, tool_id);
            tool_event.SetEventObject(this);
            tool_event.SetToolId(tool_id);
            tool_event.SetClickPoint(release_pos);
            tool_event.SetItemRect(GetToolRect(tool_id));
            GetEventHandler()->ProcessEvent(tool_event);
        }
        return;
    }

    if (HasCapture())
        ReleaseMouse();
    event.Skip();
}

void BBLTopbar::OnMouseMotion(wxMouseEvent& event)
{
    const wxPoint mouse_pos = ::wxGetMousePosition();

    if (m_child_drag_candidate && m_child_drag_source == this) {
        if (!event.LeftIsDown()) {
            m_child_drag_candidate = false;
            m_child_drag_source = nullptr;
            if (HasCapture())
                ReleaseMouse();
            return;
        }

        const int drag_x = wxMax(1, wxSystemSettings::GetMetric(wxSYS_DRAG_X, m_frame));
        const int drag_y = wxMax(1, wxSystemSettings::GetMetric(wxSYS_DRAG_Y, m_frame));
        if (std::abs(mouse_pos.x - m_child_drag_start_screen.x) < drag_x &&
            std::abs(mouse_pos.y - m_child_drag_start_screen.y) < drag_y)
            return;

        m_child_drag_candidate = false;
        m_child_drag_source = nullptr;
        if (HasCapture())
            ReleaseMouse();

#ifdef __WXGTK__
        if (begin_wayland_frame_move(m_frame, event))
            return;
#endif
#ifdef __WXMSW__
        ::PostMessage((HWND)m_frame->GetHandle(), WM_NCLBUTTONDOWN, HTCAPTION,
                      MAKELPARAM(mouse_pos.x, mouse_pos.y));
        return;
#else
        m_delta = mouse_pos - m_frame->GetScreenPosition();
        if (!HasCapture())
            CaptureMouse();
        return;
#endif
    }

#ifndef __APPLE__
    const bool over_title = !HasCapture() && !m_displayName.IsEmpty() &&
                            GetTitleDisplayRect().Contains(event.GetPosition());
    if (over_title) {
        if (!m_title_tooltip_active) {
            m_title_tooltip_active = true;
            m_tipItem = nullptr;
            SetHoverItem(nullptr);
            SetToolTip(m_displayName);
        }
        return;
    }

    if (m_title_tooltip_active) {
        m_title_tooltip_active = false;
        UnsetToolTip();
    }
#endif

    if (!HasCapture()) {
        //m_frame->OnMouseMotion(event);

        event.Skip();
        return;
    }

    if (event.Dragging() && event.LeftIsDown())
    {
        // leave max state and adjust position 
        if (m_frame->IsMaximized()) {
            wxRect rect = m_frame->GetRect();
            // Filter unexcept mouse move
            if (m_delta + rect.GetLeftTop() != mouse_pos) {
                m_delta = mouse_pos - rect.GetLeftTop();
                m_delta.x = m_delta.x * m_normalRect.width / rect.width;
                m_delta.y = m_delta.y * m_normalRect.height / rect.height;
                m_frame->Maximize(false);
            }
        }
        m_frame->Move(mouse_pos - m_delta);
    }
    event.Skip();
}

void BBLTopbar::OnMouseLeave(wxMouseEvent& event)
{
    if (m_title_tooltip_active) {
        m_title_tooltip_active = false;
        UnsetToolTip();
    }
    event.Skip();
}

void BBLTopbar::OnMouseCaptureLost(wxMouseCaptureLostEvent& event)
{
}

void BBLTopbar::OnMenuClose(wxMenuEvent& event)
{
    wxAuiToolBarItem* item = this->FindToolByCurrentPosition();
    m_skip_popup_menu_id = 0;
    for (wxAuiToolBarItem* menu_tool : m_menu_tools)
        if (item == menu_tool)
            m_skip_popup_menu_id = menu_tool->GetId();
}

wxAuiToolBarItem* BBLTopbar::FindToolByCurrentPosition()
{
    wxPoint mouse_pos = ::wxGetMousePosition();
    wxPoint client_pos = this->ScreenToClient(mouse_pos);
    return this->FindToolByPosition(client_pos.x, client_pos.y);
}

void BBLTopbar::UpdateFileNameDisplay()
{
    UpdateFileNameDisplay(m_displayName);
}

wxRect BBLTopbar::GetTitleDisplayRect() const
{
    if (!m_tabCtrol || !m_feedback_separator_item || !m_feedback_separator_item->GetSizerItem())
        return wxRect();

    // Hide the placeholder as soon as the toolbar needs a compact layout,
    // even when the remaining stretch space could still fit its text.
    if (m_displayName == _L("Untitled") && m_toolbar_layout != TopbarLayout::Normal)
        return wxRect();

    int tabs_right = m_tabCtrol->GetRect().GetRight();
    if (const auto* buttons = dynamic_cast<const ButtonsCtrl*>(m_tabCtrol))
        tabs_right = m_tabCtrol->GetPosition().x + buttons->GetButtonsRight();
    const wxRect separator_rect = m_feedback_separator_item->GetSizerItem()->GetRect();
    const int padding = FromDIP(TOPBAR_TITLE_SIDE_PADDING);
    const int left = tabs_right + padding;
    const int right = separator_rect.GetLeft() - padding;
    const int width = right - left;
    if (width < FromDIP(TOPBAR_TITLE_MIN_VISIBLE_WIDTH))
        return wxRect();

    // The placeholder is optional: hide it instead of drawing a truncated "Untitled".
    if (m_displayName == _L("Untitled")) {
        wxClientDC dc(const_cast<BBLTopbar*>(this));
        dc.SetFont(Label::Head_12);
        if (dc.GetTextExtent(m_displayName).x > width)
            return wxRect();
    }

    const int toolbar_height = GetClientSize().GetHeight();
    const int height = FromDIP(18);
    const int y = wxMax(0, (toolbar_height - height) / 2);
    return wxRect(left, y, width, height);
}

wxString BBLTopbar::TruncateTextToWidth(const wxString& text, int maxWidth, wxDC& dc) const
{
    if (text.IsEmpty() || maxWidth <= 0)
        return text;

    if (dc.GetTextExtent(text).x <= maxWidth)
        return text;

    int ellipsize_width = maxWidth - FromDIP(8);
    if (ellipsize_width < 1)
        ellipsize_width = 1;
    return wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, ellipsize_width);
}

void BBLTopbar::OnCustomRender(wxDC& dc, const wxAuiToolBarItem& item, const wxRect& rect)
{
    if (&item == item_ctrl)
        DrawTitle(dc, rect);
}

void BBLTopbar::DrawTitle(wxDC& dc, const wxRect& /*item_rect*/) const
{
#ifdef __APPLE__
    return;
#else
    const wxRect rect = GetTitleDisplayRect();
    if (rect.IsEmpty() || m_displayName.IsEmpty())
        return;

    dc.SetFont(Label::Head_12);
    dc.SetTextForeground(m_title_enabled
        ? (wxGetApp().dark_mode() ? wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT) : wxColour(0, 0, 0))
        : wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

    const wxString text = TruncateTextToWidth(m_displayName, rect.GetWidth(), dc);
    const wxSize text_size = dc.GetTextExtent(text);
    const int x = rect.x + wxMax(0, (rect.width - text_size.x) / 2);
    const int y = rect.y + wxMax(0, (rect.height - text_size.y) / 2);

    dc.SetClippingRegion(rect);
    dc.DrawText(text, x, y);
    dc.DestroyClippingRegion();
#endif
}

void BBLTopbar::ScheduleFileNameDisplayUpdate()
{
    if (m_file_name_update_scheduled)
        return;

    m_file_name_update_scheduled = true;
    CallAfter(&BBLTopbar::UpdateFileNameDisplayAfterLayout);
}

void BBLTopbar::UpdateFileNameDisplayAfterLayout()
{
    m_file_name_update_scheduled = false;
    UpdateFileNameDisplay();
    Update();
}

void BBLTopbar::UpdateFileNameDisplay(const wxString& fileName)
{
    wxString title = fileName.IsEmpty() ? m_displayName : fileName;
    m_displayName = title;

#ifdef __APPLE__
    return;
#else
    const wxRect title_rect = GetTitleDisplayRect();
    const wxString visible_title = title_rect.IsEmpty() ? wxString() : m_displayName;
    if (m_title_spacer_item) {
        m_title_spacer_item->SetShortHelp(visible_title);
        if (m_title_tooltip_active || m_tipItem == m_title_spacer_item) {
            if (visible_title.IsEmpty()) {
                UnsetToolTip();
                m_title_tooltip_active = false;
            } else {
                SetToolTip(visible_title);
            }
        }
    }
    if (title_rect.IsEmpty())
        Refresh(true);
    else
        RefreshRect(title_rect, true);
#endif
}

void BBLTopbar::OnWindowResize(wxSizeEvent& event)
{
    event.Skip();
    UpdateResponsiveLayout(event.GetSize().GetWidth());
    ScheduleFileNameDisplayUpdate();
}
