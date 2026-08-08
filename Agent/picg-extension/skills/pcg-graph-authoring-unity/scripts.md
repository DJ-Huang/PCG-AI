# Orchestration scripts cheatsheet

All scripts are pure Python 3.10+ **stdlib** (PNG via `struct`/`zlib`; macOS `sips` for non-PNG).
Run from anywhere; prefer absolute paths. Non-zero exit = gate failed.

**Division of labor:** scripts enforce plan structure and package evidence; they **never** score
visuals. Agent vision inspects the comparison sheet and supplies fidelity / action.

Shared scripts: `../shared/pcg-scripts/` (generic Python helpers, shared across Unity/web variants).
Unity C# templates: `scripts/unity/` (this skill directory).

**Unity gate (P0):** before any visual `continue`, follow [unity-review.md](unity-review.md) — `user-tuanjie` connect + new `PcgReview_*` scene + SceneView capture. Templates in `scripts/unity/`.

## new_authoring_plan.py

```bash
python3 ../shared/pcg-scripts/new_authoring_plan.py "Ghost Protocol Glock" \
  --image /path/ref.png --complexity complex \
  --pcg examples/ghost-protocol-glock.pcg \
  --out examples/ghost-protocol-glock-plan.json --force
```

Emits a starter `*-plan.json` with buildPasses, qualityContract, detailInventory,
observation, and visualTokens scaffolds. Fill from layered observation before
authoring `.pcg` nodes.

## archive_reference.py (P0 — run immediately after new_authoring_plan.py)

```bash
python3 ../shared/pcg-scripts/archive_reference.py \
  --image /path/ref.png \               # local path | https://URL | data: URI
  --plan examples/ghost-protocol-glock-plan.json
```

Copies/downloads the reference to `ref_<slug>.<ext>` next to the plan and
rewrites `sourceImage` to that local file (original kept in
`referenceArchive.originalSource`). A chat attachment or URL is not durable
memory — compaction drops it. Never feed `make_comparison_sheet.py` a URL.

## validate_plan.py

```bash
python3 ../shared/pcg-scripts/validate_plan.py plan.json
python3 ../shared/pcg-scripts/validate_plan.py plan.json --strict-quality
```

`--strict-quality` blocks shallow plans (generic DoD, unassessed class, too few mapped details /
modules for the complexity tier) **and** broken reference persistence (no archived local file,
`sourceImage` still a URL/data URI, fewer than 6 of 8 `observation.layers` filled).

## report_pass.py

```bash
python3 ../shared/pcg-scripts/report_pass.py plan.json
python3 ../shared/pcg-scripts/report_pass.py plan.json --json
python3 ../shared/pcg-scripts/report_pass.py plan.json --resume            # writes <plan-stem>-RESUME.md
python3 ../shared/pcg-scripts/report_pass.py plan.json --resume /path/R.md # custom location
```

Prints current unlocked pass, next `orchestrate_passes.py check` command, unmet acceptance
criteria, and a suggested `append_review.py` line. Analogous to img2threejs `forge/next.py`.

`--resume` regenerates a deterministic RESUME file (archived reference path, latest comparison,
current pass, fidelity trend, last mismatches, next command). Run it at every stage transition
and after each review cycle; after context compaction, read the RESUME file **first**, then the
plan JSON, then the archived reference.

## orchestrate_passes.py

```bash
python3 ../shared/pcg-scripts/orchestrate_passes.py status plan.json
python3 ../shared/pcg-scripts/orchestrate_passes.py check plan.json --pass-id blockout
python3 ../shared/pcg-scripts/orchestrate_passes.py sync plan.json --in-place
```

- `status` — unlocked pass + strict-quality issues
- `check` — non-zero if pass locked or (for post-module-plan) strict-quality fails
- `sync` — recompute `sculptPipeline` from `reviewHistory`

Pass order: `module-plan → blockout → structural → form-refinement → bevel-pass → assembly →
material-pass → parameters → validation`.

## make_comparison_sheet.py

```bash
python3 ../shared/pcg-scripts/make_comparison_sheet.py \
  --reference /path/ref.png \
  --render /path/SceneView.png \
  --out /tmp/cmp.png --json
```

Left = reference, right = render screenshot. Does **not** score — inspect with agent vision.

## append_review.py

```bash
python3 ../shared/pcg-scripts/append_review.py plan.json \
  --pass-id blockout \
  --fidelity 0.72 \
  --action continue \
  --summary "Macro silhouette OK; grip taper still boxy" \
  --reference-screenshot examples/ref_ghost-protocol-glock.png \
  --render-screenshot /path/SceneView.png \
  --comparison-image /tmp/cmp.png \
  --ai-vision-score 0.72 \
  --ai-vision-notes "slide stepped sight reads; grip candy gradient flat" \
  --mismatches "grip taper" \
  --in-place
```

Actions: `continue | refine-plan | refine-graph | refine-cook | request-input | stop`.

`continue` on visual passes requires reference + render + comparison paths, non-empty
`--ai-vision-notes`, and score ≥ threshold (default 0.7 for mid-pass unlock). Reference must be
the archived local file, not a chat attachment or URL. Entries missing these do not unlock the
next pass (`review_completes_pass` enforces it on replay). Under **Autonomous mode** (SKILL.md):
overall stop target is **0.9** — if score < 0.9, prefer `refine-graph` / `refine-cook` and
immediately loop; do not `stop` at ~0.75–0.85. Use `request-input` only for critical blockers
(Unity MCP / unusable reference / pipeline GAP via dev skill).

## layout_pcg.py

```bash
python3 ../shared/pcg-scripts/layout_pcg.py path/to/graph.pcg \
  --out path/to/graph-layout.pcg
python3 ../shared/pcg-scripts/layout_pcg.py path/to/graph.pcg \
  --subgraph walls --skip-root --out /tmp/walls-layout.pcg
```

Relayouts the root and every inline `subgraphs[]` definition independently.
The pass derives topological rows, source lanes, multi-input join centers, and
same-row collision spacing. It changes only node `position.x/y`; it does not
rewrite node data, edges, parameters, or Subgraph interfaces. Review the output
copy, run `validate_pcg.py`, then use `--in-place` if the positions are accepted.

The helper uses `ROW_STEP_Y=160` and `COL_STEP_X=360` by default. Keep
`--col-step` at or above 320 to preserve node title spacing.

## validate_pcg.py

```bash
python3 ../shared/pcg-scripts/validate_pcg.py examples/your-graph.pcg
```

Graph JSON, pin, title, layout, Subgraph modularity, fan-in centering, and
Merge→Bevel validator. It checks every root and inline Subgraph scope. Run it
after the layout pass and after each substantive `.pcg` edit.

## Suggested agent loop

```text
0. unity_list_instances → select workspace Unity → ping  (unity-review.md)
1. new_authoring_plan.py → archive_reference.py (reference to local disk, plan rebound)
2. Layered observation → write observation.layers + visualTokens INTO plan.json
3. validate_plan.py --strict-quality
4. report_pass.py --resume → unlock current pass (+ RESUME.md)
5. Auto params + auto saveDir → author .pcg for that pass (no ask_user)
6. layout_pcg.py → relayout root + every inline Subgraph definition
7. validate_pcg.py
8. create_review_scene.cs.txt → Assets/PICGGenerator/Scenes/PcgReview_<slug>.scene  (fixed; no ask)
9. execute_csharp_script setup_pcg_review_subject.cs.txt → cook
10. execute_csharp_script capture_sceneview_png.cs.txt
11. make_comparison_sheet.py --reference <archived ref_<slug>> --render …
12. Agent vision → append_review.py (one action; reference + vision notes mandatory)
13. If fidelity < 0.9 or DoD unmet → refine-* → goto 5 (no user gate)
14. Else report_pass.py --resume → next pass / stop at ceiling (final stop ≥ 0.9)
```

Do **not** insert "confirm params / save dir / continue?" between steps 5–14.
After any context compaction: read `<plan-stem>-RESUME.md` → plan.json → archived reference → latest cmp sheet, then continue.

## Unity C# templates (`scripts/unity/`)

| File | Use with |
|------|----------|
| `create_review_scene.cs.txt` | `execute_csharp_script` — Empty scene → `Assets/PICGGenerator/Scenes/PcgReview_<slug>.scene` |
| `setup_pcg_review_subject.cs.txt` | `execute_csharp_script` — load `.pcg`, cook, frame SceneView |
| `capture_sceneview_png.cs.txt` | `execute_csharp_script` — write `screenshots/SceneView_*.png` |

Replace `__GRAPH_ASSET_PATH__`, `__REVIEW_SLUG__`, `__SCREENSHOT_REL__` before running.
Full sequence: [unity-review.md](unity-review.md).
