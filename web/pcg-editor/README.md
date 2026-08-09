# PCG Web Editor

The Web editor is the live authoring client for `pcg-server`. Start the native
server first, then Vite:

```bash
./scripts/run-pcg-server.sh
./scripts/run-pcg-web.sh
```

## External Agent bridge

While the editor is open it publishes the complete in-memory graph, current
subgraph path, selection, preview target, node manifest, and a SHA-256 graph
hash to `pcg-server`. It applies queued Agent graph commands through the normal
Web undo stack and answers on-demand Preview capture requests from the live
WebGL canvas.

The MCP endpoint is built into the same native process and port:

```json
{
  "mcpServers": {
    "pcg": {
      "url": "http://127.0.0.1:17890/mcp"
    }
  }
}
```

The MCP tools cover the whole authoring loop:

- Inspect: `pcg_get_editor_context`, `pcg_get_node`, `pcg_list_nodes`,
  `pcg_get_graph`, `pcg_get_node_types`
- Author: `pcg_patch_node`, `pcg_apply_graph_ops`, `pcg_replace_graph`,
  `pcg_save_graph`
- Verify: `pcg_validate`, `pcg_cook`, `pcg_capture_preview`

Use the `graphHash` returned by context as `ifGraphHash` for every write; stale
writes return `graph_conflict` instead of overwriting edits. Atomic graph-op
batches are one Undo step and either apply completely or leave the graph
unchanged. Full replacement must run at root scope. Save paths must be
workspace-relative `.pcg` paths.

Write calls wait for an apply acknowledgement from this editor. A response with
`accepted=true` but `applied=false` is not a completed edit; follow its timeout
and cancellation fields, then refresh context before retrying.

When `PCG_AGENT_TOKEN` is set, REST and MCP bridge calls require
`Authorization: Bearer <token>`. With no token the localhost bridge runs in
development mode and prints a warning once.

Run the protocol integration test against a running server with:

```bash
python3 scripts/validate-agent-bridge.py
npm run test:graph-commands
```

The existing `/review?graph=...` Playwright flow remains the deterministic,
saved-file review path. MCP Preview capture is intentionally the live editor
path: it preserves the user's current camera and shading state.
