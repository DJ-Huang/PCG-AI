# Tripo YOYO Puppy

This showcase separates a third-party generated reference graph from a procedural reconstruction.

- `reference.pcg` — Tripo-backed reference graph; it requires a valid local cache or Provider request.
- `procedural.pcg` — editable reconstruction that does not depend on source faces or indices.
- `asset-spec.md` — provenance, measurements, semantic components and acceptance boundary.
- `reference.png` — the admitted visual reference used by both workflows.
- `evidence/` — curated final views only.
- `artifacts/` — local GLB exports; ignored because they are large and reproducible.

Review the procedural graph with:

```text
http://127.0.0.1:5173/review?graph=examples/showcases/tripo-yoyo-puppy/procedural.pcg
```
