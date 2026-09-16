# PICG End-to-End Demo

This walkthrough verifies the native backend, Web editor, and optional Unity client with one graph.

## Prerequisites

Install Node.js `^20.19.0` or `>=22.12.0`, CMake 3.20+, and a C++17 compiler. Initialize submodules from the repository root:

```bash
git submodule update --init --recursive
```

## Start the backend and Web editor

```bash
./scripts/run-pcg-web.sh
```

Confirm that `http://127.0.0.1:17890/v1/health` reports `"ok": true`, then open `http://127.0.0.1:5173`.

Import `examples/graphs/stone-arch-bridge.pcg`, validate it, cook it, and inspect the preview. A successful demo has no graph-validation errors, produces non-empty geometry, and remains stable after a second cook.

## Optional Unity verification

1. Open `Unity/` in Unity 2022.3 or Unity 1.6.x.
2. Keep `pcg-server` running.
3. Choose **PCG > Server > Health Check**.
4. Open a sample scene or select a `.pcg` asset and run the graph.

Unity should display the same graph output through the HTTP backend. It must not require a copied native library.

## Troubleshooting

- Rebuild and restart `pcg-server` after native code changes.
- Validate node types and handles against `schema/node-manifest.json`.
- Recreate ignored showcase exports rather than committing `artifacts/` directories.
- See [Getting Started](getting-started.md) and [PCG Server](pcg-server.md) for platform-specific details.
