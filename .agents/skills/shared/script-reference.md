# Shared helper reference

These helpers are optional workflow tools, not instructions to create a full plan for every edit. Run only what the task needs. Paths below assume a shell at the repository root:

```bash
S=.agents/skills/shared/pcg-scripts
```

## Saved graph

```bash
python3 "$S/layout_pcg.py" path/to/graph.pcg --out path/to/graph-layout.pcg
python3 "$S/validate_pcg.py" path/to/graph-layout.pcg
```

Review the position-only diff before applying it. A focused layout can use `--subgraph <id> --skip-root`. Live targets need the reviewed positions applied back to the editor. `validate_pcg.py --check-server http://127.0.0.1:17890` adds running-server validation when relevant, especially after native node changes. A static PASS is not visual acceptance.

## Structured reference plans

```bash
python3 "$S/new_authoring_plan.py" "Cabin" --image /path/reference.png \
  --pcg path/to/cabin.pcg --out path/to/cabin-plan.json
python3 "$S/archive_reference.py" --plan path/to/cabin-plan.json --from-plan
python3 "$S/validate_plan.py" path/to/cabin-plan.json --strict-quality
python3 "$S/report_pass.py" path/to/cabin-plan.json --resume
```

For a complete triplet, replace `--image` with `--front`, `--side` and `--top`, and add `--require-triview` when archiving. Do not use `--force` to overwrite an existing plan by default.

The strict validator has structured-plan requirements (including archived references, populated observation layers and mapped details for the chosen complexity). Those checks remain intact. Choose this workflow when useful; do not invent details or views to pass it. `orchestrate_passes.py status/check/sync` and `report_pass.py` operate on its recorded pass model, not arbitrary task notes.

## Visual evidence

```bash
python3 "$S/make_comparison_sheet.py" --reference /path/archived-reference.png \
  --render screenshots/current.png --out screenshots/comparison.png
```

Use `--view-id <view>` for a named-view comparison. Image helpers have format/dependency constraints; check the script's `--help` and use an available converter when needed rather than assuming every host has macOS `sips`.

`append_review.py` records a review; it does not inspect pixels. For single-image visual passes, provide archived reference, current render, comparison and actual vision notes. Multi-view continuation uses `--view-evidence-json` with each required view's `viewId`, `referenceScreenshot`, `renderScreenshot`, `comparisonImage`, `aiVisionScore`, `aiVisionNotes` and `cameraReceipt`. Preserve an existing plan's pass thresholds and supported action names; only use `continue` when its checks truly pass.

## Binary cook diagnostics

[parse_pcgr.py](pcg-scripts/parse_pcgr.py) decodes a saved PCGR cook response:

```bash
python3 "$S/parse_pcgr.py" /path/cook.pcgr --json
```

A successful decode is not a successful cook: inspect the reported error/output fields. Platform capture and export commands live in [Web scripts](../pcg-graph-authoring-web/scripts.md) and [Unity scripts](../pcg-graph-authoring-unity/scripts.md).
