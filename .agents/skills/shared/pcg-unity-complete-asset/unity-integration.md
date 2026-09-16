# Unity integration

Use the selected workspace Unity instance and the clean review-scene rules in `AUTHORING_SKILL_DIR/unity-review.md`. Create durable assets; a working temporary scene is not a delivery.

## Required integration sequence

1. Create or update Unity material assets from the material plan using project-supported shaders.
2. Configure imported texture assets and connect each declared channel to its material property.
3. Bind material assets to the generated output by stable renderer/slot identity. Verify bindings survive a fresh cook and a prefab reload.
4. Construct or update the prefab at the AssetSpec output path. Preserve an intelligible hierarchy, pivot/front-axis convention, and required PCG component/graph reference.
5. Regenerate once in a clean scene. Remove only stale artifacts that are proven to belong to this asset; do not delete unrelated project assets.
6. Render the final prefab in the clean review scene with a camera and lighting that make geometry and surface response inspectable. Use the same reference view when one exists.

## Integration checks

- All material and texture references resolve after reload.
- No renderer uses a default, missing, pink, or accidental fallback material.
- The prefab opens with the expected hierarchy, scale, pivot, and front orientation.
- Graph parameters, if exposed, remain wired to valid root nodes and do not break material bindings after recook.
- Asset paths are project-relative, deterministic, and contain no scene-instance-only references.
- The review scene remains dedicated to the asset under review; do not judge it in a cluttered demo scene.

Record concrete paths for material assets, texture assets, prefab, review scene, and final render.

