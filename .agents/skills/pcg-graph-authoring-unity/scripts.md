# Unity helper reference

Shared planning, layout, validation and comparison helpers are documented once in [script reference](../shared/script-reference.md). They are optional; an existing structured plan keeps its schema and agreed thresholds.

The following C# snippets are templates for an available Unity script-execution tool, not standalone C# programs:

| Template | Purpose |
| --- | --- |
| [create_review_scene.cs.txt](scripts/unity/create_review_scene.cs.txt) | Check scene safety, create an isolated scene and save to a fresh `.unity` path. |
| [setup_pcg_review_subject.cs.txt](scripts/unity/setup_pcg_review_subject.cs.txt) | Load the graph, create the review subject, request a cook and frame SceneView. |
| [capture_sceneview_png.cs.txt](scripts/unity/capture_sceneview_png.cs.txt) | Capture the current SceneView to a project-relative screenshot path. |

Replace `__REVIEW_SLUG__`, `__GRAPH_ASSET_PATH__` and `__SCREENSHOT_REL__` with validated, correctly escaped values before execution. Use a simple alphanumeric/hyphen/underscore slug and paths inside the intended project. Read the template before running it and inspect its result; a requested asynchronous cook is not proof that output is ready.

Use [Unity review](unity-review.md) for project selection, dirty-scene protection, waiting for cooked output and clean visual evidence. A script's success message or static graph check cannot establish prefab acceptance.
