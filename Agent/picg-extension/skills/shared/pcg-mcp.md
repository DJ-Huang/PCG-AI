# PCG MCP live-editor contract

The **open Web editor page** is the authoring surface. Use the `pcg` MCP to bind to that page, create/edit nodes, wire pins, cook, capture Preview, and save. Do not author by writing a `.pcg` JSON file or a Python/JS generator on disk.

Saved-file `validate_pcg.py` and the Vite `/review` route remain independent **acceptance** layers after MCP save. They are not the place to build the graph.

## Scope and routing

| Need | Use | Reason |
|---|---|---|
| Inspect the live graph, current subgraph, selection, or Preview target | PCG MCP | Avoid re-reading or guessing editor state |
| Read a node or compact node inventory | PCG MCP | `pcg_get_node` / `pcg_list_nodes` return the active edit scope |
| Discover types, pins, properties, defaults, and ranges | `pcg_get_node_types` | Uses the same live manifest as the Web Inspector |
| Read the full document including edges, parameters, and Subgraphs | `pcg_get_graph` | Required before structural or whole-document edits |
| Tune one existing node | `pcg_patch_node` | One undoable action with apply acknowledgement |
| Add/delete/move/patch nodes, add/delete edges, edit parameters | `pcg_apply_graph_ops` | Atomic batch in the current root/Subgraph scope; one Undo step |
| Create or replace a complete graph with Subgraphs | `pcg_replace_graph` | Native pre-validation plus atomic full-document replacement |
| Persist the authored graph | `pcg_save_graph` | Saves the full live document to a workspace-relative `.pcg` path |
| Fast native validation, cook metrics, or current viewport capture | PCG MCP | Rapid feedback only; not a substitute for saved `/review` ortho cameras |
| Prove manifest/pin/layout correctness of the saved deliverable | `validate_pcg.py` | MCP validation covers the live full graph, not all authoring/style checks |
| Final Unity acceptance | Tuanjie/Unity MCP clean scene | The Web viewport is not Unity runtime evidence |
| Final Web acceptance of a saved graph | Vite `/review` + Playwright `front/side/top/three-quarter` | Stable clean page, canvas capture, per-view comparison |

## Open the Web page, then MCP (P0)

Authoring starts on a live editor tab. Sequence:

1. Ensure Vite (`http://127.0.0.1:5173`) and pcg-server (`http://127.0.0.1:17890`) are up. If `check_server.py` fails, start them with Shell `block_until_ms: 0` (`cd web/pcg-editor && npm run dev` and `./scripts/run-pcg-server.sh`). Do not use `nohup`/`&`.
2. Call `pcg_get_editor_context` (and `pcg_kb_*` as needed). The MCP session is the open page.
3. If `editor_offline` / MCP discovery error: start or foreground the Web editor, retry context. Do **not** switch to dumping a `.pcg` file.
4. Several editor pages: ask which `editorSessionId` / `graphPath` to use, then pass that id on later calls.
5. Empty or unrelated live graph: stay on that page. Use `pcg_replace_graph` or `pcg_apply_graph_ops` to build the intended document in the editor, then `pcg_save_graph` to the AssetSpec path.

There is no separate “write graph as a script” path. Nodes and edges exist when the Web editor apply acknowledgement returns.

## Mandatory handshake

Call `pcg_get_editor_context` before every MCP batch. Treat its response as a snapshot, not durable state.

Proceed only when `ok=true`, `online=true`, and the session has a `graphHash`. Compare `session.graphPath` with the intended graph:

- Exact target: full MCP authoring, validation, cook, capture, and save are allowed.
- Different or ambiguous target: do not write an unrelated page. Ask which page, or replace the current document only when the user asked to create a new graph on whatever is open.
- Offline: recover the editor/server, then retry MCP. File/HTTP cook is only for **saved-deliverable** checks after `pcg_save_graph`, never for constructing topology.

## Efficient read and edit loop

1. Call `pcg_get_editor_context`; retain `graphPath`, `editPath`, selection, Preview target, and `graphHash` for the batch.
2. Call `pcg_get_node_types` for the exact candidate types before creating nodes. Never invent type/property/pin ids.
3. Call `pcg_get_graph` before structural work. Use `pcg_get_node` only for focused diagnosis and `pcg_list_nodes` for compact scope inventory.
4. Prefer one `pcg_apply_graph_ops` batch for a coherent pass. Use explicit, stable node/edge ids and explicit edge handles. The final batch state must be valid; any failed operation rejects the whole batch.
5. Use `pcg_patch_node` for a single small data change. Pass data properties directly, not a nested `{ "data": ... }` wrapper.
6. Use `pcg_replace_graph` from root scope when creating from scratch or replacing Subgraph definitions/ports. Preserve version, parameters, positions, titles, and explicit pin ids.
7. Every write requires the fresh `graphHash` as `ifGraphHash` and returns only after the Web editor acknowledges apply. After success, call context again and use the new hash for the next write. On `graph_conflict`, re-read graph state and recompute the delta; never blind-retry.
8. Run `pcg_validate`, then `pcg_cook` with a fixed seed. Refine through MCP and capture current Preview as rapid feedback.
9. Call `pcg_save_graph` with the latest hash. Then run saved-file `validate_pcg.py`; final visual acceptance still uses the target-specific clean review.

Do not write an unrelated live graph. Prefer `pcg_apply_graph_ops` (add/delete/move/patch nodes, add/delete edges) so the canvas shows real nodes and wires per pass. Use `pcg_replace_graph` from root when creating from scratch or replacing Subgraph definitions. Do not emit a one-off `_author.py` / graph-builder script and paste JSON onto disk.

Forbidden: generating the product `.pcg` with a custom script, hand-writing the full document in the agent then `Write` to disk as the authoring method, or asking the user to “open the file later.”

Required: explicit node ids, `__nodeTitle`, manifest pin handles on every edge, `ifGraphHash` on every write, `applied=true` before the next batch.

## Validation layers

Use all layers that apply; one green layer does not imply the next:

```text
pcg_get_node_types + pcg_get_graph
→ atomic MCP authoring + pcg_validate
→ live pcg_cook with fixed seed(s)
→ live pcg_capture_preview (rapid visual feedback)
→ pcg_save_graph + saved-file validate_pcg.py
→ target-specific clean final review (Unity scene or Web /review)
```

For Dev variants, repeat `pcg_cook` across the gate's required seeds and boundary parameter sets when those states can be represented safely in the live graph. Restore temporary parameter values through normal undo or explicit patches, and verify the final hash/state. If the live target does not match, use the deterministic file/HTTP gate instead.

## Error routing

| Result | Action |
|---|---|
| `editor_offline` | Start/foreground Vite editor + pcg-server, retry `pcg_get_editor_context`. Do not author on disk. Report blocked only if the editor still cannot come online |
| `no_node_selected` / `node_not_found` | Use context + `pcg_list_nodes`; do not guess an id |
| `graph_conflict` | Refresh context/full graph and recompute the delta |
| `root_scope_required` | Navigate the Web editor to root before whole-document replacement |
| `apply_timeout` with `cancelled=true` | Re-open/foreground the Web editor, refresh context, then retry with a new hash |
| `apply_timeout` with `cancelled=false` | Refresh graph first; the editor may have fetched the command |
| `preview_timeout` | Keep the Web editor and Preview panel open, then retry once; otherwise use the clean review route |
| validation/cook error | Fix the earliest graph error; do not continue to visual scoring |
| cook transport error | Check pcg-server health and decode through the documented saved-file tools |

## Receipt

Record a compact line whenever MCP contributes evidence:

```text
PCG MCP: graph=<path> hash=<short> scope=<root|subgraph> ops=<n|replace|patch|none> apply=<PASS|FAIL|SKIP> validate=<PASS|FAIL|SKIP> cook=<PASS|FAIL|SKIP> seed=<n|-> preview=<PASS|FAIL|SKIP> save=<PASS|FAIL|SKIP>
```

The receipt is evidence, not an acceptance label. Final acceptance still comes from the target-specific review contract.
