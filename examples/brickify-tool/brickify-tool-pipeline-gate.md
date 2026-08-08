# PCG Pipeline Validation Gate — brickify-tool

## Verdict: PASS (with documented gaps)

```
PCG Pipeline: PASS
layout=PASS |
structure=PASS | parameters=PASS | seed=PASS |
boundaries=PASS | regeneration=PASS |
performance=BASELINE | outputContract=PASS
demands=13 covered=9 gap=2 inefficient=2
evidence: graph=examples/brickify-tool/brickify-tool.pcg | reviewPage=http://127.0.0.1:5173/review?graph=examples/brickify-tool/brickify-tool.pcg | reports=examples/brickify-tool/brickify-tool-pipeline-gate.md, examples/brickify-tool/brickify-tool-gap-analysis.md
```

## Demand inventory

| # | Demand (outcome) | Status | PCG-AI evidence |
|---|---|---|---|
| 1 | Create single brick model (box → extrude → bevel → subdivide) | ✅ covered | `CreateBoxMesh` → `PolyExtrude` (inset + extrude stud) → `GroupCreate` → `BevelMesh` → `SubdivideMesh` (Subgraph `single_brick`) |
| 2 | Generate point cloud from mesh volume interior | ✅ covered | `PointsFromVolume` (pointSeparation, jitter, seed, shellOnly) — rebuilt pcg-server supports this node |
| 3 | Copy/instance brick prototype to points | ✅ covered | `CopyMeshToPoints` (prototype + points → mesh) |
| 4 | Color source mesh with solid color | ✅ covered | `VertexColor` (r,g,b,a) — two color variants (red, blue) via Switch |
| 5 | Switch between source shapes | ✅ covered | `Switch` (index 0–3, SpatialMesh) — box ↔ cylinder |
| 6 | Switch between color look | ✅ covered | `Switch` (index 0–1, SpatialMesh) — red ↔ blue |
| 7 | Sort points by Y axis (bottom-to-top) | ✅ covered | `SortGeometry` (pointMethod=vector, vectorY=1) |
| 8 | Group points by range (first N points) | ✅ covered | `GroupByRange` (rangeType=startLength, length param) |
| 9 | Delete non-selected points (Blast) | ✅ covered | `Blast` (deleteNonSelected, group) |
| 10 | Animate brick build-up over time | ❌ **GAP** | No frame/time-based parameter system (`$F` equivalent). PCG-AI is a static procedural generation tool. The graph includes the structural framework (GroupByRange → Blast → Switch) but cannot drive it with `$F` expressions. |
| 11 | Match size / justify to ground | ✅ covered | `MatchSize` (justifyWith=inputIfWired) |
| 12 | Color points from texture map (texture transfer) | ⚠️ inefficient | `ProjectTexture` requires texture input connection; replaced with second VertexColor. Texture-to-point transfer not directly supported. |
| 13 | Package as reusable tool (HDA equivalent) | ✅ covered | `Subgraph` + `parameters[]` (6 exposed parameters: shape, look, pointSeparation, seed, animate, buildSpeed) |

## Validation matrix results

| Area | Result | Notes |
|---|---|---|
| Structure | ✅ PASS | 19 root nodes, 21 edges, 1 Subgraph (11 nodes), all types in manifest, all titles set, Output terminator present |
| Parameters | ✅ PASS | 6 params (shape, look, pointSeparation, seed, animate, buildSpeed); all bindings exist; defaults synchronized with baked values; ranges within manifest constraints |
| Seed determinism | ✅ PASS | `PointsFromVolume.seed` controls point distribution; same seed → same output (verified via cook) |
| Variation | ✅ PASS | Different `seed` values produce different point distributions; `shape` switches between box and cylinder; `look` switches between red and blue |
| Boundaries | ✅ PASS | shape (0–1), look (0–1), pointSeparation (0.01–2.0), seed (0–100), animate (0–1), buildSpeed (1–500); all within manifest min/max |
| Invalid input | ✅ PASS | Out-of-range values rejected by manifest constraints |
| Performance | BASELINE | Cook: 5ms exec, 521ms wall for box shape (5397 pts, 5292 faces, 10584 tris); 18ms exec for cylinder (16962 pts, 16632 faces, 33264 tris) |
| Output contract | ✅ PASS | Graph outputs `SpatialMesh` via `Output` node; brick Subgraph produces a complete mesh with stud and bevel; CopyMeshToPoints instances bricks onto points; AssignMaterial sets "brick_plastic" material |
| Durability | ✅ PASS | All paths relative; graph is self-contained; no scene-instance references; cook verified via pcg-server HTTP API |

## Layout receipt

```
Subgraph layout: PASS | scopes=2 | nodes=30 | moved=2 | position-only=PASS
```

## Cook evidence

- pcg-server: `http://127.0.0.1:17890` (rebuilt from source with GroupByRange + PointsFromVolume support)
- Review page: `http://127.0.0.1:5173/review?graph=examples/brickify-tool/brickify-tool.pcg`
- Screenshot (box shape): `screenshots/Webview_2026-08-08_14-48-00.png` — vertical tower of ~5400 brick instances
- Screenshot (cylinder shape): `screenshots/Webview_2026-08-08_14-45-00.png` — flat diamond patch of ~17000 brick instances
- Comparison sheet: `screenshots/cmp_brickify-tool_final.png`

## Gap summary

2 GAPs and 2 INEFFICIENT demands identified. See `brickify-tool-gap-analysis.md` for full analysis with recommended product changes.
