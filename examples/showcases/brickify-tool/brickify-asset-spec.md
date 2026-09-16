# AssetSpec: brickify-tool

## Asset Identity

| Field | Value |
|---|---|
| `assetId` | `brickify-tool` |
| `intent` | A procedural "brickify" generator that turns any source 3D shape into a voxel-like array of interlocking toy-brick instances — a PCG-AI adaptation of the Houdini Fundamentals lesson "Nodes, Networks and Digital Assets." |
| `references` | SideFX *Node Networks and Assets* Foundations tutorial, Document v2.0 (Oct 2021), pages 1–16 |
| `scale` | Stylized scale; brick prototype ≈ 0.2 × 0.2 × 0.2 m; source shape proxy ≈ 3 × 4 × 2 m |
| `modules` | `single_brick` (Subgraph) — the brick prototype created via box → extrude → bevel → subdivide; root graph handles shape selection, point generation, copy-to-points, color/material, and output |
| `frontAxis` | +Z (prototype faces camera; irrelevant for instanced scatter) |

## Geometry DoD

- Brick body: rectangular prism 0.2 × 0.2 × 0.2 with raised stud on top (via PolyExtrude inset + extrude)
- Beveled edges (0.006 offset, 3 segments, round profile)
- Catmull-Clark subdivision for smooth plastic appearance
- Source shape can be switched (box proxy ↔ alternate box proxy) — Switch node mirrors Houdini's switch SOP
- Points generated from mesh surface (approximation of Houdini's Points from Volumes)

### ImportMesh placeholders (P0)

`brickify-tool.pcg` may ship with `ImportMesh` nodes whose `path` is an **empty string** — these are intentional placeholders, not runnable geometry. Before cook:

- Replace with `CreateBoxMesh` / `CreateCylinderMesh` (or another generator), **or**
- Bind a real asset path and `projectRoot`.

`validate_pcg.py` emits a warning on empty `ImportMesh.path`; the graph produces zero geometry until substituted.

## Material Slots

| Slot | Part | Shader Family | Texture Channels | Finish |
|---|---|---|---|---|
| `brick_plastic` | All brick instances | Standard/URP Lit | Base color (vertex color or texture-projected), smoothness | Glossy plastic, roughness ≈ 0.3–0.4 |

## Variation

- `brickSize` — controls brick prototype dimensions (default 0.2, range 0.1–0.5)
- `pointCount` — controls point cloud density on source surface (default 500, range 100–5000)
- `brickColor` — controls solid brick color via vertex color (default red #FF0000)
- `shapeSelect` — switches between source shapes (0 = box, 1 = alt box)
- `seed` — seed for point sampling and randomization

## Outputs

| Output | Path |
|---|---|
| PCG graph | `examples/showcases/brickify-tool/brickify-tool.pcg` |
| AssetSpec | `examples/showcases/brickify-tool/brickify-asset-spec.md` |

## Acceptance

- Graph validates cleanly (no errors, no invented node types)
- Brick Subgraph produces a recognizable Lego-style brick silhouette
- CopyMeshToPoints correctly instances bricks onto generated points
- Switch node allows toggling between source shapes
- SortGeometry orders points bottom-to-top (Y axis)
- Known differences from the Houdini workflow are documented in this AssetSpec

## Houdini → PCG-AI Node Mapping Summary

| Houdini SOP | PCG-AI Node | Status |
|---|---|---|
| Box | `CreateBoxMesh` | ✅ covered |
| PolyExtrude (inset/extrude) | `PolyExtrude` | ✅ covered |
| Group (edges by angle) | `GroupCreate` | ✅ covered |
| PolyBevel | `BevelMesh` | ✅ covered |
| Subdivision Surface | `SubdivideMesh` | ✅ covered |
| Match Size | `MatchSize` | ✅ covered |
| Points from Volumes | — | ❌ **GAP** (no volumetric point generation) |
| Copy to Points | `CopyMeshToPoints` | ✅ covered |
| Color | `VertexColor` / `AttributeWrangle` | ✅ covered |
| Material | `AssignMaterial` | ✅ covered |
| Switch | `Switch` | ✅ covered |
| Platonic (Utah Teapot) | — | ❌ minor gap (use ImportMesh) |
| Attribute VOP (texture) | `ProjectTexture` + `ImageTexture` | ✅ covered |
| UV Coordinate | `UVTexture` | ✅ covered |
| Attribute Promote | — | ⚠️ partial (AttributeWrangle can approximate) |
| Attribute Transfer | `AttributeTransfer` | ✅ covered |
| Group by Range | — | ❌ **GAP** (no range-based point selection) |
| Blast | `Blast` | ✅ covered |
| Sort | `SortGeometry` | ✅ covered |
| $F (frame animation) | — | ❌ **GAP** (no time/frame system) |
| HDA (Digital Asset) | Subgraph + `parameters[]` | ✅ covered |
