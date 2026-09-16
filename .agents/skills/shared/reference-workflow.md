# Reference-driven authoring

Use for reconstruction or visual matching, not routine node/layout repairs. Read the supplied images and define observable success criteria: proportions, silhouette, identity details, material intent and required views. Separate visible facts from inferred hidden surfaces or approximate dimensions.

## Use the available evidence

A single image is a valid starting point. Do not automatically request front, side and top, or invent missing orthographic drawings. Ask for additional input only when a consequential requirement cannot be inferred responsibly. For a labelled composite sheet, preserve the original and record any unambiguous view crops and their source rectangles. Ambiguous labels or projections remain uncertainties, not facts.

For orthographic reconstruction, use the [three-view reference](../pcg-graph-authoring-web/triview.md). Match every required view; a good front view cannot compensate for a failed side view. Use a three-quarter/integrity view to check depth and assembly, not as a substitute for a supplied orthographic constraint.

## Durable state without ritual

For multi-pass work, save the supplied reference files and a compact plan beside the artifact: sources, observations, dimensions/frame, key parts, material intent, graph targets, acceptance criteria and unresolved assumptions. Do not overwrite originals or use expiring URLs as the only durable evidence. A brief edit can use a short task record instead of a full generated plan.

The optional [planning helpers](script-reference.md) maintain structured `*-plan.json`, archived references, review history and RESUME files. When using them, honor their actual schema and validation requirements; do not fabricate observations to satisfy fields. Existing structured plans keep their pass IDs and evidence format. Helper-specific strict thresholds are not a requirement to activate that pipeline for every task.

Read saved context after compaction or a handoff, or when the relevant assumptions change. Do not reread every unchanged document and image at every minor step. Update the durable record at meaningful milestones.

## Iterate from evidence

Build the important volumes and identity features first, then refine the failures visible in the current result. Choose the number and size of passes for the task; combine related corrections when their effects can be reviewed together. Map claimed details to real graph or material targets, not prose alone.

After geometry/material changes, cook and inspect fresh target-platform images with known camera settings. Keep comparisons tied to the same reference view and current graph revision. Scripts can validate structure and package images; they do not judge visual similarity. Numerical reviewer scores are subjective unless produced by a defined measurement method.

Continue while concrete corrections are useful within scope. Stop at the agreed deliverable, an actual blocker, or a justified diminishing-return/budget boundary; record residual differences. Do not enforce a universal cycle count, invent a quality score, silently lower an agreed threshold, or claim a white model is a finished textured asset.
