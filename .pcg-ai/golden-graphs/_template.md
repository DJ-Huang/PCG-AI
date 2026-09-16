---
rag_index: false
id: golden-<slug>
name: <Human Title>
objectClass: <vehicle|bridge|building|prop|scatter|other>
pcg_path: PCG AI Rule/Golden Graphs/<objectClass>/<slug>.pcg
verified_date: YYYY-MM-DD
---

# <Human Title>

> 本目录整夹在 `.ragignore` 中，不进 RAG。Agent：Glob/Read 磁盘上的 `.pcg` / 本卡片。勿从工程 `examples/` 找替代品。

## 适用场景

- 

## 拓扑摘要

```text
<source> → … → Bevel (per part) → Merge → Output
```

## 模块表

| 模块 | 节点族 | 复用？ | 备注 |
|------|--------|--------|------|
| | | | |

## 尺度（米）

| 部位 | 约略尺寸 |
|------|----------|
| 整体 AABB | |

## 质量勾选

- [ ] validate_pcg 通过
- [ ] bevel 在 Merge 前
- [ ] 标题 / 布局合规
- [ ] 无已知反模式

## Graph JSON

<!-- 小图可嵌全文；大图只保留 pcg_path，避免笔记膨胀 -->

```json
{
  "version": "1.0",
  "nodes": [],
  "edges": [],
  "parameters": []
}
```
