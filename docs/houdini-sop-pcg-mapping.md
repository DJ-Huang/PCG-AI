# Houdini 基础建模 SOP ↔ PCG-AI 节点对照表

> 生成日期：2026-08-08
> 数据来源：[SideFX SOP 官方索引](https://www.sidefx.com/docs/houdini/nodes/sop/index.html) + `schema/node-manifest.json`

## 范围说明

| 范围 | 数量 | 说明 |
|------|------|------|
| Houdini **全部** SOP | **1040** | 官方文档索引（含模拟、绑定、Labs 等） |
| **基础建模** SOP（本文清单） | **237** | 12 类建模常用子集 |
| **PCG-AI** 节点 | **134** | `schema/node-manifest.json` |

### 状态图例

| 标记 | 含义 |
|------|------|
| ✅ | 有独立 PCG 节点，功能基本对齐 |
| 部分 | 有对应节点，但参数/能力为子集 |
| 组合 | 需多个 PCG 节点拼出同等效果 |
| ❌ | 无对应节点 |

### 覆盖率（基础建模 237 节点）

- ✅ 完整：60
- 部分：19
- 组合：1
- ❌ 无：157

---

## 一、Houdini 基础建模 SOP → PCG 映射（237）

### 1. 图元创建

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Add | `add` | `Add` | ✅ |
| Box | `box` | `CreateBoxMesh` | ✅ |
| Circle | `circle` | `CreateArcSpline` | ✅ |
| Circle from Edges | `circlefromedges` | — | ❌ |
| Circle Spline | `circlespline` | — | ❌ |
| Curve | `curve` | `CreateSpline` / `CreateBezierSpline` | 部分 |
| Draw Curve | `drawcurve` | — | ❌ |
| Font | `font` | — | ❌ |
| Grid | `grid` | `CreateGridMesh` | ✅ |
| Line | `line` | — | ❌ |
| Metaball | `metaball` | — | ❌ |
| Platonic Solids | `platonic` | — | ❌ |
| Sphere | `sphere` | — | ❌ |
| Spiral | `spiral` | `CreateSpiralSpline` | ✅ |
| Starburst | `starburst` | — | ❌ |
| Super Quad | `superquad` | — | ❌ |
| Torus | `torus` | — | ❌ |
| Tube | `tube` | `CreateCylinderMesh` | ✅ |

### 2. 变换与变形

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Bend | `bend` | `BendMesh` | 部分 |
| Bulge | `bulge` | — | ❌ |
| Lattice Deform | `lattice` | — | ❌ |
| Lattice Deform | `latticedeform` | — | ❌ |
| Lattice from Volume | `latticefromvolume` | — | ❌ |
| Magnet | `magnet` | — | ❌ |
| Match Size | `matchsize` | `MatchSize` | ✅ |
| Mirror | `mirror` | `MirrorMesh` | ✅ |
| Path Deform | `pathdeform` | — | ❌ |
| Peak | `peak` | — | ❌ |
| Point Deform | `pointdeform` | — | ❌ |
| Soft Transform | `softxform` | — | ❌ |
| Surface Deform | `surfacedeform` | — | ❌ |
| Transform | `xform` | `TransformMesh` | ✅ |
| Transform By Attribute | `xformbyattrib` | `TransformByAttribute` | ✅ |
| Transform Pieces | `xformpieces` | — | ❌ |

### 3. 多边形建模

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Blast | `blast` | `Blast` | ✅ |
| Boolean | `boolean` | `BooleanMesh` | ✅ |
| Clean | `clean` | — | ❌ |
| Clip | `clip` | `Clip` | ✅ |
| Cookie | `cookie` | — | ❌ |
| Crease | `crease` | — | ❌ |
| Delete | `delete` | `Delete` | ✅ |
| Dissolve | `dissolve` | — | ❌ |
| Divide | `divide` | `SubdivideMesh` | 部分 |
| Edit | `edit` | — | ❌ |
| Ends | `ends` | — | ❌ |
| Extrude | `extrude` | `PolyExtrude` / `ExtrudeAlongSpline` | 部分 |
| Fuse | `fuse` | `FuseMesh` | ✅ |
| Hole | `hole` | — | ❌ |
| Inset | `inset` | — | ❌ |
| Poly Bridge | `polybridge` | — | ❌ |
| Poly Expand 2D | `polyexpand2d` | `LotSubdivision` | 部分 |
| Poly Extrude | `polyextrude` | `PolyExtrude` | ✅ |
| PolyBevel | `polybevel` | `BevelMesh` | ✅ |
| PolyBevel 3.0 | `polybevel-3.0` | `BevelMesh` | ✅ |
| PolyCut | `polycut` | `PolySlice` | 部分 |
| PolyDoctor | `polydoctor` | — | ❌ |
| PolyFill | `polyfill` | — | ❌ |
| PolyFrame | `polyframe` | — | ❌ |
| PolyHinge | `polyhinge` | — | ❌ |
| PolyLoft | `polyloft` | `LoftMesh` | 部分 |
| PolyPatch | `polypatch` | — | ❌ |
| PolyReduce | `polyreduce` | — | ❌ |
| PolySoup | `polysoup` | — | ❌ |
| PolySpline | `polyspline` | — | ❌ |
| PolySplit | `polysplit` | — | ❌ |
| PolyWire | `polywire` | `PolyWire` | ✅ |
| Quad Remesh | `quadremesh` | — | ❌ |
| Remesh | `remesh` | — | ❌ |
| Remesh to Grid | `remeshgrid` | — | ❌ |
| Reverse | `reverse` | `ReverseMesh` | ✅ |
| Sculpt | `sculpt` | — | ❌ |
| Sculpt 2.0 | `sculpt-2.0` | — | ❌ |
| Subdivide | `subdivide` | `SubdivideMesh` | ✅ |
| Unsubdivide | `unsubdivide` | — | ❌ |

### 4. 边操作

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Comb | `comb` | — | ❌ |
| Edge Collapse | `edgecollapse` | — | ❌ |
| Edge Cusp | `edgecusp` | — | ❌ |
| Edge Divide | `edgedivide` | — | ❌ |
| Edge Equalize | `edgeequalize` | — | ❌ |
| Edge Flip | `edgeflip` | — | ❌ |
| Edge Fracture | `edgefracture` | — | ❌ |
| Edge Relax | `edgerelax` | — | ❌ |
| Edge Straighten | `edgestraighten` | — | ❌ |
| Edge Transport | `edgetransport` | — | ❌ |

### 5. 组与选择

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Blast by Attribute | `blastbyattribute` | — | ❌ |
| Group | `groupcreate` | `GroupCreate` | ✅ |
| Group | `group` | `GroupCreate` | ✅ |
| Group by Lasso | `groupbylasso` | — | ❌ |
| Group Combine | `groupcombine` | `GroupCombine` | ✅ |
| Group Copy | `groupcopy` | — | ❌ |
| Group Delete | `groupdelete` | `GroupDelete` | ✅ |
| Group Expand | `groupexpand` | — | ❌ |
| Group Expression | `groupexpression` | — | ❌ |
| Group Find Path | `groupfindpath` | — | ❌ |
| Group from Attribute Boundary | `groupfromattribboundary` | — | ❌ |
| Group Invert | `groupinvert` | — | ❌ |
| Group Paint | `grouppaint` | — | ❌ |
| Group Promote | `grouppromote` | `GroupPromote` | ✅ |
| Group Range | `grouprange` | `GroupByRange` | ✅ |
| Group Rename | `grouprename` | — | ❌ |
| Group Transfer | `grouptransfer` | `GroupTransfer` | ✅ |
| Groups from Name | `groupsfromname` | — | ❌ |
| Split | `split` | `Split` | ✅ |

### 6. 属性

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Attribute Blur | `attribblur` | — | ❌ |
| Attribute Cast | `attribcast` | — | ❌ |
| Attribute Combine | `attribcombine` | — | ❌ |
| Attribute Composite | `attribcomposite` | — | ❌ |
| Attribute Copy | `attribcopy` | `CopyAttributes` | ✅ |
| Attribute Create | `attribcreate` | — | ❌ |
| Attribute Delete | `attribdelete` | `DeleteAttributes` | ✅ |
| Attribute Expression | `attribexpression` | — | ❌ |
| Attribute Fade | `attribfade` | — | ❌ |
| Attribute Fill | `attribfill` | — | ❌ |
| Attribute from Map | `attribfrommap` | — | ❌ |
| Attribute From Pieces | `attribfrompieces` | — | ❌ |
| Attribute from Volume | `attribfromvolume` | — | ❌ |
| Attribute Interpolate | `attribinterpolate` | — | ❌ |
| Attribute Mirror | `attribmirror` | — | ❌ |
| Attribute Noise | `attribnoise` | — | ❌ |
| Attribute Promote | `attribpromote` | — | ❌ |
| Attribute Randomize | `attribrandomize` | `AttributeRandomize` | ✅ |
| Attribute Remap | `attribremap` | — | ❌ |
| Attribute Reorient | `attribreorient` | — | ❌ |
| Attribute Sort | `attribsort` | — | ❌ |
| Attribute String Edit | `attribstringedit` | — | ❌ |
| Attribute Swap | `attribswap` | — | ❌ |
| Attribute Transfer | `attribtransfer` | `AttributeTransfer` | ✅ |
| Attribute VOP | `attribvop` | — | ❌ |
| Attribute Wrangle | `attribwrangle` | `AttributeWrangle` | ✅ |
| Color | `color` | `VertexColor` | ✅ |
| Extract Centroid | `extractcentroid` | `ExtractCentroid` | ✅ |
| Extract Transform | `extracttransform` | — | ❌ |
| Material | `material` | `AssignMaterial` | ✅ |
| Name | `name` | — | ❌ |

### 7. 复制 / 合并 / 流程

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Assemble | `assemble` | `Assemble` | ✅ |
| Block Begin | `block_begin` | `ForEachBegin` | 部分 |
| Block End | `block_end` | `ForEachEnd` | 部分 |
| Cache | `cache` | — | ❌ |
| Cache If | `cacheif` | — | ❌ |
| Copy | `copy` | `CopyMesh` | ✅ |
| Copy and Transform | `copyxform` | `CopyMesh` + `TransformMesh` | 组合 |
| Copy to Curves | `copytocurves` | `InstanceAlongSpline` | 部分 |
| Copy to Points | `copytopoints` | `CopyMeshToPoints` | ✅ |
| File | `file` | `ImportMesh` | ✅ |
| For Each | `foreach` | `ForEachBegin` + `ForEachEnd` | 部分 |
| Instance | `instance` | — | ❌ |
| Merge | `merge` | `MergeMesh` | ✅ |
| Null | `null` | — | ❌ |
| Output | `output` | `Output` | ✅ |
| Pack | `pack` | — | ❌ |
| Stash | `stash` | — | ❌ |
| Switch | `switch` | `Switch` | ✅ |
| Switch-If | `switchif` | `SwitchIf` | ✅ |
| Unpack | `unpack` | — | ❌ |

### 8. 曲线与曲面

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Carve | `carve` | `Carve` | ✅ |
| Convert Line | `convertline` | `ConvertLine` | ✅ |
| Cross Section Surface | `crosssectionsurface` | — | ❌ |
| Curve Intersect | `curvesect` | — | ❌ |
| Loft | `loft` | `LoftMesh` | ✅ |
| Orientation along Curve | `orientalongcurve` | — | ❌ |
| Planar Patch | `planarpatch` | — | ❌ |
| Planar Patch from Curves | `planarpatchfromcurves` | — | ❌ |
| Rails | `rails` | — | ❌ |
| Resample | `resample` | `ResampleSpline` | ✅ |
| Revolve | `revolve` | `RevolveMesh` | ✅ |
| Revolve 2.0 | `revolve-2.0` | `RevolveMesh` | ✅ |
| Skin | `skin` | `LoftMesh` | 部分 |
| Spline Align | `align` | — | ❌ |
| Spline Basis | `basis` | — | ❌ |
| Spline Cap | `cap` | — | ❌ |
| Spline Clay | `clay` | — | ❌ |
| Spline Creep | `creep` | — | ❌ |
| Spline Curve Clay | `curveclay` | — | ❌ |
| Spline Fillet | `fillet` | — | ❌ |
| Spline Fit | `fit` | — | ❌ |
| Spline Profile | `profile` | `CrossSectionProfile` | 部分 |
| Spline Project | `project` | — | ❌ |
| Spline Round | `round` | — | ❌ |
| Spline Surfsect | `surfsect` | — | ❌ |
| Spline Trim | `trim` | — | ❌ |
| Sweep | `sweep` | `SweepAlongSpline` | ✅ |
| Sweep 2.0 | `sweep-2.0` | `SweepAlongSpline` | ✅ |

### 9. 点与散布

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Cluster | `cluster` | — | ❌ |
| Cluster Points | `clusterpoints` | — | ❌ |
| Point Generate | `pointgenerate` | — | ❌ |
| Point Jitter | `pointjitter` | — | ❌ |
| Point Relax | `relax` | `PointRelax` | 部分 |
| Point Replicate | `pointreplicate` | — | ❌ |
| Point Weld | `pointweld` | — | ❌ |
| Points from Volume | `pointsfromvolume` | `PointsFromVolume` | ✅ |
| Scatter | `scatter` | `SampleMeshSurface` / `SurfaceSampler` | 部分 |
| Scatter and Align | `scatteralign` | — | ❌ |

### 10. UV

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| UV Auto Seam | `uvautoseam` | — | ❌ |
| UV Brush | `uvbrush` | — | ❌ |
| UV Edit | `uvedit` | — | ❌ |
| UV Flatten | `uvflatten` | — | ❌ |
| UV Flatten 3.0 | `uvflatten-3.0` | — | ❌ |
| UV Flatten from Points | `uvflattenfrompoints` | — | ❌ |
| UV Fuse | `uvfuse` | — | ❌ |
| UV Layout | `uvlayout` | — | ❌ |
| UV Pelt | `uvpelt` | — | ❌ |
| UV Pelt 2.0 | `uvpelt-2.0` | — | ❌ |
| UV Project | `uvproject` | `UVTexture` | 部分 |
| UV Relax | `uvrelax` | — | ❌ |
| UV Texture | `texture` | `UVTexture` | 部分 |
| UV Transform | `uvtransform` | — | ❌ |
| UV Transform 2.0 | `uvtransform-2.0` | — | ❌ |
| UV Unwrap | `uvunwrap` | — | ❌ |

### 11. 分析工具

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Bound | `bound` | `BoundMesh` | ✅ |
| Connectivity | `connectivity` | `Connectivity` | ✅ |
| Convert | `convert` | — | ❌ |
| Distance along Geometry | `distancealonggeometry` | — | ❌ |
| Distance from Geometry | `distancefromgeometry` | — | ❌ |
| Facet | `facet` | `ComputeNormals` | 部分 |
| Find Shortest Path | `findshortestpath` | `FindShortestPath` | ✅ |
| Flatten | `flatten` | — | ❌ |
| Intersection Analysis | `intersectionanalysis` | — | ❌ |
| Intersection Stitch | `intersectionstitch` | — | ❌ |
| Join | `join` | — | ❌ |
| Match Axis | `matchaxis` | — | ❌ |
| Match Topology | `matchtopology` | — | ❌ |
| Measure | `measure` | `MeasureMesh` | ✅ |
| Measure Thickness | `measurethickness` | — | ❌ |
| Normal | `normal` | `ComputeNormals` | ✅ |
| Primitive Properties | `primitive` | `PrimitiveTransform` | 部分 |
| Proximity | `proximity` | — | ❌ |
| Separate Pieces | `separatepieces` | — | ❌ |
| Sort | `sort` | `SortGeometry` | ✅ |
| Winding Number | `windingnumber` | — | ❌ |

### 12. 体素 / 隐式曲面建模

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Convert VDB | `convertvdb` | — | ❌ |
| Implicit Surface | `implicitsurface` | — | ❌ |
| IsoOffset | `isooffset` | — | ❌ |
| MetaGroups | `metagroups` | — | ❌ |
| VDB | `vdb` | — | ❌ |
| VDB from Polygons | `vdbfrompolygons` | — | ❌ |
| Volume | `volume` | — | ❌ |
| Volume SDF | `volumesdf` | — | ❌ |

---

## 二、PCG-AI 节点 → Houdini 反向对照（134）

| PCG 节点 | 类别 | Houdini SOP |
|---|---|---|
| `Add` | Geometry | `add` |
| `AttributeTransfer` | Attribute | `attribtransfer` |
| `AttributeWrangle` | Attribute | `attribwrangle` |
| `ExtractCentroid` | Attribute | `extractcentroid` |
| `AttributeFilter` | Filter | 组合 |
| `Blast` | Filter | `blast` |
| `Delete` | Filter | `delete` |
| `DensityFilter` | Filter | — |
| `Split` | Filter | `split` |
| `ForEachBegin` | Flow | `block_begin` |
| `ForEachEnd` | Flow | `block_end` |
| `Switch` | Flow | `switch` |
| `SwitchIf` | Flow | `switchif` |
| `CreatePointGrid` | Generation | `add` / `grid` |
| `CreatePoints` | Generation | `add`（legacy 简化版） |
| `PointRelax` | Generation | `relax` |
| `PointsFromVolume` | Generation | `pointsfromvolume` |
| `SampleMeshSurface` | Generation | `scatter` |
| `SpawnPoints` | Generation | `scatter` |
| `SurfaceSampler` | Generation | `scatter` |
| `FaceGroupByNormal` | Geometry | `group`（法线选择） |
| `GroupByRange` | Geometry | `grouprange` |
| `GroupCombine` | Geometry | `groupcombine` |
| `GroupCreate` | Geometry | `group` |
| `GroupDelete` | Geometry | `groupdelete` |
| `GroupPromote` | Geometry | `grouppromote` |
| `GroupTransfer` | Geometry | `grouptransfer` |
| `SortGeometry` | Geometry | `sort` |
| `GetMeshData` | Input | — |
| `GetSplineData` | Input | — |
| `AssignMaterial` | Material | `material` |
| `VertexColor` | Material | `color` |
| `Assemble` | Mesh | `assemble` |
| `BendMesh` | Mesh | `bend`（子集） |
| `BevelMesh` | Mesh | `polybevel` |
| `BooleanMesh` | Mesh | `boolean` |
| `BoundMesh` | Mesh | `bound` |
| `Clip` | Mesh | `clip` |
| `ComputeNormals` | Mesh | `normal` / `facet` |
| `Connectivity` | Mesh | `connectivity` |
| `CopyMesh` | Mesh | `copy` |
| `CopyMeshToPoints` | Mesh | `copytopoints` |
| `CreateBoxMesh` | Mesh | `box` |
| `CreateCylinderMesh` | Mesh | `tube` |
| `CreateGridMesh` | Mesh | `grid` |
| `FindShortestPath` | Mesh | `findshortestpath` |
| `FuseMesh` | Mesh | `fuse` |
| `ImportMesh` | Mesh | `file` |
| `LoftMesh` | Mesh | `loft` / `skin` |
| `LotSubdivision` | Mesh | `polyexpand2d`（Labs） |
| `MatchSize` | Mesh | `matchsize` |
| `MeasureMesh` | Mesh | `measure` |
| `MergeMesh` | Mesh | `merge` |
| `MeshNoiseDeform` | Mesh | `attribnoise`（子集） |
| `Meshy3DGenerator` | Mesh | — |
| `MeshyMeshOps` | Mesh | — |
| `MeshyRetexture` | Mesh | — |
| `MeshyTextTo3D` | Mesh | — |
| `MirrorMesh` | Mesh | `mirror` |
| `OutlineSolid` | Mesh | 组合（轮廓+厚度） |
| `PolyExtrude` | Mesh | `polyextrude` |
| `PolySlice` | Mesh | `polycut` |
| `PolyWire` | Mesh | `polywire` |
| `PrimitiveTransform` | Mesh | `primitive` |
| `ReverseMesh` | Mesh | `reverse` |
| `RevolveMesh` | Mesh | `revolve` |
| `ShellMesh` | Mesh | `polyextrude` |
| `SmoothMesh` | Mesh | `smooth` |
| `SubdivideMesh` | Mesh | `subdivide` |
| `ThickenMesh` | Mesh | `polyextrude` |
| `TransformByAttribute` | Mesh | `xformbyattrib` |
| `TransformMesh` | Mesh | `xform` |
| `Tripo3DGenerator` | Mesh | — |
| `BreakAttributes` | Metadata | — |
| `CopyAttributes` | Metadata | `attribcopy` |
| `DeleteAttributes` | Metadata | `attribdelete` |
| `ExportFBX` | Output | ROP（非 SOP） |
| `Output` | Output | `output` |
| `GetTerrainData` | Sampler | — |
| `SampleSurface` | Sampler | — |
| `TreeSimpleLeaf` | Scatter | Labs tree |
| `MergeSpawnPoints` | Spawner | — |
| `PlaceInScene` | Spawner | — |
| `StaticMeshSpawner` | Spawner | — |
| `Carve` | Spline | `carve` |
| `ConditionOutline` | Spline | — |
| `ConvertLine` | Spline | `convertline` |
| `CreateArcSpline` | Spline | `circle` |
| `CreateBezierSpline` | Spline | `curve` |
| `CreateSpiralSpline` | Spline | `spiral` |
| `CreateSpline` | Spline | `curve` |
| `CrossSectionProfile` | Spline | `profile` |
| `ExtrudeAlongSpline` | Spline | `extrude` |
| `InstanceAlongSpline` | Spline | `copytocurves` |
| `Resample` | Spline | — |
| `ResampleSpline` | Spline | `resample` |
| `SampleAlongSpline` | Spline | `resample` + `copy`（组合） |
| `SweepAlongSpline` | Spline | `sweep` |
| `AStarPathfinding` | Structural | — |
| `ConnectNearest` | Structural | — |
| `ConvexHull` | Structural | — |
| `Delaunay` | Structural | — |
| `MST` | Structural | — |
| `Voronoi` | Structural | `voronoifracture`（部分） |
| `ConvertHeightField` | Terrain | `heightfield_convert` |
| `HeightField` | Terrain | `heightfield` |
| `HeightFieldBlur` | Terrain | `heightfield_blur` |
| `HeightFieldClip` | Terrain | `heightfield_clip` |
| `HeightFieldCopyLayer` | Terrain | `heightfield_copylayer` |
| `HeightFieldDistortByNoise` | Terrain | `heightfield_distort` |
| `HeightFieldErode` | Terrain | `heightfield_erode` |
| `HeightFieldFile` | Terrain | `heightfield_file` |
| `HeightFieldFlowField` | Terrain | `heightfield_flow` |
| `HeightFieldIsolateLayer` | Terrain | `heightfield_isolatelayer` |
| `HeightFieldLayer` | Terrain | `heightfield_layer` |
| `HeightFieldLayerClear` | Terrain | `heightfield_layerclear` |
| `HeightFieldLayerProperties` | Terrain | `heightfield_layerprop` |
| `HeightFieldMaskByFeature` | Terrain | `heightfield_maskbyfeature` |
| `HeightFieldMaskByObject` | Terrain | `heightfield_maskbyobject` |
| `HeightFieldMaskNoise` | Terrain | `heightfield_masknoise` |
| `HeightFieldNoise` | Terrain | `heightfield_noise` |
| `HeightFieldPattern` | Terrain | `heightfield_pattern` |
| `HeightFieldProject` | Terrain | `heightfield_project` |
| `HeightFieldResample` | Terrain | `heightfield_resample` |
| `HeightFieldScatter` | Terrain | `heightfield_scatter` |
| `HeightFieldSlump` | Terrain | `heightfield_slump` |
| `HeightFieldTerrace` | Terrain | `heightfield_terrace` |
| `ImageTexture` | Texture | — |
| `MeshyImageGen` | Texture | — |
| `AttributeRandomize` | Transform | `attribrandomize` |
| `ProjectPoints` | Transform | — |
| `TransformPoints` | Transform | `xform`（点域） |
| `ProjectTexture` | UV | `texture` |
| `UVTexture` | UV | `uvproject` / `texture` |

---

## 三、附录：HeightField 地形节点（22）

PCG 在 `heightfield_*` 系列有完整对标，属于地形域，未计入上文 237 个多边形基础建模节点。

| PCG 节点 | Houdini SOP |
|---|---|
| `ConvertHeightField` | `heightfield_convert` |
| `HeightField` | `heightfield` |
| `HeightFieldBlur` | `heightfield_blur` |
| `HeightFieldClip` | `heightfield_clip` |
| `HeightFieldCopyLayer` | `heightfield_copylayer` |
| `HeightFieldDistortByNoise` | `heightfield_distort` |
| `HeightFieldErode` | `heightfield_erode` |
| `HeightFieldFile` | `heightfield_file` |
| `HeightFieldFlowField` | `heightfield_flow` |
| `HeightFieldIsolateLayer` | `heightfield_isolatelayer` |
| `HeightFieldLayer` | `heightfield_layer` |
| `HeightFieldLayerClear` | `heightfield_layerclear` |
| `HeightFieldLayerProperties` | `heightfield_layerprop` |
| `HeightFieldMaskByFeature` | `heightfield_maskbyfeature` |
| `HeightFieldMaskByObject` | `heightfield_maskbyobject` |
| `HeightFieldMaskNoise` | `heightfield_masknoise` |
| `HeightFieldNoise` | `heightfield_noise` |
| `HeightFieldPattern` | `heightfield_pattern` |
| `HeightFieldProject` | `heightfield_project` |
| `HeightFieldResample` | `heightfield_resample` |
| `HeightFieldScatter` | `heightfield_scatter` |
| `HeightFieldSlump` | `heightfield_slump` |
| `HeightFieldTerrace` | `heightfield_terrace` |

---

## 四、关键缺口（建模向）

| 优先级 | Houdini SOP | 影响 |
|--------|-------------|------|
| 高 | `sphere` / `torus` / `platonic` | 缺少基础图元生成器（目前只有 Box/Cylinder/Grid） |
| 高 | `attribcreate` / `attribpromote` | 属性创建/提升链路不完整 |
| 高 | `edit`（Make Circle 等） | 交互式面编辑缺失 |
| 中 | `dissolve` / `remesh` / `polyreduce` | 拓扑清理与重网格化 |
| 中 | `uvflatten` / `uvunwrap` | 仅 planar/cylindrical/spherical 投射 |
| 中 | `lattice` / `peak` / `bulge` | 变形器族缺失 |
| 中 | `vdbfrompolygons` / `isooffset` | 体素/隐式建模缺失 |
| 低 | `cookie` | 2D 曲线布尔 |
| 低 | `sculpt` | 雕刻 |

---

## 参考

- [SideFX Geometry (SOP) nodes](https://www.sidefx.com/docs/houdini/nodes/sop/index.html)
- [PCG-AI node-manifest.json](../schema/node-manifest.json)
- [PCG-AI node-reference.md](./node-reference.md)
- [brickify-tool gap analysis](../examples/brickify-tool/brickify-gap-analysis.md)
