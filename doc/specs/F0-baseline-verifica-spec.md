## F0 "Baseline and verification tooling": implementation spec

I only read files and ran three read-only checks (`check-gcode.mjs` on the existing fixture, `apply-web-patches.mjs --check`, `tsc --noEmit` of the sidecar). Nothing was edited, built or sliced, and `CrealityPrint.exe` was not started. Line numbers refer to HEAD `14528e0e` (branch `advisor`). Statements are verified against the code unless they are marked **[inference]**. Everything marked [inference] is checked by the F0 dry run in section 7.

---

## 0. Key facts and corrections to the brief

1. **CLI slicing exists and does not need the GUI instance.**
   - `CrealityPrint.exe --cli ...` goes `wWinMain` (`src/CrealityPrint_app_msvc.cpp`) → `crealityprint_main` (exported by `CrealityPrint_Slicer.dll`, `src/CrealityPrint.cpp:463`) → `run_application` → `CLI::run` (`src/CrealityPrint.cpp:287-290`).
   - It returns **before** `GUI::GUI_Run`. `instance_check` lives only in `GUI_Run` (`src/slic3r/GUI/GUI_Init.cpp:164`). So the CLI never forwards to the running app and can run while the GUI is open.
2. **The real code is in the DLL.** `CrealityPrint.exe` is a 300 KB WIN32-subsystem launcher (`src/CMakeLists.txt:479-484`, `SLIC3R_WRAPPER_NOCONSOLE`). The build check must look at `build\src\Release\CrealityPrint_Slicer.dll`, not only at the exe. Today the exe is from 15:47:46 and the DLL from 16:08:29.
3. **No `--export-gcode` and no `--load-settings` for a 3MF.**
   - The only CLI command is `--slice <n>` (`src/slic3r/CLI/CLI.cpp:248`).
   - G-code is written only with `--need-gcode-file` plus `--outputdir`. The file name is fixed: `<outputdir>\plate_<n>.gcode` (`SliceCommand.cpp:1244-1249`).
   - `--load-settings` / `--load-filaments` together with a 3MF fail with -2 (`SliceCommand.cpp:1998-2002`).
4. **Option names use dashes.** The code's error messages say `--load_slicedata`, but `read_cli` maps an option without an explicit `cli` name by replacing `_` with `-` (`src/libslic3r/Config.cpp:265-283`). An unknown spelling such as `--need_gcode_file` is rejected ("Invalid option", exit -2).
5. **The CLI never opens AppConfig and never uses the GUI data folder by default.**
   - The CLI files contain no `AppConfig`, `app_config`, `wxGetApp` or `Http` (grep of `src/slic3r/CLI/*.cpp`).
   - Without `--datadir`, `data_dir()` becomes `%APPDATA%\Creality` (`SLIC3R_APP_KEY` = "Creality", `CLI.cpp:93-124`). The GUI uses `%APPDATA%\Creality\Creality Print\7.3 Alpha`.
   - The CLI **does write** there: `PresetBundle::load_vendor_configs_from_json` keeps a parse cache in `<data_dir>\cache\system_presets` (`PresetConfigCache.cpp:169-173`, `PresetBundle.cpp:5033-5035`).
   - **Always pass `--datadir <empty scratch folder>`.**
6. **The CLI baseline has no thumbnails, no task id and no `profile_version` line.**
   - `export_gcode(outfile, gcode_result, nullptr)`: no thumbnail callback (`SliceCommand.cpp:1255`, signature `Print.hpp:897`).
   - `m_write_task_id_placeholder` defaults to `false` (`PrintBase.hpp:611`). Only `BackgroundSlicingProcess.cpp:299` sets it.
   - `printer_profile_version` is set only by the GUI (`BackgroundSlicingProcess.cpp:1003`, `Plater.cpp:21985`).
7. **Calibration mode is not saved in a 3MF.**
   - `Print::set_calib_params` is called only from `Plater.cpp` (17692, 18067, ...). No `calib_params`/`CalibMode` appears in `src/libslic3r/Format/` or `Model.*`.
   - So a saved temperature, PA or retraction tower, sliced from the CLI, loses its per-layer `M104`/PA/retraction commands (`GCode.cpp:8486-8535`).
   - `Plater::calib_flowrate` (17904-18003) has **no** `set_calib_params`: it stores everything as per-object settings (`print_flow_ratio`, walls, speeds) and process values. Of the calibrations, only the **flow rate** test slices the same from the CLI as from the GUI. It is the calibration fixture (section 2).
8. **`result.json` is written only on Linux** (`record_exit_reson`, `SliceCommand.cpp:556-599`, guarded by a Linux preprocessor check). On Windows the result is the exit code plus `--logfile`.
9. **Breakpad is off in this build** (`build\CMakeCache.txt`: `ENABLE_BREAKPAD:BOOL=OFF`, `LIB_BREAKPAD_RELEASE-NOTFOUND`).
   - A CLI crash therefore does **not** relaunch `CrealityPrint.exe "minidump://..."`. That relaunch would skip the single-instance check (`GUI_Init.cpp:161-163`) and save the shared AppConfig (`GUI_App.cpp:4330-4331`).
   - If Breakpad is ever turned on, a CLI crash becomes a risk for the running instance's configuration.
10. **The current DLL matches HEAD in code terms.**
    - No file under `src/` is newer than `CrealityPrint_Slicer.dll` (16:08:29). HEAD `14528e0e` was committed at 17:14:42 and contains `GCode.cpp`.
    - `git status` shows only untracked docs.
    - Re-check this right before F0 (section 4, step 1).
11. **New network finding for F1/F7.**
    - `resources/web/deviceMgr/assets/BZCDzYbb.js` calls `fetch("https://www.baidu.com/...")` as an "online" probe. The Device page therefore contacts Baidu.
    - `src/slic3r/GUI/GUI.cpp:763` has `http://as.cxswyjy.com/slice/#/OnlineSupport?`.
    - Both hosts are on the flag list in section 5.

---

## 1. Slicing a 3MF from the command line

### 1.1 Call flow (verified)

1. `wWinMain` (`CrealityPrint_app_msvc.cpp`): probes OpenGL with a hidden window, may load `mesa\opengl32.dll`, loads `CrealityPrint_Slicer.dll` and calls `crealityprint_main`. These steps run in CLI mode too; they do no harm.
2. `crealityprint_main` (`CrealityPrint.cpp:463-726`) installs the new-handler. With `BBL_RELEASE_TO_PUBLIC=1` and no Breakpad it installs no crash handler.
3. `run_application`:
   - `parse_application_arguments` removes `--cli` (lines 149-165);
   - `initialize_application_environment` sets `resources_dir = <exe dir>\resources` (in `build\src\Release` this is a junction to the repository's `resources`) and the temporary dir = `wxFileName::GetTempDir()`, which honours `TMP`/`TEMP`;
   - then `CLI::run`.
4. `CLI::run` (`CLI.cpp:268`):
   - `read_cli`;
   - `validate`;
   - `initialize_cli_data_directory`;
   - `extra_config` = print options given on the command line;
   - exactly one action, which must be `slice`.
5. `SliceCommand::run` (`SliceCommand.cpp:2650`):
   - `parse_options`;
   - `load_input`;
   - `prepare_configuration`: `refresh_creality_project_configuration`, flush volumes, `finalize_configuration`;
   - `prepare_plates`;
   - `execute` → `execute_plate_slicing`.

### 1.2 Options that matter (from `CLIActionsConfigDef` / `CLIMiscConfigDef`, `PrintConfig.cpp:9495-9650`)

| Flag | Key / type | Use for F0 |
|---|---|---|
| `--cli` | (stripped by the launcher) | required, anywhere on the line |
| `--slice <n>` | `slice` coInt | `0` = all plates (fixtures have one plate). Needs a value. |
| `--need-gcode-file` | `need_gcode_file` coBool | required, otherwise the G-code is written to the temp dir and deleted (1266-1281) |
| `--outputdir <dir>` | `outputdir` | required with the flag above; it is created (542-551) |
| `--datadir <dir>` | `datadir` | **always**: an empty scratch folder |
| `--allow-newer-file` | `allow_newer_file` coBool | recommended: skips the 3MF version check (1903-1908) so the fixtures still load after a version or name change (F8) |
| `--debug <0..5>` | `debug` | `3` (info), so the preset-merge line is logged |
| `--logfile <file>` | `logfile` | always: on Windows this is the only reliable diagnostic channel |
| `--normative-check=0` | `normative_check` | only if a fixture has `post_process` scripts (otherwise exit -19) |
| `--no-check` | `no_check` | **do not use**: it hides the path-conflict and empty-layer errors that F0 should catch |
| `--help` | `help` | prints the help on the parent console (`AttachConsole`) |

Plate selection: `--slice 1` slices only plate 1. If the plate number is above the plate count, the result is -2 (2523-2533).

### 1.3 Side effects and coexistence with the running GUI

- **Single instance**: not involved (0.1). [inference] Two processes may slice at the same time; the only shared resource is the CPU.
- **AppConfig / user profiles**: never read (0.5).
- **Data dir**: with `--datadir X`, only `X\cache\system_presets\*.cereal` is written. `X\system\Creality.json` does not exist, so the system presets come from `<resources>\profiles` (2012-2014). This is deterministic and follows the repository. It does not use the profiles downloaded into the GUI data folder.
- **Temp files**:
  - `Model::get_backup_path` creates `%TEMP%\crealityprint_model\<Day_Mon_DD>\<HH_MM_SS>#<pid>#<id>\` (`Model.cpp:1109-1140`). The name contains the PID, so it never collides with the GUI's folder.
  - It is removed at the end (`remove_backup_path_if_exist`, 2633-2634 and in `finish_with_error`).
  - The runner also sets `TEMP`/`TMP` to a scratch folder for the child process, so nothing is left in the user's temp folder after a crash.
- **Logs**: only `--logfile`. Without it, boost.log writes to a console that does not exist.
- **Network**: none in the CLI path (no `Http` in `src/slic3r/CLI`). [inference] DLL static initialisers do no network I/O; the F0 network check with only the CLI running can confirm it (optional).
- **Printer**: never contacted. Slicing during a print is technically safe. Still, the heavy CPU load can slow down the GUI's telemetry, so run F0 when the printer is idle (decision for the user).
- **Build task conflict**: `tools/close-build-instance.ps1` kills **every** `CrealityPrint.exe` under `build*`, including a CLI slice in progress. Do not start a build while slicing.

### 1.4 How the project settings reach the slicer

1. `Model::read_from_file(..., LoadModel|LoadConfig|AddDefaultInstances|LoadAuxiliary, &plate_data, &project_presets, ...)` (1880) reads:
   - the objects with their per-object settings and modifiers;
   - the plates (`plate_data_src`, including per-plate `bed_type` and `print_sequence`);
   - the full flattened project config (`Metadata/project_settings.config`);
   - the embedded presets (`Metadata/print_setting_*`, `filament_settings_*`, `machine_settings_*`, `bbs_3mf.cpp:193-196`).
2. `refresh_creality_project_configuration` (2006-2066): if `bbs_is_creality_3mf`, it
   - loads the Creality system presets;
   - loads the embedded presets;
   - calls `PresetBundle::load_config_model(project, FullPrintConfig defaults + project config)`;
   - uses `full_config()`. If this throws, it logs "Unable to refresh Creality project presets; using embedded settings" and goes on with the project config as it is.
3. Which presets get embedded:
   - `Plater` stores only `preset_bundle.get_current_project_embedded_presets()` as embedded presets (`Plater.cpp:22115-22124`). A normal user copy such as "ASA K1C - pezzi grandi" is **not** stored as a preset.
   - Its values are still in the flattened project config, because unsaved and saved values are both in `cfg`.
   - [inference] `load_config_model` keeps the project values when the preset name is unknown, so `heat_soak_time`, `heat_soak_bed_temp`, `brim_layers` and `brim_layers_object_gap` arrive unchanged.
   - **Acceptance check in the dry run**: read them back from the `CONFIG` block of the CLI G-code.
4. Per-plate config: `new_print_config.apply(*part_plate->config())` (1057-1059) applies the plate's `bed_type` and similar overrides.
5. The four options added by the fork are ordinary config keys, so no CLI-specific code is involved.
   - The heat soak is emitted by `GCode.cpp` before `machine_start_gcode`.
   - `brim_layers` / `brim_layers_object_gap` are used by the brim generator and `GCode.cpp`.
6. **Validation becomes an exit code**:
   - `print->validate` errors → -51/-61/-62/-63/-64;
   - "partly outside" → -52;
   - empty plate → -50;
   - path conflict → -101;
   - `SlicingEmptyGcodeLayers`/`SlicingGcodeOverlap` warnings → -100.

### 1.5 Exit codes (`src/libslic3r/Utils.hpp:28-64`)

0 ok; -2 invalid parameters; -3 file not found; -5 preset file; -6 model file unreadable; -18 invalid values in 3MF; -19 post_process present; -24 3MF version; -50 no object fully on the plate; -51 validate error; -52 object partly outside; -61 filament/bed type; -63/-64 collisions; -100 slicing error; -101 G-code path conflicts.

- PowerShell `Process.ExitCode` shows the negative value.
- Node `spawnSync().status` may report it as an unsigned DWORD (for example 4294967294 for -2): normalise with `status | 0`.

### 1.6 CLI output compared with the GUI (why F0 compares CLI with CLI only)

| Item | CLI | GUI |
|---|---|---|
| `; generated by Creality_Print V7.3.0.0 on <date> at <time>` | yes | yes |
| `; creality_uuid:` (random UUID, `GCode.cpp:5343-5347`) | yes | yes |
| `; creality_task_id_pending:` / `; creality_task_id:` | no | yes (removed by F1) |
| `; profile_version:` | no | yes |
| Thumbnails (`THUMBNAIL_BLOCK`) | no | yes, for printers with thumbnails in the profile |
| System preset root | `resources\profiles` | GUI data folder `system\` (may be a newer download) |

[inference] The executable block of the K1C PLA fixture should be identical between the GUI and the CLI. This is an informational check in the dry run, not a gate.

### 1.7 Exact command lines

Paths without spaces are mandatory. PowerShell 5.1 `Start-Process -ArgumentList` joins the arguments without quoting them.

**PowerShell (one fixture, by hand):**

```powershell
$repo = '<repo>'
$exe  = "$repo\build\src\Release\CrealityPrint.exe"
$name = 'k1c-pla'
$work = "$repo\tests\regression\out\F0\_work\$name"
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory "$work\datadir","$work\tmp","$work\plates" | Out-Null
$oldTemp = $env:TEMP; $oldTmp = $env:TMP
$env:TEMP = "$work\tmp"; $env:TMP = "$work\tmp"          # inherited by the child only while set
$p = Start-Process -FilePath $exe -Wait -PassThru -ArgumentList @(
  '--cli','--slice','0','--need-gcode-file','--allow-newer-file',
  '--outputdir',"$work\plates",'--datadir',"$work\datadir",
  '--debug','3','--logfile',"$repo\tests\regression\out\F0\$name.log",
  "$repo\tests\regression\fixtures\$name.3mf")
$env:TEMP = $oldTemp; $env:TMP = $oldTmp
"exit=$($p.ExitCode)"                                      # expected: exit=0
Move-Item "$work\plates\plate_1.gcode" "$repo\tests\regression\out\F0\$name.gcode" -Force
```

**cmd / .bat (same thing):**

```bat
set "R=<repo>"
set "W=%R%\tests\regression\out\F0\_work\k1c-pla"
rmdir /s /q "%W%" 2>nul & mkdir "%W%\datadir" "%W%\tmp" "%W%\plates"
set "TEMP=%W%\tmp" & set "TMP=%W%\tmp"
start "" /wait "%R%\build\src\Release\CrealityPrint.exe" --cli --slice 0 --need-gcode-file --allow-newer-file --outputdir "%W%\plates" --datadir "%W%\datadir" --debug 3 --logfile "%R%\tests\regression\out\F0\k1c-pla.log" "%R%\tests\regression\fixtures\k1c-pla.3mf"
echo exit=%ERRORLEVEL%
```

- `start /wait` is needed: the exe is a WIN32-subsystem program, so `cmd` would not wait for it otherwise.
- Write it as separate lines in a `.bat`, because `%ERRORLEVEL%` on the same `&` line expands too early.
- In the agents' shell, `NoDefaultCurrentDirectoryInExePath=1` applies: use absolute paths, as above.

**Help (to check that the option names are accepted):** `"%R%\build\src\Release\CrealityPrint.exe" --cli --help` from a console. The text goes to the parent console through `AttachConsole`.

### 1.8 Runner: `tests/regression/slice-fixtures.mjs` (Node 20+, no dependencies)

```
node tests/regression/slice-fixtures.mjs --out <dir> [--exe <path>] [--only a,b] [--runs 1|2] [--timeout-min 15]
```

1. **Defaults**:
   - `--exe` = `<repo>\build\src\Release\CrealityPrint.exe`;
   - fixtures = `tests/regression/fixtures/*.3mf` in name order;
   - `--runs 1`.
2. **Preconditions** (exit 2):
   - the exe and `CrealityPrint_Slicer.dll` exist next to each other;
   - no path contains spaces;
   - `--out` is not inside `%APPDATA%`.
3. **For each fixture `<name>.3mf`**:
   1. `work = <out>\_work\<name>`: delete it, then create `datadir`, `tmp` and `plates`. A fresh data dir on every run, so the preset cache is never reused.
   2. `spawnSync(exe, ['--cli','--slice','0','--need-gcode-file','--allow-newer-file','--outputdir',plates,'--datadir',datadir,'--debug','3','--logfile', <out>\<name>.log, fixture], { env: {...process.env, TEMP: tmp, TMP: tmp}, windowsHide: true, timeout, stdio: ['ignore','pipe','pipe'] })`.
   3. `code = status | 0`. If `code != 0`, record FAIL with the meaning from 1.5, the last 30 log lines and stderr.
   4. Move `plates\plate_<n>.gcode` to `<out>\<name>.gcode`, or to `<out>\<name>.plate_<n>.gcode` when there is more than one plate. Exactly one is expected: report extra plates.
   5. **Log check**:
      - `refreshed Creality project with GUI preset merge: root=...` → record the root; it must end in `resources\profiles`;
      - `Unable to refresh Creality project presets` → WARN (still deterministic, but different from the GUI merge).
   6. Delete `work` unless `--keep-temp`.
4. **`--runs 2`**: slice everything twice (`<out>\run1`, `<out>\run2`) and compare the two outputs of each fixture with `compare-gcode.mjs`. Any difference is a FAIL "nondeterministic" (exit 1). F0 uses `--runs 2` once.
5. **Writes `<out>\slice-manifest.json`**:
   - `exe`, `dll` (path, size, mtime, sha256);
   - `git_head`, `git_dirty_src` (`git status --porcelain -- src`);
   - `node` version, `date`;
   - per fixture: `exit_code`, `seconds`, `gcode`, `bytes`, `sha256_raw`, `sha256_normalized` (via the compare module), `preset_root`, `warnings[]`.
6. **Exit codes**: 0 all sliced; 1 at least one slice failed or nondeterministic; 2 usage or environment.
7. It never writes outside `--out` and never touches `%APPDATA%`.

### 1.9 If the CLI path proves unreliable

The F0 dry run (section 7, step 3) decides. Count the CLI as unreliable if it crashes, returns a code other than 0 on a fixture that slices in the GUI, is nondeterministic, or loses fixture values in the `CONFIG` block.

| Option | How | Pros | Cons |
|---|---|---|---|
| **A. GUI export (recommended fallback)** | Open the fixture with **File → Open project**, then **Slice piatto**, then **Esporta file G-code** to `tests\regression\out\<phase>\<name>.gcode`. Normalise and compare as usual. | Real GUI path, no code. Also covers calibration modes. | About 1 min per fixture by hand. GUI-only lines must be masked (section 3). Uses the data-folder system profiles. The dev build must be the open instance. |
| B. Print Advisor routes | Only `POST /v1/slice` (slices the **current** plate) and `GET /v1/slice_result` (statistics, **no file path**, `PrintAdvisorServer.cpp:1719-1761`) exist. Opening a project is possible only through `/v1/archive/open`, with a confirmation and a temporary copy. | Already in the app | Cannot point at a fixture or fetch the G-code without a new C++ route. That means a code change during the cleanup, so it is **rejected**. |
| C. Standalone harness | New small exe that links `libslic3r` (+ `PartPlate`) and runs `read_from_file`, the preset refresh, `process()` and `export_gcode()` | Fully automatic, no GUI | New CMake target and a second copy of `refresh_creality_project_configuration`. It must survive F5/F7 changes. Not worth it while the CLI works. |

Even if the CLI works, plan a **one-time GUI-against-CLI sanity check** on `k1c-pla` (section 7, step 4). It shows that the CLI baseline really exercises the same engine path.

---

## 2. Fixtures

### 2.1 Repository layout (all new; nothing existing moves)

```
tests/regression/
  README.md                      how to run, how to regenerate, what is masked (Italian, short)
  masks.json                     mask rules for compare-gcode.mjs (section 3.4)
  compare-gcode.mjs              section 3
  slice-fixtures.mjs             section 1.8
  fixtures/
    k1c-pla.3mf
    asa-soak-brim.3mf
    asa-soak-brim.expected.json  input for tests/print-advisor/check-gcode.mjs
    supports-tree.3mf
    calib-flow.3mf
    FIXTURES.md                  one paragraph per fixture: printer, filament, process, plate type, objects, edited values
  baseline/F0/
    k1c-pla.gcode.gz             full CLI G-code, gzip (node:zlib), committed
    asa-soak-brim.gcode.gz
    supports-tree.gcode.gz
    calib-flow.gcode.gz
    manifest.json                copy of slice-manifest.json + sha256_normalized per fixture
    smoke.txt                    output of npm run smoke (app running)
    web-patches.txt              output of apply-web-patches --check
    manual-checklist.md          ticked checklist, start-up time, notes
    net-watch.csv / net-watch.txt  network reference for the pre-lean build (expected to show Creality/Google hosts)
  out/                           generated per phase, ignored by git
tools/verify-phase.mjs            section 4
tools/verify/removed-F1.txt ...   one list per phase (section 4.3)
tools/net-watch.ps1               section 5
```

- Add `tests/regression/out/` to `.gitignore`. Today nothing under `tests/` is ignored: verified with `git check-ignore`.
- **Commit the baseline gzipped.**
  - [inference] The four plain G-codes are about 10-25 MB together; gzip brings that to roughly a quarter.
  - Committing them keeps the baseline usable from any branch, and `compare-gcode.mjs` reads `.gz` directly.
  - If the user prefers not to commit G-code, commit only `manifest.json` (normalized hashes). Then any difference can be detected but not shown.

### 2.2 Who and when

- The **user** makes the fixtures in the GUI, with the **dev build** (`build\src\Release`, HEAD `14528e0e`), not with the stable copy.
  - The stable copy is `014576c8` plus uncommitted changes (`C:\CrealityPrint-Advisor\stable-version.txt`), so it does not have `brim_layers_object_gap` yet.
- Only one instance should be open, so `endpoint.json` and the profiles stay consistent.
- Do it with the **printer idle**.
  - Making the fixtures does not command the printer.
  - But the calibration generator and the profile edits leave unsaved changes. The Print Advisor refuses proposals on profiles with unsaved changes, so discard them at the end (step "Clean up").
- Each fixture has **one plate**. Every object must be fully inside the plate (otherwise the CLI returns -52), with no automatic arrange after placing.
- **Clean up after every fixture**: select the original profile again and answer **Non salvare** when asked about the changes. A project-only change is saved in the 3MF. It must not stay in the profile.

### 2.3 The four fixtures (exact GUI steps)

**F-1 `k1c-pla.3mf`: K1C, PLA, standard settings**

1. **File → New project**.
2. Printer **Creality K1C 0.4 nozzle**, filament **Hyper PLA @Creality K1C 0.4 nozzle** (system), process **0.20mm Standard @Creality K1C 0.4 nozzle** (system). Use the plate type the user prints PLA on, and write it in `FIXTURES.md`.
3. **Importa 3MF/STL/STEP/SVG/OBJ/AMF** (Ctrl+I) → `resources\creality_models\ksr_fdmtest_v4.stl` (75 x 70 x 92 mm, Creality's own test model: overhangs, bridges, small details).
4. Leave it where the import puts it (centred). Change nothing else.
5. **Slice piatto**: no errors and no "outside the plate" warnings.
6. **Salva Progetto come** → `tests\regression\fixtures\k1c-pla.3mf`.

**F-2 `asa-soak-brim.3mf`: ASA large parts, soak, brim on 3 layers with the upper-layer gap, two close objects**

1. **New project**. Printer **Creality K1C 0.4 nozzle**, filament **ASA K1C - pezzi grandi** (user copy: `heat_soak_time` 10, `heat_soak_bed_temp` 100; check both values in the filament tab), process **0.20mm ASA K1C**, plate type **smooth PEI** (the one used for ASA).
2. Import `resources\creality_models\Block20XY.stl` **twice**, as two separate objects, not two instances of one object. [inference] Instances share their object's brim, so they would test something different.
3. Place them with the position fields of the object panel. Cube A centre X = 98, Y = 110; cube B centre X = 122, Y = 110. That leaves a 4 mm gap between the faces, so the 5 mm brims overlap. **Do not** use Disponi tutti gli oggetti.
4. Process tab, project-only changes (do not save the profile):
   - **Tipo di brim** = outer only;
   - **Larghezza brim** = 5;
   - **Distanza Brim-Oggetto** = 0;
   - **Layer del brim** = 3;
   - **Distanza brim-oggetto sui layer superiori del brim** = 0.3.
5. **Slice piatto**. In the preview check that layer 1 of the brim touches the cubes and layers 2-3 keep the gap.
6. **Salva Progetto come** → `tests\regression\fixtures\asa-soak-brim.3mf`.
7. Clean up: go back to the original process profile with **Non salvare**.
8. Write `asa-soak-brim.expected.json`. The keys are those that `check-gcode.mjs` knows (`check-gcode.mjs:336-343`). Do **not** set `layers_in_file`, because the baseline is a full G-code:
   ```json
   {
     "_source": "2 x Block20XY.stl 4 mm apart, ASA K1C - pezzi grandi, 0.20mm ASA K1C + brim edits, K1C; CLI slice of the pre-lean build",
     "heat_soak_time": 10,
     "heat_soak_bed_temp": 100,
     "gcode_flavor": "klipper",
     "machine_start_command": "START_PRINT",
     "support_air_filtration": true,
     "auxiliary_fan": true,
     "brim_layers": 3,
     "brim_object_gap": 0,
     "brim_layers_object_gap": 0.3,
     "brim_objects": 2,
     "brim_blocks_per_object_layer": 1
   }
   ```
   - `support_air_filtration`/`auxiliary_fan` are `true` in the current K1C fixture (`tests/print-advisor/fixtures/brim3-soak10.expected.json`).
   - Add `first_layer_z` after reading it from the first CLI G-code: the ASA profile may use a 0.6 mm first layer.
   - This fixture also covers what `tests/print-advisor/README.md` lists as "not covered": `brim_layers_object_gap` > `brim_object_gap` and two neighbouring objects.

**F-3 `supports-tree.3mf`: two objects with tree supports**

1. **New project**. Printer **K1C 0.4**, filament **Hyper PLA @Creality K1C 0.4 nozzle**, process **0.20mm Standard @Creality K1C 0.4 nozzle**.
2. Import `resources\creality_models\Overhang.stl` (75 x 25 x 41 mm) and `resources\creality_models\torus.stl` (28.5 x 28.5 x 5.7 mm). Rotate the torus 90° about X so it stands on its edge: its inner hole and lower half then need support.
3. Place them about 15 mm apart (for example Overhang centre X = 90, Y = 110; torus centre X = 160, Y = 110), fully on the plate.
4. Process → Supporto:
   - **Abilita supporti** on;
   - **Tipo** = **albero(auto)**;
   - **Stile** = default;
   - threshold angle as in the profile.
   - Project-only change. Check in the preview that **both** objects get tree supports.
5. **Slice piatto**, then **Salva Progetto come** → `tests\regression\fixtures\supports-tree.3mf`, then clean up with **Non salvare**.

**F-4 `calib-flow.3mf`: calibration plate**

1. Printer **K1C 0.4**, filament **Hyper PLA @Creality K1C 0.4 nozzle**, process **0.20mm Standard @Creality K1C 0.4 nozzle**.
2. **Calibrazione → Flusso → Passaggio 1** (`MainFrame.cpp:3717`, `Plater::calib_flowrate(1)`). It opens a new project with the flow-rate blocks and their per-object `print_flow_ratio`.
3. **Slice piatto**, then **Salva Progetto come** → `tests\regression\fixtures\calib-flow.3mf`.
4. Clean up: the generator changed the process (`layer_height`, `initial_layer_print_height`, `max_volumetric_extrusion_rate_slope`, ...). Go back to the saved profiles with **Non salvare**.
5. Why the flow-rate test: it is the only calibration that is fully stored in the 3MF (0.7). The temperature, PA and retraction towers need the GUI's calibration mode. Covering them is the optional GUI check below.

### 2.4 Optional extras (not part of the four)

- **`asa-soak0` (`heat_soak_bed_temp` = 0 resolution)**: the scenario from `tests/print-advisor/README.md`, "Fixture per le nuove funzioni".
- **GUI-only calibration check (temperature tower)**:
  1. at F0 and after F2/F7, run **Calibrazione → Temperatura** (230 → 190, step 5) in the GUI;
  2. **Slice piatto**, then **Esporta file G-code** → `out\<phase>\calib-temp.gui.gcode`;
  3. compare with `compare-gcode.mjs` against `baseline\F0\calib-temp.gui.gcode.gz`. The masks cover the GUI-only lines (section 3).

---

## 3. G-code normalisation and `tests/regression/compare-gcode.mjs`

### 3.1 Lines that change from run to run, or from phase to phase (verified sources)

| Rule id | Line | Source | Why it varies | Action |
|---|---|---|---|---|
| `generated_by` | `; generated by Creality_Print V7.3.0.0 on 2026-10-20 at 17:28:48` | `GCode.cpp:5274-5287`, `utils.cpp:1232-1237` (`header_slic3r_generated2`/`_cloud`), `Utils::local_timestamp()` | timestamp every run; app name and version change in F8 | replace with `; generated by <APP> on <TIMESTAMP>` |
| `creality_uuid` | `; creality_uuid: b542fb4d-...` | `GCode.cpp:5343-5347` (`boost::uuids::random_generator`) | random every run | replace with `; creality_uuid: <UUID>` |
| `task_id_pending` | `; creality_task_id_pending: 000...` | `GCode.cpp:5351-5368` | GUI only; removed by F1 | drop |
| `task_id` | `; creality_task_id: <hash>_<ms>_<plate>` | `GCode.cpp:5370-5374`, rewritten by `replace_task_id_placeholder_in_gcode` (`Plater.cpp:22106`) | GUI only, time-based; removed by F1 | drop |
| `profile_version` | `; profile_version: <x>` | `GCode.cpp:5289-5295` | GUI only, follows the downloaded profile package (F1 changes the updater) | drop (soft rule, kept with `--strict`) |
| `thumbnail_block` | `; THUMBNAIL_BLOCK_START` ... `; THUMBNAIL_BLOCK_END` and the `; <tag> begin` ... `; <tag> end` payload inside | `GCode/Thumbnails.hpp:83-110` | GUI only; rendered images can differ by GPU or driver | drop the whole block |
| `thumbnail_inline` | lines starting with `;gimage:` / `;simage:` and the bigtree `;` + 8 hex digits header ... `; bigtree thumbnail end` | `Thumbnails.hpp:69-78` | same (other thumbnail formats) | drop |
| `fixture_marker` | `; PRINT_ADVISOR_FIXTURE: ...` | `tests/print-advisor/make-fixture.mjs` | only in trimmed fixtures | drop |

Line endings: G-code is written with `\n` (`GCodeOutputStream`), but normalise `\r\n` → `\n` anyway, and drop one trailing empty line.

### 3.2 Lines that look variable but must NOT be masked (they are deterministic and are what the test is for)

- `; estimated printing time ...`, `; model printing time: ...; total estimated time: ...`, `M73 P.. R..` (GCodeProcessor post-pass, `GCodeProcessor.cpp:9075-9097`): computed from the moves. A change means the motion changed.
- `; total layer number`, `; max_z_height`, `MINX/MAXX/...`, `; filament used [mm|g|cm3]`, `; total filament cost`, `EXCLUDE_OBJECT_DEFINE ...` (object names come from the fixture).
- The whole `CONFIG_BLOCK`.
  - It is deterministic. It holds `filament_settings_id`, `print_settings_id` and `inherits_group`, which are names stored in the fixture.
  - `append_full_config` already drops the host and password keys (`GCode.cpp:11048-11062`).
  - A key that appears or disappears there after a phase **is** a regression to look at, unless the phase removed an option on purpose. Then add a phase mask (section 3.4) and say why.
- [inference] `{timestamp}`/`{year}`... placeholders and `random()` in custom G-code (`GCode.cpp:5537-5538`) would vary. The K1C start and end G-code do not use them. If a difference shows up there, add a rule to `masks.json`.

### 3.3 Script specification

```
node tests/regression/compare-gcode.mjs <baseline.gcode[.gz]> <candidate.gcode[.gz]> [options]
node tests/regression/compare-gcode.mjs --baseline-dir <dir> --candidate-dir <dir> [options]

options:
  --masks <file>        extra rules (default: tests/regression/masks.json when present)
  --phase <Fn>          also apply phase_rules up to this phase (section 3.4)
  --strict              disable soft rules (profile_version); EXTRA files become errors
  --max-diffs <n>       differing lines shown per file (default 20)
  --context <n>         context lines around each shown difference (default 2)
  --write-normalized <dir>  write <name>.norm.gcode for inspection
  --hash                print only "<sha256>  <file>" of the normalised content (used by slice-fixtures.mjs)
  --json                machine-readable summary on stdout instead of text
```

**Inputs**
- `.gz` is decompressed with `node:zlib` (`gunzipSync`). Text is read as `latin1`, so any byte round-trips; G-code is ASCII.
- **Directory mode** pairs files by name after removing the `.gz` suffix: `asa-soak-brim.gcode(.gz)`.
  - A baseline file without a candidate → MISSING.
  - A candidate without a baseline → EXTRA (informational, unless `--strict`).

**Normalisation (in this order)**
1. Split on `/\r?\n/`. Remove one trailing empty element.
2. Block filter: drop from a line starting with `; THUMBNAIL_BLOCK_START` to the matching `; THUMBNAIL_BLOCK_END` (inclusive), and from a bigtree header to `; bigtree thumbnail end`. Drop `;gimage:` and `;simage:` lines.
3. Line rules from 3.1 plus `masks.json`, as `{ id, pattern, replace | null (= drop), soft?: bool, expect?: "once" | "any" }`. The first matching rule wins.
4. Count the matches per rule and file. For a rule with `expect: "once"` (`generated_by`, `creality_uuid`), a count other than 1 in a CLI file is a **WARN** ("header format changed?"). It does not fail the comparison.

**Comparison**
- If the normalised SHA-256 values are equal → IDENTICAL.
- Otherwise walk both line arrays.
  - When the line counts match, list every differing index (up to `--max-diffs`, then a count).
  - When they differ, show the first divergence and both lengths. Then run the CONFIG diff below. Do not attempt a full LCS diff: the files are tens of MB.
- Locate each shown difference by tracking, while scanning:
  - the section: `HEADER` (until `; HEADER_BLOCK_END`), `PREAMBLE`, `EXECUTABLE` (between `; EXECUTABLE_BLOCK_START/END`), `FOOTER`, `CONFIG` (between `; CONFIG_BLOCK_START/END`);
  - in `EXECUTABLE`, the layer index (count of `;LAYER_CHANGE`) and the last `;Z:` value;
  - the last `;TYPE:` value (for example "layer 2, z 0.6, TYPE Brim");
  - the object from `; printing object <name>`.
- **CONFIG key diff**, always printed when the files differ: parse `; key = value` lines inside `CONFIG_BLOCK` in both files and list added, removed and changed keys with both values (values truncated to 120 characters).

**Text output (example)**

```
== k1c-pla        IDENTICAL  sha256 3f2a...  412318 lines  masked: generated_by=1 creality_uuid=1
== asa-soak-brim  DIFFERENT  2 of 98211 lines differ (first: line 5120, EXECUTABLE layer 1 z=0.40 TYPE Brim object Block20XY.stl_id_1)
   - 5120: G1 X96.112 Y99.876 E.02311
   + 5120: G1 X96.098 Y99.871 E.02305
   CONFIG: changed brim_layers_object_gap: 0.3 -> 0.35
== supports-tree  MISSING    candidate not found
RESULT: 1 identical, 1 different, 1 missing, 0 extra, 0 warnings
```

**Exit codes**: `0` all IDENTICAL; `1` at least one DIFFERENT; `2` usage error; `3` input unreadable or MISSING (and EXTRA with `--strict`). When `1` and `3` both apply, `3` wins.

**Module use**: export `normalize(text, rules)` and `compareFiles(a, b, opts)`, so `slice-fixtures.mjs` and `verify-phase.mjs` can import them. The CLI is a thin wrapper.

### 3.4 `tests/regression/masks.json`

```json
{
  "rules": [
    { "id": "generated_by",    "pattern": "^; generated by .+ on .+$",        "replace": "; generated by <APP> on <TIMESTAMP>", "expect": "once" },
    { "id": "creality_uuid",   "pattern": "^; creality_uuid: .*$",            "replace": "; creality_uuid: <UUID>",             "expect": "once" },
    { "id": "task_id_pending", "pattern": "^; creality_task_id_pending: .*$", "replace": null },
    { "id": "task_id",         "pattern": "^; creality_task_id: .*$",         "replace": null },
    { "id": "profile_version", "pattern": "^; profile_version: .*$",          "replace": null, "soft": true },
    { "id": "fixture_marker",  "pattern": "^; PRINT_ADVISOR_FIXTURE: .*$",    "replace": null }
  ],
  "phase_rules": {}
}
```

- `phase_rules` is keyed by phase (`"F3": [...]`), for **justified** differences only. Each entry must have a `"why"` field.
- `--phase F3` applies the phase rules of every phase up to F3.
- Today no phase is expected to change the G-code. F1 removes only the GUI-only task id line, which is not in the CLI baseline.

### 3.5 Baseline manifest (`baseline/F0/manifest.json`)

```json
{
  "phase": "F0", "tag": "pre-lean", "commit": "14528e0ed770d32a49dc49028353b789914399a0",
  "dll": { "mtime": "...", "bytes": 103692288, "sha256": "..." },
  "made": "<ISO date>", "node": "v24.13.1", "runs": 2, "deterministic": true,
  "fixtures": {
    "k1c-pla": { "fixture_sha256": "...", "gcode": "k1c-pla.gcode.gz", "lines": 0, "layers": 0,
                 "sha256_normalized": "...", "preset_root": "...\resources\profiles", "seconds": 0 }
  }
}
```

- `fixture_sha256` makes sure that a later phase compares against the same 3MF.
- `verify-phase.mjs` refuses to compare when a fixture's hash changed. The fix is to regenerate the baseline at the tag (section 7, step 3), never at the candidate.

---

## 4. One verification script: `tools/verify-phase.mjs`

Node 20+ (installed: v24.13.1, npm 11.8.0) with no dependencies. It works from any cwd: it finds the repository root from its own location.

- **Why `.mjs` and not `.ps1`**: every check it chains is already a Node script; the only Windows-specific part is the network watch, which stays a separate `.ps1` (section 5).
- It **never builds**, never starts or closes `CrealityPrint.exe` (except the CLI slices through `slice-fixtures.mjs`), and never calls a tool that changes something.

### 4.1 Command line

```
node tools/verify-phase.mjs --phase F1 [--slice] [--exe <path>] [--skip build,grep,gcode,web,sidecar,smoke]
                            [--require-live] [--report <file>] [--no-manual]
```

- `--phase` (required): `F0`..`F8`. It selects `tools/verify/removed-<phase>.txt` and the `phase_rules` in `masks.json`.
- `--slice`: run `slice-fixtures.mjs --out tests/regression/out/<phase>` first. Without it, the G-code step compares what is already in `out/<phase>`.
- `--require-live`: a smoke test without the app becomes FAIL instead of SKIP.
- `--report`: default `tests/regression/out/<phase>/verify-report.txt`, the same text as the console.

### 4.2 Steps, commands and expected output

Each step prints `ok  `, `WARN`, `SKIP` or `FAIL` lines and a step summary.

**Step 0: context (always)**
- Commands:
  - `git rev-parse --abbrev-ref HEAD`;
  - `git rev-parse --short HEAD`;
  - `git status --porcelain`;
  - `git describe --tags --always`.
- Expected for a phase: branch `lean`; dirty files listed as WARN (not FAIL; the user may verify before committing).

**Step 1: build is current**
- Files:
  - `build\src\Release\CrealityPrint_Slicer.dll` (FAIL if missing);
  - `build\src\Release\CrealityPrint.exe` (FAIL if missing).
- Newest input:
  - mtime over `git ls-files -- src cmake CMakeLists.txt version.inc`;
  - plus `git ls-files -o --exclude-standard -- src` (new untracked sources);
  - files deleted in the working tree are skipped.
- **FAIL** if any input is newer than the DLL: `build is stale: src/slic3r/GUI/GUI_App.cpp (17:02) is newer than CrealityPrint_Slicer.dll (16:08)`.
  - Exception: if only files outside `src/libslic3r` and `src/slic3r` changed (for example `src/CrealityPrint_app_msvc.cpp`), compare them with the exe.
- **WARN** if the DLL is older than the HEAD commit time but newer than every source. That is the normal case today (16:08 DLL, 17:14 commit).
- Info: the `localization/i18n/it/*.mo` files are rebuilt by `build_exe_only.bat`. Report the `.mo` mtime against `localization/i18n/it/*.po`.
- Info: whether a `CrealityPrint.exe` from `build\src\Release` is running, from `Get-CimInstance Win32_Process -Filter "Name='CrealityPrint.exe'"` (`ProcessId`, `ExecutablePath`) run through `powershell -NoProfile -Command`.
- Expected today:
  - `ok   build: CrealityPrint_Slicer.dll 2026-10-20 16:08:29 newer than all 4xxx inputs (newest src/... 15:5x)`;
  - `WARN build: DLL older than HEAD commit 17:14:42 (only docs/scripts changed after the build)`.

**Step 2: removed names are gone**
1. Read `tools/verify/removed-<phase>.txt` (format in 4.3). If the file is missing: SKIP for F0, FAIL for F1 to F8.
2. For each `word:` / `re:` entry run:
   ```
   git grep -n -I --untracked -w -F -e <Name> -- . ":(exclude)doc/**" ":(exclude)**/*.md" ":(exclude)tools/verify/**" ":(exclude)tests/regression/**" ":(exclude)localization/i18n/*.po" ":(exclude)localization/i18n/*.pot"
   ```
   (for `re:` use `-E` and no `-F -w`).
   - `git grep` exit 1 → `ok`.
   - Exit 0 → FAIL with the first 20 `file:line: text` matches.
   - Any other exit → FAIL "git grep error".
3. For each `file:` entry, FAIL if the path still exists in the working tree.
4. **`localization/i18n/list.txt` check**: every non-empty line not starting with `#` must be an existing file. Today: 2518 entries, 0 missing (verified). Expected: `ok   list.txt: 2518 entries, all present` (the number shrinks in later phases).
5. **CMake reference check**: grep `src/**/CMakeLists.txt` and `src/**/*.cmake` for `.cpp`/`.hpp`/`.h` names that do not exist (regex `[\w/]+\.(cpp|hpp|h)`, relative to the folder of the CMake file). FAIL lists them. This catches a source removed from disk but not from CMake before the build does.

**Step 3: G-code**
1. If `--slice`: `node tests/regression/slice-fixtures.mjs --out tests/regression/out/<phase> [--exe ...]`. Exit 1 → FAIL; per-fixture errors are shown.
2. Fixture integrity: the sha256 of each `fixtures/*.3mf` must equal the value in `baseline/F0/manifest.json` → otherwise FAIL "fixture changed, regenerate the baseline at pre-lean".
3. `node tests/regression/compare-gcode.mjs --baseline-dir tests/regression/baseline/F0 --candidate-dir tests/regression/out/<phase> --phase <phase>`
   - Expected: `RESULT: 4 identical, 0 different, 0 missing, 0 extra, 0 warnings`, exit 0.
4. `node tests/print-advisor/check-gcode.mjs`: the existing trimmed fixture. Expected today (run on 2026-10-20): `PASSED: 20 ok, 0 failed`, exit 0.
5. For each `tests/regression/fixtures/<name>.expected.json`: `node tests/print-advisor/check-gcode.mjs tests/regression/out/<phase>/<name>.gcode --expected tests/regression/fixtures/<name>.expected.json`
   - Expected: `PASSED: N ok, 0 failed`, where N is recorded at F0 in `manifest.json`.
   - Includes: one soak block, `M190 S100`, `G4 P600000`, `START_PRINT` right after; brim on layers 0-2 only; upper-layer gap >= 0.3 mm (tolerance 0.05) from both cubes; no brim inside an island.

**Step 4: web patches**
- `node tools/web-patches/apply-web-patches.mjs --check` (writes nothing).
- Expected today (run on 2026-10-20): the table of patches `1, 2, 2b, 3, 4, 5, 6, 7, 8, 9, 10` all `applied`, last line `11 applied, 0 applicable, 0 NOT FOUND, 0 AMBIGUOUS`, exit 0.
- Gate: exit 0 and `0 NOT FOUND, 0 AMBIGUOUS`.
  - The `applied` count may change only in a phase that removes a patched page on purpose (F2/F7). Then `patches.mjs` must be updated in the same phase.
  - Save the output to `out/<phase>/web-patches.txt`.

**Step 5: sidecar**
1. `npm run typecheck` with cwd `tools/print-advisor-mcp`, i.e. `tsc --noEmit -p .`.
   - Expected: no compiler output, exit 0 (verified on 2026-10-20 with `node node_modules/typescript/bin/tsc --noEmit -p .`).
   - `npm` must be run as `npm.cmd` (or `shell: true`) from Node on Windows.
2. `npm run smoke`.
   - The first line is `Print Advisor MCP smoke test (app running)` or `(... app not running)` (`scripts/smoke.mjs:136`).
   - The script calls only read-only tools (`READ_ONLY` allowlist, lines 33-46) and compares the tool list with `scripts/tools-expected.json` (50 tools today).
   - Expected with the dev build open and **Vista → Enable Print Advisor MCP** on: `PASSED: N ok, 0 failed`, where N is recorded at F0 in `baseline/F0/smoke.txt`.
   - "app not running" → SKIP (FAIL with `--require-live`).
   - The smoke test reads the newest `endpoint.json` under `%APPDATA%\Creality\Creality Print\*\cache\print-advisor`. The stable and dev copies share it, so the dev build must be the instance with the MCP on. verify-phase checks that the `pid` in the descriptor belongs to a `CrealityPrint.exe` under `build\src\Release`, and WARNs otherwise ("smoke ran against the stable copy").

**Step 6: manual checklist (printed, unless `--no-manual`)**

The text below is printed, then `Esito manuale registrato? (s/n)` when stdin is a TTY. The answer is written to the report; it does not change the exit code.

```
CONTROLLO MANUALE (circa 3 minuti) - fase <phase>, build <dll mtime>
[ ] Avvio senza errori (nessuna finestra di errore, log senza "error" nuovi rispetto a F0)
[ ] Prepara: carico un modello e faccio "Slice piatto"
[ ] Anteprima: si vede il percorso, i layer scorrono
[ ] Dispositivo: K1C collegata, telemetria (temperature, ventole) e telecamera
[ ] Si apre la finestra di invio (senza inviare)
[ ] Print Advisor attivo (Vista -> Enable Print Advisor MCP) e smoke "app running" PASSED
[ ] Si aprono le Preferenze
[ ] Chiusura pulita (nessun processo CrealityPrint.exe/msedgewebview2.exe rimasto)
Solo dopo F1, F3, F4: rete 5 minuti -> powershell -NoProfile -ExecutionPolicy Bypass -File tools\net-watch.ps1 -Minutes 5
Solo se la fase tocca Dispositivo o invio: doc/MCP/printer-test-checklist.md, a stampante ferma
```

### 4.3 Removed-names list format (`tools/verify/removed-<phase>.txt`, UTF-8)

```
# F1 - privacy and network. One entry per line; "#" starts a comment.
word:AnalyticsDataUploadManager          # whole-word, fixed string (git grep -w -F)
word:check_new_version
re:creality_task_id_pending              # extended regex (git grep -E)
file:src/slic3r/GUI/AppUpdater.cpp       # must not exist
file:tools/updater
allow:src/slic3r/GUI/AnalyticsDataUploadManager.hpp   # path where matches are still allowed (stub kept on purpose)
```

- `allow:` entries are added as `":(exclude)<path>"` pathspecs for every `word:`/`re:` of that file.
- The F1 entries come from `doc/specs/F1-privacy-rete-spec.md`.
- Note that F1 item 1 first keeps `AnalyticsDataUploadManager` as a no-op class. So for F1 the class name is `allow:`-ed in its own files, and only the removed members (`send_print_send_event`, `uploadSlice822ClickEvent`, ...) are `word:` entries.

### 4.4 Exit codes and report

- `0`: no FAIL (WARN and SKIP allowed).
- `1`: at least one FAIL.
- `2`: usage or environment error (not the repository root, Node < 20, git missing).
- Last line: `VERIFY <phase>: PASSED (n ok, n warn, n skip)` or `VERIFY <phase>: FAILED (n fail: build, gcode...)`.
- The full report goes to `--report`. For F0 it is copied to `baseline/F0/verify-report.txt` as the reference.

---

## 5. Network check on Windows without installing tools

### 5.1 Which processes

- `CrealityPrint.exe` (the dev build under `build\src\Release`, or the stable one under `C:\CrealityPrint-Advisor`).
- **Every descendant** of it, above all `msedgewebview2.exe`.
  - The web pages (Home, Device, send page, login) run in WebView2 processes (`src/slic3r/GUI/Widgets/WebView.cpp`).
  - Google Analytics/Firebase and the Baidu probe therefore come from `msedgewebview2.exe`, **not** from `CrealityPrint.exe`.
  - A filter on `CrealityPrint.exe` alone misses them.
  - The WebView2 browser process is a child of `CrealityPrint.exe`; its renderers are children of that browser process. Walk `ParentProcessId` recursively, and refresh the tree on every poll, because WebView2 processes start late.

### 5.2 Hosts to flag (verified by grep of `src/` and `resources/web/`)

| Pattern (suffix match on the host name) | Seen in | Why |
|---|---|---|
| `crealitycloud.com`, `crealitycloud.cn` (`api`, `api-dev`, `admin-pre`, `pre`, `pre-2`, `dev`, `dev2`, `www`, `cxagent`, `cxagent-dev`, `ai-test`, `makenow`, `makenow-dev`, `share`, `cp`) | `src/slic3r/GUI/*`, `resources/web/*` | cloud API, analytics, chat, model library |
| `creality.com`, `creality.cn` (`id`, `id-pre`, `id-dev`, `file-cdn`, `pic-cdn`, `pic2-cdn`, `store`, `www`, `support`, `zentao`, `wiki`) | idem | login, CDN, shop. `wiki.creality.com` is opened only by help links: flag it, but count it as user-initiated if a help link was clicked |
| `cxswyjy.com` (`as.`, `devdata.`) | `src/slic3r/GUI/GUI.cpp:763` and others | Creality online support / data |
| `googletagmanager.com`, `google-analytics.com`, `analytics.google.com`, `firebase.googleapis.com`, `firebaseinstallations.googleapis.com`, `firebaseio.com`, `app-measurement.com`, `doubleclick.net` | `resources/web/homepage` bundles | Google Analytics / Firebase |
| `baidu.com` | `resources/web/deviceMgr/assets/BZCDzYbb.js` ("online" probe that fetches `https://www.baidu.com/`) and `resources/web/homepage/assets/index-806b308b.js` | connectivity probe |
| `aliyuncs.com` | OSS upload code (`-internal.aliyuncs.com`) | cloud upload |
| `bambulab.com`, `bambulab.cn` | dead Bambu code | should never appear |

- **Informational, not a failure**:
  - `*.microsoft.com`, `*.msedge.net`, `*.windows.com`, `*.azureedge.net`, `*.smartscreen.microsoft.com`, `msftconnecttest.com`: WebView2 and Windows itself;
  - private and link-local addresses (`10/8`, `172.16/12`, `192.168/16`, `169.254/16`, `fe80::/10`, `fc00::/7`): the K1C and the LAN;
  - `127.0.0.1`/`::1`: the local server on port 13666 and the Print Advisor endpoint.
- Any other public address is a **WARN**, listed for review.

### 5.3 By hand: Resource Monitor (no install, no admin)

1. Start Creality Print (the build under test). Close the other browsers if possible, because the DNS cache is shared by the whole system.
2. `Win+R` → `resmon` → **Rete** tab.
3. In **Processi con attività di rete**, tick `CrealityPrint.exe` and **every** `msedgewebview2.exe`. To be sure the WebView2 processes belong to Creality Print, compare their PIDs with Task Manager → Dettagli, "PID processo padre" column, or use the script below.
4. Panel **Attività di rete**: the **Indirizzo** column shows the host name (from the DNS cache) and the bytes sent/received. Panel **Connessioni TCP**: remote address and port.
5. Use the app normally for 5 minutes: Home, Prepara, Dispositivo, send dialog (without sending), Preferenze.
6. Write down every name that matches 5.2. Pass for F1/F3/F4: nothing from the flag table.

### 5.4 Scripted: `tools/net-watch.ps1` (Windows PowerShell 5.1, no admin)

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools\net-watch.ps1 [-Minutes 5] [-IntervalSeconds 1]
           [-PathPrefix build\src\Release\] [-OutCsv <file>]
```

**Behaviour**
1. Find root processes: `Get-CimInstance Win32_Process -Filter "Name='CrealityPrint.exe'"`, kept only if `ExecutablePath` starts with `-PathPrefix` (when given). None → exit 2, `Creality Print non trovato`.
2. Each poll:
   1. rebuild the descendant PID set from `Get-CimInstance Win32_Process` (`ProcessId`, `ParentProcessId`, `Name`);
   2. `Get-NetTCPConnection -ErrorAction SilentlyContinue`, filtered on `OwningProcess` in the PID set;
   3. skip `0.0.0.0`, `::`, `127.0.0.1` and `::1`;
   4. record new `(RemoteAddress, RemotePort, process name, PID)` with first and last seen time and the states seen.
3. Each poll also adds `Get-DnsClientCache` A/AAAA records (`Type` 1/28) to a map IP → names. The map accumulates, because entries expire.
4. At the end:
   - resolve every remote IP through the map, or through `Resolve-DnsName -Type PTR -QuickTimeout -DnsOnly` as a fallback (PTR names of CDNs are often generic, so the cache map is the main source);
   - classify each IP as LAN / LOCAL / INFO / FLAG / WARN (5.2).
5. **DNS-only pass**:
   - list the `Get-DnsClientCache` entries whose `Entry` matches the flag patterns and that are **new compared with the snapshot taken at start**;
   - report them as `FLAG (dns only)`;
   - this catches connections shorter than the poll interval, and QUIC/UDP (`Get-NetUDPEndpoint` has no remote address);
   - it is system-wide, so other programs can cause false positives: the report says so.
6. Print a table sorted by class, write `-OutCsv` (default `tests\regression\out\net-watch-<yyyyMMdd-HHmm>.csv`) and a text summary.
7. **Exit codes**: 0 = no FLAG; 1 = at least one FLAG; 2 = app not found or error. WARN does not change the exit code.

**Core of the loop (reference; the implementation may differ)**

```powershell
$flag = '(^|\.)(crealitycloud\.(com|cn)|creality\.(com|cn)|cxswyjy\.com|googletagmanager\.com|google-analytics\.com|analytics\.google\.com|firebase(installations)?\.googleapis\.com|firebaseio\.com|app-measurement\.com|doubleclick\.net|baidu\.com|aliyuncs\.com|bambulab\.(com|cn))$'
$info = '(^|\.)(microsoft\.com|msedge\.net|windows\.com|azureedge\.net|msftconnecttest\.com)$'
function Get-Tree([int[]]$roots) {
  $all = Get-CimInstance Win32_Process | Select-Object ProcessId, ParentProcessId, Name
  $set = New-Object 'System.Collections.Generic.HashSet[int]'
  foreach ($r in $roots) { [void]$set.Add($r) }
  do { $added = 0
       foreach ($p in $all) { if ($set.Contains([int]$p.ParentProcessId) -and $set.Add([int]$p.ProcessId)) { $added++ } }
  } while ($added -gt 0)
  return $set
}
$dns0 = @{}; foreach ($d in Get-DnsClientCache) { $dns0[[string]$d.Entry + '|' + [string]$d.Data] = $true }
$ipNames = @{}; $seen = @{}
$end = (Get-Date).AddMinutes($Minutes)
while ((Get-Date) -lt $end) {
  $set = Get-Tree $rootPids
  foreach ($d in Get-DnsClientCache) {
    if ($d.Type -ne 1 -and $d.Type -ne 28) { continue }
    if (-not $ipNames.ContainsKey($d.Data)) { $ipNames[$d.Data] = @{} }
    $ipNames[$d.Data][$d.Entry] = $true
  }
  foreach ($c in Get-NetTCPConnection -ErrorAction SilentlyContinue) {
    if (-not $set.Contains([int]$c.OwningProcess)) { continue }
    if (@('0.0.0.0', '::', '127.0.0.1', '::1') -contains $c.RemoteAddress) { continue }
    $k = [string]$c.RemoteAddress + ':' + [string]$c.RemotePort
    if (-not $seen.ContainsKey($k)) {
      $seen[$k] = [pscustomobject]@{ Ip = $c.RemoteAddress; Port = $c.RemotePort; Pid = $c.OwningProcess; First = Get-Date; Last = Get-Date }
    } else { $seen[$k].Last = Get-Date }
  }
  Start-Sleep -Seconds $IntervalSeconds
}
```

**Limitations (written into the report)**
- A connection shorter than the interval is caught only by the DNS-only pass.
- Connections in `TIME_WAIT` lose their owning PID (shown as 0).
- [inference] WebView2 may use DNS-over-HTTPS when the system DNS supports it ("automatic" mode). Its names then never reach the Windows DNS cache, and only the TCP list with PTR names remains. If the user wants certainty, Resource Monitor (5.3) on the same session is the cross-check.
- At F0 the script is **expected to exit 1**: the pre-lean build talks to Creality and Google. Its CSV is saved as `baseline/F0/net-watch.csv`, the "before" picture for F1.

---

## 6. Tag and branch

State verified on 2026-10-20:
- branch `advisor`, HEAD `14528e0ed770d32a49dc49028353b789914399a0`, level with `origin/advisor` (0 ahead, 0 behind);
- no `pre-lean` tag and no `lean` branch;
- untracked: `doc/cleanup-plan-2026-10-20.md`, `doc/specs/`, `doc/branding/`, `.vscode/creality-build-button/`, the `.vsix` and `package/runtime/MicrosoftEdgeWebView2RuntimeInstallerX64.exe`.

```bat
git -C <repo> status --porcelain
rem expected: only "??" lines (untracked); any " M"/"M " line -> stop and ask the user

git -C <repo> rev-parse HEAD
rem expected: 14528e0ed770d32a49dc49028353b789914399a0

git -C <repo> tag -a pre-lean 14528e0ed770d32a49dc49028353b789914399a0 -m "Last commit before the lean cleanup (F0 baseline)"
git -C <repo> switch -c lean pre-lean
```

1. **Tag before anything else.** The baseline is generated with the binaries built from this commit (0.10).
2. **First commit on `lean`** (after section 7): `F0: regression fixtures, baseline and verification tools`, containing:
   - `tests/regression/**` (without `out/`);
   - `tools/verify-phase.mjs`, `tools/verify/`, `tools/net-watch.ps1`;
   - the `.gitignore` line;
   - and, if the user agrees, `doc/cleanup-plan-2026-10-20.md` and `doc/specs/*.md`.
   - **Do not commit** the `.vsix`, `.vscode/creality-build-button/` (CLAUDE.md: do not use that old extension) or the WebView2 installer (60+ MB; plan question 8).
3. **Pushing** `git push origin pre-lean` and `git push -u origin lean` is a user decision.
   - The tag on the remote protects the reference point.
   - CLAUDE.md asks to commit or push only on request.
4. **Stable copy before starting.**
   - `C:\CrealityPrint-Advisor` is still `014576c8` plus uncommitted changes (`stable-version.txt`, DLL from 00:32).
   - Recommendation: run **Pubblica stabile** with the pre-lean build, with the printer idle, so the copy used for printing has all the 2026-10-20 fixes during the cleanup.
   - The script closes the stable instance, so not during a print.
5. **Frozen pre-lean slicer** (to regenerate the baseline later):
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\publish-stable.ps1 -Dest C:\CrealityPrint-prelean -NoShortcut
   ```
   - `publish-stable.ps1` takes `-Dest` and `-NoShortcut` (lines 25-27). It copies the exe, the DLLs, `mesa\` and the **real** `resources\` (258 MB), so the copy keeps the pre-lean profiles even after F1/F6 change `resources\`.
   - Use it **only** with `--cli` (`slice-fixtures.mjs --exe C:\CrealityPrint-prelean\CrealityPrint.exe`). Opened as a GUI it would share the user data dir.
   - [inference] The script has no other destination-specific behaviour (it writes `stable-version.txt` in the destination). Read it once before running.

---

## 7. F0 runbook (order matters)

| # | Step | Who | Gate |
|---|---|---|---|
| 1 | Printer idle, no print in progress. Close the stable copy. Start the dev build (`Avvia CP`). | user | - |
| 2 | Tag and branch (section 6, commands 1-2). | agent/user | tag exists |
| 3 | Implement `compare-gcode.mjs`, `slice-fixtures.mjs`, `masks.json`, `verify-phase.mjs`, `net-watch.ps1`, `.gitignore` line. **No C++ change, no build.** | agent | `node --check` of each `.mjs` |
| 4 | Make the 4 fixtures in the GUI (section 2.3), then close the extra projects, with **Non salvare** on profile changes. | user | 4 files in `fixtures/` |
| 5 | **CLI dry run**: `node tests/regression/slice-fixtures.mjs --out tests/regression/out/F0 --runs 2`. | agent | exit 0, `deterministic: true`, log root ends in `resources\profiles` |
| 6 | CONFIG check of the CLI output: `heat_soak_time = 10`, `heat_soak_bed_temp = 100`, `brim_layers = 3`, `brim_layers_object_gap = 0.3`, `brim_object_gap = 0`, `filament_settings_id` = "ASA K1C - pezzi grandi" in `asa-soak-brim`; `enable_support = 1`, `support_type = tree(auto)` in `supports-tree`; for `calib-flow`, no slicing error (its per-object `print_flow_ratio` values are not in the CONFIG block, which holds only the global config). | agent | values present |
| 7 | GUI-against-CLI sanity check (informational): in the GUI open `k1c-pla.3mf`, **Slice piatto**, **Esporta file G-code** → `out\F0\k1c-pla.gui.gcode`. Then `compare-gcode.mjs` against the CLI file. Expected: identical, or differences only in the header/CONFIG keys listed in 1.6. Record the result in `manual-checklist.md`. | user + agent | explained |
| 8 | `node tests/print-advisor/check-gcode.mjs out\F0\asa-soak-brim.gcode --expected fixtures\asa-soak-brim.expected.json` → PASSED; add `first_layer_z`; record N. | agent | PASSED |
| 9 | Gzip the G-codes into `baseline/F0/` and write `manifest.json` (with `fixture_sha256`, `sha256_normalized`, N of step 8). | agent | - |
| 10 | Enable the Print Advisor MCP in the dev build. `npm run smoke` in `tools/print-advisor-mcp` → `baseline/F0/smoke.txt` (`(app running)`, `PASSED`). | agent | PASSED |
| 11 | `apply-web-patches.mjs --check` → `baseline/F0/web-patches.txt` (11 applied). | agent | exit 0 |
| 12 | Manual check (4.2 step 6) with the times: start-up to Prepara ready, slicing time of `k1c-pla`. Write `baseline/F0/manual-checklist.md`. | user | all ticked |
| 13 | `net-watch.ps1 -Minutes 5 -PathPrefix build\src\Release\` while using Home, Device and the send dialog → `baseline/F0/net-watch.csv/.txt` (FLAGs expected). | user + agent | saved |
| 14 | `node tools/verify-phase.mjs --phase F0` (no removed list: step 2 SKIP; G-code compares `out/F0` with itself, so IDENTICAL) → `baseline/F0/verify-report.txt`. | agent | PASSED |
| 15 | Optional: freeze `C:\CrealityPrint-prelean` (6.5) and check it reproduces the baseline: `slice-fixtures.mjs --exe C:\CrealityPrint-prelean\CrealityPrint.exe --out out\F0-frozen`, then `compare-gcode.mjs --baseline-dir baseline\F0 --candidate-dir out\F0-frozen`. | agent | 4 identical |
| 16 | First commit on `lean` (6.2). | user approves | - |

**If step 5 fails** (crash, a non-zero code on a fixture that slices in the GUI, or nondeterminism):
1. Read `out\F0\<name>.log`; retry that fixture once.
2. If it is a CLI-only problem, switch to fallback A (1.9) for the affected fixture(s). Mark them `"source": "gui"` in `manifest.json`; `verify-phase.mjs` then prints the GUI export steps instead of slicing them.
3. Do not fix the CLI in C++ during F0: F0 changes no code.

---

## 8. Open questions and risks

1. **[inference] CLI on Windows is not yet exercised in this fork.**
   - The code path is the one Creality uses for cloud slicing on Linux.
   - `PartPlateList` with a null `Plater` guards its `wxGetApp()` calls (`PartPlate.cpp:258` and similar), but there are 67 `wxGetApp()` calls in `PartPlate.cpp`. One unguarded call on the CLI path would crash. Step 5 answers this.
2. **[inference] Preset merge keeps the user-copy values** (1.4.3). Step 6 answers this.
3. **Determinism of tree supports and Arachne**: generally deterministic in Orca-based slicers, not proven here. `--runs 2` in step 5 answers this. If not deterministic, the supports fixture is compared with a per-phase tolerance rule or dropped. That is a user decision.
4. **Baseline in git** (2.1): about 3-6 MB of `.gz` [inference] against hash-only. User decision.
5. **Network check accuracy**: polling plus the DNS cache is good enough to find "talks to Creality/Google", not to prove "never". For proof, a packet capture (`pktmon`, built in but needs admin) is the next step. Not proposed by default.
6. **Smoke against the right instance**: both copies write the same `endpoint.json`. Keep only the dev build open during verification (4.2 step 5).
7. **`close-build-instance.ps1` kills CLI slices** under `build*` (1.3). Never build while `slice-fixtures.mjs` runs.
8. **Fixture plate type and ASA profile drift**:
   - The user copy "ASA K1C - pezzi grandi" may be edited later; that does not matter, the 3MF keeps the values.
   - If it is renamed or deleted, reopening the fixture in the GUI shows it as a project preset. Harmless.
9. **F8 rename**: masks already cover the `generated by` app name and version. `--allow-newer-file` covers the 3MF version check (1903-1908). If F8 renames `CrealityPrint.exe`, pass `--exe`.

---

## 9. Files created by F0 (all new; no existing file changes except `.gitignore`)

| File | Purpose |
|---|---|
| `tests/regression/README.md` | short Italian how-to (run, regenerate, masks) |
| `tests/regression/masks.json` | normalisation rules (3.4) |
| `tests/regression/compare-gcode.mjs` | compare normalised G-code (3.3) |
| `tests/regression/slice-fixtures.mjs` | CLI runner (1.8) |
| `tests/regression/fixtures/*.3mf`, `asa-soak-brim.expected.json`, `FIXTURES.md` | fixtures made by the user (2.3) |
| `tests/regression/baseline/F0/*` | baseline G-code (gz), manifest, smoke, web patches, manual checklist, network CSV, verify report |
| `tools/verify-phase.mjs` | single verification entry point (4) |
| `tools/verify/removed-F1.txt` (then one per phase) | names that must be gone (4.3); written at the start of each phase, not in F0 |
| `tools/net-watch.ps1` | network watch (5.4) |
| `.gitignore` | add `tests/regression/out/` |

Optional VS Code task "Verifica fase": `node tools/verify-phase.mjs --phase <Fn>` (the phase from a task input), with `options.statusbar`, following the CLAUDE.md rules (process type, no `.bat` needed).
