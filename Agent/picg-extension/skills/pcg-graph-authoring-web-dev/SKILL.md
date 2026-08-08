---
name: pcg-graph-authoring-web-dev
description: >-
  Create the same complete production-ready web assets as
  pcg-graph-authoring-web—PCG graph, cooked and validated white model,
  UV/projection, materials, textures, web bindings, exported asset, final
  render, and full-asset acceptance—while additionally validating the PCG
  generator after graph authoring for structure, parameters, seeds, boundary
  inputs, regeneration, performance, and AssetSpec-consistent output. It also
  runs a deterministic root/Subgraph layout pass before graph validation. Use
  for `/pcg-graph-authoring-web-dev`, PCG pipeline validation, capability-gap
  assessment, Subgraph layout, or generator development that must stop on a
  real pipeline gap.
---

# PCG Web complete-asset production — development validation

Produce the exact same finished asset and use the exact same acceptance standard as `pcg-graph-authoring-web`. This is a strict superset: it adds one PCG-specific validation gate; it does not replace the material, texture, export, render, or final scoring workflow.

## Single source of truth

Resolve paths relative to this skill directory:

```text
SHARED_DIR = ../shared/pcg-web-complete-asset
SHARED_SCRIPTS_DIR = ../shared/pcg-scripts
AUTHORING_SKILL_DIR = ../pcg-graph-authoring-web
DEV_SKILL_DIR = .
```

Read shared references directly. Do not inherit the other skill's `SKILL.md`, copy its workflow, or create a second final-acceptance standard. If the shared directory or authoring helper directory is missing, stop and report the installation fault.

## Mandatory read order

1. Read `SHARED_DIR/workflow.md`, `asset-contract.md`, and `pcg-graph-authoring.md`.
2. Read `DEV_SKILL_DIR/pipeline-capability-gate.md` before approving a newly authored graph.
3. For reference-image work, read `AUTHORING_SKILL_DIR/llm-orchestration.md`, `scripts.md`, and `web-review.md` before the first graph write or visual cook.
4. Read the same shared stage references as the standard skill immediately before geometry, material, texture, web, and final-acceptance stages. Recover through `SHARED_DIR/error-codes.md`.

## Web dev process guardrails (2026-08-08 brickify retrospective)

Before `PCG_PIPELINE_VALIDATION`, enforce the standard web orchestration loop — do not skip planning scripts or server preflight:

| Check | Script / doc |
|---|---|
| Plan + archived reference | `new_authoring_plan.py` → `archive_reference.py` → `validate_plan.py --strict-quality` |
| Vault evidence | `vault_search` + `rule_search(domain=pcg)` before first node write |
| Server health (every cook cycle) | `scripts/web/check_server.py` — use Shell `block_until_ms: 0`, not `nohup` |
| Manifest-server parity | `validate_pcg.py --check-server http://127.0.0.1:17890` before cook |
| PCGR error decode | `parse_pcgr.py` on `/v1/cook` binary (not manual `xxd`) |

See `AUTHORING_SKILL_DIR/web-review.md` and `llm-orchestration.md` for the full 13-step loop.

## Reference persistence and re-hydration (P0, reference-image jobs)

The dev gate adds many cook/review cycles, so compaction before the material stages is more likely than in the standard skill. The reference image is not durable memory.

- Follow the standard skill's P0: archive the reference with `SHARED_SCRIPTS_DIR/archive_reference.py` right after plan creation; keep `observation.layers` and `visualTokens` in `*-plan.json`.
- Re-hydrate before `PCG_PIPELINE_VALIDATION` and before every downstream stage: `<plan-stem>-RESUME.md` → `*-plan.json` → archived reference → latest `cmp_*.png`. Refresh via `SHARED_SCRIPTS_DIR/report_pass.py <plan> --resume` at each stage transition.
- Add the resume path to the layout/gate handoff receipts so a post-compaction session can re-enter without chat history.

## Execute the strict-superset workflow

```text
AssetSpec
→ PCG plan and graph authoring
→ ROOT_AND_SUBGRAPH_LAYOUT
→ PCG_PIPELINE_VALIDATION                 ← dev-only stage
→ white-model cook and geometry validation
→ UV/projection validation
→ material plan
→ texture production/import
→ web material creation and binding
→ exported asset output (glTF / web scene)
→ final fully textured render and acceptance
```

Author the graph according to the shared graph standard. After the graph exists, run `SHARED_SCRIPTS_DIR/layout_pcg.py` across the root and every inline `subgraphs[]` definition. Review the generated copy, confirm that only `position.x/y` changed, then run `validate_pcg.py`. Only after the independent layout pass and static authoring validation pass, run the development gate. The gate validates graph structure, connection compatibility, parameter exposure/ranges, seeds, deterministic regeneration, legal variation, boundary and invalid inputs, performance, generated hierarchy/references, output paths, stale-artifact cleanup, and correspondence with the AssetSpec.

## Dedicated root/Subgraph layout stage

Treat the root graph and each `subgraphs[]` definition as independent layout scopes. The layout helper performs a deterministic topological relayout:

- place each data-flow depth on a row using `ROW_STEP_Y=160`;
- assign independent source components to lanes with `COL_STEP_X>=320`;
- center multi-input joins and final `MergeMesh` nodes over their input lanes;
- keep `Output` / `SubgraphOutput` directly below their predecessor;
- separate true same-row siblings without changing graph data or wiring.

Use a review copy first:

```bash
python3 <SHARED_SCRIPTS_DIR>/layout_pcg.py <graph>.pcg \
  --out <graph>-layout.pcg
python3 <SHARED_SCRIPTS_DIR>/validate_pcg.py <graph>-layout.pcg
```

Use `--in-place` only after the copy passes. For a targeted repair, use `--subgraph <id> --skip-root`; for normal authoring, process all scopes. A layout warning from a Subgraph fan-in, staggered branch start, same-row title collision, or long wire is a `REWORK` at this stage—not a material or geometry issue.

Record the handoff before `PCG_PIPELINE_VALIDATION`:

```text
Subgraph layout: PASS | scopes=<n> | nodes=<n> | moved=<n> | position-only=PASS
```

## Gate behavior

- `PASS`: record the pipeline receipt and continue immediately to the same white-model and complete-asset stages as the standard skill.
- `REWORK`: return to graph planning, node construction, parameter configuration, or generation logic; retest. Do not compensate with materials or textures.
- `GAP` / `INEFFICIENT`: research the missing PCG capability and mature alternatives, report the product gap, and stop for explicit user override. Do not emit a graph that pretends the unsupported feature is production-ready.
- `OVERRIDE`: record the known fidelity/technical limit, then continue with the shared workflow. An override never changes final hard failures or score weights.

An obviously impossible required operation may be surfaced during planning, but it does not eliminate the required post-authoring validation gate for every graph that can be authored.

## Autonomous behavior and completion

Use the same autonomous defaults, receipts, refinement loop, blockers, durable deliverables, and `FINAL_ACCEPTED` definition as the standard variant. Do not ask separately about parameters, save locations, material choices, or pass continuation unless the user explicitly opted into interactive control.

Include the dev pipeline report beside—not instead of—the shared geometry, UV, material, export, render, and final-score report. The PCG gate adds technical evidence only; it must not alter the standard artistic/technical score weights or acceptance threshold.
