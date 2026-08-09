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
| PATCH | `/v1/graph/nodes/:id` | optimistic-lock, undo-compatible patch queue |
| GET / POST | `/v1/graph/patches`, `/v1/graph/patches/ack` | Web patch delivery/acknowledgement |
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

Keep the Web editor open for live context and Preview capture. Available tools:

- `pcg_get_editor_context`, `pcg_get_node`, `pcg_list_nodes`
- `pcg_capture_preview`
- `pcg_patch_node` (pass `ifGraphHash`; Web applies the patch through Undo)
- `pcg_validate`, `pcg_cook`

Set `PCG_AGENT_TOKEN` to require the same bearer token on all Agent bridge REST
routes and `/mcp`. With no token, only the localhost listener is exposed and the
server logs a development-mode warning.

Validation:

```bash
python3 scripts/validate-agent-bridge.py
opencode mcp list
```

## Scope

Localhost Editor only. No Unity `DllImport` to PcgCore/PcgFbxExporter.
