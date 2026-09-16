<p align="right">
  <strong>English</strong> | <a href="README.ja.md">日本語</a>
</p>

# PICG

**Procedural Intelligent Content Generation**

PICG is a cross-engine procedural content generation framework built around editable graph assets and intelligent content workflows. It combines a React-based graph editor, a C++17 geometry runtime, a localhost HTTP/MCP cook server, and Unity/Tuanjie integration.

The project follows one core idea: **authoring tools and engine integrations should be interchangeable front ends, while graph data and execution semantics remain portable and consistent.** Graphs can be authored manually or through agent-assisted workflows, executed by the same native core, and previewed across different environments.

> **Project status:** active development. Graph schemas, APIs, and integration details may still change before a stable release.

## Core capabilities

- **Graph-first authoring** — editable, versioned graph files form the shared contract between editors, runtimes, and integrations.
- **Native procedural runtime** — a C++17 geometry and graph execution core exposed through a C API.
- **Web authoring and preview** — a React Flow based editor with live 3D preview and GLB export workflows.
- **Unity / Tuanjie integration** — editor-side graph workflows backed by the same external `pcg-server` runtime.
- **Agent-ready workflows** — localhost HTTP and MCP endpoints for validation, cooking, preview capture, and automated content workflows.
- **Reusable content building blocks** — manifest-backed node definitions, subgraphs, schemas, and curated examples.

## Architecture

PICG separates authoring, execution, and engine integration into independent layers:

```text
Web / Unity / Tuanjie / Agent workflows
                 │
                 │ Graph JSON
                 ▼
             pcg-server
                 │
                 ▼
              pcg-core
                 │
        geometry / points / materials
                 │
        ┌────────┴────────┐
        ▼                 ▼
   Web preview       Engine preview
   + GLB export      + FBX / scene data
```

This keeps the graph contract editor-independent while using the C++ runtime as the single execution layer.

More detail is available in [Architecture](docs/architecture.md).

## Quick start

### Prerequisites

- Node.js `^20.19.0` or `>=22.12.0`
- CMake 3.20+
- A C++17 compiler
- libcurl development files where CMake does not provide them automatically
- Unity 2022.3 or Tuanjie 1.6.x for the Unity project

Clone the repository and initialize its submodule:

```bash
git clone --recurse-submodules https://github.com/DJ-Huang/PICG.git
cd PICG
```

On macOS or Linux, start the native server and Web editor together:

```bash
./scripts/run-pcg-web.sh
```

The script builds missing native artifacts, installs missing Web dependencies, and starts:

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
PICG/
├── Agent/                 Agent skills and reusable procedural workflow tooling
├── docs/                  User, architecture, and integration documentation
├── examples/              Graphs, tests, subgraphs, showcases, and storyboards
├── library/               Canonical built-in subgraph library
├── pcg-core/              C++ graph runtime and geometry algorithms
├── pcg-fbx-exporter/      Standalone FBX export library
├── pcg-server/            Local HTTP/MCP cook and agent backend
├── schema/                Graph schemas and node manifest
├── scripts/               Build, run, sync, and validation commands
├── Unity/                 Unity/Tuanjie integration project and curated samples
└── web/pcg-editor/        Vite + React authoring application
```

## Examples

All repository-level examples live under [examples/](examples/README.md):

- `examples/graphs/` — runnable feature and production graphs
- `examples/tests/` — minimal regression fixtures
- `examples/subgraphs/` — linked subgraph examples
- `examples/showcases/` — complete reference-driven case studies
- `examples/storyboards/` — sequence and layout blockouts

Unity-specific scenes, materials, and sample conventions are documented in [Unity/README.md](Unity/README.md).

Review a repository graph in the browser with:

```text
http://127.0.0.1:5173/review?graph=examples/graphs/stone-arch-bridge.pcg
```

## Build and validation

### Native runtime

```bash
./scripts/build-pcg-core.sh --run-tests
```

### Web editor

```bash
cd web/pcg-editor
npm ci
npm run lint
npm run build
npx vitest run
```

### Repository contracts

```bash
python3 scripts/validate-manifest.py
python3 scripts/validate-subgraph-schema.py
python3 scripts/validate-builtin-library.py
```

### Server smoke test

After starting `pcg-server`:

```bash
./scripts/verify-pcg-server.sh
```

The script catalog is documented in [scripts/README.md](scripts/README.md).

## Unity / Tuanjie

Open `Unity/` as the project root. Unity does not load `PcgCore` or the FBX exporter in-process; start `pcg-server` before cooking and use **PCG → Server → Health Check** to verify the connection.

The repository contains no machine-local Unity package references. Optional local editor integrations should be installed in the developer's own environment instead of being committed to `Packages/manifest.json`.

See [Unity/README.md](Unity/README.md) for scene locations, sample conventions, and generated-directory rules.

## Documentation

Start at the [documentation index](docs/README.md). Important guides include:

- [Getting Started](docs/getting-started.md)
- [Architecture](docs/architecture.md)
- [PCG server](docs/pcg-server.md)
- [Node reference](docs/node-reference.md)
- [Subgraph library](library/README.md)

## Contributing and security

Read [CONTRIBUTING.md](CONTRIBUTING.md) before opening a pull request and [SECURITY.md](SECURITY.md) before reporting a vulnerability. Community expectations are described in [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md). Media provenance and third-party terms are tracked in [ASSET_LICENSES.md](ASSET_LICENSES.md).

## License status

No open-source license has been selected yet. Until a `LICENSE` file is added by the copyright holder, the repository is not legally open source and reuse rights are not granted. Select MIT, Apache-2.0, or another license and complete the asset-rights review before the public release.
