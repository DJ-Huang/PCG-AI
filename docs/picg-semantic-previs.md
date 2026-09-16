# PICG Semantic Previs MCP

PICG exposes semantic scene assembly through direct MCP. The normal read path is:

```text
picg_list_editor_sessions -> picg_bind_editor_session
picg_describe_scene -> picg_get_component -> picg_get_recipe
```

Use `detail: "compact"` by default. Pass the returned `revision` as `sinceRevision` to receive a `notModified` receipt, and use `fields` to retain only selected top-level response fields. `detail: "full"` is reserved for the selected component, Recipe, or final graph inspection.

## Semantic ownership

Complete scene semantics live on one final output node as `data.__semantic`, or on a reusable Subgraph definition as `semantic`. `memberNodeIds` contains the owner and the other nodes in that component's current scope. Ordinary nodes may belong to only one main component. Shared inputs remain dependencies rather than duplicated members.

Library definitions keep generic semantics. `picg_instantiate_library_items` reuses one inline definition and creates a stable instance node plus a final `TransformMesh` owner with a unique scene `componentId`.

Recipes live under `.pcg-ai/kb/recipes/` and are addressed by frontmatter `recipe_id`. They describe shared structure and safe edits; they do not contain scene-instance facts.

## High-level previs

`picg_apply_previs_spec` accepts `libraryItems`, `instances`, `rooms`, `props`, and `characters` as Library instance declarations, plus `shot`, `camera`, and `shotOperations`. It validates the new Graph and all Shot operations before publishing either document. Stable `instanceId`, component, camera, and keyframe IDs make repeated submissions idempotent.

Object timeline operations are `upsert_component`, `set_object_keyframes`, `set_action_clip`, and `set_visibility_range`. Camera operations remain available in the same `.picgshot` document.

## Local delivery

Allowed destinations are configured in `.pcg-ai/output-roots.json`. `picg_save_project` writes the matching `.picg`, `.picgshot`, and `.picgproject` files as one rollback-safe batch from `outputRootId + relativePath`. Absolute relative paths, parent traversal, and symlink escape are rejected.

`picg_export_shot` can additionally receive `outputRootId` and `relativePath`; the Web editor encodes the shot, then the server copies the real MP4 or WebM into the same allowlisted delivery root.

The current `what-if-tlou` root targets `/Users/djhuang/MyMovieDesign/PROJECTS/What-If-TLOU/previs`.
