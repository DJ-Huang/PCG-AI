# Unity Project

Open this directory, not the repository root, in Unity 2022.3 or Unity 1.6.x.

## Project layout

```text
Assets/
├── PcgPlugin/                 Runtime, editor integration, tests and fixtures
├── Samples/PICG/
│   ├── Demos/                 Curated end-to-end scenes
│   ├── Showcases/             Finished asset case studies
│   └── Validation/            Small visual validation scenes
├── Settings/                  Render-pipeline settings
└── StreamingAssets/pcg/       Runtime graph input
```

Generated review scenes belong in `Assets/PICG-Workspace/`. Generated FBX/GLB exports belong in `Assets/Exports/`. Both directories are ignored so running an authoring workflow does not pollute the repository.

## First run

1. Build and start `pcg-server` from the repository root.
2. Open this project and wait for import/compilation to finish.
3. Choose **PCG → Server → Health Check**.
4. Open `Assets/Samples/PICG/Demos/Overview.scene` or another sample.

The Unity editor communicates with C++ over localhost HTTP. Do not copy `PcgCore` or `PcgFbxExporter` libraries into `Assets/PcgPlugin/Plugins/`.

## Samples

- `Demos/Overview.scene` — broad project overview.
- `Demos/LotCity.scene` — lot subdivision/city workflow.
- `Demos/GraphGallery/` — graph/material gallery and terrain sample.
- `Showcases/` — classic knife, M9 bayonet, music box, wooden cabins, third-party services and sedan graphs.
- `Validation/` — focused outline behavior scenes.

Unity `.meta` files are part of the source tree. Always move or rename an asset together with its `.meta` file so scene and prefab GUID references remain valid.

Sample scenes intentionally omit cooked preview Mesh subassets and cached result payloads. Keep `pcg-server` running and run the graph after opening a scene to regenerate its preview. Before committing a sample scene, run `python3 scripts/sanitize-unity-sample-scenes.py Unity/Assets/Samples/PICG` from the repository root.

## Local packages

Committed `Packages/manifest.json` entries must resolve on another machine. Do not add `file:/Users/...`, `file:C:/...` or other personal package paths. Install optional local tooling outside the committed manifest or document a portable registry/Git dependency.

## Validation

The repository contains editor tests under `Assets/PcgPlugin/Tests/Editor`. Run them from **Window → General → Test Runner** after the project compiles.

For any script change:

1. Clear the Unity Console.
2. Trigger compilation and wait for the editor to become idle.
3. Confirm the Console has no new errors.
4. Run the relevant editor tests and open the affected sample scene.

## Files that must stay local

- `Library/`, `Temp/`, `Logs/`, `obj/`, `UserSettings/`
- generated `.csproj` and `.sln` files
- `Assets/PICG-Workspace/`
- `Assets/Exports/`
- screenshots and crash reports
