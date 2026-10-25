---
name: print-defect-diagnostics
description: Design or implement explainable 3D-print defect questions and parameter recommendations for Print Advisor, grounded in active filament, printer, and process settings.
---

Use this skill when adding defect taxonomy, diagnosis rules, recommendation schemas, or evaluation cases for Print Advisor.

## Diagnostic method

1. Confirm printer, nozzle, material/filament profile, process profile, and only the settings relevant to the symptom. Preserve units and source identity.
2. Capture the observed defect, where it appears, when it occurs, and what changed recently. Ask only questions that distinguish plausible causes.
3. Rank candidate causes and separate directly observed facts from assumptions. If critical context is missing, return questions instead of fabricated values.
4. Recommend one or a small number of controlled changes. Include current value, candidate value or cautious range, rationale, confidence, expected effect, possible tradeoff, and a verification step.
5. Prefer a small calibration/test print and change one variable at a time where practical. Do not claim the recommendation guarantees a fix.

## Initial defect scope

Start with stringing, warping, poor bed adhesion, under/over-extrusion, blobs/zits, and weak bridging. For each category document competing causes and relevant context (for example moisture, enclosure/ambient conditions, bed surface, cooling, nozzle size, and firmware), rather than mapping a symptom to one parameter mechanically.

Do not encode material-specific numeric limits without a traceable source or repository profile evidence. Treat user-supplied values as input, not verified truth. Keep rules deterministic and testable where possible; expose provenance and uncertainty in the output schema.

Photos are out of MVP. If later added, do not store them by default, avoid logging image payloads, and verify client multimodal support and consent before sending them to an LLM.

No diagnosis tool may mutate the active profile. Mutations belong to a separately approved workflow with validation and rollback.
