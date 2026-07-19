---
description: PCG-AI 项目规则检索薄适配（正文位于 Vault PCG AI Rule）
type: project-rule-adapter
domain: pcg
---

# PCG-AI 规则入口

本文件只负责项目识别和检索路由，不保存工程/编图规则正文。

触发 `pcg-core/`、`schema/`、`Unity/Assets/PcgPlugin/`、`examples/*.pcg`、新节点、`UnknownNode`、geometry/group/bevel/sweep 或 `.pcg` 编图任务时：

1. 调用 `rule_search(query="PCG-AI + 任务意图 + 节点/模型关键词", domain="pcg", top_k=10)`。
2. 至少读取 `pcg/project-engineering`、`pcg/index`、`pcg/graph-contract`、`pcg/assembly-bevel`；编图时再读取命中的模型规则。
3. 调用 `vault_search` 获取 Pitfall/Concept 等经验；它不会返回规则正文。
4. 实现事实以当前 `schema/node-manifest.json`、`pcg-core`、Unity PcgPlugin 和测试为准。
5. 编写 `.pcg` 时使用 `pcg-graph-authoring` Skill 并执行其 validator。

规则唯一正文：`VAULT_ROOT/PCG AI Rule/{Engineering,Graph Authoring}/`。仅 `rule_search(domain=pcg)` 可检索该根目录。
