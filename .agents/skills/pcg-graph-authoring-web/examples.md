# PCG Graph Examples — Houdini Top-Down Layout

**Purpose:** curated **wiring / layout** snippets for the skill. These are **not** a substitute for `node-manifest.json`, `.pcg-ai/rules/` strategy docs, or project **Golden Graphs** (`.pcg-ai/golden-graphs/`).

**High-quality full graphs:** project `.pcg-ai/golden-graphs/` only. **Do not** open repo `examples/*.pcg` / Unity demos to model a new asset. Prefer this markdown for pin chains; prefer project rules + Golden Graphs (`.pcg-ai/`) for modeling decisions.

All positions use: `SPINE_X=200`, `ROW_STEP_Y=160`, `COL_STEP_X=320` (same-row `|Δx| ≥ 320`).
Every node should set `data.__nodeTitle` (Unity shows that, not `id`).

## Linear mesh chain

`CreateBoxMesh → SubdivideMesh → BevelMesh → Output`

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "box", "type": "CreateBoxMesh", "position": { "x": 200, "y": 0 },
      "data": { "__nodeTitle": "Body Box", "width": 3.0, "height": 1.5, "depth": 2.0 } },
    { "id": "subdiv", "type": "SubdivideMesh", "position": { "x": 200, "y": 160 },
      "data": { "__nodeTitle": "Body Subdiv", "levels": 1 } },
    { "id": "bevel", "type": "BevelMesh", "position": { "x": 200, "y": 320 },
      "data": { "__nodeTitle": "Body Bevel", "method": "edge", "amount": 0.08, "segments": 3 } },
    { "id": "out", "type": "Output", "position": { "x": 200, "y": 480 },
      "data": { "__nodeTitle": "Output", "label": "Mesh" } }
  ],
  "edges": [
    { "id": "e1", "source": "box", "target": "subdiv", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e2", "source": "subdiv", "target": "bevel", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e3", "source": "bevel", "target": "out", "sourceHandle": "out", "targetHandle": "in" }
  ]
}
```

## Mesh surface scatter

`CreateBoxMesh → SubdivideMesh → SampleMeshSurface → StaticMeshSpawner → Output`

Row 0–4 on spine `x=200`, `y = 0, 160, 320, 480, 640`.

## Spline bridge (Sweep + InstanceAlongSpline)

Topology sketch (**positions are top-down**; do not open `bridge-demo.pcg` for strategy):

```text
row 0:  path (x=520)    deck_profile (x=-120)   ← |Δx| = 640 ≥ COL_STEP_X
row 1:  pier_proto (x=-120)     deck / SweepAlongSpline (x=520)
row 2:  piers / InstanceAlongSpline (x=520)
row 3:  merge (x=200)
row 4:  out (x=200)
```

```json
{
  "version": "1.0",
  "nodes": [
    {
      "id": "path", "type": "CreateSpline", "position": { "x": 520, "y": 0 },
      "data": {
        "__nodeTitle": "Deck Path",
        "mode": "catmullRom",
        "controlPoints": "[{\"x\":0,\"y\":0,\"z\":0},{\"x\":12,\"y\":3,\"z\":8},{\"x\":28,\"y\":0,\"z\":12},{\"x\":40,\"y\":0,\"z\":0}]",
        "subdivisions": 8, "editPlane": "none"
      }
    },
    {
      "id": "deck_profile", "type": "CreateSpline", "position": { "x": -120, "y": 0 },
      "data": {
        "__nodeTitle": "Deck Profile",
        "mode": "polyline", "closed": true, "subdivisions": 1,
        "controlPoints": "[{\"x\":-3,\"y\":-0.2,\"z\":0},{\"x\":3,\"y\":-0.2,\"z\":0},{\"x\":3,\"y\":0.2,\"z\":0},{\"x\":-3,\"y\":0.2,\"z\":0}]",
        "editPlane": "xy", "sceneOffsetX": 0, "sceneOffsetY": -6, "sceneOffsetZ": 0
      }
    },
    {
      "id": "pier_proto", "type": "CreateBoxMesh", "position": { "x": -120, "y": 160 },
      "data": { "__nodeTitle": "Pier Prototype", "width": 1.2, "height": 7.0, "depth": 1.2 }
    },
    {
      "id": "deck", "type": "SweepAlongSpline", "position": { "x": 520, "y": 160 },
      "data": {
        "__nodeTitle": "Deck Sweep",
        "surfaceShape": "crossSection", "sampleSpacing": 0.8,
        "capStart": false, "capEnd": false,
        "upX": 0, "upY": 1, "upZ": 0, "twist": 0,
        "scaleStart": 1.0, "scaleEnd": 1.0, "profilePlane": "xy"
      }
    },
    {
      "id": "piers", "type": "InstanceAlongSpline", "position": { "x": 520, "y": 320 },
      "data": {
        "__nodeTitle": "Pier Instances",
        "spacing": 10.0, "offset": 5.0, "includeEnd": false,
        "alignToTangent": true, "scale": 1.0
      }
    },
    {
      "id": "merge", "type": "MergeMesh", "position": { "x": 200, "y": 480 },
      "data": { "__nodeTitle": "Merge Bridge" }
    },
    {
      "id": "out", "type": "Output", "position": { "x": 200, "y": 640 },
      "data": { "__nodeTitle": "Output", "label": "Bridge" }
    }
  ],
  "edges": [
    { "id": "e1", "source": "path", "target": "deck", "sourceHandle": "out", "targetHandle": "backbone" },
    { "id": "e1b", "source": "deck_profile", "target": "deck", "sourceHandle": "out", "targetHandle": "profile" },
    { "id": "e2", "source": "path", "target": "piers", "sourceHandle": "out", "targetHandle": "spline" },
    { "id": "e3", "source": "pier_proto", "target": "piers", "sourceHandle": "out", "targetHandle": "mesh" },
    { "id": "e4", "source": "deck", "target": "merge", "sourceHandle": "out", "targetHandle": "a" },
    { "id": "e5", "source": "piers", "target": "merge", "sourceHandle": "out", "targetHandle": "b" },
    { "id": "e6", "source": "merge", "target": "out", "sourceHandle": "out", "targetHandle": "in" }
  ]
}
```

## Semantic mapping (Houdini ↔ PCG-AI)

| Houdini | PCG-AI node |
|---------|-------------|
| Sweep SOP (curve × curve) | `SweepAlongSpline` + `CreateSpline` profile |
| Copy to Curves | `InstanceAlongSpline` |
| Merge | `MergeMesh` |
| Output / ROP display | `Output` |
| Subnet | `Subgraph` + `subgraphs[]` definition |

## Subgraph module (move mesh)

Minimal **I/O wrapper** (tutorial shape). Prefer **part-sized** modules in real graphs — do not wrap 2–3 nodes just to hit a count.

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "box", "type": "CreateBoxMesh", "position": { "x": 200, "y": 0 },
      "data": { "__nodeTitle": "Source Box", "sizeX": 2, "sizeY": 2, "sizeZ": 2 } },
    { "id": "move_instance", "type": "Subgraph", "position": { "x": 200, "y": 160 },
      "data": { "__nodeTitle": "Move Mesh", "subgraphId": "move_mesh" } },
    { "id": "out", "type": "Output", "position": { "x": 200, "y": 320 },
      "data": { "__nodeTitle": "Output", "label": "Mesh" } }
  ],
  "edges": [
    { "id": "e1", "source": "box", "target": "move_instance",
      "sourceHandle": "out", "targetHandle": "geometry" },
    { "id": "e2", "source": "move_instance", "target": "out",
      "sourceHandle": "geometry", "targetHandle": "in" }
  ],
  "subgraphs": [
    {
      "id": "move_mesh",
      "name": "Move Mesh",
      "inputs": [{ "id": "geometry", "name": "Geometry", "pinType": "Mesh" }],
      "outputs": [{ "id": "geometry", "name": "Geometry", "pinType": "Mesh" }],
      "nodes": [
        { "id": "input", "type": "SubgraphInput", "position": { "x": 200, "y": 0 },
          "data": { "__nodeTitle": "In Geometry" } },
        { "id": "transform", "type": "TransformMesh", "position": { "x": 200, "y": 160 },
          "data": { "__nodeTitle": "Translate X", "translateX": 3, "translateY": 0, "translateZ": 0 } },
        { "id": "output", "type": "SubgraphOutput", "position": { "x": 200, "y": 320 },
          "data": { "__nodeTitle": "Out Geometry" } }
      ],
      "edges": [
        { "id": "ie1", "source": "input", "target": "transform",
          "sourceHandle": "geometry", "targetHandle": "in" },
        { "id": "ie2", "source": "transform", "target": "output",
          "sourceHandle": "out", "targetHandle": "geometry" }
      ]
    }
  ]
}
```
