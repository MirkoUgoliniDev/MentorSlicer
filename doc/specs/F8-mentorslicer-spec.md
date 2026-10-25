# F8 "MentorSlicer": implementation spec

This is a read-only analysis of branch `lean`, HEAD `6f9e5931` (2026-10-22). I did not edit, build or run anything. I did look at the user's real data folder (`%APPDATA%\Creality\Creality Print\7.3 Alpha`) and at `~/.claude.json` and `~/.codex/config.toml`, read-only and with tokens redacted. Line numbers are from the current files. **[INFERENCE]** marks anything I did not check directly in code or on disk.

User decisions (closed):

1. The data folder becomes `%APPDATA%\MentorSlicer`. On first start the app copies `%APPDATA%\Creality\Creality Print\7.3 Alpha` once and leaves the original untouched.
2. Full rename: `MentorSlicer.exe`, `MentorSlicer_Slicer.dll`, MCP server `mentorslicer-advisor` (the sidecar stays in `tools/print-advisor-mcp`), plus build scripts, VS Code tasks, `tools/*.ps1`, verify tooling and the Claude Code registration (both project keys).
3. No update check (the app stays offline).

Logo, splash and exe icon are already done (`02742f3a`). Only the icon *file names* still say `CrealityPrint*`.

Key findings that shape the design:

- **There is already a transactional data-folder migration.** It lives in `src/libslic3r/DataDirectoryMigration.cpp` and is called from `GUI_App::init_app_config()` (`GUI_App.cpp:2312-2324`). It refuses any target whose folder name is not `"7.3 Alpha"` (`DataDirectoryMigration.cpp:351-352`). It also hardcodes `Creality.conf` (`:371-372`, `:385`, `:443-444`). So F8 has to extend it; it cannot simply be pointed at a new path.
- **A migration error silently ends the app.** `GUI_Init.cpp:399-402` catches `DataMigration::Error`, logs it and returns 1. The log is not configured yet at that point (`set_log_path_and_level` runs later, `GUI_App.cpp:2335`). The user sees nothing.
- **Changing `SLIC3R_APP_NAME` silently changes two external behaviours unless they are pinned:** the G-code config loader (`Config.cpp:1565-1566`) and the HTTP user agent (`Http.cpp:215`). The G-code header itself is a hardcoded literal and does not change (`utils.cpp:1232-1234`).
- **The 3MF `Application` tag is a hardcoded literal `"Creality_Print"`** (`bbs_3mf.cpp:396`, `:6940`, `:8011`). Projects written by MentorSlicer and old projects keep loading as long as it stays.

---

## Part A. Where the name is defined or derived

### A.1 Build identity (root of everything)

| Where | Today | What derives from it | F8 value |
|---|---|---|---|
| `version.inc:4` `SLIC3R_APP_NAME` | `CrealityPrint` | `libslic3r_version.h.in:5`, which feeds everything in A.2a | `MentorSlicer` |
| `version.inc:5` `SLIC3R_APP_KEY` | `Creality` | `libslic3r_version.h.in:6`, which feeds A.2b | `MentorSlicer` (see question F-1) |
| `version.inc:6` `SLIC3R_APP_USE_FORDER` | `Creality Print` | data path subfolder (`utils.cpp:335-337`), crash dir (`CrealityPrint.cpp:491-493`, `CxMiniDump.cpp:109`, `GUI_Init.cpp:66` macOS) | `MentorSlicer` (only still used with an explicit `--datadir`, see C.2) |
| `version.inc:7-9` `PROCESS_NAME` | `CrealityPrint` | `STARTUP_PROJECT_NAME` (`:11`, `${PROCESS_NAME}_app_gui`), `SOLUTION_NAME` (`:12`, so `project()` at `CMakeLists.txt:64` produces `CrealityPrint.sln`), `PROJECT_DLL_NAME_WIN32` (`cmake/BuildInfoUtil.cmake:94`, `${PROCESS_NAME}_Slicer`), exe `OUTPUT_NAME` (`src/CMakeLists.txt:430`), `SLIC3R_PROCESS_NAME` (`libslic3r_version.h.in:8`) | `MentorSlicer` |
| `version.inc:10` `VENDOR_NAME` | `Creality` | CPack vendor and install dir only (`package/package.cmake:74,85`, `squirrel.nuspec.in:7-8`) | optional, installer step E5 |
| `CMakeLists.txt:4` `CREALITYPRINT_DATA_VERSION` | `7.3` | `data_directory_name()` (`utils.cpp:310-315`), migration manifest check (`DataDirectoryMigration.cpp:361`) | **keep** `7.3` (the imported manifest says `target_data_version: "7.3"`) |
| `cmake/BuildInfoUtil.cmake:64-65` `PROJECT_VERSION_EXTRA` | `Alpha` | `" Alpha"` suffix of the data folder (`utils.cpp:313`), 3MF `AppStage` (`bbs_3mf.cpp:9415`), Application tag (`:6940`), crash dump names, `SHIPPING_LEVEL` | **keep** `Alpha` (after F8 the default data path no longer depends on it) |
| `src/libslic3r/libslic3r.h:5` `SLIC3R_APP_FULL_NAME` | `"Creality Print"` | About dialog (`AboutDialog.cpp:49`), "&About %s" (`MainFrame.cpp:3267`), error captions (`CrealityPrint.cpp:274-277`, `MsgDialog.cpp:353-354`), `Plater.cpp:6222,8119,8142`, `SysInfoDialog.cpp:41,107`, `UpdateDialogs.cpp:41,263`, `CreatePresetsDialog.cpp:1112,1121`, `Config.cpp:1598` | `"MentorSlicer"` |
| `libslic3r.h:6-7` `GCODEVIEWER_APP_NAME/KEY` | `Creality3DSlicer...` | G-code viewer mode (not used by the fork) | leave |
| `src/libslic3r/buildinfo.h:33` (**tracked, stale**, dated 26-06-02, `SOURCE_ROOT "F:/C3DSlicer"`) | `"CrealityPrint_Slicer"` | sources in `src/libslic3r` that include `"buildinfo.h"` find this file first (same directory) | edit line 33 to `"MentorSlicer_Slicer"` so grep is clean. The launcher does **not** use this file, see next row |
| generated `build/src/libslic3r/buildinfo.h` (from `cmake/buildinfo.h.in:33`, regenerated because `GENERATE_ORCA_HEADER` is on, `BuildInfoUtil.cmake:102-108`) | `CrealityPrint_Slicer` | the launcher: `CrealityPrint_app_msvc.cpp:35` includes `"../libslic3r/buildinfo.h"`. In `build/CrealityPrint_app_gui.vcxproj` the include order is `src; build\src\platform; src\libigl`, so `build\src\platform\..\libslic3r\buildinfo.h` (generated) is found before `src\libigl\..\libslic3r` (tracked). The loaded DLL is `PROJECT_DLL_NAME_WIN32 + ".dll"` (`:385`) | follows `PROCESS_NAME` automatically |

Changing `version.inc` regenerates `libslic3r_version.h`. That means a near-full rebuild, engine included, like a `PrintConfig.hpp` change.

### A.2 What each macro feeds, and what to do

**a) `SLIC3R_APP_NAME` becomes `MentorSlicer`**

| Use | Effect | Action |
|---|---|---|
| `getIconName()` / `getIconName_noTheme()` (`common_header.cpp:39-55`). About (`AboutDialog.cpp:24,56,223,244`), ConfigWizard `:1491`, MainFrame icon `:175`, `:4948`, `Plater.cpp:18597`, 7 dialogs in `CreatePresetsDialog.cpp`, `MsgDialog`, `Preferences`, `SendToPrinter` and others (about 40 sites) load `resources/images/<name>[_128px\|_192px].png/.ico/.svg` | needs the icon files renamed | `git mv` (A.8) |
| `Config.cpp:1565-1566` G-code config loader accepts `"; generated by " SLIC3R_APP_NAME` | would stop recognising `; generated by CrealityPrint` (Creality Print 6.x G-code, see `GCodeProcessor.cpp:40` `CREALITY_TAG_60`) | **pin** the literal `"CrealityPrint"` (B.1) |
| `Http.cpp:215` `CURLOPT_USERAGENT SLIC3R_APP_NAME "/" SLIC3R_VERSION` | the UA sent to the K1C upload (`Klipper4408Interface.cpp:40-50`), Moonraker, OctoPrint | **pin** `"CrealityPrint/"` (B.4) |
| `GCodeProcessor.cpp:1708` producer table | adds "MentorSlicer". `CREALITY_TAG_60/51` still cover the old headers | harmless; leave |
| `utils.cpp:1240-1242` `header_slic3r_generated()`, used for the AppConfig `"header"` (`AppConfig.cpp:1022`), ini export (`:1322`, `Config.cpp:1740`, `PresetBundle.cpp:5789`, `Snapshot.cpp:183`), 3MF `print_profile.config` and embedded presets (`bbs_3mf.cpp:8879`, `:8932`) | text becomes `MentorSlicer 7.3.0.0`. `print_profile.config` is not read on load (`bbs_3mf.cpp:2557` is commented out). I found no reader of the AppConfig `header` (`parse_semver_from_ini`, `GUI_App.cpp:2156`, has no caller) | accept **[INFERENCE: ini/config-bundle import does not check the header line]** |
| `ConfigWizard.cpp:515,2653`, `GUI_App.cpp:6731`, `PhysicalPrinterDialog.cpp:324`, `LegacyRenderer.cpp:3268,3288`, `AdvancedRenderer.cpp:258,309`, `rc.in` `FileDescription/ProductName/InternalName` (`CrealityPrint.rc.in:15-19`) | cosmetic | accept |

**b) `SLIC3R_APP_KEY` becomes `MentorSlicer` (question F-1)**

| Use | Effect |
|---|---|
| `GUI_App.cpp:2292` `SetAppName(SLIC3R_APP_KEY)`, so `wxStandardPaths::GetUserDataDir()` = `%APPDATA%\<AppName>` | becomes `%APPDATA%\MentorSlicer`, exactly the decided folder |
| `AppConfig.cpp:1755,1759` config file `<data_dir>/SLIC3R_APP_KEY ".conf"` | `MentorSlicer.conf`. The import must rename `Creality.conf` and `Creality.conf.bak` (C.3) |
| `bbs_3mf.cpp:9404-9406` 3MF `Company` = `SLIC3R_APP_KEY` | new projects say `Company="MentorSlicer"`. The loader still sets `is_creality` through `Application == "Creality_Print"` (`bbs_3mf.cpp:666-669`) |
| `CLI.cpp:107,110,114,116` CLI default root | change with C.2 |
| `BackgroundSlicingProcess.cpp:1227` temp upload name, `wxExtensions.cpp:381` tooltip | cosmetic |
| `GUI_App.cpp:5903` `GetAvailableTranslations(SLIC3R_APP_KEY)` | today it already queries domain `Creality`, which has no `.mo` **[INFERENCE: returns empty today too]**. No change in behaviour |
| `MainFrame.cpp:4211,4244` Creality 5.x preset import from `GetUserDataDir()/Creative3D/5.0/...` | becomes inert (looks under `%APPDATA%\MentorSlicer`). The imported config has `hasImoport5xPreset` set, so the prompt never shows **[INFERENCE]** |
| `ProfileFamilyLoader.cpp:239` | only under `CUSTOMIZED`. No effect |

**c) `PROCESS_NAME` / `SLIC3R_PROCESS_NAME` becomes `MentorSlicer`**

| Use | Action |
|---|---|
| exe `MentorSlicer.exe`, DLL `MentorSlicer_Slicer.dll`, target `MentorSlicer_app_gui`, `build/MentorSlicer.sln` | automatic. `build_exe_only.bat` must change (E1) |
| `GUI_App.cpp:6149` `m_wxLocale->AddCatalog(SLIC3R_PROCESS_NAME)` and `Preferences.cpp:1330` `GetAvailableTranslations(CxBuildInfo::getProjectName())` (= `SLIC3R_PROCESS_NAME`, `common_header.cpp:13`) | **pin the gettext domain to `"CrealityPrint"`**. The 23 `resources/i18n/*/CrealityPrint.mo` files (untracked, `*.mo` is in `.gitignore:25`), `localization/i18n/*/CrealityPrint*.po`, `CMakeLists.txt:673-700`, `run_gettext.bat`, `build_exe_only.bat:172-175` and `HintsToPot.cpp:63` all use that name. Add `#define SLIC3R_I18N_DOMAIN "CrealityPrint"` (in `libslic3r.h`) and use it at both sites. Without it the Italian UI falls back to English |
| crash dump names `CrealityPrint.cpp:511,770`, `GUI_Init.cpp:201`, `CxMiniDump.cpp:140`. Minidump relaunch `CrealityPrint.cpp:529-531` (`%s.exe "minidump://..."`) | automatic and correct |
| `rc.in:24` `OriginalFilename "@PROCESS_NAME@.exe"` | automatic |

### A.3 Data-folder derivation (all sites)

| Site | Today |
|---|---|
| GUI: `GUI_App::init_app_config()` `:2301-2305`: `root = GetUserDataDir()` (= `%APPDATA%\Creality`), then `set_data_dir(root)` (not direct), which appends `"\" SLIC3R_APP_USE_FORDER "\" data_directory_name()` (`utils.cpp:317-346`) | `%APPDATA%\Creality\Creality Print\7.3 Alpha` |
| `utils.cpp:343-345` | `set_data_dir` creates the folder immediately |
| CLI: `CLI.cpp:93-123` `initialize_cli_data_directory`: `%APPDATA%/SLIC3R_APP_KEY`, then `set_data_dir` (not direct). `--datadir X` means `X\Creality Print\7.3 Alpha` (`CLI.cpp:96-97`, `GUI_Init.cpp:98`) | same layout |
| Windows Breakpad callback `CrealityPrint.cpp:487-493`: `GetUserDataDir() + USE_FORDER + version + "\log"` | rebuilt by hand, so it breaks after F8 unless rewritten |
| `CxMiniDump.cpp:95-115` (hardcoded `"\\Creality\\"` and `MAJOR + ".0"`, already out of sync) | dead: `MiniDump::EnableAutoDump` is commented out (`CrealityPrint.cpp:478`) |
| macOS `GUI_Init.cpp:47-67`, Linux Breakpad `CrealityPrint.cpp:735-746` | not built |
| `GUI_App.cpp:2832` `applock.data`, `InstanceCheck.cpp:357` single-instance lock `data_dir()\cache\<hash>.lock`, Print Advisor descriptor `PrintAdvisorServer.cpp:2769,2816-2822`, `PrintAdvisorInternal.cpp:184,189`, `PrintAdvisorVerdict.cpp:81,84` | all follow `data_dir()` |

### A.4 Single instance and IPC

- `InstanceCheck.cpp:312-375`: the lock name is a hash of the **exe path**, `wxSingleInstanceChecker` in `data_dir()\cache\`. Name-neutral, and the dev and stable copies stay distinct.
- **Window-title coupling (must change together):**
  - the title is set at `Plater.cpp:14348` (`m_project_name + " - CrealityPrint"`);
  - the "other instance" finders look for `L"CrealityPrint"` in the window text: `InstanceCheck.cpp:116` (file hand-off to the running instance) and `GUI_App.cpp:1568` (WM_COPYDATA preset sync, `:1617-1640`).
  - Change all three to `MentorSlicer`. A side effect is wanted: an old `CrealityPrint` window and MentorSlicer no longer exchange messages, and their data folders are different anyway.
- Linux DBus names `io.github.crealityofficial.CrealityPrint...` (`InstanceCheck.cpp:248-251,600,638,658`) and the Flatpak id (`GUI_App.cpp:614-615`): not built, leave.

### A.5 Registry, file associations, URL schemes

- On Windows, `associate_files` (`GUI_App.cpp:7682-7706`), `disassociate_files` (`:7708`), `associate_url` (`:7761-7787`) and `check_url_association` (`:7741`) all `return` before touching the registry. **The app writes no registry keys.** `is_associate_files` (`:806-827`) only reads `HKCU\Software\Classes\.<ext>` for `" Orca.Slicer.1"`.
- Dead literals (`prog_id " Creality.Slicer.1"`, `prog_desc L"CrealityPrint"` at `:7690-7691,7717-7718`): rename to keep grep clean, or delete with the dead bodies.
- URL prefixes `crealityprintlink://` (`Utils.hpp:244`, `Downloader.cpp:148`, `InstanceCheck.cpp:78`, `GUI_App.cpp:7809`) and `crealityprint://` (`InstanceCheck.cpp:551`, `GUI_App.cpp:6450,6498`): they parse incoming links, but nothing registers them on Windows. Keep (B.6).
- Installer registry: `package/package.cmake:34,103` (`CrealityPrint-<major>`), `package/webview2_edge_fixed_patch.nsi` (a Creality patch for the official install). Neither is used by the fork's workflow (publish-stable copies files).

### A.6 WebView2, temp, logs

- WebView2 user data lives in `%TEMP%\slicer_webview_userdata\<profile>` (`Widgets/WebView.cpp:424-480`). Name-neutral, outside the data folder, shared by every build. No change.
- Temp backup folder `crealityprint_model` (`Model.cpp:1118`), referenced by `last_backup_path` in the conf (absolute, so recovery still finds old backups) and by `tests/print-advisor/make-fixture.mjs:7,27`. Optional rename to `mentorslicer_model`; make-fixture should then look in both.
- Other temp names: `CrealityPrint.cpp:399` `crealityprint_invalid_param_stack.log`, `CrealityrintDump.cpp:386` zip name. Optional.
- Dead: `GUI_App.cpp:1345-1360` `get_local_update_work_dir` (`crealityprint_squirrel`) has no caller (F1 leftover). Delete.
- Logs: `debug_<date>_<pid>.log` in `data_dir/log` (`GUI_App.cpp:2328-2335`). Name-neutral.

### A.7 User-visible names

- Window title: A.4.
- About:
  - `AboutDialog.cpp:249` literal `"Creality Print"`;
  - `:280`, `:293` `_L("Creality Print ")`;
  - `:155`, `:157` `_L("CrealityPrint is licensed/based on ...")`.
- Translatable strings that name the product: 34 matches (`git grep -E "(_L|_u8L|L)\(\"[^\"]*(Creality Print|CrealityPrint)"`).
  - **Keep** the ones that refer to the printer host type or to Creality projects: `OctoPrint.cpp:1197,1202`, `PrintConfig.cpp:1053,1070,4918` (the "Creality Print" host tooltips, see B.5), `Check3mfVendor.cpp:557`, `bbs_3mf.cpp:4825-4833`.
  - **Rename** the ones that refer to this app: `GUI_App.cpp:2205,2225,3246,5533`, `GUI_Init.cpp:405,408`, `Snapshot.cpp:596-597`, `GUI.cpp:381`, `Plater.cpp:8086,16487,22573`, `Preferences.cpp:1039`, `PrintHostDialogs.cpp:47`, `SysInfoDialog.cpp:151`, `TransmittanceDialog.cpp:448`, `WipeTowerDialog.cpp:634`.
  - Also the non-translated captions `GUI_App.cpp:3041,6132` and `WebGuideDialog.cpp:158`.
- Print Advisor C++ texts: 66 matches in `simple/printAdvisor/*` (42 in `PrintAdvisorServer.cpp`), plus `PrintAdvisorMonitor.cpp`.
- Translation impact: changing an `_L()` msgid loses its Italian text unless the new msgid is added to `localization/i18n/it/PrintAdvisor_it.po`. That file is merged with precedence over Creality's file (`build_exe_only.bat:166-197`).

### A.8 Resource files loaded by name

- Tracked `resources/images/CrealityPrint*` (14 files):
  - `CrealityPrint.ico`, `.png`, `.svg`, `.icns`;
  - `CrealityPrint_32px.png`, `_128px.png`, `_192px.png`, `_192px_grayscale.png`, `_192px_transparent.png`;
  - `CrealityPrint-mac_128px.png`, `CrealityPrint-mac_256px.ico`;
  - `CrealityPrint_gradient.svg`, `_gradient_circle.svg`, `_gradient_narrow.svg`.
- The three `_gradient*` files have **no reference** outside themselves: delete.
- `CrealityPrint.svg` and `.icns` were **not** regenerated in `02742f3a` (not in its file list). Replace their content from `doc/branding/mentorslicer-icon.svg` when renaming **[INFERENCE: the About logo, `ScalableBitmap(..., getIconName(), 32)` at `AboutDialog.cpp:244`, may still show the Creality logo today]**.
- Explicit literals that need the new names:
  - `GUI_App.hpp:322` `logo_name()` (used by `SysInfoDialog.cpp:98`);
  - `UpdateDialogs.cpp:120`;
  - `StepMeshDialog.cpp:87` (`CrealityPrint-mac_256px.ico`);
  - `MainFrame.cpp:294` (macOS);
  - `DesktopIntegrationDialog.cpp:296,308,390-404` (Linux);
  - `CMakeLists.txt:1019` (Linux install);
  - `src/platform/osx/Info.plist.in:10,64,81`, `flatpak/*.yml`.
- The exe icon comes from `package/app_ico/app_link.ico` (`CrealityPrint.rc.in:1`), already rebranded, neutral name.

### A.9 Localization domains

See A.2c: keep `CrealityPrint` as the gettext domain through a pinned constant. Renaming 23 catalogs and every `.po`/`.pot` gives the user nothing.

### A.10 Installer (`package/`, not built by the fork's workflow)

`package.cmake:2` (`DESKTOP_LINK_NAME "Creality Print"`), `:84`, `:93`, `:103`, `:107`; `macx.cmake:1-3`; `squirrel.cmake:4,9,75`; `webview2_edge_fixed_patch.nsi` (all of it). Optional step E5.

### A.11 Tooling that hardcodes names or paths

| File | Lines |
|---|---|
| `build_exe_only.bat` | 1, 27-28 (comment "7.3 Alpha"), 145-149 (`CrealityPrint_app_gui`), 156 (`CrealityPrint.exe`). The `.po`/`.mo` names at 167-175 stay (domain pinned) |
| `build_release_vs2022.bat`, `build_release_vs2022_progress.bat` | comments only (`:7`, `:8`). `OrcaSlicer_dep` and `./OrcaSlicer` are dependency/install folders: leave |
| `.vscode/tasks.json` | 5, 20-21, 27, 34-35, 48, 57, 63, 65, 68 |
| `tools/close-build-instance.ps1` | 1-17 (comments), 33 (`$DataDir`), 46 (`Get-Process CrealityPrint`), 113-121 (version-folder scan), messages |
| `tools/publish-stable.ps1` | 5, 18-22, 26 (`C:\CrealityPrint-Advisor`), 33, 43, 66, 108, 119-126 |
| `tools/net-watch.ps1` | 1-7, 49 (`Name='CrealityPrint.exe'`), 93-94 |
| `tools/verify-phase.mjs` | 10, 101-104 (`runningCrealityPrint`), 128-157 (exe/DLL names), 176, 401-407 (`newestEndpoint`: scans `APPDATA\Creality\Creality Print\*`), 436-446, 461 |
| `tests/regression/slice-fixtures.mjs` | 8, 54, 83-85 |
| `tests/print-advisor/make-fixture.mjs` | 7, 27, 43 |
| `tools/print-advisor-mcp/**` | Part D |
| `src/libslic3r/DataDirectoryMigration.cpp:139-140,148,158` | process names `CrealityPrint.exe`, `CrealityPrint_app_gui.exe` |
| `src/slic3r/Utils/Process.cpp:42` (and `:105`, non-Windows) | "open in a new instance" launches the hardcoded `"CrealityPrint.exe"`. **Must** become `SLIC3R_PROCESS_NAME ".exe"`, or new windows fail after the rename |
| `src/CrealityPrint_app_msvc.cpp:64,67,407` | OpenGL-probe window class (internal) and an error message |

---

## Part B. What must NOT change

| # | Item | Evidence | Keep? |
|---|---|---|---|
| B.1 | G-code header `; generated by Creality_Print V<ver>` | literal in `header_slic3r_generated2()` (`utils.cpp:1232-1234`), written at `GCode.cpp:5281,5286`. Read back by our own loaders: `Config.cpp:1569-1571,1588` and `GCodeProcessor.cpp:41,1710` (`CREALITY_TAG_51`, sets `m_is_own_generated_gcode`). The fixture shows it at `tests/print-advisor/fixtures/brim3-soak10.gcode:2` | **Required.** It is not tied to a macro, so it needs no edit. Creality firmware, Creality Cloud and the device page may key on it **[INFERENCE]**. Keeping it also keeps G-code identical to the F0 baseline (`compare-gcode.mjs:36` masks the line anyway) |
| B.1b | `Config.cpp:1565-1566` accepts `"; generated by " SLIC3R_APP_NAME` | after the rename, G-code headed `; generated by CrealityPrint` (6.x, `CREALITY_TAG_60`) would fail `load_from_gcode_file` with "Not a gcode file generated by" | **Required to pin:** replace `SLIC3R_APP_NAME` with the literal `"CrealityPrint"` |
| B.2 | 3MF `Application` = `Creality_Print V<ver> <stage>` and `creality.config` `Application = "Creality_Print"` | literals at `bbs_3mf.cpp:396,6940,8011,9409`. The loader sets `m_is_bbl_3mf` only for `"Creality_Print V..."`, `"BambuStudio-"`, `"OrcaSlicer-"` (`:4790-4813`). `!m_is_bbl_3mf` changes how config and objects load (`:2642,4393,6037`). `is_creality` (`:666-669`) gates the legacy filament migration (`:691-698`). Verified on the user's archived `project.3mf`: `Application">Creality_Print V7.3.0.0 Alpha`, `Company="Creality"` | **Required.** Already a literal; do not "fix" it to `SLIC3R_APP_NAME`. Old projects, the archive's `project.3mf` and future `tests/regression` fixtures keep loading |
| B.3 | 3MF `Company` = `SLIC3R_APP_KEY` | `bbs_3mf.cpp:9404-9406` | Safe to change (the Application check still matches), or keep (F-1) |
| B.4 | HTTP user agent `CrealityPrint/<ver>` | `Http.cpp:215`. Used for the K1C upload (`Klipper4408Interface.cpp:40-50`), Moonraker and OctoPrint uploads | **Safer to keep (pin the literal).** No evidence that the K1C checks it **[INFERENCE]**, but there is nothing to gain from changing it |
| B.5 | Print-host type `crealityprint` / `crealityPrint`, class `CrealityPrint`, label "CrealityPrint" | `PrintConfig.cpp:433,4933,4946`, `OctoPrint.hpp:136-141` | **Required:** saved in printer presets (`host_type`). The strings name a printer protocol, not this app |
| B.6 | URL prefixes `crealityprintlink://`, `crealityprint://` | A.5 | Safer to keep (incoming links from Creality web pages; not registered anyway) |
| B.7 | WebView user agent `Creality-Slicer/v...` | `Widgets/WebView.cpp:558,588-613,1046`, `MainFrame.cpp:2591`. The homepage bundle tests `navigator.userAgent.match(RegExp("Creality-Slicer","i"))` (`resources/web/homepage/assets/index-806b308b.js`), and so does `guide/js/globalapi.js` | **Required** (the Home page and the Add printer/filament windows depend on it) |
| B.8 | Vendor and profile names "Creality", `resources/profiles/Creality*`, `machineList.json`, `CrealityUserMode.json` | `GUI_App.cpp:386,401,4435,4864-4906,5080-5109,5718-5734`, `DataDirectoryMigration.cpp:469-477` | **Required** (preset identity, `inherits`, the user's copies such as "ASA K1C") |
| B.9 | G-code fields `creality_uuid`, `creality_task_id`, `CONFIG_BLOCK`, `printer_model = Creality K1C` | fixture lines 7-8, 28964 | **Required** (not derived from the app name anyway) |
| B.10 | LAN protocol to the printer (`{"method":"get"...}`, WebSocket `wss://<ip>:443`) | no app name in `print_manage/` except through `Http.cpp:215` (B.4) | n/a |
| B.11 | `CREALITYPRINT_VERSION*` macro names, `CREALITYPRINT_DATA_VERSION` | everywhere | keep (identifiers, not identity) |
| B.12 | Env vars `CREALITYPRINT_OPENGL_SOFTWARE_RENDERER/HARDWARE_AVAILABLE` (launcher to DLL) | `CrealityPrint_app_msvc.cpp:374-380`, `OpenGLManager.cpp:119,130,464` | internal; keep, or rename both sides in one commit. Recommended: keep |
| B.13 | Source file names (`src/CrealityPrint.cpp`, `CrealityPrint_app_msvc.cpp`, `CrealityrintDump.*`, `platform/msw/CrealityPrint.rc.in`, `.manifest.in`) | `src/CMakeLists.txt:173-174,188,423` | keep (internal; renaming only adds churn for the `upstream-update` skill) |

---

## Part C. Data migration design (decision 1)

### C.1 Verified starting state

Contents of `%APPDATA%\Creality\Creality Print\7.3 Alpha` (listed 2026-10-22):

- `Creality.conf` and `.bak`, `applock.data`;
- `cache/` (20 MB): `print-advisor/endpoint.json` (present, the app is open), `send_to_printer/user_operation_state.json`, `system_presets/*.cereal`;
- `cloud_download_data/`, `ota/`, `thumbnail_cache/` (all empty);
- `deviceInfo.json`, `privacyInfo.json`, `filament_mixing/`, `filaments_color_initialized_version.txt`;
- `log/` (8.4 MB, 30 files);
- `migration/{manifest.json,report.json}`: `state: COMMITTED`, `target_data_version: 7.3`, `source_directory: ""` (it was a fresh 7.3 init, not an import);
- `preset_name_migration_v2.done`;
- `print-advisor/` (7 MB): `history.jsonl`, `prints/*.jsonl`, `archive/<dir>/{project.3mf, *.gcode, camera.jpg, print-log.jsonl, metadata.json}`;
- `printers/`, `system/` (15 MB), `user/default/{filament,machine,process,local_device}`, `user/hints.cereal`.

The parent folder also holds `6.0`, `7.0`, `7.3`, an abandoned `.7.3 Alpha.staging-17923021581809932` (25 MB, left by a failed earlier `initialize`), and `.migration-*` lock and diagnostics entries. **Do not touch any of them.**

Files inside that hold absolute paths or the old folder name (verified by grep):

- `Creality.conf` `"download_path": "C:\\Users\\<user>\\AppData\\Roaming\\Creality\\Creality Print\\7.3 Alpha\\cloud_download_data"` is the **only** reference to the data folder.
- `last_backup_path` points to `%TEMP%\crealityprint_model\...` (fine). `last_opened_folder` and `recent` are user paths (fine).
- `"header": "CrealityPrint 7.3.0.0"` is informational.
- Print Advisor files store **relative** names: `data_relative_path` (`PrintAdvisorInternal.cpp:117-124`), archive `metadata.json` (`file`, `log`, `project.source` basename, `PrintAdvisorVerdict.cpp:525-552,817-819`), `pending_verdict.json` `log` name (`:236`). Nothing to rewrite.
- `user/**/*.info` hold `sync_info/user_id/setting_id/base_id/updated_time`, no paths.
- WebView2 data is not in the data folder (A.6).
- The conf ends with an MD5 line. AppConfig only logs on a mismatch (`AppConfig.cpp:1171-1175`), and the file is copied byte for byte anyway.

### C.2 Where to hook

The hook is `GUI_App::init_app_config()`, called from the `GUI_App` constructor (`GUI_App.cpp:2074`). It runs before `AppConfig::load()` (`:2361-2363`), before logging (`:2328-2335`), before the instance check (`GUI_Init.cpp:164`), and on the main thread before `wxEntry`, so there is no event loop and no wx windows.

Proposed shape (Windows and macOS branch, `:2301-2324`):

```cpp
    if (data_dir().empty()) {
#ifndef __linux__
        // %APPDATA%\MentorSlicer (wx AppName = SLIC3R_APP_KEY), used as is: no "<folder>\<version>" suffix.
        const std::string root = wxStandardPaths::Get().GetUserDataDir().ToUTF8().data();
        m_app_first_launch = !root.empty() && !fs::exists(root);
        set_data_dir(root, /*use_directly=*/true);
#else
        ... (same with $XDG_CONFIG_HOME/MentorSlicer, use_directly=true)
#endif
        const auto target = std::filesystem::u8path(Slic3r::data_dir());
#ifdef _WIN32
        // F8: one-time copy of the Creality Print 7.3 Alpha folder; the source is never modified.
        DataMigration::ImportOptions import;
        import.source = std::filesystem::u8path(into_u8(wxStandardPaths::Get().GetUserConfigDir())) /
                        std::filesystem::u8path(DataMigration::LEGACY_CREALITY_DATA_RELATIVE);  // "Creality/Creality Print/7.3 Alpha"
        import.target = target;
        import.config_name = SLIC3R_APP_KEY ".conf";
        DataMigration::import_legacy_directory(import);          // throws DataMigration::ImportError; never falls through
#endif
        if (!fs::exists(fs::path(data_dir()) / "migration" / "manifest.json")) {   // fresh install only
            DataMigration::Options migration;  ... as today ...
            migration.target_name = target.filename().u8string();  // new option, replaces the "7.3 Alpha" check
            migration.config_name = SLIC3R_APP_KEY ".conf";        // new option, replaces the "Creality.conf" literals
            DataMigration::initialize(migration);
        }
        ...
```

Notes:

- `GetUserConfigDir()` is `%APPDATA%` on MSW, and `GetUserDataDir()` is `GetUserConfigDir()\<AppName>`. That is wx's documented behaviour **[INFERENCE: not re-checked in the wx sources]**. Both go through the shell known-folder API, so an `APPDATA` env override does not redirect them. Use the same API for source and target.
- `initialize()` is called only when the target has no manifest. Its lock and diagnostics files are created in the **parent** (`DataDirectoryMigration.cpp:355-357`), which is now `%APPDATA%` itself. The guard avoids recreating `%APPDATA%\.migration-MentorSlicer*` on every start. For a fresh install with no Creality data, `initialize` looks for `%APPDATA%\7.0 Alpha` / `6.0 Alpha` (`:380-381`), finds nothing, and does a clean resource install. No 7.0/6.0 import for MentorSlicer, matching decision 1.
- **An import error must not fall through to `initialize`.** Otherwise a later fix would hit "Target contains existing user data" or a half-initialized target.
- `--datadir X` (`m_datadir_redefined`, `:2325-2328`) keeps today's semantics (`X\MentorSlicer\7.3 Alpha` after the `USE_FORDER` change) and never imports. `tests/regression/slice-fixtures.mjs` always passes a fresh `--datadir`, so the regression slices never import.
- CLI (`CLI.cpp:104-121`): root `%APPDATA%\MentorSlicer`, then `set_data_dir(root, true)`. The CLI never imports **[note: a CLI run before the first GUI start makes an empty folder; the GUI import then sees a runtime-only/empty target and still imports, see C.3 step 4]**.
- Windows crash folder (`CrealityPrint.cpp:487-493`): use `Slic3r::data_dir() + "\\log"` when `data_dir()` is set (it is, from the constructor on), and fall back to `GetUserDataDir() + "\\log"`.

### C.3 `DataMigration::import_legacy_directory` (new, `DataDirectoryMigration.{hpp,cpp}`)

Reuse the file's anonymous helpers: `read`, `write` (atomic, flushed), `parse(...,true)`, `check_path` (rejects symlinks and reparse points), `files` (sorted recursive list), `Lock`, `copy_checked` (copy then MD5 compare), `valid_config`, `runtime_only`, `other_slicer_running`.

1. **Already done?** If `target/migration/imported-from-creality.json` exists, or `target/<config_name>` exists, or `target/migration/manifest.json` exists, return. Never import over existing data, including a target created by a fresh `initialize`.
2. **Nothing to import?** If `source` does not exist, return (fresh install path). `check_path(source)`.
3. **Source sanity:** `parse(source/"Creality.conf", true)`, then `valid_config`. On a JSON error try `Creality.conf.bak`. If neither is valid, throw `ImportError` with `"configuration unreadable"` and **no parser excerpt** (it may hold credentials, as `:405` notes). I/O errors are fatal and retried on the next start.
4. **Exclusive and quiet:**
   - take `Lock(parent/".MentorSlicer-import.lock")`;
   - if `other_slicer_running()` reports a `CrealityPrint.exe` (current list, `:139-140`), retry for 5 s, then throw `ImportError("Chiudi Creality Print (anche la copia in C:\\CrealityPrint-Advisor) e riavvia MentorSlicer")`;
   - require `runtime_only(target)` (an empty folder made by `set_data_dir`, or one with only `log`/`cache`/`applock.data`), else throw.
5. **Stage:** `stage = parent/".MentorSlicer.import-<tx>"` (same volume, so the rename is atomic). For each `p` in `files(source)` with `rel = p.lexically_relative(source)`:
   - **skip** and record each reason in the report:
     - `log/**`;
     - `cache/print-advisor/**` (`endpoint.json` holds the **old** app's pid and session token; copying it would make `check_no_other_instance` and the sidecar see a foreign descriptor);
     - `cache/system_presets/**` (regenerable cereal cache **[INFERENCE]**);
     - `print-advisor/tmp/**` (temporary copies from `reopen_archived_project`);
     - `applock.data`, `*.lock`, `*.tmp`;
   - **rename** `Creality.conf` to `<config_name>` and `Creality.conf.bak` to `<config_name>.bak` (when the key changes, F-1);
   - copy everything else with `copy_checked(p, stage/rel')`. That includes `migration/` (Creality's committed manifest, which later keeps `initialize` from running), `preset_name_migration_v2.done`, `user/`, `system/`, `printers/`, `print-advisor/` (history, prints, archive, `pending_verdict.json`), `cache/send_to_printer/`, `deviceInfo.json`, `privacyInfo.json`, `filament_mixing/`, `filaments_color_initialized_version.txt`.
   - Empty folders (`ota`, `thumbnail_cache`, `cloud_download_data`) disappear on their own: `files()` lists only files, and `init_download_path` recreates the one it needs.
6. **Re-check:** `files(source)` equals the list from step 5, and `other_slicer_running()` is false, else throw. A writer that slipped in makes the copy suspect.
7. **Marker:** write `stage/migration/imported-from-creality.json`:

   ```json
   {"source": "<abs source>", "imported_at": "<iso>", "app_version": "7.3.0.0",
    "copied": N, "bytes": B, "renamed": {...}, "skipped": [{"path": "...", "reason": "..."}]}
   ```

   Only relative paths go inside the list, and no file contents.
8. **Commit:** if `target` exists (runtime-only), rename it to `parent/".MentorSlicer.runtime-backup-<tx>"`. Then `rename(stage, target)`, then remove the backup if it is empty (the same pattern as `:503-508`, `cleanup_empty_runtime_backup`).
9. **On any exception:**
   - `remove_all(stage)`, best effort, only if `stage.filename()` starts with `.MentorSlicer.import-`. It is this transaction's own folder.
   - write `parent/".MentorSlicer-import-last-error.txt"`.
   - throw `ImportError` (a new subclass of `DataMigration::Error`).
   - The source is never opened for writing (`copy_checked` reads; `check_path` only stats), so a crash halfway leaves the source intact, the target absent or runtime-only, and at most an orphan `.MentorSlicer.import-*` folder. The next start retries from scratch, and could sweep stale `.MentorSlicer.import-*` folders older than one day.

**"Done" marker:** `migration/imported-from-creality.json` inside the target. It appears atomically with the rename. To re-import on purpose, delete `%APPDATA%\MentorSlicer` (document this in CLAUDE.md).

**Error UI:** in `GUI_Init.cpp:399`, catch `DataMigration::ImportError` **before** the generic `DataMigration::Error` and show it with `::MessageBoxW(nullptr, ..., L"MentorSlicer", MB_ICONSTOP)`. This is plain Win32: wx is not running yet. The text should name the folder and the last-error file. Keep today's silent path for `initialize` errors, or show them the same way (recommended: show them too).

**`download_path` fix:** in `GUI_App::init_download_path()` (`:2175-2193`, called right after `init_app_config`), if `download_path` lies inside the legacy root (case-insensitive prefix compare on normalized paths), set it to `data_dir()/cloud_download_data`. Today the code only resets the path when it does not exist (`:2185-2191`), and the old one still exists.

**Size and time:** about 25 MB and about 2,500 files (mostly `system/`) **[INFERENCE: count]**, copied synchronously on the main thread before the splash: a few seconds, once.

### C.4 Coexistence with the old stable copy `C:\CrealityPrint-Advisor`

- The old copy (`CrealityPrint.exe`, Creality folder) keeps writing to `...\7.3 Alpha`. After the import the two copies **diverge**: profiles saved in one are not in the other. Document this. The plan is to stop using the old copy once MentorSlicer is published (F-2).
- **The MCP endpoints no longer collide by path.** The old copy writes `...\7.3 Alpha\cache\print-advisor\endpoint.json`; MentorSlicer writes `%APPDATA%\MentorSlicer\cache\print-advisor\endpoint.json` (`PrintAdvisorServer.cpp:2769,2816`). `check_no_other_instance()` (`:2767-2817`) reads only its own path and needs `runs_same_executable` (same image file name, `:291-308`), so it would **not** see the old copy. Both could serve MCP at once. The sidecar is registered to one path and reaches only MentorSlicer, but both apps could run printer monitors.
- **Recommended:** extend `check_no_other_instance()` with a second probe of the legacy descriptor (path from the same `LEGACY_CREALITY_DATA_RELATIVE` constant). Refuse with `PrintAdvisorOtherInstance` and an Italian message ("Print Advisor MCP è attivo nella vecchia Creality Print (PID …)") when all three hold:
  - the pid is alive (`process_alive`);
  - its image file name is `CrealityPrint.exe` (a variant of `image_name`);
  - `loopback_port_answers(port)`.
- The reverse direction (old app checking MentorSlicer) cannot be added without rebuilding the old copy. The one-way check plus "one window at a time" is enough **[INFERENCE]**.
- `pending_verdict.json` is copied. If the user later opens the old copy, both may ask the same verdict once. Accepted.
- Shared outside the data folder: the WebView2 profile in `%TEMP%` (claimed per process, `WebView.cpp:459-471`) and the local HTTP port (fallback over 20 ports, `HttpServer.cpp:400-410`). Same as dev and stable today.

---

## Part D. Sidecar (`tools/print-advisor-mcp`)

Verified today:

- **Descriptor path:** env var only. `appClient.ts:26-28` reads `process.env.CREALITYPRINT_ADVISOR_DESCRIPTOR` and fails if it is unset. It validates `base_url` as `http://127.0.0.1:<port>/`, a token, and `api_version 1` (`:53-54`).
- **Registration:** `~/.claude.json` has `mcpServers["creality-print-advisor"]` under **both** `<repo>` and `<repo>`. Same `node <tsx cli.mjs> <src/index.ts>`, env `CREALITYPRINT_ADVISOR_DESCRIPTOR=%APPDATA%\Creality\Creality Print\7.3 Alpha\cache\print-advisor\endpoint.json`.
- No `allowedTools` or `mcp__creality-print-advisor__*` rules in `~/.claude.json`, `~/.claude/settings*.json` or the repo `.claude/`.
- **Codex:** `~/.codex/config.toml` has no Print Advisor server (only `node_repl`). Only the README example changes.
- Package name `creality-print-advisor-mcp` (`package.json:2`, `package-lock.json:2,7`). Server name `creality-print-advisor`, version `0.1.0`, and the `instructions` text "Print Advisor for Creality Print..." (`src/index.ts:275-276`).
- "Creality Print" appears in user-facing texts: `appClient.ts` (15; `notRunning` `:13`, `invalidDescriptor` `:14`, timeouts `:118-119`, 409/503 `:134-137`, `confirmationNote` `:152`), `index.ts` (20), `tools/*.ts` (20), `errorCodes.ts` (3).
- `scripts/smoke.mjs:15,56-63` (`findDescriptor` scans `APPDATA/Creality/Creality Print/*/cache/print-advisor/endpoint.json`), `:47` `NOT_RUNNING = 'Creality Print non è aperto'`, which must match `appClient.ts:13`, and `:103`.
- `scripts/tools-expected.json` lists the 50 tool names only. **No change** (tool names are unchanged).

Changes:

1. `package.json` / `package-lock.json`: `"name": "mentorslicer-advisor-mcp"` (3 places). No `npm install` is needed.
2. `src/index.ts:275`: `new McpServer({ name: 'mentorslicer-advisor', version: '0.2.0' }, …)`. In the `instructions` text replace "Creality Print" with "MentorSlicer".
3. Env var `MENTORSLICER_ADVISOR_DESCRIPTOR` (`appClient.ts:27-28`, `smoke.mjs`, `verify-phase.mjs:437`). **No fallback to the old name:** a stale registration would otherwise talk to the old app. The messages say "MentorSlicer non è aperto…". Keep the smoke `NOT_RUNNING` constant in sync.
4. `smoke.mjs` `findDescriptor` and `verify-phase.mjs` `newestEndpoint`: a single path, `join(APPDATA,'MentorSlicer','cache','print-advisor','endpoint.json')`. There are no version folders any more.
5. README: title, every "Creality Print", the registration command, `claude mcp get mentorslicer-advisor`, the data folder `%APPDATA%\MentorSlicer`, the Codex example (`[mcp_servers.mentorslicer_advisor]`).
6. **Claude Code registration** (outside the repo; the user must agree to the edit; back up `~/.claude.json` first). In **both** project keys:
   - delete `creality-print-advisor`;
   - add `mentorslicer-advisor` with the same `command`/`args` (the folder stays `tools/print-advisor-mcp`);
   - set env `MENTORSLICER_ADVISOR_DESCRIPTOR=%APPDATA%\MentorSlicer\cache\print-advisor\endpoint.json`.

   `claude mcp remove/add` with the default `local` scope only touches the key that matches the current directory spelling, so either run it twice (from `C:\…` and from `c:\…`) or edit the JSON with a small node script. Tool names in sessions become `mcp__mentorslicer-advisor__*`.

---

## Part E. Ordered steps (each step builds; only E1 needs the C++ build)

### E0. Preparation (no code)

- Close every Creality Print, dev and stable.
- Back up the source folder, e.g. `robocopy "%APPDATA%\Creality\Creality Print\7.3 Alpha" <backup>\CP-7.3-Alpha-2026-10-22 /E`.
- Save a hash list so you can prove it stays untouched: `Get-ChildItem -Recurse -File | Get-FileHash` into the scratchpad, excluding `log\`.
- Write `tools/verify/removed-F8.txt` (E4) now, as the protocol requires.

### E1. C++ identity, data folder, import (one build, near full rebuild)

Files:

- `version.inc` (`SLIC3R_APP_NAME`, `SLIC3R_APP_KEY`, `SLIC3R_APP_USE_FORDER`, `PROCESS_NAME` set to `MentorSlicer`); `src/libslic3r/libslic3r.h:5` (`SLIC3R_APP_FULL_NAME "MentorSlicer"`, add `SLIC3R_I18N_DOMAIN "CrealityPrint"`).
- `src/libslic3r/DataDirectoryMigration.hpp/.cpp`: `Options::target_name`, `Options::config_name` (replace the literals at `:352`, `:371-372`, `:443-444`); `LEGACY_CREALITY_DATA_RELATIVE`; `ImportOptions`, `ImportError`, `import_legacy_directory` (C.3); add `MentorSlicer.exe` to `other_slicer_running` for `initialize` only, and keep `CrealityPrint.exe` for the import. `tools/data_migration/main.cpp` keeps compiling (defaults).
- `src/slic3r/GUI/GUI_App.cpp`:
  - `init_app_config` (C.2) and `init_download_path` (C.3);
  - `:6149` uses `SLIC3R_I18N_DOMAIN`;
  - `:1568` `L"MentorSlicer"`;
  - `:3041`, `:6132` captions;
  - `:1345-1360` delete the dead function;
  - `:7690-7718` dead literals;
  - `:1746-1747` comments;
  - `:2205,2225,3246,5533` strings (F-3).
- `GUI_App.hpp:322` (`logo_name`).
- `GUI_Init.cpp:399-408` (ImportError message box, captions).
- `InstanceCheck.cpp:116`.
- `Plater.cpp:14348` (`" - MentorSlicer"`) and `8086,16487,22573` (F-3).
- `Preferences.cpp:1330` (`SLIC3R_I18N_DOMAIN`) and `:1039`.
- `src/slic3r/CLI/CLI.cpp:104-121,175-176`.
- `src/CrealityPrint.cpp:487-493` (crash dir) and `:176-180,399` optional.
- `src/CrealityPrint_app_msvc.cpp:407`.
- `src/slic3r/Utils/Process.cpp:42`.
- `src/slic3r/Utils/Http.cpp:215` (pin).
- `src/libslic3r/Config.cpp:1565-1566` (pin).
- `src/libslic3r/buildinfo.h:1-2,33,40`.
- `AboutDialog.cpp:155,157,249,280,293`.
- `UpdateDialogs.cpp:120`, `StepMeshDialog.cpp:87`, `MainFrame.cpp:294`.
- `simple/printAdvisor/*.cpp/.hpp` texts and the legacy-descriptor probe (C.4); `print_manage/App/PrintAdvisorMonitor.cpp` texts.
- Optional: `Model.cpp:1118` plus `tests/print-advisor/make-fixture.mjs`.
- `src/platform/msw/CrealityPrint.rc.in:13,20` (`CompanyName`, `LegalCopyright`).
- `git mv resources/images/CrealityPrint*` to `MentorSlicer*` (11 files; delete the 3 `_gradient*`). Replace the `.svg` and `.icns` content from `doc/branding`. Update `CMakeLists.txt:1019` (Linux).
- `localization/i18n/it/PrintAdvisor_it.po`: Italian for every changed msgid.
- `build_exe_only.bat` (needed by this very build):
  - `:145-149` target `MentorSlicer_app_gui`, `:156` `MentorSlicer.exe`, comments `:1,27-28`;
  - force a configure when the cache predates F8. `cmake --build --target` in a stale cache would look for the old `.sln`/target. Add before `:126`:

    ```bat
    findstr /R /C:"^CMAKE_PROJECT_NAME:STATIC=MentorSlicer$" CMakeCache.txt >nul
    if errorlevel 1 (
        echo CMake cache predates the MentorSlicer rename: reconfiguring.
        goto :configure
    )
    ```

  - Today the cache says `CMAKE_PROJECT_NAME:STATIC=CrealityPrint` (`build/CMakeCache.txt:263`).
- Comments in `build_release_vs2022.bat:7` and `_progress.bat:8`.

Verification:

1. Build with the "Build EXE" task (the close task still targets `CrealityPrint`, which is right for this one build).
2. `build\src\Release\MentorSlicer.exe` and `MentorSlicer_Slicer.dll` exist. `findstr /m /c:"MentorSlicer_Slicer" build\src\Release\MentorSlicer.exe` finds the string (UTF-16, so search with `/c:` on the wide form, or use PowerShell `Select-String -Encoding Unicode`). Delete the stale `CrealityPrint.exe`, `CrealityPrint_Slicer.dll` and `.pdb` in `build\src\Release`.
3. First start with no Creality Print running:
   - `%APPDATA%\MentorSlicer` exists with `MentorSlicer.conf`, `user\default\…`, `print-advisor\…`, `migration\imported-from-creality.json`;
   - `cache\print-advisor\endpoint.json` is absent until the MCP server starts, and then it holds the new pid;
   - no `%APPDATA%\.migration-MentorSlicer*`;
   - the Creality folder's hash list equals E0.
4. Start with the old Creality Print open: the message box, no target created. Close it and start again: the import runs.
5. App: Italian UI, title `<project> - MentorSlicer`, icons in the title bar, About and dialogs, the same selected printer, filament and process, "ASA K1C - pezzi grandi" present. A second launch with a `.3mf` argument opens it in the first window. File > New window starts `MentorSlicer.exe`.
6. G-code: `node tools/verify-phase.mjs --phase F8 --skip sidecar,smoke --slice` (or plan steps 1-3), identical to F0. Also open the archived `project.3mf` (File > Open, or `reopen_archived_project` after E3): it loads with its presets.
7. Preferences > download path points into `%APPDATA%\MentorSlicer\cloud_download_data`.

### E2. Scripts and tasks

- `tools/close-build-instance.ps1`:
  - `Get-Process MentorSlicer, CrealityPrint` (path-prefix filtered, so the stable `C:\CrealityPrint-Advisor` is untouched);
  - `$DataDir` default `Join-Path $env:APPDATA 'MentorSlicer'`;
  - the candidate list becomes `[MentorSlicer endpoint, legacy ...\Creality Print\7.3 Alpha endpoint]` while the old copy exists. This file is in `allow:`. Drop the generic version-folder scan.
- `tools/publish-stable.ps1`: default `-Dest C:\MentorSlicer` (F-2), `MentorSlicer.exe`, `MentorSlicer_Slicer.dll`, stamp "MentorSlicer - copia stabile", shortcut "MentorSlicer.lnk" (target and icon `MentorSlicer.exe`), the header comment about the shared data folder.
- `tools/net-watch.ps1`: `Name='MentorSlicer.exe'`, texts.
- `.vscode/tasks.json`: labels "Chiudi MentorSlicer di build", "Build EXE MentorSlicer", "Build Install MentorSlicer", "Pubblica stabile MentorSlicer", "Avvia MentorSlicer"; `dependsOn`; tooltips; `command`/`cwd` of the start task `build/src/Release/MentorSlicer.exe`; status bar "$(play) Avvia MS". Then **Developer: Reload Window**.
- `tools/verify-phase.mjs` (A.11 lines), `tests/regression/slice-fixtures.mjs:8,54,83-85`.

Verification:

- "Avvia" starts the dev build. "Build EXE" with the dev app open closes it gracefully. With an active preheat, the close task refuses (GET `/v1/monitor_status`).
- "Pubblica stabile" creates `C:\MentorSlicer` and the shortcut. `stable-version.txt` says complete.
- `net-watch.ps1` finds the process.
- `node tools/verify-phase.mjs --phase F8 --skip smoke` passes the build and running checks.

### E3. Sidecar and registration

- Files: `tools/print-advisor-mcp/package.json`, `package-lock.json`, `src/index.ts`, `src/appClient.ts`, `src/tools/*.ts`, `src/errorCodes.ts`, `scripts/smoke.mjs`, `scripts/extract-error-codes.mjs` (comments), `README.md`.
- Then `~/.claude.json` (D.6, with the user's consent).

Verification:

- `npm run typecheck`, `npm run smoke:offline`.
- With the dev build open and Vista > Enable Print Advisor MCP: `claude mcp get mentorslicer-advisor` reports Connected, and `npm run smoke` passes with 50 tools matching `tools-expected.json`.
- `verify-phase` smoke says "descriptor pid is the dev build".
- A new Claude Code session calls `get_print_context`, `get_change_history` (old entries visible) and `list_archived_prints` (the 2026-10-20 archive visible).
- With the old Creality Print open and its Print Advisor on: enabling the Print Advisor in MentorSlicer shows the "vecchia Creality Print" message (C.4).

### E4. Docs and the removed-names list

- Docs: `CLAUDE.md` (buttons, `build\src\Release\MentorSlicer.exe`, the stable copy folder, data folder `%APPDATA%\MentorSlicer`, `endpoint.json`, server name, the "7.3 Alpha depends on Alpha" note now only for `--datadir`, how to re-import); `README.md`; `doc/MCP/printer-test-checklist.md:8`; `tools/verify/README.md` (nothing); the F8 entry in `doc/cleanup-plan-2026-10-20.md`; the memory status file.
- `tools/verify/removed-F8.txt` (`allow:` applies to the whole list):

```
# F8 - MentorSlicer rename (doc/specs/F8-mentorslicer-spec.md). One entry per line; "#" starts a comment.
# Kept on purpose (not listed): Creality_Print (G-code header, 3MF Application tag), "generated by CrealityPrint"
# (CREALITY_TAG_60) and the pinned "CrealityPrint" in Config.cpp/Http.cpp, the print-host type crealityprint/
# htCrealityPrint/class CrealityPrint, vendor "Creality" and its profiles, CREALITYPRINT_VERSION*/DATA_VERSION,
# crealityprintlink:// and crealityprint://, CREALITYPRINT_OPENGL_* env vars, the "Creality-Slicer" web user agent,
# the gettext domain CrealityPrint (.po/.mo, SLIC3R_I18N_DOMAIN), source file names (src/CrealityPrint*.cpp, *.rc.in).
re:CrealityPrint\.exe
word:CrealityPrint_Slicer
word:CrealityPrint_app_gui
word:CREALITYPRINT_ADVISOR_DESCRIPTOR
re:creality-print-advisor
re:CrealityPrint-Advisor
re:Creality Print Advisor
re: - CrealityPrint"
re:L"CrealityPrint"
re:Creality\\Creality Print|'Creality', 'Creality Print'
re:7\.3 Alpha
re:Creality Print non
re:crealityprint_model
re:crealityprint_squirrel
file:resources/images/CrealityPrint.ico
file:resources/images/CrealityPrint.png
file:resources/images/CrealityPrint.svg
file:resources/images/CrealityPrint.icns
file:resources/images/CrealityPrint_32px.png
file:resources/images/CrealityPrint_128px.png
file:resources/images/CrealityPrint_192px.png
file:resources/images/CrealityPrint_192px_grayscale.png
file:resources/images/CrealityPrint_192px_transparent.png
file:resources/images/CrealityPrint-mac_128px.png
file:resources/images/CrealityPrint-mac_256px.ico
file:resources/images/CrealityPrint_gradient.svg
file:resources/images/CrealityPrint_gradient_circle.svg
file:resources/images/CrealityPrint_gradient_narrow.svg
allow:src/libslic3r/DataDirectoryMigration.cpp      # legacy exe name and folder for the one-time import
allow:src/libslic3r/DataDirectoryMigration.hpp      # LEGACY_CREALITY_DATA_RELATIVE
allow:tools/close-build-instance.ps1                # closes/probes the old-name build exe and legacy endpoint during the transition
allow:tests/print-advisor/fixtures/brim3-soak10.expected.json   # historical "_source" text
allow:package                                       # installer not migrated unless E5 is done
```

Today's hit counts are a baseline for the edit. They come from `git grep -E` with the protocol's excludes; `word:` entries match a bit less.

| Pattern | Matches | Files |
|---|---|---|
| `CrealityPrint\.exe` | 31 | 11 |
| `CrealityPrint_Slicer` | 7 | 5 |
| `CrealityPrint_app_gui` | 4 | 2 |
| `CREALITYPRINT_ADVISOR_DESCRIPTOR` | 6 | 3 |
| `creality-print-advisor` | 4 | 3 |
| `CrealityPrint-Advisor` | 5 | 2 |
| `Creality Print Advisor` | 4 | 1 |
| ` - CrealityPrint"` | 1 | 1 |
| `L"CrealityPrint"` | 5 | 2 (incl. the comment at `GUI_App.cpp:3202`) |
| `Creality\\Creality Print\|'Creality', 'Creality Print'` | 16 | 5 |
| `7\.3 Alpha` | 4 | 3 |
| `Creality Print non` | 11 | 7 |
| `crealityprint_model` | 3 | 2 |

`CrealityPrint\.exe` also matches the comments at `GUI_App.cpp:1746-1747` and `package.cmake:93`.

Verification: `node tools/verify-phase.mjs --phase F8` (all steps; manual checklist of the plan, plus "title bar MentorSlicer", "About MentorSlicer", "Italian UI").

### E5 (optional, not built or verified by the fork)

- `package/package.cmake`, `macx.cmake`, `squirrel.cmake` names and registry key; decide on `webview2_edge_fixed_patch.nsi` (Creality-specific, drop the `allow:package` line once done).
- Linux/macOS identifiers (`Info.plist.in`, Flatpak, DBus, `DesktopIntegrationDialog`).
- The F8 plan's secret scan (gitleaks) before publishing. Note that the tracked `.claude/settings.json` holds Creality's internal `ssh root@cxagent.crealitycloud.cn …` allow-rules, including an e-mail address.

---

## F. Questions only the user can decide

1. **`SLIC3R_APP_KEY`:**
   - (a) `MentorSlicer` (recommended): `%APPDATA%\MentorSlicer` comes from wx directly, the config becomes `MentorSlicer.conf` (renamed during the import), and new 3MF files say `Company="MentorSlicer"`. Projects still load anywhere through `Application="Creality_Print"`.
   - (b) keep `Creality`: the config stays `Creality.conf` and the target path is built by hand.
2. **Stable copy:**
   - (a) new folder `C:\MentorSlicer` with a new "MentorSlicer" desktop shortcut (recommended). `C:\CrealityPrint-Advisor` stays as a fallback that still uses the Creality folder.
   - (b) reuse `C:\CrealityPrint-Advisor`. robocopy without `/MIR` would leave the old exe and DLL there, and the existing shortcut would keep starting `CrealityPrint.exe`.

   Also: should the script remove the old "Creality Print Advisor" shortcut?
3. **UI strings:** rename every message that refers to this app (about 20 `_L` strings plus the Print Advisor and sidecar texts, with Italian entries added to `PrintAdvisor_it.po`, recommended), or only the title, About, error captions and Print Advisor.
4. **Retiring the old copy:** how long the transition support stays (legacy probes in `close-build-instance.ps1` and in `check_no_other_instance`). Recommended: until `C:\CrealityPrint-Advisor` is deleted, then a small follow-up removes them and the `allow:` lines.

### Key paths

- `version.inc`
- `src\libslic3r\libslic3r.h`
- `src\libslic3r\DataDirectoryMigration.cpp`
- `src\libslic3r\utils.cpp`
- `src\libslic3r\buildinfo.h`
- `src\libslic3r\Config.cpp`
- `src\libslic3r\Format\bbs_3mf.cpp`
- `src\slic3r\GUI\GUI_App.cpp`
- `src\slic3r\GUI\GUI_Init.cpp`
- `src\slic3r\GUI\InstanceCheck.cpp`
- `src\slic3r\GUI\Plater.cpp`
- `src\slic3r\GUI\simple\printAdvisor\PrintAdvisorServer.cpp`
- `src\slic3r\CLI\CLI.cpp`
- `src\slic3r\Utils\Http.cpp`
- `src\slic3r\Utils\Process.cpp`
- `src\CrealityPrint.cpp`
- `src\CrealityPrint_app_msvc.cpp`
- `src\CMakeLists.txt`
- `cmake\BuildInfoUtil.cmake`
- `build_exe_only.bat`
- `.vscode\tasks.json`
- `tools\close-build-instance.ps1`
- `tools\publish-stable.ps1`
- `tools\net-watch.ps1`
- `tools\verify-phase.mjs`
- `tools\print-advisor-mcp\src\appClient.ts`
- `tools\print-advisor-mcp\src\index.ts`
- `tools\print-advisor-mcp\scripts\smoke.mjs`
- `tests\regression\slice-fixtures.mjs`
- `%USERPROFILE%\.claude.json`
