# pcg-server — localhost C++ backend (Unity has no native plugins)

Unity Editor talks to C++ only over HTTP. `PcgCore` / `PcgFbxExporter` dylib/dll are **not** loaded in Unity.

## Quick start

```bash
./scripts/build-pcg-server.sh
./scripts/run-pcg-server.sh
```

Default: `http://127.0.0.1:17890`

```bash
curl -s http://127.0.0.1:17890/v1/health
./scripts/verify-pcg-server.sh
```

## Unity

1. Start `pcg-server` before cooking.
2. **PCG → Server → Health Check**
3. Cook / Export FBX as usual — both hit the server.

URL: Project Settings → PCG AI, or **PCG → Server → Set Server URL…**

## API

| Method | Path | Notes |
|--------|------|-------|
| GET | `/v1/health` | `{ ok, version, fbx_version, api }` |
| POST | `/v1/cook` | multipart → cook result binary |
| POST | `/v1/cancel` | best-effort cancel |
| POST | `/v1/validate` | graph JSON body |
| POST | `/v1/cache/clear` | clear server cook cache |
| POST | `/v1/export-fbx` | multipart geometry → FBX bytes |

## Scope

Localhost Editor only. No Unity `DllImport` to PcgCore/PcgFbxExporter.
