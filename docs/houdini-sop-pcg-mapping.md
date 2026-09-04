# Houdini 基础建模 SOP ↔ PCG-AI 节点对照表

> 更新日期：2026-08-15
> 数据来源：[SideFX SOP 官方索引](https://www.sidefx.com/docs/houdini/nodes/sop/index.html) + `schema/node-manifest.json`

## 范围说明

| 范围 | 数量 | 说明 |
|------|------|------|
| Houdini **全部** SOP | **1040** | 官方文档索引（含模拟、绑定、Labs 等） |
| **基础建模** SOP（本文清单） | **237** | 12 类建模常用子集 |
| **PCG-AI** 节点 | **305** | `schema/node-manifest.json`（304 个 Core cook 节点 + `ExportFBX`） |

### 状态图例

| 标记 | 含义 |
|------|------|
| ✅ | 有独立 PCG 节点，Houdini 主参数与基础 cook 能力已对齐 |
| 部分 | 有对应节点，但参数/能力为子集 |
| 组合 | 需多个 PCG 节点拼出同等效果 |
| ❌ | 无对应节点 |

### 覆盖率（基础建模 237 节点）

- ✅ 完整：237（100%）
- 部分：0
- 组合：0
- ❌ 无：0

---

## 一、Houdini 基础建模 SOP → PCG 映射（237）

### 1. 图元创建

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Add | `add` | `Add` | ✅ |
| Box | `box` | `CreateBoxMesh` | ✅ |
| Circle | `circle` | `CreateArcSpline` | ✅ |
| Circle from Edges | `circlefromedges` | `CircleFromEdges` | ✅ |
| Circle Spline | `circlespline` | `CircleSpline` | ✅ |
| Curve | `curve` | `Curve` | ✅ |
| Draw Curve | `drawcurve` | `DrawCurve` | ✅ |
| Font | `font` | `Font` | ✅ |
| Grid | `grid` | `CreateGridMesh` | ✅ |
| Line | `line` | `Line` | ✅ |
| Metaball | `metaball` | `Metaball` | ✅ |
| Platonic Solids | `platonic` | `PlatonicSolids` | ✅ |
| Sphere | `sphere` | `Sphere` | ✅ |
| Spiral | `spiral` | `CreateSpiralSpline` | ✅ |
| Starburst | `starburst` | `Starburst` | ✅ |
| Super Quad | `superquad` | `SuperQuad` | ✅ |
| Torus | `torus` | `Torus` | ✅ |
| Tube | `tube` | `CreateCylinderMesh` | ✅ |

### 2. 变换与变形

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Bend | `bend` | `Bend` | ✅ |
| Bulge | `bulge` | `Bulge` | ✅ |
| Lattice Deform | `lattice` | `LatticeDeform` | ✅ |
| Lattice Deform | `latticedeform` | `LatticeDeform` | ✅ |
| Lattice from Volume | `latticefromvolume` | `LatticeFromVolume` | ✅ |
| Magnet | `magnet` | `Magnet` | ✅ |
| Match Size | `matchsize` | `MatchSize` | ✅ |
| Mirror | `mirror` | `MirrorMesh` | ✅ |
| Path Deform | `pathdeform` | `PathDeform` | ✅ |
| Peak | `peak` | `Peak` | ✅ |
| Point Deform | `pointdeform` | `PointDeform` | ✅ |
| Soft Transform | `softxform` | `SoftTransform` | ✅ |
| Surface Deform | `surfacedeform` | `SurfaceDeform` | ✅ |
| Transform | `xform` | `TransformMesh` | ✅ |
| Transform By Attribute | `xformbyattrib` | `TransformByAttribute` | ✅ |
| Transform Pieces | `xformpieces` | `TransformPieces` | ✅ |

### 3. 多边形建模

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Blast | `blast` | `Blast` | ✅ |
| Boolean | `boolean` | `BooleanMesh` | ✅ |
| Clean | `clean` | `Clean` | ✅ |
| Clip | `clip` | `Clip` | ✅ |
| Cookie | `cookie` | `Cookie` | ✅ |
| Crease | `crease` | `Crease` | ✅ |
| Delete | `delete` | `Delete` | ✅ |
| Dissolve | `dissolve` | `Dissolve` | ✅ |
| Divide | `divide` | `Divide` | ✅ |
| Edit | `edit` | `Edit` | ✅ |
| Ends | `ends` | `Ends` | ✅ |
| Extrude | `extrude` | `Extrude` | ✅ |
| Fuse | `fuse` | `FuseMesh` | ✅ |
| Hole | `hole` | `Hole` | ✅ |
| Inset | `inset` | `Inset` | ✅ |
| Poly Bridge | `polybridge` | `PolyBridge` | ✅ |
| Poly Expand 2D | `polyexpand2d` | `PolyExpand2D` | ✅ |
| Poly Extrude | `polyextrude` | `PolyExtrude` | ✅ |
| PolyBevel | `polybevel` | `BevelMesh` | ✅ |
| PolyBevel 3.0 | `polybevel-3.0` | `BevelMesh` | ✅ |
| PolyCut | `polycut` | `PolyCut` | ✅ |
| PolyDoctor | `polydoctor` | `PolyDoctor` | ✅ |
| PolyFill | `polyfill` | `PolyFill` | ✅ |
| PolyFrame | `polyframe` | `PolyFrame` | ✅ |
| PolyHinge | `polyhinge` | `PolyHinge` | ✅ |
| PolyLoft | `polyloft` | `PolyLoft` | ✅ |
| PolyPatch | `polypatch` | `PolyPatch` | ✅ |
| PolyReduce | `polyreduce` | `PolyReduce` | ✅ |
| PolySoup | `polysoup` | `PolySoup` | ✅ |
| PolySpline | `polyspline` | `PolySpline` | ✅ |
| PolySplit | `polysplit` | `PolySplit` | ✅ |
| PolyWire | `polywire` | `PolyWire` | ✅ |
| Quad Remesh | `quadremesh` | `QuadRemesh` | ✅ |
| Remesh | `remesh` | `Remesh` | ✅ |
| Remesh to Grid | `remeshgrid` | `RemeshToGrid` | ✅ |
| Reverse | `reverse` | `ReverseMesh` | ✅ |
| Sculpt | `sculpt` | `Sculpt` | ✅ |
| Sculpt 2.0 | `sculpt-2.0` | `Sculpt` | ✅ |
| Subdivide | `subdivide` | `SubdivideMesh` | ✅ |
| Unsubdivide | `unsubdivide` | `Unsubdivide` | ✅ |

### 4. 边操作

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Comb | `comb` | `Comb` | ✅ |
| Edge Collapse | `edgecollapse` | `EdgeCollapse` | ✅ |
| Edge Cusp | `edgecusp` | `EdgeCusp` | ✅ |
| Edge Divide | `edgedivide` | `EdgeDivide` | ✅ |
| Edge Equalize | `edgeequalize` | `EdgeEqualize` | ✅ |
| Edge Flip | `edgeflip` | `EdgeFlip` | ✅ |
| Edge Fracture | `edgefracture` | `EdgeFracture` | ✅ |
| Edge Relax | `edgerelax` | `EdgeRelax` | ✅ |
| Edge Straighten | `edgestraighten` | `EdgeStraighten` | ✅ |
| Edge Transport | `edgetransport` | `EdgeTransport` | ✅ |

### 5. 组与选择

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Blast by Attribute | `blastbyattribute` | `BlastByAttribute` | ✅ |
| Group | `groupcreate` | `GroupCreate` | ✅ |
| Group | `group` | `GroupCreate` | ✅ |
| Group by Lasso | `groupbylasso` | `GroupByLasso` | ✅ |
| Group Combine | `groupcombine` | `GroupCombine` | ✅ |
| Group Copy | `groupcopy` | `GroupCopy` | ✅ |
| Group Delete | `groupdelete` | `GroupDelete` | ✅ |
| Group Expand | `groupexpand` | `GroupExpand` | ✅ |
| Group Expression | `groupexpression` | `GroupExpression` | ✅ |
| Group Find Path | `groupfindpath` | `GroupFindPath` | ✅ |
| Group from Attribute Boundary | `groupfromattribboundary` | `GroupFromAttributeBoundary` | ✅ |
| Group Invert | `groupinvert` | `GroupInvert` | ✅ |
| Group Paint | `grouppaint` | `GroupPaint` | ✅ |
| Group Promote | `grouppromote` | `GroupPromote` | ✅ |
| Group Range | `grouprange` | `GroupByRange` | ✅ |
| Group Rename | `grouprename` | `GroupRename` | ✅ |
| Group Transfer | `grouptransfer` | `GroupTransfer` | ✅ |
| Groups from Name | `groupsfromname` | `GroupsFromName` | ✅ |
| Split | `split` | `Split` | ✅ |

### 6. 属性

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Attribute Blur | `attribblur` | `AttributeBlur` | ✅ |
| Attribute Cast | `attribcast` | `AttributeCast` | ✅ |
| Attribute Combine | `attribcombine` | `AttributeCombine` | ✅ |
| Attribute Composite | `attribcomposite` | `AttributeComposite` | ✅ |
| Attribute Copy | `attribcopy` | `CopyAttributes` | ✅ |
| Attribute Create | `attribcreate` | `AttributeCreate` | ✅ |
| Attribute Delete | `attribdelete` | `DeleteAttributes` | ✅ |
| Attribute Expression | `attribexpression` | `AttributeExpression` | ✅ |
| Attribute Fade | `attribfade` | `AttributeFade` | ✅ |
| Attribute Fill | `attribfill` | `AttributeFill` | ✅ |
| Attribute from Map | `attribfrommap` | `AttributeFromMap` | ✅ |
| Attribute From Pieces | `attribfrompieces` | `AttributeFromPieces` | ✅ |
| Attribute from Volume | `attribfromvolume` | `AttributeFromVolume` | ✅ |
| Attribute Interpolate | `attribinterpolate` | `AttributeInterpolate` | ✅ |
| Attribute Mirror | `attribmirror` | `AttributeMirror` | ✅ |
| Attribute Noise | `attribnoise` | `AttributeNoise` | ✅ |
| Attribute Promote | `attribpromote` | `AttributePromote` | ✅ |
| Attribute Randomize | `attribrandomize` | `AttributeRandomize` | ✅ |
| Attribute Remap | `attribremap` | `AttributeRemap` | ✅ |
| Attribute Reorient | `attribreorient` | `AttributeReorient` | ✅ |
| Attribute Sort | `attribsort` | `AttributeSort` | ✅ |
| Attribute String Edit | `attribstringedit` | `AttributeStringEdit` | ✅ |
| Attribute Swap | `attribswap` | `AttributeSwap` | ✅ |
| Attribute Transfer | `attribtransfer` | `AttributeTransfer` | ✅ |
| Attribute VOP | `attribvop` | `AttributeVOP` | ✅ |
| Attribute Wrangle | `attribwrangle` | `AttributeWrangle` | ✅ |
| Color | `color` | `VertexColor` | ✅ |
| Extract Centroid | `extractcentroid` | `ExtractCentroid` | ✅ |
| Extract Transform | `extracttransform` | `ExtractTransform` | ✅ |
| Material | `material` | `AssignMaterial` | ✅ |
| Name | `name` | `Name` | ✅ |

### 7. 复制 / 合并 / 流程

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Assemble | `assemble` | `Assemble` | ✅ |
| Block Begin | `block_begin` | `BlockBegin` | ✅ |
| Block End | `block_end` | `BlockEnd` | ✅ |
| Cache | `cache` | `Cache` | ✅ |
| Cache If | `cacheif` | `CacheIf` | ✅ |
| Copy | `copy` | `CopyMesh` | ✅ |
| Copy and Transform | `copyxform` | `CopyAndTransform` | ✅ |
| Copy to Curves | `copytocurves` | `CopyToCurves` | ✅ |
| Copy to Points | `copytopoints` | `CopyMeshToPoints` | ✅ |
| File | `file` | `ImportMesh` | ✅ |
| For Each | `foreach` | `ForEach` | ✅ |
| Instance | `instance` | `Instance` | ✅ |
| Merge | `merge` | `MergeMesh` | ✅ |
| Null | `null` | `Null` | ✅ |
| Output | `output` | `Output` | ✅ |
| Pack | `pack` | `Pack` | ✅ |
| Stash | `stash` | `Stash` | ✅ |
| Switch | `switch` | `Switch` | ✅ |
| Switch-If | `switchif` | `SwitchIf` | ✅ |
| Unpack | `unpack` | `Unpack` | ✅ |

### 8. 曲线与曲面

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Carve | `carve` | `Carve` | ✅ |
| Convert Line | `convertline` | `ConvertLine` | ✅ |
| Cross Section Surface | `crosssectionsurface` | `CrossSectionSurface` | ✅ |
| Curve Intersect | `curvesect` | `CurveIntersect` | ✅ |
| Loft | `loft` | `LoftMesh` | ✅ |
| Orientation along Curve | `orientalongcurve` | `OrientationAlongCurve` | ✅ |
| Planar Patch | `planarpatch` | `PlanarPatch` | ✅ |
| Planar Patch from Curves | `planarpatchfromcurves` | `PlanarPatchFromCurves` | ✅ |
| Rails | `rails` | `Rails` | ✅ |
| Resample | `resample` | `ResampleSpline` | ✅ |
| Revolve | `revolve` | `RevolveMesh` | ✅ |
| Revolve 2.0 | `revolve-2.0` | `RevolveMesh` | ✅ |
| Skin | `skin` | `Skin` | ✅ |
| Spline Align | `align` | `SplineAlign` | ✅ |
| Spline Basis | `basis` | `SplineBasis` | ✅ |
| Spline Cap | `cap` | `SplineCap` | ✅ |
| Spline Clay | `clay` | `SplineClay` | ✅ |
| Spline Creep | `creep` | `SplineCreep` | ✅ |
| Spline Curve Clay | `curveclay` | `SplineCurveClay` | ✅ |
| Spline Fillet | `fillet` | `SplineFillet` | ✅ |
| Spline Fit | `fit` | `SplineFit` | ✅ |
| Spline Profile | `profile` | `SplineProfile` | ✅ |
| Spline Project | `project` | `SplineProject` | ✅ |
| Spline Round | `round` | `SplineRound` | ✅ |
| Spline Surfsect | `surfsect` | `SplineSurfsect` | ✅ |
| Spline Trim | `trim` | `SplineTrim` | ✅ |
| Sweep | `sweep` | `SweepAlongSpline` | ✅ |
| Sweep 2.0 | `sweep-2.0` | `SweepAlongSpline` | ✅ |

### 9. 点与散布

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Cluster | `cluster` | `Cluster` | ✅ |
| Cluster Points | `clusterpoints` | `ClusterPoints` | ✅ |
| Point Generate | `pointgenerate` | `PointGenerate` | ✅ |
| Point Jitter | `pointjitter` | `PointJitter` | ✅ |
| Point Relax | `relax` | `PointRelax` | ✅ |
| Point Replicate | `pointreplicate` | `PointReplicate` | ✅ |
| Point Weld | `pointweld` | `PointWeld` | ✅ |
| Points from Volume | `pointsfromvolume` | `PointsFromVolume` | ✅ |
| Scatter | `scatter` | `Scatter` | ✅ |
| Scatter and Align | `scatteralign` | `ScatterAndAlign` | ✅ |

### 10. UV

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| UV Auto Seam | `uvautoseam` | `UVAutoSeam` | ✅ |
| UV Brush | `uvbrush` | `UVBrush` | ✅ |
| UV Edit | `uvedit` | `UVEdit` | ✅ |
| UV Flatten | `uvflatten` | `UVFlatten` | ✅ |
| UV Flatten 3.0 | `uvflatten-3.0` | `UVFlatten` | ✅ |
| UV Flatten from Points | `uvflattenfrompoints` | `UVFlattenFromPoints` | ✅ |
| UV Fuse | `uvfuse` | `UVFuse` | ✅ |
| UV Layout | `uvlayout` | `UVLayout` | ✅ |
| UV Pelt | `uvpelt` | `UVPelt` | ✅ |
| UV Pelt 2.0 | `uvpelt-2.0` | `UVPelt` | ✅ |
| UV Project | `uvproject` | `UVProject` | ✅ |
| UV Relax | `uvrelax` | `UVRelax` | ✅ |
| UV Texture | `texture` | `UVTexture` | ✅ |
| UV Transform | `uvtransform` | `UVTransform` | ✅ |
| UV Transform 2.0 | `uvtransform-2.0` | `UVTransform` | ✅ |
| UV Unwrap | `uvunwrap` | `UVUnwrap` | ✅ |

### 11. 分析工具

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Bound | `bound` | `BoundMesh` | ✅ |
| Connectivity | `connectivity` | `Connectivity` | ✅ |
| Convert | `convert` | `Convert` | ✅ |
| Distance along Geometry | `distancealonggeometry` | `DistanceAlongGeometry` | ✅ |
| Distance from Geometry | `distancefromgeometry` | `DistanceFromGeometry` | ✅ |
| Facet | `facet` | `Facet` | ✅ |
| Find Shortest Path | `findshortestpath` | `FindShortestPath` | ✅ |
| Flatten | `flatten` | `Flatten` | ✅ |
| Intersection Analysis | `intersectionanalysis` | `IntersectionAnalysis` | ✅ |
| Intersection Stitch | `intersectionstitch` | `IntersectionStitch` | ✅ |
| Join | `join` | `Join` | ✅ |
| Match Axis | `matchaxis` | `MatchAxis` | ✅ |
| Match Topology | `matchtopology` | `MatchTopology` | ✅ |
| Measure | `measure` | `MeasureMesh` | ✅ |
| Measure Thickness | `measurethickness` | `MeasureThickness` | ✅ |
| Normal | `normal` | `ComputeNormals` | ✅ |
| Primitive Properties | `primitive` | `PrimitiveProperties` | ✅ |
| Proximity | `proximity` | `Proximity` | ✅ |
| Separate Pieces | `separatepieces` | `SeparatePieces` | ✅ |
| Sort | `sort` | `SortGeometry` | ✅ |
| Winding Number | `windingnumber` | `WindingNumber` | ✅ |

### 12. 体素 / 隐式曲面建模

| Houdini SOP | 内部名 | PCG 节点 | 状态 |
|---|---|---|---|
| Convert VDB | `convertvdb` | `ConvertVDB` | ✅ |
| Implicit Surface | `implicitsurface` | `ImplicitSurface` | ✅ |
| IsoOffset | `isooffset` | `IsoOffset` | ✅ |
| MetaGroups | `metagroups` | `MetaGroups` | ✅ |
| VDB | `vdb` | `VDB` | ✅ |
| VDB from Polygons | `vdbfrompolygons` | `VDBFromPolygons` | ✅ |
| Volume | `volume` | `Volume` | ✅ |
| Volume SDF | `volumesdf` | `VolumeSDF` | ✅ |

---

## 二、PCG-AI 节点 → Houdini 反向对照（305）

| PCG 节点 | 类别 | Houdini SOP |
|---|---|---|
| `AttributeBlur` | Attribute | `attribblur` |
| `AttributeCast` | Attribute | `attribcast` |
| `AttributeCombine` | Attribute | `attribcombine` |
| `AttributeComposite` | Attribute | `attribcomposite` |
| `AttributeCreate` | Attribute | `attribcreate` |
| `AttributeExpression` | Attribute | `attribexpression` |
| `AttributeFade` | Attribute | `attribfade` |
| `AttributeFill` | Attribute | `attribfill` |
| `AttributeFromMap` | Attribute | `attribfrommap` |
| `AttributeFromPieces` | Attribute | `attribfrompieces` |
| `AttributeFromVolume` | Attribute | `attribfromvolume` |
| `AttributeInterpolate` | Attribute | `attribinterpolate` |
| `AttributeMirror` | Attribute | `attribmirror` |
| `AttributeNoise` | Attribute | `attribnoise` |
| `AttributePromote` | Attribute | `attribpromote` |
| `AttributeRemap` | Attribute | `attribremap` |
| `AttributeReorient` | Attribute | `attribreorient` |
| `AttributeSort` | Attribute | `attribsort` |
| `AttributeStringEdit` | Attribute | `attribstringedit` |
| `AttributeSwap` | Attribute | `attribswap` |
| `AttributeTransfer` | Attribute | `attribtransfer` |
| `AttributeVOP` | Attribute | `attribvop` |
| `AttributeWrangle` | Attribute | `attribwrangle` |
| `ExtractCentroid` | Attribute | `extractcentroid` |
| `ExtractTransform` | Attribute | `extracttransform` |
| `Name` | Attribute | `name` |
| `AttributeFilter` | Filter | 组合 |
| `Blast` | Filter | `blast` |
| `Delete` | Filter | `delete` |
| `DensityFilter` | Filter | — |
| `Split` | Filter | `split` |
| `BlockBegin` | Flow | `block_begin` |
| `BlockEnd` | Flow | `block_end` |
| `Cache` | Flow | `cache` |
| `CacheIf` | Flow | `cacheif` |
| `CopyAndTransform` | Flow | `copyxform` |
| `CopyToCurves` | Flow | `copytocurves` |
| `ForEach` | Flow | `foreach` |
| `ForEachBegin` | Flow | `block_begin` |
| `ForEachEnd` | Flow | `block_end` |
| `Instance` | Flow | `instance` |
| `Null` | Flow | `null` |
| `Pack` | Flow | `pack` |
| `Stash` | Flow | `stash` |
| `Switch` | Flow | `switch` |
| `SwitchIf` | Flow | `switchif` |
| `Unpack` | Flow | `unpack` |
| `CircleFromEdges` | Generation | `circlefromedges` |
| `CircleSpline` | Generation | `circlespline` |
| `Cluster` | Generation | `cluster` |
| `ClusterPoints` | Generation | `clusterpoints` |
| `CreatePointGrid` | Generation | `add` / `grid` |
| `CreatePoints` | Generation | `add`（legacy 简化版） |
| `Curve` | Generation | `curve` |
| `DrawCurve` | Generation | `drawcurve` |
| `Font` | Generation | `font` |
| `Line` | Generation | `line` |
| `Metaball` | Generation | `metaball` |
| `PlatonicSolids` | Generation | `platonic` |
| `PointGenerate` | Generation | `pointgenerate` |
| `PointJitter` | Generation | `pointjitter` |
| `PointRelax` | Generation | `relax` |
| `PointReplicate` | Generation | `pointreplicate` |
| `PointWeld` | Generation | `pointweld` |
| `PointsFromVolume` | Generation | `pointsfromvolume` |
| `SampleMeshSurface` | Generation | `scatter` |
| `Scatter` | Generation | `scatter` |
| `ScatterAndAlign` | Generation | `scatteralign` |
| `SpawnPoints` | Generation | `scatter` |
| `Sphere` | Generation | `sphere` |
| `Starburst` | Generation | `starburst` |
| `SuperQuad` | Generation | `superquad` |
| `SurfaceSampler` | Generation | `scatter` |
| `Torus` | Generation | `torus` |
| `Add` | Geometry | `add` |
| `BlastByAttribute` | Geometry | `blastbyattribute` |
| `Convert` | Geometry | `convert` |
| `DistanceAlongGeometry` | Geometry | `distancealonggeometry` |
| `DistanceFromGeometry` | Geometry | `distancefromgeometry` |
| `FaceGroupByNormal` | Geometry | `group`（法线选择） |
| `Facet` | Geometry | `facet` |
| `Flatten` | Geometry | `flatten` |
| `GroupByLasso` | Geometry | `groupbylasso` |
| `GroupByRange` | Geometry | `grouprange` |
| `GroupCombine` | Geometry | `groupcombine` |
| `GroupCopy` | Geometry | `groupcopy` |
| `GroupCreate` | Geometry | `group` |
| `GroupDelete` | Geometry | `groupdelete` |
| `GroupExpand` | Geometry | `groupexpand` |
| `GroupExpression` | Geometry | `groupexpression` |
| `GroupFindPath` | Geometry | `groupfindpath` |
| `GroupFromAttributeBoundary` | Geometry | `groupfromattribboundary` |
| `GroupInvert` | Geometry | `groupinvert` |
| `GroupPaint` | Geometry | `grouppaint` |
| `GroupPromote` | Geometry | `grouppromote` |
| `GroupRename` | Geometry | `grouprename` |
| `GroupTransfer` | Geometry | `grouptransfer` |
| `GroupsFromName` | Geometry | `groupsfromname` |
| `IntersectionAnalysis` | Geometry | `intersectionanalysis` |
| `IntersectionStitch` | Geometry | `intersectionstitch` |
| `Join` | Geometry | `join` |
| `MatchAxis` | Geometry | `matchaxis` |
| `MatchTopology` | Geometry | `matchtopology` |
| `MeasureThickness` | Geometry | `measurethickness` |
| `PrimitiveProperties` | Geometry | `primitive` |
| `Proximity` | Geometry | `proximity` |
| `SeparatePieces` | Geometry | `separatepieces` |
| `SortGeometry` | Geometry | `sort` |
| `WindingNumber` | Geometry | `windingnumber` |
| `GetMeshData` | Input | — |
| `GetSplineData` | Input | — |
| `AssignMaterial` | Material | `material` |
| `Material` | Material | — |
| `VertexColor` | Material | `color` |
| `Assemble` | Mesh | `assemble` |
| `Bend` | Mesh | `bend` |
| `BendMesh` | Mesh | `bend`（子集） |
| `BevelMesh` | Mesh | `polybevel` |
| `BooleanMesh` | Mesh | `boolean` |
| `BoundMesh` | Mesh | `bound` |
| `Bulge` | Mesh | `bulge` |
| `Clean` | Mesh | `clean` |
| `Clip` | Mesh | `clip` |
| `Comb` | Mesh | `comb` |
| `ComputeNormals` | Mesh | `normal` / `facet` |
| `Connectivity` | Mesh | `connectivity` |
| `ConvertVDB` | Mesh | `convertvdb` |
| `Cookie` | Mesh | `cookie` |
| `CopyMesh` | Mesh | `copy` |
| `CopyMeshToPoints` | Mesh | `copytopoints` |
| `Crease` | Mesh | `crease` |
| `CreateBoxMesh` | Mesh | `box` |
| `CreateCylinderMesh` | Mesh | `tube` |
| `CreateGridMesh` | Mesh | `grid` |
| `Dissolve` | Mesh | `dissolve` |
| `Divide` | Mesh | `divide` |
| `EdgeCollapse` | Mesh | `edgecollapse` |
| `EdgeCusp` | Mesh | `edgecusp` |
| `EdgeDivide` | Mesh | `edgedivide` |
| `EdgeEqualize` | Mesh | `edgeequalize` |
| `EdgeFlip` | Mesh | `edgeflip` |
| `EdgeFracture` | Mesh | `edgefracture` |
| `EdgeRelax` | Mesh | `edgerelax` |
| `EdgeStraighten` | Mesh | `edgestraighten` |
| `EdgeTransport` | Mesh | `edgetransport` |
| `Edit` | Mesh | `edit` |
| `Ends` | Mesh | `ends` |
| `Extrude` | Mesh | `extrude` |
| `FindShortestPath` | Mesh | `findshortestpath` |
| `FuseMesh` | Mesh | `fuse` |
| `Hole` | Mesh | `hole` |
| `ImplicitSurface` | Mesh | `implicitsurface` |
| `ImportMesh` | Mesh | `file` |
| `Inset` | Mesh | `inset` |
| `IsoOffset` | Mesh | `isooffset` |
| `LatticeDeform` | Mesh | `lattice` / `latticedeform` |
| `LatticeFromVolume` | Mesh | `latticefromvolume` |
| `LoftMesh` | Mesh | `loft` / `skin` |
| `LotSubdivision` | Mesh | `polyexpand2d`（Labs） |
| `Magnet` | Mesh | `magnet` |
| `MatchSize` | Mesh | `matchsize` |
| `MeasureMesh` | Mesh | `measure` |
| `MergeMesh` | Mesh | `merge` |
| `MeshNoiseDeform` | Mesh | `attribnoise`（子集） |
| `Meshy3DGenerator` | Mesh | — |
| `MeshyMeshOps` | Mesh | — |
| `MeshyRetexture` | Mesh | — |
| `MeshyTextTo3D` | Mesh | — |
| `MetaGroups` | Mesh | `metagroups` |
| `MirrorMesh` | Mesh | `mirror` |
| `OutlineSolid` | Mesh | 组合（轮廓+厚度） |
| `PathDeform` | Mesh | `pathdeform` |
| `Peak` | Mesh | `peak` |
| `PointDeform` | Mesh | `pointdeform` |
| `PolyBridge` | Mesh | `polybridge` |
| `PolyCut` | Mesh | `polycut` |
| `PolyDoctor` | Mesh | `polydoctor` |
| `PolyExpand2D` | Mesh | `polyexpand2d` |
| `PolyExtrude` | Mesh | `polyextrude` |
| `PolyFill` | Mesh | `polyfill` |
| `PolyFrame` | Mesh | `polyframe` |
| `PolyHinge` | Mesh | `polyhinge` |
| `PolyLoft` | Mesh | `polyloft` |
| `PolyPatch` | Mesh | `polypatch` |
| `PolyReduce` | Mesh | `polyreduce` |
| `PolySlice` | Mesh | `polycut` |
| `PolySoup` | Mesh | `polysoup` |
| `PolySpline` | Mesh | `polyspline` |
| `PolySplit` | Mesh | `polysplit` |
| `PolyWire` | Mesh | `polywire` |
| `PrimitiveTransform` | Mesh | `primitive` |
| `QuadRemesh` | Mesh | `quadremesh` |
| `Remesh` | Mesh | `remesh` |
| `RemeshToGrid` | Mesh | `remeshgrid` |
| `ReverseMesh` | Mesh | `reverse` |
| `RevolveMesh` | Mesh | `revolve` |
| `Sculpt` | Mesh | `sculpt` / `sculpt-2.0` |
| `ShellMesh` | Mesh | `polyextrude` |
| `SmoothMesh` | Mesh | `smooth` |
| `SoftTransform` | Mesh | `softxform` |
| `SubdivideMesh` | Mesh | `subdivide` |
| `SurfaceDeform` | Mesh | `surfacedeform` |
| `ThickenMesh` | Mesh | `polyextrude` |
| `TransformByAttribute` | Mesh | `xformbyattrib` |
| `TransformMesh` | Mesh | `xform` |
| `TransformPieces` | Mesh | `xformpieces` |
| `Tripo3DGenerator` | Mesh | — |
| `Unsubdivide` | Mesh | `unsubdivide` |
| `VDB` | Mesh | `vdb` |
| `VDBFromPolygons` | Mesh | `vdbfrompolygons` |
| `Volume` | Mesh | `volume` |
| `VolumeSDF` | Mesh | `volumesdf` |
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
| `CrossSectionSurface` | Spline | `crosssectionsurface` |
| `CurveIntersect` | Spline | `curvesect` |
| `ExtrudeAlongSpline` | Spline | `extrude` |
| `InstanceAlongSpline` | Spline | `copytocurves` |
| `OrientationAlongCurve` | Spline | `orientalongcurve` |
| `PlanarPatch` | Spline | `planarpatch` |
| `PlanarPatchFromCurves` | Spline | `planarpatchfromcurves` |
| `Rails` | Spline | `rails` |
| `Resample` | Spline | — |
| `ResampleSpline` | Spline | `resample` |
| `SampleAlongSpline` | Spline | `resample` + `copy`（组合） |
| `Skin` | Spline | `skin` |
| `SplineAlign` | Spline | `align` |
| `SplineBasis` | Spline | `basis` |
| `SplineCap` | Spline | `cap` |
| `SplineClay` | Spline | `clay` |
| `SplineCreep` | Spline | `creep` |
| `SplineCurveClay` | Spline | `curveclay` |
| `SplineFillet` | Spline | `fillet` |
| `SplineFit` | Spline | `fit` |
| `SplineProfile` | Spline | `profile` |
| `SplineProject` | Spline | `project` |
| `SplineRound` | Spline | `round` |
| `SplineSurfsect` | Spline | `surfsect` |
| `SplineTrim` | Spline | `trim` |
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
| `UVAutoSeam` | UV | `uvautoseam` |
| `UVBrush` | UV | `uvbrush` |
| `UVEdit` | UV | `uvedit` |
| `UVFlatten` | UV | `uvflatten` / `uvflatten-3.0` |
| `UVFlattenFromPoints` | UV | `uvflattenfrompoints` |
| `UVFuse` | UV | `uvfuse` |
| `UVLayout` | UV | `uvlayout` |
| `UVPelt` | UV | `uvpelt` / `uvpelt-2.0` |
| `UVProject` | UV | `uvproject` |
| `UVRelax` | UV | `uvrelax` |
| `UVTexture` | UV | `texture` |
| `UVTransform` | UV | `uvtransform` / `uvtransform-2.0` |
| `UVUnwrap` | UV | `uvunwrap` |

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

## 四、本轮对齐结果

| 项目 | 结果 |
|---|---|
| 基础建模 SOP 覆盖 | 237 / 237，全部具备独立 PCG 映射 |
| 新增兼容节点 | 170；另将 `PointRelax`、`UVTexture` 补齐到 Houdini 参数集 |
| Houdini 参数 | 172 个节点类型、2,887 个 SideFX 官方帮助页派生参数；保留原始标签、顺序、枚举与参数文件夹 |
| Web | 节点选择器、参数排序、条件显示/禁用、折叠文件夹与页签均由共享 manifest 驱动 |
| Unity / Tuanjie | 本轮暂不改动；待 Web 前端完成并验收后再同步共享 manifest 与 Inspector |
| Core cook | 304 个 manifest cook 节点均已注册；新增 SOP 兼容层覆盖生成、变形、拓扑、组、属性、流程、曲线、点、UV、分析与体素类别 |

> “✅”表示本文基础建模范围内已有独立节点、官方主参数和可执行基础行为；Houdini 中依赖专有求解器的高级子模式采用确定性的 PCG 兼容实现，不承诺与 SideFX 数值结果逐顶点完全一致。

---

## 参考

- [SideFX Geometry (SOP) nodes](https://www.sidefx.com/docs/houdini/nodes/sop/index.html)
- [PCG-AI node-manifest.json](../schema/node-manifest.json)
- [PCG-AI node-reference.md](./node-reference.md)
- [brickify-tool showcase](../examples/showcases/brickify-tool/README.md)
