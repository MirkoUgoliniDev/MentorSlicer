---
name: print-advisor-implementation
description: Plan or implement the Creality Print Print Advisor MCP feature. Use for tasks involving its native module, MCP sidecar, defect recommendations, approval flow, or handoff in doc/MCP/handoff.md.
---

Use `doc/MCP/handoff.md` as the feature brief, then verify all code assumptions against the current repository. Do not treat the handoff as proof that a named API or lifecycle hook exists.

## Work sequence

1. Read the handoff and repository instructions. Inspect the current git diff and preserve unrelated user changes.
2. Verify existing bridge/config APIs, GUI thread dispatch, lifecycle, dependencies, and build integration before choosing seams.
3. Keep domain logic in `src/slic3r/GUI/simple/printAdvisor/` where possible. Separate context acquisition, defect catalog, recommendations, patch validation, transport, and UI approval.
4. Implement the smallest end-to-end milestone. Prefer read-only context and recommendations before adding mutations.
5. Keep MCP transport outside the domain logic. Confirm current official MCP SDK and Claude Code/Codex setup docs when changing protocol or setup claims.
6. Review the diff for scope, thread safety, lifecycle, data minimization, and regressions. Run checks only when requested or required by repository instructions.

## Product invariants

- MVP is read-only. It must not change profiles, send files, start a print, or control devices.
- The LLM is hosted by Claude Code or Codex; do not embed a model/provider in Creality Print.
- Recommendations must use the active printer/material/process context, include units, state assumptions and confidence, and suggest a way to verify.
- Ask diagnostic questions when material condition, printer setup, or symptom details materially affect the recommendation. Avoid universal parameter values.
- The full feature includes targeted writing to a specific filament or process preset, not only recommendations. Create a user copy and preserve the source; map every setting to its canonical owning preset; validate allowlist/ranges; bind preview to an unchanged context; require explicit in-app approval; save and verify the copy; keep selecting the new preset as a separate action; support conflict-aware rollback.
- Do not pass credentials, cloud account data, arbitrary paths, or unnecessary project data to the model.
- Keep changes narrowly scoped. Any change outside the module, build integration, or essential adapter/lifecycle seam needs a demonstrated reason.

## Completion report

Summarize what changed, why, checks run, and unresolved risks. Link files with absolute paths when reporting in Codex.

