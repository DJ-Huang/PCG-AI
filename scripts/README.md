# Scripts

Run these commands from the repository root unless a script says otherwise.

## Build and run

| Script | Purpose |
| --- | --- |
| `build-pcg-core.sh` / `.ps1` | Configure and build the C++ runtime; optionally run tests |
| `build-pcg-server.sh` / `.ps1` | Configure and build the local cook server |
| `run-pcg-server.sh` | Start the server on localhost |
| `stop-pcg-server.sh` | Stop the repository-managed server process |
| `run-pcg-web.sh` | Start the server and Web editor together |
| `build-pcg-fbx-exporter.sh` / `.ps1` | Build the standalone FBX exporter |

## Synchronization

| Script | Purpose |
| --- | --- |
| `sync-manifest.sh` | Copy the canonical node manifest to Web/Unity consumers |
| `build_library_index.py` | Validate/index `library/` and sync Web/Unity copies |

## Validation

Scripts named `validate-*` check a focused contract. Some require only source files; others require a running `pcg-server` or Web editor. Start with:

```bash
python3 scripts/validate-manifest.py
python3 scripts/validate-subgraph-schema.py
python3 scripts/validate-builtin-library.py
python3 scripts/check-doc-language.py
python3 scripts/check-markdown-links.py
```

Then use `verify-pcg-server.sh` for an end-to-end server smoke test. See each script's module docstring or `--help` output for prerequisites.

`sanitize-unity-sample-scenes.py` removes reproducible preview Mesh subassets and cached cook results from Unity sample scenes. CI runs it with `--check`; run it without that flag after saving a generated preview into a source scene.

Generated build directories are ignored and can be removed safely between runs.
