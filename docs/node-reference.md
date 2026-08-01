# PCG 节点参考手册

本文档详细说明 `schema/node-manifest.json`（v1.5）中定义的全部 **119 种** PCG 节点。

每个节点包含：功能描述、输入/输出 Pin、属性表、执行逻辑和用法示例。

---

## 目录

- [Pin 数据类型](#pin-数据类型)
- [Geometry 数据契约](#geometry-数据契约)
- [Terrain 类别](#terrain-类别)
  - [HeightField](#heightfield)
  - [HeightFieldNoise](#heightfieldnoise)
  - [HeightFieldMaskNoise](#heightfieldmasknoise)
  - [HeightFieldMaskByFeature](#heightfieldmaskbyfeature)
  - [HeightFieldMaskByObject](#heightfieldmaskbyobject)
  - [HeightFieldPattern](#heightfieldpattern)
  - [HeightFieldClip](#heightfieldclip)
  - [HeightFieldTerrace](#heightfieldterrace)
  - [HeightFieldBlur](#heightfieldblur)
  - [HeightFieldResample](#heightfieldresample)
  - [HeightFieldLayer](#heightfieldlayer)
  - [HeightFieldCopyLayer](#heightfieldcopylayer)
  - [HeightFieldLayerClear](#heightfieldlayerclear)
  - [HeightFieldLayerProperties](#heightfieldlayerproperties)
  - [HeightFieldIsolateLayer](#heightfieldisolatelayer)
  - [HeightFieldErode](#heightfielderode)
  - [HeightFieldFlowField](#heightfieldflowfield)
  - [HeightFieldSlump](#heightfieldslump)
  - [HeightFieldDistortByNoise](#heightfielddistortbynoise)
  - [HeightFieldProject](#heightfieldproject)
  - [HeightFieldScatter](#heightfieldscatter)
  - [HeightFieldFile](#heightfieldfile)
  - [ConvertHeightField](#convertheightfield)
- [Input 类别](#input-类别)
  - [GetTerrainData](#getterraindata)
  - [GetMeshData](#getmeshdata)
  - [GetSplineData](#getsplinedata)
- [Generation 类别](#generation-类别)
  - [SpawnPoints](#spawnpoints)
  - [CreatePointGrid](#createpointgrid)
  - [CreatePoints](#createpoints)
  - [SurfaceSampler](#surfacesampler)
  - [SampleMeshSurface](#samplemeshsurface)
- [Filter 类别](#filter-类别)
  - [DensityFilter](#densityfilter)
  - [AttributeFilter](#attributefilter)
  - [Delete](#delete)
  - [Split](#split)
  - [Blast](#blast)
- [Attribute 类别](#attribute-类别)
  - [AttributeWrangle](#attributewrangle)
  - [AttributeTransfer](#attributetransfer)
- [Transform 类别](#transform-类别)
  - [TransformPoints](#transformpoints)
  - [ProjectPoints](#projectpoints)
- [Sampler 类别](#sampler-类别)
  - [SampleSurface](#samplesurface)
- [Metadata 类别](#metadata-类别)
  - [CopyAttributes](#copyattributes)
  - [DeleteAttributes](#deleteattributes)
  - [BreakAttributes](#breakattributes)
- [Spawner 类别](#spawner-类别)
  - [PlaceInScene](#placeinscene)
  - [StaticMeshSpawner](#staticmeshspawner)
  - [MergeSpawnPoints](#mergespawnpoints)
- [Spline 类别](#spline-类别)
  - [CreateSpline](#createspline)
  - [CreateBezierSpline](#createbezierspline)
  - [ResampleSpline](#resamplespline)
  - [ConditionOutline](#conditionoutline)
  - [SampleAlongSpline](#samplealongspline)
  - [SweepAlongSpline](#sweepalongspline)
  - [ExtrudeAlongSpline](#extrudealongspline)
  - [CrossSectionProfile](#crosssectionprofile)
  - [InstanceAlongSpline](#instancealongspline)
  - [CreateSpiralSpline](#createspiralspline)
  - [CreateArcSpline](#createarcspline)
- [Structural 类别](#structural-类别)
  - [ConvexHull](#convexhull)
  - [ConnectNearest](#connectnearest)
  - [Delaunay](#delaunay)
  - [MST](#mst)
  - [Voronoi](#voronoi)
  - [AStarPathfinding](#astarpathfinding)
- [Mesh 类别](#mesh-类别)
  - [CreateGridMesh](#creategridmesh)
  - [CreateBoxMesh](#createboxmesh)
  - [SubdivideMesh](#subdividemesh)
  - [BevelMesh](#bevelmesh)
  - [MeshNoiseDeform](#meshnoisedeform)
  - [TransformMesh](#transformmesh)
  - [LoftMesh](#loftmesh)
  - [MirrorMesh](#mirrormesh)
  - [FuseMesh](#fusemesh)
  - [PolyExtrude](#polyextrude)
  - [LotSubdivision](#lotsubdivision)
  - [CopyMesh](#copymesh)
  - [ShellMesh](#shellmesh)
  - [ImportMesh](#importmesh)
  - [MatchSize](#matchsize)
  - [BendMesh](#bendmesh)
  - [MergeMesh](#mergemesh)
  - [BooleanMesh](#booleanmesh)
  - [OutlineSolid](#outlinesolid)
  - [CreateCylinderMesh](#createcylindermesh)
  - [RevolveMesh](#revolvemesh)
- [Geometry 类别](#geometry-类别)
  - [GroupCreate](#groupcreate)
  - [GroupCombine](#groupcombine)
  - [GroupPromote](#grouppromote)
  - [FaceGroupByNormal](#facegroupbynormal)
- [Texture 类别](#texture-类别)
  - [ImageTexture](#imagetexture)
- [建筑生成核心节点](#建筑生成核心节点)
  - [CopyMeshToPoints](#copymeshtopoints)
  - [AttributeRandomize](#attributerandomize)
  - [Switch](#switch)
- [Output 类别](#output-类别)
  - [Output](#output)
  - [ExportFBX](#exportfbx)
- [Material 类别](#material-类别)
  - [VertexColor](#vertexcolor)
  - [AssignMaterial](#assignmaterial)
- [UV 类别](#uv-类别)
  - [UVTexture](#uvtexture)
  - [ProjectTexture](#projecttexture)
  - [Output](#output)
- [常见节点组合](#常见节点组合)
- [示例 .pcg 文件](#示例-pcg-文件)
  - [PCGDemo](#pcgdemo)
  - [Test](#test)

---
  - [ForEachBegin](#foreachbegin)
  - [ForEachEnd](#foreachend)
  - [PrimitiveTransform](#primitivetransform)
  - [ConvertLine](#convertline)
  - [ExtractCentroid](#extractcentroid)
  - [GroupTransfer](#grouptransfer)
  - [Clip](#clip)

## Pin 数据类型

节点间的连线（Pin）遵循以下数据类型约束，与 UE EPCGDataType 对齐：

| Pin 类型 | 说明 | 对应 C++ 数据结构 |
|----------|------|-------------------|
| `Param` | 参数对象（JSON 键值对） | `PcgParamData` / `nlohmann::json` |
| `SpatialPoint` | 空间点云（含坐标和属性） | `PcgPointData` → `PcgPoint{x, y, z, attributes}` |
| `SpatialSpline` | 样条/线段集合 | `PcgSplineData` → `PcgSpline{points[], closed}` |
| `SpatialMesh` | Houdini 风格多边形 Geometry；Sink 时才三角化 | `PcgGeometry` → points / vertices / primitives / detail attributes + groups；兼容旧 `PcgMeshData` 输入 |
| `Any` | 任意类型透传 | — |
| `Texture` | 纹理数据（2D 图像） | `PcgTextureData` → `width, height, channels, data[]` |
| `HeightField` | 命名层 2D volume（`height`/`mask`/自定义层） | `PcgHeightField` → grid transform + `PcgHeightFieldLayer[]` |

> **连接规则**：输出 Pin 类型必须与输入 Pin 类型匹配。`Any` 类型可接受任意输入。

Node Manifest 是 Pin id、类型和 variadic 基数的唯一契约源。Core 在执行前验证 handle、类型、重复边及非 variadic 多重输入；Web 与 Unity 导入/导出均保留 Graph v1/v2 的 typed edge 元数据。Graph v2 的 parameters / subgraphs 同样跨端保留；Web 当前只对嵌套 subgraph 定义做无损导入与回写，不在本批增加子图内部编辑 UI。

## Geometry 数据契约

`SpatialMesh` 链路以 `PcgGeometry` 为 source of truth，节点中途不得通过 `PcgMeshData` 三角汤往返。数据域与 Houdini 对齐为：

| Owner / Domain | 元素含义 | 稳定索引 |
|----------------|----------|----------|
| Point | 可被多个面角共享的位置 | point index |
| Vertex | 某个 primitive 的 face-corner | 展平 corner index |
| Primitive | polygon / face | face index |
| Detail | 整份 geometry | 单元素 |

通用属性由 schema（name、owner、type、tuple size、default、transform role）与等长数组组成。`Position` / `Vector` / `Normal` role 会在 Match Size、Bend 等变换节点中分别按点、向量、逆转置法线语义处理。Group 同样支持 point / vertex / primitive(face) / edge；edge member 使用两个 32-bit point index 打包成 64-bit `GroupId`，不再依赖点数阈值或十进制乘数。

改变拓扑的节点必须提供 destination→source remap，再由统一传播层处理 attributes、UV、颜色、材质和 groups；无法映射的新元素使用属性默认值。Geometry Binary v3 会序列化上述通用属性及 64-bit group member；v2 reader 保持向后兼容，未知 chunk 可由旧宿主跳过。

---

## Terrain 类别

### HeightField

**功能**：创建 Houdini 风格的 typed 2D HeightField，同时生成标量 `height` 与 `mask` 命名层。Unity 路径默认使用 Corner sampling；Houdini 原生默认是 Center sampling。

**输出 Pin**：`out`（`HeightField`）

| 属性名 | 默认值 | 说明 |
|--------|--------|------|
| `orientation` | `zx` | 栅格平面；`zx` 为 Unity 地面，位移沿 Y |
| `sampling` | `corner` | `corner` 或 `center`；决定 sample index 与世界坐标的半格偏移 |
| `divisionMode` | `bySize` | `bySize` 使用 `gridSpacing`；`byAxis` 使用最长轴 `gridSamples` |
| `gridSpacing` | 1.0 | 相邻采样点的目标间距（米） |
| `gridSamples` | 257 | By Axis 最长轴采样数 |
| `sizeX`, `sizeZ` | 256 | HeightField 平面尺寸（米） |
| `centerX/Y/Z` | 0 | 栅格中心；ZX 时 `centerY` 是基准高度 |
| `initialHeight`, `initialMask` | 0 | 两个默认层的初值 |

Corner sampling 下，`size=256`、`gridSpacing=1` 会生成 257×257 个样本，适合 Unity 的 `2^n+1` 高度图约束。所有命名层共享同一栅格变换；层结构从第一版即保留 tuple size，以支持后续 `flowdir` 向量层。

### HeightFieldNoise

**功能**：向指定 HeightField 层增加垂直分形噪声。第一个输入是被修改的 HeightField；可选第二输入是 mask HeightField，`maskLayer` 指定其中用于缩放效果的层。

**输入 Pin**：`in`（`HeightField`）、`mask`（可选 `HeightField`）
**输出 Pin**：`out`（`HeightField`）

| 属性名 | 默认值 | 说明 |
|--------|--------|------|
| `noiseLayer` | `height` | 被修改的标量层 |
| `maskLayer` | `mask` | 第二输入中的遮罩层；0 不生效、1 完全生效 |
| `noiseType` | `perlin` | L0 公开子集；与 `fractal` 分开 |
| `fractal` | `terrain` | `none` / `standard` / `terrain` / `hybridTerrain` |
| `centerNoise` | true | true 输出围绕 0，适合叠加地形 |
| `amplitude` | 30 | 垂直位移幅度（米） |
| `elementSize` | 64 | 主要地貌特征尺寸（米） |
| `scaleX/Z`, `offsetX/Z` | 1 / 0 | 噪声空间缩放与平移 |
| `maxOctaves` | 5 | 分形 octave 上限（1–12） |
| `lacunarity`, `roughness` | 2.0 / 0.5 | 频率递增与幅度衰减 |
| `seed` | 0 | PCG 确定性扩展；用于稳定空间噪声，不伪装成 SideFX 原生参数 |

L0 对齐的是 SideFX 节点名、输入、命名层和参数语义，不承诺与 SideFX 私有噪声实现逐 voxel 相同。建议至少串联一次宏观 Noise 与一次低振幅细节 Noise。

### HeightFieldMaskNoise

**功能**：生成 0–1 为主的分形噪声并写入 `mask` 或任意标量层；可选第二输入继续遮罩本节点效果。

**输入 Pin**：`in`、可选 `mask`（均为 `HeightField`）
**输出 Pin**：`out`（`HeightField`）

核心属性为 `outputLayer`、`maskLayer`、`combine`、`blend`、`invert`，以及与 `HeightFieldNoise` 同构的 Noise Type / Fractal / Element Size / Scale / Offset / Octave 参数。`combine` 支持 Replace、Add、Subtract、Difference、Multiply、Maximum、Minimum、Blend。Mask 默认不居中，噪声先映射到 `[0,1]`。

### HeightFieldMaskByFeature

**功能**：从高度与坡度生成特征 mask；同时启用时取条件交集，未启用任何条件时按 SideFX 语义填充为 1。

**输入 Pin**：`in`、可选 `mask`（`HeightField`）
**输出 Pin**：`out`（`HeightField`）

| 属性 | 说明 |
|------|------|
| `maskByHeight`, `minHeight`, `maxHeight`, `heightFeather` | 高度范围与线性平滑边缘 |
| `maskBySlope`, `minSlopeAngle`, `maxSlopeAngle`, `slopeFeather` | 由世界米制梯度计算 0–90° 坡度范围 |
| `smoothRadius` | 输出 mask 的采样格半径平滑 |
| `combine`, `blend`, `invert` | 与已有输出层的组合方式 |

L1 暂不暴露 SideFX 的任意 Ramp、curvature、direction 和 occlusion，避免把固定函数误称为完整节点。

### HeightFieldClip

**功能**：把 `heightLayer` 限制在可选最小/最大高度；输出 `mesa`（被裁剪区域）与 `cliffs`（边界）层，也可把其中一层复制为 `mask`。

**输入 Pin**：`in`、可选 `mask`；**输出 Pin**：`out`（`HeightField`）

核心属性：`minClipEnabled/minClip`、`maxClipEnabled/maxClip`、`edgeMaskRadius`、`generateMaskFrom`、`outputClippedLayer`、`outputEdgeLayer`。当前子集为硬裁剪；Soft Clip 留待后续参数映射。

### HeightFieldTerrace

**功能**：在指定高度范围内创建阶梯平原，输出 `mesa` 与 `cliffs` 命名层。

**输入 Pin**：`in`、可选 `mask`；**输出 Pin**：`out`（`HeightField`）

核心属性：`minHeight/maxHeight`、`fade`（0 为完整台阶，1 为原地形）、`maxStepSize`（米）、`stepOffset`、`smoothEdges`。L1 实现固定步长与平滑阶沿；SideFX 的可变 Step Ramp、Fade Ramp 和 Undulations 属于后续扩展。

### HeightFieldBlur

**功能**：平滑 `height`、`mask` 或任意标量层。半径使用米；可选第二输入控制生效区域。

**输入 Pin**：`in`、可选 `mask`；**输出 Pin**：`out`（`HeightField`）

`method` 支持可分离 Gaussian 与 Box；`iterations` 增强平滑，`radius` 是米制半径。卷积尊重层的 Constant / Repeat / Streak border policy。Expand、Shrink、Sharpen 留待后续。

### HeightFieldResample

**功能**：保持尺寸、中心、朝向、Sampling、所有命名层和 tuple size，改变 HeightField 分辨率。

**输入 Pin**：`in`；**输出 Pin**：`out`（`HeightField`）

`specifyExactResolution=false` 时使用 `resolutionScale`；启用时按 `divisionMode`、`gridSamples` 或 `gridSpacing` 求新栅格。所有 tuple 分量按世界坐标双线性采样。L1 公开 Bilinear 子集，其他 SideFX Filter/Filter Scale 尚未暴露。

### HeightFieldLayer

**功能**：SideFX 风格三输入合成：`base` + `layer` + 可选 `mask`。第二输入始终重采样到 base grid，输出继承 base 的栅格变换。

**输入 Pin**：`base`、`layer`、可选 `mask`（`HeightField`）
**输出 Pin**：`out`（`HeightField`）

`layerMode` 支持 Replace/Add/Subtract/Multiply/Maximum/Minimum/Blend；`layers` 是 `*` 或空格分隔层名。`maskStrength/invertMask` 控制合成区域，Base/Layer/Final Offset+Scale 与可选 Min/Max Clamp 用于完整的合成前后重映射。tuple size 不匹配会显式失败，不做静默降维。

### HeightFieldErode

**功能**：确定性的水力 + 热力侵蚀核心，保持 typed HeightField 并写出 Houdini 工作流常用的 `sediment`、`debris`、`flow` 与二维 `flowdir` 命名层。可选第二输入控制侵蚀区域。

**输入 Pin**：`in`、可选 `mask`（`HeightField`）
**输出 Pin**：`out`（`HeightField`）

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `iterations`, `seed` | 30 / 0 | 模拟迭代与确定性降雨扰动 |
| `rainfallCoverage`, `flowForce` | 0.04 / 1.0 | 每轮加水量与下坡输运比例 |
| `erodability`, `erosionRate` | 1.0 / 0.18 | 基岩可侵蚀性与水力切削速率 |
| `sedimentCapacity`, `depositionRate`, `evaporationRate` | 1.4 / 0.12 / 0.12 | 携沙容量、沉积与蒸发 |
| `weatheringForce`, `cutAngle`, `reposeAngle` | 0.08 / 35° / 28° | 热力风化、切削角与安息角 |
| `addDebrisToHeight`, `addSedimentToHeight` | true / true | 是否把松散物/沉积反馈到最终高度 |
| `debrisLayer`, `sedimentLayer`, `flowLayer`, `flowDirectionLayer` | 见属性名 | 输出层命名 |

水力阶段在 8 邻域中按最大下坡输运水和携沙，根据容量差进行侵蚀/沉积；热力阶段把超过 Cut Angle 的物质搬运到低处，并按 Repose Angle 继续滑移。`flowdir` 是归一化的平面二维向量层。该节点对齐 SideFX 的两类模拟与输出层契约，但属于 CPU 确定性核心子集，不声称复刻 SideFX OpenCL 求解器、Freeze at Frame、侵蚀 bedrock/debris 各向异性、riverbed/riverbank 等全部高级参数。

### HeightFieldDistortByNoise

**功能**：通过噪声向量场反向追踪并搬移已有层，打破规则边缘；与 HeightField Noise 的“修改高度值”不同，本节点重采样输入内容。

**输入 Pin**：`in`、可选 `mask`（`HeightField`）；**输出 Pin**：`out`（`HeightField`）

`distortLayers` 是 `*` 或空格分隔层名；`noiseType` 支持 Simplex 语义的双通道梯度场与 Curl 场。`amplitude` 和 `elementSize` 均使用米，`substeps` 把总位移拆为多次 advection，较大振幅时可减少折叠。Scale、Offset、Roughness、Max Octaves 与 Seed 控制空间场。所有 tuple 分量一起搬移，且遵守各层 border policy。当前噪声核不承诺与 SideFX Simplex 逐 voxel 相同。

### HeightFieldProject

**功能**：把第二输入的多边形几何沿 HeightField 法线方向投影到指定高度层。这是 geometry → HeightField，不能与 points → terrain 的 `ProjectPoints` 混用。

**输入 Pin**：`heightfield`（`HeightField`）、`geometry`（`SpatialMesh`）；**输出 Pin**：`out`（`HeightField`）

`hitFarthest=true` 选择法线方向最高交点，通常与 `combineMethod=maximum` 组合抬升山体/建筑；关闭后选择最低交点，通常与 Minimum 组合压出谷地。Combine 支持 Replace/Add/Maximum/Minimum，`maxRayDistance` 限制相对原高度的投影距离。实现先保留 polygon 投影核心；SideFX 的 Mask Mode、supersampling/jitter 和 ray combiner 属于后续子集。

### HeightFieldScatter

**功能**：从 HeightField 表面生成确定性点云。`scatterAmountLayer` 必须指向存在且有正值的标量层；空层按官方语义输出零点。

**输入 Pin**：`in`（`HeightField`）；**输出 Pin**：`out`（`SpatialPoint`）

开启 `useExactPointCount` 时使用 `pointCount`；否则 `density` 表示每平方米点数，并按 mask 权重与坡面面积估算数量。`maxPoints` 是硬上限，`globalSeed` 保证复现，`candidatesPerPoint` 以 best-candidate 方式降低局部团簇。输出点贴合双线性高度并携带 `nx/ny/nz`、`u/v`、`height`、`density`。SideFX 的多轮 Relax/半径参数当前由轻量 best-candidate 子集替代。

### HeightFieldMaskByObject

**功能**：把第二输入几何投影到 HeightField，生成/合成 `mask`（或指定输出层）。对齐 SideFX HeightField Mask by Geometry（`heightfield_maskbyobject`）的 Project 路径；Fog/SDF Volume 方法未实现。

**输入 Pin**：`in`（`HeightField`）、`geometry`（`SpatialMesh`）；**输出 Pin**：`out`（`HeightField`）

`maskingByGeometry` 控制 Above / Below / Either；`combine` 与既有 mask 合成；可选 `blurRadius` 羽化边缘。

### HeightFieldPattern

**功能**：在指定层写入程序化图案位移（Ramp / Exponential Ramp / Step / Stripes）。对齐 SideFX HeightField Pattern 的常用子集；Stars / Voronoi / Distortion 未纳入本版。

**输入 Pin**：`in`、可选 `mask`；**输出 Pin**：`out`

`patternLayer` 默认 `height`；也常写到 `mask` 做梯度蒙版。`size` / `rotate` / `center*` / `phase` 控制图案摆放。

### HeightFieldFlowField

**功能**：降雨后沿地形下坡输运，累积 `flow` / `flowdir` / `water`；可选 `copyToMask` 与 `adjustHeight` 挖槽。对齐 SideFX HeightField Flow Field 的核心参数（Smooth/Granular、Rain、Spread/Smoothing Iterations）。

**输入 Pin**：`in`；**输出 Pin**：`out`

### HeightFieldSlump

**功能**：按休止角把松散物质（默认 `debris`）滑移到更稳构型，可选写回流场。对齐 SideFX HeightField Slump 的 Smooth/Granular 核心；高级河床侵蚀参数未纳入。

**输入 Pin**：`in`、可选 `mask`；**输出 Pin**：`out`

### HeightFieldCopyLayer

**功能**：把源层复制到目标层（可只建层不拷数据）。对齐 SideFX HeightField Copy Layer。

### HeightFieldLayerClear

**功能**：把指定层填成常量。对齐 SideFX HeightField Layer Clear。

### HeightFieldLayerProperties

**功能**：设置层的 border 类型/常量。对齐 SideFX HeightField Layer Properties。

### HeightFieldIsolateLayer

**功能**：把指定层拷到 `mask`（和/或覆盖 `height`）以便预览。对齐 SideFX HeightField Isolate Layer。

### HeightFieldFile

**功能**：从磁盘载入栅格创建 HeightField。对齐 SideFX HeightField File 的 Size/Scale/Clamp/Sampling 契约；当前读取 **PGM** 与 **float32 raw**（`.raw`/`.r32`/`.f32`，需 `rawResolutionX/Z`），不依赖 COP。

**输入 Pin**：无；**输出 Pin**：`out`

### ConvertHeightField

**功能**：把 HeightField 的指定高度层转换为共享顶点的四边形 `PcgGeometry`。转换是 HeightField→Polygon 的边界；上游 HeightField 节点不会做 geometry→mesh 往返。

**输入 Pin**：`in`（`HeightField`）
**输出 Pin**：`out`（`SpatialMesh`，内部保持 `PcgGeometry` 到 Sink）

| 属性名 | 默认值 | 说明 |
|--------|--------|------|
| `heightLayer` | `height` | 转换的标量高度层 |
| `density` | 1.0 | 输出分辨率/输入分辨率比例 |

输出自带平滑着色策略和归一化 UV0；Sink 才三角化。L0 只覆盖 Polygon surface，SideFX 的 Polygon Soup、VDB、Extrude Base、Bake Point Colors 留在后续阶段。

典型质量链：

```text
HeightField → HeightFieldNoise (macro) → HeightFieldNoise (detail)
            → HeightFieldMaskByFeature → HeightFieldTerrace → HeightFieldBlur
            → HeightFieldDistortByNoise → HeightFieldErode
            → ConvertHeightField → Output
```

可控山体 / 河流 / 植被蒙版链：

```text
HeightField → HeightFieldPattern / HeightFieldProject / HeightFieldMaskByObject
            → HeightFieldFlowField (copyToMask + adjustHeight)
            → HeightFieldSlump → HeightFieldScatter → CopyMeshToPoints
```

官方语义参考：[HeightField](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield.html)、[Mask by Feature](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_maskbyfeature.html)、[Clip](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_clip)、[Terrace](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_terrace.html)、[Blur](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_blur.html)、[Resample](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_resample.html)、[Layer](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_layer.html)、[Distort by Noise](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_distort.html)、[Erode](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_erode.html)、[Project](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_project.html)、[Scatter](https://www.sidefx.com/docs/houdini/nodes/sop/heightfield_scatter-.html)、[Convert HeightField](https://www.sidefx.com/docs/houdini/nodes/sop/convertheightfield.html)、[Unity Terrain sampling](https://www.sidefx.com/docs/houdini/unity/terrain/basics.html)。

---

## Input 类别

### GetTerrainData

**类别**：Input / Sampler

**功能**：旧图兼容节点。保留原参数和噪声外观，但内部输出 typed `PcgHeightField`（`height` + 零 `mask`），供 `ProjectPoints`、`SampleSurface` 采样。新图优先使用 `HeightField` + `HeightFieldNoise`。

**输入 Pin**：无

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Terrain | `HeightField` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `gridSize` | integer | 32 | ≥ 2, ≤ 256 | 地形网格分辨率（gridSize × gridSize 个采样点） |
| `cellSize` | number | 2.0 | ≥ 0 | 每个网格单元的世界空间尺寸 |
| `amplitude` | number | 5.0 | — | 噪声高度振幅 |
| `seed` | integer | 42 | — | 噪声种子（缺省取 `graph_seed`） |

**执行逻辑**：
1. 在 `gridSize × gridSize` 网格上逐点计算 `simple_noise(wx, wz, seed) * amplitude`
2. 世界坐标 `wx = (x - gridSize/2) * cellSize`，`wz` 同理
3. 把结果写入 `PcgHeightField.height`，同时创建零值 `mask` 层；不再把扁平 `heights[]` 当终态

**用法示例**：

```json
{
  "id": "terrain",
  "type": "GetTerrainData",
  "position": { "x": 0, "y": 200 },
  "data": { "gridSize": 64, "cellSize": 1.5, "amplitude": 8.0, "seed": 42 }
}
```

> 通常连接到 `ProjectPoints` 或 `SampleSurface` 的 `terrain` 输入。

---

### GetMeshData

**类别**：Input

**功能**：从外部数据源获取网格数据。支持三种来源：Binding（绑定键）、Self（节点自身）、Asset（资源路径）。

**输入 Pin**：无

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `source` | enum | `"Binding"` | 数据来源：`Binding`（绑定键）/ `Self`（自身）/ `Asset`（资源路径） |
| `bindingKey` | string | `"targetMesh"` | Binding 模式下的绑定键名 |
| `meshAsset` | string | `""` | Asset 模式下的网格资源路径 |

**执行逻辑**：
1. 根据 `source` 选择数据获取方式
2. `Binding`：从执行上下文的绑定表中查找 `bindingKey` 对应的网格数据
3. `Self`：从节点自身绑定的组件获取网格
4. `Asset`：从 `meshAsset` 指定的资源路径加载网格
5. 输出 `SpatialMesh` 类型数据

**用法示例**：

```json
{
  "id": "getmesh",
  "type": "GetMeshData",
  "position": { "x": 0, "y": 0 },
  "data": { "source": "Binding", "bindingKey": "targetMesh" }
}
```

> 通常连接到 `SampleMeshSurface`、`TransformMesh` 等下游 mesh 处理节点。

---

### GetSplineData

**类别**：Input

**功能**：从外部数据源获取样条数据。支持两种来源：Binding（绑定键）和 Self（节点自身）。

**输入 Pin**：无

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Spline | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `source` | enum | `"Binding"` | 数据来源：`Binding`（绑定键）/ `Self`（自身） |
| `bindingKey` | string | `"bridgePath"` | Binding 模式下的绑定键名 |

**执行逻辑**：
1. 根据 `source` 选择数据获取方式
2. `Binding`：从执行上下文的绑定表中查找 `bindingKey` 对应的样条数据
3. `Self`：从节点自身绑定的组件获取样条
4. 输出 `SpatialSpline` 类型数据

**用法示例**：

```json
{
  "id": "getspline",
  "type": "GetSplineData",
  "position": { "x": 0, "y": 0 },
  "data": { "source": "Binding", "bindingKey": "bridgePath" }
}
```

> 通常连接到 `ResampleSpline`、`SampleAlongSpline`、`SweepAlongSpline` 等下游样条处理节点。

---

## Generation 类别

### SpawnPoints

**类别**：Generation

**功能**：在圆形区域内生成随机空间点。点的数量由 `count` 直接控制，位置在指定半径内呈环形分布。随机种子来自图级 `graph_seed`。

**输入 Pin**：无

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `count` | integer | 100 | ≥ 0, ≤ 10000 | 点数量 |
| `radius` | number | 10.0 | ≥ 0 | 点分布的圆形半径 |

**执行逻辑**：
1. 读取 `count` 和 `radius`
2. 使用线性同余 RNG（种子 = `graph_seed ^ (graph_seed * 2654435761)`）生成随机角度和半径
3. 每个点位置：角度 = `i/count × 2π + 随机扰动`，径向 = `radius × (0.25 + 随机 × 0.75)`
4. 点坐标 Y = 0（水平面）
5. 输出 `SpatialPoint` 类型点云

**用法示例**：

```json
{
  "id": "spawn",
  "type": "SpawnPoints",
  "position": { "x": 300, "y": 0 },
  "data": { "count": 200, "radius": 15.0 }
}
```

> 典型连接：`SpawnPoints → PlaceInScene`

---

### CreatePointGrid

**类别**：Generation

**功能**：生成规则网格状点阵。在 XZ 平面上以指定间距排列点，网格居中于原点。支持合并上游输入点。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Points | `SpatialPoint` | 可选。上游点会与新网格点合并 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `pointCountX` | integer | 10 | ≥ 1, ≤ 256 | X 轴方向点数 |
| `pointCountY` | integer | 10 | ≥ 1, ≤ 256 | Z 轴方向点数 |
| `spacing` | number | 2.0 | ≥ 0 | 网格间距 |

**执行逻辑**：
1. 若有上游输入点，先合并
2. 以原点为中心生成 `countX × countY` 网格：起点 `origin = -((count-1) × spacing) / 2`
3. 每个点坐标 `(origin_x + x × spacing, 0, origin_z + y × spacing)`
4. 输出合并后的点云

**用法示例**：

```json
{
  "id": "grid",
  "type": "CreatePointGrid",
  "position": { "x": 300, "y": 0 },
  "data": { "pointCountX": 20, "pointCountY": 20, "spacing": 1.5 }
}
```

---

### CreatePoints

**类别**：Generation

**功能**：在指定坐标位置生成一个或多个点，可选随机抖动。支持合并上游输入点。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Points | `SpatialPoint` | 可选。上游点会与新点合并 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `x` | number | 0 | — | 点的 X 坐标 |
| `y` | number | 0 | — | 点的 Y 坐标 |
| `z` | number | 0 | — | 点的 Z 坐标 |
| `count` | integer | 1 | ≥ 1, ≤ 1000 | 生成点数量 |
| `jitter` | number | 0 | ≥ 0 | 随机抖动幅度，0 表示无抖动 |

**执行逻辑**：
1. 若有上游输入点，先合并
2. 生成 `count` 个点，每个点位置为 `(x + 随机抖动, y, z + 随机抖动)`
3. RNG 种子 = `mix_seed(graph_seed, (int)(x * 17 + z * 31))`
4. 输出合并后的点云

**用法示例**：

```json
{
  "id": "pt",
  "type": "CreatePoints",
  "position": { "x": 300, "y": 100 },
  "data": { "x": 5.0, "y": 0, "z": 3.0, "count": 1, "jitter": 0 }
}
```

---

### SurfaceSampler

**类别**：Generation

**功能**：在指定范围内均匀采样生成网格状点。与 `CreatePointGrid` 类似但使用 `extent` 和 `subdivisions` 控制覆盖范围。支持合并上游输入点。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Points | `SpatialPoint` | 可选。上游点会与新采样点合并 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `subdivisions` | integer | 8 | ≥ 2, ≤ 128 | 每个轴向的细分数，总点数 = `subdivisions²` |
| `extent` | number | 10.0 | ≥ 0 | 采样范围（从 `-extent/2` 到 `+extent/2`） |

**执行逻辑**：
1. 若有上游输入点，先合并
2. 计算步长 `step = subdivisions ≤ 1 ? extent : extent / (subdivisions - 1)`
3. 在 `subdivisions × subdivisions` 网格上生成点，坐标 `(ix × step - extent/2, 0, iz × step - extent/2)`
4. 输出合并后的点云

**用法示例**：

```json
{
  "id": "sampler",
  "type": "SurfaceSampler",
  "position": { "x": 300, "y": 0 },
  "data": { "subdivisions": 16, "extent": 20.0 }
}
```

---

### SampleMeshSurface

**类别**：Generation

**功能**：在输入网格表面均匀采样生成点云。支持法线偏移、松散度、face group 过滤与边缘避让（对齐 Houdini Scatter Group + Distance From Border 硬阈值）。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Mesh | `SpatialMesh` | 要采样的网格 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `count` | integer | 100 | ≥ 0, ≤ 1000000 | 采样点数量 |
| `seed` | integer | 0 | — | 随机种子 |
| `normalOffset` | number | 0.0 | — | 沿法线方向的偏移量 |
| `looseness` | number | 0.0 | ≥ 0 | 松散度。0 = 严格在表面上，>0 = 点在表面附近随机分布 |
| `faceGroup` | groupSelect | `""` | face | 只在指定 face group 上采样（Houdini Scatter Group）。空 = 全部面 |
| `excludeGroups` | groupMultiSelect | `""` | face | 从采样池排除的 face group 列表 |
| `edgeMargin` | number | 0.0 | ≥ 0 | 拒绝距采样面组边界 < margin 的点（Houdini Distance From Border 硬阈值） |

**执行逻辑**：
1. 优先读取 `PcgGeometry`（保留 face group）；仅有 triangle soup 时回退 mesh 路径
2. 按 `faceGroup` / `excludeGroups` 过滤可采样面，再 fan 三角化并按面积加权采样
3. 在选中三角形内生成均匀分布的随机点（重心坐标采样）
4. 若 `edgeMargin` > 0，拒绝距采样面组边界（组内出现一次的边）过近的点并重试
5. 若 `normalOffset` ≠ 0，沿该点法线方向偏移
6. 若 `looseness` > 0，在表面附近添加随机扰动
7. 输出 `SpatialPoint` 类型点云

**用法示例**：

```json
{
  "id": "sms2",
  "type": "SampleMeshSurface",
  "position": { "x": 300, "y": 0 },
  "data": {
    "count": 500,
    "seed": 42,
    "normalOffset": 0.5,
    "looseness": 0.3,
    "faceGroup": "extrude_top",
    "edgeMargin": 1.1
  }
}
```

> 典型连接：`PolyExtrude(topGroup=extrude_top) → SampleMeshSurface(faceGroup=extrude_top, edgeMargin>0) → CopyMeshToPoints`。

---

## Filter 类别

### DensityFilter

**类别**：Filter

**功能**：按概率密度过滤点云。每个点以 `density` 概率被保留。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `density` | number | 1.0 | 0 ~ 1 | 保留概率。1.0 = 全部保留，0.0 = 全部过滤 |

**执行逻辑**：
1. 读取 `density`，校验 ∈ [0, 1]
2. RNG 种子 = `mix_seed(graph_seed, 17)`
3. 对每个点生成随机阈值 `threshold = rand() % 10000 / 10000`，若 `threshold ≤ density` 则保留
4. 当 `density ≥ 1.0` 时跳过随机判断，直接保留所有点
5. 输出过滤后的点云

**用法示例**：

```json
{
  "id": "df",
  "type": "DensityFilter",
  "position": { "x": 600, "y": 0 },
  "data": { "density": 0.3 }
}
```

> 典型连接：`SpawnPoints → DensityFilter → PlaceInScene`，通过降低 density 实现稀疏化。

---

### AttributeFilter

**类别**：Filter

**功能**：按属性值过滤点云。仅保留指定属性等于指定值的点。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `attributeName` | string | `"tag"` | 要过滤的属性名 |
| `matchValue` | string | `""` | 匹配值（仅保留属性值等于此值的点） |

**执行逻辑**：
1. 读取 `attributeName`（空则报错）和 `matchValue`
2. 遍历所有点，保留 `point.attributes[name] == matchValue` 的点
3. 支持字符串值匹配和 JSON 值匹配
4. 输出过滤后的点云

**用法示例**：

```json
{
  "id": "af",
  "type": "AttributeFilter",
  "position": { "x": 600, "y": 100 },
  "data": { "attributeName": "type", "matchValue": "tree" }
}
```

> 配合 `CopyAttributes` 使用：先给点打属性标签，再用 `AttributeFilter` 分类过滤。

---

### Delete

**类别**：Filter

**功能**：Houdini 风格几何删除。支持 Group、Number（pattern/range/expression）、Bounding Volume、Normal、Degenerate、Random 条件并集选择，再按 Entity（points/primitives/edges）执行拓扑删除。默认空配置原样直通。

`group` 字段对齐 Houdini：除命名 group 外，支持数字/范围 pattern（如 `"0"`、`"1"`、`"0-2"`、`"!*"`）。`entity=points` 时按点序号（curve 上为局部 `@ptnum`）选择；`entity=primitives` 时按图元序号选择。与 Number 页签条件取并集，再按 `deleteNonSelected` 决定删除选中或非选中。

**限制**：仅支持 polygon mesh、spline、point 元素；不支持 VDB、NURBS 等 Houdini 专属类型。Bounding Volume 为参数框，无第二几何输入。

**属性**：`group`、`deleteNonSelected`、`entity`、`geometryType`、Number/Bounding/Normal/Degenerate/Random 页签字段、`keepPoints`、`deleteUnusedGroups`。

```json
{
  "id": "del_top",
  "type": "Delete",
  "data": {
    "entity": "primitives",
    "group": "top",
    "deleteNonSelected": false
  }
}
```

---

### Split

**类别**：Filter

**功能**：对齐 Houdini Split SOP。按 Group 将几何分成两路：第一输出（`out`）为选中部分，第二输出（`rest`）为补集。

**属性**（与 Houdini UI 对齐）：

| 属性 | 显示名 | 默认 | 语义 |
|------|--------|------|------|
| `group` | Group | `""` | 送入第一输出的子集；空 = 全部 |
| `groupType` | Group Type | `guess` | `guess` / `points` / `primitives`；`guess` 按组域名推断 |
| `invertSelection` | Invert Selection | `false` | 交换两路输出 |
| `deleteUnusedGroups` | Delete Unused Groups | `false` | 删除拆分后变空的 group；关闭时保留空 group 名 |

兼容旧图：仍可读 `entity`（等同 `groupType`）与 `removeUnusedPoints`（几何 primitive 拆分时默认丢弃未引用点）。

```json
{
  "id": "split_fire_escape",
  "type": "Split",
  "data": {
    "group": "fireEscape",
    "groupType": "guess",
    "invertSelection": false,
    "deleteUnusedGroups": false
  }
}
```

---

### Blast

**类别**：Filter

**功能**：按 group 或标量表达式删除 Point、Spline point 或 Geometry point/primitive。Spline 中间删除会拆成多条 open spline，不跨缺口重连。

**属性**：`entity` (`points`/`primitives`)、`group`、`expression`、`parameters`、`deleteNonSelected`、`removeUnusedPoints`。

```json
{
  "id": "broken_u",
  "type": "Blast",
  "data": { "entity": "points", "expression": "@curveu >= 0.4 && @curveu <= 0.6" }
}
```

完整表达式语法与数据类型行为见 [Attribute Wrangle 与 Blast](Tutorials/12-attribute-wrangle-and-blast.md)。

---

## Attribute 类别

### AttributeWrangle

**类别**：Attribute

**功能**：以 Houdini 风格标量表达式逐点修改 `@P.x/y/z`；Point 输入还可读写任意数值 `@attribute`。支持只读 `@curveu`、元素编号、数学函数与 `chf("name")` 参数。

```json
{
  "id": "gravity_sag",
  "type": "AttributeWrangle",
  "data": {
    "expression": "@P.y -= chf(\"sag\") * 4.0 * @curveu * (1.0 - @curveu);",
    "parameters": "{\"sag\":3.0}"
  }
}
```

完整语法见 [Attribute Wrangle 与 Blast](Tutorials/12-attribute-wrangle-and-blast.md)。

---

### AttributeTransfer

**类别**：Attribute

**功能**：Houdini [`attribtransfer`](https://www.sidefx.com/docs/houdini/nodes/sop/attribtransfer.html) 子集。Inspector 顶部为 Source/Destination Group；下方 **Attributes / Conditions** 两个 Tab。

**Attributes**：Detail / Primitives / Points / Vertices（开关 + 属性名，`*`/`^` 风格通配子集）；Allow P Attribute；Copy Local Variables（no-op）。

**Conditions**（对齐 Houdini）：

| 参数 | 说明 |
|------|------|
| Kernel Function | `elendt`（默认）/ `wyvill` / `blinn` / `hart` / `links` / `heron` / `uniform` |
| Kernel Radius | 多样本加权衰减半径；趋近 0 → 最近邻 |
| Max Sample Count | 参与插值的源元素上限；`1` = 最近邻 |
| Distance Threshold | 硬距离上限（可关）；阈值内完全由源决定 |
| Blend Width | 阈值外羽化带，用 Kernel 与目标原值混合 |
| Uniform Bias | 仅 `uniform` Kernel：源混合系数，目标为 `1 - bias` |

Detail 仍直接拷贝；Point/Prim/Vertex 按邻近 + Kernel 加权。

```json
{
  "id": "xfer",
  "type": "AttributeTransfer",
  "data": {
    "transferDetail": true,
    "detailAttributes": "xform",
    "transferPoints": true,
    "pointAttributes": "Cd",
    "kernelFunction": "elendt",
    "kernelRadius": 10.0,
    "maxSampleCount": 1,
    "enableDistanceThreshold": true,
    "distanceThreshold": 10.0,
    "blendWidth": 0.0
  }
}
```

典型接法：`MatchSize` 写出 Detail `xform` 后，用本节点把 `xform` 传到另一份几何。

---

## Transform 类别

### TransformPoints

**类别**：Transform

**功能**：对点云进行平移、缩放和 Y 轴旋转变换。变换顺序：缩放 → 旋转 → 平移。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `translateX` | number | 0 | X 轴平移量 |
| `translateY` | number | 0 | Y 轴平移量 |
| `translateZ` | number | 0 | Z 轴平移量 |
| `scale` | number | 1.0 | 均匀缩放系数 |
| `rotationY` | number | 0 | Y 轴旋转角度（度数，非弧度） |

**执行逻辑**：
1. 读取变换参数，`rotationY` 转换为弧度
2. 对每个点先缩放：`(x × scale, y × scale, z × scale)`
3. 绕 Y 轴旋转：`x' = sx × cos(θ) - sz × sin(θ)`，`z' = sx × sin(θ) + sz × cos(θ)`
4. 平移：加上 `(translateX, translateY, translateZ)`
5. 保留原始属性，输出变换后的点云

**用法示例**：

```json
{
  "id": "tf",
  "type": "TransformPoints",
  "position": { "x": 600, "y": 0 },
  "data": { "translateX": 5.0, "scale": 2.0, "rotationY": 45 }
}
```

---

### ProjectPoints

**类别**：Transform

**功能**：将点云的 Y 坐标投影到地形表面或固定基面上。可使用上游 `GetTerrainData` 输出的地形数据采样高度，或使用固定 `baseY`。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Points | `SpatialPoint` | 要投影的点云 |
| `terrain` | Terrain | `HeightField` | 可选。来自 `HeightFieldNoise` 或兼容节点 `GetTerrainData` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `useTerrain` | boolean | true | 是否使用地形数据采样高度。缺省自动检测：有 terrain 输入则为 true |
| `baseY` | number | 0 | 不使用地形时的固定 Y 坐标 |

**执行逻辑**：
1. 检测是否有 typed HeightField terrain 输入，`useTerrain` 缺省取 `terrain != nullptr`
2. 若使用 typed 地形：按 HeightField sampling 与 border policy 对命名 `height` 层做世界空间双线性采样；不会在越界时静默换成随机噪声
3. 若不使用地形：Y = `baseY`
4. X 和 Z 坐标不变，保留原始属性

**用法示例**：

```json
{
  "id": "proj",
  "type": "ProjectPoints",
  "position": { "x": 600, "y": 0 },
  "data": { "useTerrain": true, "baseY": 0 }
}
```

> 典型连接：`HeightFieldNoise → ProjectPoints(terrain)`，`CreatePointGrid → ProjectPoints(in)`；旧图仍可连接 `GetTerrainData`。

---

## Sampler 类别

### SampleSurface

**类别**：Sampler

**功能**：将点云的 Y 坐标混合到地形表面。与 `ProjectPoints` 不同，`SampleSurface` 支持混合系数 `blend`，可以在原始 Y 和地形 Y 之间线性插值。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Points | `SpatialPoint` | 要采样的点云 |
| `terrain` | Terrain | `HeightField` | **必需**。来自 `HeightFieldNoise` 或兼容节点 `GetTerrainData` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `offsetY` | number | 0 | — | 采样后 Y 轴偏移量 |
| `blend` | number | 1.0 | 0 ~ 1 | 混合系数。0 = 保持原始 Y，1 = 完全使用地形 Y |

**执行逻辑**：
1. 读取 typed HeightField 的命名 `height` 层
2. 按 sampling 与 border policy 做双线性采样得到 `terrain_y`
3. 计算 `y = point.y × (1 - blend) + terrain_y × blend + offsetY`
4. X 和 Z 不变，保留原始属性

**用法示例**：

```json
{
  "id": "ss",
  "type": "SampleSurface",
  "position": { "x": 600, "y": 200 },
  "data": { "offsetY": 0.5, "blend": 0.8 }
}
```

> 典型连接：`GetTerrainData → SampleSurface(terrain)`，`SurfaceSampler → SampleSurface(in)`

---

## Metadata 类别

### CopyAttributes

**类别**：Metadata

**功能**：为点云中的每个点复制指定的属性。属性的值从节点自身属性中读取。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `attributeNames` | string | `"prefab,scale"` | 逗号分隔的属性名列表 |
| `prefab` | string | `""` | 要写入的 prefab 属性值 |
| `scale` | number | 1.0 | 要写入的 scale 属性值 |

> `attributeNames` 中列出的每个属性名，都会从节点的同名属性中取值。例如 `attributeNames = "prefab,scale"` 时，会读取节点的 `prefab` 和 `scale` 属性值写入到每个点。

**执行逻辑**：
1. 解析 `attributeNames` 为名称列表
2. 对每个点的每个指定属性名，从节点属性中查找同名 key 并写入
3. 保留点坐标和其他已有属性，输出更新后的点云

**用法示例**：

```json
{
  "id": "ca",
  "type": "CopyAttributes",
  "position": { "x": 600, "y": 0 },
  "data": { "attributeNames": "prefab,scale", "prefab": "TreePrefab", "scale": 1.5 }
}
```

> 配合 `AttributeFilter` 使用：先 `CopyAttributes` 打标签，再 `AttributeFilter` 按值过滤。

---

### DeleteAttributes

**类别**：Metadata

**功能**：从点云中的每个点删除指定的属性。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `attributeNames` | string | `"tag"` | 逗号分隔的待删除属性名列表 |

**执行逻辑**：
1. 解析 `attributeNames` 为名称列表
2. 对每个点，从 `attributes` 中移除列表中的所有 key
3. 坐标不变，输出更新后的点云

**用法示例**：

```json
{
  "id": "da",
  "type": "DeleteAttributes",
  "position": { "x": 600, "y": 100 },
  "data": { "attributeNames": "temp,scale" }
}
```

---

### BreakAttributes

**类别**：Metadata

**功能**：从点云中"提取"指定属性，将其从逐点属性转换为点云级别的元数据数组。原始属性值会被收集为一个数组存储在 `metadata` 中，不再附属于单个点。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `attributeName` | string | `"prefab"` | 要提取的属性名（单个，非逗号列表） |

**执行逻辑**：
1. 读取 `attributeName`（空则报错）
2. 遍历所有点，收集每个点中该属性的值（无则 null）组成数组
3. 将数组存入 `point_data.metadata[attributeName]`
4. 点本身保持不变（属性仍在点上，同时提取到 metadata）

**用法示例**：

```json
{
  "id": "ba",
  "type": "BreakAttributes",
  "position": { "x": 600, "y": 200 },
  "data": { "attributeName": "prefab" }
}
```

> 类似 Houdini 的 Attribute Wrangle：将逐点属性提取为全局数组供后续使用。

---

## Spawner 类别

### PlaceInScene

**类别**：Spawner

**功能**：终端放置节点。将点云数据包装为场景实例输出，附带 prefab 名称和缩放信息。Y 坐标保持不变。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Instances | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `prefab` | string | `""` | 要实例化的 Prefab 名称/路径 |
| `scale` | number | 1.0 | 实例缩放系数 |

**执行逻辑**：
1. 读取输入点云（校验含 `points` 数组）
2. 读取 `prefab` 和 `scale` 属性
3. 输出 JSON：`{"status": "ok", "prefab": ..., "scale": ..., "pointCount": N, "points": [...]}`
4. Unity 侧据此在场景中放置 prefab 实例

**用法示例**：

```json
{
  "id": "place",
  "type": "PlaceInScene",
  "position": { "x": 900, "y": 0 },
  "data": { "prefab": "Assets/Prefabs/Tree.prefab", "scale": 1.0 }
}
```

> 典型连接：`SpawnPoints → PlaceInScene`（最基础的流水线）

---

### StaticMeshSpawner

**类别**：Spawner

**功能**：将点云转换为静态网格实例。与 `PlaceInScene` 类似，但额外支持 `mesh` 属性，并将 prefab/mesh/scale 写入每个点的属性中。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Points | `SpatialPoint` | 要放置的点云 |
| `mesh` | Instance Mesh | `SpatialMesh` | 可选。上游网格实例 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Instances | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `prefab` | string | `""` | Prefab 名称/路径 |
| `mesh` | string | `""` | 静态网格资源路径 |
| `scale` | number | 1.0 | 实例缩放系数 |

**执行逻辑**：
1. 读取输入点云（校验含 `points` 数组）
2. 若连接了 `mesh` 输入，使用上游网格实例；否则读取 `mesh` 属性
3. 读取 `prefab` 和 `scale` 属性
4. 对每个点写入 `attributes["prefab"]`、`attributes["mesh"]`、`attributes["scale"]`
5. 输出 JSON：点云数据 + `{"status": "ok", "prefab": ..., "mesh": ..., "scale": ..., "pointCount": N}`

**用法示例**：

```json
{
  "id": "sms",
  "type": "StaticMeshSpawner",
  "position": { "x": 900, "y": 0 },
  "data": { "prefab": "RockPrefab", "mesh": "Assets/Meshes/Rock.obj", "scale": 0.8 }
}
```

---

### MergeSpawnPoints

**类别**：Spawner

**功能**：合并多路 `StaticMeshSpawner` 输出。按输入顺序拼接 Points，并为每一路保留独立的 `spawnMesh` 原型（一种 building → 一批 GPU instance）。Unity 侧按 `spawnProtoCounts` 切片后，对每个原型分别做 multi-submesh `RenderMeshIndirect`。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Spawn Streams | `SpatialPoint`（variadic） | 每路来自 `StaticMeshSpawner.out`，并携带对应 `spawnMesh` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Instances | `SpatialPoint` |

**属性**：无

**执行逻辑**：
1. 按输入顺序收集 Points 与同序 `spawnMesh`
2. 拼接 Points，sidecar 写入 `spawnProtoCounts`
3. 输出全部 `spawnMesh`（多原型）

**典型连接**：

```text
blast_tall  → StaticMeshSpawner(tall)  ─┐
blast_med   → StaticMeshSpawner(med)   ─┼→ MergeSpawnPoints → Output
blast_short → StaticMeshSpawner(short) ─┘
```

---

## Spline 类别

样条（Spline）类别节点负责创建、编辑和消费样条数据，是桥梁、道路等线性结构生成的核心。

### CreateSpline

**类别**：Spline

**功能**：创建样条曲线。支持三种插值模式：直线、折线、Catmull-Rom 样条。可通过控制点列表定义任意形状，或通过起止点快速生成直线。

**输入 Pin**：无

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Spline | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `mode` | enum | `"catmullRom"` | `line` / `polyline` / `catmullRom` | 插值模式 |
| `closed` | boolean | false | — | 是否闭合为环 |
| `subdivisions` | integer | 8 | 1 ~ 64 | Catmull-Rom 模式下每段的细分数 |
| `startX` | number | 0 | — | 直线模式起点 X |
| `startY` | number | 0 | — | 直线模式起点 Y |
| `startZ` | number | 0 | — | 直线模式起点 Z |
| `endX` | number | 40 | — | 直线模式终点 X |
| `endY` | number | 0 | — | 直线模式终点 Y |
| `endZ` | number | 0 | — | 直线模式终点 Z |
| `controlPoints` | string | `[{"x":0,"y":0,"z":0},...]` | — | JSON 格式的控制点列表 |
| `editPlane` | enum | `"none"` | `none` / `xy` / `xz` / `yz` | 编辑平面约束（Scene 拖动时被锁定轴保持该点原分量，不归零） |
| `sceneOffsetX` | number | 0 | — | 场景偏移 X |
| `sceneOffsetY` | number | 0 | — | 场景偏移 Y |
| `sceneOffsetZ` | number | 0 | — | 场景偏移 Z |

**执行逻辑**：
1. `line` 模式：从 `(startX, startY, startZ)` 到 `(endX, endY, endZ)` 生成直线段
2. `polyline` 模式：解析 `controlPoints` 为坐标列表，依次连线
3. `catmullRom` 模式：解析 `controlPoints`，对每段进行 Catmull-Rom 插值，`subdivisions` 控制每段细分点数
4. 若 `closed = true`，将首尾相连形成闭合环
5. 应用 `sceneOffset` 偏移所有点坐标
6. 输出 `SpatialSpline` 类型数据

**用法示例**：

```json
{
  "id": "spline",
  "type": "CreateSpline",
  "position": { "x": 0, "y": 0 },
  "data": {
    "mode": "catmullRom",
    "closed": false,
    "subdivisions": 12,
    "controlPoints": "[{\"x\":0,\"y\":0,\"z\":0},{\"x\":10,\"y\":2,\"z\":5},{\"x\":25,\"y\":0,\"z\":15},{\"x\":40,\"y\":0,\"z\":0}]"
  }
}
```

> 典型连接：`CreateSpline(backbone) → SweepAlongSpline`，`CreateSpline(profile) → SweepAlongSpline`。

---

### Carve

**类别**：Spline

**功能**：对齐 Houdini Carve SOP。按 U 参数（0~1）切割样条为多段（Cut），或在指定 U 位置提取点（Extract）。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Spline | `SpatialSpline` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Out | `Any`（Cut 输出 `SpatialSpline`，Extract 输出 `SpatialPoint` 点云） |

**属性**（面板顺序与 Houdini 一致）：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `group` | string | `""` | 只处理该属性组内的样条；组外样条原样透传（Extract 模式下以其顶点形式进入点云） |
| `arcLengthU` | boolean | true | Carve Curves by Relative Arc Length：U 按弧长而非顶点参数化解释 |
| `useFirstU` / `uStart` | boolean / number | true / 0.0 | First U 起始位置 |
| `uStartAttrib` | string | `""` | First U Attrib：样条属性缩放 First U（`uStart * attrib`） |
| `useSecondU` / `uEnd` | boolean / number | true / 1.0 | Second U 结束位置 |
| `uEndAttrib` | string | `""` | Second U Attrib：样条属性缩放 Second U |
| `useFirstV` / `vStart` / `vStartAttrib` | — | true / 0.25 / `""` | Houdini 曲面雕刻参数；对一维样条无作用，仅为参数面板对齐保留 |
| `useSecondV` / `vEnd` / `vEndAttrib` | — | true / 0.75 / `""` | 同上 |
| `location` | enum(radio) | `"divisions"` | `divisions` / `breakpoints` 页签 |
| `uDivisions` | integer | 2 | Divisions 页签：U 方向切/提取段数（N 段 → N-1 个内部切点） |
| `vDivisions` | integer | 2 | 曲面参数，样条上无作用 |
| `cutAtAllInternalUBreakpoints` | boolean | true | Breakpoints 页签：在区间内所有内部顶点处切割 |
| `cutAtAllInternalVBreakpoints` | boolean | true | 曲面参数，样条上无作用 |
| `operation` | enum(radio) | `"cut"` | `cut` / `extract` 单选 |
| `keepInside` | boolean | true | Cut：保留 `[First U, Second U]` 区间内的段 |
| `keepOutside` | boolean | false | Cut：保留区间外的段 |
| `extractType` | enum | `"curves3d"` | Extract：`curves3d`（Extract 3D Isoparametric Curve(s)）/ `points`（Extract Point(s)）；一维样条的等参截面即点，两者行为一致 |
| `keepOriginal` | boolean | false | Extract：输出中追加原始样条顶点 |
| `onlyAtBreakpoints` | boolean | false | Extract：只在已存在顶点（breakpoints）处执行，落在边中间的 U 位置被丢弃 |

**执行逻辑**：
1. Cut：在 First/Second U（可被 Attrib 缩放）与 Divisions/Breakpoints 内部切点处拆分样条，按 `keepInside`/`keepOutside` 保留区间段；`group` 外的样条不参与切割、原样输出。
2. Extract：在每个 U 位置输出一个点（属性继承自源样条），`keepOriginal` 追加原始顶点，`onlyAtBreakpoints` 将位置限制到已存在顶点。

**Houdini 差异说明**：Houdini Carve 同时支持面/曲面（V 参数、2D 等参曲线提取）；本节点输入为 `SpatialSpline`，V 参数与 Extract Type 仅做面板与参数解析对齐，对一维样条无额外效果（与 Houdini 作用于曲线时一致：V 无效、提取结果为点）。

---

### CreateBezierSpline

**类别**：Spline

**功能**：以 Hermite 切线或 3n+1 cubic Bezier 控制点生成确定性样条，适合车身轮廓和精确装配路径。

**输入 Pin**：无；**输出 Pin**：`out`（`SpatialSpline`）。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `mode` | `catmullRom` | 当前实现为 cubic Bezier / Hermite 子集 |
| `closed` | false | 是否闭合首尾段 |
| `subdivisions` | 12 | 每段采样数，范围 1–64 |
| `controlPoints` | 两个 JSON 点 | anchor 列表；或不提供匹配 tangents 时使用 3n+1 cubic controls |
| `tangents` | 两个 JSON 向量 | 数量与 anchors 相同时按 Hermite 求值 |
| `editPlane` | `none` | Scene 编辑约束：`none` / `xy` / `xz` / `yz` |
| `sceneOffsetX/Y/Z` | 0 | 编辑器场景偏移 |

控制点少于两个，或既不满足“anchor+tangent 等长”也不满足 3n+1 cubic control 形式时明确失败。该节点输出曲线，不生成 Geometry。

---

### ResampleSpline

**类别**：Spline

**功能**：对输入样条进行重采样，按等间距或指定点数重新分布采样点。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Spline | `SpatialSpline` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Spline | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `mode` | enum | `"spacing"` | `spacing` / `count` | 重采样模式 |
| `spacing` | number | 1.0 | ≥ 0.01 | spacing 模式下相邻点间距 |
| `pointCount` | integer | 32 | ≥ 2 | count 模式下采样点数 |

**执行逻辑**：
1. 计算样条总弧长
2. `spacing` 模式：按 `spacing` 等间距采样，点数 = `ceil(总弧长 / spacing)`
3. `count` 模式：均匀分布 `pointCount` 个点
4. 保留闭合/开放属性，输出重采样后的样条

**用法示例**：

```json
{
  "id": "resample",
  "type": "ResampleSpline",
  "position": { "x": 300, "y": 0 },
  "data": { "mode": "spacing", "spacing": 2.0 }
}
```

---

### ConditionOutline

**类别**：Spline

**功能**：对输入 `SpatialSpline` 先做窗口平滑，再做 RDP 简化。`protectSpans` 用**输入点索引闭区间**保护特征点（不是 Group）。默认 `win=1`、`eps=0` 为 no-op；进入实际处理时，closed spline 的显式重复首点会被规范化为 `closed=true` 且不重复首点。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Spline | `SpatialSpline` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Spline | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `win` | integer | 1 | 奇数 ≥ 1 | 平滑窗口；`1` 禁用平滑 |
| `eps` | number | 0.0 | ≥ 0 | RDP 点到线段距离阈值；`0` 禁用简化（不因共线隐式删点） |
| `protectSpans` | string(JSON) | `[]` | — | `[{start,end},…]` 输入点索引闭区间；跨 seam（`start>end`）仅允许 closed；索引按规范化前输入解释 |

**执行逻辑**：
1. 校验 `win`（奇数）、`eps`（有限且 ≥0）、`protectSpans` JSON/索引
2. 每条 spline 独立处理；保留顺序、`closed`、curve attributes
3. 点数 < 3 或参数 no-op → 原样输出；实际处理时 closed 输入若首尾重复，先移除末尾重复点，末点保护映射到逻辑首点
4. 平滑：仅更新未保护点；open 端点邻域 clamp，closed 环绕；输出不产生非重合 seam
5. RDP：保护点与 open 端点为锚；相邻锚点片段上按 `distance <= eps` 可删
6. **非目标**：弧长重采样、自交修复、point attribute 插值、转 columns

**用法示例**：

```json
{
  "id": "condition",
  "type": "ConditionOutline",
  "position": { "x": 0, "y": 220 },
  "data": {
    "win": 3,
    "eps": 0.002,
    "protectSpans": "[{\"start\":0,\"end\":2}]"
  }
}
```

> 典型连接：`CreateSpline(closed) → ConditionOutline → OutlineSolid(inputMode=outline)`。

---

### SampleAlongSpline

**类别**：Spline

**功能**：沿样条曲线均匀采样生成点云。支持间距控制、偏移量和切线对齐。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Spline | `SpatialSpline` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Points | `SpatialPoint` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `spacing` | number | 8.0 | ≥ 0.1 | 沿样条的采样间距 |
| `offset` | number | 0.0 | — | 起始偏移量 |
| `includeEnd` | boolean | true | — | 是否包含终点 |
| `alignToTangent` | boolean | true | — | 是否将点的旋转对齐到切线方向 |
| `seed` | integer | 0 | — | 随机种子（用于抖动） |

**执行逻辑**：
1. 计算样条弧长，按 `spacing` 等间距采样
2. 从 `offset` 位置开始采样
3. 若 `includeEnd = true`，在终点追加一个点
4. 若 `alignToTangent = true`，将点的 `rotation` 属性设为切线方向
5. 输出 `SpatialPoint` 类型点云

**用法示例**：

```json
{
  "id": "sas",
  "type": "SampleAlongSpline",
  "position": { "x": 300, "y": 0 },
  "data": { "spacing": 5.0, "includeEnd": true, "alignToTangent": true }
}
```

> 典型连接：`CreateSpline → SampleAlongSpline → PlaceInScene`，沿路径放置路灯/栅栏。

---

### SweepAlongSpline

**类别**：Spline

**功能**：沿 backbone 样条扫掠 profile 截面生成网格。支持自定义截面输入或内置矩形/圆形/带状截面。是桥梁、道路、管道等线性结构生成的核心节点。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `backbone` | Backbone | `SpatialSpline` | 扫掠路径 |
| `profile` | Cross Section | `SpatialSpline` | 截面轮廓 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**输出组**：

| 组名 | 域 | 条件 | 含义 |
|------|-----|------|------|
| `side` | face | 总是 | 沿 backbone 的侧壁面 |
| `cap_start` | face | `capStart=true` | 起始端盖面 |
| `cap_end` | face | `capEnd=true` | 结束端盖面 |
| `seam` | edge | 总是 | 截面闭合处的缝合边（沿扫掠方向） |
| `profile_corner` | edge | 总是 | 截面折角处的纵向棱边 |
| `unshared` | edge | 总是 | 边界边（只有一侧面的边） |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `surfaceShape` | enum | `"crossSection"` | `crossSection` / `rectangle` / `circle` / `ribbon` | 截面来源 |
| `profileWidth` | number | 6.0 | ≥ 0.01 | 矩形截面宽度 |
| `profileHeight` | number | 0.4 | ≥ 0.01 | 矩形截面高度 |
| `radius` | number | 1.0 | ≥ 0.01 | 圆形截面半径 |
| `columns` | integer | 16 | 3 ~ 128 | 圆形截面分段数 |
| `sampleSpacing` | number | 1.0 | ≥ 0.05 | 沿路径的采样间距 |
| `capStart` | boolean | false | — | 是否封闭起始端 |
| `capEnd` | boolean | false | — | 是否封闭结束端 |
| `upX` | number | 0 | — | 上方向向量 X |
| `upY` | number | 1 | — | 上方向向量 Y |
| `upZ` | number | 0 | — | 上方向向量 Z |
| `twist` | number | 0 | — | 沿路径的扭转量 |
| `profileRoll` | number | 0 | -360 ~ 360 | 截面旋转角度 |
| `scaleStart` | number | 1.0 | ≥ 0.001 | 起始缩放 |
| `scaleEnd` | number | 1.0 | ≥ 0.001 | 结束缩放 |
| `profilePlane` | enum | `"xy"` | `xy` / `xz` / `yz` | 截面所在平面 |

**执行逻辑**：
1. 读取 backbone 样条，按 `sampleSpacing` 采样路径点
2. 根据 `surfaceShape` 确定截面：
   - `crossSection`：使用 `profile` 输入的自定义截面
   - `rectangle`：生成宽 `profileWidth`、高 `profileHeight` 的矩形截面
   - `circle`：生成半径 `radius`、分段 `columns` 的圆形截面
   - `ribbon`：生成带状截面
3. 在每个路径点处放置截面，对齐到路径切线方向（使用 `up` 向量和 `twist` 控制朝向）
4. 相邻截面间连接四边形面，形成侧壁
5. 若 `capStart`/`capEnd` 为 true，在端点处生成端盖面
6. 应用 `scaleStart` → `scaleEnd` 线性缩放
7. 标记输出组（side / cap_start / cap_end / seam / profile_corner / unshared）
8. 输出 `SpatialMesh` 类型数据

**用法示例**：

```json
{
  "id": "sweep",
  "type": "SweepAlongSpline",
  "position": { "x": 300, "y": 100 },
  "data": {
    "surfaceShape": "crossSection",
    "sampleSpacing": 1.0,
    "capStart": true,
    "capEnd": true,
    "upY": 1
  }
}
```

> 典型连接：`CreateSpline(backbone) + CreateSpline(profile) → SweepAlongSpline → GroupCreate → BevelMesh → Output`。详见 [常见节点组合 § 5](#5-桥梁截面圆角sweep--group--bevel)。

---

### ExtrudeAlongSpline

**类别**：Spline

**功能**：沿样条拉伸 profile mesh 生成网格。与 `SweepAlongSpline` 类似，但截面输入为 mesh 而非 spline，支持更复杂的截面形状。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `spline` | Spline | `SpatialSpline` | 拉伸路径 |
| `profile` | Profile | `SpatialMesh` | 截面 mesh |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `profileWidth` | number | 6.0 | ≥ 0.01 | 截面宽度 |
| `profileHeight` | number | 0.4 | ≥ 0.01 | 截面高度 |
| `sampleSpacing` | number | 1.0 | ≥ 0.05 | 沿路径的采样间距 |
| `capStart` | boolean | true | — | 是否封闭起始端 |
| `capEnd` | boolean | true | — | 是否封闭结束端 |
| `upX` | number | 0 | — | 上方向向量 X |
| `upY` | number | 1 | — | 上方向向量 Y |
| `upZ` | number | 0 | — | 上方向向量 Z |
| `twist` | number | 0 | — | 沿路径的扭转量 |
| `profileRoll` | number | 0 | -360 ~ 360 | 截面旋转角度 |
| `scaleStart` | number | 1.0 | ≥ 0.001 | 起始缩放 |
| `scaleEnd` | number | 1.0 | ≥ 0.001 | 结束缩放 |
| `profilePlane` | enum | `"auto"` | `auto` / `xy` / `xz` / `yz` | 截面所在平面。`auto` = 自动检测 |

**执行逻辑**：
1. 读取样条路径和 profile mesh
2. 按 `sampleSpacing` 采样路径点，在每个点处放置 profile 截面
3. 若 `profilePlane = auto`，自动检测 profile 所在平面
4. 相邻截面间连接面，形成侧壁
5. 若 `capStart`/`capEnd` 为 true，生成端盖
6. 应用缩放和扭转
7. 输出 `SpatialMesh` 类型数据

**用法示例**：

```json
{
  "id": "extrude",
  "type": "ExtrudeAlongSpline",
  "position": { "x": 300, "y": 100 },
  "data": { "sampleSpacing": 1.0, "capStart": true, "capEnd": true, "profilePlane": "auto" }
}
```

---

### CrossSectionProfile

**类别**：Spline

**功能**：从输入 mesh 中提取截面轮廓。用指定平面切割 mesh，提取截面形状作为 profile 输出。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Mesh | `SpatialMesh` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Profile | `SpatialMesh` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `plane` | enum | `"auto"` | `auto` / `xy` / `xz` / `yz` | 切割平面。`auto` = 自动选择最佳平面 |
| `weldEpsilon` | number | 0.0001 | ≥ 1e-8 | 焊接容差。距离小于此值的顶点合并为一个 |
| `center` | boolean | true | — | 是否将截面轮廓居中到原点 |

**执行逻辑**：
1. 读取输入 mesh
2. 用 `plane` 指定的平面切割 mesh
3. 提取切割线作为截面轮廓
4. `weldEpsilon` 焊接近距离顶点
5. 若 `center = true`，将轮廓几何中心移到原点
6. 输出 `SpatialMesh` 类型数据（截面轮廓）

**用法示例**：

```json
{
  "id": "csp",
  "type": "CrossSectionProfile",
  "position": { "x": 300, "y": 0 },
  "data": { "plane": "auto", "weldEpsilon": 0.0001, "center": true }
}
```

> 典型连接：`GetMeshData → CrossSectionProfile → ExtrudeAlongSpline(profile)`，从现有 mesh 提取截面后沿路径拉伸。

---

### InstanceAlongSpline

**类别**：Spline

**功能**：沿样条路径等间距实例化 mesh，生成重复排列的网格实例。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `spline` | Spline | `SpatialSpline` | 实例化路径 |
| `mesh` | Prototype | `SpatialMesh` | 要实例化的网格 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `spacing` | number | 10.0 | ≥ 0.1 | 实例间距 |
| `offset` | number | 0.0 | — | 起始偏移量 |
| `includeEnd` | boolean | false | — | 是否在终点放置实例 |
| `alignToTangent` | boolean | true | — | 是否对齐到切线方向 |
| `scale` | number | 1.0 | ≥ 0.001 | 实例缩放 |

**执行逻辑**：
1. 计算样条弧长，按 `spacing` 等间距采样
2. 从 `offset` 位置开始放置 mesh 实例
3. 若 `alignToTangent = true`，旋转实例对齐路径切线
4. 应用 `scale` 缩放
5. 合并所有实例为单个 mesh 输出

**用法示例**：

```json
{
  "id": "ias",
  "type": "InstanceAlongSpline",
  "position": { "x": 300, "y": 0 },
  "data": { "spacing": 5.0, "alignToTangent": true, "scale": 1.0 }
}
```

> 典型连接：`CreateSpline → InstanceAlongSpline(spline) + GetMeshData → InstanceAlongSpline(mesh)`，沿路径放置路灯柱/桥墩。

### CreateSpiralSpline

生成螺旋线样条（pitch-driven 模型）。

| 属性 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| radius | number | 1.0 | > 0 | 螺旋半径 |
| pitch | number | 0.5 | ≠ 0 | 每完整一圈沿轴前进的距离 |
| turns | number | 3.0 | > 0 | 圈数（支持小数如 2.5） |
| pointsPerTurn | integer | 24 | ≥ 4 | 每圈采样点数 |
| axis | enum | y | x/y/z | 螺旋轴 |

**输出**：`out: SpatialSpline`（polyline，closed=false）。

**执行逻辑**：`sample_count = ceil(turns * pointsPerTurn) + 1`。对每个采样点：`t = min(i / pointsPerTurn, turns)`，高度 = `t * pitch`，角度 = `t * 2π`。

> 典型连接：`CreateSpiralSpline → SweepAlongSpline → Output`

---

### CreateArcSpline

生成圆弧样条（对齐 Houdini Circle SOP 的 arc 模式），用于拱窗剖面、拱门线脚等。

| 属性 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| radius | number | 1.0 | > 0 | 圆弧半径 |
| startAngle | number | 0.0 | | 起始角（度） |
| endAngle | number | 180.0 | | 结束角（度） |
| segments | integer | 16 | 1–256 | 弧段数；采样点数 = segments + 1 |
| axis | enum | z | x/y/z | 圆弧所在平面的法线轴（与 `CreateSpiralSpline.axis` 对齐） |

**输出**：`out: SpatialSpline`（polyline，closed=false）。圆弧位于垂直于 `axis` 的平面上，圆心在原点。

> 典型连接：`CreateArcSpline → SweepAlongSpline(rectangle) → TransformMesh`（拱门线脚）；或作为拱窗 Boolean cutter 的剖面轮廓。

---

## Structural 类别

### ConvexHull

**类别**：Structural

**功能**：计算输入点云的凸包，输出包围所有点的凸多边形样条。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Hull | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `tolerance` | number | 0.0 | ≥ 0 | 共线点容差。大于此距离的点才被认为是凸包顶点 |

**执行逻辑**：
1. 读取输入点云，校验至少 3 个点
2. 使用 Andrew's monotone chain 算法计算凸包
3. `tolerance` 用于过滤共线点
4. 输出 `SpatialSpline`：一条闭合多边形样条

**用法示例**：

```json
{
  "id": "ch",
  "type": "ConvexHull",
  "position": { "x": 600, "y": 0 },
  "data": { "tolerance": 0.01 }
}
```

---

### ConnectNearest

**类别**：Structural

**功能**：为每个点连接其 K 个最近邻，输出边样条集合。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Edges | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `k` | integer | 1 | ≥ 1, ≤ 16 | 每个点连接的最近邻数量 |
| `maxDistance` | number | -1 | — | 最大连接距离。-1 = 无限制 |

**执行逻辑**：
1. 读取输入点云
2. 对每个点找到 K 个最近邻（暴力搜索，O(n²k)）
3. 为每对邻接关系创建一条线段样条
4. `maxDistance ≥ 0` 时过滤超距边
5. 输出 `SpatialSpline`：线段集合

**用法示例**：

```json
{
  "id": "cn",
  "type": "ConnectNearest",
  "position": { "x": 600, "y": 0 },
  "data": { "k": 3, "maxDistance": 5.0 }
}
```

---

### Delaunay

**类别**：Structural

**功能**：对输入点云执行 Delaunay 三角剖分，输出三角形边样条集合。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Edges | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `maxEdgeLength` | number | -1 | 最大边长度过滤。-1 = 不过滤 |

**执行逻辑**：
1. 读取输入点云，校验至少 3 个点
2. 执行 Delaunay 三角剖分（Bowyer-Watson 算法）
3. 收集所有三角形边
4. `maxEdgeLength ≥ 0` 时过滤端点距离超过阈值的边
5. 输出 `SpatialSpline`：边集合

**用法示例**：

```json
{
  "id": "del",
  "type": "Delaunay",
  "position": { "x": 600, "y": 0 },
  "data": { "maxEdgeLength": 8.0 }
}
```

> 典型连接：`SpawnPoints → Delaunay → MST → AStarPathfinding`，生成道路网络。

---

### MST

**类别**：Structural

**功能**：从输入边集合中计算最小生成树，输出树形样条。需要同时提供边和点数据。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Edges | `SpatialSpline` | 来自 `Delaunay` 或 `ConnectNearest` 的边集合 |
| `points` | Points | `SpatialPoint` | 原始点云（用于确定顶点索引） |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Tree | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `maxEdgeLength` | number | -1 | 最大边长度过滤。-1 = 不过滤 |

**执行逻辑**：
1. 解析输入边为图结构（顶点索引 + 边权重 = 距离）
2. 执行 Kruskal 或 Prim 算法计算最小生成树
3. `maxEdgeLength ≥ 0` 时过滤超距边
4. 输出 `SpatialSpline`：树形边集合

**用法示例**：

```json
{
  "id": "mst",
  "type": "MST",
  "position": { "x": 900, "y": 0 },
  "data": { "maxEdgeLength": -1 }
}
```

> 典型连接：`Delaunay(out → MST in)` + `SpawnPoints(out → MST points)`

---

### Voronoi

**类别**：Structural

**功能**：对输入点云执行 Voronoi 图划分，输出单元格边样条集合。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Points | `SpatialPoint` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Cells | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `offsetY` | number | 0 | Y 轴偏移量（将所有边点 Y 加上此值） |
| `maxEdgeLength` | number | -1 | 最大边长度过滤。-1 = 不过滤 |

**执行逻辑**：
1. 读取输入点云，校验至少 3 个点
2. 基于 Delaunay 对偶图计算 Voronoi 单元格边
3. `offsetY` 应用到所有边点的 Y 坐标
4. `maxEdgeLength ≥ 0` 时过滤超距边
5. 输出 `SpatialSpline`：单元格边集合

**用法示例**：

```json
{
  "id": "vor",
  "type": "Voronoi",
  "position": { "x": 600, "y": 100 },
  "data": { "offsetY": 0.1, "maxEdgeLength": -1 }
}
```

---

### AStarPathfinding

**类别**：Structural

**功能**：在边图上执行 A* 寻路，找到从起点到终点的最短路径。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Edges | `SpatialSpline` | 来自 `Delaunay`/`ConnectNearest`/`MST` 的边集合 |
| `points` | Points | `SpatialPoint` | 顶点点云 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Path | `SpatialSpline` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `startIndex` | integer | 0 | ≥ 0 | 起点索引（点云中的序号） |
| `endIndex` | integer | 0 | ≥ 0 | 终点索引 |

**执行逻辑**：
1. 解析边为邻接图
2. `startIndex` 和 `endIndex` 被 clamp 到 `[0, n-1]`
3. 执行 A* 算法（启发函数 = 欧氏距离）
4. 将路径上的点序列输出为一条样条

**用法示例**：

```json
{
  "id": "astar",
  "type": "AStarPathfinding",
  "position": { "x": 1200, "y": 0 },
  "data": { "startIndex": 0, "endIndex": 99 }
}
```

> 典型连接：`MST(out → AStar in)` + `SpawnPoints(out → AStar points)`

---

## Mesh 类别

### CreateGridMesh

**类别**：Mesh

**功能**：创建 Houdini Grid 风格的平面网格。在指定平面上以 `rows × cols` 个 quad 铺满 `sizeX × sizeY` 区域，居中于原点。支持合并上游输入网格。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Mesh | `SpatialMesh` | 可选。上游网格会与新 grid 合并 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `sizeX` | number | 10.0 | ≥ 0 | 平面第一轴总尺寸 |
| `sizeY` | number | 10.0 | ≥ 0 | 平面第二轴总尺寸 |
| `rows` | integer | 10 | 1 ~ 512 | 行方向 quad 数量 |
| `cols` | integer | 10 | 1 ~ 512 | 列方向 quad 数量 |
| `plane` | enum | `xz` | `xz` / `xy` / `yz` | 平面朝向；`xz` 为地面（法线 +Y） |

**执行逻辑**：
1. 校验尺寸 ≥ 0，`rows`/`cols` ≥ 1
2. 在选定平面上生成 `(rows+1) × (cols+1)` 顶点与 `rows × cols` 个 quad face
3. 若有上游输入网格，合并（前缀 `in_`）
4. 输出 `SpatialMesh`（保留 polygon 拓扑）

**用法示例**：

```json
{
  "id": "ground",
  "type": "CreateGridMesh",
  "position": { "x": 0, "y": 0 },
  "data": { "sizeX": 48.0, "sizeY": 48.0, "rows": 1, "cols": 1, "plane": "xz" }
}
```

> 典型连接：`CreateGridMesh → LotSubdivision → PolyExtrude`（地块划分）；`rows=1, cols=1` 等价于单 quad 地面，替代 thin `CreateBoxMesh`。

---

### CreateBoxMesh

**类别**：Mesh

**功能**：创建一个长方体网格。支持合并上游输入网格。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Mesh | `SpatialMesh` | 可选。上游网格会与新盒子合并 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `width` | number | 2.0 | ≥ 0 | X 轴方向尺寸 |
| `height` | number | 2.0 | ≥ 0 | Y 轴方向尺寸 |
| `depth` | number | 2.0 | ≥ 0 | Z 轴方向尺寸 |

**执行逻辑**：
1. 校验三个维度 ≥ 0
2. 创建 8 顶点 / 12 三角形的盒子网格
3. 若有上游输入网格，合并顶点（偏移顶点索引后追加）
4. 输出 `SpatialMesh`

**用法示例**：

```json
{
  "id": "box",
  "type": "CreateBoxMesh",
  "position": { "x": 0, "y": 0 },
  "data": { "width": 4.0, "height": 2.0, "depth": 4.0 }
}
```

> 典型连接：`CreateBoxMesh → SubdivideMesh → BevelMesh`，程序化生成圆角立方体。

---

### SubdivideMesh

**类别**：Mesh

**功能**：对输入网格执行指定层级的细分。每层将每个三角形分为 4 个子三角形。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Mesh | `SpatialMesh` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `levels` | integer | 1 | 0 ~ 4 | 细分层级。0 = 不细分。每层三角形数 ×4 |

**执行逻辑**：
1. 读取输入网格（空则报错）
2. 对每个层级执行一次 Loop 细分：在每条边中点插入新顶点，将三角形分为 4 个
3. 输出细分后的网格

**用法示例**：

```json
{
  "id": "sub",
  "type": "SubdivideMesh",
  "position": { "x": 300, "y": 0 },
  "data": { "levels": 2 }
}
```

---

### BevelMesh

**类别**：Mesh

**功能**：对网格执行倒角/斜切操作。当前已支持能力按 Blender 4.5 Bevel 子集对齐参数命名与 Inspector 分区；边选择保留 Houdini PolyBevel 风格 Group。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Mesh | `SpatialMesh` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**（Inspector：Group → 主区 → Profile / Geometry / Advanced 折叠）：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `edgeGroup` | groupSelect | `""` | 上游可用边组 | Houdini 风格 Group。空 = 全部候选边；命名组只允许组内边；未知组不倒角 |
| `offsetType` | enum | `"offset"` | `offset` / `width` | Width Type。`offset` = 偏移距离，`width` = 倒角宽度 |
| `amount` | number | 0.1 | ≥ 0, ≤ 1.0 | Amount |
| `segments` | integer | 2 | 1 ~ 8 | Segments |
| `limitMethod` | enum | `"angle"` | `none` / `angle` | Limit Method。`angle` 再按 `angleLimit` 过滤；`none` 不过滤角度 |
| `angleLimit` | number | 30.0 | 0 ~ 180 | Angle。仅当 `limitMethod=angle` 时显示/生效 |
| `profile` | number | 0.5 | 0 ~ 1 | Profile Shape。0.5 = 圆弧 |
| `miterOuter` | enum | `"sharp"` | `sharp` / `patch` / `arc` | Miter Outer |
| `miterInner` | enum | `"sharp"` | `sharp` / `patch` / `arc` | Miter Inner |
| `vmeshMethod` | enum | `"adj"` | `adj` / `cutoff` | Intersections。`adj` ≈ Grid Fill，`cutoff` = Cutoff |
| `clampOverlap` | boolean | true | — | Clamp Overlap |
| `method` | enum | `"edge"` | `edge` / `vertexPush` | Advanced/Legacy。`vertexPush` 沿顶点法线推点，**不是** Blender Vertices Bevel |
| `excludeUnshared` | boolean | true | — | 排除边界边（单侧面） |
| `excludeGroups` | groupMultiSelect | `"cap_start,cap_end"` | 上游可用组 | 排除指定组 |

**选择流水线**：`edgeGroup` 候选 → `excludeUnshared` / `excludeGroups` → `limitMethod`（Angle / None）。

**旧图兼容**：节点 data 中缺少 `limitMethod` 时：空 Group → Angle；非空 Group → None（保持历史“组内跳过角度过滤”行为）。新节点默认写入 `limitMethod=angle`。

**当前不支持（勿当成已对齐）**：Blender Custom Profile、Depth/Percent/Absolute、Loop Slide、Harden Normals、Mark Seam/Sharp、Material Index、Face Strength；Houdini Viewport Reselect / edge-loop 点选。

**执行逻辑**：
1. 读取输入网格（空则报错）
2. 按上表选择流水线选出要倒角的边
3. `method=edge` 走 Blender 风格边倒角；`vertexPush` 走 Legacy 顶点推移
4. `offsetType` 解释 `amount`；`segments` / `profile` / miter / clamp 控制形状
5. 输出倒角后的网格

**用法示例**：

```json
{
  "id": "bev",
  "type": "BevelMesh",
  "position": { "x": 600, "y": 0 },
  "data": {
    "offsetType": "offset",
    "amount": 0.2,
    "segments": 3,
    "limitMethod": "angle",
    "angleLimit": 30,
    "profile": 0.5,
    "clampOverlap": true
  }
}
```

---

### MeshNoiseDeform

**类别**：Mesh

**功能**：对输入网格沿法线方向施加噪声变形。支持内置 Perlin 噪声或外部纹理输入，可模拟自然表面起伏。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Mesh | `SpatialMesh` | 要变形的网格 |
| `texture` | Texture | `Texture` | 可选。纹理模式下的噪声来源 |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `intensity` | number | 0.02 | 0 ~ 1 | 变形强度 |
| `scale` | number | 2.0 | 0.01 ~ 64 | 噪声频率缩放 |
| `midLevel` | number | 0.5 | 0 ~ 1 | 噪声中值。0.5 = 以表面为中心双向变形 |
| `textureCoords` | enum | `"local"` | `local` | 纹理坐标模式 |
| `noiseType` | enum | `"perlin"` | `perlin` / `texture` | 噪声类型。`perlin` = 内置 Perlin，`texture` = 使用 texture 输入 |

**执行逻辑**：
1. 读取输入网格的顶点数据
2. 对每个顶点计算法线方向
3. 根据 `noiseType`：
   - `perlin`：使用内置 Perlin 噪声函数，参数 `scale` 控制频率
   - `texture`：从 `texture` 输入采样，需连接 `ImageTexture` 节点
4. 噪声值经 `midLevel` 调整后乘以 `intensity`，得到沿法线的位移量
5. 顶点位置 += 法线 × 位移量
6. 输出变形后的网格

**用法示例**：

```json
{
  "id": "noise",
  "type": "MeshNoiseDeform",
  "position": { "x": 600, "y": 0 },
  "data": { "intensity": 0.05, "scale": 3.0, "midLevel": 0.5, "noiseType": "perlin" }
}
```

> 典型连接：`CreateBoxMesh → SubdivideMesh → MeshNoiseDeform → Output`，生成岩石等自然形态。

---

### TransformMesh

**类别**：Mesh

**功能**：对网格执行三轴平移、旋转和缩放变换。变换顺序：缩放 → 旋转 → 平移。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Mesh | `SpatialMesh` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `translateX` | number | 0 | X 轴平移量 |
| `translateY` | number | 0 | Y 轴平移量 |
| `translateZ` | number | 0 | Z 轴平移量 |
| `rotationX` | number | 0 | X 轴旋转角度（度数） |
| `rotationY` | number | 0 | Y 轴旋转角度（度数） |
| `rotationZ` | number | 0 | Z 轴旋转角度（度数） |
| `scaleX` | number | 1.0 | X 轴缩放系数 |
| `scaleY` | number | 1.0 | Y 轴缩放系数 |
| `scaleZ` | number | 1.0 | Z 轴缩放系数 |

**执行逻辑**：
1. 读取变换参数
2. 对每个顶点先缩放：`(x × scaleX, y × scaleY, z × scaleZ)`
3. 绕 X/Y/Z 轴旋转（度数转弧度）
4. 平移：加上 `(translateX, translateY, translateZ)`
5. 输出变换后的网格

**用法示例**：

```json
{
  "id": "tm",
  "type": "TransformMesh",
  "position": { "x": 600, "y": 0 },
  "data": { "translateY": 2.0, "rotationY": 90, "scaleX": 1.5 }
}
```

---

### LoftMesh

**类别**：Mesh

**功能**：对两个及以上 profile spline 做统一列数重采样并沿指定轴排序，生成 profile 之间的 polygon loft；输入 `profiles` 为 variadic `SpatialSpline`，输出 `out` 为 `SpatialMesh`。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `columns` | 32 | 每条 profile 的采样列数，2–256 |
| `sortAxis` | `x` | profile 排序轴：`x` / `y` / `z` |
| `closedProfile` | true | profile 是否首尾闭合 |
| `capStart` / `capEnd` | true | 生成起止端盖 |
| `autoAlign` | true | 自动对齐相邻 profile 的起始列和方向 |
| `shadeMode` | `auto` | `auto` / `smooth` / `flat` |
| `cuspAngle` | 30 | Auto 法线折角阈值，0–180° |

输出 face groups 为 `side`、条件性的 `cap_start` / `cap_end`，并维护 boundary edge group `unshared`。少于两条有效 profile 时失败。

### MirrorMesh

**类别**：Mesh

**功能**：关于 `axis=value` 平面镜像 polygon geometry，并反转镜像面的 winding。输入/输出均为 `SpatialMesh`。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `axis` | `z` | `x` / `y` / `z` |
| `offset` | 0 | 镜像平面的轴坐标 |
| `mergeOriginal` | true | 原件与镜像件是否合并输出 |
| `weldSeam` | true | 合并时是否 Fuse 镜像缝 |
| `weldTolerance` | 0.0001 | 缝合容差 |

镜像拓扑通过 destination→source remap 传播 point/vertex/primitive/detail attributes、UV、颜色、材质和 groups；Position / Vector / Normal role 使用反射矩阵更新。

### FuseMesh

**类别**：Mesh

**功能**：按空间量化容差焊接重合 points，重映射 faces 并可删除退化面/重复面。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `tolerance` | 0.0001 | point weld 容差，必须大于 0 |
| `removeDegenerate` | true | 删除不足三个不同 point 的面及重复面 |

焊接后的属性采用稳定 first-source 语义，所有 owner 的 cardinality 与 groups 会随 remap 更新；最后重建 `unshared` 边组。

### PolyExtrude

**类别**：Mesh

**功能**：沿所选 polygon 的面法线挤出 top 和 side polygons，保持 polygon topology 到最终 Sink。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `faceGroup` | `""` | 可选 face group；空值选择全部面 |
| `distance` | 0.02 | 法线方向挤出距离 |
| `inset` | 0 | top 相对面中心的 inset 比例 |
| `keepOriginal` | false | 是否保留被挤出的原面 |
| `topGroup` | `extrude_top` | 新 top face group 名 |
| `sideGroup` | `extrude_side` | 新 side face group 名 |

生成元素从来源 face/corner/point 继承 attributes、UV 和材质；Position role 属性跟随新 point 位移。输出同时维护动态 top/side groups 与 `unshared`。

### LotSubdivision

**类别**：Mesh

**功能**：对齐 SideFX Labs Lot Subdivision——迭代将 polygon 面切成更小的 lot 面，控制最小尺寸、迭代次数与不规则度。输出保留 n-gon 拓扑（`emit_geometry`），并带 primitive `lotid` 与 face group `lots`，可直接接 `PolyExtrude` / 散布链路。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `minSize` | `1.0` | 面平面 AABB **短边**低于此值不再切分（扁平 Box 侧面不会被误切） |
| `iterations` | `3` | 切分轮数；矩形上约得到 `2^iterations` 个 lot（受 `minSize` 截断） |
| `irregularity` | `0.5` | `0` 为中点切分，越大切点越偏，lot 尺寸越不均匀 |
| `seed` | `0` | 随机种子（与 `graph_seed` 异或） |
| `alignment` | `longestEdge` | `longestEdge` 沿最长边垂直切开；`boundingBox` 按世界平面 AABB 长轴切开 |

**输入 Pin**：`in: SpatialMesh`（polygon faces）  
**输出 Pin**：`out: SpatialMesh`（lot faces）  
**输出组**：`lots`（face，全部 lot）

```json
{
  "id": "lots",
  "type": "LotSubdivision",
  "data": {
    "minSize": 2.0,
    "iterations": 3,
    "irregularity": 0.35,
    "seed": 1,
    "alignment": "boundingBox"
  }
}
```

> 典型连接：`CreateGridMesh / 平面 polygon → LotSubdivision → PolyExtrude → Output`（地块挤出）；或 `LotSubdivision →` 面中心点/`CopyMeshToPoints` 散布建筑。示例：`examples/lot-extrude-demo.pcg`、`examples/lot-city-demo.pcg`。

### CopyMesh

**类别**：Mesh

**功能**：在一个节点内做固定数量的线性或环形 Geometry 复制；通用“按点复制”仍使用 `CopyMeshToPoints`。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `mode` | `circular` | `circular` / `linear` |
| `count` | 6 | 副本数量，1–256 |
| `axis` | `x` | 环形旋转轴 |
| `angle` | 360 | 全部副本覆盖的角度 |
| `translateX/Y/Z` | 0 | linear 模式每个序号的平移增量 |
| `centerX/Y/Z` | 0 | circular 模式旋转中心 |

每个实例都保留 topology 和通用属性；Position / Vector / Normal role 使用对应实例 affine 变换。同名 groups 按 Houdini Copy 语义合并成员，不添加会破坏下游 selector 的副本前缀，随后重建边界组。

### ShellMesh

**类别**：Mesh

**功能**：沿平均 point normal 生成 outer/inner 两层 polygon shell，并可在输入 boundary 上补 rim faces。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `thickness` | 0.02 | 壳厚，必须大于 0 |
| `direction` | `centered` | `centered` / `outward` / `inward` |
| `closeBoundaries` | true | 是否为开口边生成 rim |
| `outerGroup` | `shell_outer` | 外层 face group |
| `innerGroup` | `shell_inner` | 内层 face group |
| `rimGroup` | `shell_rim` | 边界墙 face group |

Inner faces 会反转 winding；新层与 rim 使用拓扑 remap 传播各 owner 属性、UV 和材质，Position role 属性跟随壳体位移。输出维护三个动态 face groups 与 `unshared`。

---

### ImportMesh

**功能**：从文件系统读取 OBJ / FBX / glTF 资产，转换为 `PcgGeometry` 并以 `emit_geometry()` 输出。场景节点变换、多 mesh、UV、顶点色、法线、材质名会被保留；每个导入实例同时生成 primitive `name` 属性和同名 face group。

**输入 Pin**：无；若宿主按本节点 ID 上传 `PcgMeshSlot`，运行时 mesh 优先于文件路径。

**输出 Pin**：`out`（`SpatialMesh`）

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `path` | `""` | 绝对路径，或相对工程根的资产路径 |
| `projectRoot` | `""` | 可选；相对路径的明确解析根。空时相对宿主进程工作目录 |
| `scale` | 1.0 | 导入后的统一单位缩放，必须大于零且有限 |
| `axisConversion` | `none` | `none` / `zUpToYUp` / `yUpToZUp` |

文件不存在、格式不支持、没有 polygon 或 Assimp 校验失败都会返回包含路径/Assimp 原因的明确错误。Cook cache 的输入指纹包含规范化路径、文件大小和修改时间，因此替换外部资产会使节点失效重算。

### MatchSize

**功能**：Houdini Match Size 对齐的 bbox 匹配：Translate / Justify / Offset、Scale to Fit、Uniform Scale + Scale Axis、Restore/Stash Transform。不改变 points/faces 拓扑、groups、材质或非变换属性。

**输入 Pin**：`source`（必需 `SpatialMesh`）、`reference`（可选 `SpatialMesh`，标签 Destination Size）。

**输出 Pin**：`out`（`SpatialMesh`）

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `group` / `groupType` | `""` / `guess` | 仅变换子集；空 group 变换全部 |
| `justifyWith` | `inputIfWired` | `inputIfWired` / `locationAndSize` / `secondInput` / `originAndUnitSize` |
| `useGroupsForBounds` | false | 用 `sourceGroup` / `targetGroup` 计算 justification bbox |
| `targetPosition` | `[0,0,0]` | 无 reference 时的目标锚点（Min/Max 时表示边，Center 时表示中心） |
| `targetSize` | `[1,1,1]` | 无 reference 时的目标尺寸；各分量必须非负 |
| `translate` | true | 是否平移对齐 |
| `justifyX/Y/Z` | `center` | source：`none` / `min` / `center` / `max` |
| `targetJustifyX/Y/Z` | `same` | target：`same` / `min` / `center` / `max` |
| `offset` | `[0,0,0]`（或 `offsetX/Y/Z`） | 各轴额外偏移 |
| `scaleToFit` | true | 是否缩放到目标 bbox |
| `uniformScale` | true | 等比缩放 |
| `scaleAxis` | `bestFit` | 等比时轴策略：`x` / `y` / `z` / `bestFit` |
| `scaleX/Y/Z` | true | 非等比时各轴是否缩放 |
| `restoreTransform` / `restoreAttribute` | false / `xform` | 先应用 detail 矩阵的逆变换 |
| `stashTransform` / `stashAttribute` | true / `xform` | 把本次 4×4 写入 detail float16 |

兼容旧图：`targetCenter*`、`sourceJustify*`、`uniformScaleMode`（`fit`→`bestFit`，`fill` 仍取最大轴比）。

> 通用装配：`ImportMesh → MatchSize → CopyMeshToPoints`；需要下垂/弧形时在复制前接 `BendMesh`。沿线 chain 继续使用 `InstanceAlongSpline`，或 `ResampleSpline → CopyMeshToPoints`，不增加重复的专用节点。

### BendMesh

**功能**：围绕 capture frame 做直线 Bend，保持面连通与全部拓扑语义。首版不伪装成完整 Houdini Bend：不包含 Twist、Taper、双 capture 或 Lattice。

**输入 Pin**：`source`（必需 `SpatialMesh`）、`rest`（可选 `SpatialMesh`）。Rest 必须与 source point/face 拓扑完全一致；其位置用于计算变形坐标，source-rest offset 会随局部 frame 旋转。

**输出 Pin**：`out`（`SpatialMesh`）

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `captureOriginX/Y/Z` | 0 | 捕获起点 |
| `captureDirectionX/Y/Z` | (0,1,0) | 捕获轴；会归一化 |
| `upDirectionX/Y/Z` | (1,0,0) | 弯曲径向；不得与捕获轴平行 |
| `captureLength` | 1 | 从 0 到完整角度的长度，必须大于 0 |
| `angle` | 0 | 完整弯曲角度（度） |
| `maskAttribute` | `bendmask` | 输出 point float mask；空字符串可关闭 |

捕获区之前保持不动；区间内按恒定曲率弯曲；区间之后沿末端切线刚性延伸。带 `Position` / `Vector` / `Normal` transform role 的 float3+ 属性会使用各 owner 的位置同步变换。

---

### MergeMesh

**类别**：Mesh

**功能**：合并多个输入网格为一个。顶点直接拼接，不做布尔运算。支持变长输入（variadic），可连接任意数量的 mesh。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 变长 |
|--------|------|------|------|
| `in` | Meshes | `SpatialMesh` | ✅ |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**属性**：无

**执行逻辑**：
1. 遍历所有连接到输入端的 mesh（不限数量）
2. 依次拼接顶点数组
3. 三角形索引偏移后追加
4. 输出合并后的网格

**用法示例**：

```json
{
  "id": "merge",
  "type": "MergeMesh",
  "position": { "x": 600, "y": 0 },
  "data": {}
}
```

> 典型连接：`CreateBoxMesh(A) + CreateBoxMesh(B) + CreateBoxMesh(C) → MergeMesh → Output`，简单拼接几何体。一个 MergeMesh 节点即可替代多个链式 MergeMesh。

---

### BooleanMesh

**类别**：Mesh

**功能**：对两个网格执行 CSG 布尔运算（并/交/差/碎裂）。支持实体和表面模式，输出带分类组的网格。基于 CDT（Constrained Delaunay Triangulation）实现精确布尔切割。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `a` | Mesh A | `SpatialMesh` |
| `b` | Mesh B | `SpatialMesh` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**输出组**：

| 组名 | 域 | 说明 |
|------|-----|------|
| `a_inside_b` | face | A 在 B 内部的面 |
| `a_outside_b` | face | A 在 B 外部的面 |
| `b_inside_a` | face | B 在 A 内部的面 |
| `b_outside_a` | face | B 在 A 外部的面 |
| `ab_seams` | edge | A-B 交线边 |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `operation` | enum | `"subtract"` | 布尔运算：`union`（并集）/ `intersect`（交集）/ `subtract`（差集 A-B）/ `shatter`（碎裂） |
| `treatAAs` | enum | `"solid"` | A 的几何类型：`solid`（实体）/ `surface`（表面） |
| `treatBAs` | enum | `"solid"` | B 的几何类型：`solid`（实体）/ `surface`（表面） |
| `useSelf` | boolean | false | 是否对 A 自身执行自布尔运算 |
| `detriangulate` | enum | `"all"` | 去三角化：`all`（按输入面来源重建）/ `unchanged`（仅重建未被切割的输入面）/ `none`（保留三角） |
| `weldEpsilon` | number | 0.0001 | 焊接容差（≥ 1e-8） |
| `triangleBudget` | integer | 500000 | 三角形数量上限（≥ 1000） |
| `timeoutMs` | integer | 0 | 墙钟超时（毫秒）。`0` = 不超时。在交线候选循环中 best-effort 中止，返回可观测错误（或见 `onFailure`） |
| `onFailure` | enum | `"error"` | `error`：cook 失败并带明确消息（cancel/timeout/budget）；`passthroughA`：输出 A 并继续（避免空结果，失败细节仅在 `error` 路径可见） |

**执行逻辑**：
1. 读取两个输入网格，按 `weldEpsilon` 焊接重合顶点
2. 计算两网格的相交线，将面沿交线切割（循环中响应 `ctx.is_cancel_requested` 与 `timeoutMs`）
3. 根据 `operation` 选择保留的面：
   - `union`：保留 A 外部 + B 外部的面
   - `intersect`：保留 A 内部 + B 内部的面
   - `subtract`：保留 A 外部的面，去除 A 内部的面
   - `shatter`：将 A 沿 B 的切割面碎裂为多个独立片
4. 根据 `treatAAs`/`treatBAs` 调整整/表面模式下的内部/外部判定
5. `detriangulate` 按 Houdini Boolean 语义重建输入面：`all` 只合并来自同一输入 polygon 的相邻三角；`unchanged` 进一步排除被交线切割的输入面；A-B seam 边不会被跨越
6. 标记输出组（a_inside_b / a_outside_b / b_inside_a / b_outside_a / ab_seams）
7. 若三角形数超过 `triangleBudget`，或 cancel/timeout，报错终止（除非 `onFailure=passthroughA`）
8. 输出布尔运算结果网格

**稳定性提示**：
- 多 cutter / 高密度交线可能很慢；设 `timeoutMs` 并确保失败路径可观测（默认 `onFailure=error`），不要依赖静默挂死。
- **不要**用一长串亚毫米 `CreateCylinderMesh` cutter 作为 jimping/锯齿的唯一手段；优先 `OutlineSolid` / 剖面包络，或更大、更少的 cutter。

**用法示例**：

```json
{
  "id": "bool",
  "type": "BooleanMesh",
  "position": { "x": 600, "y": 0 },
  "data": {
    "operation": "subtract",
    "treatAAs": "solid",
    "treatBAs": "solid",
    "detriangulate": "all",
    "weldEpsilon": 0.0001,
    "timeoutMs": 30000,
    "onFailure": "error"
  }
}
```

> 典型连接：`GetMeshData(A) + CreateBoxMesh(B) → BooleanMesh(subtract) → Output`，从实体中挖洞/开槽。

---

### OutlineSolid

**类别**：Mesh

**功能**：闭合平面轮廓 × 厚度 → 焊接实体（front / back / rim），或列向 loft（对齐 img2threejs `loft(outline,zAt)`）。用 **`inputMode`** 显式选择路径（不按有无数据自动切换）。

**输入 Pin**（随 `inputMode` 显隐）：

| Pin ID | 标签 | 类型 | 模式 |
|--------|------|------|------|
| `outline` | Outline | `SpatialSpline` | `outline` |
| `spine` | Spine | `SpatialSpline`（`(x, y_top, half_z)`） | `spineEdge` |
| `edge` | Edge | `SpatialSpline`（`(x, y_bot, half_z)`） | `spineEdge` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Mesh | `SpatialMesh` |

**输出组**：

| 组名 | 域 | 说明 |
|------|-----|------|
| `front` | face | 厚度轴正侧盖面（可用 `frontGroup` 改名） |
| `back` | face | 厚度轴负侧盖面 |
| `rim` | face | 侧面环面 |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `inputMode` | enum | `"outline"` | `outline` / `columnsJson` / `spineEdge`（必选，决定读哪路输入） |
| `thickness` | number | 0.01 | 常量总厚度（m）；列模式也作 half_z 回退 |
| `thicknessAxis` | enum | `"z"` | `x` / `y` / `z` |
| `thicknessSamples` | string | `"[]"` | 仅 `outline`：逐顶点总厚度 |
| `columnsJson` | string | `"[]"` | 仅 `columnsJson`：站数组 |
| `rows` | integer | 0 | 仅列模式：余弦分级行数（0→1 带） |
| `profileMode` | enum | `"constant"` | 仅列模式：`constant` / `slab` / `blade` |
| `spineRollFrac` / `handleRollFrac` / `edgeFrac` / `grindStartFrac` / `edgeBevelFrac` | number | 见 manifest | blade/slab 截面参数 |
| `frontGroup` | string | `"front"` | 前盖面组名 |
| `backGroup` | string | `"back"` | 后盖面组名 |
| `rimGroup` | string | `"rim"` | 侧面组名 |

**执行逻辑**：
1. 读 `inputMode`，只走对应分支（缺输入则报错，不回退到其它模式）
2. `outline`：闭合轮廓 ±half；可选 `thicknessSamples`
3. `columnsJson` / `spineEdge`：列 × 行焊接 loft（`profileMode` = zAt）
4. 维护 `unshared` 边组

> `inputMode=outline`：`CreateSpline(closed) → OutlineSolid.outline`  
> `inputMode=columnsJson`：`OutlineSolid(columnsJson=…, rows=8, profileMode=blade)`  
> `inputMode=spineEdge`：`CreateSpline(spine)+CreateSpline(edge) → OutlineSolid`

---

## Geometry 类别

Geometry 类别节点操作 `PcgGeometry`（多边形网格 + 命名组 + 属性），是 Houdini Group SOP 的对等实现。节点间传递的 `SpatialMesh` pin 在内部携带 `PcgGeometry`，包含拓扑面/边/组语义，而非仅三角汤。

### Group 系统

Group 是 PCG 几何管线的核心概念，参考 Houdini 的 Group SOP + PolyBevel group 参数模式：

- **生产者节点**（如 `SweepAlongSpline`）在 manifest 中声明 `outputGroups`，在执行时将几何元素（边/面/点）归入命名组
- **消费者节点**（如 `BevelMesh`）通过 `edgeGroup` / `excludeGroups` 参数按组名选择操作范围
- **GroupCreate / GroupCombine / GroupPromote** 是中间过滤节点：按规则筛选、组合，或在 Point/Edge/Face 域之间提升组，供下游使用

#### 支持的域

| 域 | 说明 |
|----|------|
| `edge` | 边（两个顶点之间的连线） |
| `face` | 面（多边形面片） |
| `point` | 点（顶点位置） |

#### 上游组自动发现

Inspector 中的 `groupSelect` / `groupMultiSelect` 属性会自动遍历上游 SpatialMesh 边，收集所有可用组名并显示为下拉选择 / 复选框。无需手动输入组名。

#### SweepAlongSpline 输出组

`SweepAlongSpline` 是目前主要的组生产者，输出以下 6 个命名组：

| 组名 | 域 | 条件 | 含义 |
|------|-----|------|------|
| `side` | face | 总是 | 沿 backbone 的侧壁面 |
| `cap_start` | face | `capStart=true` | 起始端盖面 |
| `cap_end` | face | `capEnd=true` | 结束端盖面 |
| `seam` | edge | 总是 | 截面闭合处的缝合边（沿扫掠方向） |
| `profile_corner` | edge | 总是 | 截面折角处的纵向棱边（连接相邻 ring） |
| `unshared` | edge | 总是 | 边界边（只有一侧面的边） |

> `seam` 来源于截面轮廓形状，角度由路径曲率产生；`profile_corner` 来源于截面折角，角度由截面形状决定。Bevel 截面圆角时使用 `fromEdgeGroup=profile_corner`。

---

### GroupCreate

**类别**：Geometry

**功能**：从上游几何中按规则筛选边/面/点，归入一个命名组输出。参考 Houdini Group SOP。是连接生产者（如 `SweepAlongSpline`）和消费者（如 `BevelMesh`）的中间过滤节点。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Geometry | `SpatialMesh` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Geometry | `SpatialMesh` |

**输出组**：

| 组名 | 域 | 说明 |
|------|-----|------|
| `outputGroup` 属性值 | 同 `domain` 属性 | 动态命名组，组名由 `outputGroup` 属性决定 |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `outputGroup` | string | `"bevel_edges"` | — | 新建的组名。下游节点用此名称引用 |
| `domain` | enum | `"edge"` | `edge` / `face` / `point` | 操作域。当前仅 `edge` 域支持 angle 模式 |
| `mode` | enum | `"angle"` | `angle` / `unshared` | 选择模式。`angle` = 按相邻面夹角选边；`unshared` = 选边界边 |
| `minEdgeAngle` | number | `30.0` | 0 ~ 180 | **angle 模式**：选中相邻面夹角 ≥ 此值的边。值越大 = 只有越锐的转角才入选 |
| `includeUnshared` | boolean | `false` | — | 是否包含边界边（只有一侧面的边）。默认排除，对齐 Houdini PolyBevel |
| `fromEdgeGroup` | groupSelect | `""` | 上游可用边组 | 先从已有边组中筛选候选边，再应用 mode 规则。留空 = 考虑所有边 |
| `fromFaceGroup` | groupSelect | `""` | 上游可用面组 | 仅选与该面组中的面相邻的边 |

**执行逻辑**：

```
候选边集 = 所有边（或 fromEdgeGroup 指定的边集）
    ↓
fromFaceGroup 过滤：仅保留与指定面组相邻的边
    ↓
includeUnshared 过滤：若 false，排除边界边（face1 < 0）
    ↓
mode=angle: 保留相邻面夹角 ≥ minEdgeAngle 的边
mode=unshared: 保留所有边界边（忽略 fromEdgeGroup / fromFaceGroup / angle）
    ↓
选中的边 → 归入 outputGroup 命名组
```

> **关键**：`fromEdgeGroup` 是串联过滤的第一步。Sweep 生成的 mesh 有两类内部边 — `seam`（截面轮廓边）和 `profile_corner`（截面折角棱边）。不设 `fromEdgeGroup` 时 angle 模式会选中曲面内部三角化对角线，导致下游 Bevel 产生锯齿破面。

**用法示例**：

bridge-demo 桥梁截面圆角：

```
CreateSpline(backbone) ─┐
                        ├─→ SweepAlongSpline ──→ GroupCreate ──→ BevelMesh ──→ Output
CreateSpline(profile) ──┘
```

```json
{
  "id": "grp",
  "type": "GroupCreate",
  "position": { "x": 212, "y": 320 },
  "data": {
    "outputGroup": "bevel_edges",
    "domain": "edge",
    "mode": "angle",
    "minEdgeAngle": 30,
    "includeUnshared": false,
    "fromFaceGroup": "",
    "fromEdgeGroup": "profile_corner"
  }
}
```

下游 BevelMesh 引用此组：

```json
{
  "id": "bev",
  "type": "BevelMesh",
  "data": {
    "edgeGroup": "bevel_edges",
    "excludeGroups": "cap_start,cap_end",
    "amount": 0.425,
    "segments": 6
  }
}
```

> Inspector 底部绿字 "N groups available from upstream" 表示已成功发现上游输出的 N 个组。下拉框中可选择这些组名。

---

### GroupCombine

**类别**：Geometry

**功能**：对已有的多个组执行集合运算（并/交/差），生成新的组合组。参考 Houdini Group Combine SOP。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Geometry | `SpatialMesh` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Geometry | `SpatialMesh` |

**输出组**：

| 组名 | 域 | 说明 |
|------|-----|------|
| `outputGroup` 属性值 | 同 `domain` 属性 | 动态命名组，组名由 `outputGroup` 属性决定 |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `outputGroup` | string | `"combined"` | 新建的组名 |
| `domain` | enum | `"edge"` | 操作域：`edge` / `face` / `point` |
| `operation` | enum | `"union"` | 集合运算：`union`（并集）/ `intersect`（交集）/ `subtract`（差集） |
| `sourceGroups` | groupMultiSelect | `""` | 逗号分隔的源组名列表。复选框从上游可用组中选择 |

**执行逻辑**：

1. 清除 outputGroup 原有成员
2. `union`：将所有 sourceGroups 的成员取并集
3. `intersect`：取所有 sourceGroups 的交集（≥2 个源组）
4. `subtract`：以第一个源组为基准，依次减去后续源组

**用法示例**：

```json
{
  "id": "comb",
  "type": "GroupCombine",
  "position": { "x": 300, "y": 320 },
  "data": {
    "outputGroup": "all_sharp_edges",
    "domain": "edge",
    "operation": "union",
    "sourceGroups": "bevel_edges,profile_corner,seam"
  }
}
```

> 典型用途：将多个 GroupCreate 的输出合并为一个组，或用 subtract 排除某些边（如从 `profile_corner` 中减去 `cap_start` 的边）。

### GroupPromote

**类别**：Geometry

**功能**：在 Point / Edge / Face（Primitives）组之间转换，对齐 Houdini [Group Promote SOP](https://www.sidefx.com/docs/houdini/nodes/sop/grouppromote.html)。典型用途：把朝街点组 `streetFacingPoints` 提升为边组 `streetFacingEdges`（两端点都在源组内才入选）。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Geometry | `SpatialMesh` |

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Geometry | `SpatialMesh` |

**输出组**：

| 组名 | 域 | 说明 |
|------|-----|------|
| `newName`（空则用 `groupName`） | 同 `to` | 提升后的目标域组 |

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `numberOfPromotions` | integer | `1` | 提升规则数量（当前单条规则；预留多规则） |
| `convertFrom` | enum | `"point"` | Points / Vertices / Primitives / Edges |
| `to` | enum | `"edge"` | Points / Vertices / Primitives / Edges |
| `groupName` | groupSelect | `""` | 源组名 |
| `newName` | string | `""` | 新组名；空 = 沿用源名 |
| `keepOriginalGroup` | boolean | `false` | 是否保留源组 |
| `includeOnlyOnBoundary` | boolean | `false` | 先转换再只保留边界元素 |
| `includeUnsharedEdges` | boolean | `true` | 仅 Boundary：边界是否含 unshared 边 |
| `includeAllUnsharedCurveEdges` | boolean | `true` | 仅 Boundary+Unshared：是否含曲线 unshared 边 |
| `useConnectivityAttribute` | boolean | `false` | 仅 Boundary：用属性不连续当边界 |
| `connectivityAttribute` | string | `"uv"` | Connectivity Attribute 名 |
| `connectivityAttributeTolerance` | number | `0.0001` | 浮点属性容差 |
| `includeAllPrimitivesSharingAttributeBoundaryPoints` | boolean | `false` | 仅 Boundary 且 To=Primitives |
| `includeOnlyEntirelyContained` | boolean | `true` | 非 Boundary 且 To∈{Edges,Primitives,Vertices} |
| `includeOnlyPrimitivesSharingEdge` | boolean | `false` | 非 Boundary 且 To=Primitives |
| `removeDegenerateBridges` | boolean | `false` | To∈{Points,Edges,Vertices}：去掉退化桥接 |
| `outputAsIntegerAttribute` | boolean | `false` | To∈{Points,Primitives,Vertices}：写成 0/1 属性并删除组 |

灰显规则与 [Houdini Group Promote](https://www.sidefx.com/docs/houdini/nodes/sop/grouppromote.html) 一致。

**执行逻辑（Points → Edges, entirely contained）**：

```
枚举网格所有无向边 (a,b)
  ↓
仅当 a、b 均属于 groupName 点组 → 加入 newName 边组
  ↓
keepOriginalGroup=false → 删除源点组
```

**用法示例**：

```json
{
  "id": "grouppromote1",
  "type": "GroupPromote",
  "data": {
    "convertFrom": "point",
    "to": "edge",
    "groupName": "streetFacingPoints",
    "newName": "streetFacingEdges",
    "keepOriginalGroup": false,
    "includeOnlyEntirelyContained": true,
    "includeOnlyOnBoundary": false
  }
}
```

### FaceGroupByNormal

**类别**：Geometry

**功能**：按面法线与目标方向的夹角建立 face group，适合选择顶面、底面或朝向特定方向的面，再交给 `AssignMaterial` 做局部材质覆盖。

| 属性名 | 类型 | 默认值 | 说明 |
|---|---|---|---|
| `outputGroup` | string | `material_faces` | 输出 face group 名称 |
| `directionX/Y/Z` | number | `0/1/0` | 目标方向，会自动归一化 |
| `spreadAngle` | number | `30` | 法线允许偏离目标方向的最大角度（0–180°） |

典型连接：`Geometry → FaceGroupByNormal(outputGroup="top") → AssignMaterial(group="") → AssignMaterial(group="top") → Output`。

---

## Texture 类别

### ImageTexture

**类别**：Texture

**功能**：加载 2D 纹理图像，输出 `Texture` 类型数据供下游节点（如 `MeshNoiseDeform`）使用。支持纹理平铺重复。

**输入 Pin**：无

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Texture | `Texture` |

**属性**：

| 属性名 | 类型 | 默认值 | 范围 | 说明 |
|--------|------|--------|------|------|
| `texture` | texture2d | `""` | — | 纹理资源路径 |
| `repeatX` | number | 1.0 | 0.01 ~ 64 | U 方向平铺次数 |
| `repeatY` | number | 1.0 | 0.01 ~ 64 | V 方向平铺次数 |

**执行逻辑**：
1. 读取 `texture` 指定的纹理资源
2. 按 `repeatX`/`repeatY` 设置纹理平铺
3. 输出 `Texture` 类型数据

**用法示例**：

```json
{
  "id": "tex",
  "type": "ImageTexture",
  "position": { "x": 0, "y": 200 },
  "data": { "texture": "Assets/Textures/noise.png", "repeatX": 4.0, "repeatY": 4.0 }
}
```

> 典型连接：`ImageTexture → MeshNoiseDeform(texture)`，使用自定义纹理驱动 mesh 变形。

---

## 建筑生成核心节点

### CopyMeshToPoints

**类别**：Mesh

**功能**：将一份原型 Geometry 复制到输入点云的每个点，并合并为一份 Geometry。对应 Houdini `copytopoints`，中间过程保持 polygon、group、UV、color 与逐面材质，不做 geometry→mesh→geometry 往返。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `prototype` | Prototype | `SpatialMesh` | 要复制的原型 Geometry/Mesh |
| `points` | Points | `SpatialPoint` | 放置点及点属性 |

**输出 Pin**：`out`（`SpatialMesh`，内部保持 `PcgGeometry`）

**点属性约定**：

| 属性 | 类型 | 说明 |
|------|------|------|
| `P` | 点的 `x/y/z` | 实例平移 |
| `pscale` | number | Houdini 风格统一缩放 |
| `scale` | number / `[x,y,z]` / `{x,y,z}` | 统一或逐轴缩放，与 `pscale` 相乘 |
| `scaleX/Y/Z` | number | 额外逐轴缩放 |
| `rotationX/Y/Z`（或 `rx/ry/rz`） | number | XYZ 欧拉角，单位为度 |
| `orient` | `[x,y,z,w]` / `{x,y,z,w}` | 四元数；存在时优先于 frame 属性 |
| `nx/ny/nz` + `tx/ty/tz` | number | 与 `SampleAlongSpline` 一致的 normal/tangent frame |
| `Cd` | `[r,g,b]` / `{r,g,b[,a]}` | 每副本顶点色；有原型色时 RGB 相乘并保留原型 alpha，无原型色时填充 |
| `material` | string | 覆盖该副本全部面的材质名 |

原型以自身局部原点为放置基准，不自动居中。每个副本先 scale，再应用欧拉旋转与 `orient`/frame，最后平移到点坐标。

---

### AttributeRandomize

**类别**：Transform

**功能**：以 `graph_seed + seed` 为确定性随机源，随机偏移点位置，并写入供 `CopyMeshToPoints` 消费的旋转、统一缩放、颜色（`Cd`）与材质名属性。

**输入/输出 Pin**：`in` → `out`，均为 `SpatialPoint`。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `seed` | 0 | 节点随机种子，与 graph seed 组合 |
| `translateX/Y/Z` | 0 | 各轴对称随机幅度 `[-value,+value]`，直接修改点坐标 |
| `rotateX/Y/Z` | 0 | 各轴对称欧拉角幅度，写入 `rotationX/Y/Z` |
| `scaleMin/scaleMax` | 1 / 1 | 统一缩放范围，写入 `scale`；上下限反置时自动交换 |
| `colorMinR/G/B` | 1 / 1 / 1 | 颜色下界；与 Max 全为 1 时不写 `Cd` |
| `colorMaxR/G/B` | 1 / 1 / 1 | 颜色上界；写入 `Cd = [r,g,b]` |
| `materialNames` | `""` | 逗号分隔材质名列表；非空时随机写入 `material` |

相同 graph seed、节点 seed 和输入点序列必定得到相同结果。

---

### Switch

**类别**：Flow

**功能**：按整数 `index` 在固定四路 `in0`–`in3` 中选择一路 Geometry/Mesh 透传到 `out`。输入/输出 Pin 均为 `SpatialMesh`，内部会保持选中分支的 `PcgGeometry` 或 `PcgMeshData` 载荷。

`index` 会 clamp 到 `[0,3]`；clamp 后对应输入未连接时执行失败。当前执行器按拓扑顺序计算所有上游分支，Switch 只负责选择结果，不提供惰性分支求值。

---

## Output 类别

### Output

**类别**：Output

**功能**：终端输出节点。将输入数据原样透传到输出，标记图的结束点。Houdini 风格的 passthrough 节点。

**输入 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `in` | Input | `Any` |

**输出 Pin**：无

**属性**：

| 属性名 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `label` | string | `"Output"` | 输出标签名（仅用于标识，不影响执行） |

**执行逻辑**：
1. 读取 `in` 输入（缺失则报错）
2. 将数据原样存入输出（类型标记为 `Unknown`）
3. 执行引擎优先将 `Output` 节点作为图的汇点

**用法示例**：

```json
{
  "id": "out",
  "type": "Output",
  "position": { "x": 1200, "y": 0 },
  "data": { "label": "FinalResult" }
}
```

> 可连接到任何类型的输出 pin（`Any` 类型接受任意输入）。

### ExportFBX

**类别**：Output（Editor Only）

**功能**：显式把所连上游的 polygon Geometry 写成 FBX。它是类似 Houdini ROP 的手动副作用节点：普通 Editor 预览和 Player cook 会把它处理为被动 Output，绝不会自动写文件；只有 Unity Inspector 的 Export 操作会同步 cook 上游并调用原生 FBX exporter。

**输入 Pin**：`in`（`SpatialMesh`）；**输出 Pin**：无。

| 属性 | 默认值 | 说明 |
|------|--------|------|
| `path` | `Exports/$GRAPH.fbx` | 工程相对或绝对路径；支持 `$GRAPH` / `$NODE`，缺少 `.fbx` 时自动追加 |
| `scale` | 1.0 | 导出单位缩放，必须大于 0 |
| `generateNormals` | true | 是否由 exporter 生成 smooth normals |

执行前必须连接 Geometry。导出控制器会等待异步预览 cook、应用场景参数覆盖和 Mesh/Spline/Texture bindings，再只 cook Export 上游；失败会显示明确错误，不产生静默空文件。

---

## 常见节点组合

节点、Manifest 或 native core 发生变化后，提交前执行：

```bash
./scripts/build-pcg-core.sh --run-tests
scripts/sync-manifest.sh
```

第一条构建 shared/static core、运行 fast 测试并复制/签名 macOS Unity 插件；第二条把 source-of-truth `schema/node-manifest.json` 同步到 Unity Editor 与 Resources 两个消费者。

### 建筑楼层 / 开间阵列

最小建筑连线由点网格驱动，不需要手摆多份 Box：

```text
CreatePointGrid → AttributeRandomize → CopyMeshToPoints(points) → Output
CreateBoxMesh ───────────────────────→ CopyMeshToPoints(prototype)
```

规整立面可将 `AttributeRandomize` 幅度保持为 0；错位塔可设置水平平移、Y 轴旋转与 scale 范围。可选部件使用 `Switch` 在多路 Geometry 中选通，再进入后续 Merge。设置 `colorMin/Max` 可让每栋建筑获得不同立面色；窗户分支可先接 `VertexColor(emission>0)` 再 Merge。

### 建筑立面：已有节点即可实现的能力

以下能力**不需要新节点**，用现有节点组合即可（Houdini 对标）：

| 效果 | 推荐链路 |
|------|----------|
| 消防梯 zigzag | `CreatePoints` → `AttributeWrangle`(`@P.x` 交替 + `@rotationY`) → `CopyMeshToPoints` |
| 栏杆 | `CreateSpline` → `SweepAlongSpline`(圆截面扶手) + `SampleAlongSpline` → `CopyMeshToPoints`(栏杆柱) |
| 台阶 / stoop | `CreatePoints` → `AttributeWrangle`(`@P.y/@P.z` 递进) → `CopyMeshToPoints`(台阶 box) |
| 多立面分组 | 串联多个 `FaceGroupByNormal`（不同 direction + outputGroup） |
| Boolean 多 cutter | `MergeMesh` 所有 cutter → `BooleanMesh`(A=wall, B=merged, subtract) |

### 1. 基础点生成流水线

最简图：生成 → 放置。

```
SpawnPoints ──(Points)──→ PlaceInScene
```

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "sp", "type": "SpawnPoints", "position": {"x":0,"y":0}, "data": {"count":100,"radius":10.0} },
    { "id": "place", "type": "PlaceInScene", "position": {"x":300,"y":0}, "data": {"prefab":"Tree","scale":1.0} }
  ],
  "edges": [
    { "id": "e1", "source": "sp", "target": "place", "sourceHandle": "out", "targetHandle": "in" }
  ]
}
```

### 2. 地形采样流水线

生成地形 → 创建点阵 → 采样到地表 → 放置。

```
GetTerrainData ──┬──(Terrain)──→ SampleSurface ──→ StaticMeshSpawner
                 │                    ↑
CreatePointGrid ──┴──(Points)─────────┘
```

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "terrain", "type": "GetTerrainData", "position": {"x":0,"y":0}, "data": {"gridSize":64,"cellSize":1.5,"amplitude":8.0} },
    { "id": "grid", "type": "CreatePointGrid", "position": {"x":0,"y":200}, "data": {"pointCountX":20,"pointCountY":20,"spacing":1.5} },
    { "id": "ss", "type": "SampleSurface", "position": {"x":300,"y":100}, "data": {"offsetY":0,"blend":1.0} },
    { "id": "spawn", "type": "StaticMeshSpawner", "position": {"x":600,"y":100}, "data": {"prefab":"Grass","scale":0.5} }
  ],
  "edges": [
    { "id": "e1", "source": "terrain", "target": "ss", "sourceHandle": "out", "targetHandle": "terrain" },
    { "id": "e2", "source": "grid", "target": "ss", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e3", "source": "ss", "target": "spawn", "sourceHandle": "out", "targetHandle": "in" }
  ]
}
```

### 3. 道路网络生成

随机点 → 三角剖分 → 最小生成树 → A* 寻路。

```
CreatePoints ──(Points)──→ Delaunay ──(Edges)──┬──→ MST ──(Tree)──┬──→ AStarPathfinding ──→ Output
                         │                      │                  │
                         └──(Points)────────────┼──────────────────┘
                                                │
                         ┌──(Points)────────────┘
                         │
                         └──→ (also to AStar points input)
```

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "pts", "type": "CreatePoints", "position": {"x":0,"y":0}, "data": {"x":0,"y":0,"z":0,"count":30,"jitter":8.0} },
    { "id": "del", "type": "Delaunay", "position": {"x":300,"y":0}, "data": {"maxEdgeLength":-1} },
    { "id": "mst", "type": "MST", "position": {"x":600,"y":0}, "data": {"maxEdgeLength":-1} },
    { "id": "astar", "type": "AStarPathfinding", "position": {"x":900,"y":0}, "data": {"startIndex":0,"endIndex":29} },
    { "id": "out", "type": "Output", "position": {"x":1200,"y":0}, "data": {"label":"Road"} }
  ],
  "edges": [
    { "id": "e1", "source": "pts", "target": "del", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e2", "source": "del", "target": "mst", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e3", "source": "pts", "target": "mst", "sourceHandle": "out", "targetHandle": "points" },
    { "id": "e4", "source": "mst", "target": "astar", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e5", "source": "pts", "target": "astar", "sourceHandle": "out", "targetHandle": "points" },
    { "id": "e6", "source": "astar", "target": "out", "sourceHandle": "out", "targetHandle": "in" }
  ]
}
```

### 4. 程序化网格生成

盒子 → 细分 → 倒角。

```
CreateBoxMesh ──(Mesh)──→ SubdivideMesh ──(Mesh)──→ BevelMesh ──→ Output
```

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0}, "data": {"width":4.0,"height":2.0,"depth":4.0} },
    { "id": "sub", "type": "SubdivideMesh", "position": {"x":300,"y":0}, "data": {"levels":2} },
    { "id": "bev", "type": "BevelMesh", "position": {"x":600,"y":0}, "data": {"method":"edge","offsetType":"offset","amount":0.15,"segments":3,"clampOverlap":true} },
    { "id": "out", "type": "Output", "position": {"x":900,"y":0}, "data": {"label":"RoundedBox"} }
  ],
  "edges": [
    { "id": "e1", "source": "box", "target": "sub", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e2", "source": "sub", "target": "bev", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e3", "source": "bev", "target": "out", "sourceHandle": "out", "targetHandle": "in" }
  ]
}
```

### 5. 桥梁截面圆角（Sweep + Group + Bevel）

沿样条扫掠生成 mesh → 按组筛选棱边 → 倒角圆角。

```
CreateSpline ──(backbone)──┐
                           ├─→ SweepAlongSpline ──→ GroupCreate ──→ BevelMesh ──→ Output
CreateSpline ──(profile)──┘
```

```json
{
  "version": "1.0",
  "nodes": [
    { "id": "spline_path", "type": "CreateSpline", "position": {"x":0,"y":0}, "data": {"closed": false, "controlPoints": [[0,0,0],[0,0,20],[5,0,40]]} },
    { "id": "spline_profile", "type": "CreateSpline", "position": {"x":0,"y":200}, "data": {"closed": true, "controlPoints": [[-3,0,0],[3,0,0],[3,1,0],[-3,1,0]]} },
    { "id": "sweep", "type": "SweepAlongSpline", "position": {"x":300,"y":100}, "data": {"surfaceShape": "crossSection", "capStart": true, "capEnd": true} },
    { "id": "grp", "type": "GroupCreate", "position": {"x":600,"y":100}, "data": {"outputGroup": "bevel_edges", "domain": "edge", "mode": "angle", "minEdgeAngle": 30, "fromEdgeGroup": "profile_corner"} },
    { "id": "bev", "type": "BevelMesh", "position": {"x":900,"y":100}, "data": {"method": "edge", "amount": 0.3, "segments": 4, "edgeGroup": "bevel_edges", "excludeGroups": "cap_start,cap_end"} },
    { "id": "out", "type": "Output", "position": {"x":1200,"y":100}, "data": {"label": "Bridge"} }
  ],
  "edges": [
    { "id": "e1", "source": "spline_path", "target": "sweep", "sourceHandle": "out", "targetHandle": "backbone" },
    { "id": "e2", "source": "spline_profile", "target": "sweep", "sourceHandle": "out", "targetHandle": "profile" },
    { "id": "e3", "source": "sweep", "target": "grp", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e4", "source": "grp", "target": "bev", "sourceHandle": "out", "targetHandle": "in" },
    { "id": "e5", "source": "bev", "target": "out", "sourceHandle": "out", "targetHandle": "in" }
  ]
}
```

> **要点**：`fromEdgeGroup=profile_corner` 限定 GroupCreate 只在截面折角棱边中选边，避免选到 Sweep 曲面内部的三角化对角线。`excludeGroups=cap_start,cap_end` 让 BevelMesh 跳过端盖边。

---

---

### CreateCylinderMesh

生成 Y 轴圆柱 mesh。

| 属性 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| radius | number | 1.0 | ≥ 0.001 | 半径。实用下限约 0.001 m；亚毫米 cutter 做 jimping/锯齿再进 `BooleanMesh` 不稳定，优先 `OutlineSolid` 剖面包络或更大 cutter |
| height | number | 2.0 | ≥ 0.001 | 高度。避免把「一串微圆柱 Boolean」当作唯一微细节手段 |
| radialSegments | integer | 16 | 3–128 | 圆周分段 |
| heightSegments | integer | 1 | 1–64 | 高度分段 |
| capTop | boolean | true | | 顶盖 |
| capBottom | boolean | true | | 底盖 |

**输入**：可选 `in: SpatialMesh`（与 CreateBoxMesh 相同的 optional merge 行为）。

**执行逻辑**：生成 `(heightSegments+1) * radialSegments` 个侧面顶点，每个 cap 复用 rim 并加一个 center 顶点。不生成 normals/colors/uvs。

> 典型连接：`CreateCylinderMesh → UVTexture → VertexColor → AssignMaterial → Output`

### RevolveMesh

将剖面曲线绕轴旋转生成回转体。

| 输入 Pin | 类型 | 说明 |
|----------|------|------|
| profile | SpatialSpline | 剖面曲线 |

| 属性 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| axis | enum | y | x/y/z | 旋转轴 |
| segments | integer | 16 | 3–256 | 旋转分段 |
| closeProfile | boolean | false | | 连接首尾（启用时忽略 cap） |
| capStart | boolean | false | | 起始端盖 |
| capEnd | boolean | false | | 结束端盖 |

**执行逻辑**：内部使用 `PcgGeometry` 传输（可直接连接 BevelMesh）。对每个 profile point：到旋转轴距离 ≤ 1e-8 时仅创建一个轴上点；否则创建 segments 个环上点。相邻 profile point 之间：ring-ring → quad，axis-ring → triangle，axis-axis → 不生成面。

> 典型连接：`CreateSpline → RevolveMesh → BevelMesh → Output`

---

## Material 类别

### VertexColor

为 mesh 的每个顶点写入统一的 RGBA 颜色；可选 emission 模式用于夜间窗户发光（alpha 作 emission mask）。

| 属性 | 类型 | 默认值 | 范围 | 说明 |
|------|------|--------|------|------|
| r | number | 1.0 | [0, 1] | 红色通道 |
| g | number | 1.0 | [0, 1] | 绿色通道 |
| b | number | 1.0 | [0, 1] | 蓝色通道 |
| a | number | 1.0 | [0, 1] | Alpha 通道（不会丢失） |
| emission | number | 0.0 | [0, 1] | >0 时启用发光：RGB 改为 emissionColor，alpha 写为 emission |
| emissionColorR/G/B | number | 1 / 1 / 1 | [0, 1] | 发光颜色 |

**执行逻辑**：读取输入 geometry/mesh，为所有顶点设置相同颜色。`emission > 0` 时覆盖为发光色 + alpha mask。

**范围限制**：本期仅支持 solid RGBA（统一颜色）。不支持按 face group 着色。应放在最后一个拓扑修改节点之后。

### AssignMaterial

向全部面或指定 face group 赋予材质名，并在 Unity 输出中生成对应 SubMesh。

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| group | groupMultiSelect (face) | "" | 逗号分隔的 face group；空值表示全部面 |
| materialName | string | "" | 材质名称（空字符串允许） |

**执行逻辑**：Geometry 输入时写入逐面材质属性；多个节点链式使用时，下游节点只覆盖命中 Group 的面，最后赋值获胜。Sink 将逐面属性展开为逐三角形材质槽，Mesh Binary v3 传给 Unity 并创建 SubMesh。Mesh-only 输入仅支持空 Group 的全局赋值。

**Unity 绑定**：在 `PcgGraphComponent > Material Bindings` 中将 `materialName` 映射到 Unity `Material`。未映射和空名称使用 `Mesh Material` fallback。完整流程见 [多材质工作流](multi-material-workflow.md)。

---

## UV 类别

### UVTexture

为 mesh 生成 UV 坐标，支持 planar / cylindrical / spherical 投射。

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| projection | enum | planar | 投射方式：planar / cylindrical / spherical（`box` 已关闭，P4 再实现真盒体投影） |
| axis | enum | y | 投射轴：x / y / z |
| scaleU | number | 1.0 | U 方向缩放 |
| scaleV | number | 1.0 | V 方向缩放 |
| offsetU | number | 0.0 | U 方向偏移 |
| offsetV | number | 0.0 | V 方向偏移 |

**执行逻辑**：
- Planar：将 mesh AABB 归一化到 [0,1]，取垂直于 axis 的两个坐标作为 UV。
- Cylindrical：U = atan2 角度 / 2π + 0.5，V = 轴向坐标归一化。
- Spherical：U = 经度，V = 纬度（基于顶点到中心的方向向量）。
- Geometry 路径写入 **point UV**，并展开为 **corner（vertex）UV**；Sink 优先 corner → Mesh UV0。
- 未知 / 遗留 `box` 投影返回执行错误（禁止静默写常数 UV）。

**范围限制**：应放在最后一个拓扑修改节点之后。不支持 UV Flatten/Pack、lightmap unwrap；`uv2` 仅 P5。

### ProjectTexture

根据 ImageTexture descriptor 的 repeat 参数生成 UV。

| 输入 Pin | 类型 | 说明 |
|----------|------|------|
| in | SpatialMesh | 输入 mesh |
| texture | Texture | ImageTexture 输出（必须连接） |

| 属性 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| direction | enum | z | 投射方向：x / y / z |
| scaleU | number | 1.0 | U 方向缩放 |
| scaleV | number | 1.0 | V 方向缩放 |
| offsetU | number | 0.0 | U 方向偏移 |
| offsetV | number | 0.0 | V 方向偏移 |

**执行逻辑**：读取 texture pin 的 ImageTexture descriptor 中的 `repeatX/repeatY`，与节点 `scaleU/scaleV` 相乘，按 direction 垂直平面做 AABB-normalized planar UV。不读取 texture pixels，不依赖 TextureRuntime。

**范围限制**：未连接 texture pin 时返回 `PCG_ERR_EXECUTION`。不进行像素投射或贴花混合。

---

### 6. 属性分类放置

生成点 → 打标签 → 按标签过滤 → 分别放置不同 prefab。

```
                   ┌──(Points)──→ AttributeFilter(tag=tree) ──→ PlaceInScene(Tree)
SpawnPoints ──┬───┤
              │   └──(Points)──→ AttributeFilter(tag=rock) ──→ PlaceInScene(Rock)
              │
CopyAttributes(tag, values=tree/rock)
```

> 注：`CopyAttributes` 对所有点写入相同的属性值。要实现不同标签分类，可串联多个 `CopyAttributes` + `AttributeFilter` 分支，或在 Web 编辑器中手动编辑不同分支的属性。

---

## 示例 .pcg 文件

项目提供两组示例图，可直接在 PCG Graph Editor 中打开运行。

### PCGDemo

位于 `examples/` 和 `Unity/Assets/PcgPlugin/Examples/PCGDemo/`，展示完整场景级用法：

| 文件 | 说明 |
|------|------|
| `demo.pcg` | 综合演示（Box → Bevel + 基础 mesh 流水线） |
| `bridge-demo.pcg` | 桥梁场景（Sweep + Bevel） |
| `boolean-test.pcg` | Boolean CSG 四种操作演示 |
| `car.pcg` / `lowpoly-car.pcg` / `lowpoly-car-2.pcg` | 程序化车辆生成 |
| `lowpoly-sedan.pcg` | 低多边形轿车 |
| `excavator.pcg` | 挖掘机场景 |
| `spiral-staircase.pcg` | 螺旋楼梯（InstanceAlongSpline + Sweep） |
| `stone-arch-bridge.pcg` | 石拱桥（BooleanMesh + BevelMesh） |
| `village-demo.pcg` | 村落场景（点生成 + 地形 + 实例放置） |
| `lot-extrude-demo.pcg` | Labs Lot Subdivision：平面 → 切 lot → PolyExtrude |
| `lot-city-demo.pcg` | Lot 城市场景：地块挤出 + 建筑散布 + 道路 Sweep（分件 Bevel 再 Merge） |

### Test

位于 `examples/Test/` 和 `Unity/Assets/PcgPlugin/Examples/Test/`，覆盖 Phase 5 新增 7 节点的最小验证图：

| 文件 | 测试链路 | 验证内容 |
|------|---------|---------|
| `test-cylinder.pcg` | `CreateCylinderMesh → Output` | 圆柱生成、顶点/索引数、cap winding |
| `test-outline-solid.pcg` | `CreateSpline → OutlineSolid → Output` | 闭合轮廓×厚度焊接实体、front/back/rim |
| `test-condition-outline.pcg` | `CreateSpline → ConditionOutline → Output` | 平滑/RDP/protectSpans；JSON spline sink |
| `test-revolve-bevel.pcg` | `CreateSpline → RevolveMesh → BevelMesh → Output` | 回转体生成、BevelMesh 几何链保持 |
| `test-spiral-sweep.pcg` | `CreateSpiralSpline → SweepAlongSpline → Output` | 螺旋线采样、Sweep 扫掠 |
| `test-color-uv-material.pcg` | `CreateCylinderMesh → UVTexture → VertexColor → AssignMaterial → Output` | RGBA colors（含 alpha）、UV0、material metadata 跨 native boundary 传递 |
| `test-project-texture.pcg` | `ImageTexture → ProjectTexture + CreateCylinderMesh → Output` | texture descriptor repeat 读取、planar UV 投射 |

> **验证步骤**：在 Unity Editor 中打开 PCG Graph Editor → File → Open .pcg → Cook → 检查 Scene View mesh、Console 无报错。`test-color-uv-material.pcg` 可通过 mesh.colors / mesh.uv 长度验证属性传递。

### ForEachBegin

**类别**：Flow

Houdini `block_begin` 子集：按 primitive / piece 属性 / count 迭代，输出当前件。执行器识别 Begin→End 区域并循环 cook。

**输入**

| id | label | pinType |
|----|-------|---------|
| in | Geometry | SpatialMesh |

**输出**

| id | label | pinType |
|----|-------|---------|
| out | Piece | SpatialMesh |

**属性**：`method`（primitive|piece|count）、`pieceAttribute`、`iterations`

### ForEachEnd

**类别**：Flow

Houdini `block_end` 子集：收集 Begin 区域结果。`gatherMethod=merge` 合并各次输出；`feedback` 将结果回喂下一轮（count 叠层）。

### PrimitiveTransform

**类别**：Mesh

Houdini `primitive` SOP 子集：绕各面质心均匀缩放（默认 0.85），独立复制顶点，用于 lot inset。

### ConvertLine

**类别**：Spline

Houdini `convertline`：将面边转为折线/线段。

| 参数 | Houdini 对应 | 说明 |
|------|--------------|------|
| `group` | **Group** | 要转换的 edge group；空 = 全部边 |
| `connectPath` | **Connect Path** | 将端点连成连续折线（默认开） |
| `maxDistance` | **Max Distance** | Connect Path 端点合并距离 |
| `connectOnlyToOtherEndPoints` | **Connect Only To Other End Points** | 仅端点互连 |
| `keepGroupOrder` | **Keep Group Order** | 按 group 顺序排列（预留） |
| `makeIsolatedLoopsClosed` | **Make Isolated Loops Closed** | 孤立闭环标记为 closed |
| `removeUnusedPoints` | **Remove Unused Points** | 移除未引用点（默认开） |
| `computeLength` + `lengthAttribute` | **Compute Length** | 写入段长属性（默认 `restlength`） |

兼容旧图：`mode=unshared|all|group` 与 `edgeGroup` 仍可读。

开窗链：`ConvertLine` → `ResampleSpline` → `CopyMeshToPoints` → `BooleanMesh`。

### ExtractCentroid

**类别**：Attribute

Houdini `extractcentroid` 子集：输出每面质心点（或整体点云质心）。

### GroupTransfer

**类别**：Geometry

Houdini `grouptransfer`：按最近邻把 source 的 Primitive / Point / Edge 命名 group 传到 target。

| 参数 | 说明 |
|------|------|
| Primitive / Point / Edge Groups | 开关 + 源 group 选择（空 = `*`） |
| * Group Prefix | 目标 group 名前缀 |
| Group Name Conflict | `Skip Group` / `Overwrite` / `Add Suffix` |
| Enable Distance Threshold + Distance Threshold | 最近邻距离上限 |
| Create Groups Even If Empty | 传空组时是否仍创建 |

兼容旧属性：`groupName` + `domain` + `distance`（单组 overwrite）。

### Clip

**类别**：Mesh

Houdini `clip` 子集：平面剖切，保留法线正侧（或负侧）整面。
