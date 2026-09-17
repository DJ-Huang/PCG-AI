# Custom Function contract v1

This is the runtime-independent node contract for [#22](https://github.com/DJ-Huang/PCG-AI/issues/22), under [#20](https://github.com/DJ-Huang/PCG-AI/issues/20). The machine-readable structural contract is [custom-function-schema-v1.json](custom-function-schema-v1.json). Cross-field rules and the executable reference validator below are also normative.

**This change does not register an executable node.** Native execution, actual editor import/render/history wiring, and scheduling remain host integration work in #24/#25/#28. Do not add an unimplemented operation to `node-manifest.json`, advertise successful Cook, or treat the fixture as a runnable modeling example. Its source illustrates the intended API shape, not a finalized operation bridge. #21 is not a dependency of these static tests.

## Wire format and versioning

A node has the existing `id`, `type: "CustomFunction"`, `position`, and `data` fields. Its complete script contract is stored at `data.customFunction`. Ordinary editor metadata such as `data.__nodeTitle` remains outside that object. Existing `GraphNode`, `GraphEdge`, and subgraph scopes are used; there is no script-only graph or edge structure.

| Field | v1 meaning |
| --- | --- |
| `schemaVersion` | Integer `1`, the persisted declaration layout. |
| `language` / `languageVersion` | `"javascript"` / integer `1`. This is the PCG synchronous module authoring profile, **not** an ECMAScript edition or QuickJS version. |
| `apiVersion` | String `"1"`, version of the public PCG scripting contract. |
| `entryPoint` | Explicit named module export, normally `"main"`. `default`, member expressions, and reserved object keys are rejected. Existence is checked during execution, never by executing source during inspection. |
| `source` | Portable source text, never engine bytecode as the only representation. Empty text or invalid JavaScript syntax is a savable draft. |
| `seed` | Unsigned 32-bit integer. The RNG algorithm and runtime integration belong to #27. |
| `dependencies` | Explicit descriptors with `id`, `kind` (`resource` or `module`), `reference`, `version`, and `contentHash` (`sha256:` plus 64 lowercase hexadecimal digits). |
| `parameters` / `parameterValues` | Static declarations and values keyed by stable parameter ID. |
| `inputs` / `outputs` | Ordered display declarations; zero inputs and one or more outputs are supported. |
| `primaryOutput` | A declared, required, single output ID, or `null`. Never an index or display label. |

Every field above is explicit and required. Unknown fields are rejected rather than silently discarded. [The zero-edge fixture](fixtures/custom-function-v1.pcg) includes two outputs and a parameter override. The ordinary graph v2 schema already accepts the node data envelope: validate that envelope with the existing graph schema, then the script object with the new schema and semantic validator. This is not a claim that the existing Web importer or Core loader already accepts an unregistered CustomFunction node.

`migrateCustomFunction` currently implements **v1 identity migration only**. There was no published v0 contract to guess. Missing/future schema versions, unsupported languages/profiles, or API versions fail with actionable guidance to select a supporting host or an explicit migration. Preserve the original document and source on failure; do not overwrite them with defaults, re-label a future layout as v1, or transpile implicitly. Future migrations must be explicit, versioned, transactional, and covered by fixtures.

## Stable parameters and ports

An ID uses `[A-Za-z_][A-Za-z0-9_-]{0,63}`, excluding `__proto__`, `prototype`, and `constructor`. IDs are unique within each node's parameter, input, output, or dependency namespace. Input and output namespaces may use the same ID. Labels are display-only strings; duplicate labels are allowed. Hyphenated IDs require bracket access in JavaScript.

The following are the same ordinary edge before and after changing label `Body` to `Housing` or reordering the output declarations:

```json
{
  "id": "body-edge",
  "source": "script1",
  "sourceHandle": "body",
  "target": "sink",
  "targetHandle": "in"
}
```

`ctx.inputs`, `ctx.params`, and the returned output map use stable IDs, not labels. Changing an ID is removal plus addition, not rename; it can invalidate connections and source references. Static inspection does not rewrite JavaScript to repair references.

Ports use the ordinary v2 pin vocabulary: `Any`, `Param`, `SpatialPoint`, `SpatialSpline`, `SpatialSurface`, `SpatialMesh`, `SpatialGeometry`, `Texture`, `HeightField`, and `Material`. `Param` additionally requires `valueType`: `integer`, `number`, `boolean`, `string`, or `vector3`. Other payload types must not carry `valueType`. Integers are restricted to the JSON/JavaScript safe-integer range; numbers and vector components must be finite. This does not introduce a second geometry type system: hosts inject their normal compatibility function and scope-aware ordinary port resolver.

Every port declares `cardinality` (`single` or `many`) and `required` independently:

- `single` receives one value and at most one incoming connection. It may fan out through multiple outgoing edges without repeated node execution.
- `many` receives an array. A single output can contribute one item to a many input; a many output contributes its ordered items. A many output cannot feed a single input. Duplicate identical connections are invalid.
- Required means a value must be supplied after input default normalization. A required many value cannot be empty. Optional means its key may be absent; explicit `null` or `undefined` is not an omitted payload.

**Many-input binding order is the persisted ordinary edge-array order**, followed by each upstream array's item order. Do not sort by edge ID: copy/paste regenerates edge IDs and must not reorder geometry. Reordering port declarations changes display order only. #25 implements binding; #27 must include effective connection/value order in cache inputs.

Parameters and Param inputs may declare a correctly typed `default`, numeric `minimum`/`maximum`, and/or a nonempty typed `enum`. Many inputs/outputs may also constrain `minItems`/`maxItems`. Bounds must be consistent; required many ports cannot have `maxItems: 0`. Single ports and parameters cannot use item bounds. Native payloads cannot persist handles/defaults or literal constraints. Outputs have **no defaults**: a missing required output is an error, not permission to reuse old data.

Input defaults apply only when a port is genuinely unbound. A connected upstream failure must remain a failure; the binder must not erase its key and accidentally activate a default. Missing required parameter/input values may be saved as incomplete authoring data, but `validateParameters` and `validateCustomFunctionConnections` diagnose them before Cook.

`parameterValues` is a nested node-local map. This contract does not invent dotted `GraphParameter.targetProperty` interpretation. Mapping exposed graph/blackboard parameters into that map is an explicit host integration task, not a hidden path parser in #22.

## Static inspection and value diagnostics

The reference modules are:

- [customFunctionContract.ts](../web/pcg-editor/src/customFunctionContract.ts): structural and semantic validation, v1 parsing/migration, normalization, complete value-map validation, and canonical cache descriptor.
- [customFunctionGraph.ts](../web/pcg-editor/src/customFunctionGraph.ts): static connection checks, atomic contract-edit planning/replay, selection identity remapping, presets, and static port inspection.

Neither module imports QuickJS, React, a JavaScript parser, geometry operations, networking, or filesystem code. Source is opaque text throughout validation, inspection, copying, hashing, and persistence. Tests trap `eval`/`Function` and supply side-effecting source, invalid syntax, and infinite loops without executing them. Persisted objects must be plain finite JSON data; getters, custom iterators, sparse arrays, cycles, and non-JSON values are rejected instead of being invoked or silently lost on save.

The host must pass detached data maps/arrays and trusted native handles to runtime value validation. Arbitrary user-created JavaScript Proxies are **not** a safe cross-runtime inspection interface; the execution bridge must extract/validate results at its controlled boundary. `ValueInspector` must verify handle ownership, liveness, and actual native type. It must not trust a user object's `pinType` tag, execute JavaScript getters, or reinterpret pointers. A supplied compatibility callback uses the ordinary graph rules; the isolated validator's exact-type fallback is deliberately fail-closed.

`validatePortValues` validates the complete input or output map. Missing/undeclared ports, wrong types, wrong value shape/cardinality, and invalid handles carry `code`, `path`, `nodeId` when supplied, `portId`, and `direction`. Parameter errors carry `parameterId`; connection errors also carry `edgeId`. There is no successful partial `value` on failure. The host must validate **all** outputs before publishing or caching any of them; actual publication and process isolation belong to #24/#26.

Dependency descriptors are metadata, not authority: they never fetch files, import modules, install packages, or grant capabilities. A future execution host must resolve allowed dependencies, verify content hashes, and reject unsupported/disallowed modules. No wildcard package version substitutes for content verification.

## One explicit edit transaction

`planCustomFunctionEdit(scope, nodeId, next, adapter, policy)` prepares a complete `before`/`after` transaction without mutating the input graph. The adapter resolves ordinary nodes in the selected root/subgraph scope and supplies the existing type compatibility rules. Edge type annotations are advisory, never authoritative. Port removal, ID changes, incompatible types, and reduced cardinality are revalidated by stable endpoint ID.

Default policy is `reject`: return port/edge diagnostics and leave the graph unchanged. Explicit `remove` deletes every invalid incident edge in the **same** transaction as the contract edit; it never picks an arbitrary winner when several sources compete for a newly single input. It returns the exact removed edge IDs and warnings. Retained edge IDs/handles do not change, and existing cached type annotations are refreshed.

The host applies `after` through its ordinary history stack exactly once. `replayCustomFunctionEdit` restores schema, source, parameters, declarations, and edges together for Undo/Redo, rejecting a stale snapshot instead of overwriting intervening edits. It is not a second global editor history stack. Normal graph cycle checks, unrelated node validation, session/version authorization, dirty propagation, and the real UI transaction dispatch remain the host's responsibility. The planner may produce a savable draft with unconnected required inputs; pre-Cook diagnostics still apply.

## Persistence, copying, presets, and subgraphs

Save/reopen preserves the complete contract, source string (including Unicode and line endings), parameters, local IDs, and ordinary edges. Canonical JSON changes object-key ordering, not source contents or array ordering. Serialization never stores runtime handles or compiled bytecode as portable data.

`copyContractSelection` generates fresh **node, edge, and graph-parameter IDs**, remapping edge endpoints and graph-parameter `targetNode` references. It preserves node-local parameter/port IDs, source, dependency references, positions/metadata, and relative edge ordering. Only selected internal edges are copied; external edges are not silently rebound. Copies are deep-detached; allocator collisions fail atomically. The same operation covers duplication of one or several nodes.

A preset is a detached full contract without graph/node identity. Applying a preset must use the same edit planner with explicit edge policy, not a shallow property patch that leaves orphaned edges or undeclared parameter values.

A Custom Function inside a subgraph obeys the same node-local rules. Pass the definition's scope to edits; a node with the same ID in the root graph must remain unchanged. Duplicated Subgraph instances keep their shared `subgraphId` reference. Cloning an entire definition is a separate host operation which remaps definition/node/global IDs, not local script port IDs. The existing subgraph outer-interface restrictions (including its single output boundary) are unchanged; multiple internal script outputs do not grant new subgraph features.

## Cache-affecting fields and primary selection

`customFunctionCacheDescriptor` returns deterministic canonical JSON of the entire static contract. It includes source, all versions, entry point, seed, dependencies, declaration defaults/constraints, parameter values, ports, and primary selection. It conservatively includes labels and declaration ordering too: unnecessary invalidation is preferable to missing an output-affecting field. It is **not** a final cryptographic cache key or a production cache implementation.

#27 must additionally include evaluated upstream values and ordering, effective graph connections, verified dependency/resource contents, Core/runtime versions, execution options, capability policy, and relevant budgets. Unknown dependencies must fail closed rather than becoming hidden graph inputs.

`primaryOutput` identifies a candidate by stable ID only. Whether its type can be previewed/exported, explicit downstream Output precedence, zero-edge sink eligibility, and ambiguous-root diagnostics are #25 host decisions. This contract does not auto-select the first declared output or execute a script to discover one.

## Verification and handoff

Run from the repository root with Node.js supporting `node:test`, TypeScript from `web/pcg-editor/node_modules` (or `tsc` on PATH), and Python's `jsonschema` 4.x installed:

```sh
node --test scripts/test-custom-function-contract.mjs
python scripts/test-custom-function-schema.py
```

The Node suite compiles the two actual modules with strict type checking and unused-symbol checks into a temporary directory, runs the built-in Node test runner, and removes compiled output. It performs no npm downloads. The Python suite independently validates Draft 2020-12 and ordinary v2 graph fixtures. Both consume [57 shared conformance cases](fixtures/custom-function-cases.json), explicitly distinguishing structural-schema checks from semantic invariants such as uniqueness by ID, primary references, and cross-field constraint consistency.

| #22 acceptance area | Evidence in this change |
| --- | --- |
| Zero/multiple inputs and multiple outputs; ordinary edge format | Zero-edge and fan-out fixtures; ordinary graph-schema validation; typed connection and value tests. |
| Label/reorder stability | Edge identity and handle equality after rename/reorder, then replay tests. |
| Atomic port removal/type/cardinality edits | Reject/remove policies, all-invalid-edge removal, stale replay guard, Undo/Redo equality. |
| Port-specific diagnostics | Missing/undeclared/wrong output and input tests, trusted-handle rejection, no partial result map. |
| Persistence and reuse | Exact-source roundtrip, selection/duplicate remapping, detached presets, scoped and shared subgraph tests. |
| Unsupported versions | v1 identity migration and explicit failure/guidance for missing/future schema/API/language/profile. |
| Inspection never executes source | Side-effect/loop/invalid-source tests and accessor/iterator rejection. |

These are contract/reference-module tests, not Web UI, native Cook, QuickJS, Windows/macOS, or Unity acceptance evidence. #23 can consume this stable declaration model for the shared native operation invocation bridge. #25/#28 must wire these contracts into actual host persistence, dynamic-port rendering, connections, and history; #24 remains responsible for JavaScript execution and checked geometry handles.
