# Material workflow

Create a material plan after the white model is approved and before generating textures or Unity materials. The plan must describe the finished asset, not merely list the PCG `AssignMaterial` node names.

## Material plan

For every AssetSpec material slot, define:

| Field | Requirement |
|---|---|
| Slot and owner | Stable part/module name; one owner or an explicit shared-slot list |
| Visual intent | Base colour/value, metallic/dielectric behavior, roughness range, opacity/emission if applicable |
| Shader family | Project-supported shader that matches the render pipeline; verify availability in the selected Unity project |
| Texture channels | Albedo/base colour, normal, mask/metallic-roughness-AO, emission, or an explicit procedural/flat alternative |
| Mapping | UV/projection source, scale, seams, and direction for patterned/detail maps |
| Resolution target | Texture size justified by screen importance; avoid one oversized atlas by default |
| Validation view | Camera distance and lighting that reveal the intended surface response |

## Rules

- Use physically plausible values unless the AssetSpec explicitly requests a stylized treatment.
- Prefer a small number of meaningful, reusable material slots over per-face material noise.
- Separate visually different construction materials—paint, bare metal, rubber, glass, decals, emissive parts—when the reference requires different response.
- Treat graph material assignment as a binding intent. Unity material assets and actual renderer bindings are separate deliverables and must be verified after cook and prefab creation.
- Do not use a material adjustment to compensate for incorrect geometry. Return to geometry validation when the visual issue is a form issue.
- Do not mark this stage complete with Unity Default-Material, missing shaders, pink renderers, or a placeholder texture.

## Review

Check the fully bound material under the final review lighting for hue/value relationship, roughness readability, metallic response, normal strength, transparency/emission behavior, and repetition/scale of texture detail. Record material asset paths and the renderers/subgraphs they bind.

