---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/assembly-bevel
tags: [type/rule, domain/pcg, project/picg]
type: rule
verified_status: verified_true
verified_by: "Current graph validator and hard-surface examples"
---

# Bevel complete parts before assembly

For a multi-part hard-surface asset, finish each named part before the final assembly merge:

```text
part source -> shape operations -> bevel -> UV/material -> part output
parts -> final merge -> output
```

Do not apply one global bevel after unrelated parts have been merged. It can soften contact seams, create invalid topology, and remove per-part control. A small purposeful part may omit bevel only when the design calls for a sharp edge and the omission is recorded.

Use real-world dimensions and choose bevel width relative to the part, not the whole asset. Validate topology, silhouette, normals, and material groups after beveling.
