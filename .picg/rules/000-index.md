---
domain: pcg
intents: author_graph,write_code
rag_index: true
rule_id: pcg/index
tags: [type/rule, domain/pcg, project/picg]
type: rule
---

# PICG project rules

The `.picg/` directory is the authoritative rule and knowledge source for this repository.

- Start graph work with `pcg_kb_search` and read the applicable rule files with `pcg_kb_get`.
- Use `pcg_golden_graph_*` for reviewed graph references.
- Use the current node manifest for node types, properties, pins, defaults, and compatibility.
- Use the general vault only for cross-project engineering knowledge; ignore unrelated rendering-pipeline rules.
