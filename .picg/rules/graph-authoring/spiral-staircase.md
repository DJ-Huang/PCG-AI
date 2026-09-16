---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/spiral-staircase
tags: [type/rule, domain/pcg, project/picg]
type: rule
verified_status: limited
verified_by: "PICG spiral staircase example and current manifest"
---

# Spiral staircase authoring strategy

Use `CreateSpiralSpline` for the primary path unless the design needs a custom control-point spline. Instance a complete step prototype along the path, sweep a circular profile along an offset spiral for the rail, and use a cylinder or vertical sweep for the centre column.

Complete bevel and material work for steps, rail, and column before merging them. Validate tangent alignment, rise, tread depth, clear width, headroom, and railing height at human scale. Example radii and spacing are starting points, not universal defaults.
