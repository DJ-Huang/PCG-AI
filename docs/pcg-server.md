# pcg-server — localhost C++ backend (Unity has no native plugins)

Unity Editor talks to C++ only over HTTP. `PcgCore` / `PcgFbxExporter` dylib/dll are **not** loaded in Unity.

## Quick start

```bash
./scripts/build-pcg-server.sh
./scripts/run-pcg-server.sh   # stops any old listener on :17890, then starts
```

Or together:

```bash
./scripts/build-pcg-server.sh --run
./scripts/run-pcg-web.sh      # always replaces pcg-server; reuses Vite if already up
```

Stop server only:

```bash
./scripts/stop-pcg-server.sh
```

Default: `http://127.0.0.1:17890`

```bash
curl -s http://127.0.0.1:17890/v1/health
./scripts/verify-pcg-server.sh
```

## Unity

1. Start `pcg-server` before cooking.
2. **PCG → Server → Health Check**
3. Cook / Export FBX as usual — both hit the server.

URL: Project Settings → PCG AI, or **PCG → Server → Set Server URL…**

## API

| Method | Path | Notes |
|--------|------|-------|
| GET | `/v1/health` | version plus MCP/editor-bridge status |
| POST | `/v1/cook` | multipart → cook result binary |
| POST | `/v1/cancel` | best-effort cancel |
| POST | `/v1/validate` | graph JSON body |
| POST | `/v1/cache/clear` | clear server cook cache |
| POST | `/v1/export-fbx` | multipart geometry → FBX bytes |
| PUT / GET | `/v1/session` | Web in-memory graph and editor context |
| POST | `/v1/session/heartbeat` | lightweight editor liveness heartbeat |
| PUT / GET | `/v1/preview/screenshot` | live WebGL PNG upload/download |
| POST | `/v1/preview/request-capture` | allocate a correlated capture request |
| PATCH | `/v1/graph/nodes/:id` | legacy optimistic-lock node-patch entry point |
| GET / POST | `/v1/graph/patches`, `/v1/graph/patches/ack` | queued graph-command delivery and apply acknowledgement |
| POST | `/mcp` | MCP Streamable HTTP; SSE response via `Accept` |

## External Agent MCP

Cursor and OpenCode connect directly to the same native process—there is no
stdio child process or Node sidecar:

```json
{
  "mcpServers": {
    "pcg": {
      "url": "http://127.0.0.1:17890/mcp"
    }
  }
}
```

Keep the Web editor open for graph authoring and live Preview capture. Available tools:

- `pcg_get_editor_context`, `pcg_get_node`, `pcg_list_nodes`
- `pcg_get_graph`, `pcg_get_node_types`
- `pcg_patch_node`, `pcg_apply_graph_ops`
- `pcg_replace_graph`, `pcg_save_graph`
- `pcg_validate`, `pcg_cook`
- `pcg_capture_preview`

The MCP surface supports complete graph creation, including nodes, edges,
parameters, positions, and inline Subgraphs. Use `pcg_get_node_types` instead
of inventing manifest property or pin names, then read `pcg_get_graph` before
writing.

Every write requires the latest `graphHash` as `ifGraphHash`. The server queues
one command at a time and returns only after the Web editor reports whether it
was actually applied. `pcg_apply_graph_ops` applies 1–500 operations atomically
as one Undo step; any invalid operation rejects the whole batch.
`pcg_replace_graph` is root-scope only and validates the complete graph before
queueing. `pcg_save_graph` accepts only workspace-relative `.pcg` paths and
rejects absolute paths, parent traversal, and workspace escapes.

A normal creation loop is:

```text
context → node types + full graph → atomic ops or full replacement
→ context (fresh hash) → validate → cook → capture
→ context (fresh hash) → save → saved-file validation/final review
```

`graph_conflict` means the editor state changed: re-read and recompute the
operation. An `apply_timeout` response reports whether the still-pending
command was cancelled; if it may already have been fetched, refresh the graph
before deciding whether to retry.

Set `PCG_AGENT_TOKEN` to require the same bearer token on all Agent bridge REST
routes and `/mcp`. With no token, only the localhost listener is exposed and the
server logs a development-mode warning.

Protocol validation:

```bash
python3 scripts/validate-agent-bridge.py --base http://127.0.0.1:17890
opencode mcp list
```

## Scope

Localhost Editor only. No Unity `DllImport` to PcgCore/PcgFbxExporter.
