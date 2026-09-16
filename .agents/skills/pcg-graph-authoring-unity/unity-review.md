# Unity visual review

Unity acceptance requires evidence from the intended Unity project. Web previews and static graph validation do not establish Unity imports, shaders, bindings or prefab behavior.

## Connect without changing the wrong project

Discover the available Unity connector's tools. A typical flow is list instances, select the matching project root, then ping; tool names and server IDs can vary by installation. Use request/workspace context to resolve the instance, and ask only when no correct instance is reachable or several remain ambiguous. Never choose an unrelated project to make a check pass.

## Preserve scene state

Record the loaded scene setup and active scene before review. Do not discard unsaved changes, silently save user scenes, or overwrite an existing review scene. The [creation template](scripts/unity/create_review_scene.cs.txt) refuses dirty/unsaved populated scenes and uses a fresh `.unity` path under `Assets/PICG-Workspace/Scenes`. Resolve any blocked scene state with the user or an explicitly safe workflow before continuing.

Create an isolated scene containing only the review subject, intended lighting and camera. Preserve existing review files; reuse a known task-owned scene only deliberately. Restore the previous scene setup when safe, or state the resulting editor state. Review paths are defaults, not permission to overwrite unrelated assets.

## Cook, capture and inspect

Use [setup_pcg_review_subject.cs.txt](scripts/unity/setup_pcg_review_subject.cs.txt) with the intended graph asset. Wait for the requested cook to finish and confirm nonempty current output; the script returning after `RequestPreviewCook` is not completion evidence. Check console errors, transforms, bounds and material bindings before visual judgment.

Capture the isolated SceneView using [capture_sceneview_png.cs.txt](scripts/unity/capture_sceneview_png.cs.txt) or an equivalent available capture tool. Match the supplied reference viewpoint(s), inspect a depth/assembly view where needed, and keep fresh evidence linked to the current graph. Compare against archived sources using the [shared helper](../shared/script-reference.md). Do not score a cluttered demo scene, stale capture or missing output.

For a complete-asset request, save and reload the actual prefab and inspect its persistent mesh/material references and final appearance. A temporary scene object is not prefab evidence. Report project/graph/prefab paths, cook and console results, screenshots and any blocked checks; preserve the [complete-asset acceptance contract](../shared/complete-asset-workflow.md).
