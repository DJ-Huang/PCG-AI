# Complete Web asset

Use the shared [complete-asset workflow](../complete-asset-workflow.md) only when a finished Web asset was requested. It owns stage applicability, recovery, acceptance and handoff; there is no separate development variant.

| Current work | Platform reference |
| --- | --- |
| Specify outputs and materials | [Asset contract](asset-contract.md). |
| Build/change graph | [Graph contract](pcg-graph-authoring.md). |
| Check dimensions, topology and UVs | [Geometry validation](geometry-validation.md). |
| Choose slots and surface response | [Material workflow](material-workflow.md). |
| Create/import required maps | [Texture workflow](texture-workflow.md). |
| Bind, export and reload | [Web integration](web-integration.md). |
| Review the final deliverable | [Final acceptance](final-acceptance.md). |
| Diagnose a named stage failure | [Error codes](error-codes.md). |

Read the row needed now, not every document upfront. Use the saved-graph `/review` route for repeatable capture and validate the actual export by reopening it. A screenshot of the source graph alone does not establish export correctness. Surface stages use deliberate materials; texture files are required only where specified, not for an intentionally flat-colour material. Required views are those supplied or agreed for the task; a single-image brief does not acquire a mandatory triplet.

For existing stage references, resolve aliases from this directory: `SHARED_DIR = .`, `SHARED_SCRIPTS_DIR = ../pcg-scripts`, `AUTHORING_SKILL_DIR = ../../pcg-graph-authoring-web`. These are path conventions, not additional documents to load.
