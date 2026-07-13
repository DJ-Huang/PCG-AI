# 审查上下文 [Local] [S2026071301]

## 变更信息
- Source: dev/0722-01 → Target: main
- 作者: DJ-Huang
- 规模: 45 文件, 1802 insertions / 169 deletions (超大, 分批审查)
- 8 commits: split normals + shading params, node geometry stats, bevel_geometry fix, group viewer, etc.

## 变更摘要
本次变更为 PCG-AI 引擎新增 **Split Normals（顶点法线分割）** 功能，支持 Auto/Smooth/Flat 三种着色模式，基于 cusp angle + face group boundary 进行硬边分类。同时将 `bevel_geometry` 返回类型从 `PcgMeshData` 改为 `PcgGeometry`（保留拓扑+面组），升级 mesh binary 格式至 v2（含 normals），并新增 Unity 编辑器内的 Group Viewer（Vertex/Edge/Face 可视化）和 per-node geometry stats 显示。

## 文件分类
| 模块 | 文件 | 关键变更 |
|------|------|----------|
| C++ Data | pcg_geometry.cpp/hpp | 新增 `compute_split_normals()`, `triangulate_geometry_shared()`, ShadeMode, NormalComputeOptions |
| C++ Data | pcg_mesh_binary.cpp/hpp | Binary v1→v2 迁移, flags 字段, normals 序列化, v1 backward compat |
| C++ Data | pcg_mesh_data.cpp/hpp | normals 存储 + JSON 序列化 |
| C++ Elements | bevel_blender.cpp/hpp | face_origins 追踪, out_geometry 参数, face group 传播 |
| C++ Elements | mesh_algorithms.cpp/hpp | bevel_geometry 返回 PcgGeometry, 新增 transform_geometry() |
| C++ Elements | mesh_elements.cpp | BevelMesh emit_geometry, 移除 exclude_groups 默认值 |
| C++ Elements | geometry_algorithms.cpp/hpp | from_face_group → from_face_groups (vector) |
| C++ Elements | spline_mesh_elements.cpp | SweepAlongSpline shade mode, TransformMesh geometry, MergeMesh geometry merge |
| C++ Elements | sweep_geometry.cpp | 移除 seam/profile_corner edge group 赋值 |
| C++ Infra | graph_executor.cpp | build_group_stats face→tri 展开, build_node_stats, sink 用 compute_split_normals |
| C++ Infra | cook_hash.cpp | hash 含 shade_mode + cusp_angle |
| C# Editor | PcgCreateSplineSceneHandles.cs | Group Viewer overlay (~270 行) |
| C# Editor | PcgGraphView.cs, PcgNodeInfoPanel.cs | Node mesh stats 解析与显示 |
| C# Runtime | PcgNative.cs, PcgResultParser.cs | V2 binary 解析 + normals |
| C# Runtime | PcgGraphComponent.cs | LastCookResultJson, auto-add GroupVisualizer |
| Tests | test_split_normals.cpp (新增 16 tests) | Split normals + binary v2 round-trip + v1 compat |
| Tests | test_bridge_bevel.cpp, test_phase46.cpp | 适配 PcgGeometry 返回类型, weld_by_position |
| Config | *.pcg, schema/node-manifest.json | shadeMode/cuspAngle 属性, 移除 profile_corner 引用 |

## 关键实体与依赖
- `compute_split_normals()` ← Union-Find + Newell's method + cusp angle classification
- `bevel_geometry()` 返回类型变更 → 所有调用方已适配 (mesh_elements, tests)
- Binary v2 → C++ write/read + C# PcgNative/PcgResultParser 全链路适配
- `build_group_stats()` face→triangle 展开 → 依赖 fan triangulation 顺序一致性

## 影响范围
- 所有 PcgGeometry 输出节点的渲染网格拓扑变更（split vertices）
- BevelMesh 默认行为变更（不再默认排除 cap_start/cap_end）
- sweep 生成的 geometry 不再包含 seam/profile_corner edge groups
- Binary 格式版本升级，v1 向后兼容

## 知识检索结果
- rule_search `Rules/agents/pcg-ai.md`: PCG-AI 约定要求 `PcgGeometry` + `GroupTable` 作为 canonical 传输；建模算子不写死三角汤；Sweep/Bevel 需保留命名组与拓扑语义。
- rule_search `Rules/agents/pcg-ai.md`: C++ 注册/行为变更需同步 CMake、manifest、tests、Unity dylib；本次已覆盖 CMake、manifest、测试与 Unity plugin binary。
- vault_search `pit-bevel-edge-strip-connection`: Bevel edge strip / face ownership 类问题需验证闭合流形与 BMesh 拓扑来源，本次重点检查了 `bevel_blender.cpp` 的 face group/geometry 传播。
- vault_search `pit-boolean-fix-wrong-pipeline-phase`: mesh pipeline 类问题应优先用 `bad_edges` / topology 强指标验证，而不是仅看体积或视觉结果。
- vault_search `cpt-agent-complex-bug-playbook`: 复杂几何变更需明确阶段归因和强指标；本次将 C++ ABI、binary serialization、Unity interop 状态作为重点风险面。

## 审查重点
- 公开 C ABI 与内部 mesh binary v2 格式是否一致。
- `node_stats` 从 C++ executor 到 Unity Node Info 的传递是否完整。
- `PcgGeometry` face/edge/point group 到 Unity Group Viewer 的数据模型是否稳定。
- split normals 与 bevel/sweep 拓扑传播是否保留面组与 fan triangulation 映射。
- 新测试是否覆盖外部宿主真实调用路径，而不只覆盖内部 helper。
