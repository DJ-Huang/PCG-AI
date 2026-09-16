# Getting Started

This guide brings up `pcg-server`, the Web editor and the optional Unity client from a fresh clone.

## 1. Prepare the repository

```bash
git submodule update --init --recursive
```

Install Node.js `^20.19.0` or `>=22.12.0`, CMake 3.20+ and a C++17 compiler. On Linux, install the development package for libcurl if CMake cannot find it.

## 2. Build the native server

macOS or Linux:

```bash
./scripts/build-pcg-server.sh
```

Windows PowerShell:

```powershell
.\scripts\build-pcg-server.ps1
```

The build is written to `pcg-server/build/` and is not committed.

## 3. Start the server

```bash
./scripts/run-pcg-server.sh
```

Verify it from another terminal:

```bash
curl http://127.0.0.1:17890/v1/health
```

The response should contain `"ok": true`.

## 4. Start the Web editor

```bash
cd web/pcg-editor
npm ci
npm run dev
```

Open `http://127.0.0.1:5173`. The editor proxies cook requests to `pcg-server` on port 17890.

To launch both services with one command on macOS or Linux, use `./scripts/run-pcg-web.sh` from the repository root.

## 5. Open an example

Use the editor's import action, or open a deterministic review route:

```text
http://127.0.0.1:5173/review?graph=examples/graphs/stone-arch-bridge.pcg
```

Paths passed to `?graph=` are repository-relative and must remain inside the workspace.

## 6. Use the Unity project

1. Open the `Unity/` directory in Unity 2022.3 or Unity 1.6.x.
2. Wait for package import and script compilation.
3. Keep `pcg-server` running.
4. Choose **PCG → Server → Health Check**.
5. Open a scene under `Assets/Samples/PICG/` or a `.pcg` asset in the Project window.

Do not commit `Library/`, `Temp/`, IDE project files, `Assets/Exports/` or `Assets/PICG-Workspace/`. They are generated locally.

## 7. Verify the checkout

```bash
python3 scripts/validate-manifest.py
python3 scripts/validate-subgraph-schema.py
python3 scripts/validate-builtin-library.py

cd web/pcg-editor
npm run lint
npm run build
npx vitest run
```

For the C++ suite:

```bash
./scripts/build-pcg-core.sh --run-tests
```

## Common problems

### Web editor reports that the server is unavailable

Confirm that `http://127.0.0.1:17890/v1/health` responds and that `PCG_SERVER_PORT` matches the port used to start both processes.

### Unity cannot cook a graph

Run **PCG → Server → Health Check**, then verify the graph against `schema/node-manifest.json`. Unity communicates with the native runtime only through the local server.

### A showcase references a missing `artifacts/` file

Large generated exports are deliberately ignored. Recreate them from the included graph or place the local result in that showcase's `artifacts/` directory.
