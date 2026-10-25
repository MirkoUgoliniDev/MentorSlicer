F2 implementation spec ("Interfaccia"). Nothing was edited, built or run.

Everything below was checked against the `advisor` branch (HEAD 14528e0e) unless marked **[INFERENCE]**. Line numbers are from the current files. Files use CRLF line endings, so patches must keep them. No file is added or deleted from CMake, so `src/slic3r/CMakeLists.txt` and `localization/i18n/list.txt` need no change (removed `_L()` strings only leave unused `.po` entries). One build with `build_exe_only.bat`; it does not reconfigure. The touched headers (MainFrame.hpp, GLCanvas3D.hpp, GUI_App.hpp, FilamentPanel.h) cause a medium recompile. PrintConfig.hpp is not touched.

---

## Item 1 — Tabs: Prepare, Preview, Device only

### 1.0 How tab indices actually work today

- **`Notebook::SetSelection(n)`** (src/slic3r/GUI/Notebook.hpp:191-227) tries `n` as a page index first. If that fails, it looks for a page whose window ID equals `n`. That is the only reason `tpDeviceMgr=10` works today, through `m_printer_mgr_view->SetId(tpDeviceMgr)` at MainFrame.cpp:1571.
- **Current notebook order:**
  - `init_tabpanel` (MainFrame.cpp:1413) adds webview (index 0), then printer_mgr (1), then ai_creation (2).
  - `update_layout` (MainFrame.cpp:1141-1148) then inserts online-model at 1, plater at 2, plater at 3.
  - Result: Home 0, Online 1, Prepare 2, Preview 3, Device 4, AI 5.
  - So the old `tpMonitor=4` matched the Device page by accident. `is_printer_view()` and `Plater::priv::on_tab_selection_changing` relied on that.
- **After the change, page index equals enum value in both variants:**
  - Variant A: `init_tabpanel` adds Device (index 0); `update_layout` inserts plater at 0 and plater at 1, giving Prepare 0, Preview 1, Device 2.
  - Variant B: Home 0 and Device 1; the two inserts at 1 and 2 give Home 0, Prepare 1, Preview 2, Device 3.
- **Topbar buttons:** each `ButtonsCtrl` button's window ID is the enum value (BBLTopbar.cpp:439-440, `new Button(..., index)`). Home is not a button; it is the logo (`OnLogo`, BBLTopbar.cpp:1566). An ID of 0 for the Prepare button in Variant A is fine; `tpHome=0` is used the same way today. Note that `wxCommandEvent`'s default ID is 0, which means Home today and Prepare in Variant A. No poster relies on the default (all posters were checked).

### 1.1 Enum (MainFrame.hpp:206-220)

Replace the enum with:

- **Variant A:** `enum TabPosition { tp3DEditor = 0, tpPreview = 1, tpDeviceMgr = 2 };`
- **Variant B:** `enum TabPosition { tpHome = 0, tp3DEditor = 1, tpPreview = 2, tpDeviceMgr = 3 };`

### 1.2 Every reference to a removed name (grep of the whole `src` tree)

Outside `src`, only resources/web/deviceMgr/assets/BZCDzYbb.js has the string `tabName:"tpHome"`, in 4 spots. It reaches `GUI_App::switch_to_tab`, which becomes a no-op, so no web patch is needed.

**tpHome** (all deleted in Variant A; all kept in Variant B unless noted)

| Location | Variant A | Variant B |
|---|---|---|
| BBLTopbar.cpp:1490-1494 `SetSelection` | Drop the tpHome branch; body becomes `if (m_tabCtrol) { if (auto* item = FindTool(ID_LOGO)) item->SetState(wxAUI_BUTTON_STATE_NORMAL); static_cast<ButtonsCtrl*>(m_tabCtrol)->SetSelection(int(index)); }` | Keep |
| BBLTopbar.cpp:1566-1584 `OnLogo` | Delete it, plus its Bind at :1206 and its declaration at BBLTopbar.hpp:48. The logo becomes decorative. | Line :1577 becomes `select_tab(size_t(MainFrame::tpHome))` |
| GUI_App.cpp:2129-2145 `switch_to_tab` | Body becomes empty, with a comment that the Home and Online tabs were removed | Same no-op, as you asked. Alternative: keep only the `"tpHome"` branch. |
| GUI_App.cpp:2318-2343 (`default_page`) | Replace the whole if/else-if with `mainframe->select_tab(size_t(MainFrame::tp3DEditor)); mainframe->m_topbar->SetSelection(size_t(MainFrame::tp3DEditor));` | Keep |
| GUI_App.cpp:8883-8884 (homepage `get_is_first_install` guide) | Delete both lines; the handler is unreachable without Home | Line 8883 becomes `select_tab(size_t(MainFrame::tpHome))` |
| InstanceCheck.cpp:503 | Delete the line (keep the `if (message=="logout")` block empty or remove it) | Keep |
| LoginTip.cpp:104, 120, 131, 143 | Delete each `select_tab` line (the `if (res == wxID_YES) {}` stays harmless) | Keep |
| NotificationManager.cpp:2463 | Delete | Keep |
| Plater.cpp:7980 | Delete | Keep |
| print_manage/UploadGcodeToCloud.cpp:1099, 1110, 1136 | Delete (and the comment at :1088) | Keep |
| SyncUserPresets.cpp:175 | Delete | Keep |
| WebModelLibraryView.cpp:454 | Delete (the file is still compiled until F3) | Keep |
| Utils/PresetUpdater.cpp:1439 | Delete | Keep |
| simple/sendWorkflow/EasyPrintSender.cpp:873-874 | Delete both lines | Keep |
| simple/bridge/SlicerBridgeState.cpp:687 | Delete the case | Keep |
| MainFrame.cpp:1454-1455 | Delete the branch | Keep |
| MainFrame.cpp:1530-1536 (EVT_LOAD_URL Bind, `select_tab(tpHome)`) | Delete the Bind. `MainFrame::load_url` (:5268) then posts an unhandled event, which is harmless. | Keep |
| MainFrame.cpp:1508-1524 (commented switch) | Delete | Delete |

**tpOnlineModel**

| Location | Change (both variants) |
|---|---|
| BBLTopbar.cpp:1058 | Delete `InsertPage(tpOnlineModel…)` |
| GUI_App.cpp:2138-2144 | Removed with the `switch_to_tab` no-op |
| MainFrame.cpp:1142-1145 | Delete the `InsertPage` |
| MainFrame.cpp:1174-1185 | Delete the branch |
| MainFrame.cpp:1444-1448 | Delete the branch |
| MainFrame.cpp:1566-1568 | Delete the `m_webmodellibrary_view` creation and its SetId |
| MainFrame.cpp:4861-4862 | Delete |
| SlicerBridgeActionsObject.cpp:421-426, `DoOpenModelLibrary` | Body becomes `return {{"success", false}, {"message", "The online model library was removed from this build"}};` (F3 deletes the action) |
| SlicerBridgeState.cpp:688 | Delete |
| MCPToolCallsObject.cpp:180 | Delete; also fix the comment at :171 ("tab index 2" becomes 0 in A, 1 in B) |

**tpAICreation**

| Location | Change (both variants) |
|---|---|
| BBLTopbar.cpp:269-274 `ApplyButtonStyle` | Body becomes `button->SetTextColor(DefaultTextColor(selected));` |
| BBLTopbar.cpp:438-444 `InsertPage` | Use `new Button(this, text.empty() ? text : " " + text, bmp_name, wxNO_BORDER, 0, index);` and delete the `is_ai_creation` block |
| BBLTopbar.cpp:1057 | Delete |
| MainFrame.cpp:1186-1193 | Delete the branch |
| MainFrame.cpp:1449-1453 | Delete |
| MainFrame.cpp:1575-1580 | Delete the creation, SetId and AddPage |
| MainFrame.cpp:4827-4834 | Delete |
| MainFrame.cpp:4840 | Becomes `this->topbar()->SetSelection(new_selection);` |
| MainFrame.cpp:4863-4864 | Delete |

**tpMonitor**

| Location | Change |
|---|---|
| MainFrame.cpp:5305 `is_printer_view` | Delete it, its declaration at MainFrame.hpp:366, and its only caller at GLCanvas3D.cpp:4060-4061 (the F5 branch; `m_printer_view` is never created) |
| Plater.cpp:14465-14481 | Delete the whole BBL/printer_view block; keep 14461-14464 |
| Plater.cpp:23271 | Delete the line |
| Plater.cpp:23287 | Delete the comment |
| SlicerBridgeState.cpp:691 | Delete |
| MCPToolCallsObject.cpp:181 | Delete |
| Utils/SimplyPrint.cpp:328-329 | Replace both lines with `wxLaunchDefaultBrowser(url);` (`load_printer_url` has no handler) |
| PrintHost.cpp:324 (comment) | Delete |
| MainFrame.cpp comments at 1561, 1633, 1678, 1683, 4802 | Delete; in `show_device`, keep only the `UpdateDarkUIWin` call |

**tpMultiDevice:** SlicerBridgeState.cpp:692 (delete); comments at MainFrame.cpp:1642 and 4810 (delete).

**tpCalibration:** Plater.cpp:23245, `send_calibration_job_finished` (delete the line); comment at MainFrame.cpp:1650 (delete).

**tpProject, tpAuxiliary, toDebugTool:** no references besides the enum itself.

**Hidden numeric uses:**

- GUI_App.cpp:5154 `select_tab(size_t(0))`: becomes `tp3DEditor` in A and `tpHome` in B (lines 5153-5157 collapse to one call in A).
- Plater.cpp:7697 `select_tab(size_t(1))`: today this selects Online Models, an existing bug. Becomes `size_t(MainFrame::tp3DEditor)` in both variants.
- Plater.cpp:20463 `select_tab(size_t(0))` ("jump to plater panel"): becomes `tp3DEditor` in both variants.
- print_manage/Upload3mfToCloud.cpp:1167 and UploadGcodeToCloud.cpp:1225 `e.SetId(9)` ("printer details page"): become `MainFrame::tpDeviceMgr`.
- BBLTopbar.cpp:1577 `select_tab(size_t(0))`: see `OnLogo` above.
- MainFrame.cpp:1134 `m_last_selected_tab = 1;`: becomes `tp3DEditor`.

**Unchanged and verified fine:** CreatePresetsDialog:7756, Downloader:140, GUI_ObjectList:9212, GUI_App 2196/2202/2270/5156/5212/6209/9281-9335, DeviceListSimple 1360/1418, GLCanvas3DSimple:854, MCPToolCallsRegistration 544-568, DeviceMgrRoutes:1227, PrintHost 332-334, SelectMachine:1834, NotificationManager (tp3DEditor uses), every `GetSelection()==tp3DEditor/tpPreview` lambda in MainFrame (673, 2051-2066, 3027-3956), slice_plate 2100-2101, select_tab(wxPanel*) 4789, preview_only_hint 1738, switch_to_device_page 1062-1069, BBLTopbar 1077-1085.

**Tab.cpp:8067-8099** (FindPage / InsertPage / SetSelection(last)) only runs on an FFF↔SLA technology switch. Leave it unchanged.

**GUI_ObjectList.cpp:2173/2229/2294** use `GetPage(0)` only as a dialog parent: that is the plater in A and the webview in B, as today.

### 1.3 MainFrame page creation and handlers

- **`init_tabpanel`:**
  - Variant A: delete lines 1529-1537 (`m_webview` creation, Bind, AddPage) but keep the `m_param_panel` block inside `if (wxGetApp().is_editor())`.
  - Variant B: keep them.
  - Both: delete 1566-1568 and 1575-1580; keep 1570-1573 (Device AddPage plus `SetId(MainFrame::tpDeviceMgr)`).
- **`update_layout` (1142-1148):** keep only the two plater `InsertPage(tp3DEditor/tpPreview…)` calls.
- **Page-changed handler (1440-1457):** reduce it to the following.

```cpp
if (m_topbar) {
    size_t topbar_sel = static_cast<size_t>(sel);
    if (panel == m_printer_mgr_view)
        topbar_sel = static_cast<size_t>(tpDeviceMgr); // keeps the G-code viewer layout right
    m_topbar->SetSelection(topbar_sel);
}
```

  In Variant B also keep `else if (panel == m_webview) topbar_sel = tpHome;`.
- **`wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED` handler (1151-1195):** keep the tp3DEditor, tpPreview and tpDeviceMgr branches. Every path now reaches `evt.Skip()`.
- **`select_tab(size_t)` (4816-4865):** after the deletions, the selection is just `new_selection = tab == size_t(-1) ? m_last_selected_tab : tab`.
- **Online/AI members (real deletion):**
  - Delete `m_webmodellibrary_view`, `m_ai_creation_view` and their getters (MainFrame.hpp:380-382, 415-416).
  - Delete `ensure_web_model_view_ready` (MainFrame.cpp:108-120) and the `#include WebModelLibraryView.hpp` at :93.
  - Delete `update_model_webviews_user_agent` (MainFrame.cpp:1344-1350) and its 5 call blocks in GUI_App.cpp (2060-2062, 2106-2108, 5817-5819, 8194-8196, 14487-14489); delete each whole `if (mainframe) {…}`.
  - Delete `GUI_App::reload_region_sensitive_views` (GUI_App.cpp:1886-1916, GUI_App.hpp:415) and its callers at GUI_App 8199 and 14492, Preferences.cpp:371 and InstanceCheck.cpp:518.
  - Delete `get_ai_creation_webaddress` (GUI.cpp:682-696, GUI.hpp:83).
- **`destroy_webviews_for_recreate` (1337-1341):** delete the `m_webmodellibrary_view` and `m_ai_creation_view` lines. Keep `m_webview` (it stays null in A), `m_printer_view` and `m_printer_mgr_view`. `recreate_GUI` (GUI_App.cpp:6184-6209) needs nothing else; it already selects `tp3DEditor`.
- **G-code viewer mode (`!is_editor`):**
  - No webview is created. Variant A gives Prepare 0, Preview 1, Device 2, which is correct.
  - Variant B gives Device 0, Prepare 1, Preview 2. `tpDeviceMgr=3` still resolves through the SetId fallback, and the page-changed handler's `m_printer_mgr_view` mapping keeps the topbar right.
  - **[INFERENCE]** This mode is only reached with `--gcodeviewer`.

### 1.4 BBLTopbar insertions (1056-1061)

Both variants: `pCtr->InsertPage(MainFrame::tp3DEditor, _L("Prepare"), 0); pCtr->InsertPage(MainFrame::tpPreview, _L("Preview"), 0); pCtr->InsertPage(MainFrame::tpDeviceMgr, _L("Device"), 0);`

### 1.5 Variant A only: `m_webview` uses that must be null-checked

Keep the member `WebViewPanel* m_webview{nullptr}` (it stays null; F7 deletes it). Add guards at these 8 unguarded uses:

- GUI_App.cpp:6612 (`SetLoginPanelVisibility`)
- GUI_App.cpp:12008 (`switch_staff_pick`)
- GUI_App.cpp:12481 (`update_mode`, inside `#if !AUTO_CONVERT_3MF`)
- GUI_App.cpp:12517 (`update_internal_development`)
- MainFrame.cpp:2860-2862 (`on_sys_color_changed`; `CUSTOM_COMMUNITY_ENABLE` is true)
- MainFrame.cpp:4055
- MainFrame.cpp:5241
- MainFrame.cpp:5265 (`SendRecentList`)

The others are already guarded: GUI_App 1878 and 8080, MainFrame 1406, 4980 and 5316, Preferences 1877 and 1895, ConfigRelateDialog 3498, PrinterPresetConfig 416, WebGuideDialog 1553.

Also in A, delete the "Default Page" radio:
- `create_default_page_radiobutton` (Preferences.cpp:1255-1282, Preferences.hpp:136)
- its use at Preferences.cpp:1535 and 1564-1565

Risk **[INFERENCE]**: without Home, a clean install with only default printers loses the onboarding guide. Printers can still be added from the sidebar.

### 1.6 Grep check after the change

`git grep -nE "\b(tpOnlineModel|tpMonitor|tpMultiDevice|tpProject|tpCalibration|tpAuxiliary|toDebugTool|tpAICreation)\b" -- src` must return nothing. In Variant A, also add `tpHome`.

---

## Item 2 — Remove the "Pro ⇌ AI" switch and force easy mode off

- **`easy_mode()`:** GUI_App.cpp:14626-14629 body becomes `return false;`. Every reader goes through `easy_mode()`, including MCPChatPanel.cpp:178 and 2494; nothing else reads `easy_print_mode` directly.
- **AppConfig.cpp:507-508** (inside `#ifdef _WIN32`): replace with `set("easy_print_mode", "0"); m_storage.erase("pro_mode");`. This clears a stale "1" and the floating-button position keys (`m_storage` is the member at AppConfig.hpp:355).
- **BBLTopbar.cpp:**
  - Delete `class EasyModeSwitchCtrl` (541-669) and its creation at 1110-1114, including the `m_action_spacer_items` push.
  - Delete the feedback block 1116-1125 and the More item 1126-1128.
  - Delete the Binds at 1191-1192.
  - Delete `OnMore` (1732-1748) and `OnFeedback` (1590-1602).
  - In `Rescale`, delete 1839-1851, 1861-1865 and 1875-1878.
  - `SetActionsCollapsed` (1671-1687) keeps its signature with an empty body: there is nothing left to collapse, so "overflow" equals "compact". Keep it so `TopbarLayout` and its test (tests/gui/topbar_layout_tests.cpp) stay valid.
  - **Must-do:** UpdateResponsiveLayout:1691 guard becomes `if (m_responsive_layout_busy || !m_tabCtrol) return;`. If `!m_more_item` stays in the guard, the responsive layout silently stops working.
  - Enum IDs `ID_FEEDBACK_BTN` and `ID_TOP_MORE` may stay or go.
- **BBLTopbar.hpp:** delete `OnFeedback` (51), `OnMore` (105), `m_feedback_item` (142), `m_easy_mode_switch_ctrl`/`_item` (145-146), `m_more_item` (148) and `m_action_spacer_items` (149). Keep `m_feedback_separator_item`: `GetTitleDisplayRect` (2275-2289) uses it as the title's right edge.
- **Preferences:** delete the creation at Preferences.cpp:1531, the sizer adds at 1554-1555, the function at 1900-1932, the declaration at Preferences.hpp:119 and the member `m_easy_mode_checkbox` (Preferences.hpp:101).
- **GUI_App.cpp:8234-8241:** delete the `set_begin_guide_version_mode` branch. The homepage then sends an unknown command, which is ignored.
- **What `Update_easy_mode_flag` must still do:** its only remaining caller is `post_init` (GUI_App.cpp:2172).
  - It still has to call `canvas->on_easy_mode_switch()`. In the Pro branch (GLCanvas3DSimple.cpp:2134-2153) that hides the MCP chat and syncs the object list's last printer preset.
  - It still has to call `plater->update()`, refresh the Print/Filament/Printer tabs (`update_dirty`, `reload_config`, `update`) and refresh the topbar.
  - Recommendation: keep the body, but drop the `if (!easy_mode())` wrapper (always true now) and keep `NotifyAIChatEditionChanged()` until F3.
  - `GUI_App.hpp:313 m_is_easy_mode` is unused and can be deleted.
  - The ~200 remaining `easy_mode()` branches stay as dead code until F7.

---

## Item 3 — Feedback, CrealityCloud upload, floating AI button

- **User Feedback:**
  - The topbar button is removed with item 2.
  - Help menu: delete MainFrame.cpp:2961-2969 (the `#if … User Feedback … #endif` block).
  - Delete `user_feedback_website` (GUI.cpp:760 to its closing brace, GUI.hpp:85); it has no other callers.
  - This overlaps F1 point 5; do it once.
- **"Upload (3mf) to CrealityCloud":** delete MainFrame.cpp:3266-3270. Keep `fileMenu->AppendSeparator()` at 3272: it now separates Export from Quit, which is correct. `GUI_App::open_upload_3mf` (12708) is left to F4; its other caller, `BBLTopbar::OnUpload3mf` (1903), is not bound.
- **Floating AI button on the 3D canvas:**
  - Delete the call at GLCanvas3D.cpp:11510.
  - In GLCanvas3D.hpp, delete the members at 724-729, the declaration at 1410 and the declaration at 1457.
  - In GLCanvas3DSimple.cpp, delete `_ensure_ai_chat_logo_tex` (1219-1238), `_render_ai_chat_toggle` (1963-2101) and the constants at 160-163. Keep `parse_layout_float`, which the easy-mode panel at 1557 still uses.
  - The `pro_mode` AppConfig keys are erased (item 2).
  - Optional: `git rm resources/web/image/ai_chat_float_logo.svg` (referenced only by that code and a doc).

---

## Item 4 — Wider sidebar filament cards (FilamentPanel)

Verified facts:
- Cards are `FilamentItem` panels whose two `FilamentButton` children are placed absolutely (`update_button_size`, FilamentPanel.cpp:2044-2066).
- Each card is created at `FromDIP(FILAMENT_BTN_WIDTH)` (110×46, FilamentPanel.h:21-22, created at .cpp:3183).
- Cards sit in a `wxWrapSizer` (2170) inside a scrolled window; `update_scroll_height` (3235-3289) sizes it with up to 5 visible rows.
- The label is truncated once in `update()` (2010-2036), using the button's current width.
- "Small" state is dead code: `m_small_count == m_max_count == 64`, and `add_filament` refuses the 64th card. Leave it untouched.
- Mixed-filament rows are a separate sidebar panel (`update_mixed_filament_panel`) and are not affected.

Changes:

1. **FilamentPanel.h:** after line 22 add `#define FILAMENT_MAX_COLUMNS 3` and `#define FILAMENT_VISIBLE_ROWS 5`. In `FilamentItem`, add public `void set_card_width(int width);`, and private `void update_param_label(); wxString m_display_name; wxString m_full_preset_name;`.
2. **`FilamentItem::update` (FilamentPanel.cpp):** replace 2010-2036 with `m_display_name = current_selection; m_full_preset_name = from_u8(preset->name); update_param_label();`.
3. **Constructor:** after 1753, set `m_display_name = from_u8(filament_type); m_full_preset_name = preset ? from_u8(preset->name) : wxString();`, then call `update_param_label();` after `SetSizer` (1755).
4. **New functions:**

```cpp
void FilamentItem::set_card_width(int width)
{
    const wxSize target(width, FromDIP(FILAMENT_BTN_HEIGHT));
    if (GetSize() == target && GetMinSize() == target)
        return;
    SetMinSize(target);   // wxWrapSizer lays out on min size
    SetSize(target);
    update_button_size();
    update_param_label(); // re-truncate for the new width
    Refresh();
}

void FilamentItem::update_param_label()
{
    if (!m_btn_param_list) return;
    wxClientDC dc(m_btn_param_list);
    wxFont font = m_btn_param_list->GetFont();
    font.SetPointSize(Label::Body_13.GetPointSize());
    font.SetWeight(wxFONTWEIGHT_NORMAL);
    dc.SetFont(font);
    const int max_w = m_btn_param_list->GetSize().GetWidth() - FromDIP(16) - FromDIP(10);
    wxString label = m_display_name;
    int w = 0;
    dc.GetTextExtent(label, &w, nullptr);
    if (w > max_w) {
        while (w > max_w && !label.empty()) { label.RemoveLast(); dc.GetTextExtent(label + "...", &w, nullptr); }
        label += "...";
    }
    m_btn_param_list->SetLabel(label);
    wxString tip = m_display_name;            // full name, also when truncated
    if (!m_full_preset_name.empty() && m_full_preset_name != m_display_name)
        tip += "\n" + m_full_preset_name;     // preset name differs from the alias display name
    m_btn_param_list->SetToolTip(tip);
    m_btn_param_list->Refresh();
}
```

5. **`update_scroll_height` (3235-3289):**
   - Replace the columns computation (3240-3251) with:
     - `slot_min = FromDIP(FILAMENT_BTN_WIDTH + 8)`
     - `fit = max(1, width / slot_min)`
     - `columns = clamp(min({max<size_t>(count,1), FILAMENT_MAX_COLUMNS, fit}), 1)`
     - When `rows > FILAMENT_VISIBLE_ROWS`, recompute with `width - scrollbar_width`.
   - Replace the literal 5 with `FILAMENT_VISIBLE_ROWS` (3245, 3247, 3252, 3257, 3262).
   - Right after `content_width` is known (3259) and before `m_filament_content->Layout()` (3266), insert:

```cpp
const int border = FromDIP(4); // sizer border of each card (add_filament)
const int card_width = std::max(FromDIP(FILAMENT_BTN_WIDTH),
                                content_width / int(std::max<size_t>(columns, 1)) - 2 * border);
for (FilamentItem* item : m_vt_filament)
    if (item) item->set_card_width(card_width);
```

   Measuring against the real `content_width` (after the scrollbar decision) and rounding down guarantees `columns` cards fit on one wrap row.

Edge cases:
- **One filament:** the card spans the full width, as requested. Optionally cap at `2*FILAMENT_BTN_WIDTH`.
- **Sidebar narrower than 3 minimum slots:** fewer columns, and the card never goes below 110 DIP.
- **More than 15 filaments:** the scrollbar appears and the width is recomputed without it.
- **DPI:** `FilamentPanel::msw_rescale` resets each item to the fixed size, then `update_scroll_height` re-applies the width. The early return in `set_card_width` prevents size-event loops.
- **New card:** `add_filament` already calls `update_scroll_height`.
- **Dropdown:** `layout_filament_popup` uses the card rect and a fixed list width, so nothing changes.

---

## Item 5 — Wider "Temp camera" card (web patch)

Verified facts:
- The card is the `PrintParams` SFC: root `{class:"print-params-item"}`, scope `data-v-9fc1b45c`.
- The CSS is embedded in a JS template literal (backtick) inside BZCDzYbb.js.
- The card has fixed widths: `.print-params-item{width:180px;min-width:156px}`, and `@media (max-width:1380px){width:130px;min-width:80px}`.
- The Advisor text sits in the right `el-col span:12` (patch 7.2 adds `class:"target-temp advisor-chamber"`, `white-space:nowrap`), so "Preriscaldo mm:ss" overflows about an 82 px half column.
- The anchor regex below matches exactly once (checked with Node).
- `:has(` does not occur anywhere in the file today.

New entry for tools/web-patches/patches.mjs (after "10"):

```js
  // 11 --------------------------------------------------------------------
  {
    id: "11",
    title: "Wider chamber card for the Advisor state",
    dir: DEVICE,
    fileContains: 'registerHandler("print_control"',
    requires: ["7"],
    edits: [
      {
        marker: "/*PA:11*/",
        applied: /\/\*PA:11\*\/|:has\(\.advisor-chamber\)/,
        // end of the PrintParams media query (inside the CSS template literal: no backticks, no ${)
        find: new RegExp(String.raw`@media screen and \(min-width:80px\)and \(max-width:1380px\)\{\.print-params-item\[data-v-(?<h>[0-9a-f]+)\]\{width:130px;min-width:80px\}\[data-v-\k<h>\] \.el-input__wrapper\{padding:1px 3px!important\}\}`, "g"),
        replace: (m, v) => m[0] + fill(String.raw`/*PA:11*/.print-params-item[data-v-@@h@@]:has(.advisor-chamber){width:240px;min-width:240px}.print-params-item[data-v-@@h@@]:has(.advisor-chamber) .el-row>.el-col:first-child{flex:0 0 36%;max-width:36%}.print-params-item[data-v-@@h@@]:has(.advisor-chamber) .el-row>.el-col:last-child{flex:0 0 64%;max-width:64%}`, v),
      },
    ],
  },
```

- **Why CSS only:** the same layout applies above and below the 1380 px breakpoint, and no Vue props change. Changing the root props instead would not update without a patch flag; the block root skips the props diff.
- **Width math:** 240 − 16 padding gives about 134 px for the Advisor column, enough for "Preriscaldo 59:59" and "Soak -09:59".
- **`:has()`** needs Chromium 105+ (WebView2 is evergreen). **[INFERENCE]** On an older runtime the rule is ignored and the layout is as today.
- **Wrapper:** `.print-params-wrapper` is a non-wrapping flex row, so the other cards shrink toward their `min-width`.
- **Docs:** add section "### 11" to doc/MCP/web-bundle-patches.md.
- **Verify:** `node tools/web-patches/apply-web-patches.mjs --check`, then `--apply`; open the Device tab at window widths above and below 1380 px.

---

## Item 6 — Scheduled preheat ("voglio stampare alle 15")

Verified basis:
- `/v1/preheat` is `PrintAdvisorServer::preheat` (PrintAdvisorServer.cpp:1901-1966, registered at 1037). Being a POST, it is automatically mutating and exclusive.
- The monitor runs a 30 s GUI-thread `wxTimer` (`set_tick_timer`, PrintAdvisorMonitor.cpp:1797) that calls `Impl::tick()` (1483). State lives under `Impl::mutex`, and side effects run through `Actions` / `run()`.
- Bed auto-off uses `WebSocketProxy::SendToDevice(address, bed_setpoint_message(v))` directly. The manual start goes through the Device page (`send_device_control`), which requires the selected printer.
- History entries `{"action":"monitor_event","event":"preheat_target"|"preheat_plateau"|"preheat_print_started","address",detail:{minutes,chamber,baseline_chamber,bed_target,chamber_target,max_minutes}}` are written by `end_preheat` (822-826).
- `read_history` and `ConflictError` are local to PrintAdvisorServer.cpp (anonymous namespace, 77-751). That is why the route goes there.

### 6.1 Monitor (PrintAdvisorMonitor.hpp / .cpp)

**Header additions:**

```cpp
struct PreheatSchedule {
    std::string address; long long start_at_ms{0}; long long ready_at_ms{0};
    double bed_target{0}; double chamber_target{-1}; int max_minutes{60};
    int lead_minutes{0}; std::string lead_source; // history | default | user
};
// Arms one schedule (throws when stopping or schedule_conflict() is not empty). At start_at_ms the
// monitor switches the bed on WITHOUT a dialog only if the printer reports fresh, idle, bed off,
// no upload, no followed preheat and no cool-down on it; then it is an ordinary preheat (same
// auto-off), kept hot at least until ready_at + 20 min (still capped by max_minutes). Memory only.
void schedule_preheat(const PreheatSchedule& schedule);
bool cancel_scheduled_preheat();           // false when none was waiting
std::string schedule_conflict() const;     // a schedule waiting, or a preheat followed
```

Also add stubs for all three in the `#else // GUI_SIMPLE` section (about 2491-2498).

**.cpp changes:**

- **Constants:** `SCHEDULE_WAIT_DATA_MS = 5*MINUTE_MS` (wait for fresh data or a send after the due time) and `SCHEDULE_LATE_MAX_MS = 10*MINUTE_MS` (never heat later than this, for example after PC sleep).
- **`struct Preheat`:** add `long long hold_until_mono{0};`.
- **`update_preheat`:** at 903 and 907, use `off.grace_until_mono = std::max(now.mono + AUTO_OFF_GRACE_MS, preheat.hold_until_mono);`. A manual preheat keeps 0, so its behaviour is unchanged.
- **`Impl`:** add `struct Schedule { bool waiting{false}; PrintAdvisorMonitor::PreheatSchedule r; long long armed_ms{0}, ended_ms{0}; std::string outcome{"none"}; } schedule;`.
- **Refactor `arm_preheat` (1868-1906):** move it into `Impl::start_preheat_locked(Actions&, address, bed_target, chamber_target, max_minutes, const json& telemetry, const Now&)`. `arm_preheat` calls it with `telemetry_of(address)` read before the lock, exactly as today.
- **New `Impl::check_schedule(Actions&, const Now&)`**, called at the end of `tick()` and of `on_telemetry` (before `maybe_push`):
  - The trigger uses wall-clock time, because `wxTimer` does not run during sleep. It fires when `now.wall >= r.start_at_ms`.
  - If more than `SCHEDULE_LATE_MAX_MS` late: outcome `missed_late`.
  - No fresh connected device (`FRESH_FOR_ACTION_MS`): wait, and after `SCHEDULE_WAIT_DATA_MS` outcome `missed_no_telemetry`.
  - `!is_idle(state)`: outcome `refused_busy`.
  - `upload_in_progress`: wait, then `refused_busy`.
  - `targetBedTemp0 > 0`: outcome `refused_bed_on`.
  - `follows_preheat()`: outcome `refused_preheat`.
  - A cool-down on that address, or a `cooldown_ends` entry: outcome `refused_cooldown`.
  - `SendToDevice(r.address, bed_setpoint_message(lround(bed)))` fails: retry, then `send_failed`.
  - On success: `start_preheat_locked(...)`, set `hold_until_mono = now.mono + max(0, ready_at - now.wall) + AUTO_OFF_GRACE_MS`, outcome `started`, history entry `{"action":"preheat","scheduled":true,...}`.
  - Every non-started outcome raises an `add_event` notification saying the bed was NOT switched on. `started` notifies "Preriscaldo avviato".
- **`disarm_all`:** if a schedule is waiting, set outcome `stopped` with the notification "Preriscaldo programmato annullato: Print Advisor disattivato".
- **`preheat_status_locked`:** add a `"schedule"` object with waiting, address, ready_at_ms, start_at_ms, bed_target, chamber_target, max_minutes, lead_minutes, lead_source, outcome, armed_ms and ended_ms.

### 6.2 Server (PrintAdvisorServer.cpp / .hpp)

- **Registration:**
  - Header declaration: `nlohmann::json preheat_schedule(const std::string& body);`.
  - `add("/v1/preheat_schedule", "POST", ...)` next to line 1037. It is mutating and exclusive by default, so a cancel also gets 409 while a dialog is open, the same as the `slow_cooldown` cancel.
  - Add `#include <ctime>`.
- **`preheat()`:** when `bed_temp > 0` and a schedule is waiting, throw `ConflictError("C'è un preriscaldo programmato per le HH:MM: annullalo prima (cancel_scheduled_preheat)")`.
- **Handler, `{cancel:true}`:** call `monitor.cancel_scheduled_preheat()` and `append_history({"action":"preheat_schedule_cancel"})`, then return success plus the schedule. A preheat that already started is not touched; say so in the response.
- **Handler, scheduling:**
  1. Validate `bed_temp` (1-120 and machine limits), `target_chamber_temp` and limits exactly as `preheat()`. Factor that validation into a shared helper.
  2. Parse `ready_at` as "HH:MM" (next local occurrence) or "YYYY-MM-DDTHH:MM", using `mktime` with `tm_isdst=-1`. It must be in the future and at most 24 h ahead.
  3. `selected_device_snapshot()`; the state must be in {0,2,3,4}.
  4. ConflictError if `preheat_conflict(addr)` or `schedule_conflict()` is not empty.
  5. Lead time: `lead_minutes` (10-110, source "user"), or `estimate_preheat_lead(addr, bed, chamber_target, chamber_now)`.
     - Read `read_history(2000)`, keep monitor events of the same address with |bed_target − bed| ≤ 5.
     - With a chamber target: use `preheat_target` events whose chamber_target ≥ target − 1.
     - Without one: use `preheat_target` or `preheat_plateau` events.
     - Prefer samples whose baseline is within ±5 °C of the current `boxTemp` when there are at least 2 of them.
     - Lead = median + 5, clamped to 10-90.
     - With no samples: 35 min, source "default". That figure comes from the K1C cold-room run of 2026-10-19 (30 °C at 23 min, about 32 °C at 35 min).
     - Return a warning when past plateaus with this bed temperature stopped below the target.
  6. `start_at = ready_at − lead`. If `start_at < now + 60 s`, refuse: "troppo tardi per programmare: usa preheat_chamber ora (camera pronta verso HH:MM)".
  7. `max_minutes` defaults to `clamp(lead+30, 30, 120)`. A value given below `lead+10` is refused.
  8. `ask_user("Preriscaldo programmato", question, m_callback_stopping, {{"Accensione piatto","—","HH:MM"},{"Pronto per","—","HH:MM"},{"Piatto","—","N °C"}})`. Question text:

     > "Preriscaldare la camera di <addr> per le HH:MM?\n\nIl piatto verrà acceso a N °C alle HH:MM (anticipo L min, <stimato da K preriscaldamenti / valore predefinito>) SENZA altre conferme, solo se in quel momento la stampante è collegata (scheda Dispositivo aperta), inattiva e con il piatto spento. Chiudi il coperchio. Il piatto si spegne da solo dopo M min se la stampa non parte, e non prima di 20 min dopo le HH:MM. La programmazione si annulla se Print Advisor o Creality Print vengono chiusi."

  9. After the dialog: `check_device_unchanged`, re-check both conflicts, re-check `start_at ≥ now + 30 s` (the dialog can stay open up to 600 s), then `monitor.schedule_preheat`, then `append_history`.

### 6.3 Sidecar (tools/print-advisor-mcp/src/tools/monitor.ts)

Add `schedule_preheat`, registered after `preheat_chamber`:

```ts
inputSchema: z.object({
  ready_at: z.string().regex(/^(\d{4}-\d{2}-\d{2}T)?([01]\d|2[0-3]):[0-5]\d$/).describe('Local PC time when the user wants to start printing: "HH:MM" (next occurrence) or "YYYY-MM-DDTHH:MM"'),
  bed_temp: z.number().int().min(1).max(120),
  target_chamber_temp: z.number().min(20).max(70).optional(),
  lead_minutes: z.number().int().min(10).max(110).optional(),
  max_minutes: z.number().int().min(5).max(120).optional(),
})
```

- It calls `requestApp('/v1/preheat_schedule', {...})` with the default mutation timeout of 610 s.
- Its description must state:
  - the lead estimate and the default 35 min;
  - that the bed switches on later with NO dialog, and only when the printer is idle, the bed is off and the Device tab is open;
  - that the auto-off rules are the same as `preheat_chamber`;
  - one schedule at a time;
  - that it is cancelled when Print Advisor or Creality Print stops (it does not survive a restart);
  - follow with `get_preheat_status.schedule`;
  - the `confirmationNote`.

Add `cancel_scheduled_preheat` with `{}` input: it posts `{cancel:true}`, shows no dialog, and does not switch off a preheat that already started (use `preheat_chamber` with `bed_temp` 0).

Also update:
- the `get_preheat_status` description (add the schedule field and its outcomes);
- the server `instructions` (index.ts:276) exceptions sentence;
- the README tool list (README.md:22);
- `scripts/tools-expected.json` via `npm run smoke -- --update-expected` (the count goes up by 2).

**Restart:** I recommend that a schedule does not survive. It is cancelled on server stop with a notification, as specified above. A missed schedule never heats late (10 min cap).

### 6.4 Safety documentation

This is a new "one confirmation, delayed action" exception. Add it to the CLAUDE.md "Eccezioni esplicite" list and the README. **This relaxes a safety rule, so the user must approve it explicitly.**

---

## Compile risks

- MainFrame.hpp enum change: every `tpHome` use must be gone in A, otherwise the build fails. Simple and toolcalls files (MCPToolCallsObject, SlicerBridgeState, EasyPrintSender) are compiled because `GUI_SIMPLE` is ON.
- BBLTopbar: remove every reference to `m_more_item`, `m_easy_mode_switch_*`, `m_feedback_item` and `m_action_spacer_items`.
- GLCanvas3D.hpp member removals must match GLCanvas3DSimple.cpp.
- `start_preheat_locked` is called under `Impl::mutex`: do not call the public `arm_preheat` from `tick()`, it would deadlock.
- The patches.mjs CSS must not contain a backtick or `${`.
- `from_u8` is already used in FilamentPanel.cpp.

## Verification (one build)

1. `build_exe_only.bat`.
2. The grep check from 1.6.
3. `apply-web-patches.mjs --check`.
4. Sidecar `npm run typecheck` and `npm run smoke`.
5. Manually:
   - only 3 tabs, with clicks plus Ctrl-shortcuts working in Prepare, Preview and Device;
   - "Invia" jumps to Device;
   - change language (`recreate_GUI`);
   - Preferences has no Easy-mode checkbox (and in A no default-page radio);
   - narrow the window: topbar Compact works and there is no More button;
   - no floating AI button; File menu has no cloud upload; Help menu has no feedback;
   - filament cards with 1, 2, 3, 4 and 16 filaments, at 100%/150% DPI, with full-name tooltips;
   - Device card shows "Preriscaldo mm:ss" without overflow;
   - schedule a preheat a few minutes ahead, then cancel it;
   - let one start only with the printer idle and no print running.
6. G-code identical to F0 (no engine change).

## Open decisions

1. Home: Variant A or B.
2. Approval of the scheduled-preheat safety exception (6.4).
3. Whether to cap the single-filament card width.
4. Optional: a follow-up web patch 6 extension to show "Programmato HH:MM" on the chamber card.
