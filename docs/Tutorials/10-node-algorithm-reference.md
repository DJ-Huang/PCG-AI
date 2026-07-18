# 节点算法参考手册

> [返回目录](index.md) | 基于 commit `f50d744`
> 本文档覆盖全部 52 个内置节点的算法分析，按类别组织。

---

## 1. Generation 节点（点生成）

### 1.1 SpawnPoints

| 项 | 值 |
|---|---|
| 文件 | `element_registry.cpp` |
| 算法 | LCG 随机数生成圆盘上的点 |
| 输入 | 无 |
| 输出 | Points (pin "out") |

**算法**：使用线性同余生成器 `rng = rng * 1664525u + 1013904223u`，seed 来自 `graph_seed ^ (graph_seed * 2654435761u)`。每个点在圆盘上按等角分布 + 随机径向偏移生成。count 限制 0-10000。

**参数**：`count` (int, 0-10000), `radius` (double, ≥0)

### 1.2 CreatePointGrid

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 网格采样 |
| 输入 | Points (可选, pin "in") |
| 输出 | Points (pin "out") |

**算法**：在 `pointCountX × pointCountY` 网格上按 `spacing` 间距生成点。可选与输入点合并。

**参数**：`pointCountX`, `pointCountY` (int, ≥1), `spacing` (double, ≥0)

### 1.3 CreatePoints

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 单点/多点生成 |
| 输入 | Points (可选, pin "in") |
| 输出 | Points (pin "out") |

**算法**：在指定坐标 `(x, y, z)` 生成 `count` 个点，可选 `jitter` 随机偏移。

**参数**：`x`, `y`, `z` (double), `count` (int, ≥1), `jitter` (double, ≥0)

### 1.4 SurfaceSampler

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 网格面采样 |
| 输入 | Points (可选, pin "in") |
| 输出 | Points (pin "out") |

**算法**：在 `subdivisions × subdivisions` 的网格上按 `extent` 范围均匀采样点。

**参数**：`subdivisions` (int, ≥2), `extent` (double, ≥0)

### 1.5 SampleMeshSurface

| 项 | 值 |
|---|---|
| 文件 | `mesh_scatter_elements.cpp` |
| Algorithm | `mesh_scatter_algorithms.cpp:sample_mesh_surface()` |
| 输入 | Mesh (pin "in") |
| 输出 | Points (pin "out") |

**算法**：在输入网格的三角形表面上随机采样点。按三角形面积加权选择三角形，在三角形内部用重心坐标采样。支持 `normalOffset`（沿法线偏移）和 `looseness`（随机扰动）。count 限制 0-1,000,000。

**参数**：`count` (int, 0-1M), `seed` (int), `normalOffset` (double), `looseness` (double, ≥0)

---

## 2. Structural 节点（图算法）

所有 Structural 节点在 XZ 平面（2D 投影）上计算，输出为 SplineData（线段集合）。

### 2.1 ConvexHull

| 项 | 值 |
|---|---|
| 文件 | `structural_elements.cpp` / `structural_algorithms.cpp` |
| 算法 | Andrew's Monotone Chain |
| 输入 | Points (pin "in") |
| 输出 | Spline (pin "out", closed) |

**算法**：
1. 将点投影到 XZ 平面
2. 按 x 坐标排序
3. 构建下凸包（从左到右扫描）
4. 构建上凸包（从右到左扫描）
5. 合并为闭合多边形

使用 `cross()` 判断方向（叉积 ≤ tolerance 时弹栈）。时间复杂度 O(n log n)。

**参数**：`tolerance` (double, ≥0)

### 2.2 ConnectNearest

| 项 | 值 |
|---|---|
| 文件 | `structural_elements.cpp` / `structural_algorithms.cpp` |
| 算法 | K-近邻连接 |
| 输入 | Points (pin "in") |
| 输出 | Spline (pin "out") |

**算法**：对每个点找到最近的 K 个邻居（按欧氏距离排序），创建连接边。使用 `set<pair<int,int>>` 去重（无向边）。可选 `maxDistance` 过滤。

**参数**：`k` (int, 1-16), `maxDistance` (double, -1 = 无限制)

### 2.3 Delaunay

| 项 | 值 |
|---|---|
| 文件 | `structural_elements.cpp` / `structural_algorithms.cpp` |
| 算法 | Bowyer-Watson 三角剖分 |
| 输入 | Points (pin "in") |
| 输出 | Spline (pin "out") |

**算法**：
1. 创建包含所有点的超级三角形
2. 逐个插入点：找到外接圆包含该点的"坏三角形"
3. 提取坏三角形的边界形成空腔多边形
4. 删除坏三角形，用新点与多边形边创建新三角形
5. 移除与超级三角形顶点相连的三角形
6. 从剩余三角形提取唯一边

`in_circumcircle()` 使用行列式判断点是否在三角形外接圆内。可选 `maxEdgeLength` 过滤长边。

**参数**：`maxEdgeLength` (double, -1 = 无限制)

### 2.4 MST (Minimum Spanning Tree)

| 项 | 值 |
|---|---|
| 文件 | `structural_elements.cpp` / `structural_algorithms.cpp` |
| 算法 | Kruskal's 算法 + Union-Find |
| 输入 | Spline (pin "in"), Points (pin "points") |
| 输出 | Spline (pin "out") |

**算法**：
1. 从输入 Spline 提取边列表（每条 spline 映射回点索引）
2. 按权重（边长）升序排序
3. Union-Find 结构判断是否形成环
4. 不形成环的边加入 MST

时间复杂度 O(E log E)。`UnionFind` 使用路径压缩优化。

**参数**：`maxEdgeLength` (double, -1 = 无限制)

### 2.5 Voronoi

| 项 | 值 |
|---|---|
| 文件 | `structural_elements.cpp` / `structural_algorithms.cpp` |
| 算法 | Delaunay 对偶图 |
| 输入 | Points (pin "in") |
| 输出 | Spline (pin "out") |

**算法**：
1. 先执行 Bowyer-Watson 三角剖分
2. 计算每个三角形的外接圆中心（circumcenter）
3. 共享边的相邻三角形的外心连线即为 Voronoi 边
4. 使用去重集合避免重复边

`circumcenter()` 通过三个点的坐标计算外心。可选 `offsetY` 抬升和 `maxEdgeLength` 过滤。

**参数**：`offsetY` (double), `maxEdgeLength` (double, -1 = 无限制)

### 2.6 AStarPathfinding

| 项 | 值 |
|---|---|
| 文件 | `structural_elements.cpp` / `structural_algorithms.cpp` |
| 算法 | A* 寻路 |
| 输入 | Spline (pin "in"), Points (pin "points") |
| 输出 | Spline (pin "out") |

**算法**：
1. 从输入 Spline 提取边并构建邻接表
2. 启发函数：到终点的欧氏距离
3. 优先队列（最小堆）按 `g_score + heuristic` 排序
4. 松弛操作：如果 `tentative < g_score[next]` 则更新
5. 从 `came_from` 回溯路径

`g_score` 初始为 `infinity`，`came_from` 初始为 -1。使用 `std::priority_queue<pair<double,int>, greater>` 实现最小堆。

**参数**：`startIndex` (int, ≥0), `endIndex` (int, ≥0)

---

## 3. Mesh 节点（网格构造与变形）

### 3.1 CreateBoxMesh

| 项 | 值 |
|---|---|
| 文件 | `mesh_elements.cpp` |
| Algorithm | `mesh_algorithms.cpp:create_box_geometry()` |
| 输入 | Geometry/Mesh (可选, pin "in") |
| 输出 | Geometry (pin "out") |

**算法**：创建 8 个共享顶点 + 6 个四边形面（n-gon）的立方体。如果有输入，使用 `merge_geometries()` 合并。输出为 PcgGeometry（延迟三角化）。

**参数**：`width`, `height`, `depth` (double, ≥0)

### 3.2 CreateCylinderMesh

| 项 | 值 |
|---|---|
| 文件 | `mesh_elements.cpp` |
| Algorithm | `mesh_algorithms.cpp:create_cylinder_mesh()` |
| 输入 | Mesh (可选, pin "in") |
| 输出 | Geometry (pin "out") |

**算法**：按 `radialSegments` 等分圆周，按 `heightSegments` 等分高度，生成圆柱体网格。可选顶/底封口。输出通过 `geometry_from_mesh()` 转为 PcgGeometry。如有输入则合并。

**参数**：`radius`, `height` (double, ≥0.001), `radialSegments` (int, 3-128), `heightSegments` (int, 1-64), `capTop`, `capBottom` (bool)

### 3.3 SubdivideMesh

| 项 | 值 |
|---|---|
| 文件 | `mesh_elements.cpp` |
| Algorithm | `mesh_algorithms.cpp:subdivide_geometry()` |
| 输入 | Geometry (pin "in") |
| 输出 | Geometry (pin "out") |

**算法**：支持三种细分方法：
- **Catmull-Clark**：每条边生成新顶点，旧顶点位置按邻居加权更新，每个 n-gon 面分裂为 n 个四边形
- **Loop**：仅适用于三角形网格。每条边生成中点，旧顶点按 (n-2)/n 权重更新，每个三角形分裂为 4 个小三角形
- **Simple (Linear)**：每条边取中点，面分裂但不更新顶点位置

输出为 PcgGeometry，保留 n-gon 拓扑。

**参数**：`levels` (int, 0-4), `method` (enum: catmullClark/loop/simple)

### 3.4 BevelMesh

| 项 | 值 |
|---|---|
| 文件 | `mesh_elements.cpp` |
| Algorithm | `bevel_blender.cpp:bevel_mesh_blender()` |
| 输入 | Geometry (pin "in") |
| 输出 | Geometry (pin "out") |

**算法**：Blender 对齐的 bevel 实现：
1. 从 PcgGeometry 构建 BMesh（半边结构）
2. 按 `edgeGroup` 或 `angleLimit` 选择要 bevel 的边
3. 每条选定边按 `segments` 数分割，生成圆角面
4. `profile` 控制圆角形状（0=直角, 0.5=圆弧, 1=凸出）
5. `miterOuter`/`miterInner` 处理转角（sharp/patch/arc）
6. `vmeshMethod` 处理顶点网格填充（adj=Grid Fill / cutoff=Cutoff）
7. `clampOverlap` 防止 bevel 结果自交
8. 转回 PcgGeometry，保留 face/edge groups

支持 cancel 检查。支持 `excludeGroups` 排除特定边组。

**参数**：`amount`, `segments`, `offsetType` (offset/width), `profile`, `angleLimit`, `limitMethod` (none/angle), `miterOuter`/`miterInner` (sharp/patch/arc), `vmeshMethod` (adj/cutoff), `clampOverlap`, `method` (edge/vertexPush), `excludeUnshared`, `excludeGroups`, `edgeGroup`

### 3.5 MeshNoiseDeform

| 项 | 值 |
|---|---|
| 文件 | `mesh_elements.cpp` |
| Algorithm | `mesh_algorithms.cpp:noise_deform_mesh()` |
| 输入 | Geometry (pin "in"), Texture (可选, pin "texture") |
| 输出 | Geometry (pin "out") |

**算法**：
1. 将 Geometry 三角化为共享顶点网格（`triangulate_geometry_shared`）
2. 对每个顶点按 `intensity` 量沿 Y 轴施加噪声位移
3. Perlin 噪声：内置实现，参数为 `scale`/`midLevel`/`seed`
4. Texture 噪声：从 ImageTexture 采样像素值
5. 将位移后的顶点位置写回 Geometry

**参数**：`intensity` (0-1), `scale` (0.01-64), `midLevel` (0-1), `noiseType` (perlin/texture), `textureCoords` (local)

### 3.6 TransformMesh

| 项 | 值 |
|---|---|
| 文件 | `spline_mesh_elements.cpp` |
| Algorithm | `mesh_algorithms.cpp:transform_geometry()` / `transform_mesh()` |
| 输入 | Geometry 或 Mesh (pin "in") |
| 输出 | Geometry 或 Mesh (pin "out") |

**算法**：对每个顶点应用 平移 → 旋转 → 缩放 变换。优先处理 Geometry（保留 n-gon），否则处理 Mesh。

**参数**：`translateX/Y/Z`, `rotationX/Y/Z` (度), `scaleX/Y/Z` (≥0.001)

### 3.7 MergeMesh

| 项 | 值 |
|---|---|
| 文件 | `spline_mesh_elements.cpp` |
| Algorithm | `data::merge_geometries()` / `merge_meshes()` |
| 输入 | Geometry/Mesh (variadic, pin "in") |
| 输出 | Geometry 或 Mesh (pin "out") |

**算法**：遍历所有输入，Geometry 优先。如有 Geometry 输入，使用 `merge_geometries()` 合并（GroupTable 自动加前缀避免名称冲突）。要求所有输入的 shade_mode 和 cusp_angle 一致。否则回退到 Mesh 合并（顶点偏移 + 索引偏移）。

**参数**：无

### 3.8 RevolveMesh

| 项 | 值 |
|---|---|
| 文件 | `mesh_elements.cpp` |
| Algorithm | `mesh_algorithms.cpp:revolve_geometry()` |
| 输入 | Spline (pin "profile") |
| 输出 | Geometry (pin "out") |

**算法**：将 profile 样条线绕指定轴（X/Y/Z）旋转 `segments` 等分，生成旋转体网格。可选闭合 profile、生成端面封口。输出为 PcgGeometry。

**参数**：`axis` (x/y/z), `segments` (int, 3-256), `closeProfile`, `capStart`, `capEnd` (bool)

---

## 4. Spline 节点（样条线）

### 4.1 CreateSpline

| 项 | 值 |
|---|---|
| 文件 | `spline_elements.cpp` |
| Algorithm | `spline_algorithms.cpp:create_spline_data()` |
| 输入 | 无 |
| 输出 | Spline (pin "out") |

**算法**：按 `mode` 生成样条线：
- **line**：起点到终点的直线
- **polyline**：连接控制点的折线
- **catmullRom**：Catmull-Rom 样条插值，按 `subdivisions` 细分

控制点从 `controlPoints` JSON 字符串解析。`editPlane` 用于 Scene 编辑约束。

**参数**：`mode`, `closed`, `subdivisions` (1-64), `startX/Y/Z`, `endX/Y/Z`, `controlPoints`, `editPlane`, `sceneOffsetX/Y/Z`

### 4.2 CreateSpiralSpline

| 项 | 值 |
|---|---|
| 文件 | `spline_elements.cpp` |
| Algorithm | `spline_algorithms.cpp:create_spiral_spline_data()` |
| 输入 | 无 |
| 输出 | Spline (pin "out") |

**算法**：生成螺旋线。按 `pointsPerTurn` 等分每圈，`turns` 圈数，`pitch` 每圈高度增量，绕指定轴旋转。

**参数**：`radius`, `pitch`, `turns`, `pointsPerTurn` (int, ≥4), `axis` (x/y/z)

### 4.3 GetSplineData

| 项 | 值 |
|---|---|
| 文件 | `spline_elements.cpp` |
| 算法 | Runtime slot 查找 |
| 输入 | 无（从 `ctx.splines` 获取） |
| 输出 | Spline (pin "out") |

**算法**：从 `SplineRuntime` 中按节点 ID 查找上传的样条线数据。对应 v7 API 的 `PcgSplineSlot`。

**参数**：`source` (Binding/Self), `bindingKey`

### 4.4 ResampleSpline

| 项 | 值 |
|---|---|
| 文件 | `spline_elements.cpp` |
| Algorithm | `spline_algorithms.cpp:resample_spline_data()` |
| 输入 | Spline (pin "in") |
| 输出 | Spline (pin "out") |

**算法**：按等间距或等数量重新采样样条线：
- **spacing** 模式：按 `spacing` 距离等间距采样
- **count** 模式：按 `pointCount` 等分

**参数**：`mode` (spacing/count), `spacing` (≥0.01), `pointCount` (int, ≥2)

### 4.5 SampleAlongSpline

| 项 | 值 |
|---|---|
| 文件 | `spline_elements.cpp` |
| Algorithm | `spline_algorithms.cpp:sample_along_spline()` |
| 输入 | Spline (pin "in") |
| 输出 | Points (pin "out") |

**算法**：沿样条线按 `spacing` 间距采样点。`offset` 调整起始位置。`alignToTangent` 控制是否将点的旋转对齐到切线方向（写入 point 属性）。`includeEnd` 控制是否包含终点。seed 用于 jitter。

**参数**：`spacing` (≥0.1), `offset`, `includeEnd`, `alignToTangent` (bool), `seed` (int)

### 4.6 SweepAlongSpline

| 项 | 值 |
|---|---|
| 文件 | `spline_mesh_elements.cpp` |
| Algorithm | `spline_algorithms.cpp:sweep_along_spline_geometry()` → `geometry/sweep_geometry.cpp` |
| 输入 | Spline (pin "backbone"), Spline (可选, pin "profile") |
| 输出 | Geometry (pin "out") |

**算法**：
1. 沿 backbone 样条线按 `sampleSpacing` 采样，生成 Frame3 序列（位置 + 切线 + 法线 + 副法线）
2. 准备截面（4 种 `surfaceShape`：crossSection/rectangle/circle/ribbon）
3. 将截面变换到每个 Frame3 的局部坐标系
4. 相邻截面缝合四边形面
5. 可选 `capStart`/`capEnd` 生成端面
6. `twist`/`profileRoll`/`scaleStart`/`scaleEnd` 控制变形
7. 输出 PcgGeometry，带 face groups（side/cap_start/cap_end/unshared）
8. 设置 `shadeMode` 和 `cuspAngleDeg`

**参数**：`surfaceShape`, `profileWidth`, `profileHeight`, `radius`, `columns` (3-128), `sampleSpacing`, `capStart`, `capEnd`, `upX/Y/Z`, `twist`, `profileRoll`, `scaleStart`, `scaleEnd`, `profilePlane`, `shadeMode`, `cuspAngle`

### 4.7 ExtrudeAlongSpline

| 项 | 值 |
|---|---|
| 文件 | `spline_mesh_elements.cpp` |
| Algorithm | `spline_algorithms.cpp:extrude_along_spline()` |
| 输入 | Spline (pin "spline"), Mesh (可选, pin "profile") |
| 输出 | Geometry (pin "out") |

**算法**：与 SweepAlongSpline 类似，但使用 Mesh 作为 profile（而非 Spline）。支持 `profileWidth/Height` 参数化截面或使用输入 Mesh 作为截面。输出通过 `geometry_from_mesh()` 转为 PcgGeometry。

**参数**：`profileWidth`, `profileHeight`, `sampleSpacing`, `capStart`, `capEnd`, `upX/Y/Z`, `twist`, `profileRoll`, `scaleStart`, `scaleEnd`, `profilePlane`

### 4.8 CrossSectionProfile

| 项 | 值 |
|---|---|
| 文件 | `spline_mesh_elements.cpp` |
| Algorithm | `spline_algorithms.cpp:extract_cross_section_profile()` → `geometry/sweep_geometry.cpp:prepare_cross_section()` |
| 输入 | Mesh (pin "in") |
| 输出 | Geometry (pin "out") |

**算法**：从输入网格中提取截面轮廓。按 `plane` (auto/xy/xz/yz) 投影，焊接近点（`weldEpsilon`），可选居中。输出为 PcgGeometry。

**参数**：`plane` (auto/xy/xz/yz), `weldEpsilon` (≥1e-8), `center` (bool)

### 4.9 InstanceAlongSpline

| 项 | 值 |
|---|---|
| 文件 | `spline_mesh_elements.cpp` |
| Algorithm | `spline_algorithms.cpp:instance_along_spline()` |
| 输入 | Spline (pin "spline"), Mesh (pin "mesh") |
| 输出 | Geometry (pin "out") |

**算法**：沿样条线按 `spacing` 间距放置 prototype Mesh 的副本。`alignToTangent` 控制是否对齐切线。`scale` 缩放每个副本。输出合并后的 Geometry。

**参数**：`spacing` (≥0.1), `offset`, `includeEnd`, `alignToTangent`, `scale` (≥0.001)

---

## 5. Geometry 节点（组操作）

### 5.1 GroupCreate

| 项 | 值 |
|---|---|
| 文件 | `geometry_elements.cpp` |
| Algorithm | `geometry_algorithms.cpp:group_create()` |
| 输入 | Geometry (pin "in") |
| 输出 | Geometry (pin "out") + outputGroup |

**算法**：在 PcgGeometry 的 GroupTable 中创建命名组：
- **mode=angle**：二面角 > `minEdgeAngle` 的边加入组
- **mode=unshared**：边界边（face1==-1）加入组
- **fromFaceGroup/fromEdgeGroup**：从现有组中筛选成员

domain 支持 point/edge/face。输出组名通过 `outputGroup` 参数指定，并在 manifest 中声明为 `isGroupOutput`。

**参数**：`outputGroup`, `domain` (edge/face/point), `mode` (angle/unshared), `minEdgeAngle` (0-180), `includeUnshared`, `fromFaceGroup`, `fromEdgeGroup`

### 5.2 GroupCombine

| 项 | 值 |
|---|---|
| 文件 | `geometry_elements.cpp` |
| Algorithm | `geometry_algorithms.cpp:group_combine()` |
| 输入 | Geometry (pin "in") |
| 输出 | Geometry (pin "out") + outputGroup |

**算法**：对现有组执行集合操作：
- **union**：`union_into(dst, src1, src2, ...)`
- **intersect**：`intersect_into(dst, a, b)`
- **subtract**：`subtract_into(dst, src)`

使用 GroupTable 的 `eval()` 解析表达式（如 `"a - b"`）。

**参数**：`outputGroup`, `domain`, `operation` (union/intersect/subtract), `sourceGroups`

---

## 6. Boolean 节点

### 6.1 BooleanMesh

| 项 | 值 |
|---|---|
| 文件 | `boolean_elements.cpp` |
| Algorithm | `geometry/boolean_output.cpp:execute_boolean()` → `geometry/boolean_csg.cpp` |
| 输入 | Geometry (pin "a"), Geometry (pin "b") |
| 输出 | Geometry (pin "out") + face/edge groups |

**算法**：
1. 两个 PcgGeometry 转为 IMesh（索引网格）
2. `execute_boolean()` 调用：
   - `arrangement`：三角形-三角形精确求交 + 交叉分割
   - `find_patches`：共面三角形分组
   - `find_cells`：空间分区
   - `find_ambient_cell`：找外部 cell
   - `propagate_windings`：BFS 传播 winding number
   - `extract_boolean_geometry`：提取标记的 cell 面
3. `finalize_boolean_output()`：依据 `source + orig_face` 重建输入 polygon；`unchanged` 跳过交线切割面，`ab_seams` 作为合并屏障

输出包含 face groups：`a_inside_b`, `a_outside_b`, `b_inside_a`, `b_outside_a`, `ab_seams`。

支持 `useSelf`（A 与自身做 boolean）、`triangleBudget`（限制三角形数量）。

**参数**：`operation` (union/intersect/subtract/shatter), `treatAAs`/`treatBAs` (solid/surface), `useSelf`, `detriangulate` (all/unchanged/none), `weldEpsilon`, `triangleBudget`

---

## 7. Filter / Metadata 节点

### 7.1 DensityFilter

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 概率过滤 |
| 输入 | Points (pin "in") |
| 输出 | Points (pin "out") |

**算法**：对每个点按 `density` 概率保留（0=全部删除，1=全部保留）。使用 LCG 随机数。

**参数**：`density` (0-1)

### 7.2 AttributeFilter

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 属性匹配过滤 |
| 输入 | Points (pin "in") |
| 输出 | Points (pin "out") |

**算法**：保留 `attributeName` 属性值等于 `matchValue` 的点。

**参数**：`attributeName`, `matchValue` (string)

### 7.3 CopyAttributes

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 属性写入 |
| 输入 | Points (pin "in") |
| 输出 | Points (pin "out") |

**算法**：将指定属性值写入所有点。`attributeNames` 指定要设置的属性（逗号分隔）。

**参数**：`attributeNames` (string), `prefab`, `scale` 等

### 7.4 DeleteAttributes

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 属性删除 |
| 输入 | Points (pin "in") |
| 输出 | Points (pin "out") |

**算法**：从所有点中删除指定属性。

**参数**：`attributeNames` (string)

### 7.5 BreakAttributes

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 属性分离 |
| 输入 | Points (pin "in") |
| 输出 | Points (pin "out") |

**算法**：将指定属性从点数据中分离为独立 JSON 载荷。

**参数**：`attributeName` (string)

---

## 8. Transform 节点

### 8.1 TransformPoints

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 仿射变换 |
| 输入 | Points (pin "in") |
| 输出 | Points (pin "out") |

**算法**：对每个点应用 平移 → Y 轴旋转 → 缩放。

**参数**：`translateX/Y/Z`, `scale`, `rotationY` (度)

### 8.2 ProjectPoints

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 地形投影 |
| 输入 | Points (pin "in"), Param (pin "terrain") |
| 输出 | Points (pin "out") |

**算法**：将点的 Y 坐标设置为地形高度。`useTerrain` 启用地形采样，否则使用 `baseY`。

**参数**：`useTerrain` (bool), `baseY` (double)

---

## 9. Sampler / Input 节点

### 9.1 GetTerrainData

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 程序化地形生成 |
| 输入 | 无 |
| 输出 | Param (pin "out") |

**算法**：按 `gridSize`/`cellSize` 生成网格，`amplitude`/`seed` 控制高度噪声。输出为 JSON Param 供 ProjectPoints/SampleSurface 消费。

**参数**：`gridSize` (≥2), `cellSize` (≥0), `amplitude`, `seed`

### 9.2 SampleSurface

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 地形表面采样 |
| 输入 | Points (pin "in"), Param (pin "terrain") |
| 输出 | Points (pin "out") |

**算法**：将点投影到地形表面，`blend` 控制混合系数（0=原始高度，1=完全投影）。

**参数**：`offsetY`, `blend` (0-1)

### 9.3 GetMeshData

| 项 | 值 |
|---|---|
| 文件 | `mesh_scatter_elements.cpp` |
| 算法 | Runtime slot 查找 |
| 输入 | 无（从 `ctx.meshes` 获取） |
| 输出 | Mesh (pin "out") |

**算法**：从 `MeshRuntime` 中按节点 ID 查找上传的网格数据。对应 v4 API 的 `PcgMeshSlot`。

**参数**：`source` (Binding/Self/Asset), `bindingKey`, `meshAsset`

---

## 10. Material / UV / Texture 节点

### 10.1 VertexColor

| 项 | 值 |
|---|---|
| 文件 | `material_elements.cpp` |
| Algorithm | `material_algorithms.cpp:vertex_color_mesh()` |
| 输入 | Mesh (pin "in") |
| 输出 | Mesh (pin "out") |

**算法**：为网格的每个顶点设置统一的 RGBA 颜色。

**参数**：`r`, `g`, `b`, `a` (0-1)

### 10.2 AssignMaterial

| 项 | 值 |
|---|---|
| 文件 | `material_elements.cpp` |
| Algorithm | `material_algorithms.cpp:assign_material()` |
| 输入 | Mesh (pin "in") |
| 输出 | Mesh (pin "out") |

**算法**：Geometry 路径向全部面或所选 face group 写入逐面材质名；Sink 三角化时建立稳定材质槽表与逐三角形槽索引。Mesh-only fallback 仅支持全局材质。

**参数**：`group` (face groupMultiSelect), `materialName` (string)

### 10.3 UVTexture

| 项 | 值 |
|---|---|
| 文件 | `uv_elements.cpp` |
| Algorithm | `uv_algorithms.cpp:generate_uv()` |
| 输入 | Mesh (pin "in") |
| 输出 | Mesh (pin "out") |

**算法**：按投影方式生成 UV 坐标：
- **planar**：沿指定轴投影到平面
- **cylindrical**：圆柱投影
- **spherical**：球面投影

`scaleU/V` 控制缩放，`offsetU/V` 控制偏移。Geometry 路径同时写 point UV 与 corner UV；未知投影（含遗留 `box`）拒绝执行。

**参数**：`projection` (planar/cylindrical/spherical), `axis` (x/y/z), `scaleU`, `scaleV`, `offsetU`, `offsetV`

### 10.4 ProjectTexture

| 项 | 值 |
|---|---|
| 文件 | `uv_elements.cpp` |
| Algorithm | `uv_algorithms.cpp:project_texture_uv()` |
| 输入 | Mesh (pin "in"), Texture (pin "texture") |
| 输出 | Mesh (pin "out") |

**算法**：沿指定方向投影纹理到网格表面生成 UV。`repeatX/Y` 从 ImageTexture 节点传入。

**参数**：`direction` (x/y/z), `scaleU`, `scaleV`, `offsetU`, `offsetV`

### 10.5 ImageTexture

| 项 | 值 |
|---|---|
| 文件 | `mesh_elements.cpp` |
| 算法 | 纹理 slot 声明 |
| 输入 | 无 |
| 输出 | Param (pin "out") |

**算法**：声明一个纹理 slot，将节点 ID 作为 `slotId`。实际纹理像素由 Unity 通过 `PcgTextureSlot` (v3 API) 上传到 `TextureRuntime`。`repeatX/Y` 控制平铺。

**参数**：`texture` (texture2d), `repeatX` (0.01-64), `repeatY` (0.01-64)

---

## 11. Spawner / Output 节点

### 11.1 PlaceInScene

| 项 | 值 |
|---|---|
| 文件 | `element_registry.cpp` |
| 算法 | 点包装 |
| 输入 | Points (pin "in") |
| 输出 | Points (pin "out") + sidecar JSON |

**算法**：将输入点包装为带 prefab/scale 信息的输出。sidecar JSON 包含 `status`/`prefab`/`scale`/`pointCount`。

**参数**：`prefab` (string), `scale` (double)

### 11.2 StaticMeshSpawner

| 项 | 值 |
|---|---|
| 文件 | `primitive_elements.cpp` |
| 算法 | 实例化网格生成 |
| 输入 | Points (pin "in"), Mesh (pin "mesh") |
| 输出 | Points (pin "out") + spawnMesh |

**算法**：在每个点位置生成一个 mesh 实例，合并为 `spawnMesh`。输出点数据保持不变，`spawnMesh` 作为 sidecar 数据传递到 Sink。

**参数**：`prefab`, `mesh`, `scale`

### 11.3 Output

| 项 | 值 |
|---|---|
| 文件 | `element_registry.cpp` |
| 算法 | Pass-through |
| 输入 | Any (pin "in") |
| 输出 | Any (pin "out") |

**算法**：按优先级转发输入：Geometry > Mesh > Points > Spline > JSON。同时转发 `spawnMesh`。执行引擎优先选择 `type=="Output"` 的无出边节点作为 Sink。

**参数**：`label` (string)

---

## 算法复杂度汇总

| 节点 | 时间复杂度 | 空间复杂度 | 备注 |
|------|-----------|-----------|------|
| SpawnPoints | O(n) | O(n) | n=count |
| ConvexHull | O(n log n) | O(n) | 排序主导 |
| ConnectNearest | O(n² log k) | O(nk) | 每点排序 k 个邻居 |
| Delaunay | O(n²) 最坏, O(n log n) 平均 | O(n) | Bowyer-Watson |
| MST | O(E log E) | O(V) | Kruskal + Union-Find |
| Voronoi | O(n²) 最坏 | O(n) | Delaunay 对偶 |
| AStarPathfinding | O((V+E) log V) | O(V) | 优先队列 |
| SubdivideMesh | O(V × 4^L) | O(V × 4^L) | L=levels |
| BevelMesh | O(E × S) | O(V+E) | S=segments |
| BooleanMesh | O((A+B) log(A+B)) | O(A+B) | Arrangement |
| MeshNoiseDeform | O(V) | O(V) | 每顶点独立 |
| SweepAlongSpline | O(F × C) | O(F × C) | F=frames, C=columns |
