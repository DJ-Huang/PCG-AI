---
rag_index: false
id: golden-<slug>
name: <Human Title>
objectClass: <vehicle|bridge|building|prop|scatter|other>
pcg_path: .picg/golden-graphs/<objectClass>/<slug>.pcg
verified_date: YYYY-MM-DD
---

# <Human Title>

## Intended use

- <Describe the graph-authoring situations this reference supports.>

## Topology

```text
<source> -> ... -> per-part bevel/material -> merge -> output
```

## Modules

| Module | Node family | Reusable | Notes |
| --- | --- | --- | --- |
| | | | |

## Scale

| Part | Approximate size in metres |
| --- | --- |
| Overall bounds | |

## Review checklist

- [ ] The graph validates and cooks.
- [ ] Bevels are applied before the final merge.
- [ ] Titles and top-down layout pass validation.
- [ ] No known anti-pattern remains.
