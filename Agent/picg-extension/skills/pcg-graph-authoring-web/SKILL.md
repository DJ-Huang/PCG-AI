---
name: pcg-graph-authoring-web
description: >-
  Create complete production-ready web assets through PCG-AI: plan and author
  `.pcg` graphs, validate and cook the white model via the pcg-server HTTP API,
  validate UV/projection, create and bind materials and textures, output a
  reproducible exported asset (glTF / web scene), render it, and apply
  full-asset acceptance. Use for PCG graph authoring for web, reference-image
  reconstruction, procedural web asset production, bridge/building/vehicle/
  scatter generators, Subgraphs, graph parameters, PCG cooking via pcg-server,
  materialized web asset delivery, or `/pcg-graph-authoring-web`.
---

# PCG Web complete-asset production

Create a finished web asset, not merely a `.pcg` graph or a white model. The asset is complete only at `FINAL_ACCEPTED` under the shared production specification.

## Single source of truth

Resolve these paths relative to this skill directory before work:

```text
SHARED_DIR = ../shared/pcg-web-complete-asset
SHARED_SCRIPTS_DIR = ../shared/pcg-scripts
AUTHORING_SKILL_DIR = .
```

`SHARED_DIR` is authoritative for the complete production workflow, AssetSpec, geometry validation, materials, textures, web integration, final score, and recovery rules. `SHARED_SCRIPTS_DIR` holds the generic Python helper scripts shared across all target variants (Unity, web, …). `AUTHORING_SKILL_DIR/scripts/web/` holds web-specific review scripts and templates. Do not copy, redefine, or weaken those rules here. If either shared directory is missing, stop and report the missing installation rather than silently falling back to a divergent workflow.

## Mandatory read order

1. Read `SHARED_DIR/workflow.md`, `asset-contract.md`, and `pcg-graph-authoring.md` before planning a new graph or a material rebuild.
2. For a reference-image job, read `llm-orchestration.md`, `scripts.md`, and `web-review.md` before the first graph write or review cook.
3. Read each shared stage reference immediately before that stage: `geometry-validation.md`, `material-workflow.md`, `texture-workflow.md`, `web-integration.md`, and `final-acceptance.md`.
4. On any failure, read `SHARED_DIR/error-codes.md` and repair the earliest failed stage.

## Reference persistence and re-hydration (P0, reference-image jobs)

The reference image is not durable memory: context compaction drops pasted images and URLs rot, while material/texture/final stages run long after first observation.

- **Archive first:** run `SHARED_SCRIPTS_DIR/archive_reference.py --image <path|URL|data-URI> --plan <plan.json>` immediately after `new_authoring_plan.py`. The plan points at the archived `ref_<slug>` file; never feed later stages a URL or chat attachment.
- **Observation on disk:** `observation.layers` and `visualTokens` (hex/roughness/ratios) live in `*-plan.json`, enforced by `validate_plan.py --strict-quality`.
- **Re-hydrate at every stage entry** (geometry, UV, material, texture, asset export, final acceptance) and after any compaction: read `<plan-stem>-RESUME.md` → `*-plan.json` (observation + visualTokens + reviewHistory) → archived reference → latest `cmp_*.png`. Refresh the RESUME file via `SHARED_SCRIPTS_DIR/report_pass.py <plan> --resume` at each stage transition and review cycle.

## Execute the complete workflow

Follow the shared stage contract exactly:

```text
AssetSpec
→ PCG plan and graph authoring
→ root/Subgraph layout pass
→ white-model cook and geometry validation
→ UV/projection validation
→ material plan
→ texture production/import
→ web material creation and binding
→ exported asset output (glTF / web scene)
→ final fully textured render and acceptance
```

The graph-authoring reference preserves the mandatory manifest/rule/Golden-Graph retrieval, top-to-bottom graph layout, independent root/Subgraph layout pass, node naming, physical sizing, module/Subgraph rules, per-part bevel rule, parameters, clean-scene cook, and visual refinement loop. Use the shared Python helper scripts from `SHARED_SCRIPTS_DIR` and web review templates from `AUTHORING_SKILL_DIR/scripts/web/`.

## Autonomous production defaults

Apply sensible graph parameters, save directories, material values, texture resolution, and fixed `PcgReview_<slug>` review page paths without pausing. Record the receipt and assumptions. Continue to the next stage when its definition of done passes; on a reference image, keep refining the fully textured asset until the shared final target is met or the shared hard ceiling is reached.

Ask only for a genuinely unusable reference, unreachable pcg-server, unavailable required source asset, or an explicit request for interactive control. Unknown back faces, material inference, bevel taste, Subgraph partitioning, and parameter candidates are normal production decisions—make and record the best-supported choice.

## Completion and handoff

Never report success based on a graph canvas, white-model image, placeholder texture, or default material. Deliver the AssetSpec, valid graph, geometry/UV/material/export receipts, and the final fully textured render score defined by `final-acceptance.md`.

This standard variant has no PCG pipeline-validation stage. Use `pcg-graph-authoring-web-dev` when the user also needs the generator itself stress-tested for structure, exposed parameters, seed determinism, boundary inputs, regeneration, and output consistency.
