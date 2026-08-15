---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/vehicle
source_path: PCG AI Rule/Graph Authoring/vehicle.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: limited
verified_by: PCG-AI vehicle examples and current manifest
verified_date: 2026-07-19
---

# 车辆建模策略

这是策略候选，不是固定配方；先按参考图逐部件判断形状。

| 部件 | 首选策略 |
|---|---|
| 有纵向截面变化的车身/驾驶室 | `LoftMesh` 或 profile/backbone `SweepAlongSpline`；不要用单个 Box 伪造曲面 |
| 真正矩形的货床壁、保险杠、小灯 | `CreateBoxMesh` + `TransformMesh` |
| 轮胎/轮毂 | `CreateCylinderMesh` 或圆截面 sweep；分段与真实车轮尺寸匹配 |
| 对称零件 | 完整部件 Subgraph 多实例，或经验证的 `MirrorMesh`/`CopyMesh` 路径 |
| 轮拱/开口 | `BooleanMesh` 前先保证 cutter 与主体拓扑、尺度和 operation 正确 |

主体在进入整车 `MergeMesh` 前完成自身 subdivide/bevel/material；车轮、玻璃、车门和灯具作为可命名模块单独完成。真实轿车通常约长 4–5 m、宽 1.8–2.0 m、高 1.4–1.5 m，轮径约 0.6–0.7 m；风格化尺度须显式记录。

