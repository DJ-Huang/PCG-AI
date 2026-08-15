---
domain: pcg
intents: author_graph,write_code
rag_index: true
rule_id: pcg/assembly-bevel
source_path: PCG AI Rule/Graph Authoring/assembly-bevel.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: verified_true
verified_by: PCG-AI bevel topology tests and assembly graph review
verified_date: 2026-07-19
---

# 装配倒角规则

多部件装配默认采用“单部件完成 bevel，再 merge”，禁止把分离固体合并后用一个 amount 盲倒角：

```text
part A → BevelMesh ─┐
part B → BevelMesh ─┼→ MergeMesh → Output
details (optional) ─┘
```

原因：`MergeMesh` 不等于 Boolean Union/Fuse；混合尺度部件共享 amount 会让小件自交/塌陷，全局 angle/group 也会误选高分段 sweep 的边。

Merge 后 Bevel 仅当以下条件全部满足时允许：输入已 union/fuse 成一个连通流形、特征尺度同族、边选择显式且局部。

amount 按当前部件最大尺寸定，不按整机 AABB 定。圆管/软管的截面已平滑时通常无需额外 bevel；薄辐条等小件宁可不倒角，也不要共享主体 amount。

