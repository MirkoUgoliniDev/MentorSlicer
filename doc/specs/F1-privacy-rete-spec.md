## F1 "Privacy and network": implementation spec

I only read files. Nothing was edited, built or run. Line numbers refer to HEAD `14528e0e` (branch `advisor`), and I checked them with `cat -n` or `grep -n`. Inside each file, apply the edits from the bottom up so the line numbers stay valid. Statements are verified against the code unless they are marked **[inference]**.

---

## 0. Corrections to the brief

1. **`filaments_color.json` is already in resources and tracked in git**: `resources/profiles/Creality/filament/filaments_color.json`, 65,626 bytes. Do not copy the data-dir version over it.
   - The data-dir copy at `%APPDATA%\Creality\Creality Print\7.3 Alpha\system\Creality\filament\filaments_color.json` is 62,577 bytes, dated 2026-10-20 08:38. It was downloaded from the Alpha dev endpoint.
2. **`materialList.json` is the only file to copy.**
   - Source: `%APPDATA%\Creality\Creality Print\7.3 Alpha\system\Creality\materialList.json`. It exists: 94,241 bytes, 2026-10-20 16:08, top-level key `"materials"`, so the format matches `MaterialListManager`.
   - Destination: `resources/profiles/Creality/materialList.json`.
   - The source was written by the running Alpha build, so it came from Creality's dev server (`api-dev`).
3. **The data-dir `machineList.json` is the reduced Alpha list.** It is 4,535 bytes and holds only 3 printers (`printerIntName` F021/K2, F012, F008), with no K1C. `resources/profiles/Creality/machineList.json` is 53,921 bytes and contains the K1C (`K1C_CFS-C`, `K1C 2025_CFS-C`). **Recommendation: read from resources only.**
4. **Today the Device page and the send dialog read only the data-dir list, with no resources fallback** (`PrinterMgrView::load_machine_preset_data`, `CxSentToPrinterDialog::load_machine_preset_data`). Switching to resources will change what the Device and send pages receive (see section 8).
5. **`WebUpdatePlugin.cpp` and `WebUpdatePlugin.hpp` are empty (0 lines)** and are not in CMake. Only `git rm` them and remove their `list.txt` lines.
6. **`EnableLiteModeDialog` is not about privacy.** It asks about `gcode_preview_lite_mode` (`GUI_App::check_user_lite_mode_dlg`, line 10680). **Keep it.**
7. **The G-code header writer is already gated by a runtime flag** (`PrintBase::m_write_task_id_placeholder`, default `false`). Item 1 therefore needs **no libslic3r change**.
8. **The CLI / F0 baseline already lacks the task-id line**: only `BackgroundSlicingProcess` sets the flag. Section 1.2 covers what this means for the G-code comparison.

---

## 1. Item 1: AnalyticsDataUploadManager becomes a no-op

### 1.1 What callers use outside the class (complete list from `git grep`)

**Types:**
- enums `AnalyticsUploadTiming` and `AnalyticsDataEventType`, all values;
- struct `AnalyticsEventPayload { type; data; }` (callers write `payload.data["entry"]`, so `data` must be `nlohmann::json`);
- nested `AnalyticsDataUploadManager::ModelModifyType`, all 18 values;
- nested class `AnalyticsDataUploadManager::ProjectModificationTracker`.

**`AnalyticsDataUploadManager` members used:**
- `getInstance`, `triggerUploadTasks`, `triggerUploadTasksWithPayload`
- `mark_analytics_project_info`, `set_analytics_project_info_valid`, `clear_analytics_project_info`
- `uploadSlice822ClickEvent` (static; `MainFrame.cpp:1180` uses the default `id`)
- `should_send_print_event`, `send_print_send_event`, `computeAndSetModelIdAsync`

**`ProjectModificationTracker` members used:**
- `getInstance`, `collect_params`, `collect_obj_params`, `mark_modified`, `reset`, `cache_slice_info`
- `generate_task_id` (both overloads), `replace_task_id_placeholder_in_gcode`
- `set_plate_task_id`, `get_plate_task_id`, `set_current_task_id`

**Never used outside the class:**
- `AnalyticsProjectInfo` (it appears only inside a log string in Plater);
- `test_sensors_connection`, `send_sensors_payload_to_creality`;
- the fingerprint getters (`computeModelFingerprint`, `getCachedFingerprint`, `clearFingerprintCache`);
- `is_essentially_modified`, the `get_*_info` getters, `get_current_task_id`, `set/get_current_plate_id`, `clear_plate_task_id`.

**Free functions in the .cpp** (`get_system_architecture`, `serialize_with_semicolon`, statics) have no outside users. `CrealityrintDump.cpp` has its own local lambda with the same name.

**Calling files (22):** BBLTopbar, BackgroundSlicingProcess, GLCanvas3D, GUI_App, GUI_ObjectList, Gizmos (Cut, Drill, Emboss, Hollow, MeshBoolean, PainterBase, Rotate (include only), Simplify, ObjectManipulation), MainFrame, Plater, Preferences, print_manage (PrinterMgrView, SendToPrinter, DeviceMgrRoutes).

### 1.2 Edits

**A. Replace all of `src/slic3r/GUI/AnalyticsDataUploadManager.hpp` (480 lines) with a header-only stub.**
- Keep the same include list: `<functional> <mutex> <condition_variable> <unordered_map> <map> <future> <string> <vector> <memory>`, `"nlohmann/json.hpp"`, `"libslic3r/PrintConfig.hpp"`. Some callers depend on these transitively (Plater 22095 uses `std::chrono`).

```cpp
#ifndef ANALYTICS_DATA_UPLOAD_MANAGER_HPP
#define ANALYTICS_DATA_UPLOAD_MANAGER_HPP
// Fork: analytics removed. Same API as before; nothing is collected or sent.
#include <functional>
#include <mutex>
#include <condition_variable>
#include <unordered_map>
#include <map>
#include <future>
#include <string>
#include <vector>
#include <memory>
#include "nlohmann/json.hpp"
#include "libslic3r/PrintConfig.hpp"
namespace Slic3r { namespace GUI {
class PartPlate;
enum class AnalyticsUploadTiming { /* copy the 7 values verbatim from the old header lines 21-29 */ };
enum class AnalyticsDataEventType { /* copy all values verbatim from lines 32-113 */ };
struct AnalyticsEventPayload { AnalyticsDataEventType type; nlohmann::json data; };
class AnalyticsDataUploadManager {
public:
    static AnalyticsDataUploadManager& getInstance() { static AnalyticsDataUploadManager instance; return instance; }
    void triggerUploadTasks(AnalyticsUploadTiming, const std::vector<AnalyticsDataEventType>&, int = 0, const std::string& = "") {}
    void triggerUploadTasksWithPayload(const AnalyticsEventPayload&, int = 0, const std::string& = "") {}
    void mark_analytics_project_info(const std::string&, const std::string&, const std::string&, const std::string&, const std::string&) {}
    void set_analytics_project_info_valid(bool) {}
    void clear_analytics_project_info() {}
    static void uploadSlice822ClickEvent(const std::string&, int = 1) {}
    void send_print_send_event(int) {}
    bool should_send_print_event() const { return false; }
    void computeAndSetModelIdAsync(const std::string&, const std::string&, const std::string&, const std::string&) {}
    enum class ModelModifyType { REPAIR, SIMPLIFY, HOLLOW, ADD_HOLE, CUT, BOOLEAN, EMBOSS, VARIABLE_LAYER,
        SUPPORT_PAINT, ZSEAM_PAINT, SPLIT_OBJECTS, SPLIT_PARTS, ADD_PART, DELETE_PART, HEIGHT_RANGE,
        ADD_NEGATIVE_VOLUME, ADD_SUPPORT_BLOCKER, ADD_SUPPORT_ENFORCER };
    class ProjectModificationTracker {
    public:
        static ProjectModificationTracker& getInstance() { static ProjectModificationTracker instance; return instance; }
        static nlohmann::json collect_params(const DynamicPrintConfig&) { return nlohmann::json::object(); }
        static nlohmann::json collect_obj_params(PartPlate*, int) { return nlohmann::json::object(); }
        void mark_modified(ModelModifyType) {}
        void reset() {}
        void cache_slice_info(int, const std::string&, const std::string&, const std::string&) {}
        static std::string generate_task_id(int) { return {}; }
        static std::string generate_task_id(int, long long) { return {}; }
        static bool replace_task_id_placeholder_in_gcode(const std::string&, const std::string&) { return false; }
        void set_plate_task_id(int, const std::string&) {}
        std::string get_plate_task_id(int) { return {}; }
        void set_current_task_id(const std::string&) {}
    };
private:
    AnalyticsDataUploadManager() = default;
    AnalyticsDataUploadManager(const AnalyticsDataUploadManager&) = delete;
    AnalyticsDataUploadManager& operator=(const AnalyticsDataUploadManager&) = delete;
};
}} // namespace Slic3r::GUI
#endif
```

**B. Delete the implementation and its build entries.**
- `git rm src/slic3r/GUI/AnalyticsDataUploadManager.cpp` (2,722 lines).
- In `src/slic3r/CMakeLists.txt`, delete line 796 `GUI/AnalyticsDataUploadManager.cpp` and keep line 795 (`.hpp`).
- In `localization/i18n/list.txt`, delete line 768 (`.cpp`). Line 490 (`.hpp`) can stay; the stub has no strings.

**C. G-code placeholder path (no libslic3r edits).**
- `BackgroundSlicingProcess.cpp:299-301` calls `m_fff_print->set_write_task_id_placeholder(AnalyticsDataUploadManager::getInstance().should_send_print_event())`. With the stub it always passes `false`. Leave this line unchanged, or optionally replace it with `set_write_task_id_placeholder(false)`.
- `src/libslic3r/GCode.cpp:5349-5368` writes `; creality_task_id_pending: <56 zeros>_<13 zeros>_<NN>` only when the flag is true. That never happens now.
- `GCode.cpp:5370-5374` writes `; creality_task_id:` only when `print.get_creality_task_id()` is not empty. `Print::set_creality_task_id` (`Print.hpp:1100`) has **no callers**, so it never fires.
- Callers of `replace_task_id_placeholder_in_gcode` are `Plater.cpp:14531` (export G-code), `Plater.cpp:22106` (export 3MF with G-code), `PrinterMgrView.cpp:1114` (multi-device send) and `SendToPrinter.cpp:1283` (single send). With the stub they return `false` without opening the file. The `release_gcode_file_mapping()` calls just before them stay, so behaviour there is unchanged.
- **Exact G-code change:**
  - In the GUI slicing output, one header line disappears, just before `; HEADER_BLOCK_END` and after `; creality_uuid: …`: `; creality_task_id_pending: 00000000000000000000000000000000000000000000000000000000_0000000000000_NN` (101 bytes with the newline).
  - Exported or sent files no longer contain `; creality_task_id: <SystemId>_<ms>_<NN><spaces>`.
  - Nothing else changes.
  - The **CLI output is unchanged**: the flag defaults to false and only `BackgroundSlicingProcess` sets it.
  - `; creality_uuid:` is random on every run (`GCode.cpp:5343-5347`), so any G-code diff must mask that line.

**D. Launch, OTA and crash analytics in `GUI_App`.**
- Delete `GUI_App::schedule_software_launch_analytics` (`GUI_App.cpp:888-977`, from `void GUI_App::schedule_software_launch_analytics()` to the closing brace before `void VersionInfo::parse_version_str`).
- Delete its call at `GUI_App.cpp:8023` (inside `handle_web_request`).
- In `GUI_App.hpp`, delete line 339 (declaration) and line 328 (`m_app_launch_initialized`).
- `check_app_first_launch_info` then has no caller. Optionally delete `check_app_first_launch_info` (13988-14005), `save_app_first_launch_info` (14007-14025, its other caller is the deleted privacy dialog) and their declarations at `GUI_App.hpp:871-872`. Keep `m_app_first_launch_data` and `get_app_launch_info`; they become dead but compile.
- `GUI_App.cpp:4332-4339` (crash path) deletes cleanly. Keep `m_send_crash_report`, the ErrorReportDialog flow and the `software_crash` AppConfig key (its writer is line 4333, delete it with the block; leave `libslic3r/AppConfig.cpp` untouched).
- The other OTA analytics sites (5113-5120, 10434-10440) disappear with item 2.

**E. Recommended: Google Analytics bridge.**
- `GUI_App::track_event` (5347-5371) forwards to `MainFrame::trackEvent`, which runs `window.handleStudioCmd({command:"track"})` in the Home webview. The homepage bundle `resources/web/homepage/assets/index-806b308b.js` contains GA/Firebase code.
- Replace the body with `(void)event; (void)data;`. Callers stay the same (Plater, MainFrame, SendToPrinter, DeviceMgrRoutes, CreatePresetsDialog).
- **[inference]** The web bundles (homepage, deviceMgr, sendToPrinterPage all reference GA) may still call GA on their own clicks. That needs a web patch and is outside F1.

**F. Optional cleanup in the same batch.** All other call sites listed in 1.1 compile unchanged against the stub. Removing them is mechanical (delete the `AnalyticsEventPayload` blocks and `mark_modified` calls). The plan schedules that file by file; doing it in F1 is not required.

---

## 2. Item 2: remove the update check

### 2.1 Delete these files (`git rm`)

| Files | Build / list.txt entries to remove |
|---|---|
| `src/slic3r/GUI/AppUpdater.{cpp,hpp}` | `src/slic3r/CMakeLists.txt:249-250`; list.txt 294, 737 |
| `src/slic3r/GUI/AppUpdateDialogs.{cpp,hpp}` | CMake 251-252; list.txt 493, 551 |
| `src/slic3r/GUI/WebUpdatePlugin.{cpp,hpp}` (empty) | not in CMake; list.txt 42, 86 |
| `src/slic3r/GUI/simple/printAdvisor/PrintAdvisorUpdateNotice.{cpp,hpp}` | `PrintAdvisor.cmake:20-21`; list.txt 800 |
| `tools/updater/` (main.go, updater.exe, …) | `src/CMakeLists.txt:668` `install(FILES ${CMAKE_SOURCE_DIR}/tools/updater/updater.exe DESTINATION ".")` |
| `resources/tooltip/releasenote.html` | only used by `UpdateVersionDialog` (`ReleaseNote.cpp:367/370`) |

- Optional: `tools/update_server/` (local dev update server, only referenced by `check_new_version_local`).
- `JenkinsPipeline:224-233` signs `updater.exe`. That is Creality CI; leave it for F6.

**Other users of these symbols** (`grep`):
- `AppUpdater` / `AppUpdateDialogs` / `PackageInfo` / `EVT_APP_UPDATE_*`: only in `GUI_App.cpp`, `GUI_App.hpp:103,367` and `ReleaseNote.cpp:413-419`.
- `prepare_close_for_update`: only `AppUpdater.cpp:800,814`.
- `advisor_show_fork_update_notice`: only `GUI_App.cpp:4777`.

### 2.2 `src/slic3r/GUI/GUI_App.cpp`

| Lines | What to do |
|---|---|
| 13608-13628 | delete `GUI_App::prepare_close_for_update()` with its comment |
| 10926-10930 | delete `GUI_App::no_new_version()` |
| 9627-9839 | delete `check_update` and `check_new_version_local` |
| 9841-9851 | delete `trigger_deferred_restore_project` with its comment |
| 9853-10589 | delete `check_new_version`; the free `Semver get_version(...)` (9920-9931, used only by the deleted code); `check_new_version_cx`; `check_new_version_cx_updated` (**includes the request-header dump to the log at 10064-10067**); `process_update_packages`; `check_new_version_sf`; `request_new_version`; `enter_force_upgrade`; `set_skip_version`. Stop at the line before `void GUI_App::show_check_privacy_dlg` (10591). |
| 5047-5150 | delete the `mainframe->Bind(EVT_APP_UPDATE_PROGRESS/COMPLETE/CANCELED/ERROR …)` blocks (from line 5047 up to the `});` before `//UITour::Instance();` at 5151) |
| 4766-4966 | delete the whole `#if !AUTO_CONVERT_3MF … #endif`: the `EVT_SLIC3R_VERSION_ONLINE` handler (with the PRINT-ADVISOR fork-update hook at 4774-4778), `EVT_ENTER_FORCE_UPGRADE` and `EVT_SHOW_NO_NEW_VERSION`. `Bind(EVT_SHOW_DIALOG…)` at 4967 stays. |
| 4487-4488 | delete `cleanup_partial_downloads` (startup) |
| 4384-4385 | delete `cleanup_partial_downloads` (`OnExit`) |
| 2588-2589 | delete `wxDEFINE_EVENT(EVT_ENTER_FORCE_UPGRADE…)` and `wxDEFINE_EVENT(EVT_SHOW_NO_NEW_VERSION…)` (no other users) |
| 2652-2675 | delete `can_show_satisfaction_survey` (item 5; it reads `g_update_*`) |
| 2427-2433 | delete the `if (this->preset_updater->version_check_enabled() && …) { m_startup_update_check_running.store(true…); }` block. Keep the `CallAfter` and its `preset_updater->sync(...)` call (see 2.4). |
| 2347-2355 | replace with `plater_->trigger_restore_project(1);` (the pending-update and survey deferral go away) |
| 605-733 | delete `show_update_ready_dialog_guarded` and all `update_state.json` helpers (`get_update_state_path`, `write_/read_/clear_update_state`, `set_pending_app_update`, `set_install_attempted`, `clear_pending_app_update`, `get_pending_app_update_version`, `get_install_attempted_version`, `clear_install_attempted`) |
| 262-263 | delete `g_update_progress_dlg` and `g_update_ready_dialog_showing` |
| 174-175 | delete `#include "AppUpdater.hpp"` and `#include "AppUpdateDialogs.hpp"` |
| 42 | delete `#include ".../PrintAdvisorUpdateNotice.hpp"` |

### 2.3 `GUI_App.hpp`, `MainFrame.cpp`, `ReleaseNote.*`

**`GUI_App.hpp` deletions:**
- 685 and its comment at 684 (`prepare_close_for_update`)
- 559-575 (`check_update` … `no_new_version`, including `trigger_deferred_restore_project` and `process_update_packages`)
- 366-373 (`m_update_packages`, `m_update_version`, `m_update_base_package_name`, `m_update_file_url`, `m_update_force_full`)
- 283-286 (`m_restore_project_deferred`)
- 279 (`m_startup_update_check_running`)
- 103 (`struct PackageInfo;`)
- Keep `VersionInfo` and the `version_info` member. They become dead but are harmless.

**`MainFrame.cpp`, Help menu:** delete 2910-2928 (`#if !defined(CUSTOMIZED) || defined(CUSTOM_CHECKUPDATE_ENABLED)` … "Check for Update" … `helpMenu->AppendSeparator(); #endif`).
- Re-adding one `helpMenu->AppendSeparator();` is optional. The separator after "Show Configuration Folder" is at 2895.

**`ReleaseNote.cpp`:** delete 267-704 (`CustomScrolledWindow` and all of `UpdateVersionDialog`).
- Keep `ReleaseNoteDialog`, `UpdatePluginDialog` (used by `Plater.cpp:14892`), `SecondaryCheckDialog`, `PrintErrorDialog`, `ConfirmBeforeSendDialog` and `InputIpAddressDialog`.
- `UpdateVersionDialog` and `CustomScrolledWindow` have no other users.

**`ReleaseNote.hpp`:** delete 84-135 (`CustomScrolledWindow` and `UpdateVersionDialog`).

### 2.4 `src/slic3r/Utils/PresetUpdater.{cpp,hpp}`: remove the network part, keep `config_update` and the bundle install

**`.cpp`, bottom-up:**
- 1636-1639: delete `version_check_enabled()`.
- 1463-1467: delete `slic3r_update_notify()`.
- 1401-1461: replace `PresetUpdater::sync` with:

```cpp
// Fork: no network sync (version check, online parameter packages, plugins, machine list).
// Only the check of locally cached config updates is kept, as before on the GUI thread.
void PresetUpdater::sync(std::string /*http_url*/, std::string /*language*/, std::string /*plugin_version*/, PresetBundle *preset_bundle)
{
    if (GUI::wxGetApp().app_config->get("is_first_install") != "1")
        return;
    if (preset_bundle == nullptr || preset_bundle->vendors.empty())
        return;
    GUI::wxGetApp().CallAfter([] { GUI::wxGetApp().check_config_updates_from_updater(); });
}
```
  - The old body started a thread that ran `sync_version` (which calls `check_new_version_cx`), `UpdateParams::checkParamsNeedUpdate`, `CommunicateWithCXCloud` (only when logged in), `check_machine_list` and `sync_plugins`.
  - `prune_tmps` is dropped on purpose: it throws if `<data_dir>/ota` is missing, and with no downloads there are no tmp files.
- 380-1075: delete `sync_version`, `parse_version_string`, `sync_resources`, `sync_config`, `sync_tooltip`, `get_cached_plugins_version`, `sync_plugins` and `sync_printer_config`.
  - Stop before `install_bundles_rsrc` at 1077. Keep `prune_tmps` (370-378).
- 271-368: delete `get_file` and `extract_file`. Their only callers are inside the deleted range (lines 592, 615, 741, 747).
- 262: delete `version_check_url = app_config->version_check_url();`.
- 250: delete `enabled_version_check = true;`.
- 220, 222, 224-231: delete the matching `priv` declarations.
- 191, 193: delete `enabled_version_check` and `version_check_url`.
- 183-184: delete `wxDEFINE_EVENT(EVT_SLIC3R_VERSION_ONLINE / EVT_SLIC3R_EXPERIMENTAL_VERSION_ONLINE…)`.
- 42: delete `#include "slic3r/GUI/UpdateParams.hpp"` (the file is deleted in item 3).
- 43: delete `#include "slic3r/GUI/CommunicateWithCXCloud.hpp"` (optional, now unused).
- Kept: `check_installed_vendor_profiles`, `get_printer_config_updates`, `get_config_updates`, `perform_updates`, `config_update`, `install_bundles_rsrc`, `on_update_notification_confirm`, `do_printer_config_update`.

**`.hpp`:** delete 69-70 (event declarations), 62 (`version_check_enabled`) and 31-32 (`slic3r_update_notify`).

`UpdateDialogs.*` is untouched (`MsgUpdateSlic3r`, `MsgUpdateConfig`, `MsgNoUpdates` are still used by `config_update` and `check_updates`).

### 2.5 Docs to update (not code)
- `CLAUDE.md`, paragraph "Aggiornamenti ufficiali".
- `doc/MCP/upstream-touch-points.md` (mentions `PrintAdvisorUpdateNotice`).

---


## 3. Item 3: catalogues read from disk

### 3.1 Add one data file
Copy `%APPDATA%\Creality\Creality Print\7.3 Alpha\system\Creality\materialList.json` (94,241 bytes, top-level key `"materials"`) to `resources/profiles/Creality/materialList.json` and `git add` it. Do not touch `resources/profiles/Creality/filament/filaments_color.json`, which is already bundled (65,626 bytes).

### 3.2 Delete these files

| Files | Build / list.txt entries to remove |
|---|---|
| `src/slic3r/GUI/MachineListDownloader.{cpp,hpp}` | CMake 245-246; not in list.txt |
| `src/slic3r/GUI/MaterialListDownloader.{cpp,hpp}` | CMake 243-244; list.txt 280, 521 |
| `src/slic3r/GUI/UpdateParams.{cpp,hpp}` | CMake 783-784; list.txt 506, 568 |

### 3.3 `GUI_App.cpp`, bottom-up

**a. `GUI_App::load_machine_preset_data` (14359-14441): replace the body.**

```cpp
int GUI_App::load_machine_preset_data()
{
    // Fork: the printer catalogue is read only from the bundled resources, never downloaded.
    const std::string vendor_preset_path = (boost::filesystem::path(Slic3r::resources_dir()) / PRESET_PROFILES_DIR /
                                            "Creality" / "machineList.json").make_preferred().string();
    if (!boost::filesystem::exists(vendor_preset_path)) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": missing " << vendor_preset_path;
        return -1;
    }
    try {
        std::string contents;
        LoadFile(vendor_preset_path, contents);
        json jLocal = json::parse(contents);
        nlohmann::json commandJson;
        commandJson["command"] = "get_machine_list";
        commandJson["data"]    = jLocal["printerList"];
        wxString strJS = wxString::Format("handleStudioCmd(%s)", commandJson.dump(-1, ' ', true));
        wxGetApp().CallAfter([this, strJS] { run_script(strJS.ToStdString()); });
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": parse " << vendor_preset_path << " failed: " << e.what();
        return -1;
    }
    return 0;
}
```

**b. `handle_web_request`:** delete 9382-9383 (`if (previous_region != region) wxGetApp().refresh_machine_list_for_region();`) and `previous_region` (9376), or leave it unused (warning only).

**c. `update_preset_params` handler** (online printer-package download from `file-cdn.creality.com`, plus re-download of materialList and machineList): keep line 8662 and replace 8663-8819 with:

```cpp
BOOST_LOG_TRIVIAL(info) << "update_preset_params ignored: online profile download is disabled";
return "";
```

The static helpers at 6908-7100 and `UpdateParamPackage` (7207) become unused but compile (MSVC C4505 is level 4, no `/WX`). Delete them in a later phase.

**d. `prepare_device_add`, line 7914:** replace the `system_catalog` load from `data_dir()/system/Creality/machineList.json` with `const json system_catalog;`. The bundled catalogue at 7915 becomes the only source; `DeviceAddUtils::resolve_printer_selections` already falls back from system to bundled.

**e.** Delete 6696-6905 (`start_async_material_list_update`, `refresh_machine_list_for_region`, `check_machine_list`). Keep `std::mutex mtx;` at 6906.

**f. Startup:** at 5203-5204 delete `start_async_material_list_update();` and `OfficialMaterialColors::refresh_async();`. Keep `OfficialMaterialColors::initialize_from_resources()` at 4476.

**g. Statics and includes:** delete 264-265 (`g_async_material_list_update_running`, `g_async_machine_list_refresh_running`; keep `g_app_exiting`), 247 (`#include "UpdateParams.hpp"`), 125-126 (downloader includes).

**`GUI_App.hpp`:** delete 741-748 (`check_machine_list`, `start_async_material_list_update`, comment, `refresh_machine_list_for_region`).

### 3.4 Other callers

| Location | Change |
|---|---|
| `GUI/InstanceCheck.cpp:518-521` | delete the comment and `refresh_machine_list_for_region()` |
| `GUI/Preferences.cpp:362-365` | delete the comment and the call |
| `GUI/WebGuideDialog.cpp:1409-1411` | delete the `#if CUSTOM_CXCLOUD UpdateParams…checkParamsNeedUpdate(); #endif` |
| `GUI/WebGuideDialog.cpp:230-234` | delete the `#if CUSTOM_CXCLOUD #if UPDATE_ONLINE_MACHINES check_machine_list(); #endif #endif` |
| `GUI/WebGuideDialog.cpp:42` | delete `#include "UpdateParams.hpp"` |
| `GUI/MainFrame.cpp:1488-1490` | delete `UpdateParams::getInstance().closeParamsUpdateTip()` block |
| `GUI/MainFrame.cpp:96` | delete the include |
| `GUI/Plater.cpp:7666` | delete `UpdateParams::getInstance().checkParamsNeedUpdate();` |
| `GUI/Plater.cpp:223` | delete the include (the nested `Plater::priv::UpdateParams` enum is unaffected) |
| `print_manage/App/PrinterMgrView.cpp:2815-2893` | same body as 3.3a, keeping this file's JS format: `wxString::Format("window.handleStudioCmd('%s');", RemotePrint::Utils::url_encode(commandJson.dump(-1, ' ', true)))` and a direct `run_script(...)` |
| `print_manage/App/SendToPrinter.cpp:2601-2677` | same as PrinterMgrView, same JS format |
| `Utils/ProfileFamilyLoader.cpp:245-259` | `machine_list_path()` returns `resources_dir()/profiles/<vendor>/machineList.json` |
| `GUI/OfficialMaterialColorCache.hpp` | delete `convert` (178-244), `api_base_url` (246-249), `refresh` (251-331), `refresh_async` (332-345); keep all includes and `initialize_from_resources` |
| `libslic3r/MaterialListManager.cpp:31-34` | optional, .cpp only: return `resources_dir()/profiles/Creality/materialList.json`. Alternative with zero libslic3r edits: leave unchanged (the data-dir file has the same content, and `install_bundles_rsrc` copies resources on a fresh install or version bump) |

### 3.5 Load order
- **machineList: resources only** in all five readers (`GUI_App::load_machine_preset_data`, `PrinterMgrView`, `SendToPrinter`, `ProfileFamilyLoader::machine_list_path`, `prepare_device_add`). The data-dir copy is the reduced Alpha list (3 printers, no K1C). `LoadOldPresets.cpp:244` still reads the data-dir file for the legacy 5.x import; leave it.
- **materialList: resources** (or the alternative above).
- **filaments_color: keep the existing mechanism.** To switch to the bundled file, delete `<data_dir>/filaments_color_initialized_version.txt` once by hand.

### 3.6 Printer covers still contact the Creality CDN
`Utils/PrinterCover.cpp:117` (`sync_printer_cover`) downloads from `pic-cdn.creality.com`; `ProfileFamilyLoader.cpp:366-376` calls it at startup for installed models whose cover is missing. Recommended: return `false` before the download block (after the cached check, around line 105). Residual: the `thumbnail` URLs in `machineList.json` are passed to the Device page and probably loaded by the webview directly (open question).

---

## 4. Item 4: global HTTP headers, MAC address, header dump, `send_http_request`

| Location | Change |
|---|---|
| `Utils/Http.cpp:941-945` | body of `Http::set_extra_headers` becomes `(void)headers;` with a comment: global headers removed because they leaked the machine id and the cloud token into every request, LAN uploads included. `Http.hpp` unchanged; ~40 callers compile unchanged |
| `GUI_App.cpp:4996-4997` | delete the startup `get_extra_header()` + `set_extra_headers` |
| `GUI_App.cpp:3858` | remove `{"__CXY_DUID_", GetMACAddress()}` from `get_extra_header` |
| `GUI_App.cpp:3954-…` | `GUI_App::get_client_id()` returns `{}` |
| `GUI_App.cpp:3692-3758` | delete the three `GetMACAddress()` definitions |
| `GUI_App.cpp:10064-10067` | the header dump to the log goes away with `check_new_version_cx_updated` (item 2) |
| `GUI_App.cpp:8611-8661` (`send_http_request`) | keep 8610; body becomes a warning log and `return "";` (only the homepage bundle sends this command) |

Remaining for later phases: `__CXY_UID_`/`__CXY_TOKEN_` (empty without login, F4); `get_modellibrary_header` sends `SystemId::get_system_id()` (F3).

---

## 5. Item 5: survey, feedback, Creality privacy dialog

### 5.1 Delete
| Files | Build / list.txt |
|---|---|
| `SatisfactionSurveyManager`, `SatisfactionSurveyDialog`, `SatisfactionSurveyIntegration` (.cpp/.hpp) | CMake 237-242; list.txt 74, 277, 333, 404, 436, 561 |
| `PrivacyUpdateDialog.{cpp,hpp}` | CMake 607-608; list.txt 471, 510 |
| `resources/tooltip/privacyupdate.html` | – |
| `resources/images/user_feedback{,_disable,_hover,_light}.svg` | – |

Keep `EnableLiteModeDialog` (it is about G-code preview lite mode, not privacy).

### 5.2 `GUI_App.cpp` (bottom-up)
Delete: `check_creality_privacy_version` (10838-10924), `check_privacy_version` (10788-10836), `on_check_privacy_update` (10778-10786), `check_privacy_update` (10699-10715), `on_show_check_privacy_dlg` (10692-10697), `save_privacy_version` (10670-10678; keep `check_user_lite_mode_dlg`), `show_check_privacy_dlg` (10591-10668). Line 9602: `check_privacy_version(online_login)` becomes `request_user_handle(online_login)`. Delete the binds at 4990-4991, the call at 4633-4637 (keep `if (isdump_launcher) return true;`), 4066, the survey teardown 3375-3376, 2644-2646, 2652-2675, 2540-2543, 2349-2350, 2153-2157, includes 189 and 176-177.

### 5.3 `GUI_App.hpp`
Delete 831, 881, 599-603, 605-606 (keep 604 and 608), 392-393, 306, 278, 100. Keep `is_privacy_checked()` (now always false).

### 5.4 Other files
| Location | Change |
|---|---|
| `WebViewDialog.cpp:277` | delete `check_creality_privacy_version(false)` |
| `UITour.cpp:614` | delete `check_creality_privacy_version()` |
| `MainFrame.cpp:125-126`, `MainFrame.hpp:464-465` | delete `EVT_CHECK_PRIVACY_*` |
| `MainFrame.cpp:2961-2969` | delete Help "User Feedback" |
| `Preferences.cpp:1570-1571`, `1490-1491`, `854-865`, `789-841` | delete the "User Experience Program" item and its handlers |
| `Plater.cpp:7628-7634`, `116` | delete `SurveyRestoreCompletionGuard` and include |
| `print_manage/App/PrinterMgrView.cpp:1199`, `1143-1144`, `56` | delete survey notify calls and include |
| `simple/sendWorkflow/EasyPrintSender.cpp:702-703`, `17` | delete survey lines and include |
| `GUI.cpp:760-…`, `GUI.hpp:85` | delete `user_feedback_website()` |

### 5.5 Topbar feedback button (`BBLTopbar`)
Delete 1839-1851, 1737-1738 and 1742-1745, 1682-1683, 1590-1600 (`OnFeedback`), 1191 (Bind), 1116-1125 (AddTool); `.hpp` 51 and 142. Keep `m_feedback_separator_item` (used by the responsive layout). This sub-step may move to F2.

### 5.6 Out of scope
`CrealityrintDump.cpp::ErrorReportDialog::sendReport` emails crash dumps only after the user clicks OK; not covered by F1.

---

## 6. Item 6: GitHub automation
`git rm` all `.github/workflows/` (push-triggered: build_all, check_locale, check_profiles, sync_doc, upload_doc; workflow_call: build_check_cache, build_creality_print, build_deps; daily cron: creality_bot, schedule_bot) and `.github/dependabot.yml`. Optional: issue and PR templates. Also disable Actions in the fork settings.

---

## 7. Compile risks
1. Stub header must keep the same transitive includes; ~22 GUI files recompile, libslic3r untouched.
2. Large `GUI_App.cpp` deletions: apply bottom-up, then `git grep` for: AppUpdater, PackageInfo, g_update_, m_restore_project_deferred, m_startup_update_check_running, get_pending_app_update_version, trigger_deferred_restore_project, version_check_enabled, check_new_version, satisfaction, privacy_version, save_privacy_version, get_privacy_data, PrivacyUpdateDialog, EVT_CHECK_PRIVACY, UpdateParams::getInstance, check_machine_list, refresh_machine_list_for_region, start_async_material_list_update, MachineListDownloader, MaterialListDownloader, refresh_async, GetMACAddress, user_feedback_website, UpdateVersionDialog, CustomScrolledWindow, advisor_show_fork_update_notice, prepare_close_for_update.
3. `Plater.cpp`: nested `UpdateParams` enum still resolves.
4. `PresetUpdater`: unused `priv` members compile.
5. `list.txt` must not list deleted files.
6. CMake changes: `build_exe_only.bat` reconfigures; one build.
7. libslic3r: at most `MaterialListManager.cpp`; no header changes.

## 8. Behaviour changes to check
1. G-code: only the task-id header line disappears (GUI slices, exports, LAN uploads). Check that a LAN print still starts.
2. LAN send: no placeholder rewrite, no global CXY headers, no survey hooks.
3. Device page and send dialog receive the full resources `machineList` instead of the Alpha list: verify the K1C card, camera, send preselection and compatibility message.
4. "Add printer" tree shows the full catalogue (and may download covers unless 3.6 is applied).
5. Online printer-parameter packages disabled (K1C profile already installed).
6. Material aliases unchanged if the copied `materialList.json` is used.
7. Startup: no privacy dialog; restore-project prompt immediate.

## 9. Threads and lifecycle
Removed detached threads: material-list refresh, machine-list refresh, color refresh, `PresetUpdater::sync` worker. `PresetUpdater::sync` now only queues `check_config_updates_from_updater` on the GUI thread. Print Advisor shutdown unaffected.

## 10. Verification
1. `build_exe_only.bat`.
2. `git grep` of section 7.2 names; `list.txt` clean; `.mo` step passes.
3. G-code: CLI output of the 4 F0 projects byte-identical after masking `; creality_uuid:`; GUI export differs only by the missing task-id line.
4. Sidecar typecheck + smoke; `apply-web-patches.mjs --check`.
5. Manual: no Check for Update or User Feedback in Help; no topbar feedback; Preferences without User Experience Program; Device page with K1C, telemetry and camera; send dialog compatibility; Print Advisor works; clean exit.
6. Network: 5 minutes with Resource Monitor; expected residuals: `pic-cdn.creality.com` thumbnails from web pages, GA calls from web-bundle JS, homepage fetches.
7. Printer (idle only): LAN upload and print start with the new G-code.

## 11. Open questions
1. Drop `thumbnail` URLs from the Device and send pages (no CDN contact, generic images)?
2. Include 3.6 (block printer-cover downloads) in F1?
3. `MaterialListManager`: .cpp-only path change, or unchanged?
4. Topbar feedback button in F1 or F2?
5. Delete `.github/ISSUE_TEMPLATE` and the PR template too?

## Suggested commit order (one build at the end)
1. Workflows. 2. Analytics stub + `track_event` no-op. 3. Update check (ReleaseNote, PresetUpdater, Advisor notice, tools/updater). 4. Local catalogues (data file, downloaders, UpdateParams, readers, cover guard). 5. Headers, MAC, `send_http_request`. 6. Survey, privacy, feedback. 7. `list.txt` and docs.
