## F5 "Codice Bambu": implementation spec

Read-only analysis of branch `advisor` at HEAD `14528e0e`. Nothing was edited, built or run.

**Conventions**

- **Line numbers** are from today's files, before F1-F4. F1-F4 shift many of them, especially in `GUI_App.cpp`, `Plater.cpp` and `MainFrame.cpp`. Every edit therefore also names its function or quotes its text: use that as the anchor.
- **Labels.** Statements marked **[VERIFIED]** were checked against the code with grep or by reading it. **[INFERENCE]** marks conclusions that were not compiled or run.
- **Line endings.** All touched files are CRLF (checked: `GUI_App.*`, `MainFrame.cpp`, `Plater.cpp`, `PresetComboBoxes.*`, `ReleaseNote.*`, `Preferences.cpp`, `BBLTopbar.cpp`, `src/slic3r/CMakeLists.txt`, `localization/i18n/list.txt`). New files must be CRLF too.
- **Rebuilds.** Every stage changes `src/slic3r/CMakeLists.txt` (except A), so `build_exe_only.bat` reconfigures. Stage A touches `libslic3r/PresetBundle.cpp`: one libslic3r file, no `PrintConfig.hpp`. Stages B-D touch `GUI_App.hpp` or `MainFrame.hpp`, so nearly all of `libslic3r_gui` recompiles.
- **Size.** About 73,000 lines in 137 files:

  | Stage | Files | Lines |
  |---|---|---|
  | B | 96 | 43,159 |
  | C | 22 | 18,483 |
  | D | 19 | 11,408 |

  Stage D also removes about 1,000 lines from `GUI_App.cpp`.

---

## 0. Verified facts and corrections to the brief

### 0.1 Facts confirmed [VERIFIED]

- **`GUI_App::on_init_network` always returns false.**
  - It returns `false` at GUI_App.cpp:5462, after the `installed_networking` check.
  - It is the only place that creates `m_agent`, `m_device_manager`, `m_user_manager` and `m_task_manager` (GUI_App.cpp:5506-5560; grep for `new ...Manager` / `new NetworkAgent`).
  - So `getAgent()` and `getDeviceManager()` are always `nullptr` at runtime.
- **Bambu pages are never built.** `MonitorPanel`, `MultiMachinePage`, `ProjectPanel` and `CalibrationPanel` appear only in comments (MainFrame.cpp:1552-1555, 1585-1598, 1622-1650).
- **The LAN path is independent.** `git grep` of `print_manage/**` and `simple/**` (which includes `simple/printAdvisor` and `simple/bridge`) for DeviceManager, NetworkAgent, MachineObject, SelectMachine, BindDialog, AMS*, CalibUtils, PrintJob/SendJob, HMS, UserManager, TaskManager, getAgent and getDeviceManager finds only `print_manage/Upload3mfToCloud.*` and `print_manage/UploadGcodeToCloud.*`, which F4 deletes.
- **`use_bbl_network()` is the only switch.**
  - Defined at PresetBundle.cpp:477-482 as `is_bbl_vendor() && !bbl_use_printhost`.
  - Callers: MainFrame.cpp:2377; Plater.cpp:2543, 14402, 14704; `use_bbl_device_tab()` (PresetBundle.cpp:489).
- **`NetworkAgent::get_version()` already returns `"00.00.00.00"`** when the plug-in is not loaded (NetworkAgent.cpp:494-514). Stage A therefore changes no behaviour; it only drops a log warning.
- **Two `SendToPrinter` classes, different names.** `GUI/SendToPrinter.hpp` defines `SendToPrinterDialog` (Bambu). `GUI/print_manage/App/SendToPrinter.hpp` defines `CxSentToPrinterDialog` (kept).
  - The kept header is included by Plater.cpp:5, PrintAdvisorInternal.cpp:7, EasyPrintSender.cpp:12 and SatisfactionSurveyIntegration.cpp:6 (deleted by F1).
  - Only Plater.cpp:148 includes the Bambu header.
- **The Bambu `SendToPrinterDialog` cannot be reached once `use_bbl_network()` is false.**
  - Its only path is `MainFrame::m_print_select == eSendToPrinter/eSendToPrinterAll`.
  - Those values are set only by the "Send"/"Send all" buttons, which are appended only when `use_bbl_network()` is true (MainFrame.cpp:2372-2398).
- **`PrintPrepareData` is live and must survive.**
  - `Plater::priv::m_print_job_data` (Plater.cpp:6898) is written and read by `Plater::send_gcode` (23186-23216) and `export_config_3mf` (23218-23238).
  - It is also read by `send_gcode_legacy` (23100) and `send_gcode_to_fluidd` (23180), which is the PrintHost/Fluidd path.
  - Its only other users are PrintJob/SendJob/CalibUtils, through `Plater::get_print_job_data` (15314).
  - `PlateListData` is used only by CalibUtils.cpp:1234 (deleted), so it is not moved.
- **`ThumbnailPanel` and `PrintDialogStatus` need no move.**
  - Both are defined in SelectMachine.hpp:300 and :569.
  - Outside the deleted files, only `print_manage/Upload3mfToCloud.*` and `UploadGcodeToCloud.*` use them, and F4 deletes both.
- **`CalibrateFilamentComboBox` is self-contained.**
  - It is defined in PresetComboBoxes.hpp:228-261 and PresetComboBoxes.cpp:1697-1874, and is used only by the Calibration wizard.
  - It needs `CalibrationWizardCaliPage.hpp` (PresetComboBoxes.cpp:48) for `FilamentComboBox` and `EVT_CALI_TRAY_CHANGED`.
- **Widgets checked against kept code.** FanControl, SideTools, TempInput, ImageSwitchButton and AxisCtrlButton have no users in kept code. The only kept reference is `Widgets/ProgressBar.cpp:9`, which defines its own internal-linkage `wxCUSTOMEVT_SET_TEMP_FINISH` and never uses it. `SideButton`/`SideMenuPopup` (kept, used by MainFrame) are different files.
- **`filament_ams_list` and `sync_ams_list` are kept as asked**, but they lose all writers. See §0.2 item 6.

### 0.2 Corrections to the brief [VERIFIED unless marked]

1. **Stage membership has to change for each stage to compile alone:**
   - `AmsMappingPopup` moves from B to C: `SelectMachine.hpp:27` includes it and uses `AmsMapingPopup`, `AmsMapingTipPopup`, `AmsTutorialPopup` and `MaterialItem`.
   - `Widgets/AMSControl` moves from B to C: `AmsMappingPopup.cpp:25` includes it, and AMSControl.hpp:10 includes AmsMappingPopup.hpp.
   - `Utils/CalibUtils` moves from B to C: `Widgets/AMSControl.cpp:11` includes it and calls `CalibUtils::`, and CalibUtils.hpp:4 includes `Jobs/PrintJob.hpp`.
   - `CameraPopup` stays in B only together with an edit to `DeviceManager.hpp:16`. DeviceManager.hpp includes CameraPopup.hpp but uses nothing from it. CameraPopup.hpp in turn pulls `MonitorBasePanel.h`, `MediaPlayCtrl.h`, `wxMediaCtrl2.h` and `Widgets/AxisCtrlButton.hpp` (all stage B).
   - `InputIpAddressDialog` (ReleaseNote) is C as planned. Its GUI_App users (`show_ip_address_enter_dialog*`, `show_modal_ip_address_enter_dialog`) and the `PingCodeBindDialog` users (`popup_ping_bind_dialog`, `remove_ping_bind_dialog`) must also go in C, not D: their classes disappear in C.
   - GUI_App.cpp:3281-3283 (inside the dead `init_networking_callbacks`) calls `Plater::get_select_machine_dialog()`, which C deletes, so those 3 lines go in C.
   - Removing `TaskManager` in B requires the 11-line edit to `on_init_network` (GUI_App.cpp:5518-5528) and removing `m_task_manager`/`getTaskManager()` from GUI_App.hpp in B.
2. **Header fan-out is the main compile risk.** `GUI_App.hpp:26` includes `BindDialog.hpp`, which reaches about 290 TUs. Through it, every TU also gets:
   - `json_diff.hpp:11-12`: `using json = nlohmann::json;` **and `using namespace std;` at global scope**. These are the only global `using namespace std;` in any header reachable from `GUI_App.hpp`; `PhysicalPrinter.hpp:5` has another but is not on that path.
   - `DeviceManager.hpp:52`: `using namespace nlohmann;` (`AppConfig.hpp:14` also has one).
   - `PrintJob.hpp:9`, `SendJob.hpp:9`, `BindJob.hpp:8`, `UpgradeNetworkJob.hpp:10`: `namespace fs = boost::filesystem;`. `libslic3r/ProjectTask.hpp:13` still provides it.
   - `NetworkAgent.hpp:7`: `using namespace BBL;`. No kept code uses BBL names unqualified, so this one is not needed.
   - About 45 wx, boost and widget headers. The list was computed from the include graph; see §1.1.

   The fix is one transitional header, `LegacyTransitiveIncludes.hpp` (§1.1), introduced in B and included where the Bambu headers were.
3. **`BBLTopbar.hpp:6` (`#include "SelectMachine.hpp"`) carries `Plater.hpp`, `ReleaseNote.hpp`, `BBLStatusBarSend.hpp` and more into `MainFrame.hpp`**, which is widely included. In C it must be replaced by SelectMachine.hpp's own non-Bambu includes (§4.3), not just deleted.
4. **`resources/printers` is not Bambu-only.**
   - `libslic3r/DataDirectoryMigration.cpp:483` does `copy_tree(o.resources / "printers", ...)`.
   - `copy_tree` (:212-217) throws `Missing bundled resources` if the folder is missing.
   - Its only other reader is `DeviceManager.hpp:1028` (Bambu).
   - So deleting it needs a guard in DataDirectoryMigration.cpp. It is optional and saves 39 KB (§5.12).
5. **Latent null dereference.**
   - `Plater.cpp:7576` calls `wxGetApp().getDeviceManager()->reload_printer_settings()` on `EVT_PRINTER_CONFIG_UPDATE_AVAILABLE_CLICKED`, and `getDeviceManager()` is always `nullptr`.
   - The notification is pushed only by `PresetUpdater.cpp:1072`, inside a range F1 deletes. After F1 it is unreachable; before F1 it crashes if clicked.
   - Stage A guards it; stage D deletes it.
6. **`filament_ams_list` has no writer outside Bambu code.**
   - Writers: `Sidebar::load_ams_list` (Plater.cpp:3714, fed by `MachineObject`) and `CalibrationWizardPresetPage` (deleted).
   - Readers:
     - PresetComboBoxes.cpp:274 reads it to recolour a filament from a selected AMS slot.
     - PresetComboBoxes.cpp:426 only for `is_bbl_vendor()` presets.
     - `PresetBundle::sync_ams_list` (libslic3r).
   - `Sidebar::sync_ams_list` (Plater.cpp:3727) has no caller anywhere.
   - Nothing in `print_manage/` (the Creality CFS path) touches either.
   - Both are kept as the brief asks; after F5 the list simply stays empty. **[INFERENCE]** The claim "Creality CFS uses it" is not supported by the code; confirm with the user (§9).
7. **`HttpServer::bbl_auth_handle_request` (HttpServer.cpp:1008-1065) has no caller.** It would dereference a null agent, but it is dead. F4 deletes it; D-H covers the case where it is still there.
8. **Two extra dead `GUI_App` pieces are not in the brief:**
   - `DeviceManager::load_filaments_blacklist_config()` runs at every start (GUI_App.cpp:2505). It reads `resources/printers/filaments_blacklist.json`, which only Bambu code uses.
   - `remove_old_networking_plugins()` runs on version change (GUI_App.cpp:4658-4674). It deletes `<data_dir>/plugins`.

   Both go in D.

### 0.3 Interactions with F1-F4 (assumed applied before F5)

**F1**
- It deletes GUI_App.cpp 9853-10589 and 10591-10924, which contain `get_http_url` callers (9876, 10792), `handle_http_error` (9915) and `update_http_extra_header` (10790).
- It keeps the `preset_updater->sync(http_url, language, network_ver, ...)` call at 2441-2446, but the new `sync` ignores all three strings.
- It deletes `PresetUpdater.cpp` 380-1075, which hold the BambuSource/plugin file lists and the `BBLPrinterConfigUpdateAvailable` push.
- It removes `WebUpdatePlugin.*`.

**F2**
- It deletes `tpMonitor`, `MainFrame::is_printer_view`, the BBL/printer_view block in `Plater::priv::on_tab_selection_changing` (Plater.cpp:14465-14481, the only poster of `EVT_INSTALL_PLUGIN_HINT`), and the comment-only references at MainFrame.cpp 1561/1633/1642/1650/1678/1683/4802/4810 and Plater.cpp 23245/23271/23287.
- F5 must not re-add them. If F2 left the commented page blocks in `init_tabpanel`/`show_device`, F5 deletes them (B-3).

**F3** (`doc/specs/F3-chat-ia-modelli-spec.md`, checked)
- It deletes `ModelMall.*` and `PublishDialog.*`; PublishDialog.cpp:14 includes `Widgets/AMSControl.hpp`.
- Its item 0.15 leaves these orphans to F5 on purpose: `GUI_App::open_publish_page_dialog`, `open_mall_page_dialog`, `BBLTopbar::OnPublishClicked` and `Plater::publish_project`.
- F5 handles the first three in C-6 and §5.2 item 2.

**F4** (`doc/specs/F4-account-cloud-spec.md`, checked)

Files it deletes:
- `Upload3mfToCloud.*`, `UploadGcodeToCloud.*` (they used `ThumbnailPanel`, `PrintDialogStatus`, `MachineObject` and `getAgent`);
- `LoginDialog.*`, `WebUserLoginDialog.*`, `LoginTip.*`, `UploadFile.*` (the last two include `json_diff.hpp`);
- `SyncUserPresets`.

GUI_App code it deletes:
- it replaces GUI_App.cpp 6586-6695 with three stubs (`is_user_login()` and `is_login()` return `false`; `get_user()` returns the empty `m_user`);
- `request_user_logout`, `request_user_unbind`, `get_login_info`, `check_login`;
- `handle_script_message`, `check_track_enable`, `on_user_login`, `on_user_login_handle`;
- `start_sync_user_preset`/`stop_sync_user_preset` (including the dead code that used `ProgressFn`/`WasCancelledFn`);
- `bbl_auth_handle_request`.

Other edits:
- `MainFrame::show_sync_dialog` (with the unused agent local at 5405);
- `BindDialog.cpp:1102`;
- `CreatePresetsDialog.cpp:4693` (already `false`);
- the two `request_user_logout()` calls in Preferences (348, 1775).

Left to F5 on purpose (F4 §4 note, §11):
- `#include "slic3r/GUI/UserManager.hpp"` (GUI_App.cpp:15);
- the `getAgent()` uses in `open_mall_page_dialog`/`open_publish_page_dialog` (13311, 13352);
- `Plater.cpp:22177` (design info with `getAgent()->get_user_id()`);
- the `NetworkAgent* agent` locals in Preferences.

The §5.2 items marked **(F4)** are therefore expected to be gone already. Apply them only if `git grep` still finds the function.

**Pre-flight check before stage B.** This command must print nothing:

```
git grep -n -E "Upload3mfToCloud|UploadGcodeToCloud|WebUserLoginDialog|LoginDialog|PublishDialog|ModelMall|LoginTip|UploadFile|UpdateParams|SatisfactionSurvey" -- src/slic3r/CMakeLists.txt
```

If anything remains, apply the "(if F3/F4 left it)" items below.

---

## 1. New files (both created in stage B)

### 1.1 `src/slic3r/GUI/LegacyTransitiveIncludes.hpp` (new, CRLF)

- **Purpose.** It keeps every non-Bambu include and global directive that today reaches TUs through the Bambu header chain.
- **How the list was built.** It is the include-graph difference between "GUI_App.hpp today" and "GUI_App.hpp without BindDialog/DeviceManager/UpgradeNetworkJob", plus what CameraPopup.hpp gave DeviceManager.hpp.
  - `wx/mediactrl.h` and `wx/uri.h` (media only) are left out.
  - None of the listed headers includes `GUI_App.hpp`, `DeviceManager.hpp`, `Plater.hpp` or `MainFrame.hpp`, so there is no include cycle [VERIFIED by closure walk].

```cpp
#ifndef slic3r_GUI_LegacyTransitiveIncludes_hpp_
#define slic3r_GUI_LegacyTransitiveIncludes_hpp_

// Transitional header (F5, Bambu code removal).
// Until F5 the Bambu bind, device, camera, print-job and network headers reached almost
// every translation unit through GUI_App.hpp. They carried the includes and the global
// using-directives below. This header keeps them so the removal does not break unrelated
// files. Prune it later, one line at a time, with a build after each change.
// The comments deliberately avoid the removed class names, so the F5 grep checklist stays clean.

#include <chrono>
#include <deque>
#include <exception>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <boost/filesystem.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/optional.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition_variable.hpp>

#include <curl/curl.h>
#include "nlohmann/json.hpp"

#include <wx/wx.h>
#include <wx/artprov.h>
#include <wx/bitmap.h>
#include <wx/bmpbuttn.h>
#include <wx/bmpcbox.h>
#include <wx/button.h>
#include <wx/checklst.h>
#include <wx/colour.h>
#include <wx/combo.h>
#include <wx/dataview.h>
#include <wx/dialog.h>
#include <wx/event.h>
#include <wx/font.h>
#include <wx/gauge.h>
#include <wx/gbsizer.h>
#include <wx/gdicmn.h>
#include <wx/hyperlink.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/menu.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include <wx/progdlg.h>
#include <wx/scrolwin.h>
#include <wx/settings.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/splitter.h>
#include <wx/statbmp.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/tglbtn.h>
#include <wx/timer.h>
#include <wx/version.h>
#include <wx/vlbox.h>
#include <wx/weakref.h>
#include <wx/webrequest.h>
#include <wx/window.h>
#include <wx/xrc/xmlres.h>

#include "libslic3r/Color.hpp"
#include "libslic3r/Preset.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "libslic3r/Semver.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/calib.hpp"

#include "slic3r/GUI/BBLStatusBar.hpp"
#include "slic3r/GUI/BitmapCache.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_Utils.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Jobs/Job.hpp"
#include "slic3r/GUI/Jobs/ProgressIndicator.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"
#include "slic3r/GUI/Widgets/Button.hpp"
#include "slic3r/GUI/Widgets/Label.hpp"
#include "slic3r/GUI/Widgets/PopupWindow.hpp"
#include "slic3r/GUI/Widgets/ProgressBar.hpp"
#include "slic3r/GUI/Widgets/ProgressDialog.hpp"
#include "slic3r/GUI/Widgets/RadioBox.hpp"
#include "slic3r/GUI/Widgets/RoundedRectangle.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"
#include "slic3r/GUI/Widgets/StateHandler.hpp"
#include "slic3r/GUI/Widgets/StaticBox.hpp"
#include "slic3r/GUI/Widgets/StaticLine.hpp"
#include "slic3r/GUI/Widgets/StepCtrl.hpp"
#include "slic3r/GUI/Widgets/SwitchButton.hpp"
#include "slic3r/GUI/Widgets/TextInput.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

// Global directives that used to come from the removed Bambu JSON-diff and device headers.
using json = nlohmann::json;
using namespace std;
using namespace nlohmann;

#endif // slic3r_GUI_LegacyTransitiveIncludes_hpp_
```

**CMake.** Add `GUI/LegacyTransitiveIncludes.hpp` to `SLIC3R_GUI_SOURCES`, for example after `GUI/GUI_App.hpp`. It is a header only, so this is for IDE visibility. No `list.txt` entry is needed (no strings).

### 1.2 `src/slic3r/GUI/Widgets/AmsColors.hpp` (new, CRLF)

- **Why.** The `AMS_CONTROL_*` colour macros are used by kept files:
  - Preferences.cpp:1703-1706;
  - ReleaseNote.cpp:166, 733, 1587, 1684, 1869, 2029, 2042;
  - SingleChoiceDialog.cpp:31 and UpdateDialogs.cpp:147, both through `Auxiliary.hpp`.
- **Content.** Copy AMSControl.hpp:16-26 exactly, so the duplicate definitions stay token-identical until AMSControl.hpp is deleted in C (no C4005 warning).

```cpp
#ifndef slic3r_GUI_Widgets_AmsColors_hpp_
#define slic3r_GUI_Widgets_AmsColors_hpp_

#include <wx/colour.h>

// Colour constants that outlived the removed Bambu AMS widgets.
#define AMS_CONTROL_BRAND_COLOUR wxColour(0, 150, 136)
#define AMS_CONTROL_GRAY700 wxColour(107, 107, 107)
#define AMS_CONTROL_GRAY800 wxColour(50, 58, 61)
#define AMS_CONTROL_GRAY500 wxColour(172, 172, 172)
#define AMS_CONTROL_DISABLE_COLOUR wxColour(206, 206, 206)
#define AMS_CONTROL_DISABLE_TEXT_COLOUR wxColour(144, 144, 144)
#define AMS_CONTROL_WHITE_COLOUR wxColour(255, 255, 255)
#define AMS_CONTROL_BLACK_COLOUR wxColour(0, 0, 0)
#define AMS_CONTROL_DEF_BLOCK_BK_COLOUR wxColour(238, 238, 238)
#define AMS_CONTROL_DEF_LIB_BK_COLOUR wxColour(248, 248, 248)
#define AMS_EXTRUDER_DEF_COLOUR wxColour(234, 234, 234)

#endif
```

**CMake.** Add `GUI/Widgets/AmsColors.hpp` to the Widgets block. No `list.txt` entry is needed.

---

## 2. Stage A: switch off the BBL network path (small edit, one build)

### A1. `src/libslic3r/PresetBundle.cpp:477-482`

Replace the body of `use_bbl_network()`:

```cpp
bool PresetBundle::use_bbl_network()
{
    // Fork: the Bambu network plug-in and its dialogs were removed (F5).
    // BBL presets take the print-host path like any third-party printer.
    return false;
}
```

**Kept as is:**
- `use_bbl_device_tab()` (PresetBundle.cpp:484-495). It now returns `print_host_webui.empty()` for BBL vendors. F2 already removed the tab it drove.
- `is_bbl_vendor()`.

### A2. `src/slic3r/GUI/GUI_App.cpp:2443` (inside `post_init`, the `#if CUSTOM_CXCLOUD` CallAfter)

```cpp
            std::string network_ver = "00.00.00.00"; // F5: the Bambu network plug-in is never loaded
```

### A3 (defensive, recommended). `src/slic3r/GUI/Plater.cpp:7574-7577`

```cpp
        this->q->Bind(EVT_PRINTER_CONFIG_UPDATE_AVAILABLE_CLICKED, [](PrinterConfigUpdateAvailableClickedEvent&) {
            wxGetApp().get_preset_updater()->do_printer_config_update();
            if (auto* device_manager = wxGetApp().getDeviceManager()) // always null in this fork; removed in F5-D
                device_manager->reload_printer_settings();
        });
```

### A: behaviour after the change

**Before vs after.** Nothing changes for Creality presets. For a BBL preset:

| Action | Before | After |
|---|---|---|
| Print plate / Print all | Bambu `SelectMachineDialog` | `send_gcode_legacy`, as for any print host |
| Sidebar "connection" button | hidden | shown |
| "Send" / "Send all" | shown | hidden |

### A: verification

- Build, then start the app.
- Slice with the K1C. The Device tab and the send dialog work.
- No `getAgent`/`NetworkAgent` entry appears in the log beyond today's.

**Commit:** `F5-A: use_bbl_network() always false`.

---

## 3. Stage B: leaf UI (monitor, liveview, Bambu calibration, multi-machine, project, AMS panels, widgets)

### 3.1 Delete these files (`git rm`): 96 files, 43,159 lines

**Monitor and status pages**

```
src/slic3r/GUI/Monitor.cpp  Monitor.hpp
src/slic3r/GUI/MonitorBasePanel.cpp  MonitorBasePanel.h
src/slic3r/GUI/MonitorPage.cpp  MonitorPage.hpp
src/slic3r/GUI/StatusPanel.cpp  StatusPanel.hpp
src/slic3r/GUI/HMSPanel.cpp  HMSPanel.hpp
src/slic3r/GUI/UpgradePanel.cpp  UpgradePanel.hpp
src/slic3r/GUI/SliceInfoPanel.cpp  SliceInfoPanel.hpp
src/slic3r/GUI/CameraPopup.cpp  CameraPopup.hpp
src/slic3r/GUI/PrintOptionsDialog.cpp  PrintOptionsDialog.hpp
```

**Liveview and SD-card browser**

```
src/slic3r/GUI/MediaPlayCtrl.cpp  MediaPlayCtrl.h
src/slic3r/GUI/MediaFilePanel.cpp  MediaFilePanel.h
src/slic3r/GUI/ImageGrid.cpp  ImageGrid.h
src/slic3r/GUI/wxMediaCtrl2.cpp  wxMediaCtrl2.h  wxMediaCtrl2.mm
src/slic3r/GUI/Printer/PrinterFileSystem.cpp  PrinterFileSystem.h  BambuTunnel.h  gstbambusrc.c  gstbambusrc.h
```

`src/slic3r/GUI/Printer/` is then empty.

**Bambu calibration**

```
src/slic3r/GUI/CalibrationPanel.cpp  CalibrationPanel.hpp
src/slic3r/GUI/CalibrationWizard.cpp  CalibrationWizard.hpp
src/slic3r/GUI/CalibrationWizardPage.cpp  CalibrationWizardPage.hpp
src/slic3r/GUI/CalibrationWizardStartPage.cpp  CalibrationWizardStartPage.hpp
src/slic3r/GUI/CalibrationWizardPresetPage.cpp  CalibrationWizardPresetPage.hpp
src/slic3r/GUI/CalibrationWizardCaliPage.cpp  CalibrationWizardCaliPage.hpp
src/slic3r/GUI/CalibrationWizardSavePage.cpp  CalibrationWizardSavePage.hpp
src/slic3r/GUI/CaliHistoryDialog.cpp  CaliHistoryDialog.hpp
src/slic3r/GUI/Calibration.cpp  Calibration.hpp
src/slic3r/GUI/ExtrusionCalibration.cpp  ExtrusionCalibration.hpp
```

These are NOT Orca's `calib_dlg.*` or `Plater::calib_*`: those stay, and the Print Advisor uses them.

**Multi-machine**

```
src/slic3r/GUI/MultiMachine.cpp  MultiMachine.hpp
src/slic3r/GUI/MultiMachinePage.cpp  MultiMachinePage.hpp
src/slic3r/GUI/MultiMachineManagerPage.cpp  MultiMachineManagerPage.hpp
src/slic3r/GUI/MultiPrintJob.cpp  MultiPrintJob.hpp
src/slic3r/GUI/MultiSendMachineModel.cpp  MultiSendMachineModel.hpp
src/slic3r/GUI/MultiTaskManagerPage.cpp  MultiTaskManagerPage.hpp
src/slic3r/GUI/MultiTaskModel.cpp  MultiTaskModel.hpp
src/slic3r/GUI/SendMultiMachinePage.cpp  SendMultiMachinePage.hpp
src/slic3r/GUI/TaskManager.cpp  TaskManager.hpp
```

**Project and auxiliary files**

```
src/slic3r/GUI/Project.cpp  Project.hpp
src/slic3r/GUI/Auxiliary.cpp  Auxiliary.hpp
src/slic3r/GUI/AuxiliaryDialog.cpp  AuxiliaryDialog.hpp
src/slic3r/GUI/GUI_AuxiliaryList.cpp  GUI_AuxiliaryList.hpp
src/slic3r/GUI/AuxiliaryDataViewModel.cpp  AuxiliaryDataViewModel.hpp
```

Why the whole auxiliary chain goes:
- `AuxiliaryDialog.hpp` is included by no other file.
- `GUI_AuxiliaryList.hpp` is included only by `AuxiliaryDialog.cpp`.
- `AuxiliaryDataViewModel.hpp` is included only by `GUI_AuxiliaryList.hpp`.

`CrProject.*` is Creality's own and stays.

**AMS panels** (AmsMappingPopup and Widgets/AMSControl move to stage C)

```
src/slic3r/GUI/AMSMaterialsSetting.cpp  AMSMaterialsSetting.hpp
src/slic3r/GUI/AMSSetting.cpp  AMSSetting.hpp
src/slic3r/GUI/AmsWidgets.cpp  AmsWidgets.hpp
```

**Widgets**

```
src/slic3r/GUI/Widgets/FanControl.cpp  FanControl.hpp
src/slic3r/GUI/Widgets/SideTools.cpp  SideTools.hpp
src/slic3r/GUI/Widgets/TempInput.cpp  TempInput.hpp
src/slic3r/GUI/Widgets/ImageSwitchButton.cpp  ImageSwitchButton.hpp
src/slic3r/GUI/Widgets/AxisCtrlButton.cpp  AxisCtrlButton.hpp
```

**ModelMall (if F3 left it).** Delete `ModelMall.*` and follow F3's own spec.

### 3.2 `src/slic3r/CMakeLists.txt` (stage B)

**Remove these source lines.** Line numbers are today's; delete them bottom-up or by content.

| Lines | Files |
|---|---|
| 9-10 | PrinterFileSystem |
| 11-12 | AxisCtrlButton |
| 41-42 | ImageSwitchButton |
| 59-60 | TempInput |
| 63-64 | FanControl |
| 73-74 | SideTools |
| 101-104 | AuxiliaryDialog, Auxiliary |
| 107-108 | Project |
| 113-114 | ImageGrid |
| 217-222 | AMSSetting, AMSMaterialsSetting, ExtrusionCalibration |
| 298-299 | GUI_AuxiliaryList |
| 320-329 | AmsWidgets, MediaFilePanel, MediaPlayCtrl, MonitorBasePanel, UpgradePanel |
| 332-337 | HMSPanel, MonitorPage, StatusPanel |
| 340-345 | SliceInfoPanel, CameraPopup, Monitor |
| 585-586 | AuxiliaryDataViewModel |
| 627-646 | CaliHistoryDialog, CalibrationPanel, CalibrationWizard*, Calibration, PrintOptionsDialog |
| 657-674 | MultiMachine* … TaskManager |

**Keep these lines:**

| Lines | Files |
|---|---|
| 330-331 | HintNotification (not Bambu) |
| 338-339 | HMS.hpp/.cpp (stage D) |
| 61-62 | Widgets/AMSControl (stage C) |
| 621-622 | AmsMappingPopup (stage C) |
| 744-745 | Utils/CalibUtils (stage C) |

**Platform blocks:**

- **Apple list (936-937):** delete `GUI/wxMediaCtrl2.mm` and `GUI/wxMediaCtrl2.h`.
- **Lines 951-955:** delete the whole `else ()` branch that appends `GUI/wxMediaCtrl2.cpp`/`.h`. Line 950 `)` and line 956 `endif ()` remain, so the block reads:

  ```cmake
      set_source_files_properties(
          GUI/simple/gpu/GpuOrientMetal.mm
          PROPERTIES COMPILE_FLAGS "-fobjc-arc"
      )
  endif ()
  ```

- **Lines 958-962:** delete the `if (UNIX AND NOT APPLE) list(APPEND ... GUI/Printer/gstbambusrc.c) endif ()` block and its trailing blank line.
- **Lines 1088-1092:** delete `# We add GStreamer for bambu:/// support.` and the four GStreamer `pkg_check_modules`, `target_link_libraries` and `target_include_directories` lines. They are Linux only and do not affect the Windows build.
- **Add** `GUI/LegacyTransitiveIncludes.hpp` and `GUI/Widgets/AmsColors.hpp` (§1).

**Content-based deletion** (Git Bash). This keeps CRLF on the remaining lines. Run it from the repository root:

```bash
for p in Printer/PrinterFileSystem Widgets/AxisCtrlButton Widgets/ImageSwitchButton Widgets/TempInput \
         Widgets/FanControl Widgets/SideTools AuxiliaryDialog Auxiliary Project ImageGrid AMSSetting \
         AMSMaterialsSetting ExtrusionCalibration GUI_AuxiliaryList AmsWidgets MediaFilePanel MediaPlayCtrl \
         MonitorBasePanel UpgradePanel HMSPanel MonitorPage StatusPanel SliceInfoPanel CameraPopup Monitor \
         AuxiliaryDataViewModel CaliHistoryDialog CalibrationPanel CalibrationWizard CalibrationWizardPage \
         CalibrationWizardStartPage CalibrationWizardPresetPage CalibrationWizardCaliPage \
         CalibrationWizardSavePage Calibration PrintOptionsDialog MultiMachine MultiMachinePage \
         MultiMachineManagerPage MultiPrintJob MultiSendMachineModel MultiTaskManagerPage MultiTaskModel \
         SendMultiMachinePage TaskManager; do
  sed -i -E "\#^[[:space:]]*GUI/${p}\.(cpp|hpp|h|mm|c)\r?\$#d" src/slic3r/CMakeLists.txt
done
```

Then do the platform-block edits above by hand. `wxMediaCtrl2.*` and `gstbambusrc.c` sit inside those blocks.

### 3.3 `localization/i18n/list.txt` (stage B)

**Delete by content.** Use the same loop with pattern `^src/slic3r/GUI/${p}\.(cpp|hpp|h)\r?$` on `localization/i18n/list.txt`, then delete `src/slic3r/GUI/wxMediaCtrl2.cpp` (line 565).

**Today's line numbers** (for checking):

```
35 38 51 65 75 79 80 93 94 108 111 120 121 133 147 152 169 171 173 259 266 269 275 281 286 290 291 293
295 303 316 319 322 323 401 403 421 438 454 467 472 476 478 482 486 491 496 497 503 512 515 517 524
532 538 548 556 558 560 565 578 579 581 587 589 595 636 643 738 745 750 752 761 764 775 788 790
794 802 803 804 809 811 820 821 827
```

Use the loop, which matches exact paths. Lines 41/46 (AmsMappingPopup), 123/138 (AMSControl) and 885/909 (CalibUtils) belong to stage C.

**Never delete these lines:**
- 210 `src/slic3r/GUI/print_manage/App/SendToPrinter.cpp`
- 213 `src/slic3r/GUI/print_manage/App/SendToPrinter.hpp`

The `^src/slic3r/GUI/<name>` anchor already protects them.

### 3.4 Edits to kept files (stage B)

**B-1. `GUI/DeviceManager.hpp:16`** (a stage-D file, edited now to cut the CameraPopup chain)

```cpp
#include "LegacyTransitiveIncludes.hpp" // F5-B: replaces the camera popup header; nothing from it was used here
```

**B-2. `GUI/MainFrame.hpp`**
- **:27** `#include "MultiMachinePage.hpp"` → `#include "Tabbook.hpp"`. MainFrame.hpp loses only `Tabbook.hpp` and `wx/bookctrl.h` through this include [VERIFIED by closure diff].
- **:41-43** delete the forward declarations `class ProjectPanel;`, `class CalibrationPanel;` and `class MonitorPanel;`.
- **:405-411** delete the commented members `//MonitorPanel* m_monitor`, `//MultiMachinePage* m_multi_machine`, `//ProjectPanel* m_project` and `//CalibrationPanel* m_calibration` (optional cleanup).

**B-3. `GUI/MainFrame.cpp`**
- **Delete** `#include "Project.hpp"` (:34), `#include "CalibrationPanel.hpp"` (:35) and `#include "Monitor.hpp"` (:49).
- **Add**, next to the other GUI includes:

  ```cpp
  #include "PresetComboBoxes.hpp"
  #include "libslic3r/Thread.hpp"
  #include <wx/animate.h>
  #include <wx/dcgraph.h>
  #include <wx/dynarray.h>
  #include <wx/html/htmlwin.h>
  ```

  These are the headers the TU loses through Monitor/Project/CalibrationPanel that `LegacyTransitiveIncludes.hpp` does not provide [VERIFIED by closure diff]. If the compiler shows any of them is unused, removing it is optional.
- **Optional cleanup (only if F2 left them):**
  - in `init_tabpanel`, the commented `//m_monitor…` lines (1552-1555) and the commented MultiMachine/Project/Calibration blocks (1585-1598);
  - in `show_device`, the commented `MonitorPanel`/`MultiMachinePage`/`CalibrationPanel` blocks (1619-1649). Keep the `UpdateDarkUIWin` call.

**B-4. `GUI/Plater.cpp`**
- **Delete** `#include "slic3r/GUI/Monitor.hpp"` (:12), `#include "slic3r/GUI/CalibrationPanel.hpp"` (:13) and `#include "SendMultiMachinePage.hpp"` (:147).
- **Delete** the member `SendMultiMachinePage* m_send_multi_dlg = nullptr;` (:6294). Its only uses are the commented lines 14417-14420; delete those comments too.
- **Add** `#include <wx/animate.h>`, `#include <wx/dynarray.h>` and `#include <wx/html/htmlwin.h>` [VERIFIED lost; `Tabbook.hpp` arrives through MainFrame.hpp].

**B-5. `GUI/GUI_App.hpp`**
- **Delete** `class TaskManager;` (:72), `Slic3r::TaskManager* m_task_manager { nullptr };` (:289) and `Slic3r::TaskManager* getTaskManager() { return m_task_manager; }` (:386).
- `getTaskManager()` has no callers outside deleted files [VERIFIED].

**B-6. `GUI/GUI_App.cpp`**
- **Delete** `#include "slic3r/GUI/TaskManager.hpp"` (:16) and `#include "slic3r/GUI/Project.hpp"` (:130).
- **Add** `#include "PresetComboBoxes.hpp"`, `#include <wx/animate.h>`, `#include <wx/dynarray.h>` and `#include <wx/grid.h>` [VERIFIED lost].
- **In `on_init_network`** (dead code after `return false;`), replace lines 5518-5528 (the `if (this->is_enable_multi_machine()) {…} else {…}` block) with:

  ```cpp
        m_agent->enable_multi_machine(false);
        DeviceManager::EnableMultiMachine = false;
  ```

  This removes the only `new Slic3r::TaskManager`. Keep `is_enable_multi_machine()`: it is a plain AppConfig read, also used by `push_notification` (:10971) and MainFrame.cpp:2161.

**B-7. `GUI/InstanceCheck.cpp:1`**
- Replace `#include "Auxiliary.hpp"` with `#include "Tabbook.hpp"` and `#include "GUI.hpp"`.
- Nothing from Auxiliary.hpp is used [VERIFIED]; the TU only loses transitive includes.

**B-8. `GUI/SingleChoiceDialog.cpp:5` and `GUI/UpdateDialogs.cpp:15`.** Replace `#include "slic3r/GUI/Auxiliary.hpp"` with:

```cpp
#include "slic3r/GUI/Tabbook.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Widgets/AmsColors.hpp" // AMS_CONTROL_BRAND_COLOUR (SingleChoiceDialog.cpp:31, UpdateDialogs.cpp:147)
```

**B-9. `GUI/SelectMachine.cpp:22` and `GUI/SendToPrinter.cpp:28`** (stage-C files, edited so B builds):
- Delete `#include "slic3r/GUI/Auxiliary.hpp"`.
- [INFERENCE] Nothing from it is used: the symbol scan found no Auxiliary, StatusPanel, AmsWidgets, SideTools or UpgradePanel name. If the build says otherwise, add `#include "Tabbook.hpp"`.

**B-10. `GUI/PresetComboBoxes.hpp`**
- Delete lines 228-261: the `// *** CalibrateFilamentComboBox ***` banner and the whole class, up to and including the blank line before `} // namespace GUI`.

**B-11. `GUI/PresetComboBoxes.cpp`**
- **Delete** `#include "slic3r/GUI/CalibrationWizardCaliPage.hpp"` (:48).
- **Delete** lines 1697-1874: all `GUI::CalibrateFilamentComboBox::` member definitions, from the constructor to the end of `OnSelect`.
- The file must then end with:

  ```cpp
  } // namespace GUI

  } // namespace Slic3r
  ```

- [INFERENCE] The TU loses `Tabbook.hpp`, `Thread.hpp`, `wx/animate.h` and `wx/dynarray.h` through the CaliPage include. Add `#include "libslic3r/Thread.hpp"` only if the compiler asks.

**B-12. (if F3 left it) `GUI/PublishDialog.cpp:14`.** It includes `Widgets/AMSControl.hpp`, a stage-C file, so nothing changes in B; see C-12.

**B-13. Optional: `GUI/Widgets/ProgressBar.cpp:9`.** Delete the unused `wxDEFINE_EVENT(wxCUSTOMEVT_SET_TEMP_FINISH, wxCommandEvent);`. Its declaration lived in the deleted `TempInput.hpp`; the line still compiles, because `wxDEFINE_EVENT` is a self-contained definition.

### 3.5 Stage B compile risks

- **Lost transitive includes in unlisted TUs.**
  - Mitigation: `LegacyTransitiveIncludes.hpp` now sits where CameraPopup.hpp was, so every TU keeps what it had.
  - Failures should be limited to the headers named in B-3 to B-11.
  - The first build error tells which header to add. Do not add Bambu headers back.
- **`SelectMachine.cpp`/`SendToPrinter.cpp` (B-9).** These are still compiled in B.
- **`using namespace std;`** still comes through `DeviceManager.hpp` → `json_diff.hpp` (unchanged in B).
- **Linux only.** `Notebook.cpp:262` mentions GStreamer in a comment only.

### 3.6 Stage B verification

1. **Build** with `build_exe_only.bat` (it reconfigures because CMake changed).
2. **Grep** (must print nothing outside `doc/`):

   ```
   git grep -n -E "\b(MonitorPanel|MonitorBasePanel|StatusPanel|HMSPanel|UpgradePanel|MediaPlayCtrl|MediaFilePanel|ImageGrid|PrinterFileSystem|BambuTunnel|gstbambusrc|wxMediaCtrl2|CalibrationPanel|CalibrationWizard\w*|CaliHistoryDialog|ExtrusionCalibration|MultiMachine\w*|MultiPrintJob|MultiSendMachineModel|MultiTask\w+|SendMultiMachinePage|TaskManager|ProjectPanel|AuxiliaryPanel|AuxiliaryDialog|GUI_AuxiliaryList|AuxiliaryDataViewModel|AMSMaterialsSetting|AMSSetting|AmsWidgets|CameraPopup|PrintOptionsDialog|SliceInfoPanel|FanControl|SideTools|TempInput|ImageSwitchButton|AxisCtrlButton|CalibrateFilamentComboBox)\b" -- src localization/i18n/list.txt
   ```

   Expected exceptions:
   - comments in kept files (review them);
   - `Project.hpp` matches, but `CrProject`, `ProjectTask` and `ProjectDirtyStateManager` do not match `\bProjectPanel\b`.
3. **Startup smoke test (about 3 min).** Check:
   - start;
   - load a model and slice;
   - Preview;
   - Device tab with the K1C;
   - the send dialog opens;
   - Preferences;
   - Print Advisor `npm run smoke`;
   - clean exit.
4. **G-code** of the four F0 projects is identical. Stage B does not touch the engine.

**Commit:** `F5-B: remove Bambu monitor, liveview, calibration, multi-machine, project and AMS panels`.

---

## 4. Stage C: Bambu dialogs and jobs

### 4.1 Delete these files (`git rm`): 22 files, 18,483 lines

```
src/slic3r/GUI/SelectMachine.cpp  SelectMachine.hpp          (also defines ThumbnailPanel, PrintDialogStatus)
src/slic3r/GUI/SendToPrinter.cpp  SendToPrinter.hpp          (Bambu SendToPrinterDialog; NOT print_manage/App/SendToPrinter.*)
src/slic3r/GUI/BindDialog.cpp  BindDialog.hpp                (BindMachineDialog, UnBindMachineDialog, PingCodeBindDialog)
src/slic3r/GUI/BBLStatusBarBind.cpp  BBLStatusBarBind.hpp    (only BindDialog.hpp includes it)
src/slic3r/GUI/ConnectPrinter.cpp  ConnectPrinter.hpp
src/slic3r/GUI/AmsMappingPopup.cpp  AmsMappingPopup.hpp      (moved here from B)
src/slic3r/GUI/Widgets/AMSControl.cpp  AMSControl.hpp        (moved here from B)
src/slic3r/GUI/Jobs/BindJob.cpp  BindJob.hpp
src/slic3r/GUI/Jobs/PrintJob.cpp  PrintJob.hpp
src/slic3r/GUI/Jobs/SendJob.cpp  SendJob.hpp
src/slic3r/Utils/CalibUtils.cpp  CalibUtils.hpp              (moved here from B)
```

**Keep** `BBLStatusBar.*` and `BBLStatusBarSend.*`. MsgDialog.hpp:17-18, DownloadProgressDialog (until D), ReleaseNote and others use them.

### 4.2 CMake and list.txt (stage C)

**CMake lines to remove:** 61-62 (AMSControl), 503-504 (ConnectPrinter), 551-556 (PrintJob, SendJob, BindJob), 571-572 (BBLStatusBarBind), 613-614 (BindDialog), 617-622 (SelectMachine, SendToPrinter, AmsMappingPopup) and 744-745 (CalibUtils).

**Loop**, in the same style as §3.2:

```bash
for p in GUI/Widgets/AMSControl GUI/ConnectPrinter GUI/Jobs/PrintJob GUI/Jobs/SendJob GUI/Jobs/BindJob \
         GUI/BBLStatusBarBind GUI/BindDialog GUI/SelectMachine GUI/SendToPrinter GUI/AmsMappingPopup \
         Utils/CalibUtils; do
  sed -i -E "\#^[[:space:]]*${p}\.(cpp|hpp|h)\r?\$#d" src/slic3r/CMakeLists.txt
  sed -i -E "\#^src/slic3r/${p}\.(cpp|hpp|h)\r?\$#d" localization/i18n/list.txt
done
```

**list.txt lines removed** (today's numbers): 41 46 52 89 123 138 263 451 540 563 596 610 613 614 616 620 645 782 817 819 885 909.

The `^src/slic3r/GUI/SendToPrinter` anchor does not match `src/slic3r/GUI/print_manage/App/SendToPrinter.*` (210, 213) [VERIFIED].

### 4.3 Edits to kept files (stage C)

**C-1. `GUI/GUI_App.hpp`**
- **:26** `#include "slic3r/GUI/BindDialog.hpp"` → two lines:

  ```cpp
  #include "slic3r/GUI/LegacyTransitiveIncludes.hpp" // F5: generic includes that came with the Bambu headers
  #include "slic3r/GUI/DeviceManager.hpp"            // F5: removed in stage D (temporary)
  ```

- **Delete:**
  - `class PingCodeBindDialog;` (:96);
  - `PingCodeBindDialog* m_ping_code_binding_dialog{ nullptr };` (:692);
  - `void popup_ping_bind_dialog();` and `void remove_ping_bind_dialog();` (:711-712);
  - the three `show_ip_address_enter_dialog*` / `show_modal_ip_address_enter_dialog` declarations (:628-630).

**C-2. `GUI/GUI_App.cpp`**
- **`init_networking_callbacks`** (dead): delete lines 3281-3283, i.e. `if (wxGetApp().plater()->get_select_machine_dialog()) { wxPostEvent(...); }`. The local `event` is still built but no longer posted; the whole function goes in D.
- **`on_init_inner`**: delete `Bind(EVT_SHOW_IP_DIALOG, &GUI_App::show_ip_address_enter_dialog_handler, this);` (:4993).
- **Delete** `show_ip_address_enter_dialog`, `show_modal_ip_address_enter_dialog` and `show_ip_address_enter_dialog_handler` (12520-12566).
  - Their only callers outside deleted files were `MediaFilePanel.cpp:469` and `MediaPlayCtrl.cpp:406` (deleted in B) and `SelectMachine.cpp:2894`/`SendToPrinter.cpp:797` (deleted now).
- **Delete** `popup_ping_bind_dialog` and `remove_ping_bind_dialog` (13381-13397). They have no callers [VERIFIED].

**C-3. `GUI/MainFrame.hpp:466` and `GUI/MainFrame.cpp:127`.** Delete the `EVT_SHOW_IP_DIALOG` declaration and definition. No poster remains.

**C-4. `GUI/MainFrame.cpp:62`.** Delete `#include "BindDialog.hpp"`. MainFrame.cpp only lost `boost/filesystem/operations.hpp` and `curl/curl.h`, both in `LegacyTransitiveIncludes.hpp` [VERIFIED].

**C-5. `GUI/BBLTopbar.hpp:6`.** Replace `#include "SelectMachine.hpp"` with SelectMachine.hpp's own non-Bambu includes (its lines 4-25 and 28-44, minus AmsMappingPopup and DeviceManager). This keeps what `MainFrame.hpp` users received through it:

```cpp
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
#include "ReleaseNote.hpp"
#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "Plater.hpp"
#include "BBLStatusBar.hpp"
#include "BBLStatusBarSend.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/ScrolledWindow.hpp"
#include "Widgets/PopupWindow.hpp"
#include "Jobs/Worker.hpp"
```

- [VERIFIED] Without this, `MainFrame.hpp` would lose `Plater.hpp`, `Model.hpp`, `ReleaseNote.hpp`, `BBLStatusBarSend.hpp` and many libslic3r headers.
- `BBLTopbar.hpp:8` (`#include "DeviceManager.hpp"`) stays until D.

**C-6. `GUI/BBLTopbar.cpp:1538-1552`** (`OnPublishClicked`). The body returns at once today because `getAgent()` is null, so `show_publish_dialog`/`open_publish_page_dialog` never run. Keep that behaviour without the agent:

```cpp
void BBLTopbar::OnPublishClicked(wxAuiToolBarEvent& event)
{
    // Publishing needed the removed Bambu network agent (F5).
}
```

Optional: delete the handler, its `Bind(..., ID_PUBLISH)` (BBLTopbar.cpp:1203) and its declaration (BBLTopbar.hpp:45). F3 left it as an orphan.

**C-7. `GUI/GUI_ObjectTable.cpp:4`.** `#include "SelectMachine.hpp"` → `#include "ReleaseNote.hpp"` and `#include "BBLStatusBarSend.hpp"`. The TU lost only those two, plus `wx/hashmap.h`, `wx/richtext/richtextctrl.h` and `wx/wrapsizer.h`, which ReleaseNote.hpp brings [VERIFIED].

**C-8. `GUI/Plater.cpp` and `GUI/Plater.hpp`.** Apply bottom-up.

1. **Includes.** Delete `#include "Jobs/PrintJob.hpp"` (:141), `#include "SelectMachine.hpp"` (:146) and `#include "SendToPrinter.hpp"` (:148). **Keep** `:5 #include "slic3r/GUI/print_manage/App/SendToPrinter.hpp"`.
2. **Move `PrintPrepareData`** into Plater.cpp, inside `namespace Slic3r { namespace GUI {` and immediately before `struct Plater::priv` (:6285). Same text as PrintJob.hpp:18-29; `fs` is Plater.cpp's own alias at :240:

   ```cpp
   // Moved from Jobs/PrintJob.hpp (F5). Used by send_gcode/export_config_3mf and the
   // print-host and Fluidd upload path (send_gcode_legacy, send_gcode_to_fluidd).
   class PrintPrepareData
   {
   public:
       bool     is_from_plater = true;
       int      plate_idx;
       fs::path _3mf_path;
       fs::path _3mf_config_path;
       fs::path _temp_path;
       PrintPrepareData() { plate_idx = 0; }
   };
   ```

3. **Event definitions.** Delete `wxDEFINE_EVENT(EVT_PRINT_FINISHED…)`, `EVT_SEND_CALIBRATION_FINISHED` and `EVT_SEND_FINISHED` (:412-414), and `EVT_PRINT_FROM_SDCARD_VIEW` (:427). Delete `wxDECLARE_EVENT(EVT_PRINT_FROM_SDCARD_VIEW…)` in Plater.hpp:113. Keep `EVT_PUBLISH_FINISHED` (:415).
4. **`priv` members:**
   - delete `SelectMachineDialog* m_select_machine_dlg` and `SendToPrinterDialog* m_send_to_sdcard_dlg` (:6293, :6295; :6294 already went in B);
   - delete `hide_select_machine_dlg`, `enter_prepare_mode` and `hide_send_to_printer_dlg` (:6532-6545, keeping the `// BBS` comment line optional).
5. **`priv` declarations.** Delete:
   - `on_action_print_plate_from_sdcard` (:6809);
   - `on_action_send_to_printer` (:6911);
   - `update_print_required_data` (:6916-6920);
   - `on_action_export_to_sdcard` and `on_action_export_to_sdcard_all` (:6940-6941).
6. **Binds.** Delete:
   - `q->Bind(EVT_PRINT_FROM_SDCARD_VIEW…)` (:7510);
   - `EVT_GLTOOLBAR_SEND_TO_PRINTER` / `_ALL` (:7517-7518);
   - `EVT_PRINT_FINISHED`, `EVT_SEND_CALIBRATION_FINISHED` and `EVT_SEND_FINISHED` (:7528-7530).

   MainFrame still posts `EVT_GLTOOLBAR_SEND_TO_PRINTER(_ALL)` (MainFrame.cpp:2144-2147, 2225-2228). That code can only run when `m_print_select` is `eSendToPrinter(_All)`, which needs the BBL-network "Send" button (§0.1), and an unbound event is harmless anyway. Optional: delete those `else if` branches and the `send_to_printer_btn`/`send_to_printer_all_btn` creation plus the `support_send` logic (MainFrame.cpp:2302-2304, 2331-2349, 2372-2398).
7. **`on_action_print_plate`** (:14395-14413): the body becomes

   ```cpp
       if (q != nullptr) {
           BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received print plate event\n";
       }
       q->send_gcode_legacy(PLATE_CURRENT_IDX, nullptr, true);
   ```

8. **Delete:**
   - `on_action_print_plate_from_sdcard` (:14445-14457);
   - `update_print_required_data` (priv, :14595-14604);
   - `on_action_send_to_printer` (:14606-14617).
9. **`on_action_print_all`** (:14697-14715): the body becomes

   ```cpp
       if (q != nullptr) {
           BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << ":received print all event\n";
       }
       q->send_gcode_legacy(PLATE_ALL_IDX, nullptr, true);
   ```

   `record_start_print_preset("print_plate"/"print_all")` was only called on the BBL branch, so it is dropped with it. **[INFERENCE]** It is now unused; leave it.
10. **More deletions:**
    - `on_action_export_to_sdcard` and `_all` (:14759-14773);
    - in `on_change_color_mode`, the lines `if (m_send_to_sdcard_dlg) m_send_to_sdcard_dlg->on_change_color_mode();` (:14939-14940);
    - `Plater::get_print_job_data` (:15314-15321);
    - `get_send_calibration_finished_event`, `get_print_finished_event` and `get_send_finished_event` (:15323, :15325, :15327). Keep `get_publish_finished_event` (:15329);
    - `Plater::send_to_printer` (:20911);
    - `send_calibration_job_finished`, `print_job_finished` and `send_job_finished` (:23242-23292). Keep `publish_job_finished`;
    - `Plater::update_print_required_data` (:23877-23884);
    - `get_select_machine_dialog` and `update_print_error_info` (:24364-24377);
    - in `sys_color_changed`, `if (p->m_select_machine_dlg) p->m_select_machine_dlg->sys_color_changed();` (:24782-24783).
11. **Plater.hpp deletions:**
    - :553 `send_to_printer`;
    - :626-628 the three `*_job_finished`;
    - :648 `update_print_required_data`;
    - :680 `update_print_error_info`;
    - :693 `get_select_machine_dialog`;
    - :709-712 `get_print_job_data` and the three `get_*_finished_event` (keep `get_publish_finished_event` if it is declared there).
12. **Keep `Sidebar::load_ams_list` / `build_filament_ams_list`** until D: they still use `MachineObject`, and DeviceManager.hpp is still reachable through GUI_App.hpp.

**C-9. `GUI/ReleaseNote.hpp`**
- **:29** `#include "AmsMappingPopup.hpp"` → the generic part of what AmsMappingPopup.hpp brought:

  ```cpp
  // F5: AmsMappingPopup.hpp removed; generic headers it used to bring in.
  #include "Plater.hpp"
  #include "BBLStatusBar.hpp"
  #include "BBLStatusBarSend.hpp"
  #include "Widgets/PopupWindow.hpp"
  #include "Widgets/TextInput.hpp"
  #include "Jobs/Worker.hpp"
  #include <wx/simplebook.h>
  ```

- **Delete** `class InputIpAddressDialog` (:296-339).
- **Delete** `wxDECLARE_EVENT(EVT_CLOSE_IPADDRESS_DLG…)` (:341), `EVT_ENTER_IP_ADDRESS` (:343) and `EVT_CHECK_IP_ADDRESS_FAILED` (:344).
- **Keep** `EVT_CHECKBOX_CHANGE` (:342): `MsgDialog.cpp:84` posts it.
- **Keep** `#include "DeviceManager.hpp"` (:32) until D. Nothing in ReleaseNote.hpp needs it any more, but its includers may.
- **Keep** `ReleaseNoteDialog`, `UpdatePluginDialog`, `SecondaryCheckDialog`, `PrintErrorDialog` and `ConfirmBeforeSendDialog`.

**C-10. `GUI/ReleaseNote.cpp`**
- **:19** `#include "slic3r/GUI//Widgets/AMSControl.hpp"` → `#include "slic3r/GUI/Widgets/AmsColors.hpp"`.
- **Delete** the `wxDEFINE_EVENT` lines for `EVT_ENTER_IP_ADDRESS` (:43), `EVT_CLOSE_IPADDRESS_DLG` (:44) and `EVT_CHECK_IP_ADDRESS_FAILED` (:45). Keep `EVT_CHECKBOX_CHANGE` (:42).
- **Delete** lines 1598-2062: every `InputIpAddressDialog::` definition, from the constructor to `on_dpi_changed` and the blank line after it.
  - Line 1597 (blank, after `ConfirmBeforeSendDialog::rescale`) and the closing `}} // namespace Slic3r::GUI` (today 2064) remain.
  - This also removes the only uses of `SendJob` (:1967), `MachineObject` (:1853) and `DeviceManager::get_printer_diagram_img` (:1859) in kept code.

**C-11. `GUI/Preferences.cpp:30`.** Replace `#include "slic3r/GUI//Widgets/AMSControl.hpp"` with:

```cpp
#include "slic3r/GUI/Widgets/AmsColors.hpp" // AMS_CONTROL_* colours (1703-1706)
#include "slic3r/GUI/ReleaseNote.hpp"       // ConfirmBeforeSendDialog (1777)
```

[VERIFIED] Without AMSControl.hpp the TU loses `ReleaseNote.hpp`, `Jobs/Worker.hpp`, `ProgressBar`, `ProgressDialog`, `StepCtrl` and a few wx headers. Everything except ReleaseNote.hpp comes back through `LegacyTransitiveIncludes.hpp` via GUI_App.hpp.

**C-12. (if F3 left it) `GUI/PublishDialog.cpp:14`.** Replace the AMSControl include with `#include "slic3r/GUI/Widgets/AmsColors.hpp"`.

**C-13. (if F4 left them) `print_manage/Upload3mfToCloud.*` and `print_manage/UploadGcodeToCloud.*`.** They need `SelectMachine.hpp` (`ThumbnailPanel`, `PrintDialogStatus`), `ConnectPrinter.hpp` and `EVT_CLEAR_IPADDRESS`.
- Stage C cannot build until F4 removes them.
- Do not move `ThumbnailPanel` into kept code for them.

### 4.4 Stage C compile risks

- **Fan-out from `GUI_App.hpp:26`.**
  - Mitigated by C-1, which includes `LegacyTransitiveIncludes.hpp` and keeps `DeviceManager.hpp` (with `json_diff`'s `using namespace std` and `using json`).
  - **[INFERENCE]** Residual failures should be limited to TUs that used Bambu-only types (none outside the files edited above).
- **`MainFrame.hpp` users.** Mitigated by C-5.
- **Plater.cpp:**
  - `PrintPrepareData` must be defined before `struct Plater::priv`;
  - `get_print_job_data` must go from Plater.hpp, which otherwise names an undeclared type.
- **`DeviceManager.cpp` (still compiled)** includes `ReleaseNote.hpp` and uses `SecondaryCheckDialog` (kept). It uses nothing from the deleted IP dialog [VERIFIED].
- **`HMS.cpp` and `UserManager.cpp` (still compiled)** reach `BindDialog.hpp` only through GUI_App.hpp, which now carries the compat header instead.

### 4.5 Stage C verification

1. **Build.**
2. **Grep** (must print nothing in `src` or `list.txt`):

   ```
   git grep -n -E "\b(SelectMachineDialog|SendToPrinterDialog|BindMachineDialog|UnBindMachineDialog|PingCodeBindDialog|ConnectPrinterDialog|InputIpAddressDialog|AmsMapingPopup|AmsMappingPopup|AMSControl|CalibUtils|BindJob|PrintJob|SendJob|BBLStatusBarBind|ThumbnailPanel|PrintDialogStatus|update_print_required_data|get_select_machine_dialog|update_print_error_info|EVT_SHOW_IP_DIALOG|EVT_PRINT_FROM_SDCARD_VIEW)\b" -- src localization/i18n/list.txt
   ```

   The only expected hits are comments that mention `PrintJob`: Plater.cpp:6897 and the new `PrintPrepareData` comment. `MultiPrintJob` went in B.
3. **`git grep -n "print_manage/App/SendToPrinter" -- src localization`** still shows the kept includes and list entries.
4. **Smoke test as in B, plus:**
   - "Print plate" with the K1C (Creality, so it goes to the Device tab) and "Send" through the LAN dialog. Stop at the dialog; the printer is busy.
   - Physical-printer / print-host upload, which is the `send_gcode_legacy` → `m_print_job_data` path. If no print host is configured, check only that **Export G-code** works.
5. **G-code identical** to F0.

**Commit:** `F5-C: remove Bambu send, bind and print-job dialogs`.

---

## 5. Stage D: core (DeviceManager, NetworkAgent, UserManager, HMS, plug-in download, dead GUI_App code)

### 5.1 Delete these files (`git rm`): 19 files, 11,408 lines

```
src/slic3r/GUI/DeviceManager.cpp  DeviceManager.hpp
src/slic3r/GUI/UserManager.cpp  UserManager.hpp
src/slic3r/GUI/HMS.cpp  HMS.hpp
src/slic3r/GUI/Jobs/UpgradeNetworkJob.cpp  UpgradeNetworkJob.hpp
src/slic3r/GUI/DownloadProgressDialog.cpp  DownloadProgressDialog.hpp
src/slic3r/GUI/WebDownPluginDlg.cpp  WebDownPluginDlg.hpp
src/slic3r/GUI/NetworkTestDialog.cpp  NetworkTestDialog.hpp
src/slic3r/Utils/NetworkAgent.cpp  NetworkAgent.hpp  bambu_networking.hpp
src/slic3r/Utils/json_diff.cpp  json_diff.hpp
```

**Keep** `CloudDownloadProgressDialog.*` (Creality cloud, F4 territory) and `BBLStatusBar*.hpp`.

### 5.1b CMake and list.txt (stage D)

**CMake lines to remove:** 99-100 (NetworkTestDialog), 338-339 (HMS), 429-430 (WebDownPluginDlg), 484-487 (DeviceManager, UserManager), 516-517 (DownloadProgressDialog), 528-529 (UpgradeNetworkJob), 653-654 (json_diff) and 718-719 (NetworkAgent).

```bash
for p in GUI/NetworkTestDialog GUI/HMS GUI/WebDownPluginDlg GUI/DeviceManager GUI/UserManager \
         GUI/DownloadProgressDialog GUI/Jobs/UpgradeNetworkJob Utils/json_diff Utils/NetworkAgent \
         Utils/bambu_networking; do
  sed -i -E "\#^[[:space:]]*${p}\.(cpp|hpp|h)\r?\$#d" src/slic3r/CMakeLists.txt
  sed -i -E "\#^src/slic3r/${p}\.(cpp|hpp|h)\r?\$#d" localization/i18n/list.txt
done
```

**list.txt lines removed:** 45 181 273 274 433 455 514 526 535 547 586 599 607 757 842 843 848 869 870.

### 5.1c Optional: `src/CMakeLists.txt:81` and `:83`

Drop `media` from the `find_package(wxWidgets ... COMPONENTS ...)` lists, since nothing uses `wxMediaCtrl` after stage B [VERIFIED by `git grep wxMediaCtrl|mediactrl`].
- Leave `-DwxUSE_MEDIACTRL=ON` in `deps/wxWidgets/wxWidgets.cmake` alone; changing deps forces a deps rebuild.
- If the link fails with an unresolved `wxMedia*` symbol, something still uses it: revert the line.

### 5.2 `src/slic3r/GUI/GUI_App.cpp` (apply bottom-up, anchors in brackets)

1. [`popup_ping_bind_dialog`] already removed in C.
2. [`open_publish_page_dialog`, `open_mall_page_dialog`, 13292-13371] (F3 left both to F5):
   - **Delete `open_publish_page_dialog`** (13334-13371) and its declaration (GUI_App.hpp:701). Its callers, `BBLTopbar::OnPublishClicked` (C-6) and the non-Windows MainFrame menu (D-M4), are gone.
   - **In `open_mall_page_dialog`**, delete the `if (getAgent() && mainframe) { ... }` block (13308-13319). `result` stays `-1`, so the `link_url = host_url + model_url` fallback runs exactly as today.
   - Keep `open_mall_page_dialog`: the Home page command `homepage_modeldepot` (GUI_App.cpp:8067-8071) still calls it. It opens makerworld.com in the browser; see §9 Q7.
3. [`show_ip_address_enter_dialog*`] already removed in C.
4. [`start_sync_user_preset`, 11688-11977] **(F4)**:
   - Delete the dead code after the first `return;` (from `// has already start sync` at about 11709 to the end of the function body).
   - It uses `ProgressFn`/`WasCancelledFn` from `bambu_networking.hpp`.
5. [`on_user_login`, from 9597] **(F4)**:
   - The body starts with `if (!m_agent) { return; }`. Replace the whole body with `(void)evt;`, or delete the function and its Bind and declaration if F4 kept nothing else.
6. [`check_track_enable`, 9588-9595] **(F4)** empty the body: `{ }`. Its only call is in `on_user_login`.
7. [`on_user_login_handle`, 9560-9586] **(F4)**:
   - The body begins with `if (!m_agent) { return; }`. Replace it with `{ (void)evt; }`.
   - Keep the `Bind(EVT_USER_LOGIN_HANDLE...)` and `request_user_handle` unless F4 removed them.
8. [`on_update_machine_list`, `on_set_selected_machine`, 9544-9558] delete both.
9. [`on_http_error`, 9481-9530] inside `if (status == 401) { ... }`, delete the `if (m_agent) { ... }` block (9511-9526) and keep `return;`.
10. [`handle_http_error`, 9472-9479] delete it if `git grep -n "handle_http_error" -- src` shows no other caller. F1 removes the caller at 9915; the callers at 2488 and 3054 go below.
11. [`handle_script_message`, 9401-9420] **(F4)** delete the `if (cmd == "user_login") { if (m_agent) {...} }` branch (9407-9414). **[INFERENCE]** F4 may already have removed it.
12. [`request_user_unbind`, 6686-6695] **(F4)** delete. The only caller was `BindDialog.cpp:1102`.
13. [`request_user_logout`, 6662-6684] **(F4)**:
    - The whole body is `if (m_agent && m_agent->is_user_login()) {...}`. Replace it with `{ }`.
    - This also removes the `load_ams_list({}, {})` and `m_device_manager->clean_user_info()` calls.
14. [`check_login`, 6635-6646] **(F4)** delete the three `if (m_agent) { result = m_agent->is_user_login(); }` lines (6638-6640).
15. [`is_user_login`, 6616-6622] **(F4 already stubs it)** the body becomes `return false; // Bambu account; the network agent was removed (F5)`. It is still called by ConfigRelateDialog, Tab, Plater, WebGuideDialog and others, so keep it.
16. [`get_login_info`, 6598-6614] **(F4)** the body becomes empty `{ }`.
17. [`ShowDownNetPluginDlg`, 6296-6308] delete. Callers are edited below: Plater (D-P3), Preferences (D-R2), WebGuideDialog (D-W2) and WebUserLoginDialog (F4).
18. [`link_to_network_check`, 5959-5975] delete. Its callers were `Widgets/SideTools.cpp:333` (deleted in B) and `SelectMachine.cpp:1370`, `SendToPrinter.cpp:407` and `BindDialog.cpp:601` (deleted in C). That is why it survives until D.
19. [`on_init_network`, 5455-5564] delete.
20. [`copy_network_if_available`, 5373-5453] delete. It handled the Bambu plug-in, BambuSource and live555 files.
21. [`on_init_inner`, the idle lambda at 5241-5260] keep the `m_studio_active` update and the log lines; delete the two `if (m_agent) { ... start_subscribe/stop_subscribe ... }` blocks (5247-5250, 5253-5256).
22. [`on_init_inner`, 4999-5006] replace

    ```cpp
        copy_network_if_available();
        on_init_network();

        if (m_agent && m_agent->is_user_login()) {
            enable_user_preset_folder(true);
        } else {
            enable_user_preset_folder(false);
        }
    ```

    with

    ```cpp
        enable_user_preset_folder(false); // no Bambu account agent (F5)
    ```

23. [`on_init_inner`, 4986-4987] delete `Bind(EVT_SET_SELECTED_MACHINE, ...)` and `Bind(EVT_UPDATE_MACHINE_LIST, ...)`.
24. [`on_init_inner`, 4658-4674] delete the `if (m_last_config_version) { ... remove_old_networking_plugins(); }` block. It only compares versions to delete `<data_dir>/plugins`.
    - Check that `m_last_config_version` is still used elsewhere (`git grep`). If not, F1/F6 can drop it.
25. [`OnExit`, 4390-4407] delete the `m_device_manager`, `m_user_manager` and `m_agent` cleanup.
    - Optional: also delete the "clean up encrypted bbl network log file" `try { ... } catch` block (4409-4428). It only removes `debug_network_*.log.enc` files the plug-in used to write.
26. [`on_start_subscribe_again`, 4123-4140] delete.
27. [`update_http_extra_header`, 4115-4121] delete the two lines `if (m_agent) m_agent->set_extra_http_header(extra_headers);`. Keep the function unless F1 removed it.
28. [`init_http_extra_header`, 4107-4113] delete. The only caller was `on_init_network`.
29. [3037-3370] delete `restart_networking`, `remove_old_networking_plugins`, `updating_bambu_networking`, `check_networking_version`, `cancel_networking_install` and `init_networking_callbacks`.
    - **Keep `is_compatibility_version`** (3122-3126) as a stub. PrinterPresetConfig.cpp:398-399, WebGuideDialog.cpp:1535-1536 and WebViewDialog.cpp:659 still call it:

      ```cpp
      bool GUI_App::is_compatibility_version()
      {
          return false; // the Bambu network plug-in no longer exists (F5)
      }
      ```

30. [2730-3035] delete `get_plugin_url`, the file-static `decode` (2743-2756, used only by `install_plugin`), `download_plugin` and `install_plugin`. Callers: WebGuideDialog (D-W3).
31. [`get_model_http_url`, 2704-2727] **keep**, because `open_mall_page_dialog` still uses it (item 2). D-V1 removes its other caller, `WebViewPanel::get_model_mall_detail_url`. It goes with Q7.
32. [`get_http_url`, 2678-2702] delete. First change its last caller in `post_init` (2441):

    ```cpp
                std::string http_url; // F5: Bambu API URL removed; PresetUpdater::sync ignores it since F1
    ```

    The F1-deleted ranges held the other callers (9876, 10792).
33. [`post_init`, 2505] delete `DeviceManager::load_filaments_blacklist_config();`.
34. [`post_init`, 2474-2491] delete the whole `if(!m_networking_need_update && m_agent) { ... start_discovery ... }` block.
35. [`post_init`, 2493-2496] optional: delete the `CallAfter([this]{ mainframe->refresh_plugin_tips(); });`, since `MainFrame::refresh_plugin_tips` has an empty body. If you delete it, also delete `refresh_plugin_tips` (MainFrame.cpp:5308-5312, MainFrame.hpp:367) and its other callers in GUI_App.cpp: 3060 (gone with item 29), 6288 and 13789 (check them with `git grep`).
36. [`post_init`, 2406-2420] delete `if (app_config->get("stealth_mode") == "false") hms_query = new HMSQuery();` and the `if (m_networking_need_update) { ... updating_bambu_networking() ... }` block.
37. **Includes:** delete
    - `#include "slic3r/GUI/UserManager.hpp"` (:15);
    - `#include "slic3r/GUI/DeviceManager.hpp"` (:33);
    - `#include "slic3r/Utils/NetworkAgent.hpp"` (:34);
    - `#include "slic3r/GUI/HMS.hpp"` (:35);
    - `#include "DownloadProgressDialog.hpp"` (:172);
    - `#include "WebDownPluginDlg.hpp"` (:186).

### 5.3 `src/slic3r/GUI/GUI_App.hpp` (stage D)

**Includes and forward declarations.** Delete:
- the C-added line `#include "slic3r/GUI/DeviceManager.hpp"`. Keep `LegacyTransitiveIncludes.hpp`;
- `#include "slic3r/GUI/Jobs/UpgradeNetworkJob.hpp"` (:28);
- the commented `//#include "slic3r/GUI/DeviceManager.hpp"`, `// #include "slic3r/Utils/NetworkAgent.hpp"` and `// #include "slic3r/GUI/HMS.hpp"` (:21, :23, :27; optional);
- `class UserManager;`, `class DeviceManager;` and `class NetworkAgent;` (:69-71);
- `class UpgradeNetworkJob;` (:78);
- `class HMSQuery;` (:94).

**Members.** Delete:
- `m_device_manager` (:287), `m_user_manager` (:288) and `m_agent` (:290);
- `m_networking_compatible`, `m_networking_need_update`, `m_networking_cancel_update` and `m_upgrade_network_job` (:294-297);
- `HMSQuery *hms_query` (:308);
- `boost::thread m_check_network_thread;` (:321). It is never used [VERIFIED].

**Declarations.** Delete:
- `on_start_subscribe_again` (:343);
- `check_filaments_in_blacklist` (:350; declared, never defined);
- `getDeviceManager()` (:385), `get_hms_query()` (:387) and `getAgent()` (:388);
- `link_to_network_check` (:470);
- `ShowDownNetPluginDlg` (:517);
- `request_user_unbind` (:533);
- `handle_http_error` (:542) only if item 10 deleted it;
- `on_set_selected_machine` and `on_update_machine_list` (:544-545);
- `get_plugin_url`, `download_plugin`, `install_plugin` and `get_http_url` (:804-807);
- `open_publish_page_dialog` (:701);
- keep `get_model_http_url` (:808) and `open_mall_page_dialog` (:700);
- `check_networking_version`, `cancel_networking_install` and `restart_networking` (:810-812);
- `updating_bambu_networking` (:846);
- `copy_network_if_available`, `on_init_network` and `init_networking_callbacks` (:853-855);
- `remove_old_networking_plugins` (:857);
- `init_http_extra_header` (:858).

**Keep:** `is_compatibility_version()` (:809), `is_user_login()`, `is_enable_multi_machine()`, `update_http_extra_header()` (unless F1 removed it) and `webGetDevicesInfo` (Creality).

### 5.4 Other kept files (stage D)

**D-P. `GUI/Plater.cpp` / `Plater.hpp`**

1. **Delete** `#include "slic3r/GUI/HMS.hpp"` (:14).
2. **Delete** `Sidebar::build_filament_ams_list` and `Sidebar::load_ams_list` (3661-3725) and their declarations (Plater.hpp:186-187).
   - **Keep** `Sidebar::sync_ams_list` (3727-…, Plater.hpp:188), the `ams_list_device` member (:850), `PresetBundle::filament_ams_list` and `PresetBundle::sync_ams_list`, as the brief asks.
   - Optional: delete `class Ams;` (Plater.hpp:58).
3. **Delete** the plug-in installer handler:
   - `priv::install_network_plugin` (definition 14879-14883, declaration 6777);
   - its `Bind(EVT_INSTALL_PLUGIN_NETWORKING, …)` (:7136).

   Keep `wxDEFINE/DECLARE_EVENT(EVT_INSTALL_PLUGIN_NETWORKING)` (Plater.cpp:420, Plater.hpp:108) while `NotificationManager::bbl_show_plugin_install_notification` still posts it. See D-N for removing both.
4. **Delete** the line `wxGetApp().getDeviceManager()->reload_printer_settings();` (or the A3 guard) in the `EVT_PRINTER_CONFIG_UPDATE_AVAILABLE_CLICKED` handler (:7576).
5. **Delete** these unused locals `NetworkAgent* agent = wxGetApp().getAgent();` (:16475, :17103, :17209, :22941).
6. **Delete** the block `if (wxGetApp().is_user_login()) { p->model.design_info = …; …getAgent()->get_user_id(); … }` (22177-22184). `is_user_login()` is always false, so the block never runs.
7. **Optional:** delete `priv::show_install_plugin_hint` (14907-14911, declaration 6776), its Bind (:7137) and `EVT_INSTALL_PLUGIN_HINT` (Plater.cpp:422, Plater.hpp:109). F2 removed its only poster (Plater.cpp:14470).

**D-M. `GUI/MainFrame.cpp` / `MainFrame.hpp`**

1. **Delete** `#include "NetworkTestDialog.hpp"` (:79). Nothing else in the file uses it [VERIFIED].
2. **Delete** `wxDEFINE_EVENT(EVT_SET_SELECTED_MACHINE…)` and `EVT_UPDATE_MACHINE_LIST` (:128-129), and their declarations (MainFrame.hpp:467-468).
3. **Shutdown, :1294-1297.** Delete `// stop agent`, `NetworkAgent* agent = …;` and `if (agent) agent->track_enable(false);`.
4. **`add_common_publish_menu_items`, 3044-3067** (inside `#ifndef __WINDOWS__`): delete the two `append_menu_item(...)` blocks ("Upload Models", "Download Models").
   - They return at once without an agent; F3 may already have removed them.
   - The block does not compile on Windows. It is listed so non-Windows builds stay valid.
5. **:4593-4596** delete `NetworkAgent* agent = …; if (agent) { BOOST_LOG_TRIVIAL(...) << agent->get_user_id(); }`.
6. **:5405** **(F4)** delete `NetworkAgent* agent = wxGetApp().getAgent();` (unused local in `show_sync_dialog`, which F4 deletes).

**D-R. `GUI/Preferences.cpp`**

1. **Region combo handler (339-358).** Replace

   ```cpp
           NetworkAgent* agent = wxGetApp().getAgent();
           AppConfig* config = GUI::wxGetApp().app_config;
           if (agent) { …logout dialog… } else {
               config->set("region", region.ToStdString());
           }
   ```

   with

   ```cpp
           AppConfig* config = GUI::wxGetApp().app_config;
           config->set("region", region.ToStdString());
   ```

   The rest of the handler (`update_publish_status`, `refresh_machine_list_for_region` unless F1 removed it, …) stays.
2. **`installed_networking` branch (915-920).** Delete `if (param == "installed_networking") { … ShowDownNetPluginDlg … }`. No Preferences item creates that key [VERIFIED by grep], so the branch is dead.
3. **IoT environment handler (1756, 1774-1777).**
   - Delete `NetworkAgent* agent = wxGetApp().getAgent();`.
   - Delete the `if (agent) { wxGetApp().request_user_logout(); agent->set_country_code(country_code); }` block.
   - Keep the `ConfirmBeforeSendDialog` that follows.

**D-W. `GUI/WebGuideDialog.cpp` / `.hpp`**

1. **885-895.** Delete the `if (m_Region != oldregion) { … NetworkAgent* agent … }` block. Its only effect was on the agent.
   - Optional: delete the now-unused `oldregion` (:883) and `bLogin` (:884) and the `if (bLogin) …login()` (:903-904). `bLogin` is always false.
2. **899-901.** Delete `if (InstallNetplugin) GUI::wxGetApp().CallAfter([this] { GUI::wxGetApp().ShowDownNetPluginDlg(); });`.
   - Optional: drop `InstallNetplugin` (.hpp:111, .cpp:166, 912-923). The `network_plugin_install` branch can stay as a no-op.
3. **`GuideFrame::DownloadPlugin`, `InstallPlugin` and `ShowPluginStatus`.** Delete the definitions (1733-1756) and the declarations (.hpp:88-90). They have no callers [VERIFIED].
4. **1534-1536** (`network_plugin_install` / `_compability` in `m_ProfileJson`). These stay, using the `is_compatibility_version()` stub, so the guide page JSON keeps its shape.

**D-V. `GUI/WebViewDialog.cpp` / `.hpp`** (Home page; F2 variant A may already have removed its use)

1. **Delete** `WebViewPanel::OpenModelDetail` (632-644), `SendLoginInfo` (646-653) and `get_model_mall_detail_url` (692-700), with their declarations (.hpp:101-102, :106) and `class NetworkAgent;` (.hpp:32).
   - The only reference to OpenModelDetail is commented out (GUI_App.cpp:8254).
   - SendLoginInfo has no callers.
2. **`ShowNetpluginTip` (655-675)** only uses `is_compatibility_version()` and has no live callers (commented at MainFrame.cpp:5311 and WebViewDialog.cpp:763). Delete it and its declaration (.hpp:103), or keep it with the stub.

**D-H. `GUI/HttpServer.cpp` / `.hpp`**
- Delete `#include "slic3r/Utils/NetworkAgent.hpp"` (:8).
- **(F4)** Delete `HttpServer::bbl_auth_handle_request` (1008-1065) and its declaration (.hpp:98) if F4 left them. They have no callers [VERIFIED].

**D-T. `GUI/Tab.cpp:9044-9045`.** Delete `if (wxGetApp().is_user_login()) new_preset->user_id = wxGetApp().getAgent()->get_user_id();`.

**D-CR. `GUI/ConfigRelateDialog.cpp`**
- Delete the `if (wxGetApp().is_user_login()) pp.user_id = wxGetApp().getAgent()->get_user_id();` pairs at 1442-1443 and 1521-1522.
- In the close handler (3333-3346), delete the `NetworkAgent* agent = …; if (agent) { …track_event… }` lines (3334-3343). Keep `try { } catch(...) {}` or reduce it to `event.Skip();`.

**D-CP. `GUI/CreatePresetsDialog.cpp`**
- **:4693.** **(F4)** Already replaced by F4 with `const bool sync_user_preset_need_enabled = false;`. Check only.
- **6453-6461 and 6572-6580.** Replace each `NetworkAgent *agent = …; if (agent) {…} else {…}` with the `else` branch, keeping the `clock` line:

  ```cpp
              std::string   clock = get_curr_timestmp();
              bundle_structure["version"]   = "";
              bundle_structure["bundle_id"] = "offline_" + printer_preset_name_ + "_" + clock;   // filament_name in the second block
  ```

- **:7017.** `bundle_structure["version"] = "";`.

**D-BT. `GUI/BBLTopbar.hpp:8`.** `#include "DeviceManager.hpp"` → delete. `BBLTopbar.hpp` now includes `GUI_App.hpp` (C-5), which carries `LegacyTransitiveIncludes.hpp`.

**D-RN. `GUI/ReleaseNote.hpp:32`.** `#include "DeviceManager.hpp"` → `#include "LegacyTransitiveIncludes.hpp"`, so includers keep `using namespace std` / `json` / nlohmann.

**D-WV. `GUI/WebViewDialog.hpp:32`.** Covered by D-V.

**D-N. Optional: `GUI/NotificationManager.cpp/.hpp`**
- Delete `bbl_show_plugin_install_notification` and `bbl_close_plugin_install_notification` (cpp 3356-3384, hpp 346-348). Their callers (`show_install_plugin_hint`, `restart_networking`) are gone.
- Then delete `EVT_INSTALL_PLUGIN_NETWORKING` (Plater.cpp:420, Plater.hpp:108).
- The `BBLPluginUpdateAvailable` entry (hpp 960-968) posts `EVT_UPDATE_PLUGINS_WHEN_LAUNCH`, which opens `UpdatePluginDialog` (Plater.cpp:14885-14905; F1 kept it). With F1's `PresetUpdater` no notification of this type is ever pushed **[INFERENCE: `git grep BBLPluginUpdateAvailable` after F1 shows no push]**, so removing it, `update_plugin_when_launch` and `UpdatePluginDialog` (ReleaseNote) is also safe. Optional, one commit.
- Keep the `NotificationType` enum values. Removing them changes nothing at runtime.

**D-PR. Optional: `resources/printers/` (11 files, 39 KB)**
- Before `git rm -r resources/printers`, change `src/libslic3r/DataDirectoryMigration.cpp:483` to:

  ```cpp
          if (fs::is_directory(o.resources / "printers")) // F5: Bambu printer descriptors were removed
              copy_tree(o.resources / "printers", stage / "printers");
  ```

  `copy_tree` throws on a missing folder (:214). That would abort a data-directory migration.
- This is a libslic3r `.cpp` change (one file) and does not affect G-code.
- If you prefer not to touch libslic3r, keep the folder. Nothing reads it after F5 except the migration copy.

**(if F4 left them)**
- `LoginDialog.cpp:8` (NetworkAgent include, comment at :624), `WebUserLoginDialog.cpp` (91, 110, 272) and `print_manage/Upload*`: they must be gone before stage D builds. They are F4 deliverables.
- `request_user_logout` / `on_user_login*`: see §5.2.

### 5.5 Stage D compile risks

- **Fan-out.** `GUI_App.hpp` loses `DeviceManager.hpp`, `json_diff.hpp`, `NetworkAgent.hpp` and `UpgradeNetworkJob.hpp`. `LegacyTransitiveIncludes.hpp` restores:
  - `using json = nlohmann::json;`, `using namespace std;` and `using namespace nlohmann;`;
  - `boost/thread`, `boost/nowide/fstream`, `boost/log`, `boost/filesystem` (plus the `fs` alias from `libslic3r/ProjectTask.hpp`);
  - `curl`, `calib.hpp`, `Utils.hpp` and `PrintConfig.hpp`.
- **`using namespace BBL;`** is not restored on purpose. Only GUI_App.cpp's dead code (start_sync_user_preset after `return;`, `init_networking_callbacks`) used `BBL::` names unqualified, and both are deleted.
- **Global macros from DeviceManager.hpp** (`VIRTUAL_TRAY_ID`, `FILAMENT_MAX_TEMP`, `HOLD_*`, …). The only kept user was `build_filament_ams_list` (Plater.cpp:3683), deleted in D-P2 [VERIFIED by grep of all 25 macro names].
- **`MachineObject`/`DeviceManager` types.** After D-P2, `git grep` must show none in `src`.
- **`InstallProgressFn` / `WasCancelledFn`.** Only in deleted declarations, plus F4's `start_sync_user_preset` (§5.2 item 4).

### 5.6 Stage D verification

1. **Build.**
2. **Grep** (§7 final checklist).
3. **Full startup protocol** of the cleanup plan (§ "Protocollo di verifica", items 1-6). Then:
   - Print Advisor `npm run typecheck` and `npm run smoke`;
   - `apply-web-patches.mjs --check`;
   - G-code of the four F0 projects is identical (`check-gcode.mjs`).
4. **Log check.** No `on_init_network`, `NetworkAgent`, `DeviceManager` or `HMS` lines in the startup log (`<data_dir>/log`).
5. **Network check (F1 item 7).** 5 minutes without connections to `*.bambulab.*` or `makerworld.com`. The only remaining Bambu URL is behind the Home "model depot" button (Q7), so do not click it during the check.

**Commit:** `F5-D: remove DeviceManager, NetworkAgent, UserManager, HMS, network plug-in and dead GUI_App code`. Then, optionally, separate commits for D-N, D-PR and §5.1c.

---

## 6. Threads, lifecycle, behaviour

**Threads and lifecycle [VERIFIED]**
- Today no Bambu object is ever created, so no Bambu threads run: `DeviceManager`, `TaskManager::start`, `NetworkAgent::start`/`start_discovery` and `UserManager` are all reached only through `on_init_network` after its `return false;`, or through `restart_networking`, which nothing calls.
- **Still runs today and is removed in D:**
  - `new HMSQuery()` (empty constructor; no download is triggered: `check_hms_info` is commented out at 2499-2502);
  - `DeviceManager::load_filaments_blacklist_config()` (reads one JSON file at start);
  - `remove_old_networking_plugins()` (on a version change);
  - the empty `refresh_plugin_tips` CallAfter (optional removal);
  - the OnExit cleanup of `debug_network_*.log.enc`.
- **`OnExit` order** is unchanged. `advisor_on_shutdown()` stays first; the `m_device_manager`/`m_user_manager`/`m_agent` deletes were no-ops on null pointers.
- **No GUI-thread change.** `CallAfter` uses in kept code are untouched. The Print Advisor (`PrintAdvisorServer`, `PrintAdvisorMonitor`, `DeviceTelemetryCache`, `WebSocketProxy*`) and SlicerBridge do not reference any removed symbol [VERIFIED by grep of `simple/**` and `print_manage/**`].

**Behaviour changes**
- **BBL-vendor presets only** (stage A): "Print plate/all" uses `send_gcode_legacy`; "Send"/"Send all" are not offered; the sidebar connection button shows. Creality presets are unchanged.
- **User-visible:** none for the K1C workflow.
  - The "Network plug-in" items were already unreachable: `installed_networking` is false, and no Preferences item sets it.
  - The guide page keeps receiving `network_plugin_compability: "0"`, as today.
- **`filament_ams_list`** stays empty for good. Its only writers were Bambu, and nothing in `print_manage/` writes it.

---

## 7. Final grep checklist (after stage D)

Run from the repository root. Each command must print nothing, except the noted lines in `doc/`, comments you decide to keep, and the kept names.

```bash
# Removed classes, functions and files
git grep -n -E "\b(DeviceManager|MachineObject|NetworkAgent|UserManager|TaskManager|HMSQuery|HMSItem|json_diff|bambu_networking|UpgradeNetworkJob|DownloadProgressDialog|WebDownPluginDlg|NetworkTestDialog|SelectMachineDialog|SendToPrinterDialog|BindMachineDialog|PingCodeBindDialog|ConnectPrinterDialog|InputIpAddressDialog|AmsMapingPopup|AMSControl|AmsWidgets|AMSSetting|AMSMaterialsSetting|CalibUtils|CalibrationPanel|CalibrationWizard|CaliHistoryDialog|ExtrusionCalibration|MonitorPanel|StatusPanel|MediaPlayCtrl|MediaFilePanel|wxMediaCtrl2|PrinterFileSystem|BambuTunnel|gstbambusrc|MultiMachine\w*|SendMultiMachinePage|ProjectPanel|AuxiliaryPanel|CameraPopup|PrintOptionsDialog|SliceInfoPanel|FanControl|SideTools|TempInput|ImageSwitchButton|AxisCtrlButton|CalibrateFilamentComboBox)\b" -- src localization/i18n/list.txt

# Removed GUI_App API
git grep -n -E "\b(getAgent|getDeviceManager|getTaskManager|get_hms_query|m_agent|m_device_manager|m_user_manager|m_task_manager|hms_query|on_init_network|copy_network_if_available|init_networking_callbacks|restart_networking|download_plugin|install_plugin|get_plugin_url|get_http_url|ShowDownNetPluginDlg|link_to_network_check|popup_ping_bind_dialog|show_ip_address_enter_dialog|show_modal_ip_address_enter_dialog|request_user_unbind|on_start_subscribe_again|updating_bambu_networking|check_networking_version|remove_old_networking_plugins|init_http_extra_header|load_ams_list|build_filament_ams_list|bbl_auth_handle_request|EVT_SET_SELECTED_MACHINE|EVT_UPDATE_MACHINE_LIST|EVT_SHOW_IP_DIALOG|EVT_PRINT_FROM_SDCARD_VIEW|update_print_required_data|get_select_machine_dialog|update_print_error_info)\b" -- src

# Bambu endpoints and plug-in files (strings)
git grep -n -i -E "bambulab\.(com|cn)|bambu-lab\.com|makerworld|BambuSource|bambu_networking|live555|network_plugin\.zip" -- src

# Kept on purpose (must still be present)
git grep -n -E "\b(PrintPrepareData|filament_ams_list|sync_ams_list|use_bbl_network|is_compatibility_version|is_user_login|CxSentToPrinterDialog|BBLStatusBarSend|SecondaryCheckDialog|ConfirmBeforeSendDialog|LegacyTransitiveIncludes|AmsColors)\b" -- src | wc -l
git grep -n "print_manage/App/SendToPrinter" -- src/slic3r/CMakeLists.txt localization/i18n/list.txt
```

**Expected leftovers to review:**
- `DeviceManager`/`TaskManager` mentioned in comments (for example MainFrame.cpp's former `//MonitorPanel*` lines, if not deleted).
- `NotificationType::BBLPlugin*` enum values (optional D-N).
- `resources/printers` and `DataDirectoryMigration` (optional D-PR).
- `get_model_http_url` and the `makerworld.com` URLs in GUI_App.cpp (kept for `open_mall_page_dialog`, Q7).
- `Http::url_decode`/`base64::decode` and similar do not match these patterns.

---

## 8. Kept files touched, per stage (quick index)

| File | A | B | C | D |
|---|---|---|---|---|
| libslic3r/PresetBundle.cpp | A1 | | | |
| libslic3r/DataDirectoryMigration.cpp | | | | D-PR (opt.) |
| GUI/GUI_App.cpp | A2 | B-6 | C-2 | §5.2 |
| GUI/GUI_App.hpp | | B-5 | C-1 | §5.3 |
| GUI/Plater.cpp / .hpp | A3 | B-4 | C-8 | D-P |
| GUI/MainFrame.cpp | | B-3 | C-3, C-4 | D-M |
| GUI/MainFrame.hpp | | B-2 | C-3 | D-M2 |
| GUI/BBLTopbar.hpp / .cpp | | | C-5, C-6 | D-BT |
| GUI/ReleaseNote.hpp / .cpp | | | C-9, C-10 | D-RN |
| GUI/Preferences.cpp | | | C-11 | D-R |
| GUI/PresetComboBoxes.cpp / .hpp | | B-10, B-11 | | |
| GUI/InstanceCheck.cpp, SingleChoiceDialog.cpp, UpdateDialogs.cpp | | B-7, B-8 | | |
| GUI/GUI_ObjectTable.cpp | | | C-7 | |
| GUI/DeviceManager.hpp (deleted in D) | | B-1 | | delete |
| GUI/SelectMachine.cpp, SendToPrinter.cpp (deleted in C) | | B-9 | delete | |
| GUI/WebGuideDialog.cpp / .hpp | | | | D-W |
| GUI/WebViewDialog.cpp / .hpp | | | | D-V |
| GUI/HttpServer.cpp / .hpp | | | | D-H |
| GUI/Tab.cpp | | | | D-T |
| GUI/ConfigRelateDialog.cpp | | | | D-CR |
| GUI/CreatePresetsDialog.cpp | | | | D-CP |
| GUI/NotificationManager.* | | | | D-N (opt.) |
| GUI/Widgets/ProgressBar.cpp | | B-13 (opt.) | | |
| src/slic3r/CMakeLists.txt | | §3.2 | §4.2 | §5.1b |
| src/CMakeLists.txt | | | | §5.1c (opt.) |
| localization/i18n/list.txt | | §3.3 | §4.2 | §5.1b |
| new: GUI/LegacyTransitiveIncludes.hpp, GUI/Widgets/AmsColors.hpp | | §1 | | |

**Not touched by F5:**
- the engine;
- `print_manage/**`, `simple/**` (Print Advisor, SlicerBridge);
- `calib_dlg.*` and `Plater::calib_*`;
- `CrProject.*`;
- `BBLStatusBar.*` and `BBLStatusBarSend.*`;
- `PrinterWebView.*`. It is Orca's print-host webview, not Bambu, and is dead after F2; leave it to a later clean-up.

## 9. Open questions

1. **`filament_ams_list` / `sync_ams_list`.** The brief says Creality CFS uses them, but no code in `print_manage/` writes the list (§0.2-6). Keep them as the brief asks, or delete them in D together with `PresetComboBoxes` slot recolouring (PresetComboBoxes.cpp:265-290, 424-450) and `PresetBundle::sync_ams_list`? The second option touches libslic3r.
2. **`resources/printers`.** Delete it with the `DataDirectoryMigration` guard (D-PR), or keep it (39 KB)?
3. **`media` wx component (§5.1c).** Drop it now or later? The risk is low, but it is a CMake package change.
4. **Optional clean-ups in D** (D-N plug-in notifications and `UpdatePluginDialog`, `refresh_plugin_tips`, encrypted-log cleanup). Do them now or in F6?
5. **`LegacyTransitiveIncludes.hpp`.** Prune it in a later dedicated pass (one include at a time, one build each)? The global `using namespace std;` will need many small fixes when removed. **[INFERENCE]** Dozens of TUs may use unqualified `string`/`vector`/`map`.
6. **`is_user_login()` / `is_compatibility_version()` stubs.** Keep them as constant-false functions (smallest diff, as planned here), or remove them and their callers? Removing them is about 25 more call-site edits across ConfigRelateDialog, Tab, Plater, WebGuideDialog, PrinterPresetConfig and WebViewDialog.

7. **`homepage_modeldepot`.** It opens Bambu's makerworld.com through `open_mall_page_dialog` and `get_model_http_url`. Remove the command branch (GUI_App.cpp:8067-8071) and both functions now, or with the F7 Home decision (Home kept or removed)?

## 10. Commit plan (one build per stage)

| Commit | Content | Build |
|---|---|---|
| `F5-A` | A1, A2, A3 | build A |
| `F5-B` | new headers (§1), B-1 to B-13, `git rm` of the 96 files, §3.2, §3.3 | build B |
| `F5-C` | `git rm` of the 22 files, C-1 to C-13, §4.2 | build C |
| `F5-D` | `git rm` of the 19 files, §5.2-§5.4, §5.1b | build D |
| `F5-D-opt` | D-N, D-PR, §5.1c, B-13 if skipped | optional extra build |

**Before each commit:**
- run the stage grep;
- confirm that CRLF was kept with `git diff --stat` plus `git diff | grep -c $'\r'` on touched files, or with `git diff --check`, which flags whitespace problems.

**After stage D:**
- run the full verification protocol of `doc/cleanup-plan-2026-10-20.md`;
- update `CLAUDE.md` ("Architettura" mentions SlicerBridge only; no change is needed for Bambu) and `doc/MCP/upstream-touch-points.md` if it lists any removed file.

**[INFERENCE]** The total build time is dominated by B, C and D, each a near-full `libslic3r_gui` rebuild (GUI_App.hpp/MainFrame.hpp change). It takes a few minutes per stage on this machine, per CLAUDE.md's "Build: cose da sapere".
