---
domain: pcg
intents: author_graph,write_code
rag_index: true
rule_id: pcg/index
source_path: PCG AI Rule/Graph Authoring/000-index.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: verified_true
verified_by: PCG-AI manifest, examples and pcg-graph-authoring contract
verified_date: 2026-07-19
---

# PCG Graph Authoring 规则索引

先读通用规则，再按模型类型补查。模型文件给出策略候选，不替代当前参考图的 Shape Analysis，也不替代 manifest。

| rule_id | 用途 |
|---|---|
| `pcg/graph-contract` | `.pcg` JSON、manifest、命名、布局、Subgraph、参数与验证 |
| `pcg/triview` | 三视图输入、跨视图尺寸约束、节点精编与多视图验收 |
| `pcg/assembly-bevel` | 多部件装配倒角顺序与尺度 |
| `pcg/vehicle` | 车辆部件与节点策略 |
| `pcg/bridge` | 桥面、桥墩、拱圈与栏杆策略 |
| `pcg/building` | 建筑主体、立面门窗避让、主立面朝向道路/通道、实例间距 |
| `pcg/spiral-staircase` | 螺旋路径、台阶、栏杆和中心柱 |
| `pcg/scatter` | 点生成、过滤、投影和实例化 |

维护：新增模型规则放在本目录，使用 `rule_id: pcg/<slug>`、`domain: pcg`、`type: rule`，更新 `Rules/meta/rules-vault-index.md` 后执行 `vault-rag reindex`。高质量范例图放 `PCG AI Rule/Golden Graphs/`（**`.ragignore`，不进索引**；Agent 直读磁盘），不要把工程仓库未背书的 `examples/**/*.pcg` 当策略真源。

