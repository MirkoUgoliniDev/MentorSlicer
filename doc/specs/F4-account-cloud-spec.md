## F4 "Account and cloud": implementation spec

I only read files. Nothing was edited, built or run (a print was in progress). Line numbers refer to HEAD `14528e0e` (branch `advisor`) and were checked with `awk`/`grep -n`. F1, F2 and F3 are applied before F4, so many numbers will have shifted: every edit gives the HEAD line **and** an anchor text; locate by anchor, and inside each file apply the edits from the bottom up. Source files are CRLF (PrinterMgrView.cpp also has a UTF-8 BOM): keep both. Statements are verified against the code unless marked **[inference]**.

---

## 0. Corrections to the brief and findings

1. **OSS and Paho are linked in `src/CMakeLists.txt:145-163`**, not in the root `CMakeLists.txt` (root 146-163 is the `IS_CROSS_COMPILE`/options block). They are appended to `wxWidgets_LIBRARIES`.
2. **`DeviceVideoDownloader.{cpp,hpp}` must go too.** It includes `TimeLapseShareTypes.hpp` and its only caller is `TimeLapseShareManager.cpp:356`. Deleting only the share manager would leave a dangling include.
3. **`UserManager.*` stays for F5.** It is created by `on_init_network` (`GUI_App.cpp:5513-5516, 5559-5560`) and deleted in `OnExit` (4395-4398), all Bambu agent code. Nothing in it is Creality-account specific.
4. **`CxCloudPrintClient/Executor` belong to F3** (plan F3: "sendWorkflow (la parte cloud)"); `EasyPrintSender.cpp:2,30` and `.hpp:121` use them. F4 only checks that they are gone (section 1).
5. **`CloudDownloadProgressDialog`, `ModelDownloader`, `3mf_download_*`/`models_download_*` are online-model code (F3)**, not account code. Not touched here.
6. **`HttpServer.cpp` has a second login route**, `bbl_auth_handle_request` (1008-1065, Bambu). It is never installed (`m_request_handler` defaults to `creality_handle_request`, `HttpServer.hpp:128`), but it calls `ShowUserLogin` and `request_user_login`, so it must be deleted in F4 or the build breaks. `ResponseRedirect` stays: `Jobs/OAuthJob.cpp:48-86` (SimplyPrint) uses it.
7. **`AccountDeviceMgr` already does nothing without login.** `unbind_device*`, `add_to_my_devices`, `get_account_device_bind_json_info` and `get_account_device_info_for_printers_init` return at once when `!is_login()`. Only `load()` (reads `user/default/local_device/account_device_info.json`) and `save_to_file()` run. Device groups live in `DM::DeviceMgr`, not here. So `sync_to_cloud` is **deleted**, not stubbed.
8. **`tests/preset_sync` stays.** It tests `libslic3r/PresetSyncUtils.hpp`, which `Preset.cpp` uses for local preset filenames and loading (1377, 2002, 2091, 3978). It is a standalone project, not in `tests/CMakeLists.txt`. Same for `tests/libslic3r/test_cloud_preset_serialization.cpp`.
9. **`resources/web/login` has no code reference** (only `WebUserLoginDialog.cpp:467` loads `web\login\error.html`). 9 files, 164 KB.
10. **`PrinterCloudAuthDialog` is not a Creality account dialog.** It is the Orca web login for print hosts whose `is_cloud()` is true and that are not SimplyPrint: today only **Obico** (`Utils/Obico.hpp:25`). Deleting it, as the brief asks, removes the Obico "Login/Test" flow; see decision D1.
11. **`GUI_App::request_user_unbind` is called by `BindDialog.cpp:1102` (Bambu, F5).** F4 patches that one call.
12. **`KlipperCXInterface.hpp` leaks headers into about 10 files.** It includes `UploadFile.hpp`, which pulls `alibabacloud/oss/OssClient.h`, `nlohmann/json.hpp` and a global `using namespace nlohmann;` into everything that includes `RemotePrinterManager.hpp`. Checked: all those files get `nlohmann` from `GUI_App.hpp` (via `libslic3r/AppConfig.hpp:14`) or include it themselves; section 8 still adds `<iostream>` and `<sstream>` defensively.
13. **The send page calls Creality Cloud even when logged out.** In `sendToPrinterPage/assets/BDAlyC64.js` the `get_user` handler runs `a.setToken(v||""),a.checkTokenIsValid()`, which posts to `https://api-dev.crealitycloud.cn/api/rest/print/cluster/devices/getDeviceCount` (Alpha build) with an empty token on every `get_user`. The Device page (`BZCDzYbb.js`) does **not**: with `userId==""` it clears its cloud state and returns before `checkTokenIsValid`. Optional web patch 12 in section 15.
14. **Print Advisor guards stay correct.** `PrintAdvisorServer.cpp:711-715` clears `setting_id`, `user_id`, `sync_info` and `base_id` on every copy it creates. The only other writers of `setting_id` are the `.info` loader (`Preset.cpp:575`) and the cloud-merge paths (`Preset.cpp:1972-2137`, `PresetBundle.cpp:1542-1555`), which only the deleted sync code reaches. On this PC all 9 `user/default/*/*.info` have an empty `setting_id`, there is no `user_info.json`, `preset_folder` is `""`, `sync_user_preset` is `false`, and `Creality.conf` has no `cloud` section. So `synced_to_cloud` (2561) is always false and the refusals at 1531, 1546, 1786 and 2622 never trigger for Advisor copies.
15. **Mac-only leftovers.** `AppleSignIn.mm` and `FirebaseSignIn.mm` are listed under `if (CREALITYPRINT_APP_STORE)` (`src/slic3r/CMakeLists.txt:939-944`), and `src/CMakeLists.txt:403-438` links AuthenticationServices and the Firebase frameworks. The first block must go once the files are deleted; the second is optional.

---

## 1. Preconditions (run after F1-F3, before starting F4)

1. `git grep -n -e SAgentMqttBridge -e "mqtt_client.h" -- src` returns only `print_manage/App/mqtt_client.*` (F3 removed `SAgentMqttBridge`). If not, F3 is not finished: keep `mqtt_client.*` and the `find_package(PahoMqttCpp)` lines, and delete only `CloudDeviceMqtt*`.
2. `git grep -n CxCloudPrint -- src` returns nothing. If not, delete `simple/sendWorkflow/CxCloudPrint*` and their uses in `EasyPrintSender` (F3) first.
3. `git grep -n open_upload_3mf -- src` returns only `GUI_App.*` and `BBLTopbar.*`. If not, F2 item 3 (File menu "Upload (3mf) to CrealityCloud", `MainFrame.cpp:3266-3270`) is missing: delete it now.
4. `git grep -n NotifyAIChatLoginStatusChanged -- src` returns nothing, or only lines inside blocks F4 deletes (`GUI_App.cpp` 2057, 2095, 6667, 8186, 14470, 14480). Both are fine.
5. `git grep -n -e CommunicateWithCXCloud -e CXCloudDataCenter -- src/slic3r/Utils/PresetUpdater.cpp` returns at most the include at line 43. If not, F1 item 2.4 is missing.
6. `git grep -n -e check_privacy_version -e show_check_privacy_dlg -- src` returns nothing. If not, F1 item 5 is missing: those functions call `request_user_handle` (10620, 10785, 10829, 10833).

---

## 2. Files to delete (`git rm`)

"CMake" means `src/slic3r/CMakeLists.txt` and "list.txt" means `localization/i18n/list.txt`, both at HEAD. Remove those entries by exact path, because F1-F3 change the numbers.

| Files | Lines | CMake | list.txt |
|---|---|---|---|
| `src/slic3r/GUI/LoginDialog.{cpp,hpp}` | 913 + 77 | 348-349 | 257, 483 |
| `src/slic3r/GUI/AppleSignIn.{cpp,hpp,mm}` | 20 + 36 + mm | 350, 353, 941 | 50, 425 |
| `src/slic3r/GUI/FirebaseSignIn.{cpp,hpp,mm}` | 24 + 36 + mm | 351-352, 942 | 502, 505 |
| `src/slic3r/GUI/WebUserLoginDialog.{cpp,hpp}` (`ZUserLogin`) | 474 + 91 | 437-438 | 176, 639 |
| `src/slic3r/GUI/LoginTip.{cpp,hpp}` | 163 + 40 | 785-786 | 488, 823 |
| `src/slic3r/GUI/SyncUserPresets.{cpp,hpp}` | 1189 + 92 | 779-780 | 523, 545 |
| `src/slic3r/GUI/CommunicateWithCXCloud.{cpp,hpp}` (`CXCloudDataCenter`, `CLocalDataCenter`, `CommunicateWithFrontPage`) | 1446 + 219 | 781-782 | 57, 429 |
| `src/slic3r/GUI/UploadFile.{cpp,hpp}` (OSS) | 631 + 165 | 771-772 | 443, 508 |
| `src/slic3r/GUI/VideoOssUploader.{cpp,hpp}` (OSS) | 610 + 112 | 773-774 | 284, 516 |
| `src/slic3r/GUI/PrinterCloudAuthDialog.{cpp,hpp}` (decision D1) | 103 + 49 | 747-748 | 590, 754 |
| `src/slic3r/GUI/print_manage/Upload3mfToCloud.{cpp,hpp}` | 1705 + 205 | 415-416 | 223, 224 |
| `src/slic3r/GUI/print_manage/UploadGcodeToCloud.{cpp,hpp}` | 1922 + 209 | 417-418 | 221, 227 |
| `src/slic3r/GUI/print_manage/Device/KlipperCXInterface.{cpp,hpp}` | 163 + 24 | 382-383 | 188, 189 |
| `src/slic3r/GUI/print_manage/App/TimeLapseShareManager.{cpp,hpp}` | 617 + 42 | 364-365 | 207, 209 |
| `src/slic3r/GUI/print_manage/App/TimeLapseShareTypes.{cpp,hpp}` | 50 + 116 | 360-361 | 202, 208 |
| `src/slic3r/GUI/print_manage/App/DeviceVideoDownloader.{cpp,hpp}` (finding 2) | 557 + 20 | 362-363 | 204, 205 |
| `src/slic3r/GUI/print_manage/App/CloudDeviceMqttSession.{cpp,hpp}` | 192 + 61 | 371-372 | 214, 219 |
| `src/slic3r/GUI/print_manage/App/CloudDeviceMqttTransport.cpp` | 105 | 373 | 215 |
| `src/slic3r/GUI/print_manage/App/mqtt_client.{cpp,h}` (Paho, see precondition 1) | 290 + 154 | 374-375 | 206 (the `.h` is not listed) |
| `tests/cloud_device_mqtt/` (CMakeLists.txt, README.md, cloud_device_mqtt_session_test.cpp) | | `tests/CMakeLists.txt:39` | |
| `resources/web/login/` (9 files, 164 KB) | | | |
| `deps/paho-mqtt/`, `deps/aliyun-oss-cpp-sdk/` | | `deps/CMakeLists.txt` (3.4) | |

About 12,600 lines of C++. The `.mm` lines are inside the `if (CREALITYPRINT_APP_STORE)` block removed in 3.2.

**Keep:**
- `print_manage/AccountDeviceMgr.*` (section 9) and `HttpServer.*` (section 6);
- `UserManager.*` (F5);
- `print_manage/UploadCancellation.hpp` (LAN uploads);
- `libslic3r/PresetSyncUtils.*`, `tests/preset_sync/`, `tests/libslic3r/test_cloud_preset_serialization.cpp`;
- the libslic3r `Preset` fields `setting_id`, `sync_info`, `user_id`, `base_id`, `updated_time` and `PresetCollection::set_sync_info_and_save`. libslic3r is not touched.

Optional, for F6: icons used only by the deleted dialogs (`material_3mf`, `placehold_3mf`, `plate_3mf`, `printer_3mf`, `process_3mf`, `slice_3mf`, `upload_3mf`, `print-time`, `print-weight`). **[inference]** `plate_sliced` and `plate_empty` may be used elsewhere; grep before deleting.

---

## 3. Build system

### 3.1 `src/CMakeLists.txt`
Delete **145-163**: from the comment line with garbled GBK text (`# ... (... Visual Studio)`) through `list(APPEND wxWidgets_LIBRARIES PahoMqttCpp::paho-mqttpp3-static)`. Keep 164 (`# This fixes a OpenGL linking issue on OSX...`).
- This removes `find_library(LIBOSS_DEBUG/LIBOSS_RELEASE)`, the OSS `list(APPEND ...)` (which also appended `OpenSSL::SSL OpenSSL::Crypto`) and `find_package(PahoMqttCpp REQUIRED)`.
- OpenSSL stays linked: `target_link_libraries(libslic3r_gui ... OpenSSL::SSL OpenSSL::Crypto ...)` at `src/slic3r/CMakeLists.txt:1010`.
- No `install()` rule copies an OSS or Paho file (both are static libraries).
- Optional (macOS App Store only): delete **403-438**, from `if (CREALITYPRINT_APP_STORE)` inside `if (APPLE)` to its `endif()` just before `elseif (MSVC)` (AuthenticationServices, Firebase frameworks, `GoogleService-Info.plist` copy).

### 3.2 `src/slic3r/CMakeLists.txt`
- Delete the source lines of section 2 (HEAD 348-353, 360-365, 371-375, 382-383, 415-418, 437-438, 747-748, 771-774, 779-782, 785-786).
- Delete **939-944**:

```cmake
    if (CREALITYPRINT_APP_STORE)
        list(APPEND SLIC3R_GUI_SOURCES
            GUI/AppleSignIn.mm
            GUI/FirebaseSignIn.mm
        )
    endif()
```

### 3.3 `tests/CMakeLists.txt`
Delete line 39 `add_subdirectory(cloud_device_mqtt)` and one of the blank lines 37-38. Tests are off by default (`SLIC3R_BUILD_TESTS OFF`).

### 3.4 `deps/CMakeLists.txt` (not used by `build_exe_only.bat`; the prebuilt dependencies stay on disk, unused)
- Delete 392 `include(paho-mqtt/paho-mqtt.cmake)` and 395 `include(aliyun-oss-cpp-sdk/aliyun-oss-cpp-sdk.cmake)`.
- Delete `dep_OSS` (423 and 452), and `dep_MQTTC` and `dep_MQTT` (428-429 and 455-456), from both `_dep_list` variants.
- Nothing else depends on them (`git grep -e dep_OSS -e dep_MQTT -- deps`).

### 3.5 `localization/i18n/list.txt`
Delete the 36 lines of section 2 by exact path. Keep `AccountDeviceMgr.{cpp,hpp}` (220, 239) and `HttpServer.{cpp,hpp}` (71, 434).

### 3.6 Configure
CMake files change, so `build_exe_only.bat` reconfigures by itself. A stale `PahoMqttCpp_DIR` cache entry is harmless.

---

## 4. `src/slic3r/GUI/GUI_App.cpp` (bottom-up)

| HEAD lines | Anchor | Action |
|---|---|---|
| 14446-14501 | in `on_interinstance_message`: the two Chinese comment lines and `if (msg == std::string("CP_LOGIN_STATUS_CHANGED")) {` ... `return;` `}` | delete; the function then starts with `static constexpr const char* kPrefix = "CP_MOUSE_SCHEME=";`. Nobody sends that message any more (its only sender was `update_account_info`, 8200) |
| 13071-13095 | `static std::mutex mutex_delete_cache_presets;`, `get_delete_cache_presets`, `get_delete_cache_presets_lock`, `delete_preset_from_cloud`, `preset_deleted_from_cloud` | delete; keep `filter_string` (13096) |
| 12708-12712 | `void GUI_App::open_upload_3mf(...)` (body `Upload3mfToCloudDialog dlg(nullptr); dlg.ShowModal();`) | delete |
| 11979-11995 | `void GUI_App::stop_sync_user_preset()` | delete; keep `start_http_server` (11996) |
| 11688-11978 | `void GUI_App::start_sync_user_preset(bool with_progress_dlg)` (sync thread, `AccountDeviceMgr` file-id reset at 11851-11865) | delete |
| 10989-11609 | `reload_settings`, `send_user_presets`, `remove_user_presets`, `sync_preset`, `wait_cloud_token` | delete; **keep** `save_user_default_filaments` (11610-11687, called at 5295) |
| 9597-9605 | `void GUI_App::on_user_login(wxCommandEvent &evt)` (F1 changed its line 9602) | delete |
| 9587-9596 | `void GUI_App::check_track_enable()` | delete (its only caller is `on_user_login`) |
| 9560-9586 | `void GUI_App::on_user_login_handle(wxCommandEvent &evt)` | delete |
| 9514 | in `on_http_error`, inside `if (m_agent) { if (m_agent->is_user_login()) {`: `this->request_user_logout();` | delete the line (Bambu path, dead; F5 removes the rest) |
| 9439-9441 | body of `request_project_download` (`if (!check_login()) return;` + `download_project(project_id);`) | replace with snippet 4.4 |
| 9401-9421 | `void GUI_App::handle_script_message(std::string msg)` (only caller `WebUserLoginDialog.cpp:327`) | delete |
| 8563-8609 | `set_user_preset_status`, `get_user_preset_status`, `get_user_preset_params` branches of `handle_web_request` | replace with snippet 4.3 |
| 8084-8233 | `}else if(command_str.compare("get_account_info") == 0){` ... end of the `update_account_info` branch (`DM::AppMgr::Ins().SystemUserChanged();`) | delete, see 4.2 for the brace rule |
| 8044-8066 | `} else if (command_str.compare("trigger_login_check") == 0) {` ... the `get_login_info`, `homepage_login_or_register` and `homepage_logout` branches | replace with a single line `            }`, see 4.2 |
| 7791-7822 | `void  GUI_App::init_user_profile()` (reads `user_info.json`, sets `preset_folder` to the user id) | delete |
| 6586-6695 | `//BBS`, `request_login`, `get_login_info`, `is_user_login`, `is_login`, `get_user`, `check_login`, `request_user_handle`, `request_user_login`, `request_user_logout`, `request_user_unbind` | replace with snippet 4.1 |
| 6310-6358 | `void GUI_App::ShowUserLogin(bool show,const wxString& loginUrl)` | delete |
| 5364 | in `track_event`: `js["user_id"] = wxGetApp().get_user().userId;` | delete if F1 kept the body (F1 recommends a no-op body) |
| 5319-5322 | `#if !AUTO_CONVERT_3MF` / Chinese comment / `SyncUserPresets::getInstance().startup();` / `#endif` | delete |
| 5297 | in the idle handler: `SyncUserPresets::getInstance().syncConfigToCXCloud();` | delete; keep `save_user_default_filaments(app_config); app_config->save();` |
| 5206-5207 | Chinese comment + `start_user_info_watcher();` | delete |
| 5014 | `init_user_profile();` (just before `init_params->preset_substitutions = preset_bundle->load_presets(...)`) | delete; `enable_user_preset_folder(false)` at 5005 keeps `preset_folder` empty and the folder `user/default` |
| 4988-4989 | `Bind(EVT_USER_LOGIN, ...)`, `Bind(EVT_USER_LOGIN_HANDLE, ...)` | delete |
| 4387-4388 | `//stop_sync_user_preset();` and `SyncUserPresets::getInstance().shutdown();` in `OnExit` | delete |
| 4364-4366 | end of `OnInit`: `if (app_config->get("sync_user_preset") == "true") { start_sync_user_preset(); }` | delete |
| 3914-3918 | in `get_local_device_dir`: `std::string account_id = "default"; if (is_login()) {...}` | replace with `    const std::string account_id = "default"; // Fork: no account, always the default user folder` |
| 3886-3887 | in `get_modellibrary_header`: the `__CXY_TOKEN_` and `__CXY_UID_` emplace lines | delete if F3 kept the function. Its callers are in `WebModelLibraryView.cpp` (F3): if none is left, delete the whole function (3882-3897) and its declaration (`GUI_App.hpp:816`) |
| 3877-3881 | `void GUI_App::setUserAccount(...)` (no callers) | delete |
| 3863-3864 | in `get_extra_header`: `extra_headers.emplace("__CXY_UID_", ...)` and `extra_headers.emplace("__CXY_TOKEN_", ...)` | delete |
| 3397-3401 | `if (m_login_dialog != nullptr) { ... m_login_dialog->Destroy(); ... }` | delete |
| 3149 | in `init_networking_callbacks`, `if (return_code == 5)`: `this->request_user_logout();` | delete the line (Bambu path, dead) |
| 3139-3141 | commented `//m_agent->set_on_user_login_fn(...)` with `request_user_handle` | delete (keeps the grep check clean) |
| 3064-3073 | in the `m_agent` block: `if (m_agent->is_user_login()) { remove_user_presets(); ... }` and `if (app_config->get("sync_user_preset") == "true") { start_sync_user_preset(); }` | delete both `if` blocks (Bambu path, dead) |
| 2636-2641 | `// destroy login dialog` + `if (login_dlg != nullptr) { ... delete login_dlg; ... }` | delete |
| 2469-2472 | `#if !AUTO_CONVERT_3MF` / `if (is_user_login()) request_user_handle(0);` / `#endif` | delete |
| 2459-2460 | Chinese comment + `SyncUserPresets::getInstance().setAppHasStartuped();` | delete |
| 2448-2452 | `//this->check_new_version_cx();` and `if (is_user_login() && app_config->get("stealth_mode") == "false") { request_user_handle(0); }` | delete; keep the `preset_updater->sync(...)` call above (F1) |
| 2159-2170 | in `post_init`: `if (app_config->get("sync_user_preset") == "true") { ... start_sync_user_preset(); ... } else { ... }` | delete; keep `Update_easy_mode_flag();` |
| 2027-2116 | `start_user_info_watcher` and `on_user_info_file_event` (with their Chinese comments) | delete |
| 1996-2023 | `void GUI_App::post_login_status_cmd(bool isSuccess, UserInfo user)` | delete |
| 245 | `#include "SyncUserPresets.hpp"` | delete |
| 242-243 | `#include "print_manage/UploadGcodeToCloud.hpp"`, `#include "print_manage/Upload3mfToCloud.hpp"` | delete |
| 200 | `#include <slic3r/GUI/print_manage/AccountDeviceMgr.hpp>` | delete (its only user was the sync thread) |
| 38-39 | `#include "slic3r/GUI/WebUserLoginDialog.hpp"`, `#include "slic3r/GUI/LoginDialog.hpp"` | delete |

Not touched here: `#include "slic3r/GUI/UserManager.hpp"` (15, F5), `CloudDownloadProgressDialog.hpp` (173, F3), `enable_user_preset_folder` (9531, still called at 5002-5006 and by F5 code), the `m_agent->is_user_login()` uses at 13311 and 13352 (F5).

### 4.1 Login stubs (replace 6586-6695)

```cpp
// Fork: there is no Creality or Bambu account (phase F4). These three stubs stay because
// about 30 files and the Device page still ask for the login state: always logged out.
bool GUI_App::is_user_login()
{
    return false;
}

bool GUI_App::is_login()
{
    return false;
}

const Slic3r::GUI::UserInfo& GUI_App::get_user()
{
    return m_user; // never filled: bLogin false, empty token and userId
}
```

`m_user` stays as a member (`GUI_App.hpp:291`): `get_user()` returns a reference to it and `reinit_downloader` (10718, F3) still reads it. Nothing writes it any more.

### 4.2 `handle_web_request` brace rule

The chain is `} else if (...) {`. After the two deletions, each surviving branch must be closed by exactly one `}` before the next `else if`. Target shape:

```cpp
            else if (command_str.compare("open_project") == 0) {
                if (root.get_child_optional("data") != boost::none) {
                    pt::ptree data_node = root.get_child("data");
                    boost::optional<std::string> project_id = data_node.get_optional<std::string>("project_id");
                    if (project_id.has_value()) {
                        this->request_open_project(project_id.value());
                    }
                }
            }
            else if (command_str.compare("homepage_modeldepot") == 0) {   // or the next branch F3 left
```

```cpp
            else if (command_str.compare("get_recent_projects") == 0) {
                if (mainframe) {
                    if (mainframe->m_webview) {
                        mainframe->m_webview->SendRecentList(INT_MAX);
                    }
                }
            } else if (command_str.compare("set_begin_guide_version_mode") == 0) {   // only if F2 kept this branch
```

If F2 deleted the `set_begin_guide_version_mode` branch (F2 item 2), the `get_recent_projects` branch ends with a plain `}` followed by the commented `// else if (command_str.compare("modelmall_model_advise_get") == 0) {` block.

### 4.3 Preset-sync commands of the Home page (replace 8563-8609)

```cpp
            } else if (command_str.compare("get_user_preset_status") == 0) {
                // Fork: cloud preset sync was removed (F4); the Home page always sees it off.
                json j;
                j["sequence_id"] = "";
                j["command"]     = "get_user_preset_status";
                j["data"]        = json{{"status", false}};
                std::stringstream ss;
                ss << j << std::endl;
                return ss.str();
```

The next line stays `} else if (command_str.compare("send_http_request") == 0) {` (F1 reduced that branch to a warning). `set_user_preset_status` and `get_user_preset_params` become unknown commands and end in `return ""`.

### 4.4 `request_project_download` (replace the body, 9439-9441)

```cpp
void GUI_App::request_project_download(std::string project_id)
{
    // Fork: cloud project download needs the removed account (F4).
    BOOST_LOG_TRIVIAL(info) << "request_project_download ignored: no Creality account, project_id=" << project_id;
}
```

`download_project` (9430) then has no caller; leave it for F5.

---

## 5. `src/slic3r/GUI/GUI_App.hpp`

Delete (HEAD lines):
- 900-901 the comment and `std::unique_ptr<wxFileSystemWatcher> m_user_info_watcher;`;
- 890-893 `user_cloud_presets_`, `repeat_presets_`, `m_user_query_type`, `m_user_syncing` (keep `m_enable_test` at 894);
- 822-824 the comment, `start_user_info_watcher`, `on_user_info_file_event`;
- 817 `setUserAccount` (and 816 `get_modellibrary_header` only if section 4 removed the function);
- 659 `open_upload_3mf`;
- 647-650 `get_delete_cache_presets`, `get_delete_cache_presets_lock`, `delete_preset_from_cloud`, `preset_deleted_from_cloud`;
- 604 `check_track_enable`;
- 593-594 `wait_cloud_token`, `stop_sync_user_preset`;
- 580-591 `reload_settings` (7 lines), blank, `send_user_presets`, `remove_user_presets`, `sync_preset`, `start_sync_user_preset` (keep 592 `save_user_default_filaments` and 595 `start_http_server`);
- 546-547 `on_user_login`, `on_user_login_handle`;
- 535 `handle_script_message`;
- 530-533 `request_user_login`, `request_user_handle`, `request_user_logout`, `request_user_unbind`;
- 521-523 `request_login`, `check_login`, `get_login_info` (**keep** 524 `is_user_login`, 527 `is_login`, 528 `get_user`);
- 518 `ShowUserLogin`;
- 433 `post_login_status_cmd`;
- 354 `init_user_profile`;
- 310-311 `m_sync_update_thread`, `m_user_sync_token`;
- 301-303 `// login widget`, `login_dlg`, `m_login_dialog`;
- 292 `need_delete_presets`;
- 247-248 the comment and `get_login_dialog()`;
- 99 `class LoginDialog;` and 77 `class ZUserLogin;` (keep 69 `class UserManager;`, F5);
- 25 `// #include "slic3r/GUI/WebUserLoginDialog.hpp"--`;
- 10 `#include <wx/fswatcher.h>` (no other user in `src`).

Keep `struct UserInfo` (194-201) and the member `UserInfo m_user;` (291).

---

## 6. `src/slic3r/GUI/HttpServer.{cpp,hpp}`

- `HttpServer.cpp` 1008-1065: delete `HttpServer::bbl_auth_handle_request` (finding 6). Line 8 `#include "slic3r/Utils/NetworkAgent.hpp"` then has no user in this file; delete it too.
- `HttpServer.cpp` 858-966: delete the whole `if(path.find("/login") == 0) { ... return std::make_shared<ResponseRedirect>(location_str); }` block. It exchanged the OAuth code at `/api/cxy/account/v2/oauthLogin`, wrote `user_info.json` and called `post_login_status_cmd`. Keep 852-857 (`/resources`, `/creality_presets`) and the static-file `try` from 969.
- `HttpServer.hpp` 98: delete the `bbl_auth_handle_request` declaration. Keep `ResponseRedirect` (79-87) and `set_request_handler` (OAuthJob).
- Afterwards a request to `http://localhost:13666/login...` falls into the static branch and gets `ResponseNotFound` (404), because `resources/web/login` is gone. The server still serves `resources/web/*`, `/printer-cover/` and the printer image proxy.
- `Http.hpp` (`Http::url_decode`, 156) and `boost::format` (179, 187) stay used. `buildinfo.h` (15) is then unused (its only use was `PROJECT_VERSION_EXTRA` at 870); leaving it is harmless.

---

## 7. Device page: `print_manage/App/PrinterMgrView.{cpp,hpp}`

Removes the cloud MQTT session (broker `mqtt.crealitycloud.{cn,com}:1883`, re-armed by a 2 s `wxTimer`) and the time-lapse share to Creality Cloud. The LAN path (`ws_proxy`, `WebSocketProxy::Manager`, uploads, Print Advisor hooks) is untouched. The two `// PRINT-ADVISOR advisor-hook` lines in `OnScriptMessage` (984-985) stay exactly as they are (`doc/MCP/upstream-touch-points.md:40`).

### 7.1 `PrinterMgrView.cpp` (bottom-up)

| HEAD lines | Anchor | Action |
|---|---|---|
| 1352-1356 | `}else if(strCmd == "viewDetialEvent")` `{` `std::string address = j["address"];` `setMqttDeviceDN(address);` `}` | body becomes the comment `// Fork: this only selected the cloud MQTT target (F4); LAN devices need nothing.` |
| 1024-1031 | `if (strCmd == "start_time_lapse_share") {...}` and `if (strCmd == "cancel_time_lapse_share") {...}` | replace with snippet 7.2 |
| 983-993 | comment `// Invalidate old credentials...`, the two advisor-hook lines, `if (... == "get_user") initMqtt();`, `if (... == "set_cloud_device_mqtt") {...}` | keep 984-985 verbatim; delete 983 and 986; replace 987-993 with `        if (incoming.value("command", std::string()) == "set_cloud_device_mqtt")` / `            return; // Fork: cloud MQTT removed (F4); LAN devices use ws_proxy` |
| 853-966 | `handle_start_time_lapse_share`, `handle_cancel_time_lapse_share`, `send_time_lapse_share_event` | delete; keep `filterInvalidFileNameChars` (968) |
| 719 | `//setMqttDeviceDN("61643612032A19");` in `OnLoaded` | delete; keep `AccountDeviceMgr::getInstance().load();` (701) |
| 514 | `destoryMqtt();` in the destructor | delete |
| 490-498 | `if (m_time_lapse_share_event_bridge) {...}`, `if (m_time_lapse_share_manager) {...}`, `m_time_lapse_share_event_bridge.reset();` | delete |
| 483 | `m_cloud_mqtt_timer.Stop();` | delete |
| 417-476 | `destoryMqtt`, `initMqtt`, `processMqttMessage`, `setMqttDeviceDN` | delete; keep `get_current_milliseconds` (406-416) |
| 404 | `//initMqtt();` | delete |
| 260-305 | constructor: time-lapse bridge and manager, `m_cloud_mqtt = std::make_unique<CloudDeviceMqttSession>(...)`, `m_cloud_mqtt_timer` `SetOwner`/`Bind`/`Start(2000)` | delete; the constructor continues with `m_ws_proxy = std::make_unique<WebSocketProxy::Manager>(` (306) |
| 139-161 | `make_cloud_account_session`, `current_cloud_account_session` | delete; keep `json_identifier_value` (125-137, used by 7.2) |
| 82-86 | `struct PrinterMgrView::TimeLapseShareEventBridge {...};` | delete |
| 2 | `#include "TimeLapseShareManager.hpp"` | delete |

Keep: `#include <boost/beast/core/detail/base64.hpp>` (43, still used at 1642-1645), both `AccountDeviceMgr` includes (6, 54) and the handlers `set_device_relate_to_account` / `request_update_device_relate_to_account` (334-340, 2365-2425). They are already no-ops without login, and the Device page still sends them. `forward_init_device_cmd_to_printer_list` (2444) returns at once (`!is_login()`). `update_current_cxy_device_filament` (2435, called by `PrinterBoxFilamentPanel.cpp:385`) only posts to the page; keep it.

### 7.2 Time-lapse share commands (replace 1024-1031)

```cpp
        if (strCmd == "start_time_lapse_share") {
            // Fork: sharing time-lapse videos to Creality Cloud was removed (F4). Answer with an
            // error so the share dialog of the page leaves its waiting state.
            nlohmann::json data = {{"requestId", json_identifier_value(j, "requestId")},
                                   {"stage", "error"},
                                   {"code", "not_available"},
                                   {"message", "Sharing time-lapse videos to Creality Cloud is not available in this build"}};
            nlohmann::json command_json = {{"command", "time_lapse_share_error"}, {"data", std::move(data)}};
            run_script(wxString::Format("window.handleStudioCmd('%s');",
                RemotePrint::Utils::url_encode(command_json.dump(-1, ' ', true, nlohmann::json::error_handler_t::replace)))
                .ToStdString());
            return;
        }
        if (strCmd == "cancel_time_lapse_share")
            return; // nothing is ever running
```

Verified against the page: the share dialog listens for `time_lapse_share_progress|complete|error|cancelled` and on `time_lapse_share_error` stops waiting (`BZCDzYbb.js`, `ShareVideoDialog`). The payload keys (`requestId`, `stage`, `code`, `message`) are the ones `send_time_lapse_share_event` used for errors.

### 7.3 `PrinterMgrView.hpp`

| HEAD lines | Action |
|---|---|
| 179-181 | delete `m_cloud_mqtt`, `m_cloud_mqtt_timer`, `m_cloud_mqtt_user` |
| 171 | delete `std::string m_curDeviceDN="";` (only the MQTT code used it) |
| 156-158 | delete `struct TimeLapseShareEventBridge;`, `m_time_lapse_share_event_bridge`, `m_time_lapse_share_manager` |
| 132-134 | delete `handle_start_time_lapse_share`, `handle_cancel_time_lapse_share`, `send_time_lapse_share_event` |
| 102-105 | delete `initMqtt`, `setMqttDeviceDN`, `destoryMqtt`, `processMqttMessage` |
| 49-52 | delete the `namespace TimeLapseShare { class TimeLapseShareManager; struct ShareEvent; }` forward declarations |
| 32 | replace `#include "CloudDeviceMqttSession.hpp"` with `#include <future>` (it brought `<chrono> <cstdint> <functional> <future> <memory> <string>`; all but `<future>` are already included at 35-43) |

`MainFrame.hpp` includes `PrinterMgrView.hpp`, so this header change recompiles most of the GUI (it would anyway, because of `GUI_App.hpp`).

### 7.4 Other callers

- `GUI_App.cpp:8228` `destoryMqtt()` is inside `update_account_info`, deleted in section 4.
- No other caller of `initMqtt`, `setMqttDeviceDN`, `processMqttMessage` (checked with `git grep`).

---

## 8. Uploads: `print_manage/RemotePrinterManager.{cpp,hpp}`

The K1C path is not touched: `determinePrinterType` returns `REMOTE_PRINTER_TYPE_KLIPPER4408` for any address with a dot that is neither an "old printer" nor a Moonraker entry (`RemotePrinterManager.cpp:528-544`), and `Klipper4408Interface` uploads it. Only addresses without a dot (Creality Cloud device ids) went to `KlipperCXInterface`.

- `RemotePrinterManager.cpp:546`: replace `return RemotePrinerType::REMOTE_PRINTER_TYPE_CX;` with

```cpp
    // Fork: addresses without a dot were Creality Cloud device ids (KlipperCXInterface, removed in F4).
    // pushFile() fails them cleanly through its default branch (status 1000).
    return RemotePrinerType::REMOTE_PRINTER_TYPE_NONE;
```

- `RemotePrinterManager.cpp:458-462`: delete the `case RemotePrinerType::REMOTE_PRINTER_TYPE_CX:` block. The `default:` branch (`finishTask(task, UploadTaskState::Failed, 1000); return;`) handles `NONE`.
- `RemotePrinterManager.cpp:93` `delete m_pKlipperCXInterface;` and `:31` `m_pKlipperCXInterface = new KlipperCXInterface();`: delete.
- `RemotePrinterManager.hpp:172` `KlipperCXInterface* m_pKlipperCXInterface {nullptr};`: delete.
- `RemotePrinterManager.hpp:37` `REMOTE_PRINTER_TYPE_CX,`: delete (only users are the lines above).
- `RemotePrinterManager.hpp:22` `#include "Device/KlipperCXInterface.hpp"`: replace with

```cpp
#include <iostream> // used to come in through KlipperCXInterface.hpp -> UploadFile.hpp
#include <sstream>
```

`determinePrinterType` has a single caller (`pushFile`, 420), so the change cannot affect retry or cancel paths.

---

## 9. `print_manage/AccountDeviceMgr.{cpp,hpp}` (kept, cloud sync removed)

- `AccountDeviceMgr.cpp:480-482`: delete `reset_account_device_file_id` (its callers were `GUI_App.cpp:11865` and `CommunicateWithCXCloud.cpp:912`, both deleted).
- `AccountDeviceMgr.cpp:246-379`: delete `AccountDeviceMgr::sync_to_cloud()` (two `Http::post` calls to `get_cloud_api_url()` plus an upload to the returned URL).
- `AccountDeviceMgr.cpp:235-236`: delete the blank line and `sync_to_cloud();` in `save_to_file()`. The local file is still written (only reached when logged in, i.e. never).
- `AccountDeviceMgr.hpp:78` `m_account_device_file_id`, `:69` `void sync_to_cloud();`, `:55` `void reset_account_device_file_id(const std::string& fid);`: delete.
- Keep the includes at `.cpp` 9-12 (`Http.hpp`, boost uuid): unused afterwards but they may provide `<fstream>` to this file. Removing them is optional.

Users left: `PrinterMgrView.cpp` (701, 2372, 2406-2417, 2451-2453) and `DeviceMgrRoutes.cpp` (604 `unbind_device_by_group`, 685 `unbind_device_by_address`). All of them return early without login; `load()` keeps reading `user/default/local_device/account_device_info.json` if it exists.

---

## 10. `print_manage/Routes/SysRoutes.cpp`

The Device page and the send page send `get_user` (verified in both bundles). The Device page uses `region` and, when `userId` is empty, clears its cloud state and returns.

- Line 5: delete `#include "slic3r/GUI/LoginDialog.hpp"`.
- 33-53, `get_user` handler: make the empty answer explicit (same JSON shape as today):

```cpp
        // Fork: no Creality account (F4). The Device page still asks for the user; with an empty
        // userId it clears its cloud state, so answer with empty fields.
        this->Handler({"get_user"}, [](wxWebView* browse, const std::string& data, nlohmann::json& json_data, const std::string cmd) {
            std::string country_code = wxGetApp().app_config->get("region");
            unsigned pid = Slic3r::get_current_pid();

            nlohmann::json top_level_json = {
                {"bLogin", 0},
                {"token", ""},
                {"userId", ""},
                {"region", country_code},
                {"pid", pid}
            };

            nlohmann::json commandJson = {
                {"command", "get_user"},
                {"data", top_level_json}
            };

            AppUtils::PostMsg(browse, commandJson);
            return true;
            });
```

- 62-67, `open_login_dialog` handler (only the Home bundle sends it): replace the body with `            return true; // Fork: the Creality login was removed (F4)` and unnamed parameters, or delete the handler. Keeping it avoids the command falling through to `SenderRoutes`/`DeviceMgrRoutes`/`ModelLibraryRoutes`.

`print_manage/App/SendToPrinter.cpp:464-485` (`get_user` for the send page) stays unchanged: it reads `get_user()`, which is now always empty.

---

## 11. Cloud upload entry points: MainFrame, Plater, GLCanvas3D, GLToolbar, BBLTopbar

### 11.1 `GLCanvas3D.cpp` (print button menu, ImGui)

Fluidd devices (`deviceType == 1001`):
- 10991: `s_fluidd_print_string{_u8L("Send print"), _u8L("Export G-code"), _u8L("Upload to CrealityCloud")}` becomes `s_fluidd_print_string{_u8L("Send print"), _u8L("Export G-code")}`.
- 11005-11006 and 11041-11042: delete `else if (s_fst == 2)` / `pst = MainFrame::eUploadGcode;`.
- 11034-11035: delete the `} else if (s_fluidd_print_string[s_fst] == _u8L("Upload to CrealityCloud")) {` branch and its assignment, keeping the closing `}` of the `Export G-code` branch.

Other devices (the K1C):
- 11048-11055: replace the list and the `isEnabledCxCloud()` fallback with

```cpp
                std::vector<std::string> s_print_string{_u8L("Send print"), _u8L("Send to Multi-device"),
                                                        /*_u8L("Export plate sliced file"), */
                                                        _u8L("Export G-code"), _u8L("Export all sliced file")};
```

  The fallback list (used only when `CUSTOMIZED` without `CUSTOM_CXCLOUD_ENABLED`) had 3 items whose indices did not match the `s_pst` mapping below. It was never active in this build.
- 11093-11094 and 11221-11222: delete `else if (s_pst == 4)` / `pst = MainFrame::eUploadGcode;`.
- 11209-11210: delete the `} else if (s_print_string[s_pst] == _u8L("Upload to CrealityCloud")) {` branch and its assignment, keeping the closing `}`.
- `s_pst`/`s_fst` are function statics starting at 0 and are never persisted, so an old index 4 or 2 cannot survive a restart.

### 11.2 `MainFrame.{cpp,hpp}`

| HEAD lines | Action |
|---|---|
| `.cpp` 5391-5425 | delete `on_select_default_preset` ("synchronize your personal data from Bambu Cloud"; it called `start/stop_sync_user_preset`) |
| `.cpp` 5373-5381 | in `update_side_preset_ui`: delete `if (wxGetApp().get_user().bLogin) { ... updateLastFilement ... }` (always false) |
| `.cpp` 5367-5368 | in `update_side_preset_ui`: delete `CXCloudDataCenter::getInstance().setFirstSelectProcessPreset(false);` and the `SyncUserPresets` log line. Keep the rest of the `bFirstUpdate` block (first `select_preset` of the process tab for an untitled project): it is not cloud code |
| `.cpp` 5348-5353 | delete `show_sync_dialog` (its only caller, `on_user_login_handle`, is deleted) |
| `.cpp` 2710 | delete `topbar()->EnableUpload3mf();` |
| `.cpp` 2562 | `m_print_select == eSendToLocalNetPrinter \|\| m_print_select == eUploadGcode \|\| ...` loses the `m_print_select == eUploadGcode \|\|` term |
| `.cpp` 2531-2539 | delete the commented `//else if (m_print_select == eUploadGcode) {...//}` block |
| `.cpp` 2219-2220 | delete `else if (m_print_select == eUploadGcode)` / `wxPostEvent(... EVT_GLTOOLBAR_UPLOAD_GCODE)` |
| `.cpp` 2155-2156 | delete the commented `/* else if (m_print_select == eUpload3mf) ... */` |
| `.cpp` 2138-2139 | delete `else if (m_print_select == eUploadGcode)` / `wxPostEvent(... EVT_GLTOOLBAR_UPLOAD_GCODE)` |
| `.cpp` 2105-2106 | delete `if (tp == eUpload3mf)` / `wxPostEvent(m_plater, SimpleEvent(EVT_GLTOOLBAR_UPLOAD_3MF));` |
| `.cpp` 417 | delete `Bind(EVT_SYNC_CLOUD_PRESET, &MainFrame::on_select_default_preset, this);` |
| `.cpp` 217 | delete `wxDEFINE_EVENT(EVT_SYNC_CLOUD_PRESET, SimpleEvent);` |
| `.cpp` 123-124 | delete `wxDEFINE_EVENT(EVT_USER_LOGIN, ...)` and `wxDEFINE_EVENT(EVT_USER_LOGIN_HANDLE, ...)` |
| `.cpp` 98 | delete `#include "CommunicateWithCXCloud.hpp"` |
| `.hpp` 462-463 | delete `wxDECLARE_EVENT(EVT_USER_LOGIN, ...)`, `wxDECLARE_EVENT(EVT_USER_LOGIN_HANDLE, ...)` |
| `.hpp` 303, 301 | delete `on_select_default_preset`, `show_sync_dialog` |
| `.hpp` 245, 241 | delete `eUpload3mf = 11,` and `eUploadGcode = 7,`. The other values are explicit, so they keep their numbers; `m_print_select` is not persisted |

### 11.3 `Plater.{cpp,hpp}`

| HEAD lines | Action |
|---|---|
| `.cpp` 20915-20918 | delete `Plater::upload_3mf` and `Plater::upload_gcode` |
| `.cpp` 14783-14798 | delete `priv::on_action_upload_3mf(SimpleEvent&)` and `priv::on_action_upload_gcode(SimpleEvent&)` |
| `.cpp` 14661-14688 | delete `priv::on_action_upload_3mf(bool)` and `priv::on_action_upload_gcode()` |
| `.cpp` 7521-7522 | delete `q->Bind(EVT_GLTOOLBAR_UPLOAD_3MF, ...)` and `q->Bind(EVT_GLTOOLBAR_UPLOAD_GCODE, ...)` |
| `.cpp` 6943-6944, 6914-6915 | delete the four `on_action_upload_*` declarations |
| `.cpp` 6297-6300 | delete `m_upload_3mf_dlg` and `m_upload_gcode_dlg` (keep one blank line before `// Data`) |
| `.cpp` 2514 | delete `CXCloudDataCenter::getInstance().setTokenInvalid(true);` (after `CreatePresetSuccessfulDialog`) |
| `.cpp` 226 | delete `#include "CommunicateWithCXCloud.hpp"` |
| `.cpp` 218 | delete `#include "print_manage/UploadGcodeToCloud.hpp"` |
| `.cpp` 4 | delete `#include "slic3r/GUI/print_manage/Upload3mfToCloud.hpp"` |
| `.hpp` 556-557 | delete `upload_3mf` and `upload_gcode` |

`Plater::priv::select_ai_cloud_service` (7971-7986) uses `CommunicateWithCXCloud` and `CXCloudDataCenter`; it is AI cloud code and F3 must delete it (with `AICloudServiceDialog`). If it is still there, delete it and its callers in F4. `Plater.cpp:22177` (`if (wxGetApp().is_user_login())`, design info with `getAgent()->get_user_id()`) stays for F5: it compiles and is now always false.

### 11.4 `GLToolbar.{cpp,hpp}`
Delete `wxDEFINE_EVENT(EVT_GLTOOLBAR_UPLOAD_GCODE, SimpleEvent);` (`.cpp:31`), `wxDEFINE_EVENT(EVT_GLTOOLBAR_UPLOAD_3MF, SimpleEvent);` (`.cpp:40`) and their `wxDECLARE_EVENT` lines (`.hpp:29, 38`). No other users.

### 11.5 `BBLTopbar.{cpp,hpp}`
- `.cpp` 1901-1904: delete `OnUpload3mf` (it called `open_upload_3mf`; it is not bound). `.hpp:26`: delete its declaration.
- `.cpp` 1588: delete the empty `OnLogin`; `.cpp` 1208: delete its `Bind(... &BBLTopbar::OnLogin, this, ID_LOGIN)`; `.hpp:50`: delete its declaration. Keep the `ID_LOGIN` and `ID_UPLOAD3MF` enum values (683, 688) so later IDs do not move.
- `.cpp` 1290-1298: delete the empty `EnableUpload3mf` (body only comments under `#if CUSTOM_CXCLOUD`); `.hpp:81`: delete its declaration; caller `MainFrame.cpp:2710` is removed in 11.2.
- `.cpp` 1155 `//EnableUpload3mf();` and 1187 `//this->Bind(... &BBLTopbar::OnUpload3mf ...)`: delete the comment lines.
- F2 also edits this file (feedback, More, easy-mode switch, `OnLogo` at 1206): locate by anchor.

---

## 12. Preset screens

### 12.1 `Tab.cpp`

| HEAD lines | Action |
|---|---|
| 9245 | in `Tab::delete_preset`: delete `CXCloudDataCenter::getInstance().setTokenInvalid(true);` |
| 9110 | at the end of `Tab::save_preset`: delete `CXCloudDataCenter::getInstance().setTokenInvalid(true);` |
| 8577-8588 | in `may_discard_current_dirty_preset`: delete `if (wxGetApp().plater()->get_project_name() == _L("Untitled")) { if (CXCloudDataCenter... && get_user().bLogin && sync_user_preset ...) { ...; return false; } }` and the blank line after it. Always false without login. Keep 8576 `if (presets == nullptr) presets = m_presets;` |
| 8547-8549 | delete `if (!preset.setting_id.empty()) { wxGetApp().delete_preset_from_cloud(preset.setting_id); }` (process loop) |
| 8539-8541 | same, filament loop |
| 8430 | delete `wxGetApp().delete_preset_from_cloud(current_preset.setting_id);`; keep 8428-8429 and 8431 (`set_sync_info_and_save(..., "delete", 0)` is libslic3r and harmless) |
| 83 | delete `#include "CommunicateWithCXCloud.hpp"` |

### 12.2 `ConfigRelateDialog.cpp`
Delete the four `CXCloudDataCenter::getInstance().setTokenInvalid(true);` lines (3252, 3230, 2743, 2697; each right after `_sync();`) and `#include "CommunicateWithCXCloud.hpp"` (53).

### 12.3 `CreatePresetsDialog.cpp`
- 4739-4745: replace the lambda with

```cpp
    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_OK); });
```

- 4693: `bool sync_user_preset_need_enabled = wxGetApp().getAgent() && ...;` becomes `const bool sync_user_preset_need_enabled = false; // Fork: no cloud preset sync (F4)`. It was already false (no agent); the uses at 4703, 4722 and 4747 now fold to the plain text and buttons.
- 185: delete `wxGetApp().delete_preset_from_cloud(need_delete_preset->setting_id);` (keep the log and `set_sync_info_and_save` lines).

### 12.4 `SavePresetDialog.cpp`
361-364: delete `if (!current_preset.setting_id.empty()) { BOOST_LOG_TRIVIAL(info) << ...; wxGetApp().delete_preset_from_cloud(current_preset.setting_id); }`. Keep `current_preset.sync_info = "delete";` and `m_presets->delete_preset(m_preset_name);`.

### 12.5 `FilamentPanel.cpp` (LoginTip users)
- 42: delete `#include "LoginTip.hpp"`.
- 3612 and 4210: delete `LoginTip::getInstance().resetHasSkipToLogin();`.
- 3616: `filamentUserMaterialRet = LoginTip::getInstance().isFilamentUserMaterialValid(validMaterials[i].second.userMaterial);` becomes

```cpp
        // Fork: no account (F4): a slot mapped to a cloud user preset cannot be resolved, so only its colour is synced.
        filamentUserMaterialRet       = validMaterials[i].second.userMaterial.empty() ? 1 : int(wxID_CANCEL);
```

- 4212: same with `item->getUserMaterial()` (`std::string`, `FilamentPanel.h:369`).
- Behaviour: today, without login and with a non-empty `userMaterial`, `LoginTip` showed "Not logged in yet, unable to map user filament preset" once (Login/Cancel), then returned `wxID_CANCEL` or `wxID_YES`. Now it returns `wxID_CANCEL` silently, which is the existing "declined" path: colour synced, name and sync label cleared. Only CFS boxes report `userMaterial`; the K1C has no CFS.
- Optional: `AutoMappingLoginMsgDialog` (`MsgDialog.cpp:726-750`, `MsgDialog.hpp:434-447`) has no user left. Deleting it touches a widely included header; leave it unless you accept the extra recompile.

### 12.6 `Preferences.{cpp,hpp}`

| HEAD lines | Action |
|---|---|
| `.cpp` 1885-1899 | delete `PreferencesDialog::notify_sync_user_preset_changed()` |
| `.cpp` 1775 | delete `wxGetApp().request_user_logout();` (inside `if (agent)`, dead) |
| `.cpp` 1567-1569 | delete `sizer_page->Add(item_user_sync, ...)`, its spacer and the blank line |
| `.cpp` 1488-1489 | delete `auto item_user_sync = create_item_checkbox(_L("Auto sync user presets(Printer/Filament/Process)"), ...);` |
| `.cpp` 876-885 | delete the `if (param == "sync_user_preset") { ... start/stop_sync_user_preset ... notify_sync_user_preset_changed(); ... }` block in the checkbox handler |
| `.cpp` 348 | delete `wxGetApp().request_user_logout();` (inside `if (agent)`, dead) |
| `.hpp` 156 | delete `void notify_sync_user_preset_changed();` |

After F1 (which deletes `item_user_exp` at 1490-1491 and 1570-1571) and F2 Variant A (which deletes `item_default_page` at 1564-1565), an `#if CUSTOM_CXCLOUD` / `#endif` pair may be left empty around 1487-1492 or 1563-1572: delete it. `notify_preferences_changed` (1861-1883) still sends the stored `sync_user_preset` to the Home page; nothing can set it to `true` any more (it is `false` on this PC). `WebViewPanel::sync_user_preset` (Home) becomes unused; leave it to F7.

---

## 13. Remaining callers

| File | HEAD lines | Action |
|---|---|---|
| `GUI/InstanceCheck.cpp` | 515 | delete `wxGetApp().request_user_logout();` in the region-change branch (508-522) |
| `GUI/InstanceCheck.cpp` | 500-507 | delete the `if(message=="login" \|\| message=="logout") { ... reload_homepage(); return; }` block. Its senders were `send_app_message("login"/"logout")` at `GUI_App.cpp` 8129 and 8226, both deleted. F2 Variant A already deletes line 503 (`select_tab(MainFrame::tpHome)`) |
| `GUI/WebViewDialog.cpp` | 521-526 | `WebViewPanel::OnFreshLoginStatus`: body becomes `    (void)event; // Fork: no account (F4); the Home login panel has nothing to refresh`. The 2 s `LOGIN_INFO_UPDATE_TIMER_ID` timer and the event table entry stay for F7 |
| `GUI/WebGuideDialog.cpp` | 903-904 | delete `if (bLogin)` / `GUI::wxGetApp().CallAfter([this] { login(); });`. `bLogin` is only set when a Bambu agent is logged in; it stays as a set-but-unused local |
| `GUI/GUI.cpp` | 534-541 | delete `void login()` (it ran `ZUserLogin dlg; dlg.run();`) |
| `GUI/GUI.cpp` | 36 | delete `#include "WebUserLoginDialog.hpp"` (nothing else in `GUI.cpp` uses its wx webview headers) |
| `GUI/GUI.hpp` | 89-90 | delete `// Display a Login dialog` and `extern void login();` |
| `GUI/BindDialog.cpp` | 1102 | `int result = wxGetApp().request_user_unbind(m_machine_info->dev_id);` becomes `int result = -1; // Fork: account unbinding removed with the login (F4); BindDialog goes in F5` |
| `GUI/PhysicalPrinterDialog.cpp` | 192-199 and 3 | decision D1, see 13.1 |
| `Utils/PresetUpdater.cpp` | 43 | delete `#include "slic3r/GUI/CommunicateWithCXCloud.hpp"` (F1 marked it optional; it is mandatory now) |

### 13.1 `PhysicalPrinterDialog.cpp` (decision D1: delete `PrinterCloudAuthDialog`)

- Line 3: delete `#include "PrinterCloudAuthDialog.hpp"`.
- 192-199: the `else` branch after the SimplyPrint `OAuthDialog` becomes

```cpp
                    } else {
                        // Fork: the embedded cloud-login dialog was removed with the account code (F4).
                        // Hosts like Obico need the API key typed in the dialog.
                        msg = _L("Cloud login for this print host is not available in this build. Enter the API key manually.");
                    }
```

  This adds one new `_L()` string (shown in English until translated). Alternative if you use Obico: keep `PrinterCloudAuthDialog.{cpp,hpp}` (it is Orca code, not the Creality account) and skip this item, its CMake lines and its list.txt lines.

### 13.2 Leftovers that depend on F3 (check, do not assume)

- `print_manage/Routes/ModelLibraryRoutes.cpp`: if F3 kept it, delete its handlers `get_account_info` (69-112, reads `user_info.json` and returns the token), `update_account_info` (114-119) and `login_account_success` (121-126). They forwarded to the deleted `handle_web_request` branches.
- `get_cloud_api_url` (`GUI/GUI.cpp:655-683`, `GUI/GUI.hpp:81`): after F1 and F4 the remaining users at HEAD are F3 code (`MCPChatPanel.cpp` 2458 and 3131, `SlicerBridgeActionsObject.cpp` 460, 702 and 817, `CxCloudPrintClient.cpp:133`, `GUI_App::OpenEshopRecommendedGoods` at 14559). Run `git grep -n get_cloud_api_url -- src`: if only the definition and the declaration remain, delete them; otherwise leave them and note which F3 item still uses them.
- `get_modellibrary_header`: see section 4 (3882-3897).
- `Plater::priv::select_ai_cloud_service`: see 11.3.

---

## 14. Web commands

### 14.1 Commands handled in C++ that become dead or change

| Command | Handler | After F4 | Who sends it |
|---|---|---|---|
| `trigger_login_check`, `get_login_info`, `homepage_login_or_register`, `homepage_logout` | `GUI_App::handle_web_request` | deleted (unknown command, `return ""`) | Home bundle (`homepage`); `homepage_login_or_register` appears only in `resources/web/login` |
| `get_account_info`, `login_account_success`, `update_account_info` | `handle_web_request` (+ `ModelLibraryRoutes` 69-126) | deleted | Home bundle |
| `set_user_preset_status`, `get_user_preset_params` | `handle_web_request` | deleted | Home bundle |
| `get_user_preset_status` | `handle_web_request` | stub, always `status:false` | Home bundle |
| `request_project_download` | `handle_web_request` | logged and ignored (it needed `check_login`) | Home bundle (Bambu project download) |
| `user_login` | `GUI_App::handle_script_message` (from `ZUserLogin`) | deleted with the dialog | `WebUserLoginDialog` page only |
| `open_login_dialog` | `SysRoutes` | no-op, returns true | Home bundle only |
| `get_user` | `SysRoutes` (Device page), `SendToPrinter::RegisterHandler` (send page) | same JSON, always an empty user | Device page, send page, Home |
| `get_user` hook | `PrinterMgrView::OnScriptMessage` 986 (`initMqtt()` before routing) | removed; routing unchanged | Device page |
| `set_cloud_device_mqtt` | `PrinterMgrView::OnScriptMessage` 987-993 | consumed and ignored | Device page |
| `viewDetialEvent` | `PrinterMgrView::OnScriptMessage` 1352 | branch kept, empty body | Device page (16 places) |
| `start_time_lapse_share` | `PrinterMgrView` 1024 | answers `time_lapse_share_error` with `code:"not_available"` | Device page share dialog |
| `cancel_time_lapse_share` | `PrinterMgrView` 1028 | ignored | Device page share dialog |
| `set_device_relate_to_account`, `request_update_device_relate_to_account` | `PrinterMgrView` handlers | unchanged; already no-ops without login | Device page |

C++-to-page messages that are never sent any more:
- `login_status_cmd` (Home);
- `mqtt_message` and `cloud_device_mqtt_state` (Device page; their handlers in the page stay idle);
- `time_lapse_share_progress`, `time_lapse_share_complete`, `time_lapse_share_cancelled`;
- the replies to `get_user_preset_params` and `get_account_info`.

`reinit_related_to_account_device` and `update_device_relate_account_state` were already sent only when logged in.

### 14.2 What the LAN bundles send (from `grep -o 'command:"..."'` on the bundles)

**Device page** (`resources/web/deviceMgr/assets/BZCDzYbb.js`):
`add_device`, `add_device_fluidd`, `add_device_klipper`, `add_group`, `advisor_snapshot_result`, `browse_fluidd_ca_file`, `buy_filament_cmd`, `cancel_time_lapse_share`, `cancel_upload`, `check_upload_file_ready_result`, `common_openurl`, `delete_file_from_lan_device`, `device_detail_print`, `diagnosis_lan_connect`, `down_files`, `edit_device_group_name`, `edit_device_name`, `get_file_List_from_lan_device`, `get_lang`, `get_machine_list`, `get_printer_progress`, `get_region`, `get_system_id`, `get_user`, `get_user_custom_color_list`, `get_webrtc_local_param`, `init_device`, `is_dark_theme`, `move_print_head_result`, `move_to_group`, `print_control_result`, `print_video_connect`, `probe_lan_device`, `receive_color_match_info`, `receive_device_list`, `remove_device`, `remove_group`, `remove_to_first`, `req_device_move_direction`, `request_device_address_correction`, `request_user_operation_state`, `retry_upload`, `save_user_operation_state`, `scan_device`, `send_gcode`, `set_cloud_device_mqtt`, `set_current_device`, `set_device_detail_state`, `set_user_custom_color_list`, `sort_group`, `start_time_lapse_share`, `switch_to_tab`, `sync_filament_result`, `sync_mappinp_cfs_filament`, `test_fluidd_device`, `track_learn_about_cfs`, `update_current_device_info`, `update_device_info`, `update_devices`, `upload_device_file`, `uploade_file_oldPrinter`, `viewDetialEvent`. It also sends `set_device_relate_to_account` and `request_update_device_relate_to_account`, built in another way.

F4 changes only `get_user` (the hook), `set_cloud_device_mqtt`, `start_time_lapse_share`, `cancel_time_lapse_share` and `viewDetialEvent`, as listed in 14.1. Every other command keeps its handler.

**Send page** (`resources/web/sendToPrinterPage/assets/BDAlyC64.js`):
`buy_filament_cmd`, `cancel_send`, `common_open_wiki_url`, `common_openurl`, `forward_device_detail`, `get_devices`, `get_lang`, `get_machine_list`, `get_model_match`, `get_system_id`, `get_threeMF`, `get_user`, `get_webrtc_local_param`, `is_dark_theme`, `refresh_all_device`, `register_complete`, `request_onlygcode_plate_data_on_show`, `request_update_plate_thumbnail`, `request_user_operation_state`, `save_user_operation_state`, `send_3mf`, `send_gcode`, `send_start_print_cmd`, `set_current_plate_index`, `set_error_cmd`, `set_video_elapse_cmd`, `start_heartbeat_cmd`, `stop_heartbeat_cmd`.

F4 changes none of their handlers; `get_user` simply returns an empty user.

**Page reaction to the empty user (verified):**
- Device page, `registerHandler("get_user", ...)`: if `Rr.userId==""` it clears the stored user and token, calls `cxApi.SetAuthority("","","")`, removes the cloud group and returns. No cloud request.
- Send page: stores the empty user in `localStorage.userData`, then calls `checkTokenIsValid()`, which still posts to Creality Cloud (finding 13). See 15.

---

## 15. Optional web patch 12 (send page, no token check without a token)

Add after F2's patch "11" in `tools/web-patches/patches.mjs`:

```js
  // 12 --------------------------------------------------------------------
  {
    id: "12",
    title: "Send page: no Creality Cloud token check without an account",
    dir: SEND,
    fileContains: ".getModelMatch(",
    edits: [
      {
        marker: "/*PA:12*/",
        applied: /\/\*PA:12\*\//,
        find: new RegExp(String.raw`(?<s>${ID})\.setToken\((?<v>${ID})\|\|""\),\k<s>\.checkTokenIsValid\(\),`, "g"),
        replace: (m, v) => fill(String.raw`@@s@@.setToken(@@v@@||""),/*PA:12*/@@v@@&&@@s@@.checkTokenIsValid(),`, v),
      },
    ],
  },
```

- Checked with Node on the current bundle: the regex matches exactly once (`a.setToken(v||""),a.checkTokenIsValid(),`, so `s=a`, `v=v`), and `.getModelMatch(` is present (the same anchor patch 10 uses). `v` is the token from `const{region:g,userId:f,token:v}=h||{}`.
- Add a "### 12" section to `doc/MCP/web-bundle-patches.md`.
- Verify: `node tools/web-patches/apply-web-patches.mjs --check`, then `--apply`.
- The Device page needs no patch: it returns before `checkTokenIsValid` when `userId` is empty.

---

## 16. Compile and link risks

1. **Transitive includes lost with deleted headers.**
   - `RemotePrinterManager.hpp` loses `UploadFile.hpp` (OSS header, `nlohmann/json.hpp`, global `using namespace nlohmann`). Its includers either include `GUI_App.hpp` (which brings `AppConfig.hpp` with `using namespace nlohmann`) or `nlohmann/json.hpp`, or use no `json`: `KlipperInterface.cpp`, `LanPrinterInterface.cpp`, `OctoPrintInterface.cpp`, `RemotePrinterManager.cpp` (0 `json` uses; `<iostream>`/`<sstream>` added in section 8), `PrinterMgrView.cpp`, `SendToPrinter.cpp`, `PrintAdvisorMonitor.cpp` (`using json = nlohmann::json`), `DeviceMgrRoutes.cpp` (includes `nlohmann/json.hpp` and `AppConfig.hpp`), `EasyPrintSender.cpp`. If one still fails, add `#include <nlohmann/json.hpp>` to that `.cpp`, not to the header.
   - `GUI_App.cpp` and `Plater.cpp` lose `Upload3mfToCloud.hpp`/`UploadGcodeToCloud.hpp`, which included `SelectMachine.hpp`, `DeviceManager.hpp`, `BBLStatusBar*.hpp` and many wx headers. `Plater.cpp` includes `SelectMachine.hpp` (146) and `BBLStatusBar.hpp` (174) itself; `GUI_App.cpp` includes `DeviceManager.hpp` (33), `GUI_ObjectList.hpp` (wx dataview) and `ReleaseNote.hpp`. **[inference]** If `GUI_App.cpp` then misses a type, add `#include "SelectMachine.hpp"` there (F5 removes it anyway).
   - `PrinterMgrView.hpp` loses `CloudDeviceMqttSession.hpp`; `<future>` is added in its place.
   - `ConfigRelateDialog.cpp`, `Tab.cpp`, `MainFrame.cpp`, `Plater.cpp` lose `CommunicateWithCXCloud.hpp` (`libslic3r/PresetBundle.hpp`, `<map> <set> <string> <vector>`). All of them include `PresetBundle.hpp` through their own headers. **[inference]** Low risk.
   - `GUI.cpp` loses `WebUserLoginDialog.hpp` (wx webview headers); nothing in `GUI.cpp` uses them.
   - `GUI_App.hpp` drops `<wx/fswatcher.h>`; no other file uses `wxFileSystemWatcher`.
2. **Unresolved references if a caller is missed.** The grep checklist in section 20 covers every deleted symbol. Comment lines that mention deleted names are deleted too (section 4: 3139-3141, 4387; section 7: 404, 719; section 11: 1155, 1187, 2155-2156, 2531-2539), so a clean grep means no stale text.
3. **`handle_web_request` braces.** Two deletions inside an `if / else if` chain; follow 4.2 exactly.
4. **Link.** Removing `PahoMqttCpp::paho-mqttpp3-static` is only safe when no `mqtt::` symbol is left: after F3 (no `SAgentMqttBridge`) and F4 (no `mqtt_client`, no `CloudDeviceMqttTransport`), `git grep -n "mqtt::" -- src` must be empty. OSS: `git grep -n -e AlibabaCloud -e alibabacloud -- src` must be empty.
5. **Header fan-out.** `GUI_App.hpp`, `MainFrame.hpp`, `Plater.hpp`, `PrinterMgrView.hpp`, `RemotePrinterManager.hpp`, `GLToolbar.hpp` change: most of `libslic3r_gui` recompiles (expected; F1-F3 already do the same). libslic3r and `PrintConfig.hpp` are not touched, so the slicing engine does not recompile.
6. **Warnings only.** `bLogin` in `WebGuideDialog.cpp` becomes set-but-unused; `download_project` (`GUI_App.cpp:9430`) and `AutoMappingLoginMsgDialog` become unused. No `/WX`.
7. **list.txt.** It must not list deleted files (the `.mo` step reads the `.po` files, but `list.txt` is used by gettext extraction).

---

## 17. LAN send and Device page risks

| Area | Change | Risk | How to check |
|---|---|---|---|
| K1C upload (`Klipper4408Interface`) | none | `determinePrinterType` unchanged for dotted addresses | upload a G-code from the Device page file list (no print), see 19.8 |
| Addresses without a dot | were `KlipperCXInterface` (cloud), now `REMOTE_PRINTER_TYPE_NONE` | an upload to such an address now fails at once with status 1000 instead of going to the cloud | only cloud-bound devices had such addresses; the K1C is `192.168.x.x` |
| `PrinterMgrView` constructor | no MQTT session, no 2 s timer, no share manager | the `weak_share_bridge` was also the lifetime bridge of the MQTT events; both go together | Device tab opens, no crash on close |
| `PrinterMgrView` destructor | no `m_cloud_mqtt_timer.Stop()`, no share-manager `Shutdown()`, no `destoryMqtt()` | fewer threads to join at exit | clean exit with the Device tab opened |
| `OnScriptMessage` | `get_user` no longer triggers `initMqtt()`; `set_cloud_device_mqtt` consumed | the two advisor-hook lines must stay first | `camera_snapshot` (Print Advisor) still works; it goes through `advisor_handle_page_message` |
| `viewDetialEvent` | empty branch | opening the detail page only set the MQTT target | detail page opens with telemetry and video |
| Time-lapse share | error reply | the share dialog shows a failure instead of uploading | open a time-lapse in the Device page, press share: error message, no spinner |
| Send page `get_user` | unchanged handler, empty user | the page still calls `checkTokenIsValid` unless patch 12 | network check 19.7 |
| `AccountDeviceMgr` | no cloud sync | none: already gated by `is_login()` | Device page groups unchanged (they come from `DM::DeviceMgr`) |
| Print Advisor | none in code | `synced_to_cloud` always false; rename and delete of Advisor copies allowed as before | `list_profiles` shows `synced_to_cloud: false` for every profile |

---

## 18. Threads, lifecycle and security

**Threads and timers removed:**
- `SyncUserPresets` worker (`std::thread` started by `startup()` in `OnInit`, a 100 ms polling loop for the whole session, stopped in `OnExit`);
- `GUI_App::m_sync_update_thread` (only started when logged in);
- `wxFileSystemWatcher` on `user_info.json` (events on the GUI thread);
- `CloudDeviceMqttSession` workers and the `PrinterMgrView` 2 s `wxTimer` that re-armed it;
- `TimeLapseShareManager` worker (download from the printer + OSS upload);
- `LoginDialog` async callbacks (`CallAfter` + `get_login_dialog()` checks);
- `AccountDeviceMgr::sync_to_cloud` synchronous `Http::post` calls (only when logged in).

**Lifecycle after F4:**
- `OnInit`: no `init_user_profile`; `enable_user_preset_folder(false)` (5002-5006) sets `preset_folder=""` and loads `user/default`, as today on this PC.
- `OnExit`: `advisor_on_shutdown()` stays first. Nothing replaces `SyncUserPresets::shutdown()`.
- `MainFrame::shutdown` (Print Advisor server stop) is not touched.
- `PrinterMgrView` destructor: still stops the `ws_proxy` manager and joins the scan threads; there is no MQTT or share worker to stop first.
- Every remaining handler touched here runs on the GUI thread (`OnScriptMessage`, routes, `handle_web_request`). The new code in 7.2 calls `run_script` directly, like the code it replaces.

**Security and privacy:**
- No more Creality account token in memory, in `AppConfig` (`cloud/user_id`, `cloud/token`) or in HTTP headers (`__CXY_UID_`, `__CXY_TOKEN_` removed from `get_extra_header`; F1 already made `Http::set_extra_headers` a no-op).
- `localhost:13666/login` no longer exchanges OAuth codes or writes `user_info.json`.
- No MQTT connection to `mqtt.crealitycloud.*:1883`, no OSS upload, no preset upload or download, no account-device upload.
- A stale `user_info.json` or `cloud` section left by another install is ignored (nothing reads them any more); deleting them by hand is optional.
- The safety rules of the Print Advisor (CLAUDE.md) are unchanged; no confirmation path is touched.

---

## 19. Verification (one build)

1. **Preconditions** of section 1 hold.
2. **Build** with `build_exe_only.bat` (reconfigures because CMake files changed).
3. **Grep checklist** of section 20: no result outside this spec.
4. **G-code**: the 4 F0 projects sliced from the command line are byte-identical to F1-F3 (mask `; creality_uuid:`); F4 does not touch libslic3r.
5. **Sidecar**: `npm run typecheck` and `npm run smoke` with the app open; `scripts/tools-expected.json` unchanged (F4 adds or removes no tool).
6. **Manual start (about 3 minutes)**:
   - the app starts with no login prompt and no error;
   - Prepare: load a model, slice; Preview works;
   - print button menu (K1C): "Send print", "Send to Multi-device", "Export G-code", "Export all sliced file", and **no** "Upload to CrealityCloud";
   - Preferences: no "Auto sync user presets" checkbox; changing the region does not log out or crash;
   - process and filament tabs: save a user copy, rename it, delete it (no cloud dialog);
   - Print Advisor active; `list_profiles` shows `synced_to_cloud: false`;
   - close the app: clean exit, no hang (check that `CrealityPrint.exe` disappears from Task Manager).
7. **Network**: 5 minutes with Resource Monitor, Device tab open. Expected: no connection to `mqtt.crealitycloud.*` (port 1883), no `*.aliyuncs.com` (OSS), no `api*.crealitycloud.*` from the C++ side. Residual expected only without patch 12: `api-dev.crealitycloud.cn` from the send page when the send dialog opens. Other residuals already listed in F1 (thumbnail CDN, GA in web bundles).
8. **Printer (K1C idle, bed empty and cold)**: run `doc/MCP/printer-test-checklist.md` (sections 1-4: status, snapshot, light, 2-minute preheat, side fan), then the F4 additions:
   1. Device tab: the K1C card is online; telemetry updates; open the detail page (`viewDetialEvent`) and check the video.
   2. Device page, printer file list, **upload** a small G-code from disk (`upload_device_file` → `pushUploadTasksWithProfile` → `Klipper4408Interface`): progress reaches 100% and the file appears in the list. This exercises the changed `RemotePrinterManager` without starting a print.
   3. Delete that file from the printer list (`delete_file_from_lan_device`).
   4. Open the send dialog: the K1C is preselected and compatible; close it **without** sending. Start a real print only if the user wants one (the Print Advisor never starts a print).
   5. Time-lapse list (if any video exists): the share button shows an error instead of spinning.
   6. At the end `targetBedTemp0` is 0.

---

## 20. Final grep checklist

Run from the repository root after the edits. Each command must print **nothing** (this spec file is under `doc/`, outside the searched paths).

```sh
# 1. Deleted classes, files and functions (whole words)
git grep -n -w -E "LoginDialog|ZUserLogin|WebUserLoginDialog|AppleSignIn|FirebaseSignIn|LoginTip|SyncUserPresets|CommunicateWithCXCloud|CXCloudDataCenter|CLocalDataCenter|CommunicateWithFrontPage|UserProfileListItem|UploadFile|OssSdkGuard|VideoOssUploader|PrinterCloudAuthDialog|Upload3mfToCloudDialog|UploadGcodeToCloudDialog|KlipperCXInterface|REMOTE_PRINTER_TYPE_CX|TimeLapseShare|TimeLapseShareManager|DeviceVideoDownloader|CloudDeviceMqttSession|MakeCloudDeviceMqttTransport|MQTTClient" -- src tests

# 2. Deleted GUI_App / MainFrame / Plater / PrinterMgrView members and events
git grep -n -w -E "ShowUserLogin|request_login|get_login_info|check_login|request_user_handle|request_user_login|request_user_logout|request_user_unbind|on_user_login|on_user_login_handle|start_user_info_watcher|on_user_info_file_event|init_user_profile|setUserAccount|post_login_status_cmd|get_login_dialog|m_login_dialog|login_dlg|send_user_presets|remove_user_presets|wait_cloud_token|start_sync_user_preset|stop_sync_user_preset|reload_settings|delete_preset_from_cloud|preset_deleted_from_cloud|get_delete_cache_presets|get_delete_cache_presets_lock|need_delete_presets|handle_script_message|check_track_enable|open_upload_3mf|OnUpload3mf|EnableUpload3mf|OnLogin|eUpload3mf|eUploadGcode|EVT_GLTOOLBAR_UPLOAD_3MF|EVT_GLTOOLBAR_UPLOAD_GCODE|on_action_upload_3mf|on_action_upload_gcode|EVT_USER_LOGIN|EVT_USER_LOGIN_HANDLE|EVT_SYNC_CLOUD_PRESET|show_sync_dialog|on_select_default_preset|notify_sync_user_preset_changed|initMqtt|setMqttDeviceDN|destoryMqtt|processMqttMessage|m_cloud_mqtt|m_cloud_mqtt_timer|m_curDeviceDN|make_cloud_account_session|current_cloud_account_session|sync_to_cloud|reset_account_device_file_id|m_account_device_file_id|bbl_auth_handle_request|m_user_info_watcher|m_sync_update_thread|m_user_sync_token|m_user_query_type|m_user_syncing|user_cloud_presets_|repeat_presets_|setTokenInvalid|resetHasSkipToLogin|isFilamentUserMaterialValid" -- src

# 3. Fixed strings
git grep -n -F -e "sync_preset(" -e "upload_3mf(" -e "upload_gcode(" -e "__CXY_UID_" -e "__CXY_TOKEN_" -e "user_info.json" -e "oauthLogin" -e "mqtt::" -e "mqtt/async_client" -e "alibabacloud" -e "AlibabaCloud" -e "extern void login" -e "Upload to CrealityCloud" -e "mqtt.crealitycloud" -- src

# 4. Build files
git grep -n -i -e paho -e liboss -e alibabacloud -e aliyun-oss -e cloud_device_mqtt -- CMakeLists.txt src/CMakeLists.txt src/slic3r/CMakeLists.txt tests/CMakeLists.txt deps/CMakeLists.txt

# 5. list.txt and resources
git grep -n -E "LoginDialog|AppleSignIn|FirebaseSignIn|LoginTip|SyncUserPresets|CommunicateWithCXCloud|GUI/UploadFile|VideoOssUploader|PrinterCloudAuthDialog|Upload3mfToCloud|UploadGcodeToCloud|KlipperCXInterface|TimeLapseShare|DeviceVideoDownloader|CloudDeviceMqtt|mqtt_client" -- localization/i18n/list.txt
git ls-files resources/web/login tests/cloud_device_mqtt deps/paho-mqtt deps/aliyun-oss-cpp-sdk
```

Allowed results:
- `src/libslic3r/Preset.cpp:2094` (log text "SyncUserPresets repair lost G-code", grep 1) and `src/libslic3r/DataDirectoryMigration.cpp:445` (`user_info.json` in the list of files copied by the data-folder migration, grep 3): libslic3r is not touched by F4.
- `__CXY_TOKEN_`/`__CXY_UID_` in `GUI_App.cpp` only if F3 kept `get_modellibrary_header` with callers (section 4); otherwise none.
- `get_cloud_api_url` is not in the list: it may legitimately remain if F3 code still uses it (13.2).
- Must still be present (sanity): `git grep -n -w -e is_login -e is_user_login -e get_user -- src/slic3r/GUI/GUI_App.hpp` shows the three declarations; `git grep -n "AccountDeviceMgr::getInstance" -- src` shows `PrinterMgrView.cpp` and `DeviceMgrRoutes.cpp`; `git grep -n "synced_to_cloud" -- src` shows `PrintAdvisorServer.cpp:2561`.

---

## 21. Decisions and open questions

- **D1.** Delete `PrinterCloudAuthDialog` (Obico web login, Orca code, not Creality's account)? This spec assumes yes (13.1). Keep it if Obico is ever used.
- **D2.** Apply web patch 12 (send page token check) in F4, or leave the residual call for a later web-patch round?
- **D3.** Remove the macOS App Store link block (`src/CMakeLists.txt:403-438`) now, or with `deps/Darwin` in F6? (Required: the `.mm` source block in `src/slic3r/CMakeLists.txt:939-944`.)
- **Q1.** Delete `AutoMappingLoginMsgDialog` from `MsgDialog.{cpp,hpp}` (no users after F4; costs a wider recompile)?
- **Q2.** Rename the Preferences label "Login Region" (`Preferences.cpp`, `create_item_region_combobox(_L("Login Region"), ...)`) to "Region"? The region still selects catalogues and covers. Text change only.
- **Q3.** Update the sidecar descriptions that mention cloud sync (`tools/print-advisor-mcp/src/index.ts` 414, 636, 652 and `README.md:35`)? They stay true (the field is reported, always false); changing them would need `npm run smoke -- --update-expected` only if the tool list changes (it does not).

---

## 22. Suggested commit order (one build at the end)

Intermediate commits do not need to compile on their own (one build at the end, as in the plan), but each one only removes one feature, so it can be reverted alone.

1. **Login**: delete `LoginDialog`, `WebUserLoginDialog`, `AppleSignIn`, `FirebaseSignIn`, `LoginTip`, `resources/web/login`; `HttpServer` `/login` and `bbl_auth_handle_request`; GUI_App login functions and stubs (4.1); `SysRoutes`; `InstanceCheck`; `WebViewDialog`; `WebGuideDialog`; `GUI.cpp`/`GUI.hpp` `login()`; `BindDialog` call; `FilamentPanel` (12.5); `Preferences` logout calls.
2. **Preset sync**: delete `SyncUserPresets`, `CommunicateWithCXCloud`; GUI_App sync code and web commands (4.3); `Tab`, `ConfigRelateDialog`, `CreatePresetsDialog`, `SavePresetDialog`, `MainFrame` sync dialog, `Plater` token line, `Preferences` checkbox; `PresetUpdater` include.
3. **Cloud upload**: delete `Upload3mfToCloud`, `UploadGcodeToCloud`, `UploadFile`, `VideoOssUploader`, `KlipperCXInterface`; `RemotePrinterManager`; `GLCanvas3D` menu; `MainFrame` print select; `Plater`; `GLToolbar`; `BBLTopbar`.
4. **Time-lapse share and cloud MQTT**: delete `TimeLapseShare*`, `DeviceVideoDownloader`, `CloudDeviceMqtt*`, `mqtt_client`, `tests/cloud_device_mqtt`; `PrinterMgrView` (section 7).
5. **AccountDeviceMgr** without cloud sync (section 9).
6. **Dependencies**: `src/CMakeLists.txt` OSS/Paho, `src/slic3r/CMakeLists.txt` source lists and App Store block, `tests/CMakeLists.txt`, `deps/CMakeLists.txt`, `deps/paho-mqtt`, `deps/aliyun-oss-cpp-sdk`.
7. **D1** `PrinterCloudAuthDialog` + `PhysicalPrinterDialog`.
8. **D2** web patch 12 (+ `doc/MCP/web-bundle-patches.md` section 12).
9. `localization/i18n/list.txt`, docs: `doc/cleanup-plan-2026-10-20.md` (F4 done), `CLAUDE.md` (Print Advisor architecture: no account; `synced_to_cloud` always false), `doc/MCP/upstream-touch-points.md` if any listed hook moved (none expected).

Then build, grep (section 20), G-code, sidecar, manual and printer checks (section 19).
