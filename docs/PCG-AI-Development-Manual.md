# PCG-AI 学习开发手册

> **版本**：随当前仓库结构维护；具体支持矩阵以 `README.md` 与 CI 为准
> **范围**：算法原理、工程架构、数据流协议、开发指南
> **维护**：随主分支演进同步更新

---

## 目录

1. [项目总览](#1-项目总览)
2. [系统架构](#2-系统架构)
3. [核心数据模型](#3-核心数据模型)
4. [节点系统设计](#4-节点系统设计)
5. [图执行引擎](#5-图执行引擎)
6. [几何算法详解](#6-几何算法详解)
7. [二进制协议与跨端传输](#7-二进制协议与跨端传输)
8. [Unity 集成层](#8-unity-集成层)
9. [构建与测试](#9-构建与测试)
10. [扩展开发指南](#10-扩展开发指南)
11. [关键工程决策记录](#11-关键工程决策记录)
12. [术语表](#12-术语表)

---

## 1. 项目总览

### 1.1 定位

PCG-AI 是一个**跨引擎程序化内容生成（PCG）框架**，核心理念：

```
Graph JSON（编辑器无关的图数据契约）→ pcg-server → C++ 核心执行 → Unity / Web 预览
```

- **不自研 Block 注册表框架**，直接对齐 Unreal Engine PCG 节点模型（Settings / Element / Context / DataCollection）
- **不引入 UE 引擎依赖**（无 UObject），仅在独立 C++ 库中复刻数据流与节点语义
- **C++ 是唯一算法执行体**；各端（Unity / Web / Unreal）负责 UI 与引擎绑定

### 1.2 里程碑演进

| 里程碑 | 内容 | 状态 |
|--------|------|------|
| M0 | 核心库与回归测试就绪 | ✅ |
| M2 | Web 编辑 → JSON → C++ 执行 → Unity Gizmo 预览闭环 | ✅ |
| M2.5 | Unity GraphView 编辑器（替代 Web 优先路径） | ✅ |
| M3 | Unity 改用外置 localhost `pcg-server`，不再打包原生插件 | ✅ |
| M4 | Phase 4.1：13 UE 命名标准节点 + manifest 驱动 | ✅ |
| M4.4 | Mesh Surface Scatter + 场景绑定模型 | ✅ (4.4a–c) |
| M4.6 | PcgGeometry + Group 几何语义层 | ✅ |
| M4.6b | Bevel BMesh-Native（Blender 对齐） | ✅ |
| M4.7 | Boolean Mesh（精确 CSG） | 🔧 进行中 |
| — | Split Normal 系统（硬表面着色） | 📋 Plan 就绪 |
| — | Scene View UI（Houdini/Blender 风格交互） | 🔧 Phase 1 完成 |

### 1.3 仓库结构

```
PCG-AI/
├── .agents/                    # Agent skills、扩展清单与安装入口
├── pcg-core/                    # C++ 核心与算法测试（CMake）
│   ├── include/pcg_api.h        # 对外 C API（唯一公开接口，v1→v10 additive）
│   ├── src/
│   │   ├── data/                # 数据模型：PcgGeometry, GroupTable, PcgMeshData, PcgPointData...
│   │   ├── geometry/            # 几何内核：BMesh, Sweep, Boolean, BVH, Robust Predicates...
│   │   ├── elements/            # 节点实现：每节点一个 Element 类
│   │   ├── graph_executor.cpp   # 拓扑排序 + 逐节点执行 + Sink 输出
│   │   ├── graph_parser.cpp     # Graph JSON 解析
│   │   ├── cook_hash.cpp        # 参数 hash + 脏缓存
│   │   └── pcg_core.cpp         # C API 导出层
│   └── tests/                   # CTest 回归测试
│
├── pcg-server/                  # localhost HTTP/MCP 服务，链接 pcg-core
├── schema/                      # Graph JSON 契约（Web / C++ / Unity 共用）
│   ├── graph-schema-v2.json     # JSON Schema v2
│   ├── node-manifest.json       # 节点 Pin / 参数定义（SSOT）
│   └── editor-export.pcg        # 本地 Web→Unity 交接文件（不入库）
│
├── Unity/Assets/PcgPlugin/      # Unity 插件
│   ├── Runtime/                 # HTTP cook 客户端、GraphLoader、ResultParser、Component...
│   ├── Editor/Graph/            # GraphView 编辑器 + NodeInspector + SceneHandles
│   └── Editor/                  # 服务端设置、预览、导入与导出工具
│
├── Unity/Assets/Samples/PCG-AI/ # Unity demos、showcases 与 validation scenes
├── web/pcg-editor/              # Web 编辑器（Vite + React + ReactFlow）
├── scripts/                     # 构建脚本（PowerShell + Bash）
├── examples/                    # graphs / subgraphs / tests / showcases / storyboards
└── docs/                        # 文档
```

### 1.4 数据流总览

```
Unity / Web editor
    → Graph JSON + external resources
    → localhost pcg-server (HTTP / MCP)
    → pcg-core execution
    → versioned binary / JSON cook payload
    → Unity Scene or Web preview
```

---

## 2. 系统架构

### 2.1 三层契约

PCG-AI 的核心设计是**编辑器无关的图数据契约**：

| 层 | 文件 | 谁消费 | 通用性 |
|----|------|--------|--------|
| **图数据** | `.pcg`（Graph JSON v2） | 所有编辑器 + pcg-core | 高 — nodes/edges/settings |
| **节点定义** | `node-manifest.json` | Web / Unity GraphView / 未来 UE 面板 | 高 — Pin 类型、参数 schema |
| **执行结果** | Binary mesh / point buffer + JSON | Unity / UE Spawner / Web 3D 预览 | 高 — 点集 + Metadata |

**核心原则**：Graph JSON 不是某引擎原生资产格式；各端负责 UI 与引擎绑定，C++ 是唯一执行体。

### 2.2 UE PCG 节点模型对齐

PCG-AI 在独立 C++ 库中复刻 UE PCG 的四件套：

| UE PCG 组件 | 职责 | pcg-core 对应 |
|-------------|------|----------------|
| PCGSettings | 参数 + Input/Output Pin 定义 | `PcgNodeSettings` + JSON `data` |
| IPCGElement | `ExecuteInternal(Context)` | `IPcgElement` + 每节点实现类 |
| PCGContext | Input/Output DataCollection | `PcgContext` |
| PCGDataCollection | 节点间数据总线 | `PcgDataCollection` + TaggedData |
| PCGData | Spatial / Param 多态 | Point / Spline / Surface / Mesh / Param |
| Metadata | 点属性（prefab、scale、mesh…） | `PcgMetadata` |

### 2.3 分层架构

```
┌─────────────────────────────────────────────────────┐
│                   编辑器层                            │
│  Unity GraphView │ Web ReactFlow │ (Unreal 远期)     │
├─────────────────────────────────────────────────────┤
│                   Graph JSON (.pcg)                  │
├─────────────────────────────────────────────────────┤
│                   C API (pcg_api.h)                  │
│              v1 → v2 → v3 → v4 → v5 → v6 → v7       │
├─────────────────────────────────────────────────────┤
│                   图执行引擎                          │
│     GraphParser → TopoSort → Element.Execute()      │
│         → CookCache → Sink Output                    │
├──────────────┬──────────────┬───────────────────────┤
│   数据模型    │   几何内核    │      节点实现          │
│ PcgGeometry  │   BMesh      │  CreatePointGrid      │
│ GroupTable   │   Sweep      │  SweepAlongSpline     │
│ PcgMeshData  │   Boolean     │  BevelMesh            │
│ PcgPointData  │   BVH        │  BooleanMesh          │
│ PcgSplineData│   Robust     │  SampleMeshSurface    │
│              │   Predicates │  GroupCreate/Combine  │
│              │   Arrangement │  StaticMeshSpawner    │
└──────────────┴──────────────┴───────────────────────┘
```

### 2.4 C API 演进策略

API 采用 **additive 版本策略**——新版本不破坏旧调用方：

| 版本 | 新增能力 | 回退行为 |
|------|----------|----------|
| v1 | 基础图执行 → JSON 结果 | — |
| v2 | Mesh binary 输出 | mesh sink → `PCG_ERR_EXECUTION` |
| v3 | 纹理 slot 上传（ImageTexture） | textures=null → 退化为 v2 |
| v4 | Mesh slot 上传（GetMeshData） | mesh_count=0 → 退化为 v3 |
| v5 | Per-node 脏缓存 + CookStats | mesh_count=0 → 退化为 v4 |
| v6 | Point binary 输出 + Perf JSON | 非 point sink → 退化为 v5 |
| v7 | Spline slot 上传 | spline_count=0 → 退化为 v6 |

---

## 3. 核心数据模型

### 3.1 PcgGeometry — Canonical 几何类型

`PcgGeometry` 是节点间传递的**规范空间 mesh 类型**，对齐 Houdini `GU_Detail`：

```cpp
struct PcgGeometry {
    std::vector<PcgVec3> points;           // 点位置（double 精度）
    std::vector<std::vector<int>> faces;    // CCW 多边形（点索引，支持 n-gon）
    GroupTable groups;                       // 命名组（point/edge/face 域）
    AttributeTable attrs;                   // 按域属性（pscale, N, mat_id...）
    GeometryDetailMeta detail;               // 全局元数据（shade_mode 等）
};
```

**设计原则**：
- **三角化推迟到 Sink**：建模算子在 polygon 层操作，不绑定 Unity 三角限制
- **语义在 geometry 上**：groups/attributes 是一等公民，不藏在节点隐式启发式里
- **一套域模型**：Scatter point attrs、spline frame、geometry groups 统一命名规范

### 3.2 GroupTable — 命名组系统

对齐 Houdini primitive/point/edge group 语义：

```cpp
enum class GroupDomain { Point, Edge, Face };

class GroupTable {
    bool contains(GroupDomain, const std::string& name, int id) const;
    void add / remove / clear(...);
    // Edge ID 稳定键：edge_key(min(p0), max(p1))，int64
};
```

**核心规则**：
- **Merge 规则**：同名 group 取**并集**；可选 `prefix` 避免冲突（`deck_` / `rail_`）
- **禁止**把 group 成员塞进 `PcgMetadata` JSON
- **SweepAlongSpline 标准输出组**：

| Group | 域 | 含义 |
|-------|-----|------|
| `side` | face | 沿 backbone 侧壁 |
| `cap_start` / `cap_end` | face | 端盖 |
| `unshared` | edge | 边界边（自动维护） |

### 3.3 PcgMeshData — 显示/Sink 产物

`PcgMeshData` 是三角化后的显示 mesh，**不是建模语义的载体**：

```cpp
class PcgMeshData {
    std::vector<PcgVertex> vertices_;    // float3 位置
    std::vector<int> triangles_;          // 三角形索引
    std::vector<PcgVertex> normals_;      // 法线（v2 binary 可选）
    PcgMetadata metadata_;                // JSON bag（collection 级）
    bool has_normals_ = false;
};
```

### 3.4 PcgPointData — 点云数据

```cpp
struct PcgPoint {
    float x, y, z;               // 位置
    std::map<std::string, nlohmann::json> attributes;  // nx, ny, nz, scale, prefab...
};
```

点属性全图贯穿，支持 `nx/ny/nz`（法线）、`scale`、`prefab`、`triIndex` 等。

### 3.5 PcgSplineData — 样条数据

支持 Catmull-Rom 样条，携带控制点、闭合标志，用于 SweepAlongSpline 等节点。

### 3.6 类型转换路径

```
PcgGeometry (polygon + groups)
    │
    ├──▶ triangulate_geometry_shared()  ──▶ PcgMeshData (共享顶点，用于 manifold 检查)
    ├──▶ compute_split_normals()        ──▶ PcgMeshData (拆点 + 法线，用于显示)
    ├──▶ geometry_from_mesh()           ◀── PcgMeshData (三角汤 → 重建 polygon)
    │
    └──▶ PcgDataCollection.add_geometry()  ──▶ 节点间传递
```

---

## 4. 节点系统设计

### 4.1 节点分类与清单

| 分类 | 节点 | 状态 | UE 对标 |
|------|------|------|---------|
| **Generation** | CreatePointGrid, CreatePoints, SurfaceSampler, SampleMeshSurface | ✅ | Create Point Grid, Surface Sampler |
| **Mesh** | CreateBoxMesh, SubdivideMesh, MeshNoiseDeform | ✅ | — |
| **Spline** | CreateSpline, SweepAlongSpline | ✅ | Spline Sampler |
| **Geometry** | GroupCreate, GroupCombine | ✅ | Group SOP |
| **Mesh Ops** | BevelMesh, BooleanMesh, TransformMesh, MergeMesh | ✅/🔧 | PolyBevel, Boolean |
| **Metadata** | CopyAttributes, DeleteAttributes, BreakAttributes | ✅ | — |
| **Filter** | DensityFilter, AttributeFilter | ✅ | — |
| **Transform** | TransformPoints, ProjectPoints | ✅ | — |
| **Sampler** | GetTerrainData, GetMeshData, SampleSurface | ✅ | Get Actor Data |
| **Spawner** | StaticMeshSpawner | ✅ | Static Mesh Spawner |
| **Texture** | ImageTexture | ✅ | — |
| **Output** | Output | ✅ | — |

### 4.2 Element 模式

每个节点遵循 UE PCG Element 模式：

```cpp
class IPcgElement {
public:
    virtual ~IPcgElement() = default;
    virtual PcgResultCode execute(PcgContext& ctx) = 0;
    virtual uint64_t cook_hash(const PcgNode& node) const { /* ... */ }
};
```

**节点注册**：`ElementRegistry` 在初始化时建立 `type_string → element_factory` 映射。

### 4.3 node-manifest.json — SSOT

`schema/node-manifest.json` 是节点定义的**唯一真源**：

```json
{
  "type": "BevelMesh",
  "displayName": "Bevel Mesh",
  "category": "Mesh",
  "inputs": [{ "id": "in", "label": "Mesh", "pinType": "SpatialMesh" }],
  "outputs": [{ "id": "out", "label": "Mesh", "pinType": "SpatialMesh" }],
  "outputGroups": [
    { "name": "bevel_edges", "domain": "edge", "label": "Beveled edges" }
  ],
  "properties": {
    "amount": { "type": "number", "default": 0.1 },
    "segments": { "type": "integer", "default": 2 },
    "edgeGroup": { "type": "string", "default": "" },
    "excludeGroups": { "type": "string", "default": "" },
    "excludeUnshared": { "type": "boolean", "default": true }
  }
}
```

各编辑器读 manifest 渲染 UI，**零硬编码节点**。

### 4.4 Pin 类型

| Pin 类型 | 数据载体 | 典型节点 |
|----------|----------|----------|
| `SpatialPoint` | PcgPointData | CreatePointGrid → Spawner |
| `SpatialMesh` | PcgMeshData / PcgGeometry | CreateBoxMesh → BevelMesh |
| `SpatialSpline` | PcgSplineData | CreateSpline → SweepAlongSpline |
| `SpatialSurface` | — | GetTerrainData |
| `Param` | JSON | Settings Override |

`SpatialMesh` pin 在 `graph_executor` 中同时支持 `PcgMeshData`（旧路径）与 `PcgGeometry`（新路径，保留 groups）。

---

## 5. 图执行引擎

### 5.1 执行流程

```
pcg_execute_graph_vN(json, seed, slots...)
    │
    ▼
GraphParser::parse(json)
    │  → 构建 PcgGraph（nodes + edges + settings）
    ▼
TopologicalSort(graph)
    │  → 检测循环依赖 → PCG_ERR_CYCLE_DETECTED
    ▼
for each node in topo_order:
    │
    ├── ElementRegistry::get(node.type) → IPcgElement
    ├── CookCache::check(node, input_hash) → skip if cached
    ├── element->execute(ctx)
    │     ├── ctx.inputs = upstream outputs
    │     ├── ctx.node = current node settings
    │     └── ctx.outputs = result (Point/Mesh/Geometry/Spline)
    ├── CookCache::store(node, input_hash, output)
    └── PcgCookStats::record(node, elapsed_ms)
    │
    ▼
Sink Output:
    ├── find_geometry("out") || primary_geometry()
    ├── compute_split_normals(geometry, options)  → PcgMeshData
    ├── build_group_stats(geometry)               → face→triangle 映射 JSON
    ├── build_node_stats(geometry, nodes)         → per-node points/faces/triangles JSON
    ├── write_mesh_binary(mesh, buf)              → binary buffer
    └── write_result_json(stats, groups, nodes)   → JSON buffer
```

### 5.2 Cook Cache（Blender Depsgraph 对齐）

```cpp
struct CookCacheEntry {
    uint64_t input_hash;          // 上游输出 hash
    PcgDataCollection cached_output;
    bool dirty = true;
};
```

- 参数变更 → 脏传播：仅标记下游节点 dirty
- 同参数连点两次 Run → 第二次跳过（<10% 耗时）
- `pcg_cook_cache_clear()` 清除会话缓存

### 5.3 Cook Hash

`cook_hash()` 对节点参数 + 上游输出 hash 做组合 hash，用于：
- 缓存命中判断
- `hash_geometry()` 必须包含 groups + attrs + detail（shade_mode, cusp_angle_deg）

---

## 6. 几何算法详解

### 6.1 BMesh — 编辑内核

BMesh 是 Blender 风格的半边网格结构，作为 bevel 等 topology-sensitive 算法的编辑内核：

```cpp
struct BMeshEdge {
    int v0, v1;
    int face0, face1;        // 邻接面（face1 < 0 = boundary）
    bool sharp;
    // group membership
};

struct BMeshFace {
    std::vector<int> verts;  // CCW 顶点环
    // group membership
};
```

**关键路径**：
- `bmesh_from_geometry(PcgGeometry)` — 从 polygon geometry 构建，保留 edge/face groups
- `geometry_from_bmesh(BMesh)` — 反向转换，用于 detriangulation

### 6.2 Bevel — Blender bmesh_bevel 对齐

#### 6.2.1 算法概述

Bevel 移植自 Blender `bmesh/tools/bmesh_bevel.cc`，核心流程：

```
输入: PcgGeometry + edge selection (edge group / angle / unshared)
    │
    ▼
1. bmesh_from_geometry()           → BMesh (含 face groups)
2. bevel_mesh()                    → BevelOutput
   ├── build_boundary()            → 构建边界 BoundVert 环
   │   ├── build_boundary_terminal_edge()  → 终端边处理
   │   └── build_boundary_beveled_edge()   → 倒角边处理
   ├── build_vmesh()               → 构建 VMesh (圆角顶点环)
   ├── build_edge_polygons()      → 构建边带四边形 strip
   ├── rebuild_faces_bmesh()      → 重建原始面 (cap polygon)
   │   └── add_oriented_polygon() → fan-triangulate + 方向决策
   ├── fix_winding()              → BFS 传播 manifold winding
   └── dedup()                    → 移除退化三角形
3. OutputMesh → PcgGeometry       → 保留 face_origins (BMesh face index)
```

#### 6.2.2 关键设计决策

| 决策 | 理由 |
|------|------|
| `EdgeHalf.fprev/fnext` 存 **BMesh face index**（非 tri index） | 对齐 Blender `BMFace*` 引用模型；三角汤 tri index 无法表达 n-gon 面身份 |
| `fix_winding()` BFS 传播 + volume-based 全局校正 | fan triangulation 的 closing edge 与 strip edge 共线导致固有 winding bad；逐边 greedy flip O(N²) 不可接受 |
| `add_oriented_polygon` 法线用 **sum of all fan-triangle normals** | cap 平面多边形首 3 顶点共面/共线时 cross ≈ 0（2.7e-8），flip 决策由噪声决定 |
| **禁止** mesh_center 翻面 | Blender 无此步骤；弯扫掠上质心外向启发式无效 |
| **禁止** `representative_triangle_for_bmesh_face` | 三角汤 tri index 无法可靠映射到 BMesh face |

#### 6.2.3 Face Group 传播

Bevel 输出的 `PcgGeometry` 通过 `face_origins` 追踪每个 triangle 的原始 BMesh face index：

```
输入 face groups: {side: [0,1,2], cap_start: [3], cap_end: [4]}
    │
    ▼ bevel
输出 face_origins: [0,0,1,1,2,2, -1,-1, 3,3, 4,4]
                      ↑ side faces  ↑ edge strip  ↑ cap rebuild
    │
    ▼ build_group_stats()
face→triangle 映射: 将 geometry face 索引展开为 mesh triangle 索引
```

- edge strip / VMesh cap 新三角形 `origin = -1`（不属于任何原始 face group）
- `excludeGroups` 默认排除 `cap_start,cap_end`，对齐 Houdini PolyBevel

#### 6.2.4 Axis-Aligned Box 快捷路径

Blender `bevel_mesh_blender` 对轴对齐盒体有快捷路径，返回 `PcgMeshData`（不填充 `out_geometry`）。`bevel_geometry()` 必须使用返回值作 fallback。

### 6.3 SweepAlongSpline — 扫掠几何

#### 6.3.1 算法概述

```
输入: Spline (control points) + Profile (shape) + Options
    │
    ▼
1. spline_geometry::resample_spline()     → 等弧长采样
2. spline_geometry::compute_frames()       → Frenet frame (T, N, B)
3. sweep_geometry::sweep_curve_profile()   → 沿 backbone 扫掠 profile
   ├── 每个采样点放置 profile 截面
   ├── 相邻截面四边化 → side faces
   ├── 端盖：质心 fan / ear-clipping (凹多边形安全)
   └── 输出 PcgGeometry + face groups (side, cap_start, cap_end)
    │
    ▼
输出: PcgGeometry (共享顶点, n-gon faces, named groups)
```

#### 6.3.2 Group 契约（对齐 Houdini）

| Group | 域 | 含义 |
|-------|-----|------|
| `side` | face | 沿 backbone 侧壁 |
| `cap_start` / `cap_end` | face | 端盖 |
| `unshared` | edge | 边界边（自动维护） |

**重要决策**：SweepAlongSpline **不创建 edge group**（`seam`, `profile_corner`），只输出 face group + unshared edge group。这与早期设计不同——经实测，edge group 会导致 bevel 选边冲突。

### 6.4 Boolean Mesh — 精确 CSG（Phase 4.7）

#### 6.4.1 算法路线

PCG-AI 拒绝 Blender Fast (Float) 路径，采用 **Exact Solver**（对齐 Blender Exact + Houdini Boolean 2.0）：

```
Step 0: [可选] resolve_self_intersections(operand)     // useSelf
Step 1: IMesh 构建（三角化 + weld + 量化 + 变换烘焙）
Step 2: BVH broad-phase → 精确 tri_tri_intersect → 分割 + coplanar partition
Step 3: Arrangement adjacency → winding number BFS 传播
Step 4: CSG 分类（Union/Intersect/Subtract/Shatter）
Step 5: source-face-aware BMesh detriangulate → PcgGeometry + outputGroups
```

#### 6.4.2 精确算术

采用 **Shewchuk adaptive predicates**（非 GMP）：

| 平台 | 128-bit 乘法实现 |
|------|-----------------|
| macOS / Linux clang | `__int128` |
| Windows MSVC | `wide_int.hpp`：`_umul128` + carry chain |

**为什么不用浮点**：
- 浮点精度导致拓扑错误（共面/共线分割位置不确定 → 缝隙/重叠）
- 无法处理自交
- 无 winding number 分类 → ray casting 退化失败

#### 6.4.3 Winding Number 传播

**禁止**用「AABB 最外侧三角形」定无穷远（凹形体不可靠）：

```
1. 构建 triangle adjacency
2. 找 open boundary / convex hull 外侧 seed cell (w = 0)
3. BFS 传播：跨越三角面 w[source] += sign(cross · face_normal)
4. CSG 规则：
   - Union:      w_A > 0 || w_B > 0
   - Intersect:  w_A > 0 && w_B > 0
   - Subtract:   w_A > 0 && w_B == 0
   - Shatter:    keep all cells
```

非流形且不满足 PWN（Piecewise Winding Number）条件 → `BooleanError::NonManifold`（hard fail，禁止 ray casting fallback）。

#### 6.4.4 输出 Groups

| Group | 域 | 含义 |
|-------|-----|------|
| `a_inside_b` | face | A 在 B 内部的面 |
| `a_outside_b` | face | A 在 B 外部的面 |
| `b_inside_a` | face | B 在 A 内部的面 |
| `b_outside_a` | face | B 在 A 外部的面 |
| `ab_seams` | edge | A-B 交线（`edge_key` 编码） |

`ab_seams` 可直接作为下游 `BevelMesh` 的 `edgeGroup`，实现 Boolean → Bevel 链。

### 6.5 Split Normal 系统（硬表面着色）

#### 6.5.1 问题

SweepAlongSpline 生成的 mesh 在 Unity 中全平滑着色，cap 与 side 连接处应呈现硬边。

#### 6.5.2 算法

Split Normal 只发生在 **graph sink 输出阶段**，不修改 bevel 核心：

```
输入: PcgGeometry (共享顶点, face groups)
    │
    ▼
1. 构建 polygon corner 拓扑（不是 triangle edge！）
   ├── edge_key = minmax(point, nextPoint) per polygon edge
   └── fan diagonal 不参与 island 连通或 hard-edge 判断
    │
    ▼
2. 判定 hard edge（per real polygon edge）:
   ├── incidence == 1: boundary, always hard
   ├── incidence != 2: non-manifold, always hard
   ├── incidence == 2 + Flat: hard
   ├── incidence == 2 + Smooth: soft
   └── incidence == 2 + Auto: hard if group_sets_differ OR angle > cusp
    │
    ▼
3. Union-Find on corners (NOT triangles, NOT points)
   └── soft edge → union 两侧 corners
    │
    ▼
4. 生成 render mesh:
   ├── render vertex key = (source_point_index, island_root)
   ├── 面积加权法线累加 → normalize
   └── triangle index 仅 remap，顺序不变
```

#### 6.5.3 ShadePolicy 传播

```
SweepAlongSpline (shade_mode, cusp_angle)
    │ geometry.detail()
    ▼
BevelMesh ──── 显式 out.detail() = input.detail()
    │
    ▼
TransformMesh ── 整体复制 detail
    │
    ▼
MergeMesh ──── 相同 policy 合并；冲突 → 错误
    │
    ▼
graph_executor: compute_split_normals(geometry, detail)
```

### 6.6 Mesh Surface Scatter

#### 6.6.1 采样算法

```cpp
// 面积加权随机采样
for (int i = 0; i < count; i++) {
    // 1. 按三角面积加权随机选一个三角形
    // 2. 在三角形内做 barycentric 随机采样
    // 3. 计算面法线 → 写入 nx, ny, nz 属性
}
```

#### 6.6.2 场景绑定模型（对齐 UE PCG 三层）

| 层 | 职责 | 本项目对应 |
|----|------|------------|
| Graph Asset | 可复用配方 | `.pcg` 文件 |
| Component / Volume | 场景上下文 | `PcgGraphComponent` |
| Get Actor Data | 运行时解析 | `GetMeshData` + Mesh Binding 表 |

**核心契约**：`.pcg` 文件只存 `bindingKey`，不存场景路径/GUID。

```
解析优先级:
1. Component MeshBindings（场景 Run 正式路径）
2. Graph Editor PreviewMeshBindings（Editor-only Session）
3. 节点 source: Asset + meshAsset 路径
4. 节点 source: Self → Component 所在 GameObject 的 MeshFilter
```

---

## 7. 二进制协议与跨端传输

### 7.1 Mesh Binary

#### v1（当前兼容）

```
[magic:4][version:4=1][vert_count:4][index_count:4]   // header 16 bytes
[float3 × vert_count]                                   // positions
[uint32 × index_count]                                 // triangle indices
```

#### v2（Split Normal 系统）

```
[magic:4][version:4=2][vert_count:4][index_count:4][flags:4]  // header 20 bytes
[float3 × vert_count]                                            // positions
[uint32 × index_count]                                           // triangle indices
[float3 × vert_count]?                                           // normals (if flags & 0x1)
```

**Unity 读取规则**：
- v1：header 16 → 读取 → `RecalculateNormals()`
- v2 + hasNormals：header 20 → 读取 → **跳过** `RecalculateNormals()`
- v2 + no normals：header 20 → 读取 → fallback `RecalculateNormals()`

### 7.2 Point Binary

```
[magic:4][version:4][point_count:4][flags:4]    // header 16 bytes
[float3 × point_count]                           // positions（必有）
[float3 × point_count]?                          // normals (if flags & NORMAL)
[float2 × point_count]?                          // UV (if flags & UV)
[uint32 × point_count]?                          // triIndex (if flags & TRI_INDEX)
[float × point_count]?                           // scale (if flags & SCALE)
[float4 × point_count]?                          // rotation quaternion (if flags & ROTATION)
```

### 7.3 Geometry Binary v2（远期）

| Chunk | 内容 |
|-------|------|
| `POINTS_F32` | 位置 |
| `FACE_OFFSETS` + `FACE_INDICES` | n-gon |
| `GROUP_DEFS` + `GROUP_MEMBERS` | 命名组 |
| `ATTR_DEFS` + `ATTR_PAYLOADS` | 可选 |
| `TRIANGULATION` | Sink 用 |

Cook hash **必须**包含 groups/attrs。

### 7.4 Unity 侧读取优化

从逐元素 `BitConverter.ToSingle × N` 优化为 `Span + unsafe` 指针 reinterpret 批量读取：

```csharp
fixed (byte* ptr = data)
{
    // header 校验不变
    // float* → Span<Vector3>，零逐元素调用
    Buffer.MemoryCopy(ptr + headerSize, dst, vertCount * 12, vertCount * 12);
}
```

**保留 BlockCopy 裁剪**（8MB → 精确大小），保护异步路径中 buffer 不被覆盖。

---

## 8. Unity 集成层

### 8.1 Runtime 层

| 文件 | 职责 |
|------|------|
| `PcgCookClient.cs` | localhost HTTP 请求与 cook 载荷解包 |
| `PcgNative.cs` | 保持现有调用方兼容的 C# 门面与协议常量 |
| `PcgGraphLoader.cs` | Graph JSON 加载 + 执行调用 |
| `PcgResultParser.cs` | Binary mesh/point 解析（v1/v2 兼容） |
| `PcgGraphComponent.cs` | 场景组件：GraphAsset 绑定 + Cook 调度 + 预览 |
| `PcgGraphCookCache.cs` | 整图 hash 缓存（T3 → per-node dirty） |
| `PcgGroupVisualizer.cs` | Scene View group 着色（Handles API） |
| `PcgScatterGpuInstancer.cs` | GPU 实例化散点渲染 |
| `PcgMeshBindingTable.cs` | bindingKey → Mesh 来源解析 |
| `PcgSplineResolver.cs` | Spline slot 上传 |
| `PcgHostTerrain.cs` | Host Terrain Surface DTO、Unity Terrain 双向绑定与重采样 |

Terrain 宿主契约、同步策略与 Unreal 预留接口见
[`host-terrain-surface.md`](host-terrain-surface.md)。

### 8.2 Editor 层 — Graph 编辑器

```
PcgGraphEditorWindow (菜单入口 + Toolbar)
    │
    ├── PcgGraphView (GraphView 子类)
    │   ├── LoadDocument / CommitState / WithUndo
    │   ├── PcgSceneEditContext (Scene 编辑模式真源)
    │   ├── TryGetNodeMeshStats (惰性解析 cook result JSON)
    │   └── selection → PcgNodeInspector
    │
    ├── PcgNodeInspector (节点属性面板)
    │   ├── manifest 驱动属性渲染
    │   ├── Group viewer 提示
    │   └── Geometry section (Points/Faces/Triangles)
    │
    ├── PcgCreateSplineSceneHandles (Scene View 交互)
    │   ├── 控制点拖拽 / 插入 / 删除
    │   ├── Catmull-Rom 预览
    │   ├── Group 高亮 (Handles API)
    │   └── Cook 通知链
    │
    └── PcgGraphEditorCookBridge (Graph → Scene 预览 Cook)
```

### 8.3 Cook 模式（对齐 Houdini/Blender）

| 模式 | 行为 | 默认 |
|------|------|------|
| `Manual` | 仅按钮 Run | Editor 默认 |
| `OnParameterChange` | 参数变更后 debounce 200ms | Component 推荐 |
| `OnMouseUp` | 滑块拖完松手才算 | Graph Inspector 推荐 |
| `EveryFrame` | 每帧 cook | **仅 Play Mode** |

**规则**：`EveryFrame` **禁止**在 Edit Mode 无节流运行（自动降级为 `OnParameterChange`）。

### 8.4 Preview Quality（对齐 Blender `Is Viewport`）

| 档位 | 适用 | 典型覆盖 |
|------|------|----------|
| `Preview` | Editor 视口、拖参 | Subdivide levels ≤ 1, Scatter count ≤ 128 |
| `Full` | Play、Bake、Manual Force Update | 图内原始参数 |

### 8.5 Scene View 编辑模式

```
PcgSceneEditContext (单一 mode 真源，定义在 PcgGraphView 上)
    │
    ├── Object 模式：不接管 Scene 输入
    ├── Component + SplineControlPoint：控制点编辑
    └── Component + Vertex/Edge/Face：(Phase 3，阻塞于 mesh authoring 真源定位)
```

**关键约束**：
- `evt.Use()` 会吃掉所有同帧后续 GUI 控件事件 → 用 `HandleUtility.AddDefaultControl`
- Scene View selection lock 三层防护：`AddDefaultControl` + `Selection.selectionChanged` + `Tools.current = None`
- 多窗口隔离：所有 Scene 逻辑带活动 graph/window token

### 8.6 Cook 后端（无 Unity native plugin）

Unity Editor 通过 localhost `pcg-server` 执行 C++（HTTP）。**不要**再把 `PcgCore` / `PcgFbxExporter` copy 到 `Plugins/`。

见 `docs/pcg-server.md`。Player 本期不链入 `PcgCore.lib`。

第三方图生 3D（Meshy / 后续 Tripo 等）说明与扩展清单见 `docs/third-party-image-to-3d.md`。

---

## 9. 构建与测试

### 9.1 构建

```bash
# Cook 后端（Unity 联调必需）
./scripts/build-pcg-server.sh
./scripts/run-pcg-server.sh

# 仅 pcg-core 算法测试（不 copy 到 Unity）
./scripts/build-pcg-core.sh --run-tests
```

```powershell
.\scripts\build-pcg-server.ps1 -Run
.\scripts\build-pcg-core.ps1 -RunTests
```

### 9.2 测试

#### CTest 矩阵


| Target | 阶段 | 类型 | 覆盖内容 |
|--------|------|------|----------|
| `test_data` | 基础 | 单元 | PcgMeshData 序列化 |
| `test_executor` | 基础 | 单元 | 图执行器 |
| `test_cook_cache` | 基础 | 单元 | Cook 缓存 |
| `test_phase41` | 4.1 | 集成 | UE 原语节点 |
| `test_phase42` | 4.2 | 集成 | 结构节点 |
| `test_phase43` | 4.3 | 集成 | Mesh 节点 |
| `test_phase44` | 4.4 | 集成 | Scatter |
| `test_phase45_spline` | 4.5 | 集成 | Spline + Sweep |
| `test_phase46` | 4.6 | 集成 | Geometry + Groups |
| `test_bridge_bevel` | 4.6b | 集成 | Bevel 拓扑（6 项指标 + 18 参数化） |
| `test_stone_arch_bridge` | 4.6 | 回归 | 拱桥场景 |
| `test_robust_predicates` | 4.7 B0a | 单元 | Shewchuk predicates |
| `test_tri_intersect` | 4.7 B0b | 单元 | 三角求交 + 共面 |
| `test_bvh` | 4.7 B1 | 单元 | BVH 查询 |
| `test_imesh` | 4.7 B1 | 单元 | IMesh 构建 + 分割 |
| `test_arrangement` | 4.7 B2 | 单元 | Winding + CSG |
| `test_boolean_output` | 4.7 B3 | 单元 | BMesh 输出 + groups |
| `test_boolean_binary` | 4.7 B3 | 序列化 | geometry binary round-trip |
| `test_boolean` | 4.7 B4 | 集成 | 程序化 mesh |
| `test_boolean_graph` | 4.7 B4 | API | `pcg_execute_graph_v7` |
| `test_boolean_bevel_chain` | 4.7 B4 | 下游链 | Boolean → Bevel |
| `test_spawn_mesh_binary` | 基础 | 单元 | Mesh binary round-trip |

```bash
# 运行全部测试
ctest --test-dir pcg-core/build --output-on-failure

# 运行特定测试
ctest --test-dir pcg-core/build -R 'test_bridge_bevel' --output-on-failure

# 运行 Bevel 相关回归
ctest --test-dir pcg-core/build -R 'test_bridge_bevel|test_phase43|test_phase45_spline|test_phase46|test_stone_arch_bridge' --output-on-failure
```

### 9.3 Bevel 拓扑验证（6 项不变量）

`test_bridge_bevel` 的 sedan body fixture 同时报告并断言：

| 指标 | 期望 | 含义 |
|------|------|------|
| `boundary_edge_count` | 0 | 无边界孔洞 |
| `edge_incidence` | 全 2 | manifold 边 |
| `degenerate_count` | 0 | 无退化三角形 |
| `duplicate_face_count` | 0 | 无重复面 |
| `manifold_winding_bad_count` | 0 | winding 一致 |
| `signed_volume` | > 0 | 法线朝外 |

### 9.4 本地持续验证

当前检出不包含 GitHub Actions 工作流。提交前请在本地运行：

```bash
./scripts/build-pcg-core.sh --run-tests
```

Windows PowerShell：

```powershell
.\scripts\build-pcg-core.ps1 -RunTests
```

### 9.5 Schema 校验

```bash
python3 scripts/validate-manifest.py
python3 .agents/skills/shared/pcg-scripts/validate_pcg.py examples/graphs/bridge-demo.pcg
```

---

## 10. 扩展开发指南

### 10.1 新增一个 C++ 节点

以 `MyCustomNode` 为例：

#### Step 1: 实现 Element

```cpp
// pcg-core/src/elements/my_custom_elements.cpp
#include "pcg_element.hpp"

class MyCustomElement : public IPcgElement {
public:
    PcgResultCode execute(PcgContext& ctx) override {
        // 1. 从上游获取输入
        auto* input = ctx.inputs.find_point("in");
        if (!input) return PCG_ERR_EXECUTION;

        // 2. 算法处理
        PcgPointData output;
        // ... your algorithm ...

        // 3. 输出
        ctx.outputs.add_point("out", std::move(output));
        return PCG_OK;
    }

    uint64_t cook_hash(const PcgNode& node) const override {
        uint64_t h = 0;
        // hash 所有影响输出的参数
        hash_combine(h, node.data.value("count", 0));
        hash_combine(h, node.data.value("seed", 0));
        return h;
    }
};
```

#### Step 2: 注册

```cpp
// pcg-core/src/elements/element_registry.cpp
registry.emplace("MyCustomNode", []() { return std::make_unique<MyCustomElement>(); });
```

#### Step 3: Manifest

```json
// schema/node-manifest.json
{
  "type": "MyCustomNode",
  "displayName": "My Custom Node",
  "category": "Generation",
  "inputs": [{ "id": "in", "label": "Points", "pinType": "SpatialPoint" }],
  "outputs": [{ "id": "out", "label": "Points", "pinType": "SpatialPoint" }],
  "properties": {
    "count": { "type": "integer", "default": 100 },
    "seed": { "type": "integer", "default": 0 }
  }
}
```

#### Step 4: 同步到 Unity

```bash
# 拷贝 manifest 到 Unity 两处
# schema/node-manifest.json → Unity/Assets/PcgPlugin/Editor/Graph/node-manifest.json
#                             → Unity/Assets/PcgPlugin/Resources/node-manifest.json
```

#### Step 5: 测试

```cpp
// pcg-core/tests/test_my_custom.cpp
#include "pcg_api.h"
#include <cassert>

int main() {
    const char* graph = R"({
        "version": "2.0",
        "nodes": [
            { "id": "n1", "type": "CreatePoints", "data": { "count": 10 } },
            { "id": "n2", "type": "MyCustomNode", "data": { "count": 5 } },
            { "id": "n3", "type": "Output", "data": {} }
        ],
        "edges": [
            { "source": "n1", "target": "n2" },
            { "source": "n2", "target": "n3" }
        ]
    })";

    char err[256];
    auto rc = pcg_validate_graph(graph, err, sizeof(err));
    assert(rc == PCG_OK);
    return 0;
}
```

```cmake
# pcg-core/CMakeLists.txt
add_executable(test_my_custom tests/test_my_custom.cpp)
target_link_libraries(test_my_custom pcg_core)
add_test(NAME test_my_custom COMMAND test_my_custom)
```

### 10.2 新增一个 Geometry 算子

如果节点需要操作 `PcgGeometry`（保留 groups）：

```cpp
PcgResultCode execute(PcgContext& ctx) override {
    // 获取 geometry 输入（而非 mesh 输入）
    auto input_geo = get_geometry_input(ctx, "in");
    if (!input_geo) return PCG_ERR_EXECUTION;

    // 算法操作
    PcgGeometry result;
    result.points_mut() = input_geo->points();
    // ... 修改 geometry ...
    // 保留或传播 groups

    // 输出 geometry（而非 mesh）
    emit_geometry(ctx, "out", std::move(result));
    return PCG_OK;
}
```

**关键**：全链路必须保持 `PcgGeometry`（含 GroupTable），不能中途降级为 `PcgMeshData`。

### 10.3 新增 API 版本

当需要新的 slot 类型或输出通道时：

```cpp
// pcg_api.h — additive
PCG_API PcgResultCode pcg_execute_graph_v8(
    const char* json, int seed,
    /* ... 既有 slots ... */
    const PcgNewSlot* new_slots, int new_count,   // 新增 slot
    /* ... 既有 outputs ... */
    char* err_buf, int err_buf_size
);
```

**规则**：新版本在 `new_count == 0` 时退化为上一版本。

### 10.4 添加示例图

```bash
# 1. 创建 .pcg 文件（Houdini 自上而下布局，ROW_STEP_Y=160）
# examples/graphs/my-demo.pcg

# 2. 校验
python3 .agents/skills/shared/pcg-scripts/validate_pcg.py examples/graphs/my-demo.pcg

# 3. 如需随 Unity 示例分发，将副本放入 Assets/Samples/PCG-AI，保留对应 .meta/GUID
# 一般情况下只保留 examples/graphs/ 中的仓库真源，Unity 直接从文件运行即可。
```

---

## 11. 关键工程决策记录

### 11.1 架构决策

| 日期 | 决策 | 理由 |
|------|------|------|
| 2026-07-03 | 直接对齐 UE PCG 节点模型，不自研 Block 框架 | 工业标准、多端友好、有文档可对照 |
| 2026-07-03 | Unity GraphView 优先，Web 后补 | Scene 内即时预览；无需 Bridge/串流 |
| 2026-07-03 | 三层契约（graph / manifest / result）跨端共用 | 编辑器可互换；引擎绑定进 Metadata |
| 2026-07-09 | Canonical 类型 = `PcgGeometry`（非扩展 `PcgMeshData`） | 避免三角汤丢语义；与 Houdini `GU_Detail` 对齐 |
| 2026-07-09 | BMesh 保留为编辑内核，非传输格式 | 已有 bevel 投资；geometry→BMesh 单向富化 |

### 11.2 算法决策

| 日期 | 决策 | 理由 |
|------|------|------|
| 2026-07-09 | Bevel face 引用模型从 triangle_indices 改为 BMesh face index | 三角汤 tri index 无法表达 n-gon 面身份 |
| 2026-07-09 | Bevel 默认 `excludeUnshared=true` | 对齐 Houdini PolyBevel 工业默认 |
| 2026-07-11 | `fix_winding()` BFS 传播 + volume 校正 | 逐边 greedy flip O(N²) 不可接受 |
| 2026-07-11 | `add_oriented_polygon` 法线用 sum of all fan normals | cap 平面首 3 顶点共面时 cross ≈ 0 |
| 2026-07-10 | Boolean 拒绝 Fast (Float) 路径 | 浮点拓扑不可靠 |
| 2026-07-10 | Boolean 用 Shewchuk adaptive predicates（非 GMP） | 无新外部依赖；可证明 error bound |
| 2026-07-10 | Boolean 禁止 ray casting fallback | 与精确路径矛盾；hard fail |
| 2026-07-12 | SweepAlongSpline 不创建 edge group | edge group 导致 bevel 选边冲突 |
| 2026-07-12 | Split Normal 只在 Sink 执行，不改 bevel 核心 | bevel 依赖共享顶点索引对齐 |
| 2026-07-12 | Split Normal 邻接从 polygon loop 构建，非 triangle edge | fan diagonal 不是真实几何边 |
| 2026-07-15 | PcgGeometry 必须原生支持所有属性通道（color/UV/normal/materialName 等） | 缺通道导致 geometry→mesh→geometry round-trip，三角化 n-gon 面破坏拓扑；正确做法是扩展 PcgGeometry 字段 |
| 2026-07-15 | 中间节点禁止 `emit_mesh()`，必须 `emit_geometry()` | emit_mesh 输出三角化网格，下游 bevel/subdivide/boolean 丢失 n-gon 信息 |
| 2026-07-15 | 三角化只允许在 Sink / binary / 算法内部计算 | 中间节点三角化结果传播到下游会破坏 polygon 拓扑链 |

### 11.3 工程决策

| 日期 | 决策 | 理由 |
|------|------|------|
| 2026-07-07 | `.pcg` 只存 bindingKey，场景引用进 Component | 对齐 UE PCG；可移植 |
| 2026-07-07 | `EveryFrame` 仅 Play Mode | 工业软件不每帧全链重算 |
| 2026-07-07 | Preview/Full 两档质量 | 重链视口走 Preview |
| 2026-07-13 | per-node mesh stats 通过 cook result JSON 传递 | `node_stats` 与 `groups` 共存于同一 JSON object |
| 2026-07-13 | `PcgGraphComponent.LastCookResultJson` 是 runtime→editor 桥 | static 字段，`#if UNITY_EDITOR` 保护 |

---

## 12. 术语表

| 术语 | 含义 | 对标 |
|------|------|------|
| PCG | Procedural Content Generation | UE PCG |
| Graph JSON | 编辑器无关的图数据契约（`.pcg` 文件） | UE PCG Graph Asset |
| Element | 节点执行逻辑（`IPcgElement` 子类） | UE `IPCGElement` |
| DataCollection | 节点间数据总线 | UE `PCGDataCollection` |
| PcgGeometry | Canonical polygon mesh（polygon + groups + attrs） | Houdini `GU_Detail` |
| GroupTable | 命名组系统（point/edge/face 域） | Houdini Group |
| BMesh | 半边网格编辑内核 | Blender BMesh |
| BevelOutput | Bevel 中间结果（含 OutputMesh + VMesh） | Blender `BevelOutput` |
| BoundVert | Bevel 边界顶点（profile chain 节点） | Blender `BoundVert` |
| EdgeHalf | 半边（记录 fprev/fnext 面引用） | Blender `EdgeHalf` |
| VMesh | Vertex Mesh（圆角顶点环） | Blender `VMesh` |
| IMesh | Integer Mesh（Boolean 精确计算中间结构） | Blender `IMesh` |
| Winding Number | 缠绕数（CSG inside/outside 分类） | Blender Exact Solver |
| PWN | Piecewise Winding Number（非流形容忍条件） | — |
| Cook | 图执行（从 Houdini 借用） | Houdini Cooking |
| Cook Hash | 参数 + 输入组合 hash（缓存键） | Houdini Lock |
| bindingKey | 抽象 mesh 引用键（不绑场景路径） | UE PCG binding |
| Sink | 图输出节点 / 输出阶段 | Houdini Output |
| polygon triangulate | n-gon → triangles（凸面稳定 fan；凹面 constrained Delaunay） | Blender/Houdini polygon tessellation |
| detriangulate | triangles → n-gon（仅重建同一输入 polygon；可限制为未切割面） | Houdini Boolean Detriangulate |
| edge_key | 稳定边键 `minmax(v0, v1)` → int64 | — |
| face_origins | triangle → BMesh face index 映射 | — |
| unshared edge | 边界边（仅 1 个邻接面） | Houdini Unshared Edges |
| cusp angle | 硬边角度阈值 | Houdini Facet SOP |
| ShadeMode | 着色策略（Auto/Smooth/Flat） | Blender Shade Smooth/Flat |
| PreviewQuality | 预览质量档（Preview/Full） | Blender `Is Viewport` |

---

## 附录 A：Git 提交历史摘要

项目从 2026-07-03 首次提交到 2026-07-13 共 72 次提交，关键演进：

| 时间段 | 主题 | 关键 commits |
|--------|------|-------------|
| 07-03 | 项目初始化 + Phase 4.0/4.1 UE PCG 核心移植 | `523682e` → `cebbbe8` |
| 07-04 | Graph JSON v2 + node-manifest + 13 UE 节点 | `5616506` → `eb5cab7` |
| 07-05 | 图执行改进 + 异步 cook + 取消 | `5a419e2` → `172006c` |
| 07-05 | Point binary + BMesh 几何特征 | `c2d5db9` → `f0ff58a` |
| 07-06 | Scatter + Mesh Noise + ImageTexture | `9b55430` → `74d93d7` |
| 07-07 | Mesh Surface Scatter (4.4a) + Binary 优化 | `5b6393a` → `76c2ce1` |
| 07-08 | Spline + Sweep + 桥梁 demo | `527c4f9` → `4263a46` |
| 07-09 | PcgGeometry + Groups (4.6) + Bevel BMesh-Native (4.6b) | `fa38549` → `8751e13` |
| 07-10 | Boolean Mesh (4.7) + Boolean demo | `1987904` → `2c8997f` |
| 07-11 | Bevel terminal cap 修复 + Scene View UI + Spline handles | `f7289d9` → `2cce2e0` |
| 07-12 | Group pipeline 传播 + Sweep group 对齐 Houdini + Bevel 修复 | `a5dfc46` → `eb6b129` |
| 07-13 | Info panel mesh stats + multi-face group + bevel fix | `263557b` → `2a84559` |

## 附录 B：关键文件索引

### C++ 核心

| 文件 | 职责 |
|------|------|
| `pcg-core/include/pcg_api.h` | 对外 C API（v1→v10） |
| `pcg-core/src/graph_executor.cpp` | 图执行 + Sink 输出 + group stats + node stats |
| `pcg-core/src/graph_parser.cpp` | Graph JSON 解析 |
| `pcg-core/src/cook_hash.cpp` | 参数 hash + 脏缓存 |
| `pcg-core/src/data/pcg_geometry.hpp/.cpp` | PcgGeometry + triangulate + merge |
| `pcg-core/src/data/pcg_mesh_data.hpp/.cpp` | PcgMeshData + normals |
| `pcg-core/src/data/pcg_mesh_binary.hpp/.cpp` | Mesh binary v1/v2 读写 |
| `pcg-core/src/data/pcg_point_binary.hpp/.cpp` | Point binary 读写 |
| `pcg-core/src/data/pcg_geometry_binary.hpp/.cpp` | Geometry binary v2 读写 |
| `pcg-core/src/geometry/bmesh.hpp/.cpp` | BMesh 半边网格 |
| `pcg-core/src/geometry/group_table.hpp/.cpp` | GroupTable 命名组 |
| `pcg-core/src/geometry/sweep_geometry.cpp` | Sweep 扫掠几何 |
| `pcg-core/src/geometry/robust_predicates.hpp/.cpp` | Shewchuk 自适应谓词 |
| `pcg-core/src/geometry/imesh.hpp/.cpp` | IMesh（Boolean 中间结构） |
| `pcg-core/src/geometry/arrangement.hpp/.cpp` | Winding number + CSG |
| `pcg-core/src/geometry/bvh.hpp/.cpp` | AABB tree |
| `pcg-core/src/geometry/tri_split.hpp/.cpp` | 三角分割 + weld |
| `pcg-core/src/geometry/coplanar_partition.hpp/.cpp` | 共面 2D arrangement |
| `pcg-core/src/geometry/boolean_output.hpp/.cpp` | IMesh → BMesh → PcgGeometry |
| `pcg-core/src/elements/bevel_blender.hpp/.cpp` | Bevel（Blender 对齐） |
| `pcg-core/src/elements/mesh_algorithms.hpp/.cpp` | bevel_geometry + transform_geometry |
| `pcg-core/src/elements/geometry_algorithms.hpp/.cpp` | group_create + from_face_groups |
| `pcg-core/src/elements/boolean_elements.cpp` | BooleanMesh 节点 |
| `pcg-core/src/elements/element_registry.cpp` | 节点注册表 |

### Unity Runtime

| 文件 | 职责 |
|------|------|
| `PcgCookClient.cs` | HTTP cook 请求与结果容器解包 |
| `PcgNative.cs` | 兼容门面 + 协议常量 |
| `PcgGraphLoader.cs` | 图加载 + 执行 |
| `PcgResultParser.cs` | Binary 解析（v1/v2） |
| `PcgGraphComponent.cs` | 场景组件 + Cook 调度 |
| `PcgGraphCookCache.cs` | 整图 hash 缓存 |
| `PcgGroupVisualizer.cs` | Scene group 着色 |
| `PcgScatterGpuInstancer.cs` | GPU 实例化 |

### Unity Editor

| 文件 | 职责 |
|------|------|
| `Graph/PcgGraphEditorWindow.cs` | Graph 窗口 + Toolbar |
| `Graph/PcgGraphView.cs` | GraphView + SceneEditContext |
| `Graph/PcgNodeInspector.cs` | 节点属性面板 |
| `Graph/PcgCreateSplineSceneHandles.cs` | Scene 控制点交互 |
| `PcgGraphComponentEditor.cs` | Component Inspector |
| `PcgGraphExecutionBridge.cs` | Editor → C++ 执行桥 |
| `PcgMeshExecutionBridge.cs` | Mesh slot 上传桥 |

---

*本手册基于 2026-07-13 代码库快照生成，随主分支演进同步更新。*
