---
name: pcg-graph-authoring
description: Route the generic PCG authoring command to Web or Unity when the target platform is not already selected.
---

# Select an authoring target

Use the user's explicit target first, then the graph's workspace and active editor context. Do not infer Unity merely from the `.pcg` extension.

- Web editor or Web asset: [pcg-graph-authoring-web](../pcg-graph-authoring-web/SKILL.md).
- Unity project, prefab or Unity review: [pcg-graph-authoring-unity](../pcg-graph-authoring-unity/SKILL.md).

Read only the selected skill. When both targets are explicitly requested, share the graph work but verify each target independently. Ask one focused question only when the target remains materially ambiguous after inspecting available context.

This compatibility entry is a router, not another workflow. It does not require an installed Cursor skill, force a platform, or expand a graph-only request into a complete asset.
