# PICG graph authoring contract

Use for graph creation and edits on either platform. This is a contract, not a prescribed reasoning sequence. Match the requested scope; load image, generator, or complete-asset references only when those tasks apply.

## Discover before inventing

Inspect the target graph and the schemas of node types involved in the change. The live `pcg_get_node_types` manifest is authoritative for a running editor; `schema/node-manifest.json` and the implementation support offline inspection. Reuse a schema read within an unchanged manifest, but refresh after a server/build change. A graph hash does not prove that the manifest is unchanged.

For a new construction strategy or an unfamiliar mechanism, retrieve the relevant `.picg/rules/graph-authoring/` rule and reviewed Golden Graph. Use the project knowledge tools when available, or the same repository files. Examples illustrate wiring; they do not override current schemas. Do not load every model-type rule for a focused edit.

## Document and geometry

- Use version `1.0`, `nodes`, `edges` and `parameters`; include `subgraphs` when needed. Preserve existing unrelated fields and payloads.
- Give nodes and edges stable unique IDs. Use short, unique `data.__nodeTitle` values, explicit manifest-backed `sourceHandle` / `targetHandle` IDs and compatible pin types. End a runnable root graph in `Output`.
- Encode properties in their actual schema format; for example, `CreateSpline.controlPoints` uses a JSON string. Do not serialize transient scene-object references as durable bindings.
- Use metres unless the brief explicitly chooses another scale convention. Choose primitives from the required cross-section and volume, not just one silhouette. `MergeMesh` concatenation is not evidence of a Boolean union or manifold fusion.
- Bevel clean parts before merging a heterogeneous assembly. A post-merge bevel needs suitable connected topology and deliberate edge selection. Choose bevel width and tessellation for the part's scale and target view; arbitrary global percentages or segment counts are not proof of quality.

## Parameters and modules

Expose useful artist controls, not every node property. Static props may need no public parameters. Each root parameter targets an existing root node/property with the correct type, meaningful limits and a default matching the baked value. Do not assume expression, multi-target or subgraph-interior bindings unless the implementation supports them.

Use Subgraphs for coherent, reusable or independently editable components. Node counts can suggest a readability problem, but are not a quota. Avoid both tiny wrappers and an opaque mega-subgraph. Definitions live in root `subgraphs[]`; instances use `type: "Subgraph"` and `data.subgraphId`. Keep declared port IDs consistent with interior and instance edges, and avoid recursion. Inspect the existing schema/tutorial before changing an interface.

## Layout and writes

Use top-down data flow: vertically aligned chains, roughly 160 units between rows and at least 320 between same-row siblings. Keep independent component lanes readable and joins near their inputs. Treat root and inline Subgraphs as independent scopes.

After a coherent structural pass, review affected scopes and perform a position-only layout pass when needed. The helper is [layout_pcg.py](pcg-scripts/layout_pcg.py); inspect a copy before applying it. Do not relayout an unrelated graph or regenerate geometry for a position-only change. For live work, apply reviewed positions back through MCP and save the resulting document, not a divergent disk-only copy.

Use the [MCP contract](pcg-mcp.md) for live edits. An explicit offline/file-based task may use a reviewable saved-file change; it does not count as a live editor update.

## Verification

Run [validate_pcg.py](pcg-scripts/validate_pcg.py) on a saved deliverable and the applicable native validation/cook for changed behavior. Fix errors; investigate warnings and distinguish schema defects from style heuristics. For new geometry or material changes, inspect current target-platform output. Pure documentation or position-only changes do not require a new material/export pipeline.

A passing validator does not establish visual fidelity, and a Web render does not establish Unity acceptance. Finish when the requested deliverable and its checks are satisfied; otherwise report the verified partial result and exact blocker. Never promote an unrun check to PASS.
