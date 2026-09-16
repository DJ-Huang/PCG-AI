# PCG pipeline validation gate

This is the development-only `PCG_PIPELINE_VALIDATION` stage. Run it after a graph has been authored, independently relaid out across the root and every inline Subgraph definition, and passed static graph validation, and before accepting its cooked white model for the shared asset-production workflow.

It validates the generator itself. It does not replace geometry validation, UV/projection work, materials, textures, prefab construction, rendering, or final acceptance; those use the same shared rules as the standard skill.

## Purpose and verdicts

Validate that the current PICG pipeline can produce the AssetSpec efficiently, repeatably, and without stale or hidden failure states.

| Verdict | Meaning | Next action |
|---|---|---|
| `PASS` | Every required check passes | Continue to white-model validation |
| `REWORK` | The graph/configuration is fixable but one or more tests fail | Return to graph planning, node construction, parameter configuration, or generation logic; retest |
| `GAP` | A required operation has no supported honest composition | Research and stop for explicit user override |
| `INEFFICIENT` | Only a long hack, wrong topology semantics, or low-fidelity composition exists | Research and stop for explicit user override |
| `OVERRIDE` | The user explicitly authorizes a known gap | Record the limitation and continue; final hard failures still apply |

Never call `MergeMesh` a Boolean/fuse, turn a variable-section form into a box stack, place a global mixed-scale bevel after assembly merge, or invent a manifest node/property to produce a false `PASS`.

## Required evidence

Load the AssetSpec, the shared `pcg-graph-authoring.md`, the current `schema/node-manifest.json`, applicable PCG rules, and the generated graph. Golden Graphs under `<workspace>/.picg/golden-graphs/` may support an allowed composition; repo examples and Unity demos may not.

Before testing, create a compact demand inventory that describes outcomes rather than desired node names:

```text
Demand: variable-section body → SweepAlongSpline + cross-section profile → covered
Demand: fused manifold boolean cut → missing
Demand: exposed floor count, seed, legal range → covered
Demand: named paint/rubber material slots → covered
```

Classify each demand as `covered`, `approximate-inefficient`, or `missing`, with manifest/rule/Golden-Graph evidence. This inventory is a diagnostic; the normal gate remains post-authoring. If a requirement is obviously impossible to author honestly (for example, a required missing Boolean, UV unwrap, SDF, or topology operation), identify the gap during planning rather than fabricate a graph, then use the gap path below.

## Validation matrix

Run every relevant row against the authored graph and a clean Unity review scene.

| Area | Required check | Pass condition |
|---|---|---|
| Structure | Graph JSON, node availability, pin types, connections, Subgraph interfaces, output terminator, titles, and independent root/Subgraph layout | Manifest/static validation passes; layout receipt is `PASS`; no invented node, pin, or property |
| Parameters | Required exposed parameters, target node/property, type, baked default, ranges, and overrides | All bindings exist; defaults stay synchronized; each legal override cooks |
| Seed determinism | Two same-seed regenerations | Same intended output signature/hierarchy and no unrelated drift |
| Variation | At least two legal seed/variation cases | Allowed variation changes while required silhouette, scale, slots, and constraints remain valid |
| Boundaries | Baked default plus each exposed numeric min/max; applicable zero/empty inputs | Valid boundaries complete; empty results are intentional and reported rather than silent corruption |
| Invalid input | Out-of-range/type/path/required-binding cases applicable to the graph | Fails with a readable validation/runtime error; no crash, stale mesh, or misleading success |
| Performance | Cook/regenerate under the AssetSpec budget, or record a baseline when no budget exists | Completes without timeout, runaway node growth, or repeated leaked artifacts |
| Output contract | Bounds, front axis, modules, material-slot intent, component references, and graph output match AssetSpec | Generated result is structurally ready for shared geometry validation |
| Durability | Asset paths, prefab/graph references, clean-scene recook and reload | No scene-instance-only reference, broken path, duplicate output, or stale artifact survives regeneration |

Use the clean scene and capture procedure from `AUTHORING_SKILL_DIR/unity-review.md`. The gate may use a white model to inspect structure and variation, but it must not score artistic material fidelity; full material/texture scoring remains in shared final acceptance.

## Test procedure

1. Run the independent root/Subgraph layout pass, confirm that only `position.x/y` changed, run the normal graph-plan/static checks, and cook the authored default graph in a new clean review scene.
2. Execute structure and output-contract checks. Treat an empty or stale output as failure even if the graph UI appears valid.
3. Enumerate exposed parameters. Test the baked default, legal min/max values, repeat one same-seed cook, then test legal variation seeds. Test malformed/out-of-range inputs only where the runtime accepts external override input.
4. Re-open/re-cook the asset in the clean scene. Compare hierarchy, references, bounds, and output against the AssetSpec. Inspect the output for duplicate or stale artifacts.
5. Record performance as a measured duration or budget result. Do not claim performance passes solely because a single static graph validated.
6. Issue a verdict. For `REWORK`, fix only the earliest responsible graph stage and repeat the affected checks. Do not proceed to materials/textures until the gate is `PASS` or the user has explicitly overridden a real gap.

## Receipt

Emit this before white-model approval and write it to a durable report file beside the plan (for example `<plan-stem>-pipeline-gate.md`), so a post-compaction session can re-enter without chat history:

```text
PCG Pipeline: PASS|REWORK|GAP|INEFFICIENT|OVERRIDE
layout=<PASS|FAIL> |
structure=<PASS|FAIL> | parameters=<PASS|FAIL> | seed=<PASS|FAIL> |
boundaries=<PASS|FAIL> | regeneration=<PASS|FAIL> |
performance=<PASS|BASELINE|FAIL> | outputContract=<PASS|FAIL>
demands=<n> covered=<n> gap=<n> inefficient=<n>
evidence: graph=<path> | reviewScene=<path> | reports=<paths>
```

For `REWORK`, name the failing test, its earliest repair stage, and the retest performed. For `PASS`, continue immediately to the shared geometry stage.

## Context budget and re-hydration

This gate runs many cook cycles between graph authoring and the material stages — the highest compaction-risk window of the whole workflow.

- Script the mechanical checks (same-seed recook, boundary min/max sweeps, parameter enumeration) wherever possible and capture results to report files; spend agent vision on reference comparisons, not on repetitive receipts.
- Before issuing the verdict, re-hydrate: read `<plan-stem>-RESUME.md` (regenerate with `SHARED_SCRIPTS_DIR/report_pass.py <plan> --resume`), then the plan JSON, then the archived reference and AssetSpec. Do not judge AssetSpec correspondence from chat memory.
- Record the gate report path in the handoff receipt (`reports=<paths>` above).

## Gap research and stop rule

When any demand is `missing` or `approximate-inefficient`, do both research tracks before stopping:

1. Find the relevant Houdini SOP/HDA pattern in SideFX documentation or official examples.
2. Use `agent-reach` to research mature alternatives such as Unreal PCG/Geometry Script, Blender Geometry Nodes, established DCC workflows, or a relevant open-source implementation.

Report the exact unmet demand, evidence, Houdini/mature-solution approach, missing PICG node/property/composition, expected fidelity impact, and recommended product change. Do not continue graph authoring or downstream material work until the user explicitly says to override the gap.

```text
PCG Pipeline: GAP
Task: <one line>
Demand: <outcome> → missing|inefficient (<why>)
PICG evidence: <manifest/rule evidence>
Houdini: <SOP/pattern + source>
Mature solution: <source>
Product gap: <node/property/composition to add>
Recommendation: stop | wait for product | explicit override to approximate
```

## Invariants

- `PASS` only unlocks the shared white-model stage; it never declares the asset finished.
- A `REWORK` is normally an autonomous repair loop, not a parameter/save-path confirmation point.
- An `OVERRIDE` must be explicit, recorded, and visible in the final report.
- This gate contributes a development report only. It must not change shared final-score weights, hard failures, or the `FINAL_ACCEPTED` threshold.
