# PcgFbxExporter

Editor-only FBX export bridge for PCG-AI. It consumes the versioned
`PcgGeometry` binary payload and builds an Assimp scene directly, preserving
polygon faces, point UV/color attributes, and per-face material assignments.

This module deliberately does not link against `PcgCore`. Assimp is pinned to
v6.0.5 and statically linked into the small `PcgFbxExporter` dynamic library.
The public boundary is the versioned C ABI in `include/pcg_fbx_api.h`.

Build on macOS:

```sh
./scripts/build-pcg-fbx-exporter.sh --run-tests --copy-to-unity
```

Assimp uses the modified 3-clause BSD license. See `THIRD_PARTY_NOTICES.md`.
