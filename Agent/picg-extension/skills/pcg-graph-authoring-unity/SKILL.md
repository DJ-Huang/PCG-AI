---
name: pcg-graph-authoring-unity
description: >-
  Create complete production-ready Unity assets through PICG: plan and author
  `.pcg` graphs, validate and cook the white model in a clean Unity/Tuanjie
  scene, validate UV/projection, create and bind materials and textures, output
  a reproducible prefab, render it, and apply full-asset acceptance. Use for
  PCG graph authoring, reference-image reconstruction, procedural Unity asset
  production, bridge/building/vehicle/scatter generators, Subgraphs, graph
  parameters, PCG MCP graph creation/editing, PCG cooking, materialized prefab delivery, or
  `/pcg-graph-authoring-unity`.
---

# PCG Unity complete-asset production

Create a finished Unity asset, not merely a `.pcg` graph or a white model. The asset is complete only at `FINAL_ACCEPTED` under the shared production specification.

## Single source of truth

Resolve these paths relative to this skill directory before work:

```text
SHARED_DIR = ../shared/pcg-unity-complete-asset
SHARED_SCRIPTS_DIR = ../shared/pcg-scripts
PCG_MCP_CONTRACT = ../shared/pcg-mcp.md
AUTHORING_SKILL_DIR = .
```

`SHARED_DIR` is authoritative for the complete production workflow, AssetSpec, geometry validation, materials, textures, Unity integration, final score, and recovery rules. `SHARED_SCRIPTS_DIR` holds the generic Python helper scripts shared across all target variants (Unity, web, …). `PCG_MCP_CONTRACT` is authoritative for live Web-editor schema discovery, complete graph creation/editing/saving, native validation/cook, Preview capture, conflict handling, and fallbacks. `AUTHORING_SKILL_DIR/scripts/unity/` holds Unity-specific C# review templates. Do not copy, redefine, or weaken those rules here. If a required shared path is missing, stop and report the missing installation rather than silently falling back to a divergent workflow.

## Mandatory read order

1. Read `PCG_MCP_CONTRACT`, then `SHARED_DIR/workflow.md`, `asset-contract.md`, and `pcg-graph-authoring.md` before planning a new graph or a material rebuild.
2. For a reference-image job, read `llm-orchestration.md`, `scripts.md`, and `unity-review.md` before the first graph write or review cook.
3. Read each shared stage reference immediately before that stage: `geometry-validation.md`, `material-workflow.md`, `texture-workflow.md`, `unity-integration.md`, and `final-acceptance.md`.
4. On any failure, read `SHARED_DIR/error-codes.md` and repair the earliest failed stage.

## Reference persistence and re-hydration (P0, reference-image jobs)

The reference image is not durable memory: context compaction drops pasted images and URLs rot, while material/texture/final stages run long after first observation.

- **Archive first:** run `SHARED_SCRIPTS_DIR/archive_reference.py --image <path|URL|data-URI> --plan <plan.json>` immediately after `new_authoring_plan.py`. The plan points at the archived `ref_<slug>` file; never feed later stages a URL or chat attachment.
- **Observation on disk:** `observation.layers` and `visualTokens` (hex/roughness/ratios) live in `*-plan.json`, enforced by `validate_plan.py --strict-quality`.
- **Re-hydrate at every stage entry** (geometry, UV, material, texture, prefab, final acceptance) and after any compaction: read `<plan-stem>-RESUME.md` → `*-plan.json` (observation + visualTokens + reviewHistory) → archived reference → latest `cmp_*.png`. Refresh the RESUME file via `SHARED_SCRIPTS_DIR/report_pass.py <plan> --resume` at each stage transition and review cycle.

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
→ Unity material creation and binding
→ prefab output
→ final fully textured render and acceptance
```

The graph-authoring reference preserves the mandatory manifest/rule/Golden-Graph retrieval, top-to-bottom graph layout, independent root/Subgraph layout pass, node naming, physical sizing, module/Subgraph rules, per-part bevel rule, parameters, clean-scene cook, and visual refinement loop. Use the shared Python helper scripts from `SHARED_SCRIPTS_DIR` and Unity review templates from `AUTHORING_SKILL_DIR/scripts/unity/`.

Use PCG MCP as the primary graph-authoring path when the target is open in the Web editor: discover the live manifest, read or replace the full document, apply atomic structural passes, validate/cook/capture, and save the `.pcg` through the editor with `ifGraphHash`. It is the graph creation surface, not the Unity acceptance surface: keep saved-file validation and the mandatory Tuanjie/Unity clean-scene cook, screenshot, prefab, and final render gates.

## Autonomous production defaults

Apply sensible graph parameters, save directories, material values, texture resolution, and fixed `PcgReview_<slug>` scene paths without pausing. Record the receipt and assumptions. Continue to the next stage when its definition of done passes; on a reference image, keep refining the fully textured asset until the shared final target is met or the shared hard ceiling is reached.

Ask only for a genuinely unusable reference, missing/ambiguous Unity workspace instance, unavailable required source asset, or an explicit request for interactive control. Unknown back faces, material inference, bevel taste, Subgraph partitioning, and parameter candidates are normal production decisions—make and record the best-supported choice.

## Completion and handoff

Never report success based on a graph canvas, white-model image, placeholder texture, or default Unity material. Deliver the AssetSpec, valid graph, geometry/UV/material/prefab receipts, and the final fully textured render score defined by `final-acceptance.md`.

This standard variant has no PCG pipeline-validation stage. Use `pcg-graph-authoring-unity-dev` when the user also needs the generator itself stress-tested for structure, exposed parameters, seed determinism, boundary inputs, regeneration, and output consistency.
