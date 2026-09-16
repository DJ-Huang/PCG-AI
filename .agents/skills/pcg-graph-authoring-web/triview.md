# Three-view reconstruction (web)

Use this when the user supplies **front / side / top** orthographic references, or when a single photo is not enough to lock width, height, and depth. Read this before the first graph write. Scripts never score pixels; agent vision scores **each required view independently**.

## Input contract

Accept any of:

- three files already labelled: `--front` `--side` `--top`
- a labelled set: `archive_reference.py --view front=... --view side=... --view top=... --require-triview`
- one image (photo, concept, or a board with several drawings): **stop and ask** — do not crop, parse, or invent the other views

### After the first image: ask for views in order (P0)

Do **not** split a composite sheet. Do **not** keep `mode=single` by guessing. Before `new_authoring_plan.py`, collect dedicated files **one view per turn**:

1. Ask for the **front** view. Wait.
2. Ask for the **side** view. Wait. Record `coordinateFrame.sideView=right` unless the user says it is the left.
3. Ask for the **top** view. Wait.

Skip a step only when that role is already a separate uploaded file. If the user explicitly says to proceed with a single image, keep `mode=single` and do **not** invent drawings.

Then:

```bash
python3 <SHARED_SCRIPTS_DIR>/new_authoring_plan.py "<Name>" \
  --front <front.png> --side <side.png> --top <top.png> \
  --out <plan.json>
```

Default object frame (PCG / Unity left-handed):

| Axis | Meaning |
|---|---|
| `upAxis` | always `+y` |
| `frontAxis` | `+z` unless the reference or AssetSpec says otherwise (`+x` / `-x` / `-z`) |
| `sideView` | `right` profile unless the side drawing is the left |

Record the frame in `*-plan.json` `coordinateFrame`. Every later camera preset and `TransformMesh` offset uses this frame, not chat memory.

## Plan fields that must exist before nodes

After `new_authoring_plan.py` + `archive_reference.py --from-plan --require-triview`:

1. Fill `observation.layers` (≥6 of 8) from **all** views, not just the pretty one.
2. Fill `observation.viewObservations.front|side|top`: silhouette prose + ≥2 landmarks each.
3. Fill `observation.crossViewConstraints` for width (front+top), height (front+side), depth (side+top). Each needs a positive metre `value`, `tolerance` in `(0, 0.1]`, and a `driver` node/property owner.
4. Fill `componentHypotheses[]`: one row per macro part with `chosenNodeType`, live `manifestEvidence`, `crossViewEvidence`, and `dimensionDrivers`.
5. Run `validate_plan.py --strict-quality`. Do not write `.pcg` nodes while this fails.

A dimension has **one owner**. If front width and top width disagree beyond tolerance, record the conflict in `observation.conflictResolutions` and pick the sharper drawing; do not average silently.

## Node edit accuracy

Query `pcg_get_node_types` (or `schema/node-manifest.json`) before every new type. Then:

| Rule | Fail if |
|---|---|
| Primitive from the **section**, not from one silhouette | A box stands in for a sweep/revolve that two views contradict |
| Size from constraint drivers, in metres | Pixel-counting a screenshot without the plan scale |
| Placement from landmark deltas on the owning views | Guessing `TransformMesh` from a three-quarter shot |
| One atomic pass per mismatch | Changing width, depth, and bevel in one unreviewed dump |
| Re-capture **all required views** after a size change | Scoring only the view you just fixed |

Cross-view owners:

- width → front + top
- height → front + side
- depth → side + top
- yaw / front axis → top + front
- attachment height → front + side
- plan footprint / inset → top

Identity details (seams, fasteners, openings) must `mapsTo` a real node id/property. Prose-only inventory is a gate failure.

## Capture and score

Deterministic cameras live on `/review?graph=...&camera=front|side|top|three-quarter&frontAxis=+z&sideView=right`.

```text
setup_web_review.py graph.pcg --json
capture_webview_png.py "<url>" --cameras front,side,top,three-quarter \
  --front-axis +z --side-view right --slug <slug> --out-dir screenshots --json
```

For each required view: `make_comparison_sheet.py --reference ref_<slug>_<view>.<ext> --render screenshots/<slug>_<view>.png --view-id <view> --out screenshots/cmp_<slug>_<view>.png`

Vision checklist per view:

- silhouette / proportions
- landmark alignment
- missing or extra parts
- openings and thickness
- conflict with the other two views

`continue` on a visual pass requires `--view-evidence-json` covering every required view. The unlock score is the **worst** required view, not the average. `three-quarter` is an integrity view: it cannot hide a failed ortho view, and a failed three-quarter (collapsed depth, intersecting parts) blocks `cross-view-geometry-lock`.

Do not screenshot the editor chrome. Capture the WebGL canvas after `window.__pcgReady`. Keep the camera receipt from `capture_webview_png.py` on each view evidence object.
