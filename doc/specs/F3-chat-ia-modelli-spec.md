# F3 "Chat IA e modelli online": implementation spec

I only read files. Nothing was edited, built or run. Line numbers refer to HEAD `14528e0e` (branch `advisor`) and were checked with `sed -n`/`nl` or `grep -n`. Statements are verified against the code unless marked **[inference]**.

**F1 and F2 are applied before F3.** Both edit several of the same files:
- GUI_App.cpp, Plater.cpp, MainFrame.cpp, BBLTopbar.cpp;
- GLCanvas3DSimple.cpp, SlicerBridgeState.cpp, SlicerBridgeActionsObject.cpp, EasyPrintSender.cpp.

So the HEAD line numbers below will be shifted. Every edit gives an **anchor** (the exact text to find). Apply by anchor, from the bottom of each file upward. Files use CRLF: keep it.

**Build:**
- One batch, one build with `build_exe_only.bat`. It reconfigures, because CMakeLists.txt changes.
- The touched headers (GUI_App.hpp, Plater.hpp, MainFrame.hpp, GLToolbar.hpp, GUI.hpp) and the regenerated `buildinfo.h` cause a near-full GUI recompile, as in F2.
- `PrintConfig.hpp` and the libslic3r headers are not touched. Only `PresetBundle.cpp` recompiles in libslic3r, because it includes `buildinfo.h`.

---

## 0. Verified facts and corrections to the brief

1. **`EasyPrintSender` is NOT used by the LAN print path.**
   - The LAN path is `print_manage/App/SendToPrinter.cpp` (`CxSentToPrinterDialog`) plus `RemotePrinterManager`. Neither includes nor calls `EasyPrintSender`.
   - Its only callers are `AISendWorkflowService.cpp` and `toolcalls/MCPToolCallsRegistration.cpp:603`, both deleted in F3.
   - The other references are dead:
     - includes: `Plater.cpp:231`, `PrinterMgrView.cpp:75`, `DeviceListSimple.cpp:34`, `GLCanvas3DSimple.cpp:104`;
     - a commented line: `DeviceListSimple.cpp:1362`;
     - a comment: `PrinterMgrView.cpp:551`.
   - `git grep "PRINTER_STATE|printerStateToString|PrinterStatus"` outside `simple/sendWorkflow/` returns nothing.
   - **Recommended (Variant A):** delete the whole `simple/sendWorkflow/` folder, `EasyPrintSender` included.
   - **Variant B (as in the brief):** keep `EasyPrintSender` and strip the cloud loop (section 3.28). It would stay compiled with no caller.
2. **`print_manage/App/mqtt_client.{h,cpp}` is used only by `SAgentMqttBridge.cpp:2`.** `MQTTClient` has no other user, so delete it.
   - Paho stays: `CloudDeviceMqttTransport.cpp` still includes `<mqtt/async_client.h>`, and `src/CMakeLists.txt:162-163` still links `PahoMqttCpp`. F4 removes it.
3. **The "Export AI Config Schema" menu is already dead code.**
   - It is wrapped in `#if ENABLE_AI_CONFIG_SCHEMA_EXPORT` (MainFrame.cpp:2983-3016). The macro is defined nowhere in the repository: the comment says Technologies.hpp, but it is not there.
   - Delete the block, the include at `MainFrame.cpp:103` (`SlicerBridge.hpp`, used only there) and `SlicerBridge::ExportConfigSchemaToFile`.
4. **`CxAgentEndpointPolicy` has one includer, `SatisfactionSurveyManager.cpp:3`, which F1 item 5 deletes.** F3 therefore depends on F1 item 5: without F1, deleting the policy breaks the build.
5. **No local web bundle belongs only to these features.**
   - The chat loads the remote cxagent page (`MCPChatPanel.cpp:362`, `581-586`).
   - The model library loads `get_cloud_webaddress()` pages.
   - `AICloudServiceDialog` talks to `ai-*.crealitycloud.*`.
   - `resources/web/model` is used by `Project.cpp:48` (F5/F6), not by F3 code.
   - Only images are removed (section 5).
6. **`AICloudServiceDialog` is wired into three more places that the brief does not list.** All of them go together:
   - the 3D toolbar item `"ai_cloud_service"` (GLCanvas3D.cpp:9330-9363);
   - `Plater::select_ai_cloud_service` (priv and public), with `EVT_GLVIEWTOOLBAR_AI_CLOUD_SERVICE` (GLToolbar.cpp:68, GLToolbar.hpp:65, Plater.cpp:7425);
   - `AICloudService::getInstance()->cleanup()` in `GUI_App::load_language` (GUI_App.cpp:12207).

   The toolbar item is the last real icon before the virtual items. `GLToolbar::generate_icons_texture` builds the atlas in item order, so the sprite ids of the other icons do not shift.
7. **`PublishDialog` is tangled with Plater's slicing state** (`m_is_publishing`, `m_publish_dlg` in `on_process_completed`, `on_action_slice_all` and `on_action_publish`). It is never reachable:
   - `EVT_PUBLISH` is posted only by `PublishDialog.cpp:153,245`;
   - the only `show_publish_dialog()` call (BBLTopbar.cpp:1549) is inside `#ifdef ENABLE_PUBLISHING`, which is never defined.

   The whole publishing branch is removed (section 3.9).
8. **`ModelMallDialog* m_mall_publish_dialog` is never assigned.**
   - `GUI_App::remove_ping_bind_dialog` (13394) does `delete m_mall_publish_dialog;` on the wrong member. It is an upstream bug, harmless because the pointer is always null.
   - Delete the member, the forward declaration, `remove_mall_system_dialog` and its call at MainFrame.cpp:553.
9. **`ModelLibraryRoutes` handles commands that `GUI_App::handle_web_request` also handles.**
   - Only `WebModelLibraryView` and the Home `WebViewPanel` (WebViewDialog.cpp:836) route through `AppMgr`.
   - After the deletion, Home's commands fall through to `handle_web_request`, which already handles them all: `get_account_info` (GUI_App.cpp:8084), `login_account_success` (8127), `get_client_id` (8301), `3mf_download_start` (8403), `models_download_state` (8452), `models_download_delete` (8476).
   - Differences **[inference]**:
     - `3mf_download_start` no longer switches to the Prepare tab;
     - `models_download_state` is answered through `handleStudioCmd` (URL-encoded) instead of a raw JSON post.
   - Both only matter for Home's cloud downloads (F4/F7 territory).
   - `Routes::Invoke` matches commands exactly, so the `{""}` fallback handler only ever matched an empty command.
10. **Eshop.**
    - `GUI_App::OpenEshopRecommendedGoods` (GUI_App.cpp:14536-14624, GUI_App.hpp:771-775) is called only by the `buy_filament_cmd` handlers in `SendToPrinter.cpp:520-539` and `DeviceMgrRoutes.cpp:1243-1264`.
    - Both web bundles send that command (`deviceMgr/assets/BZCDzYbb.js`, `sendToPrinterPage/assets/BDAlyC64.js`).
    - Without a handler, `SendToPrinter.cpp:596-600` logs the command as unknown, and in the Device page it falls through `PrinterMgrView::OnScriptMessage`.
    - The "buy filament" button then does nothing (open question 2).
11. **Print Advisor check (grep).** `src/slic3r/GUI/simple/printAdvisor/*` includes only `bridge/SlicerBridge.hpp` and `bridge/SlicerAction.hpp`, and uses only:
    - `SlicerBridge::BuildConfigSchemaArray` (PrintAdvisorServer.cpp:1018);
    - `Execute(ActionID::START_SLICE)` (1033);
    - `Execute(GET_PRESETS)` and `Execute(GET_EDITED_CONFIG)` (1091-1092);
    - `SlicerBridge::AnalyzeOverhang` (PrintAdvisorServer.cpp:2363, PrintAdvisorPlate.cpp:354).

    Other checks:
    - `git grep -i "cxagent|model_library|smart_model|toolcall|mcpchat|sagent|eshop|recommend_model"` in `tools/` and `printAdvisor/` returns nothing.
    - `PrintAdvisor.cmake` lists no removed file.
    - `SEND_TO_PRINTER` is not used by the Print Advisor. Without the chat's delegate it returns `AI_SEND_WORKFLOW_UNAVAILABLE` (SlicerBridgeActionsProcess.cpp:689-695).

    Nothing the Print Advisor uses is removed.
12. **`ToolCalls::ShouldSuppressSliceWarningForAI` is the only symbol the kept code takes from `simple/toolcalls/`** (include at SlicerBridgeState.cpp:3, call at :160).
    - The other `MCPToolCallsCommon` functions (`NativeExecutionModeName`, `BuildBlockingErrorsPayload`, `AppendWebViewLogLine`, `DismissNotificationEntry`, ...) have no caller outside the deleted files.
    - The function moves into the anonymous namespace of SlicerBridgeState.cpp (section 3.22). No `MCPToolCallsCommon` stub is kept.
13. **No kept class has methods defined in a deleted file.**
    - A grep of `GLCanvas3D::`, `Plater::`, `GUI_App::`, `MainFrame::`, `SlicerBridge::`, ... definitions in the deleted .cpp files finds none.
    - The deleted files define no `wxDEFINE_EVENT` that kept code uses.
    - The only `SlicerBridge` hook they install is `SetSendToPrinterDelegate` (MCPChatPanel.cpp:429/473).
14. **CXAGENT build settings.**
    - Root CMakeLists.txt:45-64 defines the `CXAGENT_*` cache variables, and cmake/buildinfo.h.in:34 emits `#define CXAGENT_DEFAULT_API_BASE`.
    - The only reader is a commented line (MCPChatPanel.cpp:20-21). No build script and no `.bat` passes `CXAGENT_*`.
    - `GENERATE_ORCA_HEADER` is ON in `build/CMakeCache.txt`, so `buildinfo.h` is regenerated at configure. That recompiles its 13 direct includers and the 52 files that include `common_header.h`. The GUI_App.hpp/Plater.hpp recompile already covers them.
    - The stale `CXAGENT_*` cache entries stay in `CMakeCache.txt`. They do no harm.
15. **Orphans left on purpose.** They compile fine and are cleaned in F5/F7:
    - `GLCanvas3D::open_device_list_popup`, `MainFrame::switch_to_device_page`;
    - `GUI_App::open_publish_page_dialog`, `open_mall_page_dialog`;
    - `Plater::publish_project` (empty), `BBLTopbar::OnPublishClicked`;
    - the `SlicerBridge` send-to-printer delegate API.

---

## 1. Files to delete (`git rm`)

| # | Files |
|---|---|
| 1 | `src/slic3r/GUI/simple/MCPChatPanel.hpp`, `MCPChatPanel.cpp` (contains `MCPChatWindow`) |
| 2 | `src/slic3r/GUI/simple/toolcalls/`: whole folder, 14 files (`MCPToolCalls*.cpp`, `MCPToolCallsCommon.hpp/.cpp`, `MCPChatPanelSAgentNative.cpp`) |
| 3 | `src/slic3r/GUI/simple/bridge/CxAgentClientBridge.hpp/.cpp`, `SAgentMqttBridge.hpp/.cpp`, `MCPChatPanelBridgeActionExecutor.cpp` |
| 4 | `src/slic3r/GUI/simple/bridge/CrealityPrint_AI_Chat_Setup_Guide.md`, `MCP_Chat_Dify_Integration_2447a5bd.md` |
| 5 | `src/slic3r/GUI/simple/sendWorkflow/`: Variant A, whole folder (12 files). Variant B: all but `EasyPrintSender.hpp/.cpp` (10 files) |
| 6 | `src/slic3r/GUI/CxAgentEndpointPolicy.hpp/.cpp` |
| 7 | `src/slic3r/GUI/ModelDetailDialog.hpp/.cpp` |
| 8 | `src/slic3r/GUI/WebModelLibraryView.hpp/.cpp` |
| 9 | `src/slic3r/GUI/print_manage/Routes/ModelLibraryRoutes.hpp/.cpp` |
| 10 | `src/slic3r/GUI/ModelMall.hpp/.cpp` |
| 11 | `src/slic3r/GUI/PublishDialog.hpp/.cpp` |
| 12 | `src/slic3r/GUI/AICloudServiceDialog.hpp/.cpp` |
| 13 | `src/slic3r/GUI/print_manage/App/mqtt_client.h/.cpp` |
| 14 | images (section 5) |

Totals:
- Variant A: 51 source and doc files, 22,213 lines.
- Variant B: 49 files.
- Plus 4 or 5 images.

---

## 2. CMake

### 2.1 `src/slic3r/CMakeLists.txt`

Delete these lines (HEAD numbers):

| Lines | Text |
|---|---|
| 235-236 | `GUI/CxAgentEndpointPolicy.cpp`, `.hpp` (F1 deletes 237-242 right below) |
| 374-375 | `GUI/print_manage/App/mqtt_client.h`, `.cpp` |
| 410-411 | `GUI/print_manage/Routes/ModelLibraryRoutes.hpp`, `.cpp` |
| 433-436 | `GUI/WebModelLibraryView.hpp/.cpp`, `GUI/ModelDetailDialog.hpp/.cpp` |
| 603-604 | `GUI/PublishDialog.cpp`, `.hpp` |
| 615-616 | `GUI/ModelMall.hpp`, `.cpp` |
| 797-798 | `GUI/AICloudServiceDialog.hpp`, `.cpp` (tab-indented) |
| 852-853 | `GUI/simple/sendWorkflow/EasyPrintSender.hpp/.cpp` (**Variant A only**) |
| 859-868 | `GUI/simple/sendWorkflow/` `AISendWorkflowService`, `AIProcessPresetIntentResolver`, `AIProcessSwitchService`, `CxCloudPrintClient`, `CxCloudPrintExecutor` (.hpp/.cpp) |
| 869-870 | `GUI/simple/MCPChatPanel.hpp/.cpp` |
| 871-884 | every `GUI/simple/toolcalls/...` line |
| 886-889 | `GUI/simple/bridge/CxAgentClientBridge.hpp/.cpp`, `SAgentMqttBridge.hpp/.cpp` |
| 894 | `GUI/simple/bridge/MCPChatPanelBridgeActionExecutor.cpp` |

Keep:
- 885 (`SlicerAction.hpp`);
- 890-893 and 895-901 (SlicerBridge core).

Lines 852-901 are inside `if(GUI_SIMPLE) list(APPEND SLIC3R_GUI_SOURCES ...)`. The `)` at 902 and the `endif()` at 903 stay.

### 2.2 Root `CMakeLists.txt`

Delete lines 45-65:
- the `CXAGENT_DEBUG_API_BASE` and `CXAGENT_RELEASE_API_BASE` cache sets;
- the three `$ENV{CXAGENT_*}` overrides;
- the `CXAGENT_DEFAULT_API_BASE` if/else;
- the `message(STATUS ...)` and the blank line after it.

Lines 44 (blank) and 66 (`include(ConfigureAllModules)`) stay.

### 2.3 `cmake/buildinfo.h.in`

Delete line 34, `#define CXAGENT_DEFAULT_API_BASE "@CXAGENT_DEFAULT_API_BASE@"`.

### 2.4 `src/CMakeLists.txt`

No change. Paho (`find_package(PahoMqttCpp)`, 162-163) stays for `CloudDeviceMqttTransport` until F4.

---

## 3. Edits per file

"Delete" means delete the lines including their trailing newline. Line numbers are HEAD. The anchor is authoritative.

### 3.1 `src/slic3r/GUI/GUI_App.cpp`

Apply bottom-up:

| HEAD lines | Anchor | Action |
|---|---|---|
| 14675-14678 | comment `// AI / ...AIChatPage...` (3 lines) + `NotifyAIChatEditionChanged();` at the end of `Update_easy_mode_flag` | Delete. The function now ends with the `topbar()->Refresh();` lines. |
| 14536-14624 | `void GUI_App::OpenEshopRecommendedGoods(const std::string& materialColor, ...` through its closing `}` (the line after `.perform();`) | Delete the whole function. It sits after `} //Slic3r` at 14535; leave that. `bool GUI_App::easy_mode()` (14626) stays. |
| 14480 | `NotifyAIChatLoginStatusChanged();` (the `else` branch, after `app_config->set("cloud", "token", m_user.token);`) | Delete |
| 14470 | `NotifyAIChatLoginStatusChanged();` (the `if` branch) | Delete |
| 13577-13584 | `void GUI_App::remove_mall_system_dialog()` + body + blank line | Delete |
| 13394 | `delete m_mall_publish_dialog;` inside `remove_ping_bind_dialog` | Delete (wrong member; see 0.8) |
| 12207 | `AICloudService::getInstance()->cleanup();` in `load_language` | Delete |
| 8186 | `NotifyAIChatLoginStatusChanged();` (after `app_config->set("cloud", "token", m_user.token);`) | Delete |
| 6667 | `NotifyAIChatLoginStatusChanged(true);` in `request_user_logout` | Delete |
| 6189 | `DestroyAIChatPanelsForGUIRecreate();` in `recreate_GUI` | Delete; keep `update_http_extra_header();` |
| 5815-5822 | in `Update_dark_mode_flag`: the comment `// 当主题偏好被切换时...`, `// 1. 更新在线模型库...`, (F2 already deleted the `if (mainframe) { update_model_webviews_user_agent(); }` block), the blank line, `// 2. 通知AI工具重新应用主题`, `NotifyAIChatThemeChanged();` | Delete. The function body becomes the two lines `m_is_dark_mode = ...` and `BOOST_LOG_TRIVIAL(...)`. |
| 3882-3898 | `std::map<std::string, std::string> GUI_App::get_modellibrary_header()` + body + blank line | Delete. The only caller was WebModelLibraryView.cpp. |
| 2095 | `NotifyAIChatLoginStatusChanged();` (after `app_config->set("cloud", "token", m_user.token);`) | Delete |
| 2057 | `NotifyAIChatLoginStatusChanged();` (before `CallAfter([this] {`) | Delete |
| 252 | `#include "AICloudServiceDialog.hpp"` | Delete |
| 191 | `#include "ModelMall.hpp"` | Delete |
| 131 | `#include "slic3r/GUI/WebModelLibraryView.hpp"` | Delete |
| 40 | `#include "slic3r/GUI/simple/MCPChatPanel.hpp"` | Delete |

Notes:
- F2 deletes `reload_region_sensitive_views` (1886-1916), which contains `get_modellibrary_view` (1899) and `get_cloud_webaddress` (1905). Check it is gone.
- Each `NotifyAIChat*` call is a standalone statement (8 in GUI_App.cpp). Deleting it does not change the surrounding control flow.

### 3.2 `src/slic3r/GUI/GUI_App.hpp`

| HEAD line | Text | Action |
|---|---|---|
| 816 | `std::map<std::string, std::string> get_modellibrary_header();` | Delete |
| 771-775 | comment `// Open e-shop recommended goods...` (2 lines) + `void OpenEshopRecommendedGoods(...);` (3 lines) | Delete, and the blank line before `#if ENABLE_THUMBNAIL_GENERATOR_DEBUG` if you want |
| 702 | `void remove_mall_system_dialog();` | Delete |
| 691 | `ModelMallDialog* m_mall_publish_dialog{ nullptr };` | Delete |
| 95 | `class ModelMallDialog;` | Delete |

### 3.3 `src/slic3r/GUI/MainFrame.cpp`

| HEAD lines | Anchor | Action |
|---|---|---|
| 2975-3017 | `// Dev tool: dump the slicer parameter schema to a JSON file.` through `#endif // ENABLE_AI_CONFIG_SCHEMA_EXPORT` + blank line | Delete. `#ifdef __APPLE__` follows. |
| 1245-1250 | in `MainFrame::shutdown`: the 4-line comment `// Tear down the floating/embedded AI chat windows ...` + `DestroyAIChatPanelsForGUIRecreate();` + blank line | Delete. Keep `advisor_on_shutdown();` (1243) first and `// BBS: backup` next. |
| 553 | `wxGetApp().remove_mall_system_dialog();` (after `// propagate event` and a blank line) | Delete |
| 103 | `#include "slic3r/GUI/simple/bridge/SlicerBridge.hpp"` | Delete (used only by the deleted dev menu) |
| 102 | `#include "slic3r/GUI/simple/MCPChatPanel.hpp"` | Delete |
| 93 | `#include "slic3r/GUI/WebModelLibraryView.hpp"` | Already deleted by F2. Delete it if it is still there. |

### 3.4 `src/slic3r/GUI/MainFrame.hpp`

- Line 51, `class WebModelLibraryView;`: delete. F2 deletes the members and getters at 380-382 and 415-416, but not this forward declaration.

### 3.5 `src/slic3r/GUI/GUI.cpp` and `GUI.hpp`

- **GUI.cpp:698-724**: delete `std::string get_cloud_webaddress()` with its body and the blank line. `static std::string get_file_md5_streaming_impl` follows.
- **GUI.hpp:82**: delete `std::string get_cloud_webaddress();`.
- After F2 (which deletes `reload_region_sensitive_views`) and F3, there is no caller left. If F2 is not applied, keep both.
- Keep `get_cloud_api_url` (F4).

### 3.6 `src/slic3r/GUI/Plater.cpp`: AI chat dock and notifications

Apply bottom-up:

| HEAD lines | Anchor | Action |
|---|---|---|
| 24970 | `Slic3r::GUI::NotifyAIChatSceneChanged();` after `p->on_plate_selected(event);` (in `Plater::select_plate`) | Delete |
| 20374-20383 | `if (imported_from_add_file) { nlohmann::json import_event; ... NotifyAIChatModelImported(import_event); }` | Replace the whole block with `(void) imported_from_add_file; // only the removed AI chat consumed this flag`. Do not delete the variable: its assignments wrap `open_3mf_file`/`load_files` calls. |
| 17539-17552 | blank line + `json import_event;` ... `Slic3r::GUI::NotifyAIChatModelImported(import_event);` + `Slic3r::GUI::NotifyAIChatSceneChanged();` | Delete. Keep the `}` at 17553 that closes the success branch. `get_cloud_model_download()` is only read for this event. |
| 10924-10925 | first statement of `Plater::priv::selection_changed()`: `Slic3r::GUI::NotifyAIChatSceneChanged();` + blank line | Delete |
| 8264-8319 | `void Plater::priv::ensure_embedded_ai_chat_panel()` and `void Plater::priv::update_easy_mode_ai_layout()` (both whole) | Delete. `// BBS` + `statusbar()` follows. |
| 8111-8262 | `bool Plater::priv::easy_mode_ai_dock_enabled() const` through `on_easy_ai_dock_resize_capture_lost` (all dock helpers, contiguous, with blank lines) | Delete. It starts right after `update_ui_from_settings()` ends at 8109. |
| 8108 | `update_easy_mode_ai_layout();` (last line of `update_ui_from_settings`) | Delete |
| 7811 | `update_easy_mode_ai_layout();` before `update_sidebar();` | Delete |
| 7543 | `update_easy_mode_ai_layout();` after `set_current_panel(...)` | Delete |
| 7243-7248 | the 6 `m_easy_ai_resize_handle->Bind(...)` lines (and the whitespace line 7249 if you want) | Delete |
| 7237-7238 | `m_panel_3d_sizer->Add(m_left_ai_host, ...)`, `m_panel_3d_sizer->Add(m_easy_ai_resize_handle, ...)` | Delete. Keep 7239 `Add(m_main_view_host, 1, ...)`. |
| 7227-7228 | `auto* left_ai_sizer = new wxBoxSizer(wxVERTICAL);`, `m_left_ai_host->SetSizer(left_ai_sizer);` | Delete |
| 7161-7164 | `m_left_ai_host->Hide();`, `m_easy_ai_resize_handle->Hide();`, `...->SetCursor(...)`, `...->SetBackgroundColour(...)` | Delete |
| 7158-7159 | `m_left_ai_host = new wxPanel(m_panel_3d_root);`, `m_easy_ai_resize_handle = new wxPanel(m_panel_3d_root);` | Delete. Keep 7157 `m_panel_3d_root` and 7160 `m_main_view_host`. |
| 6554-6570 | declarations `easy_mode_ai_dock_enabled` ... `on_easy_ai_dock_resize_capture_lost` (17 lines) | Delete. Keep `update_ui_from_settings();` (6553) and `// BBS` (6571). |
| 6330-6337 | `MCPChatPanel* m_embedded_ai_chat_panel`, `m_easy_ai_dock_width_px`, `m_easy_ai_layout_initialized`, `m_last_easy_mode_state`, `m_restore_floating_ai_window_on_normal_mode`, `m_easy_ai_dock_dragging`, `m_easy_ai_dock_drag_start_x`, `m_easy_ai_dock_drag_start_width_px` | Delete. All are used only by the deleted functions (`m_restore_floating_ai_window_on_normal_mode` is never read). |
| 6325-6326 | `wxPanel* m_left_ai_host{nullptr};`, `wxPanel* m_easy_ai_resize_handle{nullptr};` | Delete |
| 6172-6180 | `kEasyModeLayoutSection`, `kEasyAIDockEnabledKey`, `kEasyAIDockWidthKey`, `kEasyAIDockWidthVersionKey`, `kEasyAIDockDefaultWidth`, `kEasyAIDockMinWidth`, `kEasyAIDockMaxWidth`, `kEasyAIDockResizeHandleWidth`, `kEasyAIDockWidthVersion` | Delete. All are used only by the deleted helpers. GLCanvas3DSimple.cpp has its own `kEasyModeLayoutSection`. |
| 120 | `#include "simple/MCPChatPanel.hpp"` | Delete |
| 15 | `#include "slic3r/GUI/simple/MCPChatPanel.hpp"` | Delete |

The layout after the change: `m_panel_3d_root` holds only `m_main_view_host` (proportion 1), so the 3D view fills the area exactly as in Pro mode today, where the dock was hidden.

The `easy_mode_layout` AppConfig section (`easy_ai_dock_*` keys) stays in existing user `.conf` files. Nothing reads it any more, and no cleanup code is added (that would touch libslic3r AppConfig.cpp).

### 3.7 `src/slic3r/GUI/Plater.cpp`: AI cloud service

| HEAD lines | Anchor | Action |
|---|---|---|
| 20422 | `void Plater::select_ai_cloud_service() { p->select_ai_cloud_service(); }` | Delete |
| 7971-7987 | `void Plater::priv::select_ai_cloud_service()` + body + blank line. F2 already changed the `select_tab(MainFrame::tpHome)` at 7980. | Delete |
| 7425 | `view3D_canvas->Bind(EVT_GLVIEWTOOLBAR_AI_CLOUD_SERVICE, [q](SimpleEvent&) { q->select_ai_cloud_service(); });` | Delete |
| 6489 | `void select_ai_cloud_service();` (priv declaration) | Delete |
| 214 | `#include "AICloudServiceDialog.hpp"` | Delete |

`CommunicateWithCXCloud` / `CXCloudDataCenter` stay used elsewhere in Plater.cpp. Their includes stay (F4).

### 3.8 `src/slic3r/GUI/Plater.cpp`: ModelMall and EasyPrintSender includes

- 150 `#include "ModelMall.hpp"`: delete.
- 231 `#include "simple/sendWorkflow/EasyPrintSender.hpp"`: delete (Variant A; also fine in B, since no symbol is used).

### 3.9 `src/slic3r/GUI/Plater.cpp` and `Plater.hpp`: PublishDialog and the publishing state

Plater.cpp, bottom-up:

| HEAD lines | Anchor | Action |
|---|---|---|
| 25507 | `bool Plater::show_publish_dialog(bool show) { return p->show_publish_dlg(show); }` (+ blank line) | Delete |
| 23294-23300 | `void Plater::publish_job_finished(wxCommandEvent& evt)` + body + blank line | Delete |
| 15706-15732 | `void Plater::priv::update_publish_dialog_status(...)` and `bool Plater::priv::show_publish_dlg(bool show)` + bodies (ends before `// BBS: add bed exclude area`) | Delete |
| 15329-15330 | `int Plater::get_publish_finished_event() { return EVT_PUBLISH_FINISHED; }` + blank line | Delete |
| 14368-14394 | `void Plater::priv::on_action_publish(wxCommandEvent& event)` + body + blank line | Delete |
| 14361-14362 | `if (!m_is_publishing)` / `q->select_view_3D("Preview");` | Replace with the single line `q->select_view_3D("Preview");` (same indentation) |
| 14176-14182 | `if (m_is_publishing) { if (m_publish_dlg && !m_publish_dlg->was_cancelled()) { ... UpdateStatus(msg, percent, false); } }` | Delete. Keep `q->Thaw();` before it and the `}` after it. |
| 14135-14144 | `/* BBS if in publishing progress */` + `if (m_is_publishing) { ... q->publish_project(); ... }` | Delete. Keep `q->SetDropTarget(...)` (14145). |
| 14114-14122 | `// BBS stop publishing if error occur` + the commented `// if (m_is_publishing) {...//}` block | Delete (comment only) |
| 14070-14073 | `// BBS` + `if (m_is_publishing) { m_publish_dlg->cancel(); }` | Delete. The `if (evt.cancelled()) {` branch keeps `ready_to_slice = true;` and the comment. |
| 7531 | `q->Bind(EVT_PUBLISH_FINISHED, [q](wxCommandEvent& evt) { q->publish_job_finished(evt); });` | Delete |
| 7133 | `this->q->Bind(EVT_PUBLISH, &priv::on_action_publish, this);` | Delete |
| 6807-6808 | `bool show_publish_dlg(bool show = true);`, `void update_publish_dialog_status(wxString& msg, int percent = -1);` | Delete. Keep the comment at 6806 or delete it too. |
| 6789 | `void on_action_publish(wxCommandEvent& evt);` | Delete |
| 6358 | `bool m_is_publishing{false};` | Delete |
| 6296 | `PublishDialog* m_publish_dlg = nullptr;` | Delete |
| 415 | `wxDEFINE_EVENT(EVT_PUBLISH_FINISHED, wxCommandEvent);` | Delete |
| 408 | `wxDEFINE_EVENT(EVT_PUBLISH, wxCommandEvent);` | Delete |
| 149 | `#include "PublishDialog.hpp"` | Delete |

Plater.hpp:

| HEAD line | Text | Action |
|---|---|---|
| 830 | `bool show_publish_dialog(bool show = true);` | Delete |
| 713 | `int get_publish_finished_event();` | Delete (no caller anywhere) |
| 629 | `void publish_job_finished(wxCommandEvent& evt);` | Delete |
| 494 | `void select_ai_cloud_service();` | Delete (section 3.7) |
| 103 | `wxDECLARE_EVENT(EVT_PUBLISH, wxCommandEvent);` | Delete |
| 98-99 | `#define EVT_PUBLISHING_START 1`, `#define EVT_PUBLISHING_STOP 2` (and the blank line 100) | Delete |

`Plater::publish_project()` (Plater.cpp:22235, empty, Plater.hpp:573) stays: it no longer has callers and is cleaned in F5.

### 3.10 `src/slic3r/GUI/BBLTopbar.cpp`

| HEAD lines | Anchor | Action |
|---|---|---|
| 1548-1550 | `#ifdef ENABLE_PUBLISHING` / `wxGetApp().plater()->show_publish_dialog();` / `#endif` in `OnPublishClicked` | Delete (dead branch, but it names a deleted function) |
| 1062-1064 | `if (!cxagent_hidden_switch_enabled() && cxagent_config_is_dev())` / `set_cxagent_config_api_base(cxagent_restore_api_base(), true);` / `pCtr->SetDevMode(cxagent_hidden_switch_enabled() && cxagent_config_is_dev());` | Delete. Keep `pCtr->InsertPage(...)` above and `//pCtr->InsertPage(3, ...)` / `pCtr->reLayout();` below. |
| 461-486 | inside `activate_page` in `ButtonsCtrl::InsertPage`: the whole `if (cxagent_hidden_switch_enabled()) { static int prepare_click_count ... }` block (5 clicks on Prepare in 3 s toggles the cxagent dev endpoint) | Delete. The lambda becomes: `int id = btn->GetId();` then a blank line, then `wxCommandEvent evt = ...; evt.SetId(id); wxPostEvent(this->GetParent(), evt);`. Keep the `[this, btn]` capture: `this->GetParent()` is still used. |
| 277-283 | `void ButtonsCtrl::SetDevMode(bool dev_mode) { m_dev_mode = dev_mode; ApplyButtonStyle(...); Refresh(); Update(); }` + blank line | Delete |
| 116-190 | `namespace {` ... `cxagent_hidden_switch_enabled`, `cxagent_config_is_dev`, `cxagent_config_api_base`, `cxagent_restore_api_base`, `set_cxagent_config_api_base`, `set_active_cxagent_api_base` (calls `GetActiveAIChatPanel()->SetCxAgentApiBaseAndReload`), `save_cxagent_api_base_before_dev` ... `} // namespace` + blank line | Delete the whole anonymous namespace. It contains nothing else. `wxDECLARE_EVENT(wxCUSTOMEVT_NOTEBOOK_SEL_CHANGED, ...)` at 192 stays. |
| 111 | `bool m_dev_mode{false};` (ButtonsCtrl member) | Delete |
| 100-101 | `void SetDevMode(bool dev_mode);`, `bool IsDevMode() const { return m_dev_mode; }` | Delete |
| 43-45 | `static const char* CXAGENT_PROD_API_BASE = ...`, `CXAGENT_DEV_API_BASE`, `CXAGENT_API_BASE_BEFORE_DEV_KEY` | Delete |
| 29 | `#include "simple/MCPChatPanel.hpp"` | Delete |

Notes:
- F2 rewrites `ApplyButtonStyle` (269-274), `InsertPage` (438-444), the topbar insertions (1056-1061) and deletes `EasyModeSwitchCtrl` (541-669, which holds the commented `ShowProAISliceAssistantWithEmbeddedSession` at 660). The F3 edits above do not overlap with them. Apply F3 after F2, by anchor.
- `#include <boost/algorithm/string/predicate.hpp>` (20) may become unused. Leave it.
- The `cxagent_api_base` and `cxagent_api_base_before_dev` keys stay in existing `.conf` files, unread.

### 3.11 `src/slic3r/GUI/GLCanvas3D.cpp`

| HEAD lines | Anchor | Action |
|---|---|---|
| 11863-11867 | first statement of `_render_imgui_select_plate_toolbar`: `if (should_hide_easy_mode_scene_side_controls()) { if (!m_render_preview) m_render_preview = true; return; }` | Delete |
| 11724 | `return (GetEmbeddedAIChatPanel() != nullptr ? 24.0f : 10.0f) * scale;` in `get_easy_mode_overlay_safe_left_px` | Replace with `return 10.0f * scale;` |
| 10507-10523 | first statement of `_render_cliper`: `if (should_hide_easy_mode_scene_side_controls()) { ...reset cliper slider, clipping planes...; return; }` | Delete |
| 9330-9363 | toolbar item block `{ item.name = "ai_cloud_service"; ... if (!m_main_toolbar.add_item(item)) return false; }` | Delete, including the blank line before `GLToolbarItem::Data virtual_item;` if it duplicates. |
| 4598-4601 | `if (!should_hide_easy_mode_scene_side_controls()) { IMSlider* cliper = ...; cliper->on_mouse_wheel(evt); }` | Replace with the two inner lines, de-indented: `IMSlider* cliper = get_gcode_viewer().get_cliper_slider();` / `cliper->on_mouse_wheel(evt);` |
| 4538 | `} else if (m_gizmos.is_enabled() && m_canvas_type == CanvasView3D && !should_hide_easy_mode_scene_side_controls()) {` | Replace with `} else if (m_gizmos.is_enabled() && m_canvas_type == CanvasView3D) {` |
| 3521-3523 | `// Push updated scene state to the active AI chat host.` + `NotifyAIChatSceneChanged();` + blank line | Delete |
| 211-219 | `namespace {` / `bool should_hide_easy_mode_scene_side_controls() { return wxGetApp().easy_mode() && GetEmbeddedAIChatPanel() != nullptr; }` / `} // namespace` + blank line | Delete |
| 66 | `#include "simple/MCPChatPanel.hpp"` | Delete |

Notes:
- Behaviour is unchanged: without the chat panel, `should_hide_...` was already always false, and `GetEmbeddedAIChatPanel()` returned nullptr outside easy mode.
- F2 deletes `_render_ai_chat_toggle();` at 11510 and the GLCanvas3D.hpp members. F3 does not touch GLCanvas3D.hpp.
- Safety: add `#include <unordered_set>` after the existing `#include <...>` block (GLCanvas3D.cpp uses `std::unordered_set` at 6135 and 6289 and does not include it directly). **[inference]** It probably arrives through other headers; the include is harmless.

### 3.12 `src/slic3r/GUI/GLToolbar.cpp` and `GLToolbar.hpp`

- GLToolbar.cpp:68 `wxDEFINE_EVENT(EVT_GLVIEWTOOLBAR_AI_CLOUD_SERVICE, SimpleEvent);`: delete.
- GLToolbar.hpp:65 `wxDECLARE_EVENT(EVT_GLVIEWTOOLBAR_AI_CLOUD_SERVICE, SimpleEvent);`: delete.

### 3.13 `src/slic3r/GUI/Jobs/ArrangeJob.cpp` and `OrientJob.cpp`

ArrangeJob.cpp (bottom-up):
- 1054-1057: `report_arrange_completion(false, "Arrange failed while applying results", {{"code", "AUTO_ARRANGE_FINALIZE_FAILED"}});`. Delete; the `catch (...)` keeps its log and `show_error`.
- 1044-1047: the same call with `{"exception", ex.what()}`. Delete.
- 1033-1036: `report_arrange_completion(true, "Arrange completed", {...});`. Delete; the `#endif` of `AUTO_CONVERT_3MF` stays before it.
- 850-853: the `report_arrange_completion(false, canceled ? ...);` before `return;`. Delete; `return;` stays.
- 822-829: the lambda `auto report_arrange_completion = [&](...) { if (wxGetApp().easy_mode()) { if (auto* panel = GetEmbeddedAIChatPanel()) panel->CompletePendingAsyncToolCall("job:auto_arrange", ...); } };`. Delete.
- 17: `#include "slic3r/GUI/simple/MCPChatPanel.hpp"`. Delete.

OrientJob.cpp (bottom-up):
- 259-262: `report_orient_completion(true, "Auto orient completed", {...});`. Delete.
- 245-248: `report_orient_completion(false, canceled ? ...);` before `return;`. Delete.
- 227-234: the lambda `auto report_orient_completion = [&](...) {...};` and the blank line. Delete.
- 11: `#include "slic3r/GUI/simple/MCPChatPanel.hpp"`. Delete.

### 3.14 `src/slic3r/GUI/PartPlate.cpp`

- 4217-4218: `if (m_plater)` / `Slic3r::GUI::NotifyAIChatSceneChanged();` at the end of `PartPlateList::select_plate`. Delete; `return 0;` stays.
- 84: `namespace GUI { void NotifyAIChatSceneChanged(); }`. Delete; 83 `namespace Slic3r {` and 85 `namespace GUI {` stay.

### 3.15 `src/slic3r/GUI/print_manage/Routes/DeviceMgrRoutes.cpp`

| HEAD lines | Anchor | Action |
|---|---|---|
| 1243-1264 | `this->Handler({"buy_filament_cmd"}, ...` through `});` (eshop + click analytics) | Delete. `this->Handler({"track_learn_about_cfs"} ...` follows. |
| 567 | `Slic3r::GUI::NotifyAIChatSceneChanged();` after `wxPostEvent(... EVT_CURRENT_DEVICE_CHANGED)` | Delete |
| 242 | `Slic3r::GUI::NotifyAIChatSceneChanged();` inside `if (DM::DataCenter::Ins().is_current_device_changed()) {` | Delete; the `wxPostEvent` stays. |
| 235 | `Slic3r::GUI::NotifyAIChatDeviceStatusChanged();` in `update_devices` | Delete |
| 25 | `#include "slic3r/GUI/simple/MCPChatPanel.hpp"` | Delete |

F1 may already have edited the analytics lines inside the `buy_filament_cmd` handler. Delete whatever remains of the handler.

### 3.16 `src/slic3r/GUI/print_manage/App/SendToPrinter.cpp`

- 520-539: `RegisterHandler("buy_filament_cmd", [this](const nlohmann::json& json_data) { ... });`. Delete. The closing `}` of `CxSentToPrinterDialog::bind_events()` (540) stays. The function then ends with the `get_webrtc_local_param` handler.

### 3.17 `src/slic3r/GUI/print_manage/Routes/AppRoutes.cpp`

- 40-41: `if (ModelLibraryRoutes().Invoke(browser, j, data))` / `return true;`. Delete.
- 11: `#include "ModelLibraryRoutes.hpp"`. Delete.

### 3.18 `src/slic3r/GUI/Preferences.cpp`

- 26: `#include "WebModelLibraryView.hpp"`. Delete (no symbol used).

### 3.19 `src/slic3r/GUI/print_manage/App/PrinterMgrView.cpp`

- 75: `#include "simple/sendWorkflow/EasyPrintSender.hpp"`. Delete (no symbol used; mandatory in Variant A).
- 547-554: the comment mentions `EasyPrintSender::startPrintLan / AISendWorkflowService::on_cloud_print_success`. Optional: shorten to `// Switching to the device page no longer auto-opens the current device detail page.`

### 3.20 `src/slic3r/GUI/simple/DeviceListSimple.cpp`

- 887-912: the whole `if (device_switch_applied) { const std::string switched_mac = mac; ... wxGetApp().CallAfter([...]() { ...; NotifyAIChatProjectWorkflowResetRequested(payload); }); }` block. Delete. The `return true;` at 914 stays. The `bool device_switch_applied` flag (860) is still read at 873.
- 34: `#include "sendWorkflow/EasyPrintSender.hpp"`. Delete (no symbol used).
- 4: `#include "MCPChatPanel.hpp"`. Delete.

### 3.21 `src/slic3r/GUI/simple/GLCanvas3DSimple.cpp`

This is easy-mode code, compiled but unreachable after F2 (`easy_mode()` returns false). F7 deletes it. In F3 it only has to compile without the chat. Line numbers at or after 160 shift after F2 (F2 deletes 160-163, 1219-1238 and 1963-2101): use the anchors.

| HEAD lines | Anchor | Action |
|---|---|---|
| 2144 | `MCPChatWindow::Hide();` in the `else` branch of `GLCanvas3D::on_easy_mode_switch()` | Delete. The `ObjectList* obj_list ...` sync stays. |
| 2035, 2093 | `MCPChatWindow::Show/Toggle` inside `_render_ai_chat_toggle` | Already deleted by F2 (1963-2101) |
| 1934-1935 | `if (!should_embed_easy_mode_left_workspace())` + `_render_resident_filament_panel_simple(_compute_resident_filament_rect_simple());` | Replace with the inner line alone, de-indented |
| 1740 | `m_imgui_filament_panel->set_embed_in_unified_left_panel(should_embed_easy_mode_left_workspace());` | Replace the argument with `false` |
| 869 | `OpenActiveAISliceWorkflowFromScene();` (inside the "Slice plate" button `if`) | Replace with `wxGetApp().mainframe->slice_plate(MainFrame::eSlicePlate);`. `can_slice` already checks `wxGetApp().mainframe`. |
| 863-864 | `const WorkflowToolbarState workflow_toolbar_state = GetAIWorkflowToolbarState();` + the `const bool can_slice = ...workflow_toolbar_state...;` line | Replace both with `const bool can_slice = (wxGetApp().mainframe && wxGetApp().mainframe->get_enable_slice_status());` |
| 723 | `OpenActiveAISliceWorkflowFromScene();` | Same replacement as 869 |
| 717-718 | same pair as 863-864 | Same replacement |
| 189-193 | in `open_easy_mode_send_workflow_from_scene()`: `if (OpenActiveAISendWorkflowCardFromScene()) return;`, the blank line, the 2-line `BOOST_LOG_TRIVIAL(warning) ... fallback to legacy send flow";` | Delete. The function keeps only the `print_plate(MainFrame::eSendToLocalNetPrinter)` fallback. |
| 182-186 | `static bool should_embed_easy_mode_left_workspace()` (returns `GetEmbeddedAIChatPanel() != nullptr`) + blank line | Delete |
| 323-324 | body of `GLCanvas3D::check_filament_mapping_simple()`: the comment + `refresh_ai_send_mapping_for_current_device_simple(true);` | Replace with `// The AI send-mapping refresh was removed with the AI chat (F3).` Keep the empty function: it is declared in GLCanvas3D.hpp and called by the 3MF import hook. |
| 113-127 | `namespace {` + `void refresh_ai_send_mapping_for_current_device_simple(bool auto_match) {...}` (uses `MCPChatWindow::Get()`) + `} // namespace` + blank line | Delete. `struct ImGuiDisableScope` (128) follows. |
| 104 | `#include "sendWorkflow/EasyPrintSender.hpp"` | Delete |
| 4 | `#include "MCPChatPanel.hpp"` | Delete |

### 3.22 `src/slic3r/GUI/simple/bridge/SlicerBridgeState.cpp`

1. Delete line 3, `#include "slic3r/GUI/simple/toolcalls/MCPToolCallsCommon.hpp"`.
2. Right after `namespace {` (line 42, inside `namespace Slic3r { namespace GUI { namespace Bridge {`), insert:

```cpp
// Moved here from the removed simple/toolcalls/MCPToolCallsCommon.cpp (F3): slice warnings
// hidden from agent-facing slice summaries. The bed-temperature notice is advisory only.
bool ShouldSuppressSliceWarningForAI(const std::string& message, const std::string& error_code)
{
    return message == "bed_temperature_too_high_than_filament" || error_code == "1000C001";
}
```

3. Line 160: replace `if (ToolCalls::ShouldSuppressSliceWarningForAI(warning.msg, warning.error_code))` with `if (ShouldSuppressSliceWarningForAI(warning.msg, warning.error_code))`. The call is inside the same anonymous namespace (42-432), after the new definition.
4. Optional: the comment at 668, `// Action implementations (migrated from MCPChatPanel)`, becomes `// Action implementations`.

Notes:
- `<string>` comes through `SlicerBridge.hpp`.
- F2 deletes the `tp*` cases at 687-692: there is no overlap.

### 3.23 `src/slic3r/GUI/simple/bridge/SlicerBridge.hpp`

| HEAD lines | Action |
|---|---|
| 180 | Delete `CachedModelSearchResult m_cached_model_search_result;` |
| 163-174 | Delete `struct CachedModelSearchResult { ... };` and the blank line after it |
| 128-131 | Delete the `DoImportModelFromSearch`, `DoOpenModelLibrary`, `DoRecommendModel` and `DoSmartModelSearch` declarations |
| 90-98 | Delete the "Dev-only helper" comment, `static bool ExportConfigSchemaToFile(...)` and the blank line |
| 71-72 | Delete `void ClearPendingModelSearchCache();` and the blank line |
| 33 | Optional: `/// Usage from MCPChatPanel:` becomes `/// Usage (Print Advisor):` |

Keep:
- `SetSendToPrinterDelegate`, `ClearSendToPrinterDelegate` and their members. `DoSendToPrinter` answers `AI_SEND_WORKFLOW_UNAVAILABLE` without a delegate.
- `BuildConfigSchemaArray`, `BuildPanelNameMap`, `AnalyzeOverhang`.

### 3.24 `src/slic3r/GUI/simple/bridge/SlicerAction.hpp`

- Lines 63-66: delete `IMPORT_MODEL_FROM_SEARCH`, `OPEN_MODEL_LIBRARY`, `RECOMMEND_MODEL` and `SMART_MODEL_SEARCH`.
- The only other user was CxAgentClientBridge.cpp, which is deleted.

### 3.25 `src/slic3r/GUI/simple/bridge/SlicerBridgeActionRegistry.cpp`

- Lines 217-257: delete the four `RegisterAction(...)` blocks for `OPEN_MODEL_LIBRARY`, `RECOMMEND_MODEL`, `SMART_MODEL_SEARCH` and `IMPORT_MODEL_FROM_SEARCH`, with their blank lines.
- After the deletion, the `DoImportModel` block (ends at 215) is followed by `RegisterAction({ ActionID::AUTO_ORIENT, ...` (258).

### 3.26 `src/slic3r/GUI/simple/bridge/SlicerBridgeActionsObject.cpp`

- Lines 404-919: delete the following contiguous functions:
  - `ClearPendingModelSearchCache`;
  - `DoOpenModelLibrary` (as rewritten by F2);
  - `DoRecommendModel`;
  - `DoSmartModelSearch`;
  - `DoImportModelFromSearch`.

  Keep `DoImportModel` (ends at 402) and `json SlicerBridge::DoMoveObject` (920).
- Line 24: delete `#include "slic3r/GUI/WebModelLibraryView.hpp"`.
- Optional: line 26 `#include "slic3r/Utils/Http.hpp"` and line 32 `#include <random>` become unused, because they were only used in 436-918. They can be deleted.

### 3.27 `src/slic3r/GUI/simple/bridge/SlicerBridgeActionsConfig.cpp`

- Lines 1021-1052: delete `bool SlicerBridge::ExportConfigSchemaToFile(...)` and the blank line.
- `BuildConfigSchemaArray` (ends at 1019) and `DoGetConfigOptions` (1053) stay.
- `<fstream>` may become unused. Leave it.

### 3.28 Variant B only: `simple/sendWorkflow/EasyPrintSender.{hpp,cpp}`

Skip this section in Variant A, where the files are deleted.

EasyPrintSender.cpp:
- Delete line 2, `#include "CxCloudPrintExecutor.hpp"`.
- The constructor (28-31) gets an empty body.
- `tryStartPrintCloudClosedLoop` (49-107): replace the body with `(void) deviceId; (void) fileKey; (void) printData; (void) uploadResult; return false; // cloud closed loop removed (F3)`.

EasyPrintSender.hpp:
- Delete member 121, `std::shared_ptr<class CxCloudPrintExecutor> m_cloud_print_executor;`.
- Keep `CloudPrintCallbacks`, `setCloudClosedLoopEnabled` and `setCloudPrintCallbacks`. They compile without the executor.

F1 already removed the survey include (17) and the survey lines (702-703). F2 removed 873-874.

---

## 4. `localization/i18n/list.txt`

Delete these lines by content (HEAD numbers; F1 deletes other lines first, so the numbers shift):

| HEAD line | Path |
|---|---|
| 58 | `src/slic3r/GUI/CxAgentEndpointPolicy.cpp` |
| 63 | `src/slic3r/GUI/WebModelLibraryView.hpp` |
| 206 | `src/slic3r/GUI/print_manage/App/mqtt_client.cpp` |
| 241 | `src/slic3r/GUI/print_manage/Routes/ModelLibraryRoutes.cpp` |
| 245 | `src/slic3r/GUI/print_manage/Routes/ModelLibraryRoutes.hpp` |
| 307 | `src/slic3r/GUI/AICloudServiceDialog.hpp` |
| 419 | `src/slic3r/GUI/CxAgentEndpointPolicy.hpp` |
| 432 | `src/slic3r/GUI/ModelDetailDialog.cpp` |
| 449 | `src/slic3r/GUI/AICloudServiceDialog.cpp` |
| 468 | `src/slic3r/GUI/PublishDialog.cpp` |
| 580 | `src/slic3r/GUI/WebModelLibraryView.cpp` |
| 647 | `src/slic3r/GUI/simple/bridge/CxAgentClientBridge.hpp` |
| 648 | `src/slic3r/GUI/simple/bridge/MCPChatPanelBridgeActionExecutor.cpp` |
| 653-654 | `src/slic3r/GUI/simple/bridge/SAgentMqttBridge.cpp`, `.hpp` |
| 657 | `src/slic3r/GUI/simple/bridge/CxAgentClientBridge.cpp` |
| 674-687 | all 14 `src/slic3r/GUI/simple/toolcalls/...` lines |
| 689, 692 | `src/slic3r/GUI/simple/MCPChatPanel.hpp`, `.cpp` |
| 720-731 | all 12 `src/slic3r/GUI/simple/sendWorkflow/...` lines. In Variant B keep 726 and 728 (`EasyPrintSender.hpp/.cpp`) |
| 755, 812 | `src/slic3r/GUI/ModelMall.hpp`, `.cpp` |
| 774 | `src/slic3r/GUI/ModelDetailDialog.hpp` |
| 829 | `src/slic3r/GUI/PublishDialog.hpp` |

Check: `grep -nE "MCPChatPanel|toolcalls/|CxAgent|SAgent|ModelDetailDialog|WebModelLibraryView|ModelLibraryRoutes|ModelMall|PublishDialog|AICloudServiceDialog|sendWorkflow/(AI|CxCloud)|mqtt_client" localization/i18n/list.txt` must return nothing. In Variant A, add `|EasyPrintSender` to the pattern.

The `.po` files keep orphan msgids. That is harmless; `msgmerge` drops them on the next refresh.

---

## 5. Resources

`git rm` these images (each was checked to have no reference left in `src/` or `resources/`):

| File | Former user |
|---|---|
| `resources/images/ai_cloud_service.svg`, `ai_cloud_service_dark.svg` | GLCanvas3D.cpp:9332 (toolbar item, deleted) |
| `resources/images/mall_control_back.svg`, `mall_control_forward.svg` | ModelMall.cpp only |
| `resources/web/image/ai_chat_float_logo.svg` | F2 marks it optional. Its only code user, GLCanvas3DSimple.cpp:1228, is deleted by F2, so delete it here if F2 left it. |

Keep:
- `mall_control_refresh.svg`: used by SendMultiMachinePage.cpp:1239 (F5).
- `topbar_publish*.svg`, `bar_publish.svg`: only in commented code (BBLTopbar.cpp:1812-1814, MainFrame.cpp:2168); F5.
- `tab_ai_creation*.svg`: F2 territory (BBLTopbar `ApplyButtonStyle`/`InsertPage`). After F2 they have no user. Delete them in F2, or here if F2 did not.

**Web bundles:** none to delete. No page under `resources/web/` is loaded by the removed code (the chat, the model library and AI cloud load remote URLs). `resources/web/model` belongs to `Project.cpp` (F5/F6). No web patch is needed: the `buy_filament_cmd` button stays visible but does nothing (open question 2).

---

## 6. Interactions with other phases

| Phase | Interaction |
|---|---|
| F1 item 5 | **Required.** Deletes `SatisfactionSurveyManager`, the only includer of `CxAgentEndpointPolicy.hpp`. |
| F1 item 1 | Stubs analytics. The `track_event` blocks inside the deleted `buy_filament_cmd` handlers go away anyway. |
| F1 (EasyPrintSender.cpp:17, 702-703) | Moot in Variant A (file deleted). |
| F1 note "`get_modellibrary_header` sends `SystemId`" | Resolved here (3.1). |
| F2 | **Required** before F3:<br>- `_render_ai_chat_toggle` (calls `MCPChatWindow::Toggle`) and `GLCanvas3D.cpp:11510`;<br>- `reload_region_sensitive_views` (uses `get_modellibrary_view` and `get_cloud_webaddress`);<br>- `ensure_web_model_view_ready`, MainFrame members `m_webmodellibrary_view`/`m_ai_creation_view`;<br>- `update_model_webviews_user_agent` blocks;<br>- `get_ai_creation_webaddress`;<br>- the `DoOpenModelLibrary` rewrite, `MCPToolCallsObject.cpp:180-181`, `WebModelLibraryView.cpp:454`.<br>If F2 is skipped, those items must be done in F3. F2 says "keep `NotifyAIChatEditionChanged()` until F3": F3 deletes it (3.1). |
| F4 | `CxCloudPrint*` listed in F4 are deleted here. Paho stays until F4 (`CloudDeviceMqttTransport`). `get_cloud_api_url`, `CommunicateWithCXCloud` and `getAgent` publish code stay. |
| F5 | Orphans listed in 0.15. `SendMultiMachinePage` keeps `mall_control_refresh.svg`. |
| F7 | GLCanvas3DSimple / DeviceListSimple easy-mode code is only patched to compile. `check_filament_mapping_simple` stays empty. |

---

## 7. Compile risks

1. **Transitive includes.** `MCPChatPanel.hpp` pulled in `<wx/webview.h>`, `<wx/timer.h>`, `<wx/weakref.h>`, `<unordered_set>`, `<atomic>`, `GUI_Utils.hpp`, `BackgroundSlicingProcess.hpp`, `Http.hpp` and `SlicerBridge.hpp`. It is removed from 10 files.
   - Checked by grep:
     - BBLTopbar, ArrangeJob, OrientJob, DeviceListSimple: no use of these symbols.
     - DeviceMgrRoutes: includes `Http.hpp` itself; `wxWeakRef` appears only in a comment.
     - Plater: includes `<wx/weakref.h>` itself.
     - MainFrame: `Bridge::SlicerBridge` was used only in the deleted dev menu.
   - GLCanvas3D.cpp and Plater.cpp use `std::unordered_set` without a direct include. Add `#include <unordered_set>` to both as a precaution.
   - `AICloudServiceDialog.hpp` and `ModelMall.hpp` leave GUI_App.cpp and Plater.cpp. **[inference]** Low risk. If an "undeclared identifier" error appears, add the specific standard or wx header, not the deleted header.
2. **Namespace of `OpenEshopRecommendedGoods`.** It is defined after `} // GUI } //Slic3r` (GUI_App.cpp:14534-14535) and before `GUI_App::easy_mode()`. Delete only 14536-14624; do not touch the namespace lines.
3. **Plater dock removal.** Check with `git grep "m_left_ai_host|m_easy_ai|kEasyAIDock|easy_mode_ai_dock|update_easy_mode_ai_layout|ensure_embedded_ai_chat_panel|m_embedded_ai_chat_panel"`. It must return nothing.
4. **Publishing removal.** `git grep "m_is_publishing|m_publish_dlg|show_publish_dlg|EVT_PUBLISH|EVT_PUBLISHING|publish_job_finished|get_publish_finished_event|update_publish_dialog_status|show_publish_dialog"` must return nothing in `src/`.
5. **`SlicerBridgeState.cpp`.** The moved function must be in the same anonymous namespace and above line 160, or the compiler will not find it.
6. **`slice_plate`** replacement in GLCanvas3DSimple: `MainFrame::eSlicePlate` is a nested enum of `MainFrame` (MainFrame.hpp:250-253). GLCanvas3DSimple.cpp already uses `MainFrame::eSendToLocalNetPrinter`, so `MainFrame.hpp` is reachable.
7. **CMake.** A file deleted from disk but left in `src/slic3r/CMakeLists.txt` stops configure. Run the section 2 check: `git grep -nE "CxAgentEndpointPolicy|mqtt_client|ModelLibraryRoutes|WebModelLibraryView|ModelDetailDialog|PublishDialog|ModelMall|AICloudServiceDialog|sendWorkflow/(AI|CxCloud)|MCPChatPanel|toolcalls/|CxAgentClientBridge|SAgentMqttBridge|MCPChatPanelBridgeActionExecutor" -- src/slic3r/CMakeLists.txt` must return nothing (Variant A: add `|EasyPrintSender`).
8. **Warnings only.**
   - `imported_from_add_file` is now assigned but only read through the `(void)` cast.
   - `device_switch_applied` stays read.
   - No `/WX` or `-Werror` appears in the root, `src/` or `src/slic3r/` CMakeLists or in `cmake/` (git grep), so warnings do not fail the build.
9. **Unused static/anonymous helpers.** `kEasyModeLayoutSection` in Plater is deleted together with its users. No new "unused function" warnings are expected.

---

## 8. Threads, lifecycle and security

- **Removed background activity:**
  - `CxAgentClientBridge`: Boost.Beast WebSocket client to `cxagent*.crealitycloud.*`, with its own worker threads.
  - `SAgentMqttBridge` / `MQTTClient`: Paho MQTT client with callback threads.
  - `MCPChatPanel`: WebView2 page from cxagent, `wxTimer`s for scene updates, async HTTP.
  - `AISendWorkflowService` / `CxCloudPrintExecutor`: cloud print loop.
  - `AICloudService`: worker thread, previously stopped in `load_language`.
  - The model-library WebView and the eshop HTTP request.

  After F3, none of these exist. Together with F1 and F4, this serves the "no `*.crealitycloud.*` traffic" goal.
- **Shutdown order.**
  - `MainFrame::shutdown` keeps `advisor_on_shutdown()` first (MainFrame.cpp:1243). The deleted `DestroyAIChatPanelsForGUIRecreate()` ran after it, so the Print Advisor stop order does not change.
  - `recreate_GUI` no longer has to tear down chat windows first: there is no floating frame left whose parent could be stale.
- **GUI thread.**
  - All the edited call sites (`Notify*` removals, toolbar, Plater layout) were on the GUI thread. No new cross-thread access is added.
  - `SlicerBridge::Execute` stays GUI-thread only. The Print Advisor calls it through `CallAfter`, as today.
- **Security.**
  - Removed: the BBLTopbar developer toggle. 5 clicks on Prepare within 3 s switched `cxagent_api_base` to `cxagent-dev`.
  - Removed: the `CXAGENT_API_BASE` environment override in CMake.
  - Removed: the chat's ability to drive slicer actions from a remote LLM page (`ExecuteBridgeAction` and the `send_to_printer` delegate).
  - The Print Advisor's own safety rules (`AdvisorConfirmDialog`, writable keys, limits) are untouched.
- **SlicerBridge singleton.**
  - No chat code registers the send-to-printer delegate any more, so `SEND_TO_PRINTER` returns `AI_SEND_WORKFLOW_UNAVAILABLE`.
  - This is consistent with "the Print Advisor never starts a print".

---

## 9. Verification (one build)

1. Build with `build_exe_only.bat`. It reconfigures (CMakeLists changed) and rebuilds most of the GUI.
2. **Grep checklist.**
   - Run `git grep -nE "<pattern>" -- src tests` for each line below.
   - Each must return nothing, except the residual hits listed further down. Hits in docs, `.claude`, `.po` files or in files that later phases delete are also acceptable.

   ```
   MCPChatPanel|MCPChatWindow|GetEmbeddedAIChatPanel|GetActiveAIChatPanel|RegisterEmbeddedAIChatPanel|UnregisterEmbeddedAIChatPanel
   NotifyAIChat|DestroyAIChatPanelsForGUIRecreate|OpenActiveAISendWorkflowCardFromScene|OpenActiveAISliceWorkflowFromScene
   ShowProAISliceAssistantWithEmbeddedSession|WorkflowToolbarState|GetAIWorkflowToolbarState|SetAIWorkflowToolbarState
   ToolCalls::|MCPToolCalls|MCPChatPanelSAgentNative|CxAgentClientBridge|SAgentMqttBridge|MQTTClient|mqtt_client
   CxAgentEndpoint|resolve_cxagent_endpoint|CXAGENT_|cxagent_|CxAgent
   ModelDetailDialog|WebModelLibraryView|get_modellibrary_view|get_modellibrary_header|ModelLibraryRoutes|get_cloud_webaddress
   ModelMall|m_mall_publish_dialog|remove_mall_system_dialog|PublishDialog|m_publish_dlg|m_is_publishing|show_publish_dialog|EVT_PUBLISH
   AICloudService|ai_cloud_service|EVT_GLVIEWTOOLBAR_AI_CLOUD_SERVICE|select_ai_cloud_service
   OpenEshopRecommendedGoods|buy_filament_cmd
   AISendWorkflowService|AIProcessPresetIntentResolver|AIProcessSwitchService|CxCloudPrintClient|CxCloudPrintExecutor
   DoOpenModelLibrary|DoRecommendModel|DoSmartModelSearch|DoImportModelFromSearch|OPEN_MODEL_LIBRARY|RECOMMEND_MODEL|SMART_MODEL_SEARCH|IMPORT_MODEL_FROM_SEARCH
   ClearPendingModelSearchCache|CachedModelSearchResult|ExportConfigSchemaToFile|ENABLE_AI_CONFIG_SCHEMA_EXPORT
   m_left_ai_host|m_easy_ai|easy_ai_dock|kEasyAIDock|ensure_embedded_ai_chat_panel|update_easy_mode_ai_layout|m_embedded_ai_chat_panel
   SetDevMode|IsDevMode|m_dev_mode
   ```

   Variant A, also:
   ```
   EasyPrintSender|sendWorkflow/
   ```

   Expected residual hits (comments only):
   - `simple/filamentMapping/ImGuiFilamentPanel.{hpp,cpp}`, naming `AISendWorkflowService`;
   - `PrinterMgrView.cpp:551-552` (see 3.19);
   - `SlicerBridgeActionsConfig.cpp:254` and `SlicerBridge.cpp:269`, naming SAgent/CxAgent. The `CxAgent` pattern hits them; this is harmless. Edit the comments if you want a clean grep.
   - `tests/gui/linux_crash_regressions/README.md:45`.

   Build files: `git grep -n CXAGENT -- CMakeLists.txt cmake src` must return nothing.

   Also check:
   - `ls src/slic3r/GUI/simple/toolcalls src/slic3r/GUI/simple/MCPChatPanel.hpp` must answer "No such file".
   - The `list.txt` grep of section 4 must return nothing.
3. **Print Advisor (key check).**
   - With the app open and Print Advisor enabled, run `npm run typecheck` and `npm run smoke` in `tools/print-advisor-mcp`.
   - Then check by hand:
     - `get_print_context`: presets and edited config, through GET_PRESETS / GET_EDITED_CONFIG;
     - `get_parameter_schema` (BuildConfigSchemaArray);
     - a slice with measurement (START_SLICE). Its warnings must still omit the `bed_temperature_too_high_than_filament` / `1000C001` notice;
     - the overhang analysis and the preflight (AnalyzeOverhang);
     - `preview_profile_changes`.
4. **G-code** of the 4 F0 projects identical to F0 (no engine change). Then run `check-gcode.mjs`.
5. Run `node tools/web-patches/apply-web-patches.mjs --check`. No web file changed.
6. **Manual start (about 3 min):**
   - no AI cloud icon in the 3D toolbar (it was after "Assembly View");
   - Prepare, Preview and Device work;
   - 5 quick clicks on Prepare do nothing special;
   - the language switch (`recreate_GUI`) and the dark mode toggle work;
   - Arrange and Auto-orient complete normally;
   - selecting another plate works;
   - the Device page shows the K1C with telemetry and camera, and the "buy filament" button is inert;
   - the send dialog opens and its filament mapping works;
   - Preferences opens;
   - the app closes cleanly (no crash in `MainFrame::shutdown`).
7. **Network** (with F1 and F4 applied): monitor for 5 minutes. There must be no connection to `cxagent*.crealitycloud.*`, `ai-*.crealitycloud.*`, `cp.crealitycloud.*` or `pre-2.crealitycloud.*` from these features.

---

## 10. Open questions

1. **EasyPrintSender.** Variant A (delete it: recommended, verified unused) or Variant B (keep it, stripped)?
2. **"Buy filament" button** in the Device page and in the send dialog: leave it inert, or hide it with a web patch (`tools/web-patches/patches.mjs`)? It now sends `buy_filament_cmd` to no handler.
3. **Home page** (only with F2 Variant B):
   - Home's model-download commands now go to `GUI_App::handle_web_request` instead of `ModelLibraryRoutes`, and `3mf_download_start` no longer switches to Prepare.
   - Accept this, or move that one tab switch into `handle_web_request`?
   - With F2 Variant A (no Home) the question does not apply.
4. **SlicerBridge send-to-printer delegate.** Keep the unused delegate API (current spec, smallest change), or delete now `SetSendToPrinterDelegate`, `ClearSendToPrinterDelegate` and the delegate branch of `DoSendToPrinter`?
5. **Docs that describe the removed code.** Update them now or in F8? I did not touch them.
   - `doc/MCP/handoff.md:71-72`
   - `.claude/agents/print-advisor-architect.md:16-18`
   - `.claude/skills/print-advisor-mcp/SKILL.md:12`
   - `doc/AI_Chat_Assistant_Architecture.html` (Creality doc, F6)

---

## 11. Suggested commit order (one build at the end)

1. **AI chat:**
   - delete MCPChatPanel, toolcalls, the CxAgent/SAgent bridges, the executor, `mqtt_client` and the bridge notes;
   - clean the call sites: GUI_App, MainFrame, the Plater dock, GLCanvas3D, Jobs, PartPlate, DeviceMgrRoutes, DeviceListSimple, GLCanvas3DSimple, the BBLTopbar dev toggle;
   - move `ShouldSuppressSliceWarningForAI`;
   - remove CXAGENT from CMake and buildinfo;
   - remove the dev menu and `ExportConfigSchemaToFile`.
2. **sendWorkflow cloud** (Variant A: the whole folder).
3. **Online models:** WebModelLibraryView, ModelDetailDialog, ModelLibraryRoutes, the SlicerBridge model-library actions, `get_cloud_webaddress`, `get_modellibrary_header`.
4. **ModelMall, PublishDialog** and the Plater publishing state.
5. **AICloudServiceDialog**, with its toolbar item, event and Plater handler.
6. **Eshop** (`OpenEshopRecommendedGoods`, `buy_filament_cmd`) and **CxAgentEndpointPolicy**.
7. **`list.txt` and images.**
