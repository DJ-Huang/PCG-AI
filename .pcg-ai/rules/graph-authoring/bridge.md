---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/bridge
source_path: PCG AI Rule/Graph Authoring/bridge.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: limited
verified_by: PCG-AI bridge examples and current manifest
verified_date: 2026-07-19
---

# 桥梁建模策略

| 部件 | 首选策略 |
|---|---|
| 桥面 | backbone + 闭合截面 `SweepAlongSpline`；变截面时使用 profile/loft 能力 |
| 桥墩 | 先完成一个 pier prototype，再沿路径采样/实例化；矩形桥墩可用 Box |
| 拱圈/缆索/栏杆 | 沿弧线 sweep；圆杆用 circle，石拱圈用明确截面 |
| 重复栏杆柱/铺板 | prototype + points/spline 采样 + copy/instance，避免手工复制大量节点 |

桥面作为单固体先选边并 bevel，再与桥墩、栏杆、缆索 merge。单跨桥默认按真实米制估算（跨度常为 10–40 m、桥面宽 3–8 m），具体尺寸由参考图或用户参数覆盖。

