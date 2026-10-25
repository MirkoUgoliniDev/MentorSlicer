---
name: print-defect-diagnostics
description: "Design or extend explainable 3D-print defect diagnosis and parameter recommendations for the MentorSlicer Print Advisor (defect catalog, diagnostic questions, recommendation schema, evaluation cases). Use when working on defects such as stringing, warping, poor bed adhesion, under/over-extrusion, blobs, weak bridging (in Italian: filo, ragnatele, deformazione, distacco, adesione, sottoestrusione) or on diagnose_defect / recommend_parameter_changes."
---

Use this skill when adding or changing defect taxonomy, diagnosis rules, recommendation schemas, or evaluation cases. `CLAUDE.md` is authoritative for the Print Advisor rules.

## Where it lives

The defect catalog and guide texts are in the sidecar: `defectSchema` and `defectGuide` in `tools/print-advisor-mcp/src/index.ts`, with material sheets and calibration formulas nearby. Extend them there; validation and profile changes stay in C++. Existing categories include stringing, warping, poor bed adhesion, under/over-extrusion, blobs/zits and weak bridging.

## Diagnostic method

1. Confirm printer, nozzle, material/filament profile, process profile, and only the settings relevant to the symptom (`get_print_context`, `get_profile_values`). Preserve units and source identity.
2. Capture the observed defect, where it appears, when it occurs, and what changed recently. Ask only questions that distinguish plausible causes.
3. Rank candidate causes and separate observed facts from assumptions. If critical context is missing, return questions instead of fabricated values.
4. Recommend one or a few controlled changes: current value, candidate value or cautious range, rationale, confidence, expected effect, tradeoff, verification step.
5. Prefer a small calibration/test print and one variable at a time. Never claim the recommendation guarantees a fix.

For each category document competing causes and context (moisture, enclosure/chamber temperature, bed surface and glue, cooling, nozzle size, firmware) instead of mapping a symptom to one parameter.

## Evidence

Encode material-specific numbers only with a traceable source: repository profiles, or results verified on the user's printer and recorded in `CLAUDE.md` (for example ASA/ABS on the K1C: drying 80 °C for 4–6 h, retraction tests ≤ 10 mm, glue stick on smooth PEI for large parts). Treat user-supplied values as input, not verified truth. Keep rules deterministic and testable; expose provenance and uncertainty in the output.

## Photos

Photos come only from `camera_snapshot` (read-only, not stored) and from the verdict archive the user approves at the end of a print. Never log image payloads.

## Boundaries

`diagnose_defect` and `recommend_parameter_changes` never change a profile. Changes go through `preview_profile_changes` → `apply_profile_changes`, which need the in-app confirmation and follow the safety rules in `CLAUDE.md`.
