# PICG Development Manual

This manual is the contributor-level map of the repository. It complements [Architecture](architecture.md), [Getting Started](getting-started.md), and the generated [Node Reference](node-reference.md).

## System boundaries

PICG has four principal layers:

1. `pcg-core` parses and executes graphs and owns geometry algorithms.
2. `pcg-server` exposes local HTTP and MCP boundaries, caching, and orchestration.
3. `web/pcg-editor` provides browser authoring, preview, review capture, and export.
4. `Unity/Assets/PcgPlugin` provides Unity assets, inspector integration, preview, and runtime-side HTTP calls.

`schema/node-manifest.json` is the canonical node contract. `library/` is the canonical built-in subgraph library. Generated copies must be synchronized by repository scripts.

## Graph and data model

Graph documents contain versioned nodes, typed edges, parameters, and optional subgraphs. Native execution uses topological ordering, gathers typed inputs, executes registered elements, and returns sink data plus diagnostics and statistics.

The primary runtime data families are geometry, mesh, points, splines, heightfields, materials, and metadata collections. Geometry preserves editable topology and named groups; mesh data is a render/export product. Binary envelopes are versioned contracts and require compatibility tests in every consumer.

## Adding or changing a node

1. Implement or update the native element and its algorithm.
2. Register the node in the native factory.
3. Update `schema/node-manifest.json` with manifest-supported property types and exact pin IDs.
4. Run `./scripts/sync-manifest.sh`.
5. Add native tests for behavior and contract edge cases.
6. Update Web or Unity presentation only when the manifest-driven generic UI is insufficient.
7. Regenerate `docs/node-reference.md` with `python3 scripts/generate-node-reference.py`.
8. Rebuild and restart `pcg-server`, then verify a real cook in the affected client.

Pin IDs, property types, defaults, binary layouts, and serialized enum values are compatibility boundaries. A breaking change requires a migration or versioned reader.

## Geometry changes

Keep algorithms independent from host UI. Test empty, minimal, typical, degenerate, and large inputs. Verify topology invariants, groups, attributes, normals, UVs, determinism, and cancellation. A successful compile does not prove a valid or visually correct mesh.

## Built-in subgraphs

Edit only the canonical files under `library/`. Run:

```bash
python3 scripts/build_library_index.py
python3 scripts/validate-builtin-library.py
```

The first command validates metadata, rebuilds the index, and synchronizes Unity and Web copies.

## Build and test

```bash
./scripts/build-pcg-core.sh --run-tests
./scripts/build-pcg-server.sh

python3 scripts/validate-manifest.py
python3 scripts/validate-subgraph-schema.py
python3 scripts/validate-builtin-library.py
python3 scripts/check-doc-language.py

cd web/pcg-editor
npm ci
npm run lint
npm run build
npx vitest run
```

After backend changes, restart the server and use a health check plus a representative graph. After visual or editor changes, inspect the actual client state; unit tests are supporting evidence, not a replacement.

## Repository policy

- Keep documentation in English except `README.ja.md`.
- Do not commit build output, dependencies, caches, credentials, captures, or machine-specific paths.
- Keep third-party notices and asset provenance current.
- Use focused, reviewable commits and include tests with behavior changes.
- See [Contributing](../CONTRIBUTING.md), [Support](../SUPPORT.md), and [Maintainer Workflows](maintainer-workflows.md).
