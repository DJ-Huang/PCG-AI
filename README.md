# PCG Graph AI

Web React Flow editor → Graph JSON → C++ core → Unity scene preview.

## Structure

```
pcg-core/          C++ private repo (CMake, DLL + LIB dual target)
  include/pcg_api.h   C API header (sole public interface)
  src/                Implementation
  tests/              Smoke tests
  build/              CMake build output (gitignored)

schema/            Shared Graph JSON v1 contract
  graph-schema.json   JSON Schema
  example.pcg.json    Example graph

Unity/             Unity project with PcgPlugin
  Assets/PcgPlugin/
    Runtime/           PcgNative.cs, PcgGraphLoader, PcgPreview
    Editor/            PcgGraphImporter, PcgSettingsWindow
    Plugins/x86_64/    PcgCore.dll (built from pcg-core)

web/pcg-editor/    Vite + React + @xyflow/react
  src/nodes/         3 custom node types
  src/graphSchema.ts TS types matching Graph JSON v1
  src/exportGraph.ts Export utility
```

## Quick Start

### Build C++ core
```powershell
cd pcg-core
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
# Output: build/Release/PcgCore.dll + PcgCore.lib
```

### Copy DLL to Unity
```powershell
Copy-Item pcg-core/build/Release/PcgCore.dll Unity/Assets/PcgPlugin/Plugins/x86_64/
```

### Run Web editor
```powershell
cd web/pcg-editor
npm install
npm run dev
```

## Milestone M0

Unity Editor → menu `PCG > Print PcgCore Version` → Console prints `pcg-core 0.1.0`.

## Milestone M2 — Editor loop

See [docs/DEMO.md](docs/DEMO.md) §2: Web `Send to Unity` → `PCG > Reload Watched Graph` → Scene Gizmo.

## Milestone M3 — IL2CPP release

```powershell
# Build + test + deploy native artifacts to Unity Plugins
.\scripts\build-pcg-core.ps1 -CopyToUnity -RunTests

# After Unity IL2CPP Windows build
.\scripts\verify-release-package.ps1 -PlayerBuildPath "path\to\build\folder"
```

| Mode | Native artifact | PluginImporter |
|------|-----------------|----------------|
| Editor | `PcgCore.dll` | Editor: on, Standalone: off |
| IL2CPP Player | `PcgCore.lib` | Editor: off, Standalone Win64: on |

Player runtime: attach `PcgRuntimeRunner` + `PcgPreview`, graph at `StreamingAssets/pcg/demo.pcg.json`.

CI: `.github/workflows/pcg-core-ci.yml` (Windows, Release, ctest).
