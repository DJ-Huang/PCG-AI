# Web final acceptance

Apply the acceptance and hard-failure rules in [complete-asset delivery](../complete-asset-workflow.md). This stage applies to a requested finished asset, not a graph-only edit. Preserve any existing AssetSpec scorecard and threshold; do not invent a score to replace missing evidence.

Inspect the actual exported asset after reload in the intended Web renderer. Confirm geometry/scale, required views, authored materials, required textures/mapping, hierarchy and durable references. Compare with the saved-graph review to detect export losses. Use final-quality cooking/rendering for the deliverable, not an adaptive preview as proof of full-quality output.

Report `FINAL_ACCEPTED` only when every required check passes. Otherwise report REWORK or BLOCKED, the failing or unverified check and the verified portion. Include graph/export paths, final render/comparison evidence, generator results when applicable, and residual gaps. A source-graph screenshot or weighted average cannot hide a failed required view, damaged export, missing material or failed reload.
