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

URL: Project Settings → PICG, or **PCG → Server → Set Server URL…**

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
| GET | `/v1/preview/metadata` | metadata for the latest preview capture |
| POST | `/v1/preview/request-capture` | allocate a correlated capture request |
| POST | `/v1/camera/command` | queue a camera command for the editor |
| PUT / GET | `/v1/camera/state` | applied editor camera state |
| PATCH | `/v1/graph/nodes/:id` | legacy optimistic-lock node-patch entry point |
| GET / POST | `/v1/graph/patches`, `/v1/graph/patches/ack` | queued graph-command delivery and apply acknowledgement |
| POST | `/mcp` | MCP Streamable HTTP; SSE response via `Accept` |
| GET | `/v1/third-party/tripo/status` | Tripo configuration status and masked key hint; never returns the key |
| PUT / DELETE | `/v1/third-party/tripo/config` | store or clear a local Tripo API key |
| POST | `/v1/third-party/tripo/generate` | explicit image-to-3D generation with progress/result SSE |
| GET | `/v1/third-party/cache/:file` | read a cached GLB |
| POST | `/v1/reconstruct/oriented-sdf` | measure a source mesh into topology-free oriented samples |
| GET | `/v1/assets/preserved-gltf` | retrieve a workspace GLB for rig/animation preservation |
| GET | `/v1/kb/status`, `/v1/kb/list` | project-local knowledge-base status and file listing |
| POST | `/v1/kb/reindex` | rebuild the project-local knowledge index |
| GET / POST | `/v1/kb/search`, `/v1/kb/get` | search or read project-local notes |
| GET | `/v1/golden-graphs/list` | list reusable graph templates |
| GET / POST | `/v1/golden-graphs/get` | read a graph template |

The editor session endpoints synchronize the live graph, selection, camera, and preview. They are not conversation storage.

## 3D generation and credentials

The Web Settings dialog is dedicated to **3D Generation** API configuration.
Configure Tripo there, or supply `PCG_TRIPO_API_KEY` to `pcg-server`.
`PCG_TRIPO_BASE_URL` can override its API endpoint. An environment key takes
precedence over a stored key; clearing the stored key does not unset an
environment variable.

A `Tripo3DGenerator` node calls the cloud service only when the user explicitly
chooses **Generate** or **Regenerate**. The server uploads the image, creates
and polls the task, and caches the GLB under `library/web-cache/TripoCache/`.
Normal cooking and preview load that local result instead of starting another
cloud job. See [Third-party image-to-3D](third-party-image-to-3d.md).

The independent credential store defaults to
`~/Library/Application Support/PICG/credentials.json` when `HOME` is set.
Use `PCG_CREDENTIALS_PATH` to select an explicit file location, including for
isolated tests or managed installations. On POSIX systems, the store file uses
`0600` permissions and the application-owned directory uses `0700`; Windows
uses the permissions of the selected user directory. Writes replace the file
atomically and preserve unrelated credential entries.

On macOS, explicitly set `PCG_CREDENTIAL_STORE=keychain` to select Keychain;
`PCG_KEYCHAIN_SERVICE` can override its service identifier. Development builds
default to the protected file to avoid authorization prompts caused by changing
binary identities. Existing storage-location aliases remain supported for
credential compatibility. Neither the browser nor `.pcg` files persist API keys.

## External MCP clients

External clients connect directly to the native process—there is no stdio
child process or Node sidecar:

```json
{
  "mcpServers": {
    "pcg": {
      "url": "http://127.0.0.1:17890/mcp"
    }
  }
}
```

Configure AI accounts, models, and conversation behavior in the external
client. PICG supplies the graph tools and live editor connection, not the
client's model runtime.

Keep the Web editor open for graph authoring and live Preview capture. Available tools include:

- `pcg_get_editor_context`, `pcg_get_node`, `pcg_list_nodes`
- `pcg_get_graph`, `pcg_get_node_types`
- `pcg_patch_node`, `pcg_apply_graph_ops`
- `pcg_replace_graph`, `pcg_save_graph`
- `pcg_validate`, `pcg_cook`
- `pcg_capture_preview`, `pcg_set_camera`, `pcg_get_camera`
- `pcg_bake_oriented_sdf`, `pcg_get_component_bounds`, `pcg_solve_camera`, `pcg_validate_camera_frame`
- `pcg_kb_*`, `pcg_golden_graph_list`, `pcg_golden_graph_get`

The MCP surface supports complete graph creation, including nodes, edges,
parameters, positions, and inline Subgraphs. Use `pcg_get_node_types` instead
of inventing manifest property or pin names, then read `pcg_get_graph` before
writing.

Every graph write requires the latest `graphHash` as `ifGraphHash`. The server
queues one command at a time and returns only after the Web editor reports
whether it was actually applied. `pcg_apply_graph_ops` applies 1–500 operations
atomically as one Undo step; any invalid operation rejects the whole batch.
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

## Localhost authentication

Set `PCG_SERVER_TOKEN` to require a bearer token on editor-bridge REST routes,
Tripo routes, and `/mcp`. Existing token aliases remain supported for
compatibility. With no token, only the localhost listener is exposed and the
server logs a development-mode warning. This is not a remote-deployment
security model; see [SECURITY.md](../SECURITY.md).

Start both Vite and `pcg-server` with the same token environment. For accepted
localhost editor requests, the server-side Vite proxy supplies the token when
no explicit Authorization header is present; the token is not embedded in the
browser bundle. External MCP clients must send their own matching
`Authorization: Bearer …` header. Do not enter server tokens into a graph or
commit them to client configuration.

Protocol validation (against a fresh local server without a token):

```bash
python3 scripts/validate-agent-bridge.py --base http://127.0.0.1:17890
```

Despite its historical filename, this script exercises the editor REST/MCP
protocol: graph synchronization, optimistic locking, command acknowledgements,
cooking, and preview capture.

## Scope

Localhost Editor only. No Unity `DllImport` to PcgCore/PcgFbxExporter.
