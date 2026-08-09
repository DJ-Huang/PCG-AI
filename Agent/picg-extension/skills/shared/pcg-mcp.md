# PCG MCP live-editor contract

Use the `pcg` MCP as the fast control plane for the graph currently open in the PCG Web editor. It complements file authoring and deterministic target review; it does not replace either one.

## Scope and routing

| Need | Use | Reason |
|---|---|---|
| Inspect the live graph, current subgraph, selection, or Preview target | PCG MCP | Avoid re-reading or guessing editor state |
| Read a node or compact node inventory | PCG MCP | `pcg_get_node` / `pcg_list_nodes` return the active edit scope |
| Tune existing node data with Undo and conflict protection | PCG MCP | `pcg_patch_node` queues one undoable Web-editor action |
| Fast native validation, cook metrics, or current viewport capture | PCG MCP | Operates on the in-memory graph without shell plumbing |
| Add/delete nodes, change edges, author Subgraphs, or perform bulk layout | `.pcg` file + shared scripts | The current MCP has no structural graph-edit tools |
| Prove manifest/pin/layout correctness of the saved deliverable | `validate_pcg.py` | MCP validation covers the live full graph, not all authoring/style checks |
| Final Unity acceptance | Tuanjie/Unity MCP clean scene | The Web viewport is not Unity runtime evidence |
| Final Web acceptance of a saved graph | Vite `/review` + Playwright | Stable clean page, fixed file input, reproducible screenshot |

## Mandatory handshake

Call `pcg_get_editor_context` before every MCP batch. Treat its response as a snapshot, not durable state.

Proceed only when `ok=true`, `online=true`, and the session has a `graphHash`. Compare `session.graphPath` with the intended graph:

- Exact target: live reads, validation, cook, capture, and intentional parameter patches are allowed.
- Different or ambiguous target: do not patch. Continue through the saved-file workflow, or ask the user to open the target only when live editing is required.
- Offline: use the saved-file workflow. For Web final review, follow `web-review.md` server rules; for Unity final review, follow `unity-review.md`.

Never start or restart a server merely because the MCP client is absent. Report the missing configured capability and use the documented safe fallback when it satisfies the task.

## Efficient read and edit loop

1. Call `pcg_get_editor_context` once and retain `graphPath`, `editPath`, selection, Preview target, and `graphHash` for the batch.
2. Use `pcg_get_node` when the selected or known node is sufficient. Use `pcg_list_nodes` only for inventory or when the target id is unknown.
3. Before changing an existing node, confirm its current data with `pcg_get_node`.
4. Call `pcg_patch_node` with only the node data properties to merge, not a nested `{ "data": ... }` wrapper. Always pass the fresh context hash as `ifGraphHash`.
5. An `accepted` patch is queued, not proven applied. Re-read context/node until the node reflects the value and the graph hash advances. If it does not, report a pending/unapplied patch rather than claiming success.
6. On `graph_conflict`, fetch new context and node state, reconsider the intended delta, then issue a newly justified patch. Never blind-retry an old patch.
7. Run `pcg_validate`, then `pcg_cook`. Use a fixed seed for comparisons and record cook counts/timings. Call `pcg_capture_preview` only with the Web editor and Preview panel open.

Do not patch an unrelated live graph to make it resemble a file being authored elsewhere. Do not use MCP parameter patching for large mechanical rewrites that are clearer and reviewable as a file diff.

## Validation layers

Use all layers that apply; one green layer does not imply the next:

```text
saved-file validate_pcg.py
→ live pcg_validate
→ live pcg_cook with fixed seed(s)
→ live pcg_capture_preview (rapid visual feedback)
→ target-specific clean final review (Unity scene or Web /review)
```

For Dev variants, repeat `pcg_cook` across the gate's required seeds and boundary parameter sets when those states can be represented safely in the live graph. Restore temporary parameter values through normal undo or explicit patches, and verify the final hash/state. If the live target does not match, use the deterministic file/HTTP gate instead.

## Error routing

| Result | Action |
|---|---|
| `editor_offline` | Fall back to saved-file work; do not claim a live MCP pass |
| `no_node_selected` / `node_not_found` | Use context + `pcg_list_nodes`; do not guess an id |
| `graph_conflict` | Refresh context/node and recompute the delta |
| `preview_timeout` | Keep the Web editor and Preview panel open, then retry once; otherwise use the clean review route |
| validation/cook error | Fix the earliest graph error; do not continue to visual scoring |
| cook transport error | Check pcg-server health and decode through the documented saved-file tools |

## Receipt

Record a compact line whenever MCP contributes evidence:

```text
PCG MCP: graph=<path> hash=<short> scope=<root|subgraph> validate=<PASS|FAIL|SKIP> cook=<PASS|FAIL|SKIP> seed=<n|-> preview=<PASS|FAIL|SKIP> patch=<applied|pending|none>
```

The receipt is evidence, not an acceptance label. Final acceptance still comes from the target-specific review contract.
