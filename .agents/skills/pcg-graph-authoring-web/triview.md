# Orthographic three-view reconstruction

Read for supplied front/side/top constraints or an explicit orthographic reconstruction task. An ordinary single-photo task uses [reference-driven authoring](../shared/reference-workflow.md) without mandatory additional uploads.

## Sources and frame

Use labelled front, side and top files when available. Preserve a composite original and record any clearly identifiable view crops and their rectangles. Do not invent missing drawings or treat perspective views as orthographic measurements. Resolve consequential ambiguity once; a partial set remains a partial set.

Record the object frame: PCG's left-handed space with `+y` up, chosen `frontAxis` (commonly `+z`), and `sideView` (`right` or `left`). Keep camera presets and transforms consistent with that frame.

Width is constrained by front/top, height by front/side and depth by side/top. Give each dimension an owner and explicit scale/tolerance. Record inconsistent dimensions and the evidence for a chosen interpretation; do not silently average conflicting drawings. Select geometry from the required volume and cross-section, not just a convenient silhouette.

## Optional structured-plan tooling

[Planning helpers](../shared/script-reference.md) support a single image with `--image` or a complete triplet with `--front`, `--side` and `--top` supplied together. Use `archive_reference.py --from-plan --require-triview` only for a real complete set. For incomplete or differently labelled sets, keep an explicit reference record instead of fabricating inputs to satisfy the triplet helper.

When using the structured triplet schema, fill `referenceSet`, `coordinateFrame`, per-view observations/landmarks, `crossViewConstraints`, and component hypotheses as required by the validator. Keep existing evidence fields and camera receipts compatible with those helpers.

## Review

Use [Web review](web-review.md) to capture each required view after a dimension/shape change. Associate each render with the matching archived source and known camera settings. Recheck shared width/height/depth constraints across affected views and inspect an integrity three-quarter view for collapsed depth, intersections and missing thickness.

Every required view must meet the agreed criteria. Do not average away a failing view or use the integrity view in its place. If an existing plan uses numerical pass thresholds, its worst required-view result controls that gate; label those numbers as reviewer judgments, not automatic pixel measurements.
