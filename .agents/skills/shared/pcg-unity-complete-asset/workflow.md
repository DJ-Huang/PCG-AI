# Complete Unity asset

Use the shared [complete-asset workflow](../complete-asset-workflow.md) only when a finished Unity asset was requested. It owns stage applicability, recovery, acceptance and handoff; there is no separate development variant.

| Current work | Platform reference |
| --- | --- |
| Specify outputs and materials | [Asset contract](asset-contract.md). |
| Build/change graph | [Graph contract](pcg-graph-authoring.md). |
| Check dimensions, topology and UVs | [Geometry validation](geometry-validation.md). |
| Choose slots and shaders | [Material workflow](material-workflow.md). |
| Create/import required maps | [Texture workflow](texture-workflow.md). |
| Bind, save and reload prefab | [Unity integration](unity-integration.md). |
| Review the final deliverable | [Final acceptance](final-acceptance.md). |
| Diagnose a named stage failure | [Error codes](error-codes.md). |

Read the applicable row when needed. Final evidence must come from the intended Unity project and the reloaded prefab, with no missing/pink materials or broken references. Preserve scene state through the [Unity review procedure](../../pcg-graph-authoring-unity/unity-review.md). Required textures must be present; intentional texture-free materials need no fabricated texture stage. Required views are those supplied or agreed for the task.

For existing stage references, resolve aliases from this directory: `SHARED_DIR = .`, `SHARED_SCRIPTS_DIR = ../pcg-scripts`, `AUTHORING_SKILL_DIR = ../../pcg-graph-authoring-unity`. These are path conventions, not additional documents to load.
