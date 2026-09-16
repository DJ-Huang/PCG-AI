---
domain: pcg
intents: author_graph,write_code
rag_index: true
rule_id: pcg/triview
tags: [type/rule, domain/pcg, project/picg]
type: rule
verified_status: limited
verified_by: "Web review camera presets and per-view capture receipts"
---

# Orthographic three-view reconstruction

Front, side, and top orthographic references are geometry constraints. A perspective or three-quarter image is only a completeness check.

- Archive each source as `front`, `side`, or `top`.
- Use left-handed object space with +Y up; record the chosen front and side directions.
- Width is constrained by front and top, height by front and side, and depth by side and top.
- Give each dimension one owner and record conflicts beyond the chosen tolerance instead of silently averaging them.
- Choose primitives and operations from the actual cross-section, not from a single perspective silhouette.

Capture deterministic orthographic reviews for every required view after each dimension change. The lowest required-view score controls acceptance. A three-quarter review may still block completion when it reveals collapsed depth, intersections, or missing thickness.
