# PCG Built-in Subgraph Library

`library/` is the canonical source for reusable subgraphs distributed with the Web and Unity editors.

## Layout

Each entry lives in a category directory and normally contains:

- `<name>.pcgsubgraph` — the reusable graph document;
- `<name>.libmeta.json` — title, category, version, tags, and compatibility metadata;
- `<name>.md` — optional user-facing notes.

The generated `index.json` is consumed by host editors. Do not hand-edit generated Unity or Web copies.

## Admission criteria

- The subgraph validates against the current schema and manifest.
- Inputs, outputs, defaults, units, and failure behavior are documented.
- Node IDs and titles are stable and meaningful.
- The graph has top-down layout and no machine-local paths.
- A focused test or example proves its primary behavior.
- Any bundled media has recorded redistribution rights.

## Updating the library

Edit the canonical files here, then run:

```bash
python3 scripts/build_library_index.py
python3 scripts/validate-builtin-library.py
```

The build script validates metadata, rebuilds the index, and synchronizes the Unity and Web copies. Commit the canonical files and generated copies together.
