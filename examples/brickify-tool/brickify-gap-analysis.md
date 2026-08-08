# Brickify Tool — Houdini→PCG-AI Gap Analysis Report

**Date:** 2026-08-08  
**Source:** hfoundations_03_nodes_networks_assets.pdf (Houdini Fundamentals: Nodes, Networks & Digital Assets)  
**Target:** PCG-AI `brickify-tool.pcg`  
**Graph:** `examples/brickify-tool/brickify-tool.pcg` (20 root nodes, 11 subgraph nodes, 22 edges, 6 parameters)

---

## 1. Houdini Tutorial Overview

The tutorial creates a **Brickify** tool that converts any 3D shape into toy bricks (LEGO-like). The complete Houdini network has 7 parts:

| Part | Description |
|---|---|
| 1. Single Brick | Box → PolyExtrude×4 → Edit(Make Circle) → Group → PolyBevel → Subdivision |
| 2. Copy to Points | TestGeometry(RubberToy) → MatchSize → PointsFromVolume → CopyToPoints(Pack&Instance) |
| 3. Color & Switch | Color(red) → Material(Principled, UsePackedColor) → Switch(rubbertoy↔teapot) |
| 4. Texture Color | AttributeVOP(texture+UV) → AttributePromote(vertex→point) → AttributeTransfer(Cd) → Switch(texture_switch) |
| 5. Digital Asset | Wrap network into HDA with promoted parameters (Shape, Look, Color, TextureMap) |
| 6. Test Asset | Apply brickify to different geometry (Squab) |
| 7. Animation | GroupByRange(($F-1)*20) → Blast → Sort(AlongVector) → Switch(animation_switch) + BuildSpeed param |

---

## 2. Node Mapping Table

### Part 1: Single Brick Creation

| Houdini SOP | Parameters | PCG-AI Node | Status | Notes |
|---|---|---|---|---|
| `box` | Size: 0.2, Div: 3,2,3 | `CreateBoxMesh` (w:0.2, h:0.2, d:0.2) | ⚠️ Partial | No divisions/segments parameter. Houdini uses 3×2×3 divisions for stud topology. |
| `polyextrude` | Inset: 0.04 | `PolyExtrude` (inset: 0.04) | ✅ | |
| `edit` (Make Circle) | Rounds selected faces | ❌ **MISSING** | No Edit SOP / Make Circle. Cannot round faces into circles. |
| `polyextrude` | Distance: 0.05 | `PolyExtrude` (distance: 0.05) | ✅ | |
| `polyextrude` | Inset: 0.025 (bottom) | `PolyExtrude` (inset: 0.025) | ✅ | |
| `polyextrude` | Distance: -0.175 | `PolyExtrude` (distance: -0.175) | ✅ | |
| `group` | Edge angle 89-91 | `GroupCreate` (minEdgeAngle: 60) | ⚠️ Partial | Has `minEdgeAngle` but NO `maxEdgeAngle`. |
| `polybevel` | Offset: 0.006, Round, Div: 3 | `BevelMesh` (amount: 0.006, segments: 3) | ✅ | `profile: 0.5` for round shape. |
| Subdivision display | Shift+ | `SubdivideMesh` (levels: 1, catmullClark) | ✅ | |

### Part 2: Copy Bricks to Point Cloud

| Houdini SOP | PCG-AI Node | Status | Notes |
|---|---|---|---|
| `testgeometry_rubbertoy` | `ImportMesh` | ✅ | No built-in test geometry, but can import any mesh. |
| `matchsize` (Justify Y: Min) | `MatchSize` (justifyY: min) | ✅ | `justifyWith: inputIfWired` for self-reference. |
| `pointsfromvolume` (Pt Sep: 0.2) | `PointsFromVolume` (pointSeparation: 0.2) | ✅ | |
| `copytopoints` (Pack & Instance) | `CopyMeshToPoints` | ✅ | Instancing is default behavior. |
| `merge` (temporary) | `MergeMesh` | ✅ | Available but not needed in final graph. |

### Part 3: Color and Switch

| Houdini SOP | PCG-AI Node | Status | Notes |
|---|---|---|---|
| `color` (red, on points) | `VertexColor` + `AttributeRandomize` | ⚠️ Partial | `VertexColor` works on mesh, not points. `AttributeRandomize` can set per-point colors but only random, not solid. Workaround: `VertexColor` on mesh before `PointsFromVolume`, or `AttributeWrangle` VEX. |
| `material` / `principledshader` | `AssignMaterial` | ✅ | No "Use Packed Color" option. |
| `switch` (mesh) | `Switch` | ✅ | |
| `platonic` (Utah Teapot) | ❌ **MISSING** | No platonic/primitive shape generator. Requires `ImportMesh`. |

### Part 4: Texture Color

| Houdini SOP | PCG-AI Node | Status | Notes |
|---|---|---|---|
| `attributevop` (texture+UV) | `UVTexture` + `ProjectTexture` | ⚠️ Partial | No VOP network for inline texture sampling. `ProjectTexture` has no texture input pin. |
| `attribpromote` (vertex→point) | ❌ **MISSING** | No Attribute Promote node for class promotion. |
| `attributetransfer` (Cd) | `AttributeTransfer` | ✅ | |
| `switch` (texture_switch, on points) | ⚠️ **Workaround** | `Switch` only accepts `SpatialMesh`, not `SpatialPoint`. Restructured to switch at mesh level. |
| Texture map file | `ImageTexture` | ⚠️ Partial | Exists but `ProjectTexture` doesn't accept texture as input. |

### Part 5: Digital Asset

| Houdini Feature | PCG-AI Equivalent | Status | Notes |
|---|---|---|---|
| HDA creation | Subgraph + `parameters[]` | ✅ | |
| Parameter promotion | `parameters[]` with targetNode/targetProperty | ✅ | |
| Parameter menu (dropdown) | ❌ **MISSING** | Parameters are integer ranges. No menu items (label/value pairs). |
| Disable When / Hide When | ❌ **MISSING** | No conditional parameter visibility/enabling. |
| Lock/Unlock asset | N/A | PCG-AI graphs are always editable. |

### Part 7: Animation

| Houdini SOP | PCG-AI Node | Status | Notes |
|---|---|---|---|
| `grouprange` (Length: ($F-1)*20) | `GroupByRange` (length: 20) | ⚠️ Partial | `length` is static integer, not expression. No `$F` support. |
| `blast` (delete non-selected) | `Blast` (deleteNonSelected: true) | ✅ | |
| `sort` (Along Vector 0,1,0) | `SortGeometry` (pointMethod: vector, Y: 1) | ✅ | |
| `switch` (animation_switch) | `Switch` (on mesh, not points) | ⚠️ **Workaround** | Switch only on SpatialMesh. Two `CopyMeshToPoints` nodes needed. |
| Expression `$F` | ❌ **MISSING** | No time/frame expression support. |

---

## 3. Missing Nodes & Capabilities Summary

### Critical Gaps (prevent 1:1 replication)

| # | Missing Capability | Houdini SOP | Impact | Workaround |
|---|---|---|---|---|
| 1 | **Make Circle / Edit SOP** | `edit` | Cannot round selected faces into circles. Brick stud is square instead of round. | Use `CreateCylinderMesh` for stud + `MergeMesh` with box base. Changes topology. |
| 2 | **Platonic Solid Generator** | `platonic` | No built-in Utah Teapot, sphere, torus, etc. | Use `ImportMesh` to import external shapes. |
| 3 | **Attribute Promote** | `attribpromote` | Cannot promote attributes between classes (vertex→point). | Use `AttributeWrangle` with VEX, but limited. |
| 4 | **Point-type Switch** | `switch` (on points) | `Switch` only accepts `SpatialMesh`. Cannot switch between point cloud streams. | Restructure: switch at mesh level (before PointsFromVolume) or at final mesh level (after CopyMeshToPoints). |
| 5 | **Time/Frame Expressions** | `$F` | No frame-based animation. `GroupByRange.length` is static. | Expose `length` as a parameter and drive externally (e.g., from Unity timeline). |

### Moderate Gaps (affect fidelity but have workarounds)

| # | Missing Capability | Impact | Workaround |
|---|---|---|---|
| 6 | **Box Divisions/Segments** | `CreateBoxMesh` has no division parameter. Brick topology differs from Houdini. | Use `SubdivideMesh` after box creation, or accept different topology. |
| 7 | **maxEdgeAngle on GroupCreate** | Can only select edges above min angle, not within a range (89-91°). | Use lower `minEdgeAngle` (e.g., 60°) to capture similar edges. |
| 8 | **Parameter Menu/Dropdown UI** | Parameters are integer ranges, no labeled menu items. | Acceptable for now; UI shows integer slider. |
| 9 | **Parameter Conditional Visibility** | No "Disable When" / "Hide When". All parameters always visible. | Acceptable; user ignores irrelevant params. |
| 10 | **Inline Texture Sampling (VOP)** | No VOP network for sampling textures into attributes. | Use `UVTexture` + `ProjectTexture` for UV setup, `AssignMaterial` for texture application. |
| 11 | **Packed Color Material** | No "Use Packed Color" option on materials. | Per-instance color handled via `AttributeRandomize` or `VertexColor` on points. |
| 12 | **Solid Color on Points** | `AttributeRandomize` only does random colors, not solid. | Use `VertexColor` on mesh before `PointsFromVolume`, or `AttributeWrangle` VEX. |

---

## 4. PCG-AI Graph Implementation

The graph at `examples/brickify-tool/brickify-tool.pcg` implements:

- ✅ Brick prototype (Subgraph `single_brick`): Box → GroupCreate(top) → PolyExtrude(inset) → PolyExtrude(stud) → GroupCreate(bottom) → PolyExtrude(inset) → PolyExtrude(down) → GroupCreate(bevel) → BevelMesh → SubdivideMesh
- ✅ Source shape switching (ImportMesh × 2 → Switch)
- ✅ MatchSize (justify Y: min)
- ✅ Color/Texture switching (VertexColor vs UVTexture+ProjectTexture → Switch)
- ✅ PointsFromVolume (pointSeparation: 0.2)
- ✅ CopyMeshToPoints (brick instancing)
- ✅ Animation (SortGeometry → GroupByRange → Blast → CopyMeshToPoints → Switch)
- ✅ AssignMaterial + Output
- ✅ 6 exposed parameters (Shape, Look, PointSeparation, Seed, Animate, BuildSpeed)

### Layout & Validation

```
Subgraph layout: PASS | scopes=2 | nodes=31 | moved=12 | position-only=PASS
validate_pcg.py: OK (20 root nodes, 22 edges, 1 subgraph def(s), layout=vertical)
```

---

## 5. Recommendations

### High Priority (new nodes to add)

1. **`AttributePromote`** — Promote attributes between classes (vertex↔point↔primitive↔detail). Common Houdini pattern; needed for proper color transfer workflow.

2. **Point-type `Switch`** — Either make `Switch` accept `Any` pin type (currently SpatialMesh only), or add a `SwitchPoints` node for `SpatialPoint` streams. Critical for switching between point cloud processing paths.

3. **`CreateBoxMesh` divisions** — Add `widthSegments`, `heightSegments`, `depthSegments` parameters. Needed for proper brick topology (3×2×3 divisions for stud creation).

4. **`GroupCreate.maxEdgeAngle`** — Add `maxEdgeAngle` parameter to complement `minEdgeAngle` for precise edge selection by angle range.

### Medium Priority (capability extensions)

5. **Platonic/Primitive generator** — Add a `CreatePlatonicSolid` node with options (teapot, sphere, torus, etc.) or a more general `CreatePrimitiveMesh` node.

6. **Parameter menu items** — Add `menuItems` field to `parameters[]` schema for labeled dropdown options (e.g., `{ "0": "Rubber Toy", "1": "Custom Shape" }`).

7. **Parameter conditional visibility** — Add `disableWhen` / `hideWhen` expression support to `parameters[]`.

8. **Time/Frame expression** — Add support for time-based expressions (like `$F`) or a `Time` input node for procedural animation.

### Low Priority (nice-to-have)

9. **Make Circle / Edit SOP** — Interactive geometry editing is complex. Consider a `RoundFaces` node that converts selected faces to N-gons approximating circles.

10. **VOP network** — A visual shader-like network for attribute computation. `AttributeWrangle` with VEX is the current alternative.

11. **Packed Color material option** — Add a "useVertexColor" or "usePointColor" flag to `AssignMaterial` to pick up per-instance colors.
