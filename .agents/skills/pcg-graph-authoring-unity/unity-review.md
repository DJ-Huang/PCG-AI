# Unity / Tuanjie review gate (P0 — mandatory)

This skill is **Unity-bound**. Visual validation **must** go through the `user-tuanjie` MCP.
Do **not** judge fidelity from a cluttered existing scene, a random user screenshot, or memory.

Cheatsheet: [scripts.md](scripts.md) · C# templates: [`scripts/unity/`](scripts/unity/)

## Hard rules

1. **Connect Tuanjie before any cook / screenshot / SceneView judgment.**
2. **Always open or create a dedicated review scene** that contains **only** the graph under review (+ studio light + camera). Hide or do not load other demo content.
3. **Screenshot from that clean scene**, then `make_comparison_sheet.py` vs the reference.
4. Scripts never score visuals — agent vision does, after the sheet exists.
5. If no reachable Unity/Tuanjie instance matches the workspace: **stop and ask the user** (dev-gate §7). Do not guess another project.

## Connect protocol (every session / every review cycle)

Follow workspace **dev-gate §7** exactly:

```text
1. unity_list_instances({ probe: true })
2. Prefer instance whose projectRoot is the current workspace Unity project
   (e.g. …/PCG-AI-cursor/Unity) or contains the workspace.
3. If 0 workspace-related OR ≥2 ambiguous → ask_user with pid / projectRoot / port.
4. unity_select_instance({ project: "<Unity project root>" })  # or unique pid/port
5. unity_ping → must succeed before manage_* / execute_csharp_script / read_console
```

MCP server id: **`user-tuanjie`**.

Receipt line after connect (required in the agent reply):

```text
Unity MCP: connected pid=<pid> project=<projectRoot> port=<port>
```

If ping fails: report `Unity MCP: unavailable` and **do not** claim visual pass/`continue` on a visual build pass.

## Clean review scene (no interference)

### Fixed preview directory (P0 — never ask)

| Item | Value |
|------|-------|
| Preview scene dir | **`Assets/PCG-AI-Workspace/Scenes`** |
| Scene file | `Assets/PCG-AI-Workspace/Scenes/PcgReview_<slug>.scene` |

Always create / overwrite Preview scenes here. **Do not** `ask_user` for scene location. **Do not** use `Assets/Scenes/` unless the user explicitly overrides. Ensure the folder exists (template mkdir) before save.

### Why

Prior demos (lot-city, bridges, other props) in the same SceneView pollute silhouette, scale, and color judgment. Hiding objects ad-hoc is fragile; a **new empty review scene** is the default.

### Procedure

```text
A. Record previous active scene path (manage_scene get_active) so you can restore later if the user wants.
B. Create a fresh review scene (prefer C# to avoid path quirks):

```text
execute_csharp_script: scripts/unity/create_review_scene.cs.txt
  → Assets/PCG-AI-Workspace/Scenes/PcgReview_<slug>.scene
```

   Or MCP: `manage_scene` action `create` with
   `params: { "name": "PcgReview_<slug>", "path": "Assets/PCG-AI-Workspace/Scenes" }`
   → file at `Assets/PCG-AI-Workspace/Scenes/PcgReview_<slug>.scene`.
   **Do not** pass a path that already ends in `.scene` as the directory (MCP may nest
   `….scene/….scene`).
C. execute_csharp_script with scripts/unity/setup_pcg_review_subject.cs.txt
   - Pass GRAPH_ASSET_PATH (Unity asset path to .pcg / PcgGraphAsset)
   - Optional MATERIAL_DIR for AssignMaterial bindings
   - Creates GO "PCG_Review_<slug>" with PcgGraphComponent, Directional light, Camera
   - Cooks via RequestPreviewCook(immediate: true)
   - Frames SceneView.lastActiveSceneView on the cooked bounds
D. read_console (errors) — fix cook errors before scoring
E. execute_csharp_script with scripts/unity/capture_sceneview_png.cs.txt
   - Writes Unity/screenshots/SceneView_<stamp>.png (project-relative)
F. python3 ../shared/pcg-scripts/make_comparison_sheet.py --reference … --render … --out …
G. Agent vision on the sheet → append_review.py (one action)
H. Optional: manage_scene load previous scene; leave PcgReview_* scene on disk for reruns
```

### Naming

| Item | Pattern |
|------|---------|
| Scene | `Assets/PCG-AI-Workspace/Scenes/PcgReview_<graph-slug>.scene` |
| Root GO | `PCG_Review_<graph-slug>` |
| Reference archive | `ref_<graph-slug>.<ext>` next to `*-plan.json` (via `archive_reference.py`; never a URL/chat attachment) |
| Screenshot | `Unity/screenshots/SceneView_YYYY-MM-DD_HH-MM-SS.png` |
| Comparison | `Unity/screenshots/cmp_<graph-slug>_<pass>.png` |

`<graph-slug>` = `.pcg` basename without extension (e.g. `ghost-protocol-glock`).

### Forbidden shortcuts

| Anti-pattern | Why |
|--------------|-----|
| Ask where to create the Preview scene | Path is fixed: `Assets/PCG-AI-Workspace/Scenes` |
| Write Preview scenes under `Assets/Scenes/` | Wrong default; use `Assets/PCG-AI-Workspace/Scenes` |
| Screenshot the user's busy Test / Demo scene | Other meshes dominate framing |
| Only `SetActive(false)` on siblings in a shared scene | Easy to miss lights/skyboxes/UI; state leaks |
| Score from an old screenshot without recook | Stale geometry |
| `continue` on visual pass without comparison sheet + Unity MCP ping | Violates orchestration gate |
| Feed the comparison sheet a URL / chat attachment as reference | Not durable; archive first (`archive_reference.py`) |
| Connect to a non-workspace Unity instance without ask_user | Wrong Library / wrong assets |

## Template placeholders

Replace before `execute_csharp_script`:

| Token | Example |
|-------|---------|
| `__GRAPH_ASSET_PATH__` | `Assets/PcgPlugin/Examples/PCGDemo/ghost-protocol-glock/ghost-protocol-glock.pcg` |
| `__REVIEW_SLUG__` | `ghost-protocol-glock` |
| `__SCREENSHOT_REL__` | `screenshots/SceneView_2026-07-31_22-40-00.png` |

Paths are Unity project-relative (`Application.dataPath/..` for disk writes under `screenshots/`).

## Interaction with authoring passes

| Pass | Unity MCP required? |
|------|---------------------|
| `module-plan` | No (plan/docs only) |
| `blockout` … `material-pass` | **Yes** — clean scene + sheet before `continue` |
| `parameters` | Yes if visual defaults change |
| `validation` | `validate_pcg.py` + Unity cook smoke if graph ships in Unity |

`append_review.py` requires `--reference-screenshot` (archived file), `--render-screenshot`, `--comparison-image`, and `--ai-vision-notes` for visual-pass `continue`.

## Minimal tool sequence (copy)

```text
CallMcpTool user-tuanjie unity_list_instances { probe: true }
CallMcpTool user-tuanjie unity_select_instance { project: "<ws>/Unity" }
CallMcpTool user-tuanjie unity_ping {}
CallMcpTool user-tuanjie manage_scene { action: get_active }   # remember prior scene
CallMcpTool user-tuanjie execute_csharp_script { script: <create_review_scene.cs.txt> }
CallMcpTool user-tuanjie execute_csharp_script { script: <setup_pcg_review_subject.cs.txt> }
CallMcpTool user-tuanjie read_console { action: get, types: ["error"], count: 30 }
CallMcpTool user-tuanjie execute_csharp_script { script: <capture_sceneview_png.cs.txt> }
Shell: python3 ../shared/pcg-scripts/make_comparison_sheet.py --reference … --render … --out …
# agent vision → append_review.py
```
