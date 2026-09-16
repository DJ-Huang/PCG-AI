---
id: cpt-pcg-cook-policy
name: "Interactive PCG cooking: triggers, invalidation, caching, and preview budgets"
category: Concept
tags: [type/concept, area/pcg, area/unity-editor, area/performance]
verified_status: limited
verified_by: "PICG EveryFrame crash and leak reproduction; SideFX Houdini cooking model"
verified_date: "2026-07-18"
common_assumption: "Moving every-frame full-graph cooking to a worker thread makes interactive preview inexpensive."
---

# Interactive cook policy

Interactive tools should not recook an entire graph unconditionally on every editor frame. Treat these as separate controls:

1. **Triggers:** manual cook, parameter or topology change, drag completion, or an explicitly budgeted continuous mode.
2. **Invalidation:** mark the changed node and its downstream closure dirty.
3. **Cache identity:** include parameters, input content, dependencies, implementation version, seed, and execution context.
4. **Preview budget:** return low-cost geometry first and reserve final quality for an explicit request.

Asynchronous execution prevents long main-thread stalls; it does not eliminate work, cache misses, queue pressure, or resource lifetime problems.

## Safe state flow

```text
change -> validate inputs -> mark downstream dirty -> supersede stale work
       -> serve valid cache -> execute within budget -> publish atomically
       -> release superseded native and engine resources
```

Published results must include a request generation so an older slow request cannot replace a newer result. A targeted preview must fail within its requested scope instead of silently falling back to a full-graph cook.

## Acceptance

- Empty inputs, broken edges, deletion, and rapid edits never cross into an unsafe native call.
- Unaffected upstream nodes remain cached.
- Queues, tasks, meshes, and native buffers return to a stable baseline.
- Older requests cannot overwrite newer results.
- Continuous cooking is opt-in and has an explicit time and memory budget.
