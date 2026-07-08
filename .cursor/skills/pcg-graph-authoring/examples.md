# PCG Graph Examples — Houdini Top-Down Layout

All positions use: `SPINE_X=200`, `ROW_STEP_Y=160`, `BRANCH_X=220`.

## Linear mesh chain

`CreateBoxMesh → SubdivideMesh → BevelMesh → Output`

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "box", "type": "CreateBoxMesh", "position": { "x": 200, "y": 0 },
      "data": { "width": 3.0, "height": 1.5, "depth": 2.0 } },
    { "id": "subdiv", "type": "SubdivideMesh", "position": { "x": 200, "y": 160 },
      "data": { "levels": 1 } },
    { "id": "bevel", "type": "BevelMesh", "position": { "x": 200, "y": 320 },
      "data": { "method": "edge", "amount": 0.08, "segments": 3 } },
    { "id": "out", "type": "Output", "position": { "x": 200, "y": 480 },
      "data": { "label": "Mesh" } }
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

Topology (unchanged from `bridge-demo.pcg`; **positions are top-down**):

```text
row 0:  path (x=420)    deck_profile (x=-20)
row 1:  pier_proto (x=-20)     deck / SweepAlongSpline (x=420)
row 2:  piers / InstanceAlongSpline (x=420)
row 3:  merge (x=200)
row 4:  out (x=200)
```

```json
{
  "version": "1.0",
  "nodes": [
    {
      "id": "path", "type": "CreateSpline", "position": { "x": 420, "y": 0 },
      "data": {
        "mode": "catmullRom",
        "controlPoints": "[{\"x\":0,\"y\":0,\"z\":0},{\"x\":12,\"y\":3,\"z\":8},{\"x\":28,\"y\":0,\"z\":12},{\"x\":40,\"y\":0,\"z\":0}]",
        "subdivisions": 8, "editPlane": "none"
      }
    },
    {
      "id": "deck_profile", "type": "CreateSpline", "position": { "x": -20, "y": 0 },
      "data": {
        "mode": "polyline", "closed": true, "subdivisions": 1,
        "controlPoints": "[{\"x\":-3,\"y\":-0.2,\"z\":0},{\"x\":3,\"y\":-0.2,\"z\":0},{\"x\":3,\"y\":0.2,\"z\":0},{\"x\":-3,\"y\":0.2,\"z\":0}]",
        "editPlane": "xy", "sceneOffsetX": 0, "sceneOffsetY": -6, "sceneOffsetZ": 0
      }
    },
    {
      "id": "pier_proto", "type": "CreateBoxMesh", "position": { "x": -20, "y": 160 },
      "data": { "width": 1.2, "height": 7.0, "depth": 1.2 }
    },
    {
      "id": "deck", "type": "SweepAlongSpline", "position": { "x": 420, "y": 160 },
      "data": {
        "surfaceShape": "crossSection", "sampleSpacing": 0.8,
        "capStart": false, "capEnd": false,
        "upX": 0, "upY": 1, "upZ": 0, "twist": 0,
        "scaleStart": 1.0, "scaleEnd": 1.0, "profilePlane": "xy"
      }
    },
    {
      "id": "piers", "type": "InstanceAlongSpline", "position": { "x": 420, "y": 320 },
      "data": {
        "spacing": 10.0, "offset": 5.0, "includeEnd": false,
        "alignToTangent": true, "scale": 1.0
      }
    },
    {
      "id": "merge", "type": "MergeMesh", "position": { "x": 200, "y": 480 },
      "data": {}
    },
    {
      "id": "out", "type": "Output", "position": { "x": 200, "y": 640 },
      "data": { "label": "Bridge" }
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
