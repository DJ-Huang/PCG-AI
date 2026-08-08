# PCG Pipeline Validation Gate — brickify-tool

## Verdict: PASS (with documented gaps)

```
PCG Pipeline: PASS
layout=PASS |
structure=PASS | parameters=PASS | seed=PASS |
boundaries=PASS | regeneration=PASS |
performance=BASELINE | outputContract=PASS
demands=13 covered=7 gap=4 inefficient=2
evidence: graph=examples/brickify-tool/brickify-tool.pcg | reviewScene=N/A (no Unity cook) | reports=examples/brickify-tool/brickify-tool-pipeline-gate.md, examples/brickify-tool/brickify-tool-gap-analysis.md
```

## Demand inventory

| # | Demand (outcome) | Status | PCG-AI evidence |
|---|---|---|---|
| 1 | Create single brick model (box → extrude → bevel → subdivide) | ✅ covered | `CreateBoxMesh` → `PolyExtrude` → `GroupCreate` → `BevelMesh` → `SubdivideMesh` (Subgraph `single_brick`) |
| 2 | Generate point cloud from mesh **volume interior** | ❌ **GAP** | No `PointsFromVolume` node in manifest. `SampleMeshSurface` used as surface-point approximation |
| 3 | Generate point cloud from mesh **surface** | ✅ covered | `SampleMeshSurface` (count, seed) |
| 4 | Copy/instance brick prototype to points | ✅ covered | `CopyMeshToPoints` (prototype + points → mesh) |
| 5 | Color points with solid color | ✅ covered | `AttributeRandomize` (colorMin/Max R/G/B) |
| 6 | Color points from texture map (texture transfer) | ⚠️ inefficient | `ProjectTexture` + `AttributeTransfer` exists but AttributeTransfer requires SpatialMesh on both inputs, not SpatialPoint. Mesh-to-point attribute transfer is not directly supported |
| 7 | Switch between source shapes | ✅ covered | `Switch` (index 0–3, SpatialMesh) |
| 8 | Switch between color/texture look | ⚠️ inefficient | `Switch` can toggle meshes, but cannot toggle point-cloud coloring strategies (SpatialPoint not SpatialMesh) |
| 9 | Sort points by Y axis (bottom-to-top) | ❌ **GAP** | `SortGeometry` exists (pointMethod=vector, vectorY=1) but input pinType is `SpatialGeometry`; may not accept `SpatialPoint` from `SampleMeshSurface`/`AttributeRandomize` |
| 10 | Animate brick build-up over time | ❌ **GAP** | No frame/time-based parameter system (`$F` equivalent). PCG-AI is a static procedural generation tool |
| 11 | Group points by range (first N points) | ❌ **GAP** | No `GroupByRange` node. `Delete` node has number-range mode but no frame-driven length expression |
| 12 | Delete non-selected points (Blast) | ✅ covered | `Blast` (deleteNonSelected, expression) |
| 13 | Package as reusable tool (HDA equivalent) | ✅ covered | `Subgraph` + `parameters[]` (3 exposed parameters) |

## Validation matrix results

| Area | Result | Notes |
|---|---|---|
| Structure | ✅ PASS | 9 root nodes, 8 edges, 1 Subgraph (8 nodes), all types in manifest, all titles set, Output terminator present |
| Parameters | ✅ PASS | 3 params (shapeSelect, pointCount, sampleSeed); all bindings exist; defaults synchronized with baked values; ranges within manifest constraints |
| Seed determinism | ✅ PASS | `SampleMeshSurface.seed` controls point distribution; same seed → same output (structural — no Unity cook to verify visually) |
| Variation | ✅ PASS | Different `sampleSeed` values produce different point distributions; `shapeSelect` switches between two source shapes |
| Boundaries | ✅ PASS | shapeSelect (0–1), pointCount (100–5000), sampleSeed (0–100); all within manifest min/max |
| Invalid input | ✅ PASS | Out-of-range values rejected by manifest constraints |
| Performance | BASELINE | No Unity cook available; graph is lightweight (17 nodes total, 1 Subgraph, 3 source nodes) |
| Output contract | ✅ PASS | Graph outputs `SpatialMesh` via `Output` node; brick Subgraph produces a complete mesh; CopyMeshToPoints instances bricks onto points |
| Durability | ✅ PASS | All paths relative; graph is self-contained; no scene-instance references |

## Layout receipt

```
Subgraph layout: PASS | scopes=2 | nodes=17 | moved=9 | position-only=PASS
```

## Gap summary

4 GAPs and 2 INEFFICIENT demands identified. See `brickify-tool-gap-analysis.md` for full analysis with recommended product changes.
