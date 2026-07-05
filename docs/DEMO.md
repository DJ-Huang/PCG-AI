# PCG Graph AI — End-to-End Demo (M2 / M3)

Reproduce the full loop: **Web edit → Graph JSON → C++ execute → Unity preview**.

## Prerequisites

- Visual Studio 2022 (x64)
- Node.js 18+
- Unity 2022.3+ (Tuanjie 1.6.x tested)

## 1. Build native core

```powershell
.\scripts\build-pcg-core.ps1 -CopyToUnity -RunTests
```

Produces `PcgCore.dll` (Editor) and `PcgCore.lib` (IL2CPP Player) under `Unity/Assets/PcgPlugin/Plugins/x86_64/`.

| Artifact | Platform | C# binding |
|----------|----------|------------|
| `PcgCore.dll` | Unity Editor only | `DllImport("PcgCore")` |
| `PcgCore.lib` | Windows Standalone IL2CPP | `DllImport("__Internal")` + build 时 `--linker-flags` 注入 |

## 2. Editor loop (M2)

### Web editor

```powershell
cd web/pcg-editor
npm install
npm run dev
```

1. Open http://localhost:5173
2. Edit the 3-node graph (seed, count, radius, etc.)
3. Click **Send to Unity** → writes `schema/editor-export.pcg`

### Unity Editor

1. Open `Unity/` project
2. Menu **PCG → Set Watched Graph…** (or Settings) → point to `schema/editor-export.pcg`
3. Enable **Auto Reload** or click **PCG → Reload Watched Graph**
4. Scene view shows cyan spheres on the `PCG Preview` object

## 3. IL2CPP Player build (M3)

### Project settings

1. **File → Build Settings** → Platform: **Windows**
2. **Player Settings → Other Settings**
   - Scripting Backend: **IL2CPP**
   - Architecture: **x86_64**
3. Add `Assets/Scenes/SampleScene` (or any scene with `PcgRuntimeRunner` + `PcgPreview`)

### Scene setup (runtime)

1. Create empty GameObject `PCG Runtime`
2. Add components: `PcgPreview`, `PcgRuntimeRunner`
3. `PcgRuntimeRunner` loads `StreamingAssets/pcg/demo.pcg` on Start

### Build & verify

```powershell
# After Unity Build → pick output folder, e.g. Build/Windows/PCGDemo.exe
.\scripts\verify-release-package.ps1 -PlayerBuildPath "Build\Windows"
```

**Expected Player console log:**

```
[PCG] Runtime executing graph: .../StreamingAssets/pcg/demo.pcg (core pcg-core 0.1.0)
[PCG] Runtime OK — 100 points generated.
```

**Release package must NOT contain:**

- `PcgCore.dll` (Editor-only dynamic library)
- Any `pcg-core` `.cpp` sources

Static symbols are linked into `GameAssembly.dll` via `PcgCore.lib`.

## 4. Example graphs

| File | Purpose |
|------|---------|
| `examples/demo.pcg` | Canonical M3 demo (copy for distribution) |
| `schema/example.pcg` | Schema reference |
| `schema/editor-export.pcg` | Web → Unity hot-reload target |
| `Unity/Assets/StreamingAssets/pcg/demo.pcg` | Player runtime input |

## Troubleshooting

| Symptom | Fix |
|---------|-----|
| `DllNotFoundException` in Editor | Re-run `build-pcg-core.ps1 -CopyToUnity`; restart Unity if DLL locked |
| No PCG menu items | Check Console for `PcgPlugin.Editor` compile errors |
| IL2CPP `LNK2019 pcg_*` unresolved | 确认 `PcgIl2CppBuildProcessor` 存在；`PcgCore.lib` 在 Plugins/x86_64；重跑 `build-pcg-core.ps1 -CopyToUnity` |
| IL2CPP `LNK1181 ... PCG.obj` | 工程路径含空格时 il2cpp 会错误拆分 `--linker-flags`；已自动复制 lib 到 `%TEMP%\\PcgCoreIl2CppLink\\` |
| IL2CPP link error (CRT) | Rebuild lib with `/MT`：`.\scripts\build-pcg-core.ps1 -CopyToUnity` |
| `Copy-Item` fails | Unity locks DLL → script writes `.dll.new`; close Unity and copy manually |

## CI

GitHub Actions workflow `.github/workflows/pcg-core-ci.yml` builds Release + runs `ctest` on `windows-latest`, uploads DLL/LIB/header artifacts.
