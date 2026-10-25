# F7 "Easy mode e Home": implementation spec

Read-only analysis of branch `advisor`, HEAD `14528e0e` (2026-10-20). Nothing was edited, built or run. Line numbers are from the current files; F1-F5 edit several of the same files (`GLCanvas3D.cpp`, `GUI_App.cpp`, `Plater.cpp`, `MainFrame.cpp`), so every edit also gives a content anchor. **[INFERENCE]** marks what was not checked directly.

Assumptions (state after F1-F5):

- **F2** made `GUI_App::easy_mode()` return `false`, removed the "Pro" switch, the Preferences checkbox, the floating AI button (`_render_ai_chat_toggle`, `_ensure_ai_chat_logo_tex`, the `m_ai_chat_*` members) and, in Variant A, the Home tab (with `m_webview` kept as a null member).
- **F3** removed `MCPChatPanel.*`, `simple/toolcalls/*`, `CxAgentClientBridge.*`, `SAgentMqttBridge.*`, the cloud part of `simple/sendWorkflow/` (`AISendWorkflowService`, `AIProcess*`, `CxCloudPrint*`), and with them `GetEmbeddedAIChatPanel()`, `MCPChatWindow`, `NotifyAIChat*()`. Where this spec meets a line that uses those names, it says "if F3 left it".
- `GUI_SIMPLE` stays `ON` (Print Advisor needs it).

Kept, by decision: `simple/gpu`, `simple/filamentMapping`, `simple/bridge`, `simple/printAdvisor`, `simple/sendWorkflow/EasyPrintSender.*` (but see 1.4: it has no caller left after F3).

---

## Part 1. Easy mode

### 1.1 Files to delete

| File | Lines | What it defines |
|---|---|---|
| `src/slic3r/GUI/simple/GLCanvas3DSimple.cpp` | 2,162 | `GLCanvas3D::` members of the easy UI (main toolbar, device card, resident filament panel, object drawer, overlays, `on_easy_mode_switch`, ...) |
| `src/slic3r/GUI/simple/GLObjectManipulateToolbarSimple.cpp` | 1,251 | `GLCanvas3D::_init_object_manipulate_toolbar_simple`, `_render_object_manipulate_toolbar_simple`, `_render_orient_menu_simple` and file-static helpers |
| `src/slic3r/GUI/simple/GLToolbarSimple.cpp` | 735 | `GLToolbarItem::render_icon_top_label_bottom_px`, `is_always_enable`; `GLToolbar::get_rendered_right_edge_px`, `render_horizontal_simple`, `set_layout_gap_scale`, `set_layout_step_scale`, `get_height_horizontal_simple`, `get_width_horizontal_simple`, `update_hover_state_horizontal_simple`, `get_last_bg_rect_px`, `update_items_state_simple`, `reset_item_state_simple`, `contains_mouse_horizontal_simple` |
| `src/slic3r/GUI/simple/GLSimpleUtils.hpp` | 67 | inline helpers; included only by `SimpleModelMgr.cpp` and the deleted UI files |
| `src/slic3r/GUI/simple/GUI_ObjectListSimple.cpp` | 68 | **empty** (only includes and an empty namespace) |
| `src/slic3r/GUI/simple/filamentSimplePage.cpp` | 214 | `GLCanvas3D::_draw_filament_menu_contents` |
| `src/slic3r/GUI/simple/SupportSimple.cpp/.hpp` | 216 + 18 | `SupportSimple::render_simple_input_window` |
| `src/slic3r/GUI/simple/PrintSettingsPanel.cpp/.hpp` | 399 + 165 | class `PrintSettingsPanel` (member `GLCanvas3D::m_print_settings_panel`) |
| `src/slic3r/GUI/simple/DeviceListSimple.cpp/.hpp` | 1,570 + 142 | `SimpleDeviceMgr`, `Simple_Device_List_*`, `render_device_list_popup`, file-local 3MF preset helpers |
| `src/slic3r/GUI/simple/SimpleModelMgr.cpp/.hpp` | 79 + 35 | `SimpleModelMgr` |
| `src/slic3r/GUI/simple/UploadDialog.cpp/.hpp` | 68 + 30 | class `UploadDialog`; **never instantiated** (only `#include` at `Plater.cpp:232`) |

Total about 7,300 lines.

```bash
cd src/slic3r/GUI/simple
git rm -q -- GLCanvas3DSimple.cpp GLObjectManipulateToolbarSimple.cpp GLToolbarSimple.cpp GLSimpleUtils.hpp \
  GUI_ObjectListSimple.cpp filamentSimplePage.cpp SupportSimple.cpp SupportSimple.hpp \
  PrintSettingsPanel.cpp PrintSettingsPanel.hpp DeviceListSimple.cpp DeviceListSimple.hpp \
  SimpleModelMgr.cpp SimpleModelMgr.hpp UploadDialog.cpp UploadDialog.hpp
cd ../../../..
```

CMake, `src/slic3r/CMakeLists.txt` inside `if(GUI_SIMPLE)` (`:816`): delete lines 818 (`GLSimpleUtils.hpp`), 819 (`GLToolbarSimple.cpp`), 820 (`GLCanvas3DSimple.cpp`), 821-822 (`DeviceListSimple`), 823 (`filamentSimplePage.cpp`), 824-825 (`SupportSimple`), 849 (`GLObjectManipulateToolbarSimple.cpp`), 850-851 (`PrintSettingsPanel`), 854-855 (`UploadDialog`), 856 (`GUI_ObjectListSimple.cpp`), 857-858 (`SimpleModelMgr`). Keep 826-848 (`filamentMapping`), 852-853 (`EasyPrintSender`), the `bridge` lines, and `:1002-1004` (`target_include_directories(libslic3r_gui PRIVATE .../GUI)`, used by the kept files).

`localization/i18n/list.txt`: delete lines 646, 668-673, 688, 690, 691, 693-695, 719, 732, 733 (the 16 paths above).

### 1.2 Functions of the deleted files that are reached from other code

Every function defined in the deleted files was searched with `git grep -n` on its name in `src`, excluding the deleted files and the headers. Only these have callers outside:

| Function (defined in) | Caller outside | Reached in Pro mode? | Action |
|---|---|---|---|
| `GLToolbarItem::is_always_enable` (GLToolbarSimple) | `GLToolbar.cpp:1910` | no, inside `if (easy_mode())` | collapse the branch (1.5) |
| `GLToolbar::render_horizontal_simple` | `GLToolbar.cpp:640` | no | collapse |
| `GLToolbar::get_width_horizontal_simple` | `GLToolbar.cpp:758` | no | collapse |
| `GLToolbar::update_hover_state_horizontal_simple` | `GLToolbar.cpp:938` | no | collapse |
| `GLToolbar::contains_mouse_horizontal_simple` | `GLToolbar.cpp:1189` | no | collapse |
| `GLToolbar::update_items_state_simple` | `GLCanvas3D.cpp:3765-3766` | no | collapse |
| `GLToolbar::get_rendered_right_edge_px`, `set_layout_gap_scale`, `set_layout_step_scale`, `get_height_horizontal_simple`, `get_last_bg_rect_px`, `reset_item_state_simple`, `GLToolbarItem::render_icon_top_label_bottom_px` | none | n/a | delete the declarations |
| `GLCanvas3D::_init_ui_simple` | `GLCanvas3D.cpp:8795` (`_init_toolbars`) | **yes, unconditional**: it also builds `m_easymode_main_toolbar` and `m_object_manipulate_toolbar` in Pro | delete the call |
| `GLCanvas3D::_render_overlays_easymode` | `GLCanvas3D.cpp:2438` | no | collapse |
| `GLCanvas3D::_reset_simple_toolbar_item_state` | `GLCanvas3D.cpp:4053` | no | delete the branch |
| `GLCanvas3D::easy_mode_on_scene_reloaded` (empty body) | `GLCanvas3D.cpp:3518`, `Plater.cpp:14807` | no | delete both |
| `GLCanvas3D::on_easy_mode_switch` | `GUI_App.cpp:14653` (`Update_easy_mode_flag`) | **yes, at every start** (`post_init`, `GUI_App.cpp:2172`). Its Pro branch calls `MCPChatWindow::Hide()` (F3) and `obj_list->set_last_preset_name(preset_bundle->printers.get_selected_preset_name())` | keep the `set_last_preset_name` call in `GUI_App` (1.5) |
| `GLCanvas3D::open_filament_toolbar_popup` | `Gizmos/GLGizmosManager.cpp:253-255`, handler of the "Add filament" button of `GLGizmoSimplePaint` | `SimplePaint` is in Pro's gizmo list and answers the `N` shortcut when `MmuSegmentation` is not activable **[INFERENCE: not tested]**. In Pro the call only presses an item of the hidden easy toolbar, so it does nothing visible today | delete the handler (1.5) |
| `GLCanvas3D::close_device_list_popup` | `DeviceListSimple.cpp:1560` (deleted) | n/a | delete |
| `GLCanvas3D::open_device_list_popup` | `MCPChatPanel.cpp:649` (F3) | n/a | delete |
| `SimpleDeviceMgr` (DeviceListSimple) | `GUI_ObjectList.cpp:369`, `Plater.cpp:13180`, `SlicerBridgeActionsPrinter.cpp:329,442,513`, `EasyPrintSender.cpp:777,818` | the `GUI_ObjectList` and `Plater` uses are in easy branches; the bridge and sender uses are not | see 1.4 |
| `SimpleModelMgr` | `Plater.cpp:8536`, `:9995` | no (easy branches) | delete the branches |
| `PrintSettingsPanel` | member `GLCanvas3D::m_print_settings_panel` | never created outside the deleted files | delete the member and include |

Pro-file functions that exist only for easy mode (all verified):

- `GLCanvas3D::get_easy_mode_overlay_safe_left_px/right_px/bottom_px` (`GLCanvas3D.cpp:11718-11741`, declared `GLCanvas3D.hpp:999-1001`). They return 0 when easy mode is off. Users: `NotificationManager.cpp:291-292` and `:2910-2911`.
- `GLCanvas3D::enable_easymode_main_toolbar` (`GLCanvas3D.cpp:2015`, `GLCanvas3D.hpp:964`). Users: `GUI_Preview.cpp:77` and `:874`.
- `should_hide_easy_mode_scene_side_controls` (`GLCanvas3D.cpp:211-218`, anonymous namespace). Users: `:4538`, `:4598`, `:10507`, `:11863`.
- `s_simple_arrange_close_requested`, `consume_simple_arrange_close_requested`, `render_simple_arrange_close_button` (`GLCanvas3D.cpp:126`, `:164-209`). User: `:7051-7052`. `consume_...` is used only by the deleted `GLObjectManipulateToolbarSimple.cpp:120,1054`.
- `GLGizmoBase::GizmoImguiRenderSimpleCloseButton` (`GLGizmoBase.cpp:249-292`, `GLGizmoBase.hpp:249`). Users: `GLGizmoBase.cpp:244` (inside `GizmoImguiBegin`) and `GLGizmoClone.cpp:94`. Returns false at once when easy mode is off.
- `render_simple_gizmo_close_button` (`Gizmos/GizmoObjectManipulation.cpp:37-78`, file static). Users: `:659`, `:853`, `:990`.
- `GUI_App::startTour_Simple` (`GUI_App.cpp:3991-3993`, empty; `GUI_App.hpp:836`). User: `GUI_App.cpp:3997-3998`.

### 1.3 Members that only the deleted files use

Counted with `grep -c` in `GLCanvas3D.cpp` and `git grep` elsewhere: 0 uses outside the deleted files for `m_imgui_filament_panel`, `m_print_settings_panel`, `m_resident_filament_layout_state`, `m_resident_filament_resize_state`, `m_top_prefix_width_px`, `m_top_group_left_px`, `m_top_group_right_px`, `m_top_group_width_px`, `m_top_toolbar_visible_px`, `m_right_buttons_block_px`, `m_preset_panel_open`, `m_device_card_anchor_rect`, `m_device_card_anchor_valid`, `m_preset_icon_tex`, `m_cur_device_icon_path`, `m_supports_popup_open`, `m_preset_settings_open`, `m_filament_settings_open`, `m_supports_anchor_x_px`, `m_more_tools_expanded`, `PopupHAlign`. `m_easymode_main_toolbar` (6 uses) and `m_object_manipulate_toolbar` (16 uses) appear in `GLCanvas3D.cpp`, all handled in 1.5.

`m_imgui_filament_panel` holds an `ImGuiFilamentPanel` from the kept `simple/filamentMapping`. It is created only in the deleted `GLCanvas3DSimple.cpp:1609`, and `get_filament_panel()` (`GLCanvas3D.hpp:1344`) is used only by the deleted `DeviceListSimple.cpp:865` and by `AISendWorkflowService.cpp:2388` (F3). After F7, `ImGuiFilamentPanel` and most of `filamentMapping` (`ResidentFilamentMapping*`, `ImGuiSpoolWidget`, `ImGuiThumbnailPreview`, `ImGuiFilamentExtraPage`) have **no user** left; only `FilamentMappingService` (used by `SlicerBridgeActionFilament.cpp`), `SceneFilamentSourceSnapshotManager` (used by `Plater.cpp`) and `match_color` (used by `print_manage/App/SendToPrinter`, `Plater.cpp`) are live. The plan says keep the folder, so this spec keeps it compiled; deleting the dead half is a possible follow-up (see open questions).

### 1.4 `SimpleDeviceMgr` users that survive: SlicerBridge and EasyPrintSender

**SlicerBridge.** `SlicerBridgeActionsPrinter.cpp` uses `SimpleDeviceMgr` and `Simple_Device_List_Item_Data` only in `DoListPrinters` (`:303-378`) and `DoSelectPrinter` (`:380-576`) and their helpers in the anonymous namespace (`matches_device_identity` `:70`, `matches_device_name` `:84`, `matches_device_model` `:102`, `serialize_device_candidate` `:154`, plus `trim_copy`, `lower_copy`, `normalized_id`, `contains_ci`, `contains_normalized`, `get_string_arg`, `get_bool_arg`, `preset_exists`, `refresh_printer_config_ui` if they become unused). The actions are registered at `SlicerBridgeActionRegistry.cpp:78-113` (`ActionID::LIST_PRINTERS`, `ActionID::SELECT_PRINTER`).

Who executes them: only the AI chat (`CxAgentClientBridge.cpp:393,428-431,678,920`, `MCPToolCallsCommon.cpp:170-184`, `MCPToolCallsPrinter.cpp`, `MCPToolCallsRegistration.cpp:105-106`), all removed by F3. The Print Advisor calls only `START_SLICE`, `GET_PRESETS`, `GET_EDITED_CONFIG` (`PrintAdvisorServer.cpp:1033,1091-1092`) and the static `AnalyzeOverhang`/`BuildConfigSchemaArray`; `MainFrame.cpp:3000` calls `ExportConfigSchemaToFile`. No other caller of `SlicerBridge::Execute` exists outside the files F3 deletes.

Edit (recommended):

- delete the two `RegisterAction` blocks at `SlicerBridgeActionRegistry.cpp:78-113`;
- delete `DoListPrinters` and `DoSelectPrinter` (`SlicerBridgeActionsPrinter.cpp:303-576`) and their declarations (`SlicerBridge.hpp:117-118`);
- delete `#include "slic3r/GUI/simple/DeviceListSimple.hpp"` (`SlicerBridgeActionsPrinter.cpp:7`) and the helpers that are left unused: keep `trim_copy`, `lower_copy` and `get_string_arg` (used by `DoMovePrintHead`/`DoPrintControl`); the others go (the compiler warns about unused functions in an anonymous namespace; MSVC C4505 is level 4, so check by hand);
- keep `DoMovePrintHead` (`:199`) and `DoPrintControl` (`:250`), which do not use `SimpleDeviceMgr`;
- the constants `ActionID::LIST_PRINTERS` and `SELECT_PRINTER` in `SlicerAction.hpp:54-55` may stay (plain strings).

**EasyPrintSender (finding: no caller after F3).** The class is instantiated only by `AISendWorkflowService.cpp:2221,3502` and `MCPToolCallsRegistration.cpp:603` (both removed by F3). `Plater.cpp:231` and `print_manage/App/PrinterMgrView.cpp:75` only include the header; `PrinterMgrView.cpp:551` mentions it in a comment; `DeviceListSimple.cpp:1362` has a commented use. LAN printing in Pro goes through `Plater::priv::on_action_send_to_local_net_printer` (`Plater.cpp:14639-14659`), which opens `CxSentToPrinterDialog` from `print_manage/App/SendToPrinter`, not `EasyPrintSender`. Besides, `EasyPrintSender.cpp` includes `CxCloudPrintExecutor.hpp` (F4 deletes `CxCloudPrint*`) and `SatisfactionSurveyIntegration.hpp` (F1 deletes the survey), so F1/F4 must already have touched it.

Two choices:

- **E1 (recommended): delete `EasyPrintSender.cpp/.hpp`** with this phase: `src/slic3r/CMakeLists.txt:852-853`, `list.txt:726,728`, and the includes at `Plater.cpp:231` and `PrinterMgrView.cpp:75`. This contradicts the plan's "keep EasyPrintSender (LAN)", so it needs the user's OK; the LAN path that the user actually uses is `SendToPrinter`, which stays.
- **E2 (keep it, as the plan says):** replace the two `SimpleDeviceMgr::instance().get_device_list_data_simple(true)` loops (`EasyPrintSender.cpp:777` in a commented-out function and `:818` in `updatePrinterState(const std::string& ipOrMac)`) with a scan of `DM::DataCenter::Ins().GetData()["data"]["printerList"][*]["list"]`, deserialised with `DM::Device::deserialize` (the same source `SimpleDeviceMgr::rebuild_device_list`, `DeviceListSimple.cpp:1098-1115`, uses). Match `device.address == ipOrMac || device.mac == ipOrMac`; idle is `device.online && device.deviceState == 0` (the old code used `item.state`, which `SimpleDeviceMgr` filled from `deviceState`). Delete the include at `EasyPrintSender.cpp:24`.

### 1.5 `easy_mode()` readers and their edits

After F2 there are **51** readers of `easy_mode()` outside the files deleted here and by F3 (plus 4 in `GLCanvas3DSimple.cpp`, 6 in `MCPChatPanel.cpp`, 1 in `MCPChatPanelSAgentNative.cpp`, and the 2 of `BBLTopbar.cpp` removed by F2). Since `easy_mode()` is `false`, every edit keeps the Pro side exactly; nothing changes at run time.

**`src/slic3r/GUI/GLCanvas3D.cpp` (17 readers, plus helpers)**

| Line | Today | Edit (what is kept) |
|---|---|---|
| 64-65 | `#include "simple/filamentMapping/ImGuiFilamentPanel.hpp"`, `#include "simple/PrintSettingsPanel.hpp"` | delete both (66, `MCPChatPanel.hpp`, is F3's) |
| 126, 164-209 | `s_simple_arrange_close_requested`, `consume_simple_arrange_close_requested`, `render_simple_arrange_close_button` (`:173` reader) | delete |
| 211-218 | `should_hide_easy_mode_scene_side_controls` (`:215` reader) | delete the function and the anonymous namespace around it |
| 1450-1451 | constructor initialisers `m_easymode_main_toolbar(GLToolbar::Normal, "easymode_Main")`, `m_object_manipulate_toolbar(GLToolbar::Normal, "object_manipulate")` | delete |
| 2015 | `enable_easymode_main_toolbar` | delete |
| 2437-2440 | `if(easy_mode()) _render_overlays_easymode(); else _render_overlays();` | `_render_overlays();` |
| 2494-2504 | tooltip: easy branch with `m_easymode_main_toolbar`/`m_object_manipulate_toolbar`, else main toolbar + process toolbar | keep only the `else` body (two `if (tooltip.empty())` lines) |
| 3517-3519 | `if(easy_mode()) { easy_mode_on_scene_reloaded(); }` | delete |
| 3764-3769 | `on_idle`: easy toolbars, else `m_main_toolbar.update_items_state()` | `m_dirty \|= m_main_toolbar.update_items_state();` |
| 4052-4054 | `WXK_ESCAPE`: `if(easy_mode()) _reset_simple_toolbar_item_state();` | delete (keep `deselect_all(); break;`) |
| 4538 | `... && m_canvas_type == CanvasView3D && !should_hide_easy_mode_scene_side_controls())` | drop the last condition |
| 4598-4601 | `if (!should_hide_...()) { cliper->on_mouse_wheel(evt); }` | keep the two inner lines unconditionally |
| 4835 | `FindWindowByName(easy_mode() ? "##obj_drawer" : "##obj_tree")` | `FindWindowByName("##obj_tree")` |
| 4876-4896, 4912 | `if(easy_mode()) { easy toolbars } else { main toolbar; process toolbar }` | keep the body of the `else` (`:4897-4911`) without the braces |
| 7050-7052 | `const bool arrange_window_open = imgui->begin_with_drag(...); if (arrange_window_open) render_simple_arrange_close_button(*this);` | `imgui->begin_with_drag(_L("Arrange options"), ImGuiWrapper::TOOLBAR_WINDOW_FLAGS);` |
| 8762-8791 | `// simple object manipulation toolbar` block `if (easy_mode() && m_object_manipulate_toolbar.get_items_count() > 0) {...}` | delete |
| 8795-8796 | `if (!_init_ui_simple()) return false;` | delete |
| 10507-10523 | `_render_cliper`: `if (should_hide_...()) { reset cliper; clear clipping planes; return; }` | delete the block |
| 11388-11391 | `if(!easy_mode()) { m_main_toolbar.set_icons_size(size); }` | keep the inner line |
| 11684-11685 | `get_main_toolbar_offset`: `if (easy_mode()) return 0;` | delete |
| 11708-11709 | `get_input_window_render_left_pos`: `if (easy_mode()) return get_easy_mode_overlay_safe_left_px();` | delete |
| 11718-11741 | the three `get_easy_mode_overlay_safe_*_px` | delete |
| 11863-11867 | `_render_imgui_select_plate_toolbar`: `if (should_hide_...()) { m_render_preview = true; return; }` | delete |
| 13913-13916 | `_deactivate_orient_menu`: `if (m_object_manipulate_toolbar.is_item_pressed("orient")) {...}` | delete (keep the `m_main_toolbar` block) |
| 14361-14364 | `if(easy_mode()) { if (m_canvas_type == CanvasPreview) return; }` | delete |

**`src/slic3r/GUI/GLToolbar.cpp` (5)**

| Line | Edit |
|---|---|
| 639-642 | `render_horizontal(parent, type, scroll);` |
| 757-763 | `m_layout.width = get_width_horizontal();` |
| 937-943 | `update_hover_state_horizontal(mouse_pos, parent);` |
| 1188-1191 | `return contains_mouse_horizontal(mouse_pos, parent);` |
| 1909-1919 | `ret = true; item->set_state(GLToolbarItem::Disabled);` (the `else` body) |

**Other files**

| File:line | Today | Edit |
|---|---|---|
| `GCodeRenderer/BaseRenderer.cpp:1103` | `if (!s_preview_section_fold && !wxGetApp().easy_mode())` | `if (!s_preview_section_fold)` |
| `GCodeRenderer/BaseRenderer.cpp:3663-3664` | `if (easy_mode()) return;` | delete |
| `GUI_App.cpp:3991-3998` | `startTour_Simple()` (empty) and `if (easy_mode()) return startTour_Simple();` | delete both; delete `GUI_App.hpp:836` |
| `GUI_App.cpp:14626-14629`, `GUI_App.hpp:443` | `easy_mode()` (F2: returns false) | delete after every reader is gone |
| `GUI_App.cpp:14631-14679` `Update_easy_mode_flag` (`:14662` reader), `GUI_App.hpp:455`; caller `post_init` `:2172` | see below | rename and simplify |
| `GUI_Factories.cpp:907-918` | `const bool easy_mode = ...; m_add_volume_easy_mode_state = easy_mode; ... if (easy_mode) return;` | delete the three easy lines; delete `m_add_volume_easy_mode_state` (`GUI_Factories.hpp:108`) |
| `GUI_Factories.cpp:929`, `:939` | `[]() { return !wxGetApp().easy_mode() && obj_list()->is_instance_or_object_selected(); }` | `[]() { return obj_list()->is_instance_or_object_selected(); }` |
| `GUI_Factories.cpp:2170` | `if (allow_support_enforcers() != m_support_enforcer_menu_state \|\| wxGetApp().easy_mode() != m_add_volume_easy_mode_state)` | `if (allow_support_enforcers() != m_support_enforcer_menu_state)` |
| `GUI_Factories.cpp:2172`, `:2174` | `if (wxGetApp().easy_mode() \|\| m_object_menu.FindItem(...) == wxNOT_FOUND)` | drop `wxGetApp().easy_mode() \|\| ` |
| `GUI_Factories.cpp:2427-2428`, `:2445-2446`, `:2468-2469` | `if (easy_mode()) return;` / `return false;` | delete |
| `GUI_ObjectList.cpp:368-370` | `if(easy_mode()) { SimpleDeviceMgr::instance().get_device_list_data_simple(true); }` | delete; delete `#include "simple/DeviceListSimple.hpp"` (`:56`). Keep `m_device_list_popup_*`, `EVT_OPEN_DEVICE_LIST` and `draw_device_list_popup` (`:377-385`, `:8252-8297`, `:9166`): they are the Pro ImGui object list's device popup, posted by `Plater.cpp:1881` |
| `Gizmos/GLGizmoBase.cpp:244`, `:249-292`, `GLGizmoBase.hpp:249` | `GizmoImguiRenderSimpleCloseButton()` (reader `:251`) | delete the call (keep `PushStyleVar`/`PopStyleVar` around `begin_with_drag`: they size the title bar in Pro too) and the function |
| `Gizmos/GLGizmoClone.cpp:94` | `GizmoImguiRenderSimpleCloseButton();` | delete |
| `Gizmos/GizmoObjectManipulation.cpp:37-78`, calls `:659`, `:853`, `:990` | `render_simple_gizmo_close_button` (reader `:39`) | delete the function and the three calls |
| `Gizmos/GLGizmosManager.cpp:49-63` | `is_gizmo_shortcut_available_in_easy_mode` | delete |
| `Gizmos/GLGizmosManager.cpp:457`, `:462`, `:470-473` | `const bool is_easy_mode = ...`; `!is_easy_mode && matches_shortcut_key(...)`; lambda capturing `is_easy_mode` with `(!is_easy_mode \|\| ...) &&` | delete `:457`; `:462` becomes `matches_shortcut_key(emboss_gizmo->get_shortcut_key())`; the lambda captures `[matches_shortcut_key]` and returns `gizmo->is_activable() && matches_shortcut_key(gizmo->get_shortcut_key());` (the `EType type` parameter becomes unused: keep it or drop it with the call at `:477`) |
| `Gizmos/GLGizmosManager.cpp:251-257` | `GLGizmoSimplePaint` created with `set_add_filament_handler([this]() { m_parent.open_filament_toolbar_popup(); })` | `m_gizmos.emplace_back(new GLGizmoSimplePaint(m_parent, m_is_dark ? "mmu_segmentation_dark.svg" : "mmu_segmentation.svg", EType::SimplePaint));` Optional: in `GLGizmoSimplePaint.cpp:461-465` draw the "Add filament" button only `if (m_add_filament_handler)`, so Pro shows no dead button |
| `Jobs/ArrangeJob.cpp:825-828`, `Jobs/OrientJob.cpp:230-233` | `if (easy_mode()) { if (auto* panel = GetEmbeddedAIChatPanel()) panel->CompletePendingAsyncToolCall(...); }` | if F3 left them: delete the lambda `report_*_completion` and its calls |
| `Plater.cpp:232`, `:233`, `:235`, `:236` | includes `simple/UploadDialog.hpp`, `simple/filamentMapping/ImGuiFilamentPanel.hpp`, `simple/DeviceListSimple.hpp`, `simple/SimpleModelMgr.hpp` | delete (keep `:234` `SceneFilamentSourceSnapshotManager.hpp`; `:231` see 1.4) |
| `Plater.cpp:8045-8047` | `if(easy_mode()) { should_show = false; }` | delete |
| `Plater.cpp:8283-8319` `update_easy_mode_ai_layout` (`:8288` reader) and the AI dock helpers `:8111-8176`, `kEasyModeLayoutSection` `:6172` | embedded AI dock | if F3 left them: delete the function, its three calls (`:7543`, `:7811`, `:8108`), `m_last_easy_mode_state` (`:6333`) and the dock members |
| `Plater.cpp:8535-8537` | `if(easy_mode()) { SimpleModelMgr::instance().reset(); }` | delete |
| `Plater.cpp:9994-9996` | `if(easy_mode()) { SimpleModelMgr::instance().center_3mf_objects_to_plate(); }` | delete |
| `Plater.cpp:13176-13187` `need_change_preset` | `if (easy_mode()) { return SimpleDeviceMgr...check_need_to_change_current_preset(...); } return true;` | body becomes `return true;` (keep the signature; mark the parameters unused) |
| `Plater.cpp:14641-14643` | `if(easy_mode()) { return false; }` | delete |
| `Plater.cpp:14805-14809` | `if(easy_mode() && current_panel == view3D) {...easy_mode_on_scene_reloaded();}` | delete |
| `print_manage/Routes/DeviceMgrRoutes.cpp:191` | `if (!wxGetApp().easy_mode() && wxGetApp().preset_bundle != nullptr)` | `if (wxGetApp().preset_bundle != nullptr)` |
| `simple/filamentMapping/SceneFilamentSourceSnapshotManager.cpp:81-84`, `.hpp:31` | `is_easy_mode_active()` returns `wxGetApp().easy_mode()` | body `return false;` with a comment; this keeps today's Pro behaviour exactly (`initialized()` false, `capture_from_loaded_3mf_config` returns false) without reading `easy_mode()` |
| `GUI_Preview.cpp:77`, `:874` | `m_canvas->enable_easymode_main_toolbar(true/false);` | delete |
| `NotificationManager.cpp:291-293` | `safe_left`/`safe_bottom` from `get_easy_mode_overlay_safe_*` | `ImVec2 win_pos(bias.x, cnv_size.get_height() - initial_y - m_window_height);` |
| `NotificationManager.cpp:2910-2914` | same | `ImVec2 bias = ImVec2(5.0f * canvas.get_scale(), canvas.get_canvas_size().get_height() - bottom_up_last_y);` and `float h0 = canvas.get_canvas_size().get_height() - pos.y - size.y - bottom_up_last_y - 10.0f;` |

**`Update_easy_mode_flag` (GUI_App.cpp:14631-14679).** After F2 its only caller is `post_init` (`:2172`). What it still has to do at start-up: `obj_list->set_last_preset_name(...)` (the Pro branch of `on_easy_mode_switch`), `plater->update()`, refresh the Print/Filament/Printer tabs, refresh the topbar. Replace it with:

```cpp
// Start-up refresh that used to run through the easy/Pro switch.
void GUI_App::refresh_after_startup()
{
    Plater* plater_ptr = plater();
    if (plater_ptr == nullptr)
        return;
    MainFrame* main_frame = mainframe;
    wxWindowUpdateLocker ui_locker(main_frame ? static_cast<wxWindow*>(main_frame) : static_cast<wxWindow*>(plater_ptr));
    if (ObjectList* list = obj_list())
        list->set_last_preset_name(preset_bundle->printers.get_selected_preset_name());
    plater_ptr->update();
    for (Preset::Type type : {Preset::TYPE_PRINT, Preset::TYPE_FILAMENT, Preset::TYPE_PRINTER})
        if (Tab* tab = get_tab(type)) {
            tab->update_dirty();
            tab->reload_config();
            tab->update();
        }
    if (main_frame != nullptr && main_frame->topbar() != nullptr)
        main_frame->topbar()->Refresh();
}
```

and rename the declaration (`GUI_App.hpp:455`) and the call (`:2172`). `NotifyAIChatEditionChanged()` (`:14678`) is F3's and must already be gone. `canvas->on_easy_mode_switch()` also reset `m_resident_filament_resize_state` and called `MCPChatWindow::Hide()`; both disappear with their owners.

### 1.6 Header cleanup

- `GLCanvas3D.hpp`: delete the forward declarations `class ImGuiFilamentPanel;` and `class PrintSettingsPanel;` (`:67-68`), `enum class PopupHAlign` with its comment (`:74-82`), the block `:685-738` (the `ResidentFilament*` types and everything between the two `// ==== easy print mode related ====` markers, including `m_easymode_main_toolbar` and `m_object_manipulate_toolbar`), `enable_easymode_main_toolbar` (`:964`), `get_easy_mode_overlay_safe_*` (`:999-1001`), the public block `:1341-1351` (`check_filament_mapping_simple` ... `easy_mode_on_scene_reloaded`, `get_filament_panel`), `_render_overlays_easymode` (`:1409`), and the private block `:1442-1489` (from the marker to the closing marker: `_init_ui_simple` ... `render_card_popup_if_open`). `_render_ai_chat_toggle` (`:1410`) and `_ensure_ai_chat_logo_tex` (`:1457`) were F2's.
- `GLToolbar.hpp`: delete the easy fields of `GLToolbarItem::Data` (`always_enable`, `label_below_icon`, `icon_label_split`, `:147-151`, and their copies in the copy constructor `:176-180`), `render_icon_top_label_bottom_px` (`:269`), `is_always_enable` (`:277`), `Layout::gap_scale`/`sep_scale` (`:366-369`), the `m_has_last_bg_rect_px`/`m_last_bg_*` members (`:409-414`), the public declarations `:511-519` and the private ones `:541`, `:553`. No Pro code reads these fields (checked with `git grep`).
- `GUI_App.hpp`: `easy_mode` (`:443`), `Update_easy_mode_flag` (`:455`, renamed), `startTour_Simple` (`:836`), and `m_is_easy_mode` (`:313`) if F2 left it.
- `GUI_Factories.hpp:108` `m_add_volume_easy_mode_state`; `GLGizmoBase.hpp:249`; `SceneFilamentSourceSnapshotManager.hpp:31` stays (body changed).
- `AppConfig.cpp:507-508` (F2 forces `easy_print_mode` to "0"): change to `m_storage.erase("easy_print_mode");` so the key disappears from `Creality.conf`. The `easy_mode_layout` section written by the AI dock (`Plater.cpp:6172`) and the resident filament layout keys stay as unused data; optional `erase` of the section **[INFERENCE: AppConfig section erase API not checked]**.

### 1.7 Compile risks

- Many headers change (`GLCanvas3D.hpp`, `GLToolbar.hpp`, `GUI_App.hpp`): near full GUI recompile (not the engine; `PrintConfig.hpp` is untouched).
- `GLCanvas3D.cpp` may have relied on `PrintSettingsPanel.hpp` or `ImGuiFilamentPanel.hpp` for transitive includes; if the build fails with a missing type, add the specific header, not the deleted one.
- `GLToolbarItem::Data` loses fields: any designated or aggregate initialiser that sets them would fail; only the deleted simple files set them (checked).
- `std::unique_ptr<ImGuiFilamentPanel>`/`<PrintSettingsPanel>` members with forward declarations: removing the members also removes the need for the complete types in the destructor.
- The `GLGizmosManager` lambda change must still compile with the unused `EType` parameter (MSVC warns only at level 4).
- `SlicerBridgeActionsPrinter.cpp`: unused static helpers in an anonymous namespace only warn.

### 1.8 Verification (easy mode part)

1. `git grep -nE "easy_mode\(|easymode|_simple\(|SimpleDeviceMgr|SimpleModelMgr|PrintSettingsPanel|SupportSimple|UploadDialog|GLSimpleUtils|is_always_enable|should_hide_easy_mode" -- src` returns only the kept `SlicerBridgeActionsProcess.cpp:154` comment (and `is_easy_mode_active` in the snapshot manager, if kept by name).
2. Build with `build_exe_only.bat` (reconfigures: `src/slic3r/CMakeLists.txt` changed).
3. Manual: Prepare shows the Pro toolbar; hover tooltips; Esc deselects; arrange window opens and closes with its own button; gizmo windows (move, rotate, scale, clone, cut, paint) open without the extra close button and with the same title height; `N` on a single-filament plate; right-click menu of an object has "Edit Process Settings", "Edit in Parameter Table", add part/modifier/negative volume, height range modifier; notifications sit in the bottom-left corner as before; Preview legend shows its separator; load a 3MF (no preset prompt change); Device tab selects the bound printer.
4. Print Advisor: `get_print_context` (presets + edited config), `slice_and_measure`, `get_overhang_regions`, schema export (Help/debug menu entry at `MainFrame.cpp:3000`).
5. G-code of the F0 projects identical.

---

## Part 2. Home

### 2.1 Verified facts

- **The Home tab** is `WebViewPanel` (`WebViewDialog.cpp/.hpp`, 1,232 lines; the header also declares `SourceViewDialog`, used only by `WebViewPanel`). It is created in `MainFrame::init_tabpanel` (`MainFrame.cpp:1529-1537`) and loads `http://localhost:<port>/homepage/index.html?...&privacy=%d...#/Community/Home Page` (`WebViewDialog.cpp:291`) from the local HTTP server, whose root is `resources/web` (`HttpServer.cpp:850`).
- **The same `homepage` bundle serves the "Add printer" and "Add filament" windows.** `GuideFrame::SetStartPage` (`WebGuideDialog.cpp:326-342`) loads `http://localhost:%d/homepage/index.html?lang=%s&isScale=false#/Guide/createFilament` for `BBL_FILAMENT_ONLY` and `...#/Guide/addPrinter` for `BBL_MODELS_ONLY`, falling back to the old local pages `web/guide/23` and `web/guide/24` only if the server does not start. Callers: `GUI_App::run_wizard` (`GUI_App.cpp:13749-13782`; `SP_FILAMENTS` gives `BBL_FILAMENT_ONLY`, everything else `BBL_MODELS_ONLY`), `GUI_App::ShowOnlyFilament` (`:6361-6366`), and the sidebar/combos: `Plater.cpp:1645,2087`, `PresetComboBoxes.cpp:871-1417`, `SiderBar.cpp:84,650`, `GUI_ObjectList.cpp:7831`. **So the `homepage` bundle must stay in both options.** The router (`index-806b308b.js`, offset ~3855206) has `/Guide/addPrinter`, `/Guide/createFilament`, `/Community/:menu`, `/BeginGuide`, `/Debug`, and redirects everything else to `/Community/Home Page`.
- **First-run onboarding** today: `GUI_App.cpp:2434-2439` explains that it is driven by the Home SPA (`get_is_first_install` / `deviceAddEnd`, handler `GUI_App.cpp:8854-8900`, which switches to `tpHome` at `:8883-8884`); the classic `config_wizard_startup()` call is commented out at `:2439`. `config_wizard_startup` (`:13939-13965`) still exists and runs `run_wizard(RR_DATA_EMPTY, SP_CUSTOM)`, i.e. the `GuideFrame` "add printer" page, when there is no config or only default printers.
- **Google Analytics / Firebase run in the root `App` component** of the homepage bundle, not only in the Home page: `H.cp.privacy && $cppManager.initFirebase(H.cp.type != type.Release)` inside `_sfc_main$1={__name:"App",...}` (offset ~4425855). It runs for every route, including the two Guide routes, when `cp.privacy` is set. The Home URL passes `privacy=%d` (`WebViewDialog.cpp:291`); the Guide URLs (`WebGuideDialog.cpp:332,335`) do not **[INFERENCE: so Firebase probably stays off in GuideFrame today]**. The same `App` calls `checkInternet`, which fetches `https://api.crealitycloud.com/` (or `.cn`) with `mode:"no-cors"`.
- Creality's own tracking (`initCxAnalytics` → `initDataTracking`, base URL `https://devdata.cxswyjy.com` for Release, `http://172.23.88.185:8080` for Alpha/Beta) is started by the `HomePage` component (`onMounted`, `$cppManager.initCxAnalytics(U.cp.type)`), not by the Guide routes.
- The Community layout (`_sfc_main$7`, scope `data-v-f36bccec`) has an `Account` block (login), menu items "Home Page" (recent projects), "Online Params", "Guide Forum" (Tutorials), a "Personal Center" sub-menu (My Models/Presets/Downloads), "Feedback", and a bottom "Creality Mall" link (`Shop`).

### 2.2 Web patch 12, "homepage network off" (both options)

Because the Guide windows stay in both options, apply this patch in both. New entries for `tools/web-patches/patches.mjs` (after F2's "11"), with `const HOME = "resources/web/homepage";` next to `DEVICE` and `SEND`. Every `find` below was tested with Node against `index-806b308b.js`: **each matches exactly once**.

```js
  // 12 --------------------------------------------------------------------
  {
    id: "12",
    title: "Homepage: no Firebase/GA, no Creality tracking, no internet probe",
    dir: HOME,
    fileContains: "initCxAnalytics(",
    edits: [
      { // Firebase + Google Analytics (root App, every route)
        marker: "/*PA:12a*/",
        applied: /\/\*PA:12a\*\//,
        find: new RegExp(String.raw`initFirebase\((?<u>${ID})\)\{\k<u>\?initFirebase_dev\(\):initFirebase\(\)\}`, "g"),
        replace: (m, v) => fill(String.raw`initFirebase(@@u@@){/*PA:12a*/}`, v),
      },
      { // Creality data tracking (HomePage)
        marker: "/*PA:12b*/",
        applied: /\/\*PA:12b\*\//,
        find: new RegExp(String.raw`initCxAnalytics\((?<u>${ID})\)\{initDataTracking\(\k<u>\)\}`, "g"),
        replace: (m, v) => fill(String.raw`initCxAnalytics(@@u@@){/*PA:12b*/}`, v),
      },
      { // GA events: return before getAnalytics()
        marker: "/*PA:12c*/",
        applied: /\/\*PA:12c\*\//,
        find: new RegExp(String.raw`sendGAEvent=\((?<a>${ID}),(?<b>${ID})\)=>\{`, "g"),
        replace: (m, v) => fill(String.raw`sendGAEvent=(@@a@@,@@b@@)=>{/*PA:12c*/return;`, v),
      },
      { // internet probe to api.crealitycloud.*: report offline without fetching
        marker: "/*PA:12d*/",
        applied: /\/\*PA:12d\*\//,
        find: new RegExp(String.raw`function checkInternet\((?<c>${ID})\)\{return (?<x>${ID})\(this,null,function\*\(\)\{`, "g"),
        replace: (m, v) => fill(String.raw`function checkInternet(@@c@@){return @@x@@(this,null,function*(){/*PA:12d*/return!1;`, v),
      },
    ],
  },
```

Notes: `sendTrackingEvent` already returns while `isInitialized` is false, which 12b guarantees. Code after `return` is unreachable but valid JS. With 12d the SPA believes it is offline: the Home page shows its offline state for online sections, while recent projects (C++ data) still work **[INFERENCE]**. Document the patch as "### 12" in `doc/MCP/web-bundle-patches.md`. Verify with `node tools/web-patches/apply-web-patches.mjs --check`, then `--apply`.

### 2.3 Option A: remove the Home page (requires F2 Variant A)

The `homepage` bundle **stays** (Guide routes). Only the C++ Home page goes.

1. `git rm src/slic3r/GUI/WebViewDialog.cpp src/slic3r/GUI/WebViewDialog.hpp`; delete `src/slic3r/CMakeLists.txt:346-347` and `localization/i18n/list.txt:49,90`.
2. Includes of `WebViewDialog.hpp` to remove: `BBLTopbar.cpp:15`, `ConfigRelateDialog.cpp:25`, `GUI_App.cpp:36`, `MainFrame.cpp:55`, `Preferences.cpp:4`, `PrinterPresetConfig.cpp:8`, `WebGuideDialog.cpp:13`. None of the first four uses `WebView`/`wxWebView` directly (checked); `GUI_App.cpp`, `MainFrame.cpp` and `WebGuideDialog.cpp` already include `Widgets/WebView.hpp`/`wx/webview.h`. The deleted header pulled in `ImGuiWrapper.hpp`, `wx/notifmsg.h`, `wx/numdlg.h`, `wx/infobar.h`, `wx/stdpaths.h`, `wx/timer.h` and others: if a file stops compiling, add the specific header it needs.
3. `MainFrame.hpp:412` `WebViewPanel* m_webview`, and `GUI_App.hpp:76` `class WebViewPanel;`: delete. Every use (grep of `m_webview`, 47 hits today, minus F2's):

| Location | Edit |
|---|---|
| `MainFrame.cpp:1337` (`destroy_view(m_webview)`) | delete |
| `MainFrame.cpp:1398-1411` `trackEvent` | body empty (F1 already made `GUI_App::track_event` a no-op; its only caller is `GUI_App.cpp:5370`). Or delete the function with that caller if F1 left it |
| `MainFrame.cpp:1454` (page-changed `panel == m_webview`) | F2 Variant A already removed it |
| `MainFrame.cpp:1529-1537` | F2 Variant A already removed creation, Bind and AddPage; keep the `m_param_panel` creation inside `if (wxGetApp().is_editor())` |
| `MainFrame.cpp:2857-2863` (`#if CUSTOM_COMMUNITY_ENABLE` user agent + Reload) | delete the comment and the `#if` block |
| `MainFrame.cpp:4055`, `:5241`, `:5265` (`m_webview->SendRecentList(-1)`), `:4980-4982` (`SendRecentList(0)`) | delete; the recent list lives in `m_recent_projects` and the File menu |
| `MainFrame.cpp:5314-5318` `RunScript` | body empty with a comment ("no Home page"): `GUI_App::run_script` (`GUI_App.cpp:13585-13589`) forwards here and has 77 callers in `handle_web_request` that answer the Home SPA; keeping the no-op avoids touching them |
| `GUI_App.cpp:1874-1883` `reload_homepage` and `GUI_App.hpp:414` | delete with its callers `GUI_App.cpp:8931,9328,10622,14491`, `InstanceCheck.cpp:505,517,550`, `Plater.cpp:7715`, `Preferences.cpp:370,864` (some may already be gone with F1/F4) |
| `GUI_App.cpp:6612` `m_webview->SetLoginPanelVisibility(true)` | delete (F4 may have removed the whole logout block) |
| `GUI_App.cpp:8080-8082` (`SendRecentList(INT_MAX)` in `handle_web_request`) | delete |
| `GUI_App.cpp:12006-12009` `switch_staff_pick` | delete with its declaration and callers (only commented ones at `:3075`, `:5659`) |
| `GUI_App.cpp:12480-12482` (`#if !AUTO_CONVERT_3MF m_webview->update_mode(); #endif`) | delete |
| `GUI_App.cpp:12516-12518` `update_internal_development` | body empty, or delete with its callers |
| `ConfigRelateDialog.cpp:3498-3501`, `Preferences.cpp:1877-1880`, `Preferences.cpp:1895-1897`, `PrinterPresetConfig.cpp:416-418`, `WebGuideDialog.cpp:1553-1556` (`sync_preferences`/`sync_user_preset`) | delete the `if (m_webview) {...}` blocks; in `PreferencesDialog::notify_sync_user_preset_changed` (`Preferences.cpp:1885-1898`) the whole body becomes dead: delete the function and its callers if F4 left it |

4. **First-run onboarding replacement.** In the start-up `CallAfter` (`GUI_App.cpp:2434-2439`), replace the comment block that ends with `//bool cw_showed = this->config_wizard_startup();` with `this->config_wizard_startup();`. On a clean data folder (or only default printers) this opens `GuideFrame` on `#/Guide/addPrinter` (served by the kept bundle). The Home "begin guide" flow (`get_is_first_install`, LAN connect, UI tour) is lost; the LAN connection is done from the Device tab. The handler branch `get_is_first_install` (`GUI_App.cpp:8854-8900`) becomes unreachable; delete it or leave it. Note `GUI_App.cpp:4065` and `:9280`, `:9333` set `is_first_install`; harmless.
5. Leftover C++ with no Home: the `homepage_*` commands in `GUI_App::handle_web_request` (`:8057-8082`, `:8258-8360`) are reachable only from the Home SPA. Leave them for now (the function also serves `update_preset_params` from `GUI_App.cpp:6896`); a later pass can delete them.
6. Preferences: F2 Variant A already deleted the "Default Page" radio. `app_config` key `default_page` stays unused.

### 2.4 Option B: keep Home, without community, login and analytics (requires F2 Variant B)

C++ stays as it is (after F1/F4). The web side gets patch 12 (2.2) plus patch 13 below, which leaves only "Projects" (recent files, new/open project) in the left menu. Anchors are stable strings (`Account`, `"Online Params"`, `Feedback`, `class:"menu-item"`), and the minified names (`de`, `Z`, `ae`) are captured. Each `find` matches exactly once in `index-806b308b.js` (tested with Node; 13b spans 2,106 characters, from the "Online Params" item to the end of the "Feedback" item, and is followed by `]),_:1},8,["selectedKeys","onClick"])`, so the remaining array `[HomePageItem]` stays valid).

```js
  // 13 --------------------------------------------------------------------
  {
    id: "13",
    title: "Homepage: only Projects in the menu (no account, community, shop)",
    dir: HOME,
    fileContains: "initCxAnalytics(",
    requires: ["12"],
    edits: [
      { // login/account block at the top of the side menu
        marker: "/*PA:13a*/",
        applied: /\/\*PA:13a\*\//,
        find: /createVNode\(Account,\{style:\{marginBottom:"10px"\}\}\),/g,
        replace: () => `/*PA:13a*/`,
      },
      { // menu items Online Params, Guide Forum, Personal Center (sub-menu), Feedback
        marker: "/*PA:13b*/",
        applied: /\/\*PA:13b\*\//,
        find: new RegExp(String.raw`,\(openBlock\(\),createBlock\((?<d>${ID}),\{key:(?<z>${ID})\["Online Params"\]\}[\s\S]*?\(openBlock\(\),createBlock\(\k<d>,\{key:\k<z>\.Feedback\}[\s\S]*?toDisplayString\$1\((?<a>${ID})\.\$t\("Feedback"\)\),9,${ID}\)\]\)\]\),_:1\}\)\)`, "g"),
        replace: () => `/*PA:13b*/`,
      },
      { // "Creality Mall" link at the bottom of the menu
        marker: "/*PA:13c*/",
        applied: /\/\*PA:13c\*\//,
        find: /createBaseVNode\("div",\{class:"menu-item",onClick:/g,
        replace: () => `createBaseVNode("div",{class:"menu-item",style:{display:"none"}/*PA:13c*/,onClick:`,
      },
    ],
  },
```

Remarks:

- The content panes (`OnlineParams`, `GuideForum`, `AIModeling`, `MyModels`...) stay in the bundle but can no longer be selected from the menu; the router still redirects unknown routes to `/Community/Home Page`. C++ can still send `switch_sub_page` (`page_name` "login"/"token_expired" open the login view; anything else opens "Online Params"); after F4 nothing sends it **[INFERENCE: check with `git grep switch_sub_page` after F4]**.
- The `HomePage` component itself shows, besides recent projects, a "go to Online Models" hint (`"Only one step to creation, go to"`, offset ~3440000) and may show banners from the cloud; with 12d (offline) those online parts stay empty **[INFERENCE: check visually]**.
- The "BeginGuide" first-run flow still works (it is a route of this bundle).
- C++ side: login, survey and feedback commands were removed by F1/F4; `send_http_request` became a no-op in F1.
- Docs: add "### 12" and "### 13" to `doc/MCP/web-bundle-patches.md` with the same rules as the other patches (re-apply after any bundle update; none is expected now that the fork no longer follows Creality).

### 2.5 Web bundles to delete (both options)

| Folder | Files / size | Who loads it | Verdict |
|---|---|---|---|
| `resources/web/guide/swiper/` | 304 / 4.5 MB | nobody: no `<script>`/`<link>` in `guide/*/index.html` points to it (only CSS class names `swiper-*` in `guide/css/test.css` and `guide/js/home.js`) | delete |
| `resources/web/include/` | 312 / 4.8 MB | only `resources/web/model/` refers to `include/` | delete |
| `resources/web/model/` | 31 / 2.6 MB | `Project.cpp:48-51` (`ProjectPanel`), whose creation is commented out (`MainFrame.cpp:1592`); F5 deletes the panel | delete (if `Project.cpp` is still compiled, it only builds a URL) |
| `resources/web/login/` | 9 / 0.13 MB | no reference in `src` | delete (F4 may have done it) |
| `resources/web/homepage2/` | 2 / 10 KB | no reference anywhere | delete |
| `resources/web/orca/` | 4 / 0.7 MB | `Plater.cpp:2559` builds `file://.../web/orca/missing_connection.html` and passes it to `MainFrame::load_printer_url`, whose only handler (`EVT_LOAD_PRINTER_URL`) is commented out (`MainFrame.cpp:1558`, `:1675`); the event is never handled | delete; optionally delete the dead branch in `Plater.cpp:2556-2570` with F5 |

Keep: `homepage/` (Home and Guide routes), `guide/<n>/`, `guide/js`, `guide/css` (local pages of `GuideFrame`, `WebGuideDialog.cpp:275-305`, and of `WebDownPluginDlg.cpp:41` for `guide/6`; they link to each other), `data/text.js` and `image/` (used by the guide pages), `deviceMgr/`, `sendToPrinterPage/`. `guide/index.html` is dead (it loads `../homepage/js/*.js`, which do not exist) and may go too. Total deleted: 662 files, 12.7 MB.

```bash
git rm -r -q -- resources/web/guide/swiper resources/web/include resources/web/model \
  resources/web/login resources/web/homepage2 resources/web/orca
```

### 2.6 Verification (Home part)

1. `node tools/web-patches/apply-web-patches.mjs --check` reports 12 (and 13 in B) as applicable, then `--apply`, then `--check` reports `applied`.
2. Build, start the app.
   - A: no Home tab and no logo action; File menu recent projects work; Sidebar "+" on printers and filaments opens "Aggiungi stampante"/"Aggiungi filamento" and both save; rename the data folder once (or use a copy) to see the first-run `GuideFrame`.
   - B: Home shows only "Projects"; no account block, no Shop link; recent projects open; "New project" and "Open project" work.
3. Network (plan step 7): 5 minutes with the Windows resource monitor while opening Home (B) or the add-printer window (A): no connection to `*.google-analytics.com`, `*.googleapis.com`, `firebase*`, `api.crealitycloud.*`, `devdata.cxswyjy.com`.
4. Plan protocol: G-code unchanged, sidecar smoke, Device tab, Preferences, clean exit.

---

## 3. Open questions

1. Home: Option A or B (plan question 1). It fixes F2's Variant.
2. EasyPrintSender: E1 (delete, recommended: no caller after F3) or E2 (keep with the `DM::DataCenter` replacement).
3. `simple/filamentMapping`: after F7 only `FilamentMappingService`, `SceneFilamentSourceSnapshotManager` and `match_color` are used. Delete the rest (`ImGuiFilamentPanel`, `ImGuiFilamentExtraPage`, `ImGuiSpoolWidget`, `ImGuiThumbnailPreview`, `ResidentFilamentMapping*`, `ThumbnailDataRecolor` once F3 has removed `AISendWorkflowService`) in a follow-up?
4. `GLGizmoSimplePaint` in Pro: hide its "Add filament" button (optional edit in 1.5), or remove the gizmo from Pro altogether (it is reachable only through the `N` shortcut when `MmuSegmentation` is not activable)?

## 4. Suggested commits (one build at the end)

1. `GLToolbar` simple branches and declarations.
2. `GLCanvas3D` easy branches, helpers, members; `NotificationManager`, `GUI_Preview`.
3. Gizmo close buttons and shortcut filter; `SimplePaint` handler.
4. `GUI_Factories`, `BaseRenderer`, `DeviceMgrRoutes`, `GUI_ObjectList`, `Plater`, `SceneFilamentSourceSnapshotManager`.
5. `GUI_App`: `refresh_after_startup`, `easy_mode()` and `startTour_Simple` removed; `AppConfig` key erased.
6. SlicerBridge `LIST_PRINTERS`/`SELECT_PRINTER`; EasyPrintSender (E1 or E2).
7. `git rm` of the simple UI files, CMake and `list.txt`.
8. Home: patch 12; then Option A (C++ Home removal, onboarding) or Option B (patch 13).
9. Web bundles of 2.5.
