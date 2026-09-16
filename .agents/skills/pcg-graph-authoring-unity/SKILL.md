---
name: pcg-graph-authoring-unity
description: Author and review PICG graphs for a Unity project; deliver prefabs when requested. Use for Unity-targeted PCG work or /pcg-graph-authoring-unity.
---

# PCG authoring for Unity

Deliver the requested graph, blockout, generator, or finished Unity asset. Do not require a prefab or material rebuild for an isolated graph correction.

## Core path

Read [graph authoring](../shared/graph-authoring.md). When editing a graph open in the Web editor, use the [live MCP contract](../shared/pcg-mcp.md). For an explicitly file-based Unity workflow, work on the intended `.pcg` with a reviewable diff and validate it before import; do not overwrite an unsaved live document through a disk edit.

Validate the saved graph and review its cooked result in the intended Unity project. Web cooking or screenshots can help diagnosis but do not prove Unity imports, shaders, material bindings or prefab behavior.

## Read as needed

| Task | Reference |
| --- | --- |
| Reference-image reconstruction | [Reference workflow](../shared/reference-workflow.md). |
| Unity visual review | [Unity review](unity-review.md), including scene-state protection and workspace selection. |
| Complete materialized prefab | [Unity asset workflow](../shared/pcg-unity-complete-asset/workflow.md). |
| Reusable generator or reliability request | [Generator validation](../shared/generator-validation.md). |
| Helper commands or wiring example | [Scripts](scripts.md) or [examples](examples.md), only the relevant section. |

Do not connect to an unrelated Unity project or discard scene changes to obtain a screenshot. When Unity is unavailable, deliver the verified portion and identify Unity acceptance as blocked. Report artifact paths, actual checks and remaining limitations rather than treating a static validation result as visual acceptance.
