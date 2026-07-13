# Code Review [S2026071301]

> Mode: Local | Project: generic (C++/C#)
> Round: 1 | Conclusion: needs_changes
> Last Review SHA: 4e6f39d
> Branch: dev/0722-01 → main

---

## 总结

### 功能概述

本次变更为 PCG-AI 引擎新增 **Split Normals（顶点法线分割）** 功能，使 PCG 生成的网格能够按 Auto/Smooth/Flat 三种模式计算 per-vertex normals，替代此前统一由 Unity 侧 `RecalculateNormals()` 处理的方式。核心算法基于 polygon corner topology → Union-Find 软/硬边分类 → island-based vertex split → area-weighted normal accumulation。

同时将 `bevel_geometry` 返回类型从 `PcgMeshData` 升级为 `PcgGeometry`（保留拓扑 + 面组传播），升级 mesh binary 格式至 v2（含 normals + flags），并在 Unity 编辑器内新增 Group Viewer（Vertex/Edge/Face 可视化）和 per-node geometry stats 显示。

- 涉及模块：pcg-core (data/elements/infra)、Unity PcgPlugin (editor/runtime)、tests、demo PCG 文件
- 核心动机：让 PCG 管线控制法线分割策略，而非依赖 Unity 后处理
- 关键技术决策：Newell's method 计算面法线、cusp angle + group boundary 双重硬边判定、binary v2 向后兼容 v1

### 变更要点

- **Split Normals 核心（pcg_geometry.cpp/hpp）**：新增 `compute_split_normals()` (~200 行)，使用 Union-Find 对 polygon corner 进行 soft/hard edge 分类，生成 split vertex mesh + per-vertex normals。新增 `triangulate_geometry_shared()` 用于拓扑检查场景。
- **Mesh Binary v2（pcg_mesh_binary.cpp/hpp）**：Header 从 16→20 字节（新增 flags 字段），支持 normals 序列化。Read path 支持 v1/v2 双版本，v2 新增 `index_count % 3` 和 `index >= vertex_count` 边界检查。
- **Bevel 返回类型升级（mesh_algorithms.cpp/hpp, bevel_blender.cpp/hpp）**：`bevel_geometry` 返回 `PcgGeometry`，保留面组拓扑。`bevel_mesh_blender` 新增 `out_geometry` 参数 + `face_origins` 追踪，将 BMesh 面组传播到输出 geometry。
- **Graph Executor（graph_executor.cpp）**：`build_group_stats()` 展开 face group members 到 constituent triangles（fan triangulation 映射）。新增 `build_node_stats()` 收集 per-node point/face/triangle 计数。Sink 输出从 `triangulate_geometry` 改为 `compute_split_normals`。
- **Unity Editor Group Viewer（PcgCreateSplineSceneHandles.cs）**：新增 ~270 行，支持 Vertex/Edge/Face group 高亮可视化，SceneView overlay 选择面板。
- **Unity Runtime（PcgNative.cs, PcgResultParser.cs）**：C# 侧 v2 binary 解析，动态 header size 计算，normals 直接赋值给 Mesh（跳过 `RecalculateNormals()`）。
- **测试（test_split_normals.cpp）**：新增 16 个单元测试，覆盖 split normals 算法、binary v2 round-trip、v1 向后兼容、degenerate face、detail 传播。
- **Demo/Schema**：所有 PCG 文件移除 `profile_corner` edge group 引用，添加 `shadeMode`/`cuspAngle` 属性。移除 BevelMesh 默认 `excludeGroups: "cap_start,cap_end"`。

### 审查评价

整体架构方向正确，Split normals 与 PcgGeometry 拓扑保留的设计能解决 Unity 侧统一 `RecalculateNormals()` 无法表达硬边/组边界的问题。测试已覆盖算法和内部 binary round-trip，但公开 C ABI、Points JSON 透传、多组件 editor 状态隔离仍存在会影响运行结果的缺口。

主要风险集中在 native/C# 边界：mesh binary v2 实际 payload 已变大，但公开 sizing API 仍按 v1 返回；Points sink 已写入 `node_stats` JSON，但 C# Points 分支没有传回；Node Info 统计使用全局静态 JSON，多个 graph/preview 组件会互相污染。

统计：🔴 Critical: 0 | 🟠 Major: 3 | 🟡 Minor: 4 | 🔵 Suggestion: 1
结论：needs_changes — 存在 3 个 Major 问题需修复后合并。

审查覆盖：
- [x] Pass 1: 设计审查
- [x] Pass 2: 实现审查
- [x] Pass 3: 一致性审查
- [x] Pass 4: 安全验证

---

## Review

### F1 [🟠 Major] 公开 mesh binary sizing API 仍按 v1 返回，v2 normals 输出会触发 buffer too small
- Pass: 1 - 设计审查
- File: pcg-core/src/pcg_core.cpp:961
- Category: C API / Binary ABI
- Status: open
- Verified: ✅

`write_mesh_binary()` 现在实际写 v2 payload：20 字节 header，且 `mesh.has_normals()` 时追加 `vertex_count * 12` 字节 normals。`pcg_execute_graph_v6()` 的 geometry sink 又统一通过 `compute_split_normals()` 生成带 normals 的 mesh，因此外部宿主如果按公开的 `pcg_mesh_binary_size_for_counts()` 预分配 buffer，会得到仍基于 16 字节 v1 header、且不包含 normals 的大小。随后执行 graph 时，native 写 mesh binary 会因为 buffer 小于实际 v2 payload 而失败。

证据链：公开 header 仍声明 v1/16 字节（`pcg-core/include/pcg_api.h:59`），公开 size API 仍按 v1 计算（`pcg-core/src/pcg_core.cpp:968`），实际 writer 按 v2+flags+normals 计算（`pcg-core/src/data/pcg_mesh_binary.cpp:23`、`pcg-core/src/data/pcg_mesh_binary.cpp:45`）。

**建议**：更新公开 ABI 常量和 sizing API。至少让旧 `pcg_mesh_binary_size_for_counts()` 返回 v2 worst-case（含 normals），或新增带 flags/has_normals 的 size API，并同步 Unity/外部宿主使用方式。

### F2 [🟠 Major] Points 结果丢失 result.Json，Node Info 无法显示点/散点图的 node_stats
- Pass: 2 - 实现审查
- File: Unity/Assets/PcgPlugin/Runtime/PcgNative.cs:529
- Category: Interop 数据丢失
- Status: open
- Verified: ✅

C++ executor 在 Points sink 成功分支已经把 `node_stats` 写入 JSON（`pcg-core/src/graph_executor.cpp` 的 Points 返回路径），但 C# `BuildSuccessResult()` 的 `PcgExecuteKind.Points` 分支构造 `PcgGraphExecuteResult` 时没有设置 `Json`。`PcgGraphComponent.ApplyExecutionResult()` 又把 `result.Json` 复制到 `LastCookResultJson`，GraphView/NodeInfo 只从这个 JSON 解析 node stats。因此只要 graph 输出 Points，新增的 per-node geometry stats 会稳定不可见。

同一方法的 Mesh 分支已设置 `Json = ReadNullTerminatedUtf8(jsonBuf)`，Points 分支缺失该字段，属于跨 native → C# 数据链路不一致。

**建议**：在 Points 分支补 `Json = ReadNullTerminatedUtf8(jsonBuf)`，并增加一个 Points 输出 graph 的 editor/runtime 回归测试，验证 `node_stats` 能被 UI 解析。

### F3 [🟠 Major] Node Info 使用全局 LastCookResultJson，多 graph/多组件 cook 会串数据显示错误 stats
- Pass: 2 - 实现审查
- File: Unity/Assets/PcgPlugin/Runtime/PcgGraphComponent.cs:139
- Category: Editor 状态一致性
- Status: open
- Verified: ✅

`LastCookResultJson` 是 `static string`，任意 `PcgGraphComponent` cook 成功都会覆盖它（`PcgGraphComponent.cs:530`）。GraphView 的 `TryGetNodeMeshStats()` 不按当前 editor window、graph asset 或 preview anchor 过滤，只读取这个全局值（`Unity/Assets/PcgPlugin/Editor/Graph/PcgGraphView.cs:119`）。当场景里有多个 PCG 组件、或一个 graph editor 打开时另一个组件触发 preview cook，Node Info 面板会显示另一个 graph 的 node stats，严重时同名 node id 还会给出看似合理但错误的统计。

**建议**：把 result JSON 绑定到当前 preview anchor / `PcgGraphComponent` 实例，或按 graph asset path / instance id 建立映射；GraphView 查询当前窗口关联组件的 stats，避免全局静态状态污染。

### F4 [🟡 Minor] BevelMesh 移除 exclude_groups 默认值改变现有 graph 行为
- Pass: 1 - 设计审查
- File: pcg-core/src/elements/mesh_elements.cpp:111
- Category: 破坏性变更
- Status: open
- Verified: ✅

`exclude_groups` 的默认值 `{"cap_start", "cap_end"}` 被移除。此前未显式设置 `excludeGroups` 的 graph 会自动排除 cap_start/cap_end 边，现在这些边会被包含在 bevel 中。Demo 文件已同步更新（`excludeGroups: ""`），但用户自定义的现有 graph 如果依赖此默认行为，升级后 bevel 结果会变化。

**建议**：在 CHANGELOG 或 migration note 中注明此行为变更。如果用户 graph 未设置 `excludeGroups`，建议显式添加 `"cap_start,cap_end"` 以保持原有行为。

### F5 [🟡 Minor] read_u32 bounds check 不考虑 offset（pre-existing）
- Pass: 2 - 实现审查
- File: pcg-core/src/data/pcg_mesh_binary.cpp:13-16
- Category: 内存安全
- Status: open
- Verified: ✅

`read_u32(src, remaining, out)` 检查 `remaining < 4`，但调用方传入 `buffer_size` 作为 `remaining`，不考虑 `src` 相对 buffer 起始的 offset。例如 `read_u32(bytes + 12, buffer_size, index_count)` 在 `buffer_size >= 4 && buffer_size < 16` 时会越界读取。v2 path 有显式 `buffer_size < 20` 检查兜底，v1 path 依赖后续的 `required` 计算，但 header 字段读取发生在 `required` 检查之前。

此问题为 pre-existing（旧代码 `int remaining = buffer_size` 同样存在），非本次 MR 引入。实际触发概率极低（需要 buffer 恰好 4-15 字节且 magic 匹配）。

**建议**：修复 `read_u32` 调用方传入 `buffer_size - offset` 而非 `buffer_size`，或将函数签名改为 `read_u32(bytes, offset, buffer_size, out)`。

### F6 [🟡 Minor] v1 read path 缺少 index bounds check
- Pass: 3 - 一致性审查
- File: pcg-core/src/data/pcg_mesh_binary.cpp:108-130
- Category: 一致性
- Status: open
- Verified: ✅

v2 read path 新增了 `if (index >= vertex_count) return false;` 和 `if (index_count % 3 != 0) return false;` 检查，v1 path 均缺失。虽然 v1 数据由同代码生成（理论可信），但对于从外部加载的 v1 数据（如旧版本保存的文件），缺少 index bounds check 可能导致 out-of-range 三角形索引。

**建议**：在 v1 path 补充相同的 bounds check 以保持一致性。

### F7 [🟡 Minor] compute_split_normals fallback normal 搜索为 O(n²)
- Pass: 2 - 实现审查
- File: pcg-core/src/data/pcg_geometry.cpp:230-250
- Category: 性能
- Status: open
- Verified: ✅

当 accumulated normal 长度为零（degenerate vertex）时，fallback 路径遍历所有 `triangle_corners` 查找匹配的 render vertex，再取其 face normal。复杂度为 O(vertices × triangles × 3)。对于大型 mesh 且大量 degenerate vertex 的情况（如大量零面积三角形），性能会显著下降。

**建议**：在 `get_render_vertex` lambda 中记录 `render_vertex_index → first_face_index` 反向映射，将 fallback 搜索降为 O(1)。

### F8 [🔵 Suggestion] C# edge group key 解码使用脆弱的整数除法
- Pass: 2 - 实现审查
- File: Unity/Assets/PcgPlugin/Editor/Graph/PcgCreateSplineSceneHandles.cs:1680-1681
- Category: 健壮性
- Status: open
- Verified: ✅

```csharp
int a = (int)((long)key / 1000000);
int b = (int)((long)key % 1000000);
```

Edge key 编码为 `a * 1000000 + b`，当 vertex index > 999999 时解码错误。且 `GroupJsonEntry.members` 为 `int[]`，若 C++ 侧 edge key 为 `int64_t` 且超过 `int.MaxValue`，JSON 反序列化会截断。

当前 sweep_geometry.cpp 已移除 edge group 赋值，此路径仅影响用户自定义 edge group。对于典型 PCG mesh（< 100K vertices）无影响。

**建议**：考虑使用 `long[]` members 或字符串编码 `"a:b"` 替代整数编码。
