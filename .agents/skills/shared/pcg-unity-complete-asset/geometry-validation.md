# Geometry validation

Validate the cooked white model in a new clean `PcgReview_<slug>` scene before spending time on textures or final materials. Follow the Unity MCP procedure in `AUTHORING_SKILL_DIR/unity-review.md` for selection, scene creation, cook, screenshot, and comparison sheet.

## Checks

| Check | Pass condition | Repair stage |
|---|---|---|
| Cook | No graph/runtime error; non-empty output exists | graph authoring / pipeline validation |
| Scale | Bounds match AssetSpec and use metres; no unit drift | AssetSpec / graph data |
| Silhouette | Primary outline, section changes, and identity features match the reference | graph plan / geometry nodes |
| Assembly | Parts are intentionally separated or fused; no accidental intersections, floaters, or hidden duplicate shells | graph modules / placement |
| Normals and shading | No inverted faces, unexpected faceting, or undefined shading at the intended camera distance | primitive choice / topology / shading settings |
| Hard surface | Bevel is per clean part before assembly merge; no global mixed-scale post-merge bevel | graph topology |
| Oriented output | Front axis, pivot, and generated transform match AssetSpec | graph orientation / prefab setup |
| Detail density | Macro and meso detail are sufficient before micro texture detail is used to disguise missing shape | graph plan |

For buildings and oriented scatter, verify facade opening clearance and access-facing orientation. For a multi-part object, inspect each module and the final assembled output.

## Decision rule

Geometry must carry form. Do not use decals, normal maps, roughness contrast, or camera framing to hide a wrong silhouette, wrong scale, missing openings, or intersecting parts. Return to the earliest defective graph stage, recook, and rerun this checklist.

Record:

```text
Geometry: cook=PASS|FAIL | bounds=<x,y,z>m | silhouette=PASS|FAIL |
assembly=PASS|FAIL | normals=PASS|FAIL | scale=PASS|FAIL
```

