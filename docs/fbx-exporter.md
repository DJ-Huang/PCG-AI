# PCG FBX Exporter

## Outcome

`FBX Export` is a Houdini-style, explicit Editor ROP node. It accepts any
polygon-geometry (`SpatialMesh`) output, cooks only that node's upstream graph,
and sends the resulting `PcgGeometry` binary directly to an independent native
exporter. Preview cooks, parameter-change cooks, Play Mode, and Player builds do
not write files.

```text
upstream PCG nodes -> temporary Output cook -> PCGG v2
                                           -> PcgFbxExporter C ABI
                                           -> Assimp aiScene -> FBX 7.x
```

## Why Assimp

The exporter uses [Assimp 6.0.5](https://github.com/assimp/assimp/releases/tag/v6.0.5),
pinned by CMake and built with only the FBX exporter enabled. Assimp uses a
modified BSD 3-clause license and may be statically linked. It is linked only
into `PcgFbxExporter`; `PcgCore` has no Assimp dependency.

Unity's FBX Exporter package is not the primary path because it starts from a
Unity `Mesh` (already triangulated) and its companion-package license is a poor
fit for an engine-independent/Tuanjie native pipeline.

## Blender reference

Blender does not call Autodesk's FBX SDK. Its `io_scene_fbx` add-on implements
the exporter in Python: `export_fbx_bin.py` converts Blender scene data into an
FBX element tree, then `encode_bin.py` serializes binary FBX. The important
behavioral ideas adopted here are explicit axis/unit metadata, deterministic
material/mesh construction, and binary round-trip testing. Blender code is GPL,
so none of it is copied or linked; it is only an interoperability reference.

- [Blender FBX add-on source](https://projects.blender.org/blender/blender/src/branch/main/scripts/addons_core/io_scene_fbx)
- [Blender FBX manual](https://docs.blender.org/manual/en/latest/addons/import_export/scene_fbx.html)

## Geometry mapping

| PCG geometry | FBX / Assimp mapping |
|---|---|
| Point position | `aiMesh::mVertices` |
| N-gon face | `aiFace` with the original corner count |
| Point UV | texture coordinate channel 0 |
| Point color | vertex color channel 0 |
| Detail material | default material |
| Face material | one `aiMesh` partition per material |
| Missing normals | optional area-weighted smooth vertex normals |

The FBX global settings declare Y-up, +Z front, +X coordinate axis, and a
`UnitScaleFactor` of 100 (one PCG/Unity unit is one meter). `Scale` bakes an
additional user multiplier into positions.

Point clouds and splines must first be converted to polygon geometry. The node
intentionally rejects non-polygon results instead of silently inventing a mesh.

## Runtime isolation

- Managed P/Invoke and export orchestration live in `PcgPlugin.Editor.asmdef`.
- Native binaries live under `Plugins/Editor/<platform>` and are disabled for
  Standalone Player import.
- During ordinary cooks, an `FBX Export` branch is removed when a normal
  `Output` exists. If it is the graph's only terminal, it becomes a passive
  `Output`. The file path and other exporter settings are stripped first.
- The module consumes the versioned `PCGG v2` buffer and does not link to
  `PcgCore`, keeping the ABI boundary explicit.

## Usage

1. Add `Output / FBX Export` and connect a geometry output.
2. Set a project-relative path such as `Exports/$GRAPH.fbx`. `$GRAPH` and
   `$NODE` tokens are supported.
3. Select the node and click **Export Now**. Use **Browse…** to choose a path.

## Build and verification

macOS:

```sh
./scripts/build-pcg-fbx-exporter.sh --run-tests --copy-to-unity
```

Windows:

```powershell
./scripts/build-pcg-fbx-exporter.ps1 -RunTests -CopyToUnity
```

The shipping build compiles Assimp exporter-only. A separate test configuration
can enable `PCG_FBX_ENABLE_READBACK_TESTS=ON`; it enables the FBX importer only
for tests and verifies n-gon, UV, vertex-color, material, and unit metadata after
an export/import round trip.
