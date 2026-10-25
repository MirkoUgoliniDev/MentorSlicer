---
name: upstream-update
description: Update this Creality Print fork (branch feature/mcp-advisor, Print Advisor MCP) to a new upstream Creality Print release. Use when the user says "aggiorna Creality Print alla nuova versione", "aggiorna a Creality 7.x", "upstream update", "merge the new Creality tag", or asks to bring the Print Advisor onto a newer Creality version.
---

Follow Phase 2 of `doc/MCP/upstream-update-plan.md`. Work step by step; at every **STOP** ask the user (in Italian, one question at a time) and wait. Never commit with unresolved conflicts, never force-push, never rebase `feature/mcp-advisor`. Agent shell notes from `CLAUDE.md` apply: run every command from the repository root (the `.bat` files are called as `%CD%\<name>.bat`, because the agent shell does not search the current folder), `cmd.exe /d /c` for `.bat`, CRLF sources.

## 0. Preconditions

```bash
git status --short
git branch --show-current      # feature/mcp-advisor
git config rerere.enabled      # true; else: git config rerere.enabled true
```

**STOP** if the tree has tracked changes or the branch is not `feature/mcp-advisor`: ask the user what to do with them.

## 1. Fetch the new release

```bash
git fetch upstream --tags
git tag --list "v*" --sort=-v:refname | head -5
git describe --tags --abbrev=0 --match "v*" feature/mcp-advisor   # current base <old>
```

Set `<tag>` (for example `v7.4.0`). **STOP**: confirm `<old>` → `<tag>` with the user. Then read what changed in our touch points:

```bash
git diff --stat <old> <tag> -- $(grep -oE '`(src|resources|deps)/[^`]+`' doc/MCP/upstream-touch-points.md | tr -d '`' | sort -u)
git diff --stat <old> <tag> -- deps/
```

Summarize the relevant changes (and the upstream changelog/release notes if available) for the user.

## 2. Work branch and merge

```bash
git switch -c update/<tag> <tag>
git merge --no-ff feature/mcp-advisor
```

## 3. Conflicts

For each file in `git diff --name-only --diff-filter=U`, look it up in `doc/MCP/upstream-touch-points.md`:

- Creality API changed: adapt **our** hook (the `PRINT-ADVISOR BEGIN/END` blocks), never their logic.
- Compiled web bundles under `resources/web/**`: take **theirs** (`git checkout --theirs -- <file>`), our changes come back in step 4.
- Engine options (`PrintConfig.*`, `GCode.cpp`, `Tab.cpp`): keep both; if Creality/OrcaSlicer added an option with the same name, **STOP** and propose renaming ours with the `advisor_` prefix.
- Keep CRLF line endings.

**STOP** on every conflict that is not mechanical: show both sides and the proposed resolution. When none remain: `git add` the files and `git commit --no-edit`.

## 4. Web patches

```bash
node tools/web-patches/apply-web-patches.mjs --check
node tools/web-patches/apply-web-patches.mjs --apply
```

If an anchor is not found, update its expression in `tools/web-patches/` and `doc/MCP/web-bundle-patches.md`; **STOP** if the page was rewritten and the patch needs rethinking. Run `node --check` on every patched bundle, then commit.

## 5. Build

If `deps/` changed in step 1, **STOP** (the deps build takes hours), then:

```bash
cmd.exe /d /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && call %CD%\build_release_vs2022.bat deps"
```

Otherwise (or after it), close the build instance of Creality Print (`LNK1104` otherwise; **STOP** if the user has an unsaved project open there) and:

```bash
cmd.exe /d /c "call \"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat\" -arch=x64 && call %CD%\build_exe_only.bat"
```

Fix compile errors in our code only. Then ask the user to start `build\src\Release\MentorSlicer.exe` and enable the service from **Integrazione AI → Print Advisor MCP** (button **Abilita**).

## 6. Automated tests (all must pass before going on)

```bash
cd tools/print-advisor-mcp && npm install && npm run typecheck && npm run smoke
```

`npm run smoke` calls only read-only tools; a tool list change prints `+`/`-` lines (update with `node scripts/smoke.mjs --update-expected` only if intended). Then ask the user to slice a plate with heat soak and `brim_layers` 3 and run:

```bash
node tests/print-advisor/check-gcode.mjs
node tests/print-advisor/check-gcode.mjs "<newest %LOCALAPPDATA%\Temp\mentorslicer_model\...\Metadata\*.gcode>"
```

Regenerating the fixture is described in `tests/print-advisor/README.md`; do it only when the engine change is intended.

## 7. Printer checks

**STOP**: go through `doc/MCP/printer-test-checklist.md` with the user, one step at a time. Every command needs the user's confirmation in the Creality Print dialog; never start a print. Record differences in `CLAUDE.md`.

## 8. Close

```bash
git switch feature/mcp-advisor
git merge --ff-only update/<tag>
git tag advisor-<tag>-1
```

Update `CLAUDE.md` with Creality changes that concern us. **STOP**: ask before pushing, then:

```bash
git push origin feature/mcp-advisor advisor-<tag>-1
```
