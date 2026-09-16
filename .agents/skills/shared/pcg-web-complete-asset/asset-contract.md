# Web AssetSpec

For a requested finished asset, use the outcome contract in [complete-asset delivery](../complete-asset-workflow.md). Record `assetId`, `intent`, reference sources/viewpoints, `scale`, important `modules`, `geometryDoD`, `materialSlots`, allowed `variation`, `outputs` and `acceptance` in a compact plan or asset-spec file. No particular sidecar format is required unless an existing workflow consumes it.

Web outputs include the valid `.pcg`, applicable texture/material sources, a durable glTF/GLB or requested Web scene, and current review evidence. Preserve the front axis, metre/stylized scale, root parameter defaults, hierarchy and references through export. A named slot may use a deliberate flat-colour material; do not invent texture requirements or leave placeholder materials.

Use stable asset-specific paths and avoid transient session-only bindings. A review screenshot is not a substitute for the exported artifact. Inspect the actual export after reload and report any unavailable verification. Existing acceptance criteria and explicit exceptions remain binding; numeric scoring is required only when that contract defines it.
