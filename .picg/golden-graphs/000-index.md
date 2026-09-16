---
rag_index: false
tags: [domain/pcg, project/picg, area/golden-graphs]
---

# PCG Golden Graphs

Golden Graphs are reviewed reference assets for graph-authoring agents. They are served by the project-local `pcg_golden_graph_*` tools and are intentionally excluded from the general knowledge index.

## Usage

1. List candidates with `pcg_golden_graph_list`, optionally filtered by class.
2. Read the matching card and graph with `pcg_golden_graph_get`.
3. Treat the current node manifest and `.picg/rules/` as authoritative when a reference is older.

Do not use `examples/**` or Unity demo graphs as strategy authorities. Those assets demonstrate behavior, but they may preserve compatibility or presentation choices that are unsuitable as authoring defaults.

## Classes

`vehicle`, `bridge`, `building`, `prop`, `scatter`, and `other`.

## Admission criteria

- The graph validates and cooks against the current manifest.
- Complete parts are beveled before the final assembly merge.
- Scale is real-world metres unless the card explicitly says `stylized`.
- Every node has a unique title and the graph uses top-down layout.
- The graph contains no known authoring anti-pattern.

Unreviewed candidates belong outside this directory until they meet every criterion.
