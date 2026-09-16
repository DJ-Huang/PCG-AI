# Unity final acceptance

Apply the acceptance and hard-failure rules in [complete-asset delivery](../complete-asset-workflow.md). This stage applies to a requested finished prefab, not a graph-only edit. Preserve any existing AssetSpec scorecard and threshold; subjective scores do not substitute for evidence.

Reload the saved prefab in the intended Unity project and inspect it in an isolated review scene. Confirm geometry/scale, required views, authored materials, required textures/mapping, pivot, hierarchy and persistent mesh/material references. Missing/pink materials, broken references, damaged output or a failed reload block acceptance regardless of an image score.

Report `FINAL_ACCEPTED` only when every required check passes. Otherwise report REWORK or BLOCKED with the failing or unverified check. Include graph/prefab paths, current Unity render/comparison evidence, generator results when applicable and residual gaps. Web validation or a render of an unsaved scene object is not prefab acceptance.
