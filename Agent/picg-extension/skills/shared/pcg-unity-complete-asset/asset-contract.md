# Asset contract

Create an AssetSpec before graph authoring. It is the contract used by graph planning, PCG validation, material work, prefab construction, and final scoring.

## AssetSpec

Record the following in the authoring plan or a sibling `<slug>-asset-spec.md`:

| Field | Requirement |
|---|---|
| `assetId` | Stable slug used by graph, review scene, prefab, and reports |
| `intent` | One-sentence object/generator outcome and intended use |
| `references` | Source image paths and known viewpoints; facts vs inferred details |
| `scale` | Physical target dimensions in metres, or an explicit stylized-scale override |
| `modules` | Nameable model parts and reusable PCG modules |
| `geometryDoD` | Silhouette, proportions, openings, orientation, and required detail tiers |
| `materialSlots` | Part name, visual role, shader family, texture channels, and expected finish |
| `variation` | Seed controls, legal parameter ranges, and what may vary between instances |
| `outputs` | `.pcg`, texture sources/imports, material assets, prefab, review scene, render sheet |
| `acceptance` | Reference fidelity target, exact exceptions, and quality constraints |

## Invariants

- Use metres and keep every PCG and Unity transform in one scale family unless the AssetSpec explicitly says otherwise.
- Give each visible final part a named material slot. A slot may be a deliberate flat colour, but it must be an authored material—not Unity's default material.
- Declare the local front axis for oriented generators and preserve it through PCG output and prefab creation.
- Keep texture sources, generated textures, Unity materials, prefab, and review output under deterministic, asset-specific paths. Never bind an ephemeral scene-only object reference into a durable graph or prefab.
- Keep the AssetSpec and the root graph `parameters[]` synchronized where an exposed parameter changes an asset-level promise.

## Required deliverables

| Deliverable | Minimum content |
|---|---|
| PCG graph | Valid `.pcg`, plan, parameter receipt, and clean-scene cook evidence |
| Geometry | White-model render, dimensions, and geometry-validation receipt |
| Surface assets | Material plan, texture source files/import settings, and Unity material assets |
| Unity asset | Bound prefab, stable hierarchy, no missing references, and reproducible output path |
| Acceptance | Fully textured render/comparison sheet and score breakdown |

If a requested deliverable is impossible with the active project tools, report the exact unavailable capability before claiming completion. Do not substitute a screenshot, default material, or temporary scene object for a durable asset deliverable.

