# F6 "Repository e risorse": implementation spec

Read-only analysis of branch `advisor`, HEAD `14528e0e` (2026-10-20). Nothing was edited, built or run. Sizes come from `git ls-tree -r -l HEAD` (tracked bytes, before compression) unless marked "disk" or "untracked". Line numbers are from the current files; F1-F5 change some of the same files, so every edit below also gives a content anchor. Anything not checked directly in the repository is marked **[INFERENCE]**.

Assumptions taken from the plan and the F1/F2 specs:

- F1 already removed `.github/workflows/`, `.github/dependabot.yml`, `tools/updater/` and the install line `install(FILES ${CMAKE_SOURCE_DIR}/tools/updater/updater.exe ...)` (today `src/CMakeLists.txt:668`).
- F4 already removed `resources/web/login`. If it did not, the web-bundle block in F7 covers it.
- The build entry points that must keep working are `build_exe_only.bat`, `build_release_vs2022.bat`, `build_release_vs2022_progress.bat` (CLAUDE.md names all three), `run_gettext.bat`, and the VS Code tasks in `.vscode/tasks.json`.

Current repository: 15,663 tracked files, 595.2 MB. `.git` is 588 MB on disk (pack 552 MB).

---

## 0. Summary of sizes

| Block | Decision | Files | Tracked size |
|---|---|---|---|
| 1. Base list (junk, dev tools, Perl, CI, old scripts, ARM64, NotoSansKR) | none | ~420 | ~159 MB |
| G1. Linux/macOS/Docker/Flatpak/devcontainer build files | Windows-only (question 2) | 34 | ~0.9 MB |
| G2. Non-Windows prebuilt libraries, platform folders, 32-bit libs | Windows-only, not recommended | 38 | ~4.6 MB |
| (a) Vendor profiles other than Creality and Custom | question 3 | 5,258 | 37.3 MB |
| (b) Languages other than it/en | question 4 | 80 tracked + 20 untracked dirs | 17.5 MB + 7.8 MB fonts, plus ~14 MB untracked `.mo` |
| (c) `tests/engine_test` (c1) / all non-built tests (c2) | question 7 | 33 / ~190 | 34.3 MB / ~37.5 MB |
| (d) `src/tracy` except `public/` and `LICENSE` | to confirm | 334 | 17.1 MB |
| (e) Creality `doc/` | question 7 | 418 | 11.7 MB |
| (f) `package/runtime` installers | question 8 | 8 tracked + 1 untracked | 31.7 MB + 115 MB untracked |
| (h) `.git` history rewrite | question 9, high risk | n/a | see block (h) |

Web bundles (`guide/swiper`, `include`, `model`, `login`, `homepage2`, `orca`; 662 files, 12.7 MB) are specified in F7 together with the Home decision.

---

## 1. Base list (no pending decision)

### 1.1 Files and folders to delete

Every path below was checked with `git grep` for references outside itself, the deleted CI files and the other paths in this list. "Refs and notes" says what still points at it and what to do.

| Path | Files / size | Refs and notes |
|---|---|---|
| `F008四包参数差异.csv`, `F008四包参数差异.xlsx` | 2 / 64 KB | none |
| `MATERIALLISTMANAGER.ipch` | 1 / 744 KB | none (Visual Studio IntelliSense cache) |
| `src/slic3r/GUI/mrgtmp0` | 1 / 4.6 KB | none; a stray copy of an old `WebViewDialog.hpp`, not in CMake. **Extra item, not in the brief.** |
| `clang-format.exe` | 1 / 3.5 MB | only `.githooks/pre-commit`; `git config core.hooksPath` is empty here, so the hook is not active |
| `.githooks/` (`pre-commit`) | 1 / 5 KB | none once `clang-format.exe` goes. Keep `.clang-format` (used by editors) |
| `clang-tidy/` (`clang-tidy.exe` 61 MB, `compile_commands.json`, scripts) | 7 / 63.2 MB | none |
| `Creality_doc/` | 6 / 0.6 MB | `README.md:13`, `README.md:15` (see 1.4) |
| `ai_code_check/` | 1 / 3 KB | none |
| `benchmarks/` | 2 / 1 KB | none |
| `compare_3mf/` | 29 / 0.19 MB | `CMakeLists.txt:1033` commented line (see 1.2) |
| `parameter_history/` | 5 / 0.4 MB | `src/slic3r/Utils/ExportMetas.cpp:53` and `:274` build the path `<repo>/parameter_history/` from `__FILE__`. It is a developer-only export (called from `GUI_App_Parse.cpp:56`, a command-line option). After deletion that export writes into a missing folder and `ofstream` fails silently. Recreate the folder by hand if the export is ever used. No code change. |
| `xs/`, `t/`, `lib/`, `Build.PL` (Perl bindings and Perl tests) | 53 + 27 + 19 + 1 / 0.65 MB | `CMakeLists.txt:117`, `:157`, `:1018-1023` (see 1.2); `.gitignore:9,13,14` and `.dockerignore:9,13,14` (`xs/...` patterns, optional cleanup) |
| `sandboxes/` | 19 / 0.1 MB | `CMakeLists.txt:150-151`, `:1025-1027` (see 1.2) |
| `JenkinsPipeline`, `JenkinsPipeline.MacAppStore`, `Jenkinsfile`, `Jenkinsfile.offline` | 4 / 0.14 MB | none outside themselves |
| `.gitmodules` | 1 | declares submodule `UnitTest` at `http://172.20.180.12:8050/UnitTest`; there is no gitlink in the index (`git ls-files -s` has no mode 160000), so it is dead |
| `GoogleService-Info.plist` | 1 | only `JenkinsPipeline.MacAppStore:39,92` (deleted) |
| `deps/Darwin/` (Firebase Auth SDK for macOS) | 210 / 13.2 MB | `src/CMakeLists.txt:405-430` and `src/slic3r/CMakeLists.txt:966-975` are inside `APPLE` / `CREALITYPRINT_APP_STORE` branches and guarded by `if(EXISTS ...)`. No change needed for Windows; macOS cleanup belongs to G2. |
| Old `.bat` variants: `build_deps.bat`, `build_ninja_vs2012_debug.bat`, `build_ninja_vs2019_debug.bat`, `build_release.bat`, `build_release_vs2022_arm64.bat`, `build_vs2022_unified.bat`, `extract_deps.bat`, `orca_build_release.bat`, `project_build.bat`, `start.bat`, `start1.bat`, `test_build.bat` | 12 / 30 KB | They only call each other (`start*.bat` calls `project_build.bat`, which calls `build_deps.bat`; `orca_build_release.bat` calls `extract_deps.bat`). `test_build.bat` was called only by F1-deleted workflows. `README.md:131` names `build_release.bat` (see 1.4). **Keep** `build_exe_only.bat`, `build_release_vs2022.bat`, `build_release_vs2022_progress.bat`, `run_gettext.bat`. |
| `run_creality_on_e_cores.bat` | 1 / 1.5 KB | installed by `src/CMakeLists.txt:667` (see 1.2). It is a launcher that pins the app to efficiency cores; the stable copy does not use it (`tools/publish-stable.ps1:81-86` copies only `*.exe`, `*.dll`, `mesa`, `resources`). |
| `scripts/*` except `scripts/build_with_progress.ps1` | 31 / 0.18 MB | Only the Jenkins files, F1-deleted workflows (`check_profiles.yml` uses `check_profile_format.py`) and `build_release_vs2022_arm64.bat:107` (deleted) refer to them. **Keep** `scripts/build_with_progress.ps1` (`build_release_vs2022_progress.bat:62`). `CMakeLists.txt:144` names `scripts/app_store.entitlements` inside an `APPLE` + App Store branch (dead on Windows; see G2). Do not confuse with `tools/print-advisor-mcp/scripts/`, which stays. |
| `tools/update_server/` | 4 / 16 KB | none |
| `Readme.txt` ("Init Version"), `SECURITY.md`, `CMakePresets.json` | 3 / 12 KB | none (no script uses presets; the build scripts call CMake directly) |
| `.claude/skills/upstream-update/` and `doc/MCP/upstream-update-plan.md` | 2 | The fork no longer follows Creality releases. Cross-reference to fix: `doc/MCP/upstream-touch-points.md:3` (see 1.4). |
| ARM64 binaries (Windows): `deps/mesa/win-arm64/opengl32.dll` (52 MB), `deps/WebView2/lib/winARM64/` (3 files, 10.5 MB), `deps/GMP/gmp/lib/win-arm64/` (2), `deps/MPFR/mpfr/lib/winARM64/` (2), `deps/CR_TPMS/cr_tpms/lib/winARM64/` (2) | 10 / 63.2 MB | Selected only when `_msvc_arch == ARM64`: `CMakeLists.txt:806-808,841-849,1000`, `src/CMakeLists.txt:364,392,487-490`, `deps/CMakeLists.txt:214-216,487,495`, `deps/GMP/GMP.cmake` (`win-${DEPS_ARCH}`), `deps/MPFR/MPFR.cmake:22-23` (`win${DEPS_BITS}`). No CMake edit needed: the x64 configure never reaches those paths. **Keep** `deps/GMP/gmp/lib/win64/` (copied next to the exe by `CMakeLists.txt:831`) **and** `deps/GMP/gmp/lib/win-x64/` (used by the deps build through `GMP.cmake`), `deps/MPFR/mpfr/lib/win64/`, `deps/WebView2/lib/win64/`, `deps/mesa/win-x64/`, `deps/CR_TPMS/cr_tpms/lib/win64/`. Leave `deps/OpenEXR/0001-arm64-disable-sse2.patch` (referenced by `OpenEXR.cmake:35` in an ARM64 branch; 325 bytes). |
| `resources/fonts/NotoSansKR-Bold.ttf`, `resources/fonts/NotoSansKR-Regular.ttf` | 2 / 11.8 MB | **No reference anywhere** in the repository. Korean ImGui text uses `NanumGothic-*` (`ImGuiWrapper.cpp:2946-2947`). `resources/fonts/OFL.txt` is the NanumGothic licence (NHN copyright), so it stays with NanumGothic (block b). |

Untracked, delete from disk (not `git rm`):

- `.vscode/creality-build-button/` (14 KB) and `.vscode/creality-print-build-button.vsix` (4 KB). CLAUDE.md already says not to use that extension.

Deliberately not in the list:

- `tools/data_migration/` is built: `src/CMakeLists.txt:676` adds `tools/data_migration/nanosvg.cpp` to the target `data_directory_migrate`. Keep.
- `tools/7z.exe`/`7z.dll` (used by `build_release_vs2022.bat:21` "pack"), `tools/msgfmt.exe`, `msgmerge.exe`, `xgettext.exe` (used by `build_exe_only.bat` and `run_gettext.bat`). Keep.
- `tools/vswhere.exe` (0.45 MB) is used only by `build_deps.bat` and `project_build.bat`, both deleted above, so it becomes unreferenced. **Extra item, optional**: delete it in the same commit.
- `.github/ISSUE_TEMPLATE/*` and `.github/pull_request_template.md` (F1 left them optional). Optional here.

### 1.2 CMake edits

All in the root `CMakeLists.txt` unless stated.

| Line (content anchor) | Edit |
|---|---|
| `:117` `option(SLIC3R_PERL_XS ...)` | delete |
| `:150` `# Proposal for C++ unit tests and sandboxes` | change to `# C++ unit tests` |
| `:151` `option(SLIC3R_BUILD_SANDBOXES ...)` | delete |
| `:157` `set(SLIC3R_PERL_XS OFF CACHE BOOL "" FORCE)` (inside `if (IS_CROSS_COMPILE)`) | delete this line, keep the `SLIC3R_BUILD_TESTS` line |
| `:1018-1023` (`# Perl bindings...` comment, `if (SLIC3R_PERL_XS) add_subdirectory(xs) endif ()`) | delete, including the blank line after |
| `:1025-1027` `if(SLIC3R_BUILD_SANDBOXES) add_subdirectory(sandboxes) endif()` | delete |
| `:1033` `# add_subdirectory(compare_3mf)` | delete |
| `src/CMakeLists.txt:667` `install(FILES ${CMAKE_SOURCE_DIR}/run_creality_on_e_cores.bat DESTINATION ".")` | delete (otherwise the `install` target fails on the missing file) |

Effect on `build_exe_only.bat`: a changed `CMakeLists.txt` makes it reconfigure (CLAUDE.md). The cached `SLIC3R_PERL_XS` and `SLIC3R_BUILD_SANDBOXES` entries stay in `CMakeCache.txt` as unused variables; CMake prints at most a "Manually-specified variables were not used" warning. No source file is recompiled because of these edits.

### 1.3 Scripts

No change to the kept scripts: `build_exe_only.bat`, `build_release_vs2022.bat`, `build_release_vs2022_progress.bat` and `run_gettext.bat` do not call any deleted file (checked with `git grep`). `.vscode/tasks.json` runs only `build_exe_only.bat`, `build_release_vs2022.bat`, `tools/close-build-instance.ps1`, `tools/publish-stable.ps1` and the exe.

### 1.4 Documentation edits

| File | Edit |
|---|---|
| `doc/MCP/upstream-touch-points.md:3` | Replace "(vedi `upstream-update-plan.md`, fase 2)" with a sentence saying the fork no longer follows Creality releases and the list stays as a map of the Creality files we changed. Keep the rest of the file. |
| `README.md` | Delete lines 4-20 (Discord section with `Creality_doc/` images). Replace lines 126-150 ("How to compile" for Windows/Mac/Ubuntu) with a short Windows-only section: Visual Studio 2022 Build Tools, `build_release_vs2022.bat deps` once, then `build_exe_only.bat` or `build_release_vs2022.bat`, and a pointer to CLAUDE.md. Lines 73-75 point to a `SoftFever_doc/` folder that does not exist; delete them. F8 rewrites the README completely, so keep this minimal. |
| `CLAUDE.md` | In "Pulsanti nella barra di stato", delete the bullet "Non usare la vecchia estensione locale `.vscode/creality-build-button`..." (the folder is gone). Nothing else in CLAUDE.md names a deleted file. |
| `.gitignore` | Optional: delete `xs/buildtmp`, `xs/MANIFEST.bak`, `xs/assertlib*` (lines 9, 13, 14) and `scripts/__pycache__` (line 44). |

`.claude/agents/*.md`, the other `.claude/skills/*`, `tools/print-advisor-mcp/README.md` and the other `doc/MCP/*.md` files do not name any deleted path (checked).

### 1.5 Commands (Git Bash, repository root)

```bash
git rm -q -- "F008四包参数差异.csv" "F008四包参数差异.xlsx" MATERIALLISTMANAGER.ipch \
  src/slic3r/GUI/mrgtmp0 clang-format.exe Build.PL Readme.txt SECURITY.md CMakePresets.json \
  JenkinsPipeline JenkinsPipeline.MacAppStore Jenkinsfile Jenkinsfile.offline .gitmodules \
  GoogleService-Info.plist run_creality_on_e_cores.bat \
  build_deps.bat build_ninja_vs2012_debug.bat build_ninja_vs2019_debug.bat build_release.bat \
  build_release_vs2022_arm64.bat build_vs2022_unified.bat extract_deps.bat orca_build_release.bat \
  project_build.bat start.bat start1.bat test_build.bat \
  resources/fonts/NotoSansKR-Bold.ttf resources/fonts/NotoSansKR-Regular.ttf \
  doc/MCP/upstream-update-plan.md deps/mesa/win-arm64/opengl32.dll
git rm -r -q -- .githooks clang-tidy Creality_doc ai_code_check benchmarks compare_3mf \
  parameter_history xs t lib sandboxes deps/Darwin tools/update_server \
  .claude/skills/upstream-update \
  deps/WebView2/lib/winARM64 deps/GMP/gmp/lib/win-arm64 deps/MPFR/mpfr/lib/winARM64 \
  deps/CR_TPMS/cr_tpms/lib/winARM64
git ls-files scripts | grep -v '^scripts/build_with_progress\.ps1$' | xargs git rm -q --
rm -rf .vscode/creality-build-button .vscode/creality-print-build-button.vsix
# optional extra: git rm -q tools/vswhere.exe
```

Suggested commits: (1) root junk and `mrgtmp0`; (2) dev tools (clang-tidy, clang-format, hooks); (3) Perl, sandboxes, compare_3mf and their CMake lines; (4) Creality CI, submodule file, Firebase plist, `deps/Darwin`; (5) old scripts, `scripts/` and the e-cores CMake line; (6) upstream-update skill and doc fix; (7) ARM64; (8) NotoSansKR; (9) README and CLAUDE.md.

### 1.6 Verification

1. `git grep -nE "SLIC3R_PERL_XS|SLIC3R_BUILD_SANDBOXES|compare_3mf|run_creality_on_e_cores|NotoSansKR|clang-tidy|upstream-update-plan" -- . ':!doc/specs' ':!doc/cleanup-plan-2026-10-20.md'` returns nothing.
2. `build_exe_only.bat` (it reconfigures because `CMakeLists.txt` changed). Then the "Build Install" task (`build_release_vs2022.bat`) once, to prove `install` no longer needs the e-cores file.
3. `build\src\Release\mesa\opengl32.dll` still exists (x64 Mesa), and `libgmp-10.dll`, `libmpfr-4.dll`, `WebView2Loader.dll` are next to the exe.
4. Plan protocol: G-code of the 4 F0 projects identical, `check-gcode.mjs`, `apply-web-patches.mjs --check`, sidecar `npm run typecheck` and `npm run smoke`, 3-minute manual start.

---

## 2. Block G: Windows only (plan question 2)

The brief puts "Linux/macOS/Docker/flatpak/devcontainer files" in the no-decision list, but the plan still has question 2 open ("Solo Windows?"). Everything that only matters on another OS is collected here so the dependency is visible. **Recommendation: apply G1 together with the base list; skip G2.**

### G1. Build and packaging files for other systems (recommended)

| Path | Files / size | Refs |
|---|---|---|
| `BuildLinux.sh`, `BuildLinux_clang.sh`, `build_release_macos.sh`, `uos.sh`, `run_gettext.sh` | 5 / 30 KB | each other, `Dockerfile`, `.devcontainer/postCreate.sh`, `README.md:137,149-150` (rewritten in 1.4) |
| `Dockerfile`, `DockerBuild.sh`, `DockerRun.sh`, `.dockerignore` | 4 / 4 KB | none |
| `.devcontainer/` | 4 / 5 KB | none |
| `linux.d/` | 8 / 12 KB | none outside the deleted Linux scripts |
| `flatpak/` | 13 / 0.86 MB | none outside deleted scripts |

```bash
git rm -q -- BuildLinux.sh BuildLinux_clang.sh build_release_macos.sh uos.sh run_gettext.sh \
  Dockerfile DockerBuild.sh DockerRun.sh .dockerignore
git rm -r -q -- .devcontainer linux.d flatpak
```

No CMake edit. Keep `run_gettext.bat`.

### G2. Deeper non-Windows content (not recommended now)

These are referenced by CMake in `APPLE`/`UNIX`/32-bit branches. Deleting them does not break the x64 Windows build, but leaves dangling paths in CMake, and the gain is small.

| Path | Size | CMake references to clean if done |
|---|---|---|
| `deps/CR_*/**/lib/linux/` and `deps/CR_*/**/lib/mac/` (12 static `.a`, including the non-Windows arm64/aarch64 ones of `CR_TPMS`) | 0.66 MB | `deps/CR_*/*.cmake` non-WIN32 branches, `src/CMakeLists.txt` CR_* blocks |
| `package/macx/`, `package/macx.cmake`, `package/mac_resources.sh` | 1.73 MB | `package/CMakeLists.txt` APPLE branch |
| `src/platform/osx/`, `src/platform/unix/` | 20 KB | `src/CMakeLists.txt` APPLE/UNIX install and bundle blocks |
| 32-bit Windows libs: `deps/GMP/gmp/lib/win32/`, `deps/GMP/gmp/lib/win-x86/`, `deps/MPFR/mpfr/lib/win32/`, `deps/WebView2/lib/win32/` | 2.2 MB | selected only when `CMAKE_SIZEOF_VOID_P == 4` (`CMakeLists.txt:812-814,850-851`) |
| `src/slic3r/GUI/simple/gpu/GpuOrientMetal.*` | small | **Do not delete**: `simple/gpu` is kept; the `.mm` is compiled only on APPLE (`src/slic3r/CMakeLists.txt:935,948`) |

Also APPLE-only and harmless on Windows: `CMakeLists.txt:144` (`scripts/app_store.entitlements`), `src/CMakeLists.txt:405-430` and `src/slic3r/CMakeLists.txt:966-975` (Firebase SDK, guarded by `EXISTS`), `src/slic3r/CMakeLists.txt:942` (`FirebaseSignIn.mm`). Note: `GUI/FirebaseSignIn.cpp/.hpp` (`src/slic3r/CMakeLists.txt:351-352`) are compiled on every platform without `deps/Darwin`; they belong to the login removal of F4.

---

## 3. Optional blocks (pending user decisions)

Each block is independent and can be one commit. The phase still ends with one build.

### (a) Vendor profiles: keep only Creality and Custom (plan question 3)

**Keep**: `resources/profiles/Creality/`, `Creality.json`, `Custom/`, `Custom.json`, `hotend.stl` (`Preset.cpp:4640`, `PresetBundle.cpp:734`), `blacklist.json`, `CrealityUserMode.json` (`GUI_App.cpp:4156`). `check_unused_setting_id.py` is a developer script (only the deleted CI used it); optional delete.

**Delete** (34 vendors, folder and manifest each; 5,258 files, 37.3 MB): `Anker`, `Anycubic`, `Artillery`, `BBL`, `BIQU`, `CONSTRUCT3D`, `Comgrow`, `Dremel`, `Elegoo`, `FLSun`, `Flashforge`, `FlyingBear`, `Folgertech`, `InfiMech`, `Kingroon`, `MagicMaker`, `OrcaArena`, `Peopoly`, `Positron3D`, `Prusa`, `Qidi`, `Raise3D`, `Ratrig`, `SecKit`, `Snapmaker`, `Sovol`, `Tronxy`, `TwoTrees`, `UltiMaker`, `Vivedino`, `Voron`, `Voxelab`, `Vzbot`, `Wanhao`.

```bash
cd resources/profiles
for v in Anker Anycubic Artillery BBL BIQU CONSTRUCT3D Comgrow Dremel Elegoo FLSun Flashforge FlyingBear \
         Folgertech InfiMech Kingroon MagicMaker OrcaArena Peopoly Positron3D Prusa Qidi Raise3D Ratrig \
         SecKit Snapmaker Sovol Tronxy TwoTrees UltiMaker Vivedino Voron Voxelab Vzbot Wanhao; do
  git rm -r -q -- "$v" "$v.json"
done
cd ../..
```

Verified facts:

- **Inheritance is closed inside the two kept vendors.** A Node scan of every JSON under `Custom/` (58 named presets, 52 `inherits`) and `Creality/` (1,940 named, 1,877 `inherits`) found 0 parents outside the same vendor, and 0 missing `sub_path` entries in `Custom.json` and `Creality.json`. `Custom/filament/*` inherit from `fdm_filament_common` and friends inside `Custom/`.
- **The user's data folder** (`%APPDATA%\Creality\Creality Print\7.3 Alpha\system`) contains only `Creality`, `Creality.json`, `Custom`, `Custom.json`, and the user presets in `user/default` inherit only from Creality presets (`0.20mm Standard @Creality K1C 0.4 nozzle`, `Generic ASA @Creality K1C 0.4 nozzle`). No migration needed.
- `PresetBundle::BBL_BUNDLE` is `"Custom"` (`PresetBundle.cpp:86`), so the "core vendor" logic in `PresetUpdater.cpp:1117-1150` and `ConfigWizard.cpp:128-137` keeps working.
- `PresetUpdater.cpp:1117` iterates `resources/profiles/*.json` dynamically; nothing hard-codes the deleted vendors. It also sees `blacklist.json` and `CrealityUserMode.json` as "vendors" but never installs them (not enabled). Unchanged.
- `GuideFrame` (add printer / add filament) gets its models from `ProfileFamilyLoader` and the vendor manifests, so its lists shrink automatically.
- `GUI_App.cpp:415-462` (`collect_replacements` inside `legacy_multi_extruder_preset_names`) reads only `Creality.json`. Unchanged.
- `CreatePresetsDialog.cpp:47-125`: `filament_vendors`, `printer_vendors` and `printer_model_map` are **label lists** for the "Create printer/filament" dialog (used at `:843` and `:2445-2453`), not file lookups. No change required; trimming them is cosmetic and optional.
- `SliceCommand.cpp:814-822` reads `profiles/BBL/cli_config.json` only for Bambu printer models (all keys are `Bambu Lab ...`). After deletion it logs a warning and returns; the K1C command-line slice (used by F0) is unaffected.
- The other `"BBL"` string comparisons (`Preset.cpp:342,904`, `PresetBundle.cpp:443-452,2111`) only compare names and stay harmless.

Risk **[INFERENCE, not tested]**: a 3MF made with another vendor's printer no longer finds its system parent; Creality Print then treats the presets as project presets, as it does for any unknown vendor.

Verification: printer, filament and process combos; "Aggiungi stampante" and "Aggiungi filamento" dialogs list only Creality and the generic printers; G-code of the F0 projects identical.

### (b) Languages: keep only Italian and English (plan question 4)

Tracked files to delete (80 files, 17.5 MB): `localization/i18n/<lang>/` for `ca cs de es fr hu id ja ko nl pl pt_BR ru sv th tr uk vi zh_CN zh_TW`. Keep `localization/i18n/it/` (including `PrintAdvisor_it.po`), `localization/i18n/en/`, the four `.pot` files and `list.txt`.

Untracked (ignored) build output to delete from disk: `resources/i18n/<same 20 langs>/` (the whole `resources/i18n` is 15 MB; `it` and `en` are about 1.3 MB). Keep the tracked `resources/i18n/placeholder.txt`.

Fonts (6 files, 7.8 MB): `resources/fonts/NanumGothic-Bold.ttf`, `NanumGothic-Regular.ttf`, `OFL.txt` (NanumGothic licence), `NotoSansThai-Bold.ttf`, `NotoSansThai-Regular.ttf`, `OFL-NotoSansThai.txt`. Keep `HarmonyOS_Sans_SC_*.ttf` (default ImGui font, also `libslic3r/Shape/TextShape.cpp:64`).

```bash
for l in ca cs de es fr hu id ja ko nl pl pt_BR ru sv th tr uk vi zh_CN zh_TW; do
  git rm -r -q -- "localization/i18n/$l"; rm -rf "resources/i18n/$l"
done
git rm -q -- resources/fonts/NanumGothic-Bold.ttf resources/fonts/NanumGothic-Regular.ttf resources/fonts/OFL.txt \
  resources/fonts/NotoSansThai-Bold.ttf resources/fonts/NotoSansThai-Regular.ttf resources/fonts/OFL-NotoSansThai.txt
```

Code edits:

1. `src/slic3r/GUI/Preferences.cpp:1421-1444` `supported_languages[]`: reduce to `{ wxLANGUAGE_ENGLISH, wxLANGUAGE_ITALIAN }`. (English is also added unconditionally at `:1457`.)
2. `src/slic3r/GUI/ImGuiWrapper.cpp:2945-2951` (`init_font`, after the comment "Orca: temp fix for Korean font"): delete the `if (m_glyph_ranges == ...GetGlyphRangesKorean()) {...} else if (...GetGlyphRangesThai()) {...}` block; keep the two `auto font_name_* = "HarmonyOS_Sans_SC_*"` lines.
3. `ImGuiWrapper::init_font_all` (`ImGuiWrapper.cpp:3119-3342`, declaration `ImGuiWrapper.hpp:389`): it always merges `NanumGothic-*`, and its only call is commented out (`ImGuiWrapper.cpp:551`). Delete the function and the declaration, otherwise dead code keeps naming deleted files.
4. `ImGuiWrapper::set_language` (`:405-437`): the `ko`, `th`, `ja`, `zh`, `ru/uk`, `cs/pl`, `tr`, `vi` branches only pick glyph ranges and become unreachable from the UI. Leave them (no file dependency). Italian goes to the final `else` (`GetGlyphRangesOthers()`), as today.

No change is needed in `run_gettext.bat` (it loops over the `.po` files that exist) or `build_exe_only.bat` (already Italian only). The language-keyed code branches (`== "zh_CN"` and similar in about 15 GUI files) stay harmless. Optional and not verified here: `resources/images/*_zh*` (13 files, 2.4 MB) and `resources/tooltip/zh_CN` are used only for Chinese; check each with `git grep` before deleting.

Risk **[INFERENCE]**: if `Creality.conf` names a deleted language, wx finds no catalog and the app falls back to English. The user's language is Italian.

Verification: Preferences lists only English and Italiano; switch both ways (`recreate_GUI`); ImGui text with accents (è, à, ù) renders; the Print Advisor dialogs are translated.

### (c) Tests (plan question 7)

Facts: `SLIC3R_BUILD_TESTS` defaults to `OFF` (`CMakeLists.txt:152`) and no kept build script turns it on, so nothing under `tests/` is built today. `tests/print-advisor/` is used by `tools/print-advisor-mcp/package.json:10` (`test:gcode`) and by the verification protocol: **keep it**.

- **c1 (recommended minimum)**: `git rm -r -q tests/engine_test` (33 files, 34.3 MB: Creality Python engine tests and baseline 3MF/JSON). No reference outside itself.
- **c2 (everything that is not built)**: also delete `tests/catch2 tests/catch_main.hpp tests/cloud_device_mqtt tests/cmake tests/cpp17 tests/data tests/data_migration tests/device_add tests/engine tests/example tests/fff_print tests/file_downloader tests/gui tests/libnest2d tests/libslic3r tests/preset_sync tests/printer_cover tests/pytest tests/runtest.py tests/sla_print tests/slic3rutils tests/test_utils.hpp tests/testcases tests/CMakeLists.txt` (about 160 more files, ~3.2 MB), and in the root `CMakeLists.txt` delete `option(SLIC3R_BUILD_TESTS ...)` (`:152`), its `FORCE` line inside `if (IS_CROSS_COMPILE)` (`:158`) and `if(SLIC3R_BUILD_TESTS) add_subdirectory(tests) endif()` (`:1029-1031`).
  - Side effects: F2's `tests/gui/topbar_layout_tests.cpp` disappears (F2 kept `BBLTopbar::SetActionsCollapsed` only for it; that reason goes away). `tools/data_migration/README.md:27` and `doc/requirements/data-directory-migration.md:19` mention `tests/data_migration/test_migration.py`; the latter goes with block (e), the former needs a one-line edit.

### (d) `src/tracy` except `public/`

Facts: the only CMake use is `src/CMakeLists.txt:36-45` (include directory `tracy/public` and the `TRACY_*` defines, RelWithDebInfo only) and `:214-218` (`tracy/public/TracyClient.cpp`). The only include is `src/libslic3r/Diagnostics/Performance.cpp:8` (`<tracy/Tracy.hpp>`, found under `tracy/public/tracy/`). `src/tracy/CMakeLists.txt` is never added with `add_subdirectory`.

Delete every entry of `src/tracy/` except `public/` and `LICENSE` (BSD licence: keep the notice next to the code): `.clang-format .clang-tidy .github .gitignore .mailmap .vscode CMakeLists.txt Config.cmake.in NEWS README.md capture cmake csvexport doc dtl examples extra getopt icon import manual meson.build meson.options profiler python server test update` (334 files, 17.1 MB).

```bash
git ls-files src/tracy | grep -vE '^src/tracy/(public/|LICENSE$)' | xargs git rm -q --
```

Verification: Release build (Tracy is off there) and, once, a RelWithDebInfo configure if that configuration is ever used.

### (e) Creality `doc/` (plan question 7)

Keep `doc/MCP/` (tracked), `doc/branding/`, `doc/specs/` and `doc/cleanup-plan-2026-10-20.md` (untracked today; commit them first if wanted). Delete every other tracked entry of `doc/` (418 files, 11.7 MB): the AI chat and cxagent design notes, Orca user docs (`Calibration.md`, `Seam.md`, ...), `doc/images`, `doc/bugfixes`, `doc/requirements`, `doc/simple-mode`, `doc/How to build`, the Chinese-named notes, and so on.

```bash
git ls-files -z doc | grep -zv '^doc/MCP/' | xargs -0 git rm -q --
```

(`-z`/`-0` because some names contain spaces and CJK characters.) References from outside `doc/`: none (checked with `git grep`). The F1-deleted `sync_doc.yml`/`upload_doc.yml` used `doc/`. `src/slic3r/GUI/simple/bridge/*.md` (Chinese design notes inside the source tree) are not under `doc/`; they go with F3 or stay.

### (f) Runtime installers in `package/runtime` (plan question 8)

Tracked (8 files, 31.7 MB): `MicrosoftEdgeSetup.exe`, `STLThumb-Windows_0.5.0.exe`, `STLThumbWinShellExtension.dll`, `curl.exe`, `libcurl.dll`, `stl-thumb.exe`, `vcomp140.dll`, `vcredist_x64.exe`. Untracked: `MicrosoftEdgeWebView2RuntimeInstallerX64.exe` (115 MB, shown by `git status`).

Facts:

- `src/CMakeLists.txt:659-666` install seven of the tracked files plus the untracked WebView2 installer next to the exe; `vcomp140.dll` is not installed by anything.
- `install(FILES ...)` fails when a listed file is missing, so the untracked 115 MB installer is **required today** by the "Build Install" task on any clone that lacks it.
- `Widgets/WebView.cpp:736` looks for `MicrosoftEdgeSetup.exe` next to `resources/` only when the WebView2 runtime is missing. The stable copy (`tools/publish-stable.ps1:81`) copies `*.exe`/`*.dll` from `build\src\Release`, where these installers never are, so the stable copy already lacks them.
- `package/package.cmake:137-138` names `vcredist_x64.exe` and `STLThumb...` for CPack/NSIS; CPack runs only with an explicit `cpack`/`package` target. Leave it (F8 rebuilds the installer).

Edit: delete `src/CMakeLists.txt:659-666` (the eight `install(FILES ${CMAKE_SOURCE_DIR}/package/runtime/...)` lines; the e-cores line 667 is gone with section 1.2 and the updater line 668 with F1). Then:

```bash
git rm -r -q package/runtime
rm -f package/runtime/MicrosoftEdgeWebView2RuntimeInstallerX64.exe   # untracked
```

Behaviour change: on a PC without the WebView2 runtime the app can no longer install Edge from a local file (`WebView.cpp` returns false). Windows 11 ships WebView2. **[INFERENCE]** Acceptable for this user; F8 can bring an installer back when a public build exists.

### (h) Rewriting `.git` history (plan question 9): HIGH RISK, optional

Measured (on-disk object sizes, `git cat-file --batch-check='%(objectsize:disk)'`):

- whole pack: 552 MB; objects reachable from the local branches and `origin`: ~451 MB; from `HEAD` only: ~430 MB.
- 38 tags and 18 `upstream/*` remote-tracking branches (CrealityOfficial) hold ~130 MB of objects that are not in the fork's branches. Two stray refs `refs/codex/turn-diffs/checkpoints/...` also exist.
- Inside the branches' history, paths that are no longer in the tree weigh ~175 MB: old hashed web bundles under `resources/web/*/assets` (~115 MB), `resources/dumptools` (~51 MB, already deleted from the tree; `src/CxMiniDump.cpp:158` still names it), older `package/runtime` versions (~9 MB). The base list of section 1 is ~47 MB of history; blocks (a)-(f) add roughly 18 + 22 + 9 + 4 + 9 + 29 MB.

Two levels:

**H0 (no rewrite, low risk, local only).** Drops objects that are not part of the fork's own history. No commit changes.

```bash
git tag -l                                       # review: keep advisor-v7.3.0-final and the other fork tags
git tag -d v4.3.7 v4.3.8 ...                     # only the upstream Creality tags
git remote remove upstream                       # drops refs/remotes/upstream/*
git for-each-ref --format='%(refname)' refs/codex | xargs -n1 git update-ref -d
git reflog expire --expire=now --all && git gc --prune=now --aggressive
```

Expected **[INFERENCE]**: local `.git` from ~552 MB to ~430-450 MB. `upstream` can be added again later if ever needed.

**H1 (history rewrite with git-filter-repo).** Only after F6 is committed and pushed, with no print running and no other clone in use.

1. Backup: `git clone --mirror <repo> <backup>/CrealityPrint-pre-filter.git`, and copy the whole working folder too (the untracked `doc/specs`, `doc/branding`, `resources/i18n`, `build/` and `deps/build` are not in the mirror).
2. Install the tool: `python -m pip install git-filter-repo` (not installed today: `import git_filter_repo` fails).
3. Work on a fresh clone, never on the working repository: `git clone --no-local <repo> C:/tmp/cp-filter`, then `cd C:/tmp/cp-filter`.
4. Build `paths-to-drop.txt` with one `glob:` line per path deleted by the applied blocks (for example `glob:clang-tidy/**`, `glob:deps/mesa/win-arm64/**`, `glob:resources/dumptools/**`), and `blobs-to-strip.txt` with the ids of blobs under `resources/web/` and `package/runtime/` whose path is not in the final tree (from `git rev-list --objects --all` joined with `git ls-tree -r HEAD`).
5. `git filter-repo --invert-paths --paths-from-file paths-to-drop.txt --strip-blobs-with-ids blobs-to-strip.txt`
6. Check: commit count per branch unchanged; the final tree is identical (`git rev-parse HEAD^{tree}` equals the backup's value for the same branch); full build; G-code identical to F0; MCP smoke test.
7. Publish: `git push --force --all origin` and `git push --force --tags origin`; delete stale remote branches (`origin/dependabot/*`, and `origin/archive/mcp-advisor-dev` if not needed). Every commit hash changes, so the `pre-lean` tag and the hashes quoted in CLAUDE.md and in Claude's memory files (`9541a937`, `14528e0e`, `32f5ea42`, ...) become stale: update them or keep the mapping `filter-repo` writes in `.git/filter-repo/commit-map`.
8. Re-clone the working copy (or `git fetch` and `git reset --hard origin/<branch>` on each branch) and copy back the untracked folders.

Caveats:

- The GitHub repository is a fork in the network of `CrealityOfficial/CrealityPrint`. GitHub keeps shared objects for the whole fork network, so the size GitHub reports may not shrink like the local `.git` **[INFERENCE]**.
- Other clones and the stable copy's `stable-version.txt` (it records the build commit) become inconsistent with the new hashes.
- Estimated result **[INFERENCE]**: with the base list plus blocks (a)-(f), roughly 150-200 MB for `.git`.

Recommendation: do H0 now; decide on H1 only before publishing MentorSlicer (F8), together with the secret scan.

---

## 4. Order, compile risks, verification

Order inside F6: base list, then G1, then the chosen optional blocks, then the docs, then one build.

Compile and build risks:

- The root `CMakeLists.txt` edits trigger a reconfigure; the base list changes no C++ file, so the rebuild is short.
- Block (b) touches `ImGuiWrapper.cpp/.hpp` and `Preferences.cpp` (medium recompile; `ImGuiWrapper.hpp` is widely included).
- Blocks (c2) and (f) edit CMake only.
- Forgetting `src/CMakeLists.txt:667` (e-cores) or `:659-666` (block f) breaks only the `install` target; `build_exe_only.bat` still passes, so run "Build Install" once.

Final checks for the phase (plan protocol plus):

1. `git status` clean except the intended untracked folders.
2. `git ls-files | wc -l` and the `git ls-tree -r -l HEAD` byte sum, to record the new size.
3. Tasks "Build EXE", "Build Install", "Avvia CP"; "Pubblica stabile" only after a successful manual test.
4. G-code identical to F0, `check-gcode.mjs`, `apply-web-patches.mjs --check`, sidecar typecheck and smoke, manual start, Device tab with the K1C (printer idle).

## 5. Open questions for the user

1. Question 2 (Windows only): apply G1 now? (Recommended yes; G2 no.)
2. Question 3 (profiles): block (a) yes/no.
3. Question 4 (languages): block (b) yes/no.
4. Question 7 (tests and docs): (c1), (c2), (e).
5. `src/tracy` (d): yes/no.
6. Question 8 (installers): block (f) yes/no.
7. Question 9 (history): H0 now, H1 later or never.
8. Extras found during the analysis: `src/slic3r/GUI/mrgtmp0` (in the base list), `tools/vswhere.exe` and the leftover `.github` templates (optional).
