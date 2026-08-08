# Missing-Node Analysis: Houdini Brickify → PCG-AI

**Source**: `hfoundations_03_nodes_networks_assets.pdf` (Houdini Fundamentals — Nodes, Networks and Digital Assets, SideFX v2.0 Oct 2021)
**Graph**: `examples/brickify-tool/brickify-tool.pcg`
**Date**: 2026-08-08

## Summary

| Severity | Count | Description |
|---|---|---|
| 🔴 CRITICAL | 1 | Core workflow impossible without this node |
| 🟡 MODERATE | 3 | Feature cannot be replicated; workaround is partial or missing |
| 🟢 MINOR | 3 | Feature can be approximated with existing nodes |

---

## 🔴 CRITICAL GAPS

### 1. PointsFromVolume — Volumetric Point Cloud Generation

**Houdini SOP**: `pointsfromvolume` (Part 2, Step 02)
**Purpose**: Generates a 3D grid of points filling the interior volume of a mesh, with controllable point separation. This is the **core operation** of the brickify tool — it voxelizes a shape into a grid of points where bricks will be placed.

**PCG-AI status**: ❌ No equivalent node exists.

**Available alternatives** (all insufficient):
| Node | Why insufficient |
|---|---|
| `SampleMeshSurface` | Samples points on the **surface** only, not the interior volume |
| `CreatePointGrid` | Creates a flat 2D grid (ZX plane), not a 3D volumetric fill |
| `SpawnPoints` | Random points in a sphere radius, not mesh-shaped |
| `SurfaceSampler` | Surface sampling with subdivisions, not interior |
| `HeightFieldScatter` | Heightfield-based, not mesh-volume-based |

**Impact**: The brickify tool's defining feature — turning a 3D shape into a solid array of bricks — cannot be replicated. Surface-only sampling produces a hollow shell of bricks, not a solid brickified object.

**Houdini approach**: The `pointsfromvolume` SOP uses a level-set / SDF approach to voxelize the mesh interior and place points on a regular grid inside the volume. Point separation controls grid spacing.

**Mature solutions**:
- **Unreal PCG**: `PCGVolumeSampler` element samples points within a volume
- **Blender Geometry Nodes**: `Distribute Points in Volume` node
- **Open-source**: SDF voxelization (OpenVDB, `mesh_to_volume`)

**Recommended product change**: Add a `PointsFromVolume` (or `VoxelPoints`) node with properties:
- `pointSeparation` (float, grid spacing)
- `meshInput` (SpatialMesh input)
- `output` (SpatialPoint output)
- Optional: `jitter` (random offset per point), `shellOnly` (surface points only)

---

## 🟡 MODERATE GAPS

### 2. Frame/Time-Based Animation ($F equivalent)

**Houdini SOP**: Expression system using `$F` (frame number) in parameters (Part 7, Steps 01–03)
**Purpose**: Drives the `Group by Range` Length parameter with `($F-1)*20`, creating a growing selection of points over time for the brick build-up animation.

**PCG-AI status**: ❌ No time/frame/animation system. PCG-AI generates static procedural assets.

**Impact**: The animated brick build-up reveal (Part 7) cannot be replicated. The Switch node can toggle between static states, but no temporal progression exists.

**Recommended product change**: This is a fundamental architectural gap. Two options:
1. **Expression parameters**: Allow `ch()` and frame/time variables in parameter expressions (Houdini-style)
2. **Animation bake node**: A node that takes a frame range and outputs a sequence of point subsets

### 3. GroupByRange — Sequential Point Range Selection

**Houdini SOP**: `grouprange` (Part 7, Step 01)
**Purpose**: Creates a group containing a sequential range of points (e.g., points 0 to N), where N can be driven by an expression. Used with `Delete Non Selected` on the Blast node to create the build-up animation.

**PCG-AI status**: ❌ No `GroupByRange` node. The `Delete` node has a "number range" mode (start/end, select of, offset), but it lacks expression-driven length and cannot create a named group for downstream use.

**Impact**: Cannot select "the first N points" in a sequential manner for partial reveal or progressive generation.

**Available approximations**:
| Node | Limitation |
|---|---|
| `Delete` (number range mode) | Static range only; no expression-driven length; no group output |
| `Blast` (expression) | Can use `@ptnum <= N` expression, but N is a static parameter, not frame-driven |
| `Split` (group) | Can split by existing group, but cannot create a range-based group |

**Recommended product change**: Add a `GroupByRange` node with properties:
- `groupName` (string, output group name)
- `rangeType` (enum: startLength, startEnd, everyN)
- `start` (integer)
- `length` (integer or expression)
- `selectOf` / `offset` (for every-N patterns)

### 4. SortGeometry Pin Type Mismatch

**Houdini SOP**: `sort` (Part 7, Step 03)
**Purpose**: Reorders points along a vector (0,1,0 = bottom to top) so the build-up animation starts from the ground.

**PCG-AI status**: ⚠️ `SortGeometry` exists with the correct functionality (`pointMethod=vector`, `pointVectorX/Y/Z`), but its input `pinType` is `SpatialGeometry` while the point cloud from `SampleMeshSurface` / `AttributeRandomize` is `SpatialPoint`. This may cause a pin type incompatibility.

**Impact**: If the pin types are incompatible, the sort cannot be applied to the point stream. The bricks would appear in arbitrary point-number order rather than bottom-to-top.

**Recommended product change**: Either:
1. Make `SortGeometry` accept `SpatialPoint` input (or `Any`)
2. Add a `SortPoints` node specifically for `SpatialPoint` type

---

## 🟢 MINOR GAPS

### 5. AttributePromote — Attribute Class Conversion

**Houdini SOP**: `attribpromote` (Part 4, Step 06)
**Purpose**: Promotes the `Cd` (color) attribute from vertex class to point class, so it can be transferred to the point cloud.

**PCG-AI status**: ⚠️ No direct `AttributePromote` node. `AttributeWrangle` can approximate this with VEX expressions (e.g., running over points and sampling vertex attributes), but it requires manual expression writing and is less reliable.

**Impact**: Low — the color transfer workflow from texture to points can be approximated with `AttributeWrangle`, but requires manual VEX.

### 6. Platonic Solid / Utah Teapot — Test Geometry

**Houdini SOP**: `platonic` (Part 3, Step 06, Utah Teapot)
**Purpose**: Provides a built-in test shape (Utah Teapot) for switching between different brickify targets.

**PCG-AI status**: ⚠️ No built-in platonic solid or test geometry generator. Can be replaced with `ImportMesh` (load an FBX/OBJ of a teapot or other shape).

**Impact**: Very low — users can import any mesh as a source shape. No built-in test geometry is a convenience gap, not a capability gap.

### 7. Mesh-to-Point AttributeTransfer

**Houdini SOP**: `attribtransfer` (Part 4, Step 07)
**Purpose**: Transfers the `Cd` (color) attribute from the source mesh's points to the brick point cloud's points, based on spatial proximity.

**PCG-AI status**: ⚠️ `AttributeTransfer` exists and has the correct properties (distance threshold, kernel function, point/primitive/vertex attribute transfer). However, both its inputs require `SpatialMesh` pin type, while the brick point cloud is `SpatialPoint`. The transfer from a mesh to a point cloud is not directly supported.

**Impact**: Medium — texture-based coloring of bricks (Part 4) requires transferring colors from the textured source mesh to the point cloud. Without this, only solid-color bricks are possible.

**Available workaround**: Use `ProjectTexture` to project texture colors onto the source mesh, then use `VertexColor` to bake the colors. The colors would be on the mesh, not the points — CopyMeshToPoints may or may not inherit point colors from the source.

**Recommended product change**: Allow `AttributeTransfer` to accept `SpatialPoint` on one input (either source or target), enabling mesh-to-point and point-to-point attribute transfer.

---

## Coverage Summary

| Houdini Part | Feature | PCG-AI Coverage |
|---|---|---|
| Part 1: Create a Single Brick | Box, PolyExtrude, Group, PolyBevel, Subdivision | ✅ Full coverage |
| Part 2: Copy Bricks to Point Cloud | Test Geometry, Match Size, Points from Volumes, Copy to Points | ❌ Points from Volumes missing (CRITICAL) |
| Part 3: Add Color and Switch | Color, Material, Switch, Platonic | ✅ Color + Switch covered; Platonic is minor gap |
| Part 4: Color Points using Texture | Attribute VOP, UV, Attribute Promote, Attribute Transfer, Switch | ⚠️ Partial — texture projection exists, mesh-to-point transfer blocked by pin types |
| Part 5: Create Digital Asset | HDA collapse, parameter promotion, conditional UI | ✅ Subgraph + parameters[] covered |
| Part 6: Test Digital Asset | Multiple instances on different geometry | ✅ Subgraph instances with different inputs covered |
| Part 7: Animating Bricks | Group by Range, Blast, Sort, Animation Switch, $F expression | ❌ Frame animation + GroupByRange missing; Sort has pin type concern |

## Recommended Priority for Product Roadmap

1. **🔴 PointsFromVolume** — enables the core brickify workflow and many other volumetric scatter use cases
2. **🟡 SortGeometry pin type** — small fix (accept SpatialPoint), high impact on point-based workflows
3. **🟡 AttributeTransfer pin type** — enable mesh-to-point color/attribute transfer
4. **🟡 GroupByRange** — enables sequential selection for progressive/partial generation
5. **🟢 Frame/time system** — large architectural addition; consider expression parameters first
