#pragma once

#include "wx/wxprec.h"
#include "wx/aui/auibar.h"

// F5: SelectMachine.hpp removed; these are the generic headers it used to bring in.
#include <wx/wx.h>
#include <wx/intl.h>
#include <wx/collpane.h>
#include <wx/dataview.h>
#include <wx/artprov.h>
#include <wx/xrc/xmlres.h>
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
#include <wx/wrapsizer.h>
#include <wx/srchctrl.h>
#include <wx/simplebook.h>
#include <wx/hashmap.h>
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include "Widgets/PopupWindow.hpp"
#include "Jobs/Worker.hpp"
#include "TopbarLayout.hpp"


using namespace Slic3r::GUI;

class BBLTopbar : public wxAuiToolBar
{
    wxDECLARE_EVENT_TABLE();

public:
    BBLTopbar(wxWindow* pwin, wxFrame* parent);
    BBLTopbar(wxFrame* parent);
    void Init(wxFrame *parent);
    ~BBLTopbar();
    int GetMinimumToolbarWidth(int screen_client_width) const;
    void Rescale(bool isResize);
    void OnIconize(wxAuiToolBarEvent& event);
    void OnFullScreen(wxAuiToolBarEvent& event);
    void OnCloseFrame(wxAuiToolBarEvent& event);
    // MentorSlicer: one text button per menu (File, Edit, View, Calibration, Help).
    void OnMenuToolItem(wxAuiToolBarEvent& evt);
void OnCalibToolItem(wxAuiToolBarEvent &evt);
    void OnMouseLeftDClock(wxMouseEvent& mouse);
    void OnMouseLeftDown(wxMouseEvent& event);
    void OnMouseLeftUp(wxMouseEvent& event);
    void OnMouseMotion(wxMouseEvent& event);
    void OnMouseLeave(wxMouseEvent& event);
    void OnMouseCaptureLost(wxMouseCaptureLostEvent& event);
    void OnMenuClose(wxMenuEvent& event);
    void OnSaveProject(wxAuiToolBarEvent& event);
    void OnUndo(wxAuiToolBarEvent& event);
    void OnRedo(wxAuiToolBarEvent& event);
    void OnConfigRelate(wxAuiToolBarEvent& evt);
    void OnLogo(wxAuiToolBarEvent& evt);
    void EnableSaveItem(bool enable);
    void EnableUndoItem(bool enable);
    void EnableRedoItem(bool enable);
    wxAuiToolBarItem* FindToolByCurrentPosition();
	
    void SetFileMenu(wxMenu* file_menu);
    void AddDropDownSubMenu(wxMenu* sub_menu, const wxString& title);
    wxMenu *GetTopMenu();
    void SetTitle(wxString title);
    void SetMaximizedSize();
    void SetWindowSize();


    void DisableGuideModeItems();
    void EnableGuideModeItems();
#ifdef __APPLE__
    // mac-only: safely disable/enable just the row buttons + tabs for guide
    void DisableGuideModeItemsMac();
    void EnableGuideModeItemsMac();
#endif

    void SaveNormalRect();


    void SetSelection(size_t index);

    void update_mode(int mode);


    void UpdateFileNameDisplay();
    void UpdateFileNameDisplay(const wxString& fileName);

private:

    void OnCustomRender(wxDC& dc, const wxAuiToolBarItem& item, const wxRect& rect) override;
    void DrawTitle(wxDC& dc, const wxRect& rect) const;
    wxRect GetTitleDisplayRect() const;
    void ScheduleFileNameDisplayUpdate();
    void UpdateFileNameDisplayAfterLayout();
    wxString TruncateTextToWidth(const wxString& text, int maxWidth, wxDC& dc) const;

    void OnWindowResize(wxSizeEvent& event);
    void UpdateResponsiveLayout(int width, bool rebuild_metrics = false);
    void SetActionsCollapsed(bool collapsed);
    void BindWindowDragEvents(wxWindow* window);
    void OnChildDragLeftDown(wxMouseEvent& event);
    void OnChildDragLeftUp(wxMouseEvent& event);
    void OnChildDragMotion(wxMouseEvent& event);


private:
    wxFrame* m_frame;
    wxAuiToolBarItem* logo_item = nullptr;
    wxAuiToolBarItem* tool_item = nullptr;
    wxAuiToolBarItem* m_preference_item = nullptr;
    wxAuiToolBarItem* item_ctrl = nullptr;
    wxAuiToolBarItem* m_file_menu_item{nullptr};
    // Menu buttons by tool id and the menu each one opens. The Edit/View/Calibration/Help
    // menus are owned here (deleted in the destructor); the File menu belongs to MainFrame.
    std::vector<wxAuiToolBarItem*> m_menu_tools;
    std::map<int, wxMenu*> m_menus;
    std::vector<wxMenu*> m_owned_menus;
    int m_skip_popup_menu_id{0};
    bool m_relations_bound{false};
wxRect m_normalRect;
    wxPoint m_delta;
    wxMenu m_top_menu;
    wxMenu* m_file_menu;
    wxMenu m_calib_menu;
    wxMenuItem*       m_relationsItem{NULL};   

    wxAuiToolBarItem* m_title_item;
    wxAuiToolBarItem* m_undo_item;
    wxAuiToolBarItem* m_redo_item;
    wxAuiToolBarItem* m_calib_item;
    wxAuiToolBarItem* maximize_btn;
    wxAuiToolBarItem* m_save_project_item;
    wxAuiToolBarItem* m_feedback_separator_item{nullptr};
    wxAuiToolBarItem* m_tabs_leading_spacer_item{nullptr};
    wxAuiToolBarItem* m_title_spacer_item{nullptr};
    wxControl* m_tabCtrol;
    Slic3r::GUI::TopbarWidths m_layout_widths;
    Slic3r::GUI::TopbarLayout m_toolbar_layout{Slic3r::GUI::TopbarLayout::Normal};
    bool m_responsive_layout_busy{false};

    wxBitmap maximize_bitmap;
    wxBitmap window_bitmap;

    std::atomic<int> m_set_maximized_size_count{0};

    // Track spacer items and their logical DIP sizes so they can be resized on DPI changes.
    std::vector<std::pair<wxAuiToolBarItem*, int>> m_spacer_items;

    int m_toolbar_h;
bool m_skip_popup_calib_menu;
    bool m_file_name_update_scheduled{false};
    bool m_title_enabled{true};
    bool m_title_tooltip_active{false};
    bool m_child_drag_candidate{false};
    wxWindow* m_child_drag_source{nullptr};
    wxPoint m_child_drag_start_screen;
    wxString m_displayName;
};
