---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/spiral-staircase
source_path: PCG AI Rule/Graph Authoring/spiral-staircase.md
tags: [type/rule, domain/pcg, project/pcg-ai]
type: rule
verified_status: limited
verified_by: PCG-AI spiral staircase example and current manifest
verified_date: 2026-07-19
---

# 螺旋楼梯建模策略

- 螺旋路径：优先 `CreateSpiralSpline`；需自定义形状时才用 `CreateSpline` 控制点。
- 台阶：完整 step prototype 沿螺旋采样/实例化；确认 tangent 对齐、步距、旋转和踏步宽度。
- 栏杆：沿偏移螺旋路径使用圆截面 `SweepAlongSpline`。
- 中心柱：`CreateCylinderMesh` 或直线 backbone 的圆截面 sweep。

台阶、栏杆、中心柱分别完成生成与必要的 bevel/material 后再 merge。人物尺度楼梯必须检查层高、踏步高度/深度、净空和栏杆高度；示例中的 spacing/radius 只能作起点，不得当普适值。

