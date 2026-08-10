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

## Built-in LLM Agent

The left Agent panel talks directly to the embedded C++ runtime in
`pcg-server`; it does not start OpenCode, Node, or another sidecar. Provider
accounts are managed from the app-level **Settings → AI Providers** page; the
Agent panel only selects connected Providers and models. Open Settings from
the main toolbar or with `Cmd/Ctrl+,`.

- API Key: OpenAI, Anthropic, Google Gemini, OpenRouter, Kimi for Coding, or an
  OpenAI-compatible HTTPS endpoint (loopback HTTP is allowed for local models).
- OAuth: ChatGPT Plus/Pro, GitHub Copilot, or xAI, when a PCG-AI-owned OAuth
  Client ID is configured on the server.

API keys and OAuth refresh tokens are submitted only to localhost and stored
as Generic Password entries in macOS Keychain under the `PCG-AI Agent`
service. The page stores neither credential and the Provider API returns only
connection state plus masked account metadata.

For Kimi Coding, select **Kimi for Coding** in Settings and paste the Kimi
Coding API Key. Its endpoint is fixed to `https://api.kimi.com/coding/v1` and
uses the Anthropic Messages protocol, so no Base URL is requested. The Key is
validated against Kimi's model catalog before it is stored.

Turns stream ordered Provider reasoning, Markdown text, tool calls, complete
tool results, approvals, and errors using SSE. Read, validate, cook, and capture
tools execute automatically. Node/graph writes, full replacement, and save
pause on an approval card that shows the exact tool and arguments. Approval is
bound to the Turn, call ID, and current graph hash, can execute only once, and
fails with `graph_conflict` if the graph changed while waiting.

Supported attachments are PNG/JPEG and UTF-8 `.txt`, `.json`, or `.pcg` files.
The panel blocks images before sending when the selected model lacks image
input capability.

The clock button opens durable local chat history with search, date groups,
rename, deletion, and paged loading. Sessions and attachments survive a page
refresh and `pcg-server` restart. **Settings → General** controls whether
Provider-returned reasoning is shown; models that do not return reasoning do
not receive a fabricated Thinking section. Failed and interrupted Turns can be
retried in place without duplicating the user message.

Web Agent tests:

```bash
npm run test:agent
```
