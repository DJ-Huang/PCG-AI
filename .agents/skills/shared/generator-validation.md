# Generator reliability checks

Use for reusable generators, changed parameter/seed behavior, or an explicit reliability/performance assessment. This is available to both Web and Unity authoring; it is not a separate skill or a mandatory stage for every static prop.

First state the promised output and supported variation. Inspect the actual manifest and implementation before classifying a required operation as missing. A missing node name may have a valid composition; `MergeMesh` alone is not a Boolean/fuse. Separate an unsupported requirement from an unmeasured performance concern.

## Select relevant tests

| Area | Evidence |
| --- | --- |
| Structure | Valid node types, pins, Subgraph interfaces, output and parameter bindings. |
| Defaults and boundaries | Baked defaults and relevant legal min/max or empty cases cook with intended results. Test interactions that can break dimensions or topology. |
| Determinism | Repeat a fixed seed and parameter set in the same build/environment; compare meaningful geometry/output signatures, not unrelated timestamps or serialization noise. |
| Variation | Representative legal seeds/overrides change only permitted features and preserve output invariants. |
| Invalid inputs | In a disposable test scope, invalid types/ranges/paths produce clear failures rather than stale success, crashes or data loss. |
| Regeneration | Reload/recook reproduces intended hierarchy, material slots and durable references without duplicate or stale generated output. |
| Performance | Record measured cook time, environment and workload against a stated budget; otherwise report a baseline, not an invented PASS. |

Avoid exhaustive Cartesian parameter sweeps without a reason. Use fixtures or an isolated copy for destructive/invalid cases. Do not delete unrelated assets as cleanup. When testing a live graph, save its intended state, use normal hash-protected writes, restore temporary overrides and verify the final state before delivery.

Report PASS, FAIL, BLOCKED or NOT_APPLICABLE per selected check with seed/parameter sets and evidence. Repair a fixable graph defect and rerun affected tests. For a genuine capability gap, report the unmet requirement, supporting schema/code evidence and practical alternatives. Research external techniques only when that helps the decision; no particular external research tool is required.

A user-authorized implementation change can resolve the gap using the normal engineering workflow. An approximation needs its fidelity/technical limit stated and, when it changes the requested outcome, explicit agreement. Neither a test receipt nor an approved approximation proves visual or complete-asset acceptance.
