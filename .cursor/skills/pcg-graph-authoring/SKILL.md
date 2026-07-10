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
2. **[Mandatory] Read the PCG modeling strategy directory**: open `/Users/djhuang/DJKnowledges/PCG AI Rule/README.md` and find the model type that matches your reference image. Open the corresponding `.md` file for per-part node selection, topology patterns, and parameter references. If no matching type exists, derive from shape analysis alone and note the gap.
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
| Revolve body | `SweepAlongSpline` (circle) or future `RevolveMesh` | `CreateBoxMesh` ❌ |
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

### Strategy directory consultation (mandatory)

**Path**: `/Users/djhuang/DJKnowledges/PCG AI Rule/`

1. Open `README.md` — scan the index table for a matching model type.
2. Open the corresponding `.md` file (e.g. `vehicle.md`, `bridge.md`) — use its per-part node mapping and topology pattern as a **reference**, not a copy.
3. Always validate the strategy against the current reference image. If the reference image differs from the strategy, adapt the node selection accordingly.
4. If no matching model type exists, derive purely from shape analysis. After completing the graph, consider adding a new strategy file to the directory.

**Do not** blindly copy an existing `examples/*.pcg` strategy — examples may contain anti-patterns (marked ⚠️ in the strategy files).

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
| MergeMesh | `out` → `a` or `b` |
| Output | `out` → `in` |

## Workflow

```
- [ ] 0. Shape analysis (mandatory with reference image — record in reply)
- [ ] 1. Strategy directory check (mandatory: read /Users/djhuang/DJKnowledges/PCG AI Rule/README.md, open matching .md)
- [ ] 2. Node selection (follow Node Selection Guide + strategy file; justify any deviation)
- [ ] 3. Parameter quality check (follow Parameter Quality Baselines)
- [ ] 4. List nodes + edges from manifest
- [ ] 5. Assign semantic ids and top-down positions
- [ ] 6. Fill `data` from manifest defaults + user params
- [ ] 7. Write file (see paths below)
- [ ] 8. Run validation script
- [ ] 9. If graph is a regression fixture, wire into pcg-core ctest
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
