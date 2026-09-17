# Live Web-editor MCP contract

Use this when reading or changing a graph in the open Web editor. It does not require a live editor for repository documentation or code maintenance.

## Target and state

Start with `pcg_list_editor_sessions` unless the user already supplied the full target ID. Show each candidate's visible `sessionLabel`, full `editorSessionId`, `graphPath` (or Untitled), `pageUrl` and approval state. Ask the user to choose; never infer a target from a filename, ordering, recency, focus, or the fact that only one candidate exists. A user-supplied full ID or an explicit choice from the displayed candidates is sufficient; do not ask again on every tool call.

The user must enable **Allow AI control** in the intended page's MCP panel. They can use **Copy AI target** to paste an unambiguous choice into chat. Do not manufacture consent with a tool argument, enable it through HTTP/browser automation, or switch to another already-approved window. Listing is not selection or approval.

Read `pcg_get_editor_context` with the chosen full `editorSessionId`. Confirm `ok`, `online`, approval, `graphPath`, `editPath` and current `graphHash`; echo the selected label, ID and path to the user before authoring. Include that same ID on every live read, write, camera, cook and capture call. Inspect returned `target` receipts. On unavailability or changed consent, stop and reconfirm rather than choosing a replacement. Refreshing a page creates a new unapproved ID. Permission to control a window is not permission to overwrite unrelated or unsaved work.

Use `pcg_get_node` / `pcg_list_nodes` for focused inspection, `pcg_get_graph` for structural work, and `pcg_get_node_types` for exact type/property/pin schemas. Reuse unchanged reads within the operation; refresh after a write, conflict, reconnect or relevant external change.

**Large payloads:** a graph response may contain `largePayloadsRedacted` and `redactedFields`. It is not a complete replacement document. Preserve omitted data with targeted patches/ops; never round-trip a redacted snapshot through `pcg_replace_graph`. Follow the returned redaction hint for payload-specific operations.

## Write safely

| Operation | Tool and contract |
| --- | --- |
| One node's data | `pcg_patch_node`; pass properties directly, not an extra `data` wrapper. |
| Related topology, position or parameter changes | `pcg_apply_graph_ops`; one atomic batch in the current root/Subgraph scope, explicit stable IDs and manifest handles. |
| Intentional full-document/Subgraph-definition replacement | `pcg_replace_graph`; root scope, complete unredacted input, and explicit intent to replace the target. |
| Persist the live document | `pcg_save_graph`; the intended workspace-relative `.pcg` path. |

Pass the latest `graphHash` as `ifGraphHash` on writes. Require the tool's successful apply acknowledgement (`applied=true` for graph mutations); then obtain the updated state/hash before the next mutation. On `graph_conflict`, reread and recompute the delta instead of replaying it. Keep batches coherent and reviewable rather than forcing one tool call per node.

For a live-editor task, nodes and wires must be applied on the canvas through MCP. A generated file, HTTP cook, or saved-file screenshot cannot prove that the live document changed. Preserve the user's chosen authoring surface; report an unavailable live edit honestly.

## Validate the layer you changed

For topology/data changes: `pcg_validate`, then relevant `pcg_cook` with a recorded seed; inspect `pcg_capture_preview` for visible effects and save. Position-only edits need layout/state verification, not needless geometry regeneration. Saved output additionally uses static validation and, when visual acceptance is requested, the platform's clean review route or Unity scene.

Live Preview is rapid feedback. It is not evidence that the exported file or Unity prefab reloads correctly. Generator tests must restore temporary parameter/seed changes and verify the intended final state before saving.

## Recovery

| Result | Response |
| --- | --- |
| `editor_session_required` | Show the candidates and obtain the user's choice, even for one candidate. |
| `editor_session_confirmation_required` | Ask the user to enable control in their chosen visible page. Do not bypass the UI. |
| `editor_session_unavailable` / `editor_session_binding_mismatch` / `editor_control_revoked` | Stop; retain the selected ID, refresh discovery/context and reconfirm with the user. Never fall back to another window or replay queued work. |
| `editor_offline` | Check Vite and pcg-server; start/reconnect them using the host's supported persistent process mechanism when permitted. Retry context; otherwise report the live operation blocked. |
| `node_not_found` / `no_node_selected` | Refresh context and inspect the relevant scope. |
| `root_scope_required` | Return the editor to root scope before replacement. |
| `apply_timeout` | Inspect cancellation status and refresh graph state; a timed-out command may have applied. Never blindly replay it. |
| `preview_timeout` | Foreground the relevant Preview and retry when useful; distinguish failed capture from failed cook. |
| Validation/cook failure | Repair the responsible graph or runtime issue, then rerun affected checks. |

Default local services are Vite `http://127.0.0.1:5173` and pcg-server `http://127.0.0.1:17890`; use discovered configuration rather than assuming those ports. Diagnose health after a transport failure, not before every successful call. Record target/path, relevant state hash, changed scope, checks and save result; exact receipt formatting is optional.
