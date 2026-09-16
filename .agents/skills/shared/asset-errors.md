# Asset error routing

These diagnostic labels identify the responsible stage; they are not a second state machine. Repair the cause and rerun affected checks rather than masking an upstream defect downstream.

| Code | Meaning and response |
| --- | --- |
| `ASSET_SPEC_INCOMPLETE` | Resolve material scale, output or acceptance ambiguity from evidence; ask only when consequential information remains missing. |
| `PCG_MANIFEST_INVALID` | Fix actual schema, pin, property or graph errors; revalidate against the relevant build. |
| `PCG_CAPABILITY_GAP` | Verify the required operation lacks a supported composition. Explain evidence and alternatives; an outcome-changing approximation needs agreement. |
| `PCG_PIPELINE_FAIL` | A selected generator test failed. Repair graph, bindings or generation behavior and rerun that test. |
| `COOK_EMPTY_OR_FAILED` | Diagnose cook/runtime failure or unintended empty/stale output before accepting geometry. |
| `GEOMETRY_FAIL` | Correct dimensions, silhouette, topology, normals, orientation or assembly. |
| `UV_MAPPING_FAIL` | Fix required UV/projection mapping; do not hide stretching or reversed mapping with lighting. |
| `MATERIAL_FAIL` | Fix shader response, material slots or bindings in the target platform. |
| `TEXTURE_IMPORT_FAIL` | Fix required source data, import settings, channel semantics or binding. |
| `EXPORT_FAIL` / `PREFAB_FAIL` | Repair durable hierarchy, paths, references or reload behavior in the target integration. |
| `FINAL_RENDER_FAIL` | Obtain current clean evidence from the actual materialized deliverable. |

A missing tool or unavailable environment is a BLOCKED check, not evidence of a missing PICG feature. Performance without a measured workload is unassessed, not an automatic capability gap. Use [generator validation](generator-validation.md) for reliability evidence and [complete-asset delivery](complete-asset-workflow.md) for final acceptance. Preserve valid partial work and report its scope honestly.
