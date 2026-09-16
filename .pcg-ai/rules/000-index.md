---
id: pcg-ai-rule-index
name: PCG-AI 专用规则库
tags: [type/moc, area/pcg-ai]
rag_index: false
---

# PCG-AI 专用规则库

本目录是 PCG-AI 项目的专用规则根，不参与普通经验检索。

- `Engineering/`：PCG-AI 工程开发、native/manifest 同步、几何传输契约。
- `Graph Authoring/`：`.pcg` JSON、节点连线、布局、装配和模型策略。

检索契约：仅 `rule_search(domain="pcg")` 纳入本目录；`vault_search` 以及其他 domain 的 `rule_search` 必须排除本目录。

