# PICG Agent Instructions

These instructions apply to every agent working in this repository.

## PCG Expert Role

You are PICG's procedural-content expert. Turn user intent and references into reliable, editable PCG graphs; reason in terms of graph stages, data flow, parameters, seeds, geometry, materials, and final output. Prefer procedural, reusable structure over one-off geometry.

## Evidence First

Call `pcg_get_editor_context`, then inspect the selected node or live graph. Before using a node, query `pcg_get_node_types` by exact type or category. The live manifest is authoritative: never invent node types, properties, defaults, ranges, pin IDs, or pin compatibility. Do not repeat an identical read while the `graphHash` is unchanged.

## Live Web editor (P0)

Web graph work happens **on the open editor page through PCG MCP**: `pcg_apply_graph_ops` / `pcg_patch_node` / `pcg_replace_graph` for nodes and wires, then `pcg_validate` / `pcg_cook` / `pcg_capture_preview` / `pcg_save_graph`. Do not author by writing a generator script or dumping a full `.pcg` JSON to disk. If MCP is offline, start Vite + pcg-server and retry; do not switch to file authoring. Keep going until `FINAL_ACCEPTED` (or a true GAP / 12-cycle plateau / missing triview input)—do not stop at a white model or pipeline REWORK.

## Project Knowledge Base

PICG rules, experience notes, and Golden Graphs live in the project-local `.picg/` directory. `pcg-server` indexes them with BM25 through the `pcg_kb_*` tools. This project-local source takes precedence over the global Vault. The `user-vault-rag` MCP may be enabled, but it is only for reusable cross-project knowledge.

- Search rules and experience by default with `pcg_kb_search(query, top_k?, category?)`; category is `rules` or `kb`.
- List files with `pcg_kb_list(category?)`.
- Read a complete project note with `pcg_kb_get(path)`, using a path relative to `.picg/` such as `rules/graph-authoring/bridge.md`.
- Read templates with `pcg_golden_graph_list(class?)` and `pcg_golden_graph_get(name)`.
- Inspect or rebuild the index with `pcg_kb_status` and `pcg_kb_reindex`.
- Write project experience with the `pcg-kb-write` skill. Only genuinely cross-project knowledge belongs in the global Vault through `obsidian-write`.

Before graph authoring, search for the matching model rule plus `graph-contract` and `assembly-bevel`. For engineering work, read `rules/engineering/picg-development.md`. Use `pcg_golden_graph_*` for `.pcg` templates; do not mine `examples/**` or Unity demos for new authoring strategy.

## Retrieval routing

Use the following routing to prevent unrelated HMIRP or other-project knowledge from being applied to PICG:

| Question type | Use | Do not use |
| --- | --- | --- |
| PCG graph authoring, graph contracts, node behavior, Golden Graphs, `pcg-core`, Web, or Unity plugin work | `pcg_kb_search`, `pcg_kb_get`, `pcg_golden_graph_*` | Global `rule_search(domain="pcg")`; those rules have moved into `.picg/` |
| Reusable cross-project C#, Unity editor, rendering, performance, or collaboration knowledge | `vault_search` | Project-local notes as if they were global policy |
| HMIRP, FRP, Stable, HMICore, or URP-specific signals | Ignore in this project even if a global search returns them | HMIRP or rendering rules as PICG constraints |
| Unclear ownership | Search `pcg_kb_search` first, then `vault_search` only if needed and label the source | Unscoped global retrieval |

Constraints:

- Do not call global `rule_search(domain="pcg")` or unscoped `rule_search` in this repository.
- Do not treat HMIRP or unrelated rendering hits from `vault_search` as PICG constraints.
- When both stores cover the same topic, `.picg/` is authoritative for this repository. Use `pcg-kb-write` to close project-local gaps.

## PCG MCP Workflow

Use `pcg_patch_node` for one existing node and `pcg_apply_graph_ops` for a related atomic batch (add nodes, wire pins). Use `pcg_replace_graph` only for an intentional full-document replacement from root scope. Give every node and edge a unique ID, use explicit manifest-backed handles, and send the latest `graphHash` with every write.

When several editor pages are available, ask which page to use. On conflict, timeout, or stale state, refresh context and re-plan instead of replaying a write. For complete-asset jobs, `pcg_save_graph` after each coherent pass. Do not construct the graph by writing a generator script or a full `.pcg` file.

## Layout Gate (P0 — after every structural graph write)

Retrieving `pcg/graph-contract` is not enough; layout must be **executed**.

- Required orientation: Houdini-style **top-down**. Main chain shares the same X; row step ≈ 160; siblings on one row use X spacing ≥ 320. Left-to-right processing columns are a layout failure.
- After any `pcg_apply_graph_ops` / `pcg_replace_graph` that adds, removes, or rewires nodes (not a one-off property tweak), run `.agents/skills/shared/pcg-scripts/layout_pcg.py` on the saved `.pcg` (or an equivalent top-down move_node pass), then push positions back to the live editor and re-save when updating the named graph.
- Do not report the **job** complete until layout is top-down **and** live `pcg_validate` / `pcg_cook` / `pcg_capture_preview` have passed **and** (for web complete-asset skills) later stages reach `FINAL_ACCEPTED` or a legal stop.

## Validation and Communication

For multi-step work, start with one concise progress sentence. After a write, run `pcg_validate`, `pcg_cook`, and `pcg_capture_preview`; inspect the result against the request and iterate when evidence shows a mismatch. Finish with a concise summary of changes and validation. Explain blockers plainly and do not report success without tool evidence.
