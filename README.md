# PCG-AI

PCG-AI is a procedural-content authoring stack built around editable graph files. It combines a React graph editor, a C++ geometry runtime and cook server, and a Unity/Tuanjie integration project.

> Project status: active development. Graph and API compatibility may still change before a stable release.

## What is included

- A browser editor based on React Flow with live 3D preview.
- A C++17 procedural geometry runtime with a C API.
- A localhost HTTP/MCP server for validation, cooking, preview capture and agent workflows.
- A Unity/Tuanjie editor integration that cooks through `pcg-server`.
- Versioned graph schemas, a manifest-backed node catalog, reusable subgraphs and curated examples.

## Quick start

### Prerequisites

- Node.js `^20.19.0` or `>=22.12.0`
- CMake 3.20+
- A C++17 compiler
- libcurl development files where CMake does not provide them automatically
- Unity 2022.3 or Tuanjie 1.6.x for the Unity project

Clone the repository and initialize its submodule:

```bash
git clone --recurse-submodules <repository-url>
cd PCG-AI
```

On macOS or Linux, start the native server and Web editor together:

```bash
./scripts/run-pcg-web.sh
```

The script builds missing native artifacts, installs missing Web dependencies and starts:

- Web editor: `http://127.0.0.1:5173`
- Health endpoint: `http://127.0.0.1:17890/v1/health`
- MCP endpoint: `http://127.0.0.1:17890/mcp`

For separate processes:

```bash
./scripts/build-pcg-server.sh
./scripts/run-pcg-server.sh

cd web/pcg-editor
npm ci
npm run dev
```

Windows users can build and run the server with:

```powershell
.\scripts\build-pcg-server.ps1 -Run
cd web\pcg-editor
npm ci
npm run dev
```

See [Getting Started](docs/getting-started.md) for the complete first-run and Unity workflow.

## Repository layout

```text
PCG-AI/
├── Agent/                 Agent skills and reusable PCG workflow tooling
├── docs/                  User, architecture and integration documentation
├── examples/              Graphs, tests, subgraphs, showcases and storyboards
├── library/               Canonical built-in subgraph library
├── pcg-core/              C++ graph runtime and geometry algorithms
├── pcg-fbx-exporter/      Standalone FBX export library
├── pcg-server/            Local HTTP/MCP cook and agent backend
├── schema/                Graph schemas and node manifest
├── scripts/               Build, run, sync and validation commands
├── Unity/                 Unity/Tuanjie integration project and curated samples
└── web/pcg-editor/        Vite + React authoring application
```

The main data flow is:

```text
Web or Unity graph editor
        │ graph JSON
        ▼
pcg-server ──► pcg-core ──► geometry / points / materials / preview data
        │
        ├──► Web preview and GLB export
        └──► Unity scene preview and FBX export
```

More detail is available in [Architecture](docs/architecture.md).

## Examples

All repository-level examples live under [examples/](examples/README.md):

- `examples/graphs/` — runnable feature and production graphs
- `examples/tests/` — minimal regression fixtures
- `examples/subgraphs/` — linked subgraph examples
- `examples/showcases/` — complete reference-driven case studies
- `examples/storyboards/` — sequence and layout blockouts

Unity-specific scenes and materials live under `Unity/Assets/Samples/PCG-AI/`. Generated review work belongs in `Unity/Assets/PCG-AI-Workspace/`, which is intentionally ignored.

Review a repository graph in the browser with:

```text
http://127.0.0.1:5173/review?graph=examples/graphs/stone-arch-bridge.pcg
```

## Build and validation

Native runtime:

```bash
./scripts/build-pcg-core.sh --run-tests
```

Web editor:

```bash
cd web/pcg-editor
npm ci
npm run lint
npm run build
npx vitest run
```

Repository contracts:

```bash
python3 scripts/validate-manifest.py
python3 scripts/validate-subgraph-schema.py
python3 scripts/validate-builtin-library.py
```

Server smoke test, after starting `pcg-server`:

```bash
./scripts/verify-pcg-server.sh
```

The script catalog is documented in [scripts/README.md](scripts/README.md).

## Unity / Tuanjie

Open `Unity/` as the project root. Unity does not load `PcgCore` or the FBX exporter in-process; start `pcg-server` before cooking and use **PCG → Server → Health Check** to verify the connection.

The repository contains no machine-local Unity package references. Optional local editor integrations should be installed in the developer's own environment instead of being committed to `Packages/manifest.json`.

See [Unity/README.md](Unity/README.md) for scene locations, sample conventions and generated-directory rules.

## Documentation

Start at the [documentation index](docs/README.md). Important guides include:

- [Getting Started](docs/getting-started.md)
- [Architecture](docs/architecture.md)
- [PCG server](docs/pcg-server.md)
- [Node reference](docs/node-reference.md)
- [Subgraph library](library/README.md)
- [Development manual](docs/PCG-AI-Development-Manual.md)

## Contributing and security

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request and [SECURITY.md](SECURITY.md) before reporting a vulnerability. Community expectations are described in [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). Media provenance and third-party terms are tracked in [ASSET_LICENSES.md](ASSET_LICENSES.md).

## License status

No open-source license has been selected yet. Until a `LICENSE` file is added by the copyright holder, the repository is not legally open source and reuse rights are not granted. Select MIT, Apache-2.0 or another license and complete the asset-rights review before the public release.
