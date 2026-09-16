# Final acceptance (web)

Score the fully textured exported asset in the final clean-page render. Do not score a white model, a graph canvas, an untextured mesh, placeholder materials, or a default material as the final asset.

## Shared scorecard

Use these weights in both web skill variants:

| Dimension | Weight | Assess |
|---|---:|---|
| Geometry and silhouette | 0.35 | proportion, sections, identity features, scale, assembly, visible detail, and **worst required view** |
| Materials and textures | 0.30 | material intent, map correctness, UV/projection quality, texture scale, and surface response |
| Technical asset construction | 0.15 | valid graph/output, hierarchy, pivot, parameters, and reproducible exported asset |
| Web integration | 0.10 | imports, materials, bindings, durable paths, recook/reload behavior |
| Final presentation | 0.10 | camera match, lighting readability, and clean comparison render |

`finalScore = sum(weight × dimensionScore)`. For reference-image jobs, require `finalScore >= 0.90`; for a non-reference asset, require every AssetSpec DoD and a documented technical review. Never dilute this threshold because a white-model score was high.

## Hard failures

Any hard failure blocks `FINAL_ACCEPTED` regardless of score:

- graph/cook error, empty output, or failed required graph validation;
- wrong physical/stylized scale, broken silhouette, a failed required orthographic view, invalid assembly, or unresolved geometry defect;
- missing, default, or placeholder material on a visible final renderer;
- required texture absent, wrongly imported/bound, visibly invalid mapping, or unresolved material error;
- exported asset missing/damaged, unstable on reload, or holding broken references;
- final render not from the fully textured asset in a clean review page;
- development variant: an unresolved PCG pipeline validation failure or unapproved capability gap.

## Report

Return a compact report with paths and evidence:

```text
Final: FINAL_ACCEPTED|REWORK
Geometry: <0..1> × 0.35
Materials/textures: <0..1> × 0.30
Technical construction: <0..1> × 0.15
Web integration: <0..1> × 0.10
Presentation: <0..1> × 0.10
Score: <0..1>
Evidence: asset=<path> | render=<path> | comparison=<path>
Residual gaps: <none or exact items>
```

For the dev skill, include the PCG pipeline receipt as an additional report line. It does not alter the score weights or threshold.
