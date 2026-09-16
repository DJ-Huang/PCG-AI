# Graph stage for a Unity asset

Use the shared [graph authoring contract](../graph-authoring.md). For a graph open in the Web editor, use [live MCP](../pcg-mcp.md); an explicitly file-based Unity task may use a reviewable saved-file edit without claiming a live canvas update.

Use [Unity review](../../pcg-graph-authoring-unity/unity-review.md) for target-platform evidence. Reference work uses [reference-driven authoring](../reference-workflow.md); generator promises use [generator validation](../generator-validation.md).

Preserve compatibility with the active Unity importer. In particular, use plain decimal numeric literals where the project's mini JSON parser requires them; validate with the target importer rather than assuming a different parser's behavior.

For complete-asset delivery, continue with [geometry validation](geometry-validation.md). Graph-only work ends with the requested graph and applicable checks.
