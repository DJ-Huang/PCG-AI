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
