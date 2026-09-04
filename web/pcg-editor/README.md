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
npm run test:agent           # embedded Agent-focused tests
npm run test:graph-commands  # graph command protocol validation
npm run test:library         # built-in library consistency
```

`node_modules/` and `dist/` are generated and must not be committed.

## Review route

The deterministic review page loads a repository-relative graph path:

```text
http://127.0.0.1:5173/review?graph=examples/graphs/bridge-demo.pcg
```

The development server resolves the path from the repository root and rejects paths outside it. The review route uses the local cook server and supports fixed cameras, preview quality controls, parameter overrides and GLB export.

## Unity handoff

**Send to Unity** writes `schema/editor-export.pcg` during development. That file is a local handoff target and is intentionally ignored. In Unity, select it with **PCG → Set Watched Graph…** or export/import the graph manually.

## External Agent bridge

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

Use the latest `graphHash` for every write. Graph operations are applied through the normal undo stack; a stale hash returns `graph_conflict` rather than overwriting editor changes.

## Built-in Agent

Provider accounts are configured in **Settings → AI Providers**. Credentials are submitted only to the localhost server and are never stored in the browser or repository. Read/cook/capture tools run automatically; graph writes pause for an approval card tied to the current graph hash.

Supported attachments are PNG/JPEG and UTF-8 `.txt`, `.json` or `.pcg` files. Chat history and attachments are stored in the user's application-support directory, outside the repository.

## Examples

Repository examples are kept in `examples/`, not under `web/`. See [examples/README.md](../../examples/README.md) for the catalog and review URLs.
