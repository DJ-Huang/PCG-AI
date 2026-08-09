# PCG Web Editor

The Web editor is the live authoring client for `pcg-server`. Start the native
server first, then Vite:

```bash
./scripts/run-pcg-server.sh
./scripts/run-pcg-web.sh
```

## External Agent bridge

While the editor is open it publishes the complete in-memory graph, current
subgraph path, selection, preview target, and a SHA-256 graph hash to
`pcg-server`. It also applies queued Agent patches through the normal Web undo
stack and answers on-demand Preview capture requests from the live WebGL canvas.

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

Available tools are `pcg_get_editor_context`, `pcg_get_node`,
`pcg_list_nodes`, `pcg_capture_preview`, `pcg_patch_node`, `pcg_validate`, and
`pcg_cook`. Use the `graphHash` returned by context as `ifGraphHash` when
patching; stale writes return `graph_conflict` instead of overwriting edits.

When `PCG_AGENT_TOKEN` is set, REST and MCP bridge calls require
`Authorization: Bearer <token>`. With no token the localhost bridge runs in
development mode and prints a warning once.

Run the protocol integration test against a running server with:

```bash
python3 scripts/validate-agent-bridge.py
```

The existing `/review?graph=...` Playwright flow remains the deterministic,
saved-file review path. MCP Preview capture is intentionally the live editor
path: it preserves the user's current camera and shading state.
