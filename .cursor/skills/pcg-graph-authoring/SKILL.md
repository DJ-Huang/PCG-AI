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
2. Skim a similar example under `examples/` (see [examples.md](examples.md)).
3. Never invent node types or pin ids; copy from manifest.

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
- [ ] 1. Clarify pipeline (mesh / scatter / spline bridge / points)
- [ ] 2. List nodes + edges from manifest
- [ ] 3. Assign semantic ids and top-down positions
- [ ] 4. Fill `data` from manifest defaults + user params
- [ ] 5. Write file (see paths below)
- [ ] 6. Run validation script
- [ ] 7. If graph is a regression fixture, wire into pcg-core ctest
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
python3 .cursor/skills/pcg-graph-authoring/scripts/validate_pcg.py examples/your-graph.pcg
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
