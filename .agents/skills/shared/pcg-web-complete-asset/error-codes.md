# Error codes and recovery (web)

Use these codes to identify the earliest stage that must be repaired. Do not resolve an upstream error by adding downstream artifice.

| Code | Meaning | Required recovery |
|---|---|---|
| `ASSET_SPEC_INCOMPLETE` | Scale, output, material roles, or acceptance criteria are missing | Complete or responsibly infer the AssetSpec |
| `PCG_MANIFEST_INVALID` | Node, pin, property, or graph JSON violates the current manifest | Repair graph authoring and rerun static validation |
| `PCG_CAPABILITY_GAP` | Required operation has no efficient supported composition | Dev: research and stop for explicit override; standard: disclose limitation before proceeding |
| `PCG_PIPELINE_FAIL` | Dev-only structural, parameter, regeneration, boundary, or output-contract test failed | Return to graph planning, node construction, parameters, or generation logic; retest |
| `COOK_EMPTY_OR_FAILED` | Graph does not produce a valid cooked result (pcg-server returns error or empty geometry) | Repair graph and clean-page cook |
| `GEOMETRY_FAIL` | Silhouette, scale, topology, normals, assembly, or orientation is wrong | Return to AssetSpec or graph geometry stage |
| `UV_MAPPING_FAIL` | UV/projected mapping is stretched, reversed, inconsistent, or absent | Repair unwrap/projection and texture assignment |
| `MATERIAL_FAIL` | Material response, slots, or bindings are wrong | Repair material plan/web material stage |
| `TEXTURE_IMPORT_FAIL` | Texture semantics/import settings/channel binding are invalid | Repair source/import/binding stage |
| `EXPORT_FAIL` | Exported asset hierarchy, paths, references, or recook/reload behavior is unstable | Repair web integration and regenerate |
| `FINAL_RENDER_FAIL` | Final image is not a clean-page, fully textured asset render or cannot demonstrate the asset | Repair web integration/presentation and rerender |

For `PCG_CAPABILITY_GAP`, do not disguise a missing Boolean, UV unwrap, topology operation, or attribute path with `MergeMesh`, a box stack, a material, or a texture. Record the limitation and its effect on fidelity.
