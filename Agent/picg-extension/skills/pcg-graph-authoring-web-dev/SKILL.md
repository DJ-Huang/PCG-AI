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
  for `/pcg-graph-authoring-web-dev`, PCG pipeline validation, three-view /
  三视图 / orthographic reconstruction, capability-gap assessment, Subgraph layout, PCG MCP graph authoring and seed/performance validation, or
  generator development. Stop only for a real capability GAP that needs
  user override, unusable references, or a dead editor after start attempts —
  never park at a white-model or pipeline REWORK.
---

# PCG Web complete-asset production — development validation

Produce the exact same finished asset and use the exact same acceptance standard as `pcg-graph-authoring-web`. This is a strict superset: it adds one PCG-specific validation gate; it does not replace the material, texture, export, render, or final scoring workflow.

## Single source of truth

Resolve paths relative to this skill directory:

```text
SHARED_DIR = ../shared/pcg-web-complete-asset
SHARED_SCRIPTS_DIR = ../shared/pcg-scripts
PCG_MCP_CONTRACT = ../shared/pcg-mcp.md
AUTHORING_SKILL_DIR = ../pcg-graph-authoring-web
DEV_SKILL_DIR = .
```

Read shared references directly. Do not inherit the other skill's `SKILL.md`, copy its workflow, or create a second final-acceptance standard. If the shared directory or authoring helper directory is missing, stop and report the installation fault.

## Mandatory read order

1. Read `PCG_MCP_CONTRACT`, then `SHARED_DIR/workflow.md`, `asset-contract.md`, and `pcg-graph-authoring.md`.
2. Read `DEV_SKILL_DIR/pipeline-capability-gate.md` before approving a newly authored graph.
3. For reference-image work, read `AUTHORING_SKILL_DIR/llm-orchestration.md`, `scripts.md`, and `web-review.md` before the first graph write or visual cook. For front/side/top input, also read `AUTHORING_SKILL_DIR/triview.md`.
4. Read the same shared stage references as the standard skill immediately before geometry, material, texture, web, and final-acceptance stages. Recover through `SHARED_DIR/error-codes.md`.

## Live Web editor (P0 — not optional)

Graph construction happens **on the open Web editor page through PCG MCP**, not as a disk JSON dump or a one-off generator script.

1. Start Vite + pcg-server if needed (`block_until_ms: 0`). Call `pcg_get_editor_context`. That session **is** the page.
2. Author with `pcg_get_node_types` → `pcg_apply_graph_ops` / `pcg_patch_node` / `pcg_replace_graph` (nodes + pin-accurate edges on the canvas). Rapid loop: `pcg_validate` → `pcg_cook` → `pcg_capture_preview`.
3. `pcg_save_graph` to the AssetSpec path. Then layout helper + `validate_pcg.py` on the **saved** file. `/review` + Playwright is saved-file ortho acceptance, not the authoring surface.
4. Forbidden: `_author.py`, hand-written full `.pcg` `Write`, or “open this file later.” If MCP is offline, recover the editor and retry; do not switch to file authoring.

## Web dev process guardrails (2026-08-08 brickify retrospective)

Before `PCG_PIPELINE_VALIDATION`, keep the plan/archive/preflight loop. **Do not** skip the plan. **Do not** replace MCP canvas ops with a graph-codegen script.

| Check | Script / doc |
|---|---|
| Plan + archived reference | After the first image, ask `front` → `side` → `top` in order (see `triview.md`) → `new_authoring_plan.py` (`--front/--side/--top` when given) → `archive_reference.py --from-plan` (`--require-triview` only for a complete triplet) → `validate_plan.py --strict-quality` |
| Vault evidence | `pcg_kb_search(category="rules")` + `pcg_kb_search(category="kb")` before first node write |
| Live page | `pcg_get_editor_context` online; author with MCP ops on that page |
| Server health (every cook cycle) | `scripts/web/check_server.py` — use Shell `block_until_ms: 0`, not `nohup` |
| Manifest-server parity | `validate_pcg.py --check-server http://127.0.0.1:17890` after MCP save, before saved-file cook |
| PCGR error decode | `parse_pcgr.py` on `/v1/cook` binary (not manual `xxd`) |

See `PCG_MCP_CONTRACT`, `AUTHORING_SKILL_DIR/web-review.md`, and `llm-orchestration.md`.

## Reference persistence and re-hydration (P0, reference-image jobs)

The dev gate adds many cook/review cycles, so compaction before the material stages is more likely than in the standard skill. The reference image is not durable memory.

- Follow the standard skill's P0: archive every required view with `SHARED_SCRIPTS_DIR/archive_reference.py` right after plan creation; keep `observation.layers`, `viewObservations`, `crossViewConstraints`, and `visualTokens` in `*-plan.json`.
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

Author the graph according to the shared graph standard **on the live Web page via MCP**. After MCP save, run `SHARED_SCRIPTS_DIR/layout_pcg.py` across the root and every inline `subgraphs[]` definition, push positions back with MCP `move_node` (or `--in-place` then reload/save), confirm that only `position.x/y` changed, then `validate_pcg.py`. Only after the independent layout pass and static authoring validation pass, run the development gate. The gate validates graph structure, connection compatibility, parameter exposure/ranges, seeds, deterministic regeneration, legal variation, boundary and invalid inputs, performance, generated hierarchy/references, output paths, stale-artifact cleanup, and correspondence with the AssetSpec.

Run gate cooks and parameter sweeps through MCP (`pcg_cook` / `pcg_patch_node` / `pcg_apply_graph_ops`) on the open page, confirm `applied=true`, restore the intended state with a fresh hash, then `pcg_save_graph`. The Vite `/review` route is saved-file final ortho review, not a substitute for live canvas authoring.

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
- `REWORK`: immediately return to MCP node/wire/parameter edits on the open page; recook and retest. Do **not** end the turn or ask the user to continue. Do not compensate with materials or textures.
- `GAP` / `INEFFICIENT`: research the missing PCG capability and mature alternatives, report the product gap, and stop for explicit user override. Do not emit a graph that pretends the unsupported feature is production-ready.
- `OVERRIDE`: record the known fidelity/technical limit, then continue with the shared workflow. An override never changes final hard failures or score weights.

An obviously impossible required operation may be surfaced during planning, but it does not eliminate the required post-authoring validation gate for every graph that can be authored.

## Autonomous behavior and completion

Use the same autonomous defaults, receipts, refinement loop, blockers, durable deliverables, and `FINAL_ACCEPTED` definition as the standard variant. Keep working through geometry, UV, materials, textures, export, and final scoring in the same session. Do not park at `GRAPH_WRITTEN`, `WHITE_MODEL_APPROVED`, or pipeline `REWORK`.

Ask only for missing `front` / `side` / `top` after the first reference image, a genuinely unusable reference, a Web editor that will not come online after start attempts, or an explicit pipeline `GAP`. Do not ask about parameters, save locations, material choices, or “是否继续”.

Legal `stop` is only: `FINAL_ACCEPTED`; twelve consecutive refine cycles with no measurable improvement (report residual gaps, do not lower the bar); or `GAP` / `INEFFICIENT` awaiting override.

Include the dev pipeline report beside—not instead of—the shared geometry, UV, material, export, render, and final-score report. The PCG gate adds technical evidence only; it must not alter the standard artistic/technical score weights or acceptance threshold.
