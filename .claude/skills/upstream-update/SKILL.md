---
name: upstream-update
description: "Bring a new upstream Creality Print release into MentorSlicer (fork of Creality Print 7.3.0 with the Print Advisor MCP). Run only when the user asks for it, e.g. \"aggiorna Creality Print alla nuova versione\", \"aggiorna a Creality 7.x\", \"upstream update\", \"merge the new Creality tag\"."
disable-model-invocation: true
argument-hint: "[creality tag, e.g. v7.4.0]"
---

Follow this procedure step by step (background and rationale: `doc/MCP/upstream-update-plan.md`). At every **STOP** ask the user (in Italian, one question at a time) and wait.

## Hard rules

- The MentorSlicer repository (`MirkoUgoliniDev/MentorSlicer`) is **public** and its history is a single root commit (`65e6380`, "MentorSlicer 1.0.1"). Creality's history contains secrets that were removed on purpose. **No Creality commit or history may ever enter it**: never add a Creality remote, never fetch or pull a Creality ref, never merge, rebase or cherry-pick a Creality commit. The 3-way merge and all checks happen in a **separate scratch clone**; only the final, already patched tree comes back, as a commit without parents (step 5), and becomes **one** MentorSlicer commit.
- Never push a ref whose `git rev-list --max-parents=0 <ref>` is anything other than `65e6380a120518dd540f448eada48566894ff6a8` alone. The local tags `backup/pre-public-20261025` and `pre-audit-20261024` hold the old history (root `4c67b6d`) and must stay local. Never `git push --tags`, never force-push, never push without asking.
- Never commit with unresolved conflicts. Never edit `.claude/` files as part of the update.
- Keep the removals of the cleanup phases F1–F9 (`tools/verify/removed-F*.txt`) and the F8 naming rules of `CLAUDE.md` ("Nome MentorSlicer (F8)"): Creality code we removed stays removed, unless the user decides otherwise at a STOP.
- Shell: the agent's primary shell is PowerShell 7. Chain dependent commands with `&&`. Run `.bat` files as shown in step 6 (`cmd.exe /d /c '...'` with single quotes; in Git Bash the inner double quotes break). CRLF sources: preserve existing line endings. Git prints paths with spaces in quotes: use `-z` or `-c core.quotePath=false` when a command consumes paths.

## Recorded base

- The MentorSlicer tree equals Creality tag **`v7.3.0`** (commit `59ae8cbb0437dcc7236e27433ecd4aeb52be0244`, `release-260930`) plus our changes. After an update, record the new base here and in `doc/MCP/upstream-update-plan.md` ("Base attuale"): it cannot be derived from git.
- Do not use `git describe --match "v*"` in MentorSlicer: `v1.0.1` and later `v*` tags are MentorSlicer releases, not Creality tags.

## 0. Preconditions (in MentorSlicer)

```powershell
cd C:\Lavori\CPP\MentorSlicer
git status --short                                   # must be empty
git switch main && git pull --ff-only origin main
git rev-list --max-parents=0 HEAD                    # must print only 65e6380a120518dd540f448eada48566894ff6a8
$base = git rev-parse main                           # keep it: step 5 checks that main did not move
```

**STOP** if the tree is dirty, `main` cannot fast-forward, or the root commit differs.

## 1. Scratch clone of Creality (outside MentorSlicer)

Use `C:\Lavori\CPP\CrealityPrint-upstream` (create it the first time; never inside the MentorSlicer folder). Do not use the backup `C:\Lavori\backup\CPP\CrealityPrint`: it is read-only history.

```powershell
$up = 'C:\Lavori\CPP\CrealityPrint-upstream'
git clone -c core.autocrlf=true https://github.com/CrealityOfficial/CrealityPrint.git $up   # first time only
git -C $up fetch origin --tags
git -C $up rev-parse "v7.3.0^{commit}"                                   # must be 59ae8cbb...
git -C $up tag --list "v*" --sort=-v:refname | Select-Object -First 5
git -C $up config rerere.enabled true
git -C $up config merge.renameLimit 20000                                # the fork deleted ~8,600 files of v7.3.0
git -C $up config diff.renameLimit 20000
```

Set `<old>` = the recorded base and `<tag>` = the new release. Check `git -C $up merge-base --is-ancestor <old> <tag>` (exit code 0); if not, **STOP**: the merge base would not be `<old>`. **STOP**: confirm `<old>` → `<tag>` with the user.

Read what changed in our touch points:

```powershell
$ms = 'C:\Lavori\CPP\MentorSlicer'
$paths = @(git -C $ms grep -l "PRINT-ADVISOR" -- src resources deps cmake) +
         @(Select-String -Path "$ms\doc\MCP\upstream-touch-points.md" -Pattern '`((?:src|resources|deps|cmake)/[^`]+)`' -AllMatches |
           ForEach-Object { $_.Matches } | ForEach-Object { $_.Groups[1].Value }) | Sort-Object -Unique
git -C $up diff --stat <old> <tag> -- $paths
git -C $up diff --stat <old> <tag> -- deps/ version.inc
```

Summarize the relevant changes (and the upstream release notes, if any) for the user, including new Creality network/cloud/account/updater code that phases F1–F9 would have removed.

## 2. 3-way merge in the scratch clone

Bring the MentorSlicer tree in as one commit on top of `<old>`, then merge `<tag>`; git then uses `<old>` as the merge base. (MentorSlicer objects go into the scratch clone; that direction is harmless.)

```powershell
cd $up
git fetch --no-tags $ms main:mentorslicer-main
git switch -c ms-on-<old> <old>
git read-tree -u --reset mentorslicer-main
git commit -m "MentorSlicer tree on <old>"
git diff --quiet mentorslicer-main HEAD; if ($LASTEXITCODE) { 'STOP: tree differs' }   # must be identical
git tag ms-tree-<tag>                                              # local to the scratch clone
git switch -c update/<tag>
git merge --no-ff <tag>
```

To retry from scratch: `git merge --abort`, `git switch --detach <old>`, `git branch -D ms-on-<old> update/<tag>`, `git tag -d ms-tree-<tag>`.

## 3. Conflicts (in the scratch clone)

In this merge **ours** = MentorSlicer, **theirs** = Creality `<tag>`. List the conflicts with `git -c core.quotePath=false status --short` and look each file up in `doc/MCP/upstream-touch-points.md`:

- Creality API changed: adapt **our** hook (`PRINT-ADVISOR BEGIN/END` blocks), never their logic.
- Compiled web bundles under `resources/web/**`: take Creality's new file (`git checkout <tag> -- <file>`); our patches come back in step 4. Bundle names change at every release: for `UD`/`AU`/`UA` entries on a bundle, `git rm` the old name and take the tag's new file.
- Modify/delete conflicts (`DU`, deleted by us) on files we removed (F1–F9, other vendors' profiles, Bambu code; about 8,600 files compared with v7.3.0): keep the deletion. Handle them in bulk (paths can contain spaces, `@`, brackets):

  ```powershell
  $du = (git status --porcelain -z) -split "`0" | Where-Object { $_ -like 'DU *' } | ForEach-Object { $_.Substring(3) }
  $list = Join-Path $env:TEMP 'mentorslicer-du.txt'
  $du | Set-Content -Encoding utf8NoBOM $list
  git --literal-pathspecs rm -q --pathspec-from-file=$list
  $du | ForEach-Object { ($_ -split '/')[0..2] -join '/' } | Group-Object | Sort-Object Count -Descending | Select-Object -First 15 Count, Name
  ```

  Show the user the count and the top folders. **STOP** if one of those Creality changes looks like a security fix the user might want.
- Engine options (`PrintConfig.*`, `GCode.cpp`, `Tab.cpp`): keep both; if Creality/OrcaSlicer added an option with the same name, **STOP** and propose renaming ours with the `advisor_` prefix.
- F8 naming (`version.inc`, icons `MentorSlicer*`, the strings kept on purpose): keep ours.
- Keep CRLF line endings.

**STOP** on every conflict that is not mechanical: show both sides and the proposed resolution. When none remain (`git diff --name-only --diff-filter=U` is empty): `git add` and `git commit --no-edit`.

Then look for Creality files **added** since `<old>` that bring back removed features (no conflict is raised for them), and review them with the user:

```powershell
git -c core.quotePath=false diff --name-only --diff-filter=A ms-tree-<tag> HEAD
```

## 4. Checks in the scratch clone (before anything goes to MentorSlicer)

Run these in `$up` on `update/<tag>`: the scripts are ours (they came with the MentorSlicer tree) and work on that clone.

Web patches:

```powershell
node tools/web-patches/apply-web-patches.mjs --check
node tools/web-patches/apply-web-patches.mjs --apply
```

Run `node --check` on every bundle that `--apply` reports as written. If an anchor is not found, update its expression in `tools/web-patches/patches.mjs` and `doc/MCP/web-bundle-patches.md`; **STOP** if the page was rewritten and the patch needs rethinking. Patches 14, 22–25 remove Creality's tracking, Firebase SDK (with API keys) and OSS SDK: they must all apply. `apply-web-patches` reports a patch found in two files when an old bundle was left behind: remove the old one.

Removed names (phases F1–F9); every removed name must still be gone:

```powershell
$fail = @()
foreach ($p in 'F1','F2','F3','F4','F6','F7','F8','F9') { node tools/verify-phase.mjs --phase $p --skip build,gcode,web,sidecar,smoke --no-manual; if ($LASTEXITCODE) { $fail += $p } }
foreach ($s in 'F5B','F5C','F5D') { Copy-Item tools/verify/removed-$s.txt tools/verify/removed-F5.txt; node tools/verify-phase.mjs --phase F5 --skip build,gcode,web,sidecar,smoke --no-manual; if ($LASTEXITCODE) { $fail += $s }; Remove-Item tools/verify/removed-F5.txt }
"failed: $fail"
```

Phase F5 has three lists and the script reads only `removed-F5.txt`, hence the temporary copy. Each phase takes about a minute. All lists pass on MentorSlicer `main` as of 2026-10-25, so any FAIL comes from the update. **STOP** if `failed:` is not empty.

Secrets: define the check once and run it here and again in step 5. It prints only file names, never the values. **STOP** if it reports anything.

```powershell
function Test-NoSecrets {
  $found = @()
  $found += git grep -lIE 'AIza[0-9A-Za-z_-]{35}'                       # Google/Firebase API keys
  $found += git grep -lIE 'sk-[0-9a-f]{32}'                             # DashScope/OpenAI-style keys
  $found += git grep -lIE 'LTAI[0-9A-Za-z]{12,}'                        # Alibaba Cloud access keys
  $found += git grep -lIE 'BEGIN [A-Z ]*PRIVATE KEY'                    # private keys
  $found += git grep -lIE '[a-z]{4}-[a-z]{4}-[a-z]{4}-[a-z]{4}\\?"' -- package '*.cmake' '*.sh' '*.yml' '*.py' .github   # Apple app-specific password
  if ($found) { "STOP: possible secrets in: $($found | Sort-Object -Unique)" } else { 'no secrets found' }
}
Test-NoSecrets
```

(All five patterns match the real secrets of Creality v7.3.0 or are cheap insurance, and none matches MentorSlicer `main` today. The Apple pattern needs the pathspec: without it two web bundles match.)

Commit the fixes in the scratch clone (`git add -A && git commit -m "Web patches and checks"`); `git status --short` must be empty.

## 5. Bring the result back (MentorSlicer)

Export the final tree as a commit **without parents**, so only the objects of that tree cross over and no Creality commit does:

```powershell
cd $up
$export = git commit-tree "HEAD^{tree}" -m "MentorSlicer update to <tag> (export)"
git branch -f ms-export $export
git rev-list --count ms-export                                   # must be 1

cd $ms
if ((git rev-parse main) -ne $base -or (git -C $up rev-parse mentorslicer-main) -ne $base) { 'STOP: main moved since step 2: redo step 2' }
git switch -c update/<tag> main
git fetch --no-tags $up ms-export                                # into FETCH_HEAD only, no local ref
git rev-list --count FETCH_HEAD                                  # must be 1
git read-tree -u -m HEAD FETCH_HEAD                              # index and worktree become the exported tree
git status --short | Measure-Object                              # review the size of the change
Test-NoSecrets                                                   # must print "no secrets found"
node tools/web-patches/apply-web-patches.mjs --check             # every patch "applied"
git commit -m "Update to Creality Print <tag>"
git rev-list --max-parents=0 HEAD                                # still only 65e6380...
```

If `main` moved, the export would silently revert those commits: never skip that check. Import the tree as **one** commit: every commit on `update/<tag>` ends up in the public history after step 9, so none may contain an unpatched bundle. Later fixes (compile errors, tests) are separate commits of our own code on the same branch.

## 6. Build

If `deps/` changed in step 1, **STOP** (the deps build takes hours), then:

```powershell
cmd.exe /d /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && call "%CD%\build_release_vs2022.bat" deps'
```

The normal build below is needed in either case. **STOP** first and ask whether MentorSlicer is open from `build\src\Release` with an unsaved project: the close script waits 20 s and then terminates it, losing that project. Then close the build instance: `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tools\close-build-instance.ps1`. **STOP** if it refuses (preheat or slow cooldown running, unreadable state, instance started as administrator). Never pass `-Force`. Then, from the repository root:

```powershell
cmd.exe /d /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 && call "%CD%\build_exe_only.bat"'
```

(Alternatively ask the user to press **Build EXE** in the VS Code status bar, which closes the instance first.) Fix compile errors in our code only. Then ask the user to start `build\src\Release\MentorSlicer.exe` and enable the service from **Integrazione AI → Print Advisor MCP…** (button **Abilita**).

## 7. Automated tests (all must pass before going on)

```powershell
cd tools/print-advisor-mcp
npm ci && npm run typecheck && npm run smoke && node scripts/smoke.mjs --exe ../../build/src/Release/mentorslicer-mcp.exe
cd ../..
```

`npm run smoke` calls only read-only tools; a tool list change prints `+`/`-` lines (update `scripts/tools-expected.json` with `node scripts/smoke.mjs --update-expected` only if intended). Then ask the user to slice a plate with heat soak and `brim_layers` 3 and run:

```powershell
node tests/print-advisor/check-gcode.mjs
node tests/print-advisor/check-gcode.mjs "<newest %LOCALAPPDATA%\Temp\mentorslicer_model\<date>\<time>\Metadata\.<pid>.<plate>.gcode>"
```

The G-code file name starts with a dot. Regenerating the fixture is described in `tests/print-advisor/README.md`; do it only when the engine change is intended.

## 8. Printer checks

**STOP**: go through `doc/MCP/printer-test-checklist.md` with the user, one step at a time. Every command needs the user's confirmation in the MentorSlicer "MCP Advisor" dialog; never start a print. Record differences in `CLAUDE.md`.

## 9. Close

Update the recorded base (this file and `doc/MCP/upstream-update-plan.md`) and `CLAUDE.md` with Creality changes that concern us; commit. Final guard, then **STOP** and ask before pushing:

```powershell
git rev-list --max-parents=0 update/<tag>            # only 65e6380...
git switch main && git merge --ff-only update/<tag>
git push origin main
```

Do not push `update/<tag>` or any tag unless the user asks; push a tag only by explicit name, never with `--tags`, and only after the root check above. A release is made separately (see `CLAUDE.md`, "Release su GitHub").
