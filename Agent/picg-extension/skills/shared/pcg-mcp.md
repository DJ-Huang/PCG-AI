# PCG MCP live-editor contract

Use the `pcg` MCP as the primary authoring and inspection control plane for the graph currently open in the PCG Web editor. Saved-file validation and target-specific final review remain independent acceptance layers.

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
| Fast native validation, cook metrics, or current viewport capture | PCG MCP | Operates on the in-memory graph without shell plumbing |
| Prove manifest/pin/layout correctness of the saved deliverable | `validate_pcg.py` | MCP validation covers the live full graph, not all authoring/style checks |
| Final Unity acceptance | Tuanjie/Unity MCP clean scene | The Web viewport is not Unity runtime evidence |
| Final Web acceptance of a saved graph | Vite `/review` + Playwright | Stable clean page, fixed file input, reproducible screenshot |

## Mandatory handshake

Call `pcg_get_editor_context` before every MCP batch. Treat its response as a snapshot, not durable state.

Proceed only when `ok=true`, `online=true`, and the session has a `graphHash`. Compare `session.graphPath` with the intended graph:

- Exact target: full MCP authoring, validation, cook, capture, and save are allowed.
- Different or ambiguous target: do not write. Ask the user to open the target when live authoring is required, or use the saved-file compatibility workflow.
- Offline: use the saved-file workflow. For Web final review, follow `web-review.md` server rules; for Unity final review, follow `unity-review.md`.

Never start or restart a server merely because the MCP client is absent. Report the missing configured capability and use the documented safe fallback when it satisfies the task.

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

Do not write an unrelated live graph. Use whole-document replacement only when the full intended graph is known; use granular ops for reviewable incremental passes.

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
| `editor_offline` | Fall back to saved-file work; do not claim a live MCP pass |
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
