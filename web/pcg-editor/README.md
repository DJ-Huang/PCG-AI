# PCG Web Editor

The Web editor is the browser authoring client for `pcg-server`.

## Requirements

- Node.js `^20.19.0` or `>=22.12.0`
- A running `pcg-server` at `http://127.0.0.1:17890`

## Run

From the repository root:

```bash
./scripts/run-pcg-web.sh
```

Or run the Web application separately:

```bash
cd web/pcg-editor
npm ci
npm run dev
```

Open `http://127.0.0.1:5173`.

## Commands

```bash
npm run dev                  # development server
npm run lint                 # oxlint
npm run build                # TypeScript + production bundle
npx vitest run               # complete unit/component suite
npx vitest run src/SettingsDialog.test.tsx localProxy.test.ts  # settings and local proxy
npm run test:graph-commands  # graph command protocol validation
npm run test:library         # built-in library consistency
```

`node_modules/` and `dist/` are generated and must not be committed.

## Review route

The deterministic review page loads a repository-relative graph path:

```text
http://127.0.0.1:5173/review?graph=examples/graphs/stone-arch-bridge.pcg
```

Use repository-relative graph paths. The review route uses the local cook server and supports fixed cameras, preview quality controls, parameter overrides and GLB export.

## Unity handoff

Save or download a `.pcg` graph from the File menu, then import it into Unity or select its local path with **PCG → Set Watched Graph…**. `schema/editor-export.pcg` can be used as an ignored local handoff file; it is not a versioned asset.

## External MCP clients

While the editor is open it publishes the in-memory graph, selection, subgraph scope, preview target, node manifest and graph hash to `pcg-server`.

```json
{
  "mcpServers": {
    "pcg": {
      "url": "http://127.0.0.1:17890/mcp"
    }
  }
}
```

Configure model accounts and AI conversations in your external MCP client. The Web editor provides the visual graph and viewport; the external client invokes the server's tools.

Use the latest `graphHash` for every write. Graph operations are applied through the normal undo stack; a stale hash returns `graph_conflict` rather than overwriting editor changes. See [the server guide](../../docs/pcg-server.md#external-mcp-clients) for tools and authentication.

## 3D generation settings

Open Settings from the gear button or with **Ctrl/Cmd + ,**. The dialog contains **3D Generation** API configuration, currently for Tripo.

Save or clear the Tripo API key there. The key is held by the local server's protected credential store, never persisted in the browser, graph, or repository. `PCG_TRIPO_API_KEY` can supply the key through the server environment and takes precedence over a stored key.

Add a `Tripo3DGenerator` node, assign a source image, and choose **Generate** in the Inspector. The resulting GLB is cached and its path is stored on the node. Cooking and preview load the local result without starting a cloud job. Continue procedural editing manually or through an external MCP client; generation does not automatically start a reconstruction/review conversation.

See [Third-party image-to-3D](../../docs/third-party-image-to-3d.md) for generation, cache and credential boundaries.

## Examples

Repository examples are kept in `examples/`, not under `web/`. See [examples/README.md](../../examples/README.md) for the catalog and review URLs.
