---
name: pcg-graph-authoring-web
description: Create, edit, or review PICG graphs in the live Web editor; deliver Web assets when explicitly requested. Use for Web-targeted PCG work or /pcg-graph-authoring-web.
---

# PCG authoring for Web

Deliver the requested scope: a node fix, graph, blockout, generator, or finished asset. A graph-only request does not require textures or GLB export.

## Core path

Read [graph authoring](../shared/graph-authoring.md) for PICG constraints and [live MCP](../shared/pcg-mcp.md) before editing the open Web graph. Obtain the user's explicit session choice and in-page AI-control approval as described in the live MCP contract; never auto-select a window. Echo the chosen label/ID/path, keep that ID on every live call, discover the needed node schemas, apply a coherent change, validate/cook as relevant, inspect affected output, and save the intended graph. Preserve unrelated work.

For live-editor tasks, MCP is the authoring surface. A saved-file edit is not a substitute for an acknowledged canvas update. Recover an unavailable editor when possible; otherwise report that live work is blocked rather than claiming it was applied.

## Read as needed

| Task | Reference |
| --- | --- |
| Reconstruct from one or more images | [Reference workflow](../shared/reference-workflow.md); [three-view details](triview.md) only for orthographic constraints. |
| Saved-graph visual acceptance | [Web review](web-review.md). Live Preview is useful during edits; final saved output needs a clean review. |
| Complete materialized/exported asset | [Web asset workflow](../shared/pcg-web-complete-asset/workflow.md). |
| Reusable generator or reliability request | [Generator validation](../shared/generator-validation.md). |
| Helper commands or wiring example | [Scripts](scripts.md) or [examples](examples.md), only the relevant section. |

Use the supplied views; do not demand a three-view set for an ordinary single-image task. Continue useful corrections within scope, and report the saved path, actual validation/render evidence and residual limitations. Full-asset acceptance applies only when a full asset was requested.
