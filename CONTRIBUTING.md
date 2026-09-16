# Contributing

Thanks for helping improve PICG. Keep changes focused, reproducible and easy to review.

## Before opening a pull request

1. Create a topic branch from the current default branch.
2. Keep generated output and machine-local paths out of the change.
3. Update the relevant documentation and tests with behavior changes.
4. Run the checks for every component you touched.
5. Explain the user-visible result, validation performed and any known limitation in the pull request.

## Required checks

For Web changes:

```bash
cd web/pcg-editor
npm ci
npm run lint
npm run build
npx vitest run
```

For native runtime changes:

```bash
./scripts/build-pcg-core.sh --run-tests
```

For schema, node or library changes:

```bash
python3 scripts/validate-manifest.py
python3 scripts/validate-subgraph-schema.py
python3 scripts/validate-builtin-library.py
```

For Unity changes, let the editor compile, check a clean Console, run the relevant editor tests and open the affected sample scene.

## Graph and asset changes

- Treat `schema/node-manifest.json` as the source of truth for node types, properties and pins.
- Keep graphs in top-down layout and validate/cook them before submission.
- Put repository examples under the matching `examples/` category.
- Include source, authorship, and redistribution evidence in the pull request when adding reference images, textures, models, or generated outputs.
- Put Unity-only samples under `Unity/Assets/Samples/PICG/`.
- Never commit `Unity/Assets/PICG-Workspace/`, `Unity/Assets/Exports/`, build folders or raw evidence dumps.
- Preserve Unity `.meta` files when moving assets.

## Generated copies

After editing `schema/node-manifest.json`, run:

```bash
./scripts/sync-manifest.sh
```

After editing `library/`, run:

```bash
python3 scripts/build_library_index.py
```

Commit the canonical source and its required synchronized copies together.

## Commit and review scope

Avoid unrelated formatting or refactors in the same pull request. Do not include credentials, private URLs, personal filesystem paths, crash dumps or editor histories.

By contributing, you confirm that you have the right to submit the code and assets under the repository's Apache-2.0 license. Media and third-party assets must have compatible redistribution rights.
