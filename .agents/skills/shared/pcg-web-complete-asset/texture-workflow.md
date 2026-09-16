# Texture workflow (web)

Produce and validate texture source assets only after material roles and mapping requirements are known. Generate or source textures that serve the AssetSpec; do not create decorative images with no assigned material slot.

## Per-slot process

1. List required channels and their mapping source from the material plan.
2. Produce source textures with consistent texel density and correct orientation. Use generated raster assets only when they can be assigned to a declared slot; otherwise prefer a procedural material.
3. Import them into the web asset pipeline under the asset-specific texture directory.
4. Configure each texture according to its semantic channel, then bind it to the intended material property.
5. Inspect the textured asset at the final review distance and under the final lighting.

## Import and mapping rules

| Channel | Import / review rule |
|---|---|
| Base colour / emissive | Treat as colour data (sRGB); verify hue, alpha behavior, tiling, and mip readability |
| Normal | Mark/import as a normal map using the project-supported convention; verify direction and strength in the final shader |
| Mask, metallic, roughness, AO, height | Treat as linear data; document channel packing and confirm the selected material consumes it as intended |
| Opacity | Verify cutout/transparent mode, sorting, and edge quality in the final render |
| Decal / projection | Verify crop, projection direction, material target, and no unintended spill onto adjacent parts |

Use UVs where the asset needs a stable unwrap; use planar/triplanar/projected mapping only when it is intentional and visually validated. A projection exception must be stated in the UV receipt.

## Texture acceptance

Reject and repair: stretched mapping, visible seams where the reference does not support them, mirrored readable markings, wrong normal direction, texture scale inconsistent with physical size, unexpected repetition, missing alpha, or an imported map bound to the wrong material property.

Record source path, imported asset path, semantic channel, import setting, owning material, and validation result for every texture.
