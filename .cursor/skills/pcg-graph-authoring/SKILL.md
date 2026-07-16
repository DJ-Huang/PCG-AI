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
| **Overall scale** | All geometry nodes | Model max dimension (X/Y/Z range) + unit hint | Derived from shape analysis |
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
| `id` | Semantic snake_case (`deck`, `path`, `scatter`); avoid bare `n1` in new graphs |
| `data` | Only keys defined in manifest; use manifest defaults for omitted fields |
| `edges` | `sourceHandle` / `targetHandle` = manifest pin `id` (e.g. `backbone`, `profile`, `a`, `b`) |
| Pin compatibility | Output `pinType` must match input `pinType` (or `Output.in` = `Any`) |
| Terminator | Every runnable graph ends with `Output` at the **bottom** |

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

### Constants

| Constant | Value | Use |
|----------|-------|-----|
| `ROW_STEP_Y` | `160` | Vertical gap between rows |
| `SPINE_X` | `200` | Main chain X |
| `BRANCH_X` | `±220` | Parallel inputs left/right of spine |
| Start Y | `0` | Top row |

### Placement algorithm

1. Topological sort (sources first).
2. Assign **row index** `r` (0 = top); `y = r * ROW_STEP_Y`.
3. **Single chain**: all nodes at `x = SPINE_X`.
4. **Multiple inputs to one node**: place inputs on row `r`, consumer on row `r+1` at `SPINE_X`; spread inputs at `SPINE_X - BRANCH_X` and `SPINE_X + BRANCH_X` (or both on one side if >2).
5. **`Output`**: last row, `x = SPINE_X`.

### Do NOT (legacy anti-pattern)

```text
❌  [A] ——→ [B] ——→ [C] ——→ [Out]     (same y, x += 280)
✅  [A]
      ↓
     [B]
      ↓
     [C]
      ↓
    [Out]
```

When **editing** old examples that use horizontal layout, **rewrite positions** to top-down; keep topology and data unchanged.

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
- [ ] 1. Strategy rules check (mandatory: `rule_search domain=pcg` → `pcg/assembly-bevel` + matching type under `Rules/pcg/`)
- [ ] 2. Node selection (follow Node Selection Guide + strategy file; justify any deviation)
- [ ] 2b. Bevel placement check — no MergeMesh→BevelMesh on multi-part assemblies; bevel per part then merge
- [ ] 3. Parameter quality check (follow Parameter Quality Baselines; per-part bevel amounts)
- [ ] 4. List nodes + edges from manifest
- [ ] 5. Assign semantic ids and top-down positions
- [ ] 6. Fill `data` from manifest defaults + user params
- [ ] 7. Parameter review with user (ask_user — present Tier 1 + relevant Tier 2; adjust if requested)
- [ ] 8. Write file (see paths below)
- [ ] 9. Run validation script (treat Merge→Bevel assembly warnings as must-fix on new graphs)
- [ ] 10. If graph is a regression fixture, wire into pcg-core ctest
```

### File placement

| Purpose | Path |
|---------|------|
| Canonical demo | `examples/<name>.pcg` |
| Unity import | `Unity/Assets/PCGDemo/<name>.pcg` (copy or symlink; keep in sync) |
| Schema sample | `schema/example.pcg` (minimal only) |

Keep `examples/` and `Unity/Assets/PCGDemo/` copies **identical** for paired demos.

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

Fix all errors; treat layout warnings on new graphs as must-fix.

## Quick templates

See [examples.md](examples.md) for full top-down graphs: linear mesh chain, scatter, spline bridge.

## Reference files

| File | Role |
|------|------|
| `schema/node-manifest.json` | Node + pin definitions |
| `schema/graph-schema.json` | v1 envelope (legacy enum; manifest is authoritative for types) |
| `pcg-core/tests/test_phase45_spline.cpp` | Spline graph execution tests |
| `Unity/Assets/PcgPlugin/Editor/Graph/PcgConnectionValidator.cs` | Pin type rules in editor |
