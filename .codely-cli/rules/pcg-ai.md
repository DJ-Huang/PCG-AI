---
description: PCG-AI 项目级开发规则（C++ 注册、Manifest 同步、几何约定、写码前检索）
type: project-rule
domain: pcg-ai
---

# PCG-AI 开发与 Unity 联调

**触发**：`pcg-core` C++、新增/改节点、`schema/node-manifest.json`、`.pcg` 图、`Unity/Assets/PcgPlugin`、Phase 4.6 geometry/groups、Unity 报 `UnknownNode`。

**工程路径**：`PCG-AI` 仓库（`pcg-core/`、`schema/`、`Unity/Assets/PcgPlugin/`、`examples/`）。

## 新节点或 C++ 行为变更：两步缺一不可

| 步骤 | 命令（仓库根） | 作用 |
|------|----------------|------|
| **C++ 注册** | `scripts/build-pcg-core.sh --copy-to-unity` | `register_builtin_elements`、`pcg_validate_graph`、图执行；更新 `Plugins/macOS/libPcgCore.dylib` |
| **Manifest** | `scripts/sync-manifest.sh` | `schema/node-manifest.json` → Unity `Editor/Graph/` + `Resources/`；GraphView 节点面板与 Inspector 字段 |

- 只改 manifest、不拷贝 dylib → Unity **`UnknownNode`**
- 只改 C++、不同步 manifest → 编辑器无节点定义 / 参数默认值缺失
- macOS 改 dylib 后需 **重启 Unity Editor** 以重新加载原生库
- **只 `cmake --build` 不 `--copy-to-unity` → Unity 仍用旧 dylib**：`pcg-core/build/libPcgCore.dylib` 与 `Unity/Assets/PcgPlugin/Plugins/macOS/libPcgCore.dylib` 是两份独立副本，P/Invoke 加载后者。改完 C++ 必须用 `build-pcg-core.sh --copy-to-unity` 同步，否则 Unity 渲染结果不变（症状：修了代码但效果无变化）

可选：`scripts/build-pcg-core.sh --copy-to-unity --run-tests`

## 写码前检索

1. `rule_search`：`pcg-ai`、节点名、geometry、group、bevel、sweep
2. `vault_search`：踩坑与 Phase 记录（如 bevel、triangle soup、UnknownNode、boolean）
3. 复杂 bug / 破面 / 多轮未解：先读 [[cpt-agent-complex-bug-playbook]]（层归因 + 阶段归因 + 强指标）
4. `Read` `schema/node-manifest.json`（pin / 属性 SSOT）
5. 编 `.pcg`：`.cursor/skills/pcg-graph-authoring/SKILL.md`；校验 `python3 .cursor/skills/pcg-graph-authoring/scripts/validate_pcg.py <graph.pcg>`

## Phase 4.6+ 几何约定（摘要）

- **Canonical 传输**：`PcgGeometry` + `GroupTable`（point/edge/face）；**禁止**把 group 塞进 `PcgMetadata` JSON
- **三角化**：仅 Sink / 显式节点 / `triangulate_geometry`；建模算子不写死三角汤
- **Sweep**：输出 `side`、`cap_start`/`cap_end`、`seam`、`profile_corner`、`unshared` 等命名组
- **Bevel**：默认 `excludeUnshared=true`；`edgeGroup` 空 ≠ 倒全网
- **BMesh**：编辑内核；`bmesh_from_geometry` 保留 groups

## P0 铁律：节点阶段保持 Polygon，禁止三角化 Round-Trip

> **违反此规则将破坏 n-gon 拓扑，导致 bevel/subdivide/boolean 等下游算子失效。**

### 1. PcgGeometry 是节点间唯一 Canonical 传输格式

- 节点输出必须使用 `emit_geometry()`，禁止在中间节点使用 `emit_mesh()` 输出几何数据
- `PcgMeshData` 仅用于：Sink（最终输出三角化）、算法内部计算（不输出到下游）、纯 Mesh 源节点（无 geometry 输入）

### 2. 禁止 Geometry→Mesh→Geometry Round-Trip

- **禁止**因 PcgGeometry 缺少某个属性通道（color/UV/normal/materialName 等），就通过 `get_mesh_input()` 将 geometry 三角化为 mesh，操作后再 `geometry_from_mesh()` 转回 geometry
- `get_mesh_input()` 内部调用 `compute_split_normals()`（三角化 + 顶点分裂），破坏 n-gon 面信息
- `geometry_from_mesh()` 的 `merge_coplanar_angle_deg` 回合并面，无法恢复原始 n-gon 拓扑

### 3. 三角化只允许在 Sink 发生

- `triangulate_geometry()` / `compute_split_normals()` 只允许在 Sink / binary 序列化 / 算法内部计算中调用
- 中间节点算法如果需要三角化输入，必须在内部完成三角化→计算→写回 geometry，不能将三角化结果输出到下游

### 4. 新增属性时必须同步扩展 PcgGeometry

当新增节点需要操作某个属性（如 UV、materialName、自定义属性等）时：
1. 先检查 `PcgGeometry` 是否已有该属性通道
2. 如果没有，**先给 `PcgGeometry` 增加该通道**（字段 + `has_xxx()` + `set_xxx()` + binary 序列化 chunk + `merge_geometries` 传播 + `compute_split_normals` 传播）
3. 然后在节点中直接操作 geometry，使用 `emit_geometry()` 输出
4. **禁止**以"暂时先转 mesh 处理"为理由绕过

### 5. 已修复节点（2026-07-15）

| 节点 | 修复前问题 | 修复方式 |
|------|-----------|----------|
| `UVTexture` | `get_mesh_input` + `emit_mesh`，geometry 输入被三角化 | PcgGeometry 增加 UV 通道；节点直接 `set_uvs` + `emit_geometry` |
| `ProjectTexture` | 同 UVTexture | 同上 |
| `AssignMaterial` | `get_mesh_input` + `emit_mesh`，geometry 输入被三角化 | PcgGeometry 增加 `material_name_` 通道；节点直接 `set_material_name` + `emit_geometry` |
| `VertexColor` | 已修复（前次） | PcgGeometry color 通道 |

## C++ 注册检查清单

- [ ] `element_registry.cpp` 或对应 `register_*_elements`
- [ ] `CMakeLists.txt` 新源文件
- [ ] `schema/node-manifest.json` 节点 type / pins / properties
- [ ] `tests/test_phase*.cpp` 或扩展现有 ctest
- [ ] `build-pcg-core.sh --copy-to-unity` + `sync-manifest.sh`

## 关联

- 图编排：`pcg-graph-authoring` skill
- 复杂 bug 调试：[[cpt-agent-complex-bug-playbook]]、[[tip-diagnose-before-fix]]、[[tip-mesh-closed-manifold-check]]
- Boolean 专题：[[fw-pcg-boolean-mesh-arrangement]]
- 规划：`WorkLog/执行记录/PCG Block AI/Plan/Phase4.6-PcgGeometry-Groups-Plan.md`
