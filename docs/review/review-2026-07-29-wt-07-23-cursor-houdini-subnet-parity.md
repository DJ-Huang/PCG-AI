# PCG Subgraph / Houdini Subnet Parity Review

- Date: 2026-07-29
- Source: `wt/07-23-cursor` working tree
- Target: Houdini Subnet authoring semantics
- Scope: inline `Subgraph`, linked `SubgraphAsset`, interface authoring, navigation,
  preview, save, resolution, and execution flattening

## Findings

### Major — Duplicating an inline Subgraph shares the original definition

`DuplicateSelectedNodes` clones the instance node record, including its unchanged
`subgraphId`, but does not clone and remap the referenced `PcgSubgraphDefinition`.
Editing either instance therefore edits the same child network.

- Evidence: `PcgGraphView.cs:2155-2180`
- Additional evidence: promoting an inline Subgraph to an asset explicitly refuses
  definitions referenced by more than one instance, confirming that shared inline
  definitions are currently possible.
- User-visible failure mode: duplicate a Subgraph, enter the duplicate, edit its
  contents, then enter the original; both contain the edit.
- Houdini parity: a subnet is a containing node with its own child network. Shared
  implementation belongs to the digital-asset/HDA model, not ordinary subnet copy
  semantics.
- Recommendation: make inline definitions instance-owned. On duplicate/paste,
  recursively clone the referenced definition closure, allocate new definition and
  child node/edge IDs, and rewrite every nested `subgraphId`. Reserve shared
  definitions for `SubgraphAsset`.

### Major — Subgraph-local/promoted parameters do not exist

`PcgSubgraphDefinition` contains ports, nodes, and edges but no parameter definition
or per-instance override model. `CreateSubgraphFromSelection` rejects a selection
when a graph parameter targets one of its nodes instead of promoting or remapping
the parameter.

- Evidence: `PcgGraphTypes.cs:224-231`
- Evidence: `PcgGraphView.cs:990-1007`
- User-visible limitation: a useful graph section with exposed controls cannot be
  collapsed into a configurable Subgraph without first destroying its bindings.
- Houdini parity: promoted parameters appear on the containing node; for an ordinary
  subnet they are instance-local, while a digital asset edits a shared type
  interface.
- Recommendation:
  1. add `parameters` to `PcgSubgraphDefinition`;
  2. add typed `parameterOverrides` to each inline/asset instance;
  3. promote selected internal bindings during “Create Subgraph from Selection”;
  4. resolve overrides to internal node properties before flattening;
  5. expose the promoted controls in the Subgraph node Inspector.

### Minor — Interface ports have stable IDs but cannot be reordered

The interface panel supports add, delete, rename, and retype, but no reorder
operation. List order is serialized and is part of the visible node signature.

- Evidence: `PcgSubgraphInterfacePanel.cs`
- User-visible limitation: correcting the order requires deleting/recreating ports,
  which needlessly changes stable IDs and can break parent wiring.
- Recommendation: add drag handles or up/down actions that reorder list entries
  without changing port IDs. Treat ID as identity and list index only as presentation
  order.

### Minor — `SubgraphAsset` is HDA-like but has no definition version policy

Linked assets correctly provide shared reusable definitions, source GUIDs,
recursive dependency resolution, cycle/depth protection, and fail-closed interface
ghosts. However, every instance tracks the current file definition and there is no
explicit asset namespace/version or migration policy.

- User-visible risk: an intentional incompatible asset change can block all existing
  consumers at once.
- Recommendation: keep this separate from ordinary subnet parity. Add an asset
  schema/version plus explicit “upgrade instance” tooling if HDA-style production
  reuse is required.

## Fixed in this change

1. Reconcile every persisted `SubgraphAsset` instance at model level before preview,
   authoring save, and drill-in, including instances in hidden inline definitions.
2. Remove source-deleted snapshot ports automatically when they are unconnected.
   Connected removed/type-changed ports remain red ghosts and execution fails closed,
   preventing silent topology loss.
3. Repair orphan edges on every graph and SubgraphAsset save:
   - missing source or target node;
   - removed Subgraph input/output handle;
   - invalid inline Subgraph instance handle;
   - invalid linked Subgraph snapshot handle;
   - the same cases inside nested definitions.
4. Stop virtual interface-anchor views and their display-only wires from being
   serialized as real graph nodes/edges.
5. Persist interface-anchor placement through graph and SubgraphAsset serialization.
6. Delete internal interface edges immediately when an inline interface port is
   removed.
7. Clean the existing orphan edges in `Windows_floor.pcgsubgraph` and restore its
   pre-existing valid `in_1 -> out_1` passthrough after the orphan endpoints are
   removed.

The repair never guesses a replacement endpoint. A provably invalid edge is removed;
an incompatible but still-connected external interface is retained as a ghost so the
author must choose how to reconnect it.

## Parity matrix

| Capability | Current state | Houdini subnet target |
|---|---|---|
| Collapse selection into container | Implemented | Aligned |
| Double-click navigation / nested scopes | Implemented | Aligned |
| Explicit typed multi-input/output interface | Implemented | Aligned |
| Internal input/output pseudo-connectors | Implemented with anchors | Aligned |
| Stable wiring identity across rename/reorder | ID survives rename; reorder missing | Partial |
| Preview/cook through nested containers | Resolver + flattener implemented | Aligned |
| Parent-scope references | Implemented, validated at flatten time | Useful extension |
| Duplicate ordinary subnet independently | Shared definition | Not aligned |
| Promote internal controls to parent node | Rejected when bound | Not aligned |
| Per-instance parameter overrides | Missing | Not aligned |
| Reusable shared asset definition | Implemented as `SubgraphAsset` | HDA-like extension |
| Asset version/upgrade policy | Missing | Below HDA parity |

## Recommended order

1. **V1 — Inline definition ownership:** deep-clone definition closure on
   duplicate/paste and add regression tests.
2. **V2 — Parameter interface:** subgraph-local definitions, promotion, overrides,
   Inspector UI, flatten-time resolution.
3. **V3 — Port authoring:** reorder without ID changes, optional/default semantics,
   better connection diagnostics.
4. **V4 — Asset lifecycle:** namespace/version, explicit upgrade/migration, atomic
   writes and dependency-aware change reporting.

## Validation

- Mono/MSBuild: `PcgPlugin.Editor.Tests.csproj` compiled successfully.
- Selected Tuanjie Editor: `/Users/djhuang/PCG-AI-cursor/Unity`.
- Focused Subgraph tests: 20 passed, 0 failed.
- Original `n140` lot-city path: external resolution succeeded and execution
  flattening produced 44 nodes / 47 edges.
- Tuanjie script compilation: 0 errors, 0 warnings after the final test edit.
- `git diff --check`: passed.

## Houdini references

- Subnetworks encapsulate child nodes, support collapse-from-selection, internal
  pseudo-inputs, and node navigation:
  <https://www.sidefx.com/docs/houdini/network/organize.html>
- A subnetwork signature is controlled by ordered, labeled, typed inputs/outputs;
  internal Input/Output nodes inherit that signature:
  <https://www.sidefx.com/docs/houdini/nodes/cop/subnet.html>
- Promoting an internal parameter creates a parent-node control, and Houdini
  distinguishes per-instance subnet interfaces from shared asset interfaces:
  <https://www.sidefx.com/docs/houdini/shade/parms.html>
- Digital assets are reusable custom nodes and introduce their own versioning
  concerns:
  <https://www.sidefx.com/docs/houdini/assets/index.html>
  <https://www.sidefx.com/docs/houdini/assets/namespaces.html>
