---
name: print-advisor-mcp
description: "Build, change or document the MCP sidecar of the MentorSlicer Print Advisor (tools/print-advisor-mcp, server mentorslicer-advisor, packaged as mentorslicer-mcp.exe). Use for MCP tool schemas, guide texts, timeouts and retries, smoke tests, the packaged exe, and Claude Code/Codex registration (claude mcp add, scope user)."
---

## Ground truth

`CLAUDE.md` ("Print Advisor (MCP)", "Architettura", "Richieste concorrenti", "Regole di sicurezza") and `tools/print-advisor-mcp/README.md` are authoritative. `doc/MCP/handoff.md` is the original brief and is outdated.

## Boundaries

- Claude Code or Codex is the MCP host and supplies the LLM. MentorSlicer contains the domain service and application state, not an LLM integration.
- The sidecar owns protocol framing, tool schemas and guide texts (defects, material sheets, calibration formulas). The native Print Advisor owns validation and domain behavior; do not duplicate profile logic in the sidecar.
- Tools are registered in `src/index.ts` and `src/tools/*.ts`; the expected list is `scripts/tools-expected.json` (about 50). HTTP client: `src/appClient.ts`.
- Creality's in-app AI chat (`MCPChatPanel`, CxAgent) was removed in phase F3. `SlicerBridge` (`simple/bridge/`) stays and is what the Print Advisor reuses.

## Tool contract

- Every tool that changes something calls a C++ endpoint that asks for an in-app confirmation (`AdvisorConfirmDialog`, or the dedicated verdict / Save dialog). Client-side tool confirmation is not approval in MentorSlicer. A new change tool is exposed only after its endpoint enforces the confirmation, the re-check after the dialog and the 409 rules. The Print Advisor never starts a print.
- Narrow, typed schemas and bounded output; units, preset identity and structured errors (app not running, stale context, invalid input, unsupported setting, timeout, 409 busy, 503).
- Retries: a 503 (the server did not read the request) is retried automatically up to 3 times; never retry a change that got 409 or timed out.
- Timeouts: pass the right constant of `appClient.ts` at the call site (`mutationTimeoutMs`/`guiReadTimeoutMs` 610 s for changes and GUI-thread reads, `slowReadTimeoutMs` 15 s for photo, log, support volumes, archive and slice start, `readTimeoutMs` 5 s for other reads; default 610 s for a POST, 5 s for a GET). A POST that only reads goes in `readOnlyRoutes` (method + path), so its timeout is reported as safe to retry.
- Never include secrets (session token, descriptor contents) in tool descriptions, results or logs; no arbitrary file paths or shell commands.

## Local transport

Stdio sidecar plus loopback HTTP API on `127.0.0.1` with a session token; the descriptor is `%APPDATA%\MentorSlicer\cache\print-advisor\endpoint.json` (`MENTORSLICER_ADVISOR_DESCRIPTOR` only for a copy started with `--datadir`).

## Setup and packaging

- Registration: `claude mcp add mentorslicer-advisor --scope user --transport stdio -- "<MentorSlicer folder>\mentorslicer-mcp.exe"` (the Print Advisor MCP… window shows the exact command). Old `local` registrations (`node … tsx … src/index.ts`, `creality-print-advisor`) must be removed, otherwise they hide the user one.
- The exe is a Node.js single executable built by `scripts/build-exe.mjs` via `tools\build-mcp-exe.bat` (called by the build scripts); Node is needed only on the build machine.
- Codex setup is documented in the README but not verified on the development PC; keep that caveat. Verify official MCP SDK and client documentation before changing setup claims.

## Verify

`npm ci`, `npm run typecheck`, `npm run smoke` (app running, service enabled), `node scripts/smoke.mjs --exe dist/mentorslicer-mcp.exe`. Update `scripts/tools-expected.json` with `--update-expected` only for an intended tool list change. Code comments in English.
