---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/graph-authoring-index
tags: [type/rule, domain/pcg, project/picg]
type: rule
---

# Graph-authoring rule index

Always load `pcg/graph-contract` and `pcg/assembly-bevel`. Then load the object-specific rule when applicable:

- `pcg/building`
- `pcg/bridge`
- `pcg/vehicle`
- `pcg/scatter`
- `pcg/spiral-staircase`
- `pcg/triview`
- `pcg/assign-material-late`

Use `pcg_golden_graph_*` for reviewed examples and the live manifest for node-level truth.
