---
domain: pcg
intents: write_code,debug,review,author_graph
rag_index: true
rule_id: pcg/project-engineering
source_path: PCG AI Rule/Engineering/pcg-ai-development.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: limited
verified_by: PCG-AI repository scripts, schema and pcg-core source audit
verified_date: 2026-07-28
---

# PCG-AI 工程开发契约

适用于 `pcg-core/`、`pcg-server/`、`schema/node-manifest.json`、`Unity/Assets/PcgPlugin/`、`examples/*.pcg` 及 PCG 节点新增、修复、调试和审查。

## 真源顺序

1. `schema/node-manifest.json`：节点 type、pin id/type、属性与默认值 SSOT。
2. `pcg-core` 注册、执行器和测试：native 行为 SSOT（由 `pcg-server` 进程加载）。
3. Unity PcgPlugin：编辑器序列化、Inspector 与 **HTTP cook client** 事实（无进程内 dylib）。
4. 本规则与 Skill：工作流约束；与源码冲突时先核实并更新规则，禁止用旧规则覆盖代码事实。

## 新节点或 native 行为变更

Unity Editor **不再加载** `PcgCore` / `PcgFbxExporter` dylib/dll；C++ 只跑在本机 `pcg-server`（HTTP）。变更后要完成：

1. `scripts/build-pcg-server.sh`（或 `run-pcg-server.sh`）：重建并重启 cook 后端；**不要**再把 dylib/dll copy 进 Unity `Plugins/`。
2. `scripts/sync-manifest.sh`：同步 manifest 到 GraphView/Resources；只改 C++ 或只改 manifest 都可能造成 `UnknownNode`、缺 pin 或 Inspector 默认值错位。

验证前确认 `pcg-server` 已在跑（`PCG → Server → Health Check` 或 `GET /v1/health`）。

## 几何传输

- `PcgGeometry` 是保留 polygon/group/属性语义的 canonical 表示；有 geometry 输入的中间建模节点应优先直接修改并 `emit_geometry()`。
- 禁止为了补 color/UV/material 等属性，把 geometry 无条件转成 mesh、操作后再 `geometry_from_mesh()`；该 round-trip 会三角化或重建拓扑，可能破坏 n-gon 和 group。
- 纯 mesh 输入分支、source/sink、序列化和算法内部临时三角化可以使用 `PcgMeshData`/`emit_mesh()`；不得把“中间节点一律禁止 emit_mesh”误读成无条件规则。
- 新属性优先扩展 `PcgGeometry` 及其序列化、merge、split-normal 传播，再让节点直接读写该通道。

## 写 `.pcg`

调用 `pcg-graph-authoring-unity`（或需管线评估时用 `pcg-graph-authoring-unity-dev`），并先用 `rule_search(domain="pcg")` 读取 `pcg/graph-contract`、`pcg/assembly-bevel` 与模型类型规则。所有节点/属性/pin 最终仍以当前 manifest 为准。拓扑/装配参考只来自 Vault 磁盘 `PCG AI Rule/Golden Graphs/`（不进 RAG，须直接枚举并读取）；**禁止**把工程仓库 `examples/*.pcg` 当策略参考。详见 `pcg/graph-contract`。

## Houdini UI 对齐门禁

PCG Inspector、参数面板或节点 UI 被要求“对齐 Houdini / 完全一致”时，必须执行：

1. 把参考图对应的 **Houdini 版本、当前页签、radio、toggle 启用状态**视为视觉真源；只比较相同状态，禁止把另一状态的条件参数误判成多余参数。
2. 写码前列出参数矩阵：`常驻显示 / 当前模式条件显示 / 仅内部兼容 / 从属启用条件 / 参考图不存在`。后端或 manifest 支持某参数，不代表 Inspector 必须展示。
3. Houdini 复合参数必须保持单行，例如 `toggle + label + numeric field + slider`；布局使用不可换行容器，并在目标 Inspector 宽度做人眼验证。
4. 通用 manifest renderer 必须实现 `companionField`、`visibleWhen`、禁用态和状态切换重建；当页签、边框、复合行或显隐关系无法精确表达时，使用节点专用 renderer。
5. 禁止泄漏参考 UI 不存在的 PCG 通用操作（如 `+ / bind`）或内部兼容字段；需要兼容旧图/native 时保留序列化与执行能力，只从 UI 隐藏。
6. 验证至少覆盖：相同状态可见参数集合、全部模式切换、复合行不换行、manifest 副本同步/校验、Editor 编译、节点功能测试与最终视觉验收。

详见 [[pit-pcg-houdini-ui-parity-reference-contract]]。

## 验证清单

- C++ 注册点、构建源列表、manifest 与测试覆盖一致。
- `pcg-server` 已用当前 `pcg-core` 重建并在跑；manifest 已同步。
- `.pcg` 已通过 `validate_pcg.py`；Graph Parameters 绑定目标存在且类型一致。
- 复杂几何问题按生成→传输→绑定→展示分层取证，不以“health 版本号正确”替代输出正确性验证。
