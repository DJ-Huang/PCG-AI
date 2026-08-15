---
domain: pcg
intents: author_graph,write_code
rag_index: true
rule_id: pcg/graph-contract
source_path: PCG AI Rule/Graph Authoring/graph-contract.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: verified_true
verified_by: schema/node-manifest.json, graph parser, Unity GraphView and validator; primary-face access orientation note 2026-07-21
verified_date: 2026-08-01
---

# `.pcg` 图创作基础契约

## 写前真源

1. 读取当前仓库 `schema/node-manifest.json`；禁止凭记忆发明 node type、property 或 pin id。
2. `rule_search(query="PCG-AI 编图 + 模型关键词", domain="pcg", top_k=10)`；至少读取本规则和 `pcg/assembly-bevel`。
3. **高质量图参考**：只用 Vault 磁盘目录 `PCG AI Rule/Golden Graphs/`（已在 `.ragignore`，**不进 RAG**）。写图前直接枚举并读取该目录的同类 `.pcg`（及可选 `.md` 卡片），不要用 RAG 搜索此目录。**禁止**把工程仓库 `examples/*.pcg`、Unity demo `.pcg` 当策略真源。skill `examples.md` 仅作短连线模板。用户点名编辑某文件时除外；与本规则族冲突时改掉反模式。

## JSON 与命名

- 根对象使用 `version: "1.0"`、`nodes[]`、`edges[]`、`parameters[]`，需要复用时再加 `subgraphs[]`。
- 每个节点使用语义化 snake_case `id`，并在 `data.__nodeTitle` 写唯一、短、按部件职责命名的标题。
- `edges[].sourceHandle/targetHandle` 必须匹配 manifest pin id 与 pin type；运行图以 `Output` 结束。
- `CreateSpline.controlPoints` 按 manifest 要求写 JSON 字符串，不写场景对象引用。

## 布局与模块

- Houdini 风格自上而下；主链同 X，行距约 160；同一行兄弟节点 X 间距至少 320。
- 多部件装配按 subsystem lane 排布；长距离跨 lane 对角线和全局 topo-grid 视为布局失败。
- 重复部件、约 40+ 根节点或单个完整部件链明显膨胀时，优先封装“完整可命名模块” Subgraph；禁止 2–4 节点小 stub、无关部件拼包或 mega-subgraph。

## 参数与尺度

- PCG-AI 单位为米；除非用户明确要求玩具/抽象尺度，采用真实物理尺寸并保持所有部件同一尺度族。
- 新图写入前确认 Tier 1/2 authoring 默认值；存在高频可调项时，确认后写 root `parameters[]`，并保持 `default` 与目标 node `data` 同步。
- Graph Parameter 只能绑定真实 root node 的真实 property；当前一条 parameter 对应一个 target。

## 场景约束（道路 / 地面 / 建筑 / 材质）

- 路面与地面关系：不要让“道路”完全落在“地块/地面”的封顶面之下（会表现为路面材质已绑定但整段 mesh 不可见）。在作者意图里，默认要求道路铺装至少抬到地块顶面之上或与顶面齐平（预留少量 clearance），并在 Unity 中验证道路子网格（road slot）的 Bounds/Y 范围。
- 材质创建确认：当图包含 `AssignMaterial`（或下游会生成/依赖材质 slots）时，是否创建/导入 Unity `Material` 资产与是否写入 `PcgGraphComponent.m_MaterialBindings`，必须先 ask user 决策；未经确认不得自动创建或假设材质文件/颜色方案。
- 建筑避开主道路：建筑（例如来自 `CopyMeshToPoints` / 实例采样 / 建筑点放置链路）必须应用“避开主道路”的过滤/排除逻辑，避免建筑占据 road footprint 或直接落在道路之上。主道路以“road 模块合并后的 footprint”为准（例如 road_a + road_b 合并后的区域），通过 mask/group/attribute 过滤点或控制点位放置范围实现。
- **有向实例朝向主通道（P0）**：prototype 有明确正面/入口/前进轴时（建筑门立面、车辆车头、招牌正面等），实例旋转不得默认世界 identity。每个落点应将 **主轴朝向最近的主通道**（道路、路径、河岸、广场边等约定的 access）；仅避让足迹不够。建筑细则与 `CopyMeshToPoints` 帧轴约定见 `pcg/building`；通用 scatter 见 `pcg/scatter`。
- 立面开口（门 / 窗）：同一立面上门与窗的 AABB 不得重叠；有中门时窗列必须清出门洞保护区。细则见 `pcg/building`。
- 建筑实例间距：lot / scatter 放置的多栋建筑水平 footprint 不得相交；`LotSubdivision.minSize` 须大于最大建筑边长，必要时加 `PointRelax`。细则见 `pcg/building`。

## 保存与验证

- 写文件前探测 `.pcg`/Unity Assets 候选目录并让用户确认，不自行假设路径。
- 运行 `pcg-graph-authoring/scripts/validate_pcg.py <file.pcg>`；新图的缺 title、同排间距、错误 pin、断裂参数绑定、装配 Merge→Bevel 警告都必须修复。
