# PCG FBX Exporter

The standalone exporter converts cooked PCG mesh data into FBX without adding an in-process native dependency to Unity.

## Geometry mapping

- positions, normals, colours, and UVs retain their documented domains;
- triangle indices refer to exported mesh vertices;
- material slots preserve names and face assignments;
- transforms and units are converted explicitly rather than inferred by the destination application.

## Isolation

The exporter is a separate native target. Unity and the Web editor request export through supported process or server boundaries; they do not load the exporter library directly.

## Build and test

```bash
./scripts/build-pcg-fbx-exporter.sh --run-tests
```

Assimp is downloaded during native configuration. Its notices are recorded in `pcg-fbx-exporter/THIRD_PARTY_NOTICES.md`. Verify exported files by reopening them in an independent consumer and checking bounds, orientation, materials, and UVs.
