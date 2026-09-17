# User-owned MCP session targeting

## Choose the window, not a default

Open the intended Web editor and expand the **MCP · <short ID>** control beside the brand. Check the document path and full session ID, enable **Allow AI control of this window**, and wait for **AI control allowed**. Use **Copy AI target** and paste the instruction into the AI conversation. The browser tab title and MCP panel show the same short label; the full ID remains the routing identity. Opening the displayed page URL in a new tab creates a *different* session, so a URL is a locator hint, not a session selector.

Alternatively, ask the AI to list sessions, then explicitly select a candidate by the visible label/ID and enable control in that window. Selecting once is enough for the task: the AI retains the chosen full ID on every subsequent tool call. A file name, foreground status, list order, or one remaining window is never an implicit choice.

The panel distinguishes permission, server availability and last MCP access. **Allowed** does not claim that an AI client is connected. No approval is stored in localStorage, sessionStorage, or the PCG document. A full page refresh creates a new ID with control off. Different tabs, including duplicate unsaved graphs, have independent permissions.

## Contract

`pcg_list_editor_sessions` returns candidates and never selects one. All live tools require a full, explicit `editorSessionId` that resolves to an online, approved page. For compatibility, calling `pcg_get_editor_context` without an ID still returns a useful selection-required error with candidates, even if only one exists. It never returns a default document.

Live graph reads/writes, node schemas, validation/cook, camera and capture share the check. Successful dispatches include a `target` receipt in structured content and a text block: full ID, visible label, page URL, consent revision, and document path **at the start** of the call. The document path may change during Save As, but the page ID does not. Static MCP tool discovery and knowledge-base/golden-graph tools do not need an editor.

The page uploads `aiControlEnabled` and a monotonically increasing `aiControlRevision` with its normal session snapshot. Missing approval fields default to disabled. Existing client-revision ordering prevents an older snapshot from overwriting a newer one. Queued graph commands carry the consent revision; the browser rechecks current local permission immediately before applying graph, camera or capture work. A per-call native scope also pins the consent revision so a long-running bake cannot enqueue into a later grant.

Revocation cancels queued graph work and outstanding camera/capture generations; old work is not replayed after re-enabling. Late acknowledgements of cancelled graph commands remain safe. Already-applying operations cannot be rolled back by this switch: inspect the graph/context before retrying. Ordinary session upload, heartbeat, screenshot upload and acknowledgements continue to function without AI approval. A server-instance marker resets browser command cursors after a server restart.

This is an accidental-targeting and consent UX boundary, **not authentication against an actor already holding the server's HTTP credentials**. There is intentionally no MCP tool or model-supplied confirmation flag that grants approval. Skills must not bypass the page UI through HTTP or browser automation. Multiple approved windows remain independently usable; the server does not create a process-global default that could reroute another AI conversation.

## Migration

Rebuild/restart `pcg-server` and refresh the Web editor together. Reconnect the MCP client to refresh its tool schemas and load the updated shared skill. Old editor builds that do not send approval metadata cannot be controlled by live MCP operations. API integrations must supply the selected full ID; omission is no longer a single-window shortcut. User approval applies to the window, not blanket permission to overwrite unrelated or unsaved content.

Errors are actionable: `editor_session_required` asks for selection; `editor_session_confirmation_required` asks for in-page approval; `editor_session_unavailable` means the chosen page is offline/missing; `editor_control_revoked` means consent changed during work. None permits fallback to another window.

## Verification

Run the dependency-light checks from the repository root:

```sh
node scripts/test-mcp-session-control.cjs
c++ -std=c++17 -Wall -Wextra -Werror -pthread -Ipcg-server/src pcg-server/tests/test_editor_control.cpp -o /tmp/test-editor-control
/tmp/test-editor-control
```

The Node suite strictly compiles and executes the production consent store (using the editor's TypeScript dependency when installed, otherwise `tsc` on PATH). The standalone C++ suite exercises exact-ID/approval checks, revoke/regrant, nested invocation scopes and HTTP-worker isolation. Neither substitutes for compiling the full application.

With a freshly started, **isolated test server** and no editor tabs, run `python scripts/validate-agent-bridge.py --base http://127.0.0.1:17890`. This suite simulates editor uploads/acknowledgements and verifies zero/one/two-session selection, approval rejection, explicit routing/receipts, revocation and the existing graph/preview protocol. It is not a rendered-browser test. Use the normal native and Web builds as well (`cmake --build <server-build-directory>` and `npm --prefix web/pcg-editor run build`).

Before merging, manually open two editor windows, including two Untitled documents. Confirm distinct tab labels/IDs, approve only B, and have the AI display the choices and echo B before a small graph edit. Verify A cannot be read or changed by live MCP and only B's graph/camera/preview changes. Disable B while a command is queued; rapidly re-enable it and confirm no old command runs. Close B, allow its heartbeat to expire, and verify calls to B fail instead of touching A. Refresh B and verify a new unapproved ID; restart the server and verify the still-open page can synchronize its new command counters. Check keyboard/clipboard fallback and a narrow viewport as well.
