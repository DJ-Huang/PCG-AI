---
name: pcg-graph-authoring
description: >-
  Author PCG Graph JSON (.pcg) files for the PCG-AI repo with Houdini-style
  top-to-bottom layout. Use when creating, editing, or scaffolding .pcg graphs,
  bridge/spline/mesh/scatter demos, or when the user mentions PCG graph, node
  wiring, bridge-demo, or Houdini layout.
---

# PCG Graph Authoring (PCG-AI)

Create `.pcg` files for this repo. **Layout is Houdini-style: data flows top → bottom**, not left → right.

## Before writing

1. Read `schema/node-manifest.json` — SSOT for node `type`, pin ids, pin types, property defaults.
2. **[Mandatory] Load PCG rules via vault RAG** — `rule_search(query=装配倒角 OR 模型类型关键词, domain=pcg, top_k=10)`. Always expect `pcg/assembly-bevel` + `pcg/index`; open matching type (`pcg/vehicle`, `pcg/bridge`, …) with `vault_get_chunk` or Read `VAULT_ROOT/Rules/pcg/<name>.md`. If no matching type, derive from shape analysis and note the gap.
3. Skim a similar example under `examples/` (see [examples.md](examples.md)) for **topology patterns** only (how nodes wire together), not for modeling strategy.
4. Never invent node types or pin ids; copy from manifest.

## Shape analysis (mandatory when user provides a reference image)

Before choosing any node, analyze the reference image and record the analysis in your reply:

| Aspect | What to identify | Affects |
|--------|-----------------|--------|
| **Body shape** | Box / variable-section / revolve / scatter assembly | Primary node choice |
| **Cross-section** | Constant / variable / symmetric / asymmetric | Sweep vs Box vs Loft |
| **Side profile** | Rectangular / trapezoidal / wedge / curved | Profile spline control points |
| **Surface quality** | Hard edges (low-poly) / light bevel / smooth curves | BevelMesh amount, SubdivisionMesh levels |
| **Component list** | Every visible part and its shape type | Per-part node selection |

Record the analysis as a short table in your reply before writing any nodes.

## Node selection guide

### By body shape

| Shape | Recommended nodes | Do NOT use |
|-------|-------------------|------------|
| Constant-section extrusion | `SweepAlongSpline` (circle/rectangle) + `CreateSpline` (line) | `CreateBoxMesh` |
| Variable-section body | `SweepAlongSpline` (crossSection) + `CreateSpline` (catmullRom, closed) | `CreateBoxMesh` ❌ |
| Perfect rectangular prism | `CreateBoxMesh` | `SweepAlongSpline` |
| Revolve body | `RevolveMesh` (profile spline) or `SweepAlongSpline` (circle) | `CreateBoxMesh` ❌ |
| Multi-part assembly | Choose per-part by above rules; do not default everything to Box | All `CreateBoxMesh` ❌ |

### Key decision rules

1. **Does the cross-section change along the length?**
   - Yes → `SweepAlongSpline` + `CreateSpline` (crossSection, catmullRom). Use `scaleStart`/`scaleEnd` if taper is uniform.
   - No, and the shape is a perfect rectangle → `CreateBoxMesh`.

2. **Does the reference image show curved / trapezoidal / non-rectangular cross-sections?**
   - Yes → Must use `CreateSpline` (catmullRom, closed) to define the section. `CreateBoxMesh` can only produce rectangular sections.
   - No → `CreateBoxMesh` is acceptable.

3. **Does the reference image show tapering (narrow at one end, wide at other)?**
   - Yes → `SweepAlongSpline` with `scaleStart` ≠ `scaleEnd`.
   - No → `scaleStart = scaleEnd = 1.0`.

4. **Is the part a small detail?**
   - If rectangular → `CreateBoxMesh` + `TransformMesh` (scale + translate) is fine.
   - If curved → `SweepAlongSpline` or `CreateSpline` profile.

5. **Anti-pattern: using `CreateBoxMesh` for everything in an assembly.**
   - This is the most common mistake. Even for low-poly styles, bodies with any taper or non-rectangular section must use `SweepAlongSpline`. Reserve `CreateBoxMesh` for parts that are genuinely rectangular.

6. **Anti-pattern: `MergeMesh → BevelMesh` on multi-part assemblies.**
   - See [Bevel placement](#bevel-placement-p0-for-assemblies). Never put a single final Bevel after merging heterogeneous parts.

### Bevel placement (P0 for assemblies)

Houdini hard-surface practice: **PolyBevel on one clean solid (or a Grouped selection on that solid), then Merge parts** — not the reverse. SideFX PolyBevel is topology-sensitive; forums commonly fail when beveling after Merge of separate objects without Fuse into a single manifold.

#### ❌ Forbidden default (high risk)

```text
partA / partB / hose / details → MergeMesh → BevelMesh → Output
```

| Risk | Why |
|------|-----|
| Scale mismatch | One `amount` cannot fit body (~meter) and hose/rim (~cm); small parts self-intersect or vanish |
| Multi-component soup | `MergeMesh` keeps disconnected solids; Bevel corner/mitre logic assumes local manifold neighborhood |
| Wrong edge selection | Angle/group limits applied globally pick noise edges on high-res sweeps and miss intended hard edges |
| Topology damage amplified | Attribute nodes that break n-gon topology before Merge make post-merge Bevel worse (see vault `pit-pcg-geometry-attribute-roundtrip`) |

**Historical bad example** (fixed 2026-07-16): `biohazard-canister.pcg` once ended with `merge → bevel → out`. Rule SSOT: `Rules/pcg/assembly-bevel.md` (`rule_id: pcg/assembly-bevel`).

#### ✅ Correct patterns

| Scenario | Topology |
|----------|----------|
| Single solid | `… → [GroupCreate] → BevelMesh → Output` |
| Multi-part assembly | **Per-part** `… → BevelMesh` (amount scaled to that part) → `MergeMesh` → Output |
| Main body + un-beveled details | `body → BevelMesh` → `MergeMesh ← details` → Output |
| Houdini Group style | `GroupCreate(angle)` → `BevelMesh(edgeGroup)` on **one** part, then Merge |

Strategy rules already follow this (`pcg/assembly-bevel`, `pcg/vehicle`, `pcg/bridge`: Bevel on deck/body **before** Merge of piers/wheels/details).

#### When post-merge Bevel is acceptable

Only if **all** are true:

1. Inputs were Boolean-unioned / fused into **one** connected manifold (not a visual stack of separate solids)
2. Feature sizes are homogeneous (same scale family)
3. Edge selection is explicit (`edgeGroup` / GroupCreate), not blind whole-mesh angle bevel

Otherwise: **bevel per part, then merge**.

#### Per-part amount rule

Scale `amount` to **that part’s** max dimension, not the whole assembly AABB. A hose with radius 0.06 needs ~0.005–0.015; a 3.7-tall canister body needs ~0.05–0.15. Sharing one post-merge amount is always wrong for mixed-scale assemblies.

### Strategy rules consultation (mandatory)

**Path**: `VAULT_ROOT/Rules/pcg/`（`rule_search domain=pcg`）

1. `rule_search` — always hit `pcg/assembly-bevel`; then match model type (`pcg/vehicle`, `pcg/bridge`, …).
2. Read full rule via `vault_get_chunk` or `Rules/pcg/<name>.md` — use per-part mapping as **reference**, not a copy.
3. Always validate against the current reference image; adapt if the image differs.
4. If no matching type exists, derive from shape analysis; consider adding `Rules/pcg/<type>.md` + `rules-vault-index` + reindex.

**Do not** blindly copy an existing `examples/*.pcg` strategy — examples may contain anti-patterns (marked ⚠️ in the strategy rules).

## Physical-world sizing (P0 — default unless user overrides)

**Default rule**: Unless the user explicitly states a stylized / non-physical scale (e.g. "low-poly toy", "abstract", "gameplay-scale"), every object's dimensions MUST match real-world physical sizes. PCG-AI units are **meters**; model 1 unit = 1 meter.

Rationale: graphs are imported into Unity scenes that also contain real-world-scale assets (vehicles, buildings, props). A canister modeled at 0.3 units tall would be 30 cm in scene — invisible next to a 4 m car. The canonical `examples/biohazard-canister.pcg` already follows this: ~3.7 m tall canister, hose radius 0.06 m, wheel rim radius 0.4 m.

### Quick reference — real-world anchors (meters)

Use these as sanity anchors when no reference image gives explicit dimensions. Do NOT invent sizes far outside the realistic range for the object class.

| Object class | Typical real size | Notes |
|--------------|-------------------|-------|
| Beverage can | Ø0.065 × 0.12 | radius 0.0325 |
| Biohazard / chemical canister | Ø0.4–0.8 × 0.8–1.5 (small) to 3.7 (large drum) | canister.pcg = 3.7 tall drum |
| Door | width 0.8–1.0, height 2.0–2.1 | |
| Human (standing) | height 1.6–1.85 | good scale reference to include |
| Car (sedan) | length 4.0–5.0, width 1.8–2.0, height 1.4–1.5 | wheel Ø0.6–0.7 |
| Truck / pickup | length 5.0–6.5, height 1.8–2.2 | |
| Bicycle | length 1.7, wheel Ø0.66 | |
| Table | height 0.75, top 1.2 × 0.8 | |
| Chair | height 0.45 (seat), 0.9 (back) | |
| Bridge (single span) | span 10–40, deck width 3–8 | see bridge-demo |
| Smartphone | 0.15 × 0.07 × 0.008 | |
| Coin | Ø0.02–0.026, thickness 0.002 | |

### Sizing checks (mandatory before writing)

1. **Overall dimension**: after shape analysis, pick a real-world target size for the primary object. State it in the reply (e.g. "canister ≈ 3.7 m tall, real-world chemical drum").
2. **Per-part consistency**: every sub-part's size must be consistent with the whole in real world. A wheel on a 4.5 m car cannot be Ø0.1 (that's a toy). Scale sub-parts relative to the body, not in arbitrary units.
3. **No unit drift**: keep ALL geometry nodes in meters. Do not mix cm-scale control points (e.g. `0.06`) with m-scale translations (e.g. `400`) — that is the #1 cause of "model vanished / huge" bugs. If a spline profile uses small numbers, the body transform and other parts must use the same scale family.
4. **User override**: if the user wants stylized / non-physical scale, record it explicitly in the reply ("stylized: 0.3-unit toy scale") and skip the real-world anchor — but still keep all parts internally consistent.

### Anti-pattern

- ❌ Modeling a "realistic car" at 0.5 units long, or a "human-scale prop" at 50 units. Always ask: *would this object look right standing next to a 1.8 m person in the scene?*
- ❌ Mixing scale families within one graph (cm spline + m body). Pick one and convert.

## Parameter quality baselines

### BevelMesh

Bevel parameters must be proportional to model size. Too-small bevels are invisible.

| Model max dimension | Min `amount` | Recommended `amount` | Recommended `segments` |
|---------------------|-------------|---------------------|----------------------|
| < 1.0               | 0.03        | 0.05–0.08           | 2                    |
| 1.0–3.0             | 0.05        | 0.08–0.15           | 2–3                  |
| 3.0–5.0             | 0.08        | 0.12–0.20           | 3                    |
| > 5.0               | 0.10        | 0.15–0.30           | 3–4                  |

**Rule**: `amount < modelMaxDim × 0.02` is invisible — treat as invalid and increase.

### SubdivideMesh (before BevelMesh)

`BevelMesh` with `segments ≥ 3` needs sufficient topology. `SubdivideMesh` levels=1 (4 quads per face) is not enough.

| Purpose | Min `levels` | Note |
|---------|-------------|------|
| Pre-BevelMesh (segments ≤ 2) | 1 | Barely sufficient |
| Pre-BevelMesh (segments ≥ 3) | 2 | Recommended |
| Future SubdivisionSurface | ≥ 2 | Provides base topology |

### SweepAlongSpline

| Property | Guideline |
|----------|-----------|
| `sampleSpacing` | ≤ 0.5 for smooth curves; > 1.0 produces visible facets |
| `columns` (circle) | ≥ 16 for wheels; ≥ 8 is minimum for any circle |
| `subdivisions` (CreateSpline catmullRom) | ≥ 8 for smooth profile curves |

## Parameter exposure (ask user before writing)

After filling `data` from defaults (Step 6), **present key parameters to the user** via `ask_user` before writing the file. This gives the user a chance to adjust values that affect visual quality and model proportions.

### Tier 1 — Always expose

These are model-defining parameters that the user should always see, regardless of graph complexity:

| Parameter | Source node(s) | What to show | Default logic |
|-----------|---------------|-------------|---------------|
| **Overall scale** | All geometry nodes | Model max dimension (X/Y/Z range) + unit hint + **real-world target size** | Derived from shape analysis, matched to [Physical-world sizing](#physical-world-sizing-p0--default-unless-user-overrides) anchors; flag if user wants stylized scale |
| **Bevel amount + segments** | `BevelMesh` | Current `amount` / `segments` values | Auto from Parameter Quality Baselines table |
| **Surface quality** | `SweepAlongSpline` `shadeMode` | auto / flat / smooth | Default `auto` (group + angle) |

### Tier 2 — Conditionally expose

Show these when the corresponding node type is present in the graph:

| Node type | Parameter | What to show | Default |
|-----------|-----------|-------------|---------|
| `RevolveMesh` | `segments` | Radial resolution (e.g. 32 = smooth, 8 = faceted) | 32 |
| `RevolveMesh` | `capStart` / `capEnd` | Whether ends are capped | true / true |
| `SweepAlongSpline` | `sampleSpacing` | Curve sampling density | 0.5 (smooth) or 1.0 (coarse) |
| `SweepAlongSpline` | `columns` (circle) or `radius` | Cross-section resolution / size | 16 / from profile |
| `CreateCylinderMesh` | `radialSegments` | Circle smoothness | 16 |
| `CreateSpline` (catmullRom) | `subdivisions` | Curve smoothness | 8–12 |
| `InstanceAlongSpline` | `spacing` | Instance density | From model size |
| `SubdivideMesh` | `levels` | Subdivision depth pre-bevel | 1–2 |

### Tier 3 — Optional (expose on request only)

Not shown by default, but available if the user asks "expose more parameters":

| Node type | Parameter | What to show |
|-----------|-----------|-------------|
| `VertexColor` | `r, g, b, a` | RGBA values (0–1) |
| `AssignMaterial` | `materialName` | Material name string |
| `UVTexture` | `projection, axis, scaleU, scaleV` | UV mapping mode |
| `ProjectTexture` | `direction, scaleU, scaleV` | Projection direction |
| `BevelMesh` | `profile, miterOuter, miterInner` | Bevel profile shape |
| `SweepAlongSpline` | `twist, profileRoll` | Twist / roll degrees |
| `CreateSpiralSpline` | `radius, pitch, turns` | Spiral parameters |
| `MeshNoiseDeform` | `intensity, scale` | Noise displacement |

### `ask_user` interaction pattern

Use a **single `ask_user` call** with 1–3 questions. Never exceed 4 questions.

**Question 1 (always)**: Present Tier 1 + relevant Tier 2 parameters as a summary, ask to confirm or adjust.

```
type: "choice"
header: "参数确认"
question: "以下关键参数已自动填充，是否需要调整？\n\n{parameter summary table}"
options:
  - label: "确认，直接生成"
    description: "使用当前参数直接生成 .pcg 文件"
  - label: "调整部分参数"
    description: "告诉我需要调整哪些参数"
  - label: "暴露更多参数"
    description: "显示 Tier 3 可选参数（材质、UV、高级 bevel 等）"
```

**If "调整部分参数"**: use a follow-up `ask_user` with `type: "text"` for the user to specify which parameters to change. Apply changes, then proceed.

**If "暴露更多参数"**: present Tier 3 parameters relevant to the graph as a second `ask_user` `choice` question. After user selects, apply changes, then proceed.

**If "确认，直接生成"**: proceed to write file immediately.

### Parameter summary format

When presenting parameters, use this compact format:

```
部件: 罐体 (RevolveMesh)
  segments: 32  |  capStart: true  |  capEnd: true

部件: 软管 (SweepAlongSpline)
  sampleSpacing: 0.2  |  radius: 0.08  |  columns: 8

全局:
  模型尺寸: ~1.5 × 3.7 × 1.5
  Bevel: amount=0.08, segments=2
  shadeMode: auto
```

### Rules

1. **Never skip the `ask_user` step** for new graph creation. For **editing** existing graphs, skip if user only requested a specific change.
2. **Max 4 questions per `ask_user` call**. If more parameters need confirmation, batch them into one `choice` question with a text fallback.
3. **Only show parameters that exist in the graph**. If no BevelMesh, don't mention bevel parameters.
4. **Apply user adjustments** before writing the file. Re-validate parameter quality baselines after adjustments.

## Graph JSON contract

```json
{
  "version": "1.0",
  "nodes": [ { "id", "type", "position": { "x", "y" }, "data": { ... } } ],
  "edges": [ { "id", "source", "target", "sourceHandle", "targetHandle" } ]
}
```

| Rule | Detail |
|------|--------|
| `version` | Always `"1.0"` |
| `id` | Semantic snake_case role name (`body_profile_nose`, `fl_wheel_rim`); avoid bare `n1` and type-only ids (`create_box_1`) |
| `data.__nodeTitle` | **Required display name** in Unity GraphView (see [Node display names](#node-display-names-p0--unity-graphview)); not a manifest property |
| `data` | Manifest keys + optional `__nodeTitle`; use manifest defaults for omitted fields |
| `edges` | `sourceHandle` / `targetHandle` = manifest pin `id` (e.g. `backbone`, `profile`, `a`, `b`) |
| Pin compatibility | Output `pinType` must match input `pinType` (or `Output.in` = `Any`) |
| Terminator | Every runnable graph ends with `Output` at the **bottom** |

## Node display names (P0 — Unity GraphView)

Unity does **not** show `node.id` on the canvas. Each pill uses:

1. `data.__nodeTitle` if non-empty (serialized by `PcgManifestNodeView` / `CollectData`)
2. Else manifest `displayName` for that `type` (e.g. every `CreateBoxMesh` → **"Create Box Mesh"**)

So without `__nodeTitle`, ten boxes all read identically — the overlap/confusion in the screenshot.

### Naming rules

1. **Every node** in a new/edited graph gets a unique `__nodeTitle` derived from **what the part does in this model**, not the node type.
2. Prefer short Title Case English (≤ ~24 chars so the right-side label stays readable): `"Nose Profile"`, `"FL Wheel Rim"`, `"Deck Sweep"`, `"Merge Body"`.
3. `id` = snake_case of the same meaning (`nose_profile`, `fl_wheel_rim`). Keep `id` and `__nodeTitle` aligned.
4. When multiple nodes share a `type`, titles **must** differ by role / location / stage — never leave them on the default displayName.
5. Disambiguators (use in order): body part → side/position → stage (`bevel`, `xform`, `profile`, `path`).

| Context | ❌ Bad (all look the same) | ✅ Good |
|---------|---------------------------|---------|
| Sedan loft sections | `"Create Bezier Spline"` × N | `"Nose Profile"`, `"Cabin Profile"`, `"Tail Profile"` |
| Wheels | `"Create Box Mesh"` × 4 | `"FL Hub"`, `"FR Hub"`, `"RL Hub"`, `"RR Hub"` |
| Bridge | `"Sweep Along Spline"` | `"Deck Sweep"`, `"Rail Sweep"` |
| Math / utility | `"Math"` × N | `"Wheel Track X"`, `"Body Half Width"` |

### JSON shape

```json
{
  "id": "nose_profile",
  "type": "CreateBezierSpline",
  "position": { "x": -280, "y": 0 },
  "data": {
    "__nodeTitle": "Nose Profile",
    "mode": "catmullRom",
    "closed": false
  }
}
```

`__nodeTitle` is editor-only metadata; the cook path ignores unknown keys safely. Do **not** invent a top-level `"title"` field — Unity only reads `data.__nodeTitle`.

## Layout: Houdini top → bottom (required)

Unity GraphView uses **vertical pins** (inputs top, outputs bottom). Position nodes so wires run downward.

```
        [source A]     [source B]     ← same row, branch on X
              \           /
            [operator]
                  |
            [downstream]
                  |
              [Output]              ← bottom center
```

### Unity geometry (why spacing must be large)

Houdini-style pills are narrow; the **name is a right-side overlay**, not inside the pill:

| Fact | Value | Source |
|------|-------|--------|
| Node body width | ≈ `61` (`NodeWidth = 92 × 2/3`) | `PcgGraphNodeBase` |
| Title starts | `node.x + body + 14` | overlay placement |
| Title width | `min 120` … `max 220` | `m_RightTitleLabel` |

**Same-row footprint** ≈ body + gap + title ≈ **300–295+** px. If `|Δx|` between siblings is only 60–220, titles paint over the next pill — the exact failure in dense parallel rows.

### Constants

| Constant | Value | Use |
|----------|-------|-----|
| `ROW_STEP_Y` | `160` | Vertical gap between rows |
| `SPINE_X` | `200` | Main chain X |
| `COL_STEP_X` | `320` | **Minimum** `|Δx|` between any two nodes on the same row (P0) |
| `BRANCH_X` | `±320` | Two parallel inputs: `SPINE_X ± COL_STEP_X` (was ±220 — too tight for titles) |
| Start Y | `0` | Top row |

### Placement algorithm (P0 — minimize wire crossings)

Spacing alone is not enough. **Do not** pack every topo-level into one global grid row — that mixes unrelated subsystems and creates spaghetti (door box on the right wired diagonally to door place on the left).

#### Preferred: subsystem lanes

For multi-part assemblies (vehicle, bridge+details, canister+hose+…):

1. Find the final `MergeMesh` / `Output` fan-in. Each **direct input** is a **lane tip** (body, wheels, doors, glass, …).
2. Assign every ancestor of a tip to that tip’s lane (exclusive; shared nodes stay on the primary consumer’s lane).
3. Lay lanes **left → right** in reading order; within a lane, data flows **top → bottom**.
4. Single-chain nodes in a lane share one `lane_x` (wires stay nearly vertical).
5. True siblings that feed one consumer (e.g. 7 loft profiles → `body_loft`) sit on one row **inside that lane only**, centered on `lane_x`, `|Δx| ≥ COL_STEP_X`.
6. Parallel sub-chains inside a lane (tire / rim / spokes → `wheel_origin`) get **sub-lanes** `lane_x ± k*COL_STEP_X`, then merge on the lane spine.
7. Final `MergeMesh` + `Output` sit on the bottom row at the **center of all lane tips**.
8. After placing, prefer parent and child with similar `x` (barycenter of parents for multi-parent nodes). Long diagonal edges across many lanes = failed layout — re-lane.

```text
  [body profiles…]     [tire][rim][spoke]    [door boxes]
         |                  \  |  /                |
     Body Loft            Wheel Origin         Doors Left
         |                     |                   |
        …                     …                   …
         \                     |                  /
                    Vehicle Assembly → Output
```

#### Simple graphs (one spine)

1. Topological sort (sources first).
2. Row `r`: `y = r * ROW_STEP_Y`; single chain at `x = SPINE_X`.
3. N siblings → one consumer: siblings on row `r` centered on spine with `COL_STEP_X`; consumer on `r+1` at spine.
4. Overflow: if N > 8 **siblings of the same consumer**, split sibling rows — never shrink `COL_STEP_X`, and never mix unrelated nodes into that row.

#### Crossing check (must-fix on new / relayout graphs)

Approximate geometric crossings for straight parent→child segments. Target: **near-zero long diagonals**. Treat “many edges spanning ≥ 2 lane widths” as a layout failure even if titles do not overlap.

### Do NOT (legacy anti-patterns)

```text
❌  [A] ——→ [B] ——→ [C] ——→ [Out]     (same y, left-to-right pipeline)
❌  same row, x += 60 / 100 / 220       (titles overlap neighbour pills)
❌  global topo-row grid of all nodes   (unrelated parts share a row → wire spaghetti)
❌  omit __nodeTitle when type repeats  (all labels identical)
✅  one vertical lane per assembly part; siblings only beside their consumer
✅  same row only for true siblings, |Δx| ≥ COL_STEP_X, unique __nodeTitle
```

When **editing** old examples that use horizontal layout, dense X packing, or spaghetti lanes, **rewrite positions** to lane top-down + `COL_STEP_X`; keep topology and geometry `data` unchanged, and add `__nodeTitle` for every node.

## Common pin wiring

| Pattern | sourceHandle → targetHandle |
|---------|----------------------------|
| Mesh chain | `out` → `in` |
| Point chain | `out` → `in` |
| SweepAlongSpline | spline `out` → `backbone`; profile `out` → `profile` |
| InstanceAlongSpline | spline `out` → `spline`; mesh `out` → `mesh` |
| RevolveMesh | profile `out` → `profile` |
| MergeMesh | `out` → `in` (variadic — all inputs use `in`) |
| Output | `out` → `in` |
| Assembly Bevel | part → `BevelMesh` → `MergeMesh` (not Merge → Bevel) |

## Workflow

```
- [ ] 0. Shape analysis (mandatory with reference image — record in reply)
- [ ] 0b. Physical-world sizing (mandatory: pick real-world target size + per-part consistency; flag if user wants stylized/non-physical scale — see [Physical-world sizing](#physical-world-sizing-p0--default-unless-user-overrides))
- [ ] 1. Strategy rules check (mandatory: `rule_search domain=pcg` → `pcg/assembly-bevel` + matching type under `Rules/pcg/`)
- [ ] 2. Node selection (follow Node Selection Guide + strategy file; justify any deviation)
- [ ] 2b. Bevel placement check — no MergeMesh→BevelMesh on multi-part assemblies; bevel per part then merge
- [ ] 3. Parameter quality check (follow Parameter Quality Baselines; per-part bevel amounts)
- [ ] 4. List nodes + edges from manifest
- [ ] 5. Assign semantic ids, unique `__nodeTitle`, and **lane-based** top-down positions (`COL_STEP_X ≥ 320`; keep part chains vertical)
- [ ] 5b. Overlap + crossing check — no same-row `|Δx| < 320`; no duplicate `__nodeTitle`; no global topo-grid spaghetti across subsystems
- [ ] 6. Fill `data` from manifest defaults + user params + `__nodeTitle`
- [ ] 7. Parameter review with user (ask_user — present Tier 1 + relevant Tier 2; adjust if requested)
- [ ] 7b. Save directory confirmation (ask_user — glob for .pcg / PCGDemo / Assets dirs, default to detected)
- [ ] 8. Write file to confirmed directory (see File placement)
- [ ] 9. Run validation script (treat Merge→Bevel assembly warnings as must-fix on new graphs)
- [ ] 10. If graph is a regression fixture, wire into pcg-core ctest
```

### File placement (mandatory `ask_user`)

**Never assume the save directory.** Before writing, auto-detect candidate directories and confirm with the user.

#### Auto-detection order

1. **Glob for existing `.pcg` files** — `glob(pattern="**/*.pcg")` in the workspace. The directory containing the most `.pcg` files is the primary candidate.
2. **Glob for Unity PCG directories** — `glob(pattern="**/Assets/PCG*/**")` or `glob(pattern="**/Assets/*PCG*/**")`. Matches `PCGDemo`, `PcgPlugin`, etc.
3. **Glob for Unity `Assets/` folders** — `glob(pattern="**/Assets")` to locate Unity project roots.
4. If step 1–2 finds results → propose the deepest matched directory as default.
5. If nothing found → fall back to `examples/` relative to the PCG repo root, or the workspace root.

#### `ask_user` pattern

Use a **single `ask_user` call** with the detected directory as the default option:

```
type: "choice"
header: "保存目录"
question: "检测到以下目录，选择 .pcg 文件保存位置：\n\n{detected path summary}"
options:
  - label: "{detected default path}"
    description: "自动检测到的 PCG 目录"
  - label: "examples/"
    description: "PCG 仓库 examples/ 目录（仅仓库 demo 用）"
  - label: "自定义路径"
    description: "手动输入保存目录"
```

**If "自定义路径"**: follow up with `type: "text"` to get the path from the user.

#### Save targets

| Purpose | Path (after confirmation) |
|---------|---------------------------|
| Canonical demo | `examples/<name>.pcg` |
| Unity import | `{confirmed_dir}/<name>.pcg` (default: `Unity/Assets/PCGDemo/`) |
| Schema sample | `schema/example.pcg` (minimal only) |

Keep `examples/` and Unity copies **identical** for paired demos.

### Special fields

- **CreateSpline `controlPoints`**: JSON **string** with escaped quotes:
  `"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":10,\"y\":0,\"z\":0}]"`
- **GetMeshData / GetSplineData**: use `bindingKey` + `source: "Binding"`; never serialize scene object refs in `.pcg`
- **Numbers in manifest** (not `.pcg`): avoid `1e-8` in `node-manifest.json` — breaks Unity `PcgMiniJson`; use `0.00000001`

## Validation (required)

From repo root:

```bash
python3 ~/.codely-cli/skills/pcg-graph-authoring/scripts/validate_pcg.py examples/your-graph.pcg
```

Fix all errors; treat layout warnings, same-row spacing warnings, and duplicate/missing `__nodeTitle` warnings on new graphs as must-fix.

## Quick templates

See [examples.md](examples.md) for full top-down graphs: linear mesh chain, scatter, spline bridge.

## Reference files

| File | Role |
|------|------|
| `schema/node-manifest.json` | Node + pin definitions |
| `schema/graph-schema.json` | v1 envelope (legacy enum; manifest is authoritative for types) |
| `pcg-core/tests/test_phase45_spline.cpp` | Spline graph execution tests |
| `Unity/Assets/PcgPlugin/Editor/Graph/PcgConnectionValidator.cs` | Pin type rules in editor |
