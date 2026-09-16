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

# Orthographic reconstruction

Use this for supplied orthographic views or an explicit three-view reconstruction request, not to demand three new images after every single-photo task. The maintained procedure is [three-view reconstruction](../../../.agents/skills/pcg-graph-authoring-web/triview.md).

Preserve labelled sources, record the object frame, and constrain width with front/top, height with front/side and depth with side/top. Give dimensions one owner; record source conflicts instead of silently averaging them. Recheck affected required views after dimension changes. A failed required view cannot be averaged away, and an integrity view can reveal collapsed depth or intersections. A single-photo job may use perspective evidence while declaring hidden-surface uncertainty; it must not claim orthographic reconstruction without those sources.
