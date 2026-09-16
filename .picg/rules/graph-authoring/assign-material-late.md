---
domain: pcg
intents: author_graph
rag_index: true
rule_id: pcg/assign-material-late
tags: [type/rule, domain/pcg, project/picg, area/material]
type: rule
verified_status: limited
verified_by: "PICG material examples and cook output"
---

# Assign materials after topology-changing operations

Apply topology operations first, then UVs and material assignment within each complete part. Early assignment can be lost or propagated unpredictably by boolean, bevel, merge, or remesh operations.

Recommended order:

```text
generate -> deform/boolean -> bevel -> UV -> AssignMaterial -> part merge -> output
```

The `Material` node must be wired explicitly into `AssignMaterial.material`. After cooking, inspect slot names and face coverage. Engine material asset creation is a separate user-visible choice; do not silently create or assume Unity material assets.
