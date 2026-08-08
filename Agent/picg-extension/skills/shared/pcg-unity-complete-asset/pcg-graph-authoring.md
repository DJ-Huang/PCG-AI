# PCG graph authoring standard

Use this shared standard in both skill variants. `SHARED_SCRIPTS_DIR` is `../pcg-scripts` (sibling directory), which owns the generic Python helper scripts. `AUTHORING_SKILL_DIR` is the sibling `pcg-graph-authoring-unity` directory, which owns the target-specific helpers: `schema/`, `scripts/unity/`, `examples.md`, `llm-orchestration.md`, `scripts.md`, and `unity-review.md`.

## Mandatory bootstrap

Before a new or materially rebuilt graph:

1. For visual work, select and ping the workspace Unity/Tuanjie instance, then follow `AUTHORING_SKILL_DIR/unity-review.md` end-to-end. Always use a new `Assets/PICGGenerator/Scenes/PcgReview_<slug>.scene`; do not judge a graph in a demo/cluttered scene.
2. Read `schema/node-manifest.json`. It is the sole source for node types, pin ids/types, and property defaults. Never invent a node type, pin id, or property.
3. Load `rule_search(query="PCG-AI 编图 + 装配倒角 + 模型类型关键词", domain=pcg, top_k=10)`. Read `pcg/project-engineering`, `pcg/index`, `pcg/graph-contract`, `pcg/assembly-bevel`, and the matching type rule. If the index is unavailable, read the matching files under `VAULT_ROOT/PCG AI Rule/Engineering/` and `VAULT_ROOT/PCG AI Rule/Graph Authoring/` directly.
4. Glob/read `VAULT_ROOT/PCG AI Rule/Golden Graphs/<class>/` from disk. That directory is `.ragignore`; do not RAG-search it. Do not mine repo `examples/**/*.pcg` or Unity demos for new authoring. Use `examples.md` only for short pin-wiring templates. Exception: open a graph the user explicitly asked to edit.
5. For a reference image, read `llm-orchestration.md` and `scripts.md`, perform the eight-layer observation, create and strictly validate a Graph Authoring Plan before authoring nodes.

Before writing nodes, record:

```text
Golden Graphs: hit <paths or none> | used for: topology|modules|none
Auto: params=baselines | graphParams=recommended|none | saveDir=<path> |
previewScene=Assets/PICGGenerator/Scenes/PcgReview_<slug>.scene | loop=on
```

## Plan before nodes

State visible facts separately from inferred choices. The plan must include the model parts, target scale, primary shape/cross section, material-slot intent, detail inventory, quality DoD, module candidates, and graph parameter candidates.

For a reference image, analyse identification, form, macro/meso/micro detail, spatial relationships, materials, colour/finish, identity features, and uncertainty. Select the graph primitive from the actual section:

| Shape | Use | Do not substitute |
|---|---|---|
| Perfect rectangular prism | `CreateBoxMesh` | a sweep merely for a box |
| Constant-section extrusion | `SweepAlongSpline` + line spline | `CreateBoxMesh` |
| Variable, curved, or trapezoidal section | `SweepAlongSpline` + closed Catmull-Rom cross section | a rectangular box stack |
| Revolved body | `RevolveMesh` profile or circular sweep | `CreateBoxMesh` |
| Assembly | Appropriate primitive per part | all boxes by default |

Use metres unless the AssetSpec explicitly asks for stylized scale. Anchor each primary object and all subparts to a plausible physical size; never mix centimetre control points with metre-scale transforms. The final geometry check tests that promise.

## Topology and modules

### Bevel

For an assembly, bevel each clean part before `MergeMesh`. A final `MergeMesh → BevelMesh` is forbidden unless all inputs have been fused into one connected manifold, share a scale family, and use an explicit edge selection. Set bevel amount per part—not from the combined AABB. A bevel under roughly two percent of the part maximum dimension is normally invisible and must be increased.

Use a pre-bevel `SubdivideMesh` level appropriate to the bevel: level 1 is a bare minimum for two segments; use level 2 for three or more segments. For smooth sweeps, keep `sampleSpacing <= 0.5`; use at least 16 circle columns for wheels and at least 8 for any circular section.

### Subgraphs

Consider a complete-module Subgraph when the root approaches 40 visible nodes, one lane is 8–12 nodes, a part repeats, or the user requests reusable modules. At roughly 80 root nodes, package the model into meaningful modules.

A valid module is a nameable product part with coherent I/O, is finished enough to merge, can be edited independently, and is either reused or large enough to clarify the root. Do not encapsulate tiny `Box → Transform` stubs, unrelated parts, mid-pipeline fragments, or everything except Output. Use one definition for a repeated prototype and place its instances at the root.

Definitions belong in root `subgraphs[]`; instances are `type: "Subgraph"` with `data.subgraphId`. Declare input/output ports, use the port id on both interior and root edges, give every interior/instance node a unique `data.__nodeTitle`, and never recurse. Read the project's Subgraph tutorial before writing a new interface.

### Buildings and oriented scatter

For a facade with a door and windows, reserve a door-clearance zone widened by at least 0.15–0.20 m on each relevant tangent/height edge; no window AABB may enter it. Separate the ground-floor door bays from upper grids rather than hiding an overlap with Z offset.

For buildings, vehicles, signs, and other front-facing prototypes, declare a front axis and write a true yaw/orientation toward the access path before copy/spawn. Do not feed a horizontal access vector into the `N` attribute alone when that would tip local Y; small random yaw can only follow a valid facing orientation.

## Graph document contract

Use this envelope:

```json
{
  "version": "1.0",
  "nodes": [],
  "edges": [],
  "parameters": [],
  "subgraphs": []
}
```

- Use semantic snake-case ids and a unique, short `data.__nodeTitle` for every root and interior node. The Unity graph shows `__nodeTitle`, not `id`.
- Use manifest pin ids for every `sourceHandle` and `targetHandle`; require compatible pin types.
- End every runnable graph in `Output` at the bottom.
- Keep node data at manifest defaults unless a planned, validated override is needed.
- Encode `CreateSpline.controlPoints` as a JSON string and use bindings rather than serializing Unity scene-object references.
- Use plain decimal numbers in the manifest; Unity's mini JSON parser does not accept scientific notation reliably.

## Parameters

Keep authoring defaults in node `data`. Add root `parameters[]` only for recurring artist controls, generator variation, or values the user explicitly requested. The usual set is 2–6 high-leverage controls such as floor spacing, seed, style switch, or repeated density; one-shot static props may have none.

Every parameter must target an existing root node/property, use the manifest property type, keep `default` equal to the baked node value, and have a sensible range for numeric controls. Do not invent multi-target/expression bindings or target a subgraph interior node. After a change, validate binding and default synchronization.

## Layout

Lay out data flow top-to-bottom. Use `ROW_STEP_Y = 160`, `SPINE_X = 200`, and `COL_STEP_X >= 320`. A same-row node title occupies about 300 px, so never pack unrelated siblings closer than one column step.

Use one vertical lane per final assembly input: assign each ancestor to a body/wheel/door/etc. lane, keep a lane's chain vertically aligned, use sub-lanes only for true siblings, then center final merge/output under the lanes. A global topo-row grid, long cross-lane diagonals, horizontal pipelines, dense X packing, and duplicate titles are layout failures. When editing an old graph, relayout it without changing valid geometry data.

### Dedicated root/Subgraph layout pass

Treat the root graph and every inline `subgraphs[]` definition as separate layout scopes. After authoring or materially changing a graph, run the deterministic helper before static validation:

```bash
python3 <SHARED_SCRIPTS_DIR>/layout_pcg.py <graph>.pcg \
  --out <graph>-layout.pcg
```

The helper changes only node `position.x/y`. It derives topological rows, assigns one lane per independent source component, centers multi-input joins over their source lanes, and separates same-row collisions by at least the configured column step. Use `--in-place` only after reviewing the generated copy; use `--subgraph <id> --skip-root` when relayout is intentionally limited to one inline definition.

Run `validate_pcg.py` on the relaid copy and fix every Subgraph lane, fan-in centering, same-row spacing, and long-wire warning before replacing the authored graph. Record:

```text
Subgraph layout: PASS | scopes=<n> | nodes=<n> | moved=<n> | position-only=PASS
```

## Author, validate, and cook

1. Use `new_authoring_plan.py`, `validate_plan.py --strict-quality`, and `report_pass.py` for reference-image work. Author one pass at a time rather than a one-shot 80-node root.
2. Determine save paths by directory glob only: prefer Unity PCG folders, then `examples/`, then the workspace fallback. Do not inspect graph bodies to choose a directory. Keep paired Unity/demo graph copies identical when both are intended.
3. Run the dedicated root/Subgraph layout pass, then `validate_pcg.py`, and fix every error. On new/relayout graphs, treat layout, title, broken parameter, tiny-subgraph, flat-megagraph, and mixed-assembly bevel warnings as must-fix.
4. Cook in the clean review scene, capture SceneView, generate a comparison sheet, and make one documented correction action per review cycle. A visual script cannot score the image; agent judgement on the sheet does.
5. Continue geometry/material passes until the active DoD is met. Do not claim visual fidelity if Unity MCP is unavailable.

Use `AUTHORING_SKILL_DIR/scripts.md` for exact helper invocations. At the end of graph work, hand off the valid cooked result to `geometry-validation.md`; material and texture work are later shared stages, not reasons to skip this handoff.
