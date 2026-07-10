# PCG 节点参考手册

本文档详细说明 `schema/node-manifest.json`（v1.4）中定义的全部 **42 种** PCG 节点。

每个节点包含：功能描述、输入/输出 Pin、属性表、执行逻辑和用法示例。

---

## 目录

- [Pin 数据类型](#pin-数据类型)
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
- [Spline 类别](#spline-类别)
  - [CreateSpline](#createspline)
  - [ResampleSpline](#resamplespline)
  - [SampleAlongSpline](#samplealongspline)
  - [SweepAlongSpline](#sweepalongspline)
  - [ExtrudeAlongSpline](#extrudealongspline)
  - [CrossSectionProfile](#crosssectionprofile)
  - [InstanceAlongSpline](#instancealongspline)
- [Structural 类别](#structural-类别)
  - [ConvexHull](#convexhull)
  - [ConnectNearest](#connectnearest)
  - [Delaunay](#delaunay)
  - [MST](#mst)
  - [Voronoi](#voronoi)
  - [AStarPathfinding](#astarpathfinding)
- [Mesh 类别](#mesh-类别)
  - [CreateBoxMesh](#createboxmesh)
  - [SubdivideMesh](#subdividemesh)
  - [BevelMesh](#bevelmesh)
  - [MeshNoiseDeform](#meshnoisedeform)
  - [TransformMesh](#transformmesh)
  - [MergeMesh](#mergemesh)
  - [BooleanMesh](#booleanmesh)
- [Geometry 类别](#geometry-类别)
  - [GroupCreate](#groupcreate)
  - [GroupCombine](#groupcombine)
- [Texture 类别](#texture-类别)
  - [ImageTexture](#imagetexture)
- [Output 类别](#output-类别)
  - [Output](#output)
- [常见节点组合](#常见节点组合)

---

## Pin 数据类型

节点间的连线（Pin）遵循以下数据类型约束，与 UE EPCGDataType 对齐：

| Pin 类型 | 说明 | 对应 C++ 数据结构 |
|----------|------|-------------------|
| `Param` | 参数对象（JSON 键值对） | `PcgParamData` / `nlohmann::json` |
| `SpatialPoint` | 空间点云（含坐标和属性） | `PcgPointData` → `PcgPoint{x, y, z, attributes}` |
| `SpatialSpline` | 样条/线段集合 | `PcgSplineData` → `PcgSpline{points[], closed}` |
| `SpatialMesh` | 网格数据（顶点+三角形） | `PcgMeshData` → `vertices[], triangles[]` |
| `Any` | 任意类型透传 | — |
| `Texture` | 纹理数据（2D 图像） | `PcgTextureData` → `width, height, channels, data[]` |

> **连接规则**：输出 Pin 类型必须与输入 Pin 类型匹配。`Any` 类型可接受任意输入。

---

## Input 类别

### GetTerrainData

**类别**：Input / Sampler

**功能**：生成程序化地形高度图数据。使用基于噪声的算法在指定网格范围内生成地形高度值，供下游地形相关节点（`ProjectPoints`、`SampleSurface`）采样。

**输入 Pin**：无

**输出 Pin**：

| Pin ID | 标签 | 类型 |
|--------|------|------|
| `out` | Terrain | `Param` |

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
3. 输出 `Param` 类型数据：`{"gridSize", "cellSize", "amplitude", "seed", "heights": [...]}`

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

**功能**：在输入网格表面均匀采样生成点云。支持法线偏移和松散度控制。

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

**执行逻辑**：
1. 读取输入网格的三角形列表
2. 按面积加权随机选择三角形面
3. 在选中三角形内生成均匀分布的随机点（重心坐标采样）
4. 若 `normalOffset` ≠ 0，沿该点法线方向偏移
5. 若 `looseness` > 0，在表面附近添加随机扰动
6. 输出 `SpatialPoint` 类型点云

**用法示例**：

```json
{
  "id": "sms2",
  "type": "SampleMeshSurface",
  "position": { "x": 300, "y": 0 },
  "data": { "count": 500, "seed": 42, "normalOffset": 0.5, "looseness": 0.3 }
}
```

> 典型连接：`GetMeshData → SampleMeshSurface → PlaceInScene`，在网格表面散布物体。

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
| `terrain` | Terrain | `Param` | 可选。来自 `GetTerrainData` 的地形数据 |

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
1. 检测是否有 terrain 输入，`useTerrain` 缺省取 `terrain != nullptr`
2. 若使用地形：对每个点 `(x, z)` 从地形 heightmap 中采样高度（先转换为网格索引，越界时回退到 `simple_noise`）
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

> 典型连接：`GetTerrainData → ProjectPoints(terrain)`，`CreatePointGrid → ProjectPoints(in)`

---

## Sampler 类别

### SampleSurface

**类别**：Sampler

**功能**：将点云的 Y 坐标混合到地形表面。与 `ProjectPoints` 不同，`SampleSurface` 支持混合系数 `blend`，可以在原始 Y 和地形 Y 之间线性插值。

**输入 Pin**：

| Pin ID | 标签 | 类型 | 说明 |
|--------|------|------|------|
| `in` | Points | `SpatialPoint` | 要采样的点云 |
| `terrain` | Terrain | `Param` | **必需**。来自 `GetTerrainData` 的地形数据 |

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
1. 读取地形数据中的 `seed`（缺省取 `graph_seed`）
2. 对每个点采样地形高度 `terrain_y = sample_terrain_height(terrain, x, z, seed)`
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
| `editPlane` | enum | `"none"` | `none` / `xy` / `xz` / `yz` | 编辑平面约束 |
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

**功能**：对网格执行倒角/斜切操作，支持两种方法和两种偏移类型。

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
| `method` | enum | `"edge"` | `edge` / `vertexPush` | 倒角方法。`edge` = 边倒角（Blender 风格），`vertexPush` = 顶点收缩 |
| `offsetType` | enum | `"offset"` | `offset` / `width` | 偏移类型。`offset` = 偏移距离，`width` = 倒角宽度 |
| `amount` | number | 0.1 | ≥ 0, ≤ 1.0 | 倒角量 |
| `segments` | integer | 2 | 1 ~ 8 | 倒角分段数。越大越圆滑 |
| `clampOverlap` | boolean | true | — | 是否钳制重叠（防止倒角量过大导致几何翻转） |
| `angleLimit` | number | 30.0 | 0 ~ 180 | 角度限制。仅对相邻面夹角 ≥ 此值的边进行倒角 |
| `profile` | number | 0.5 | 0 ~ 1 | 倒角轮廓形状。0.5 = 圆弧，0 = 凹陷，1 = 凸起 |
| `miterOuter` | enum | `"sharp"` | `sharp` / `patch` / `arc` | 外拐角处理方式 |
| `miterInner` | enum | `"sharp"` | `sharp` / `patch` / `arc` | 内拐角处理方式 |
| `vmeshMethod` | enum | `"adj"` | `adj` / `cutoff` | 顶点网格方法。`adj` = Catmull-Clark 邻接，`cutoff` = 截断 |
| `edgeGroup` | groupSelect | `""` | 上游可用边组 | 限制倒角范围到指定边组。留空 = 所有边 |
| `excludeUnshared` | boolean | true | — | 是否排除边界边（只有一侧面的边） |
| `excludeGroups` | groupMultiSelect | `"cap_start,cap_end"` | 上游可用组 | 排除指定组中的边不参与倒角 |

**执行逻辑**：
1. 读取输入网格（空则报错）
2. 若 `edgeGroup` 非空，仅对指定边组中的边进行倒角；否则考虑所有边
3. `excludeGroups` 中的边组被排除；`excludeUnshared` 控制是否排除边界边
4. `angleLimit` 过滤：仅对相邻面夹角 ≥ 此值的边倒角
5. 根据 `method` 选择边倒角或顶点收缩算法
6. 根据 `offsetType` 解释 `amount`：`offset` = 顶点沿法线偏移距离，`width` = 倒角后两条新边之间的距离
7. `segments` 控制每条倒角边的分段数；`profile` 控制倒角轮廓形状
8. `miterOuter` / `miterInner` 控制拐角处的连接方式
9. `clampOverlap` 启用时自动缩放 amount 防止自交
10. 输出倒角后的网格

**用法示例**：

```json
{
  "id": "bev",
  "type": "BevelMesh",
  "position": { "x": 600, "y": 0 },
  "data": {
    "method": "edge",
    "offsetType": "offset",
    "amount": 0.2,
    "segments": 3,
    "clampOverlap": true,
    "angleLimit": 30,
    "profile": 0.5
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
| `detriangulate` | enum | `"all"` | 去三角化：`all`（所有多边形）/ `unchanged`（仅未改变）/ `none`（不处理） |
| `weldEpsilon` | number | 0.0001 | 焊接容差（≥ 1e-8） |
| `triangleBudget` | integer | 500000 | 三角形数量上限（≥ 1000） |

**执行逻辑**：
1. 读取两个输入网格，按 `weldEpsilon` 焊接重合顶点
2. 计算两网格的相交线，将面沿交线切割
3. 根据 `operation` 选择保留的面：
   - `union`：保留 A 外部 + B 外部的面
   - `intersect`：保留 A 内部 + B 内部的面
   - `subtract`：保留 A 外部的面，去除 A 内部的面
   - `shatter`：将 A 沿 B 的切割面碎裂为多个独立片
4. 根据 `treatAAs`/`treatBAs` 调整整/表面模式下的内部/外部判定
5. `detriangulate` 控制是否将共面三角形合并为多边形
6. 标记输出组（a_inside_b / a_outside_b / b_inside_a / b_outside_a / ab_seams）
7. 若三角形数超过 `triangleBudget`，报错终止
8. 输出布尔运算结果网格

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
    "weldEpsilon": 0.0001
  }
}
```

> 典型连接：`GetMeshData(A) + CreateBoxMesh(B) → BooleanMesh(subtract) → Output`，从实体中挖洞/开槽。

---

## Geometry 类别

Geometry 类别节点操作 `PcgGeometry`（多边形网格 + 命名组 + 属性），是 Houdini Group SOP 的对等实现。节点间传递的 `SpatialMesh` pin 在内部携带 `PcgGeometry`，包含拓扑面/边/组语义，而非仅三角汤。

### Group 系统

Group 是 PCG 几何管线的核心概念，参考 Houdini 的 Group SOP + PolyBevel group 参数模式：

- **生产者节点**（如 `SweepAlongSpline`）在 manifest 中声明 `outputGroups`，在执行时将几何元素（边/面/点）归入命名组
- **消费者节点**（如 `BevelMesh`）通过 `edgeGroup` / `excludeGroups` 参数按组名选择操作范围
- **GroupCreate / GroupCombine** 是中间过滤节点，按规则从上游已有组中筛选或组合，生成新组供下游使用

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

---

## 常见节点组合

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
