---
name: print-advisor-implementation
description: "Plan or implement a change to the native (C++) side of the MentorSlicer Print Advisor: local API routes, confirmation dialogs, profile copies, printer commands, preheat/cooldown monitor, telemetry, verdict archive, engine options. Use for Print Advisor features and fixes in src/slic3r/GUI/simple/printAdvisor or print_manage/App; for the TypeScript MCP sidecar use print-advisor-mcp."
---

## Ground truth

`CLAUDE.md` is authoritative for the current state and the safety rules ("Print Advisor (MCP)", "Regole di sicurezza (non rilassarle)", "Richieste concorrenti", "Insidie trovate", "Build: cose da sapere"). `doc/MCP/handoff.md` is the original brief and is outdated (it still describes a read-only MVP); where they disagree, `CLAUDE.md` wins. Verify every code assumption against the repository.

The feature is complete and in use (about 50 MCP tools, profile copies, printer commands, monitors). Extend it; do not "restore" a read-only state or remove existing tools unless asked.

## Work sequence

1. Read the relevant `CLAUDE.md` sections and the code involved. Inspect the current git diff and preserve unrelated user changes.
2. Verify existing APIs, GUI-thread dispatch (`CallAfter`), lifecycle and build integration before choosing seams (the `print-advisor-architect` agent can do this read-only).
3. Place code by responsibility: validation and domain behavior in C++ (`src/slic3r/GUI/simple/printAdvisor/`, `print_manage/App/DeviceTelemetryCache.*`, `PrintAdvisorMonitor.*`, `simple/bridge/`); guide texts (defects, material sheets, calibration formulas) and tool schemas in the sidecar `tools/print-advisor-mcp/src/`. New C++ sources go in `PrintAdvisor.cmake`.
4. Hooks in Creality files: 1–3 lines with `PRINT-ADVISOR` markers, listed in `doc/MCP/upstream-touch-points.md`.
5. When the user chooses among several features, collect all answers first, then implement them together with **one build**.
6. Build, then verify with the MCP smoke test (`npm run smoke` in `tools/print-advisor-mcp`, app running, service enabled) before saying it works. Have the diff reviewed (`print-advisor-reviewer` agent).

## Product invariants (summary; the full list is in CLAUDE.md and must not be relaxed)

- The Print Advisor never starts a print. The LLM lives in the MCP client, never inside MentorSlicer.
- Every change needs an in-app confirmation (`AdvisorConfirmDialog`, or the dedicated verdict window / Save dialog of `export_profile`) and a re-check of the state and of the server stop flag after it. The only exceptions are the three in `CLAUDE.md` (exhaust guard, slow cooldown, preheat auto-off); never add another.
- Profiles: always a user copy inheriting like "Salva come", only the differences stored, never auto-selected, never over a system profile; writable keys per `advisor_key_writable`; limits per `check_advisor_safety`, `machine_limits` and the filament nozzle range; calibration limits; profiles with unsaved changes rejected; rename/delete only for Advisor copies.
- Recommendations use the active printer/material/process context, include units, assumptions, confidence and a verification step; ask diagnostic questions instead of universal values.
- Never send the session token, descriptor contents, arbitrary paths or unnecessary project data to the client. Files are written inside `<data_dir>`, or at a path the user picks in a native Save dialog; never at a path supplied by the client.

## Conventions

CRLF sources (keep line endings); code comments in English; replies to the user in Italian, short. Pitfalls: numbers with `std::locale::classic()`, `CNumericLocalesSetter`, `delete_preset_keep_selection`.

## Completion report

Summarize what changed, why, checks run (build, smoke, G-code tests if the engine changed), and what still needs a test with the printer.
