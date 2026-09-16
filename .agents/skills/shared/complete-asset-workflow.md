# Complete-asset delivery

Activate only for a requested finished Web asset or Unity prefab. Graph-only work and requested blockouts end at their own deliverables. This document owns the shared progression; platform folders contain only the applicable production details.

## Outcome contract

Record an AssetSpec appropriate to the task: intent, reference sources, scale/frame, important components, geometry criteria, material slots, allowed variation, output paths and acceptance criteria. Existing asset specifications remain binding. Inferences and agreed exceptions must be explicit.

Read the platform's stage reference when entering that stage, not the entire library at startup:

| Stage | Required evidence |
| --- | --- |
| Graph | Schema-valid saved graph, readable root/Subgraph layout, correct parameters and native cook. |
| Geometry | Intended dimensions, silhouette, orientation, assembly, nonempty valid output and fresh visual evidence. |
| Surface | Suitable UVs/projection where needed, deliberate material slots, correct texture channels/imports/bindings. A deliberate flat-colour material is valid; a placeholder is not. |
| Integration | Durable Web export or Unity prefab with stable hierarchy, pivot and references. |
| Final review | Reload the actual deliverable, inspect it in the target platform, check every required view and report the remaining gaps. |

Use [generator validation](generator-validation.md) when generator behavior is part of the promise. It supplements, not replaces, visual and asset checks. Skip a stage only when it is genuinely inapplicable to the agreed asset (for example, texture import for an intentionally texture-free material), and explain that applicability. Never skip a required stage because its tool is unavailable.

## Acceptance and recovery

`FINAL_ACCEPTED` means every agreed deliverable and required check passed. A graph/cook error, broken assembly, failed required view, missing required surface data, placeholder/pink material, damaged output, broken reference or failed reload blocks acceptance. Unavailable verification is BLOCKED, not PASS. Repair the responsible upstream stage; do not hide geometry errors with materials or hide export failures with screenshots.

For an existing scored specification, preserve its threshold and weights. When a new task needs a scorecard, define it in the AssetSpec before review. The legacy rubric is geometry 0.35, materials/textures 0.30, technical construction 0.15, target integration 0.10 and presentation 0.10, with 0.90 for reference matching. These are reviewer judgments, not measured probabilities or proof of quality; no aggregate score overrides a hard failure. Tasks without a scorecard use explicit observable criteria rather than invented decimal precision.

Continue useful corrections without asking permission after every pass. Stop for a real blocker or a justified budget/diminishing-return boundary and deliver the verified portion with its limitations; do not relabel it as accepted or lower the target silently.

## Handoff

Keep the graph, material/texture sources where applicable, exported asset/prefab and render evidence at durable asset-specific paths. Report those paths, actual checks, seed/parameter state and exact residual gaps. Distinguish the saved graph, cooked geometry, target render and reloaded final asset; none alone proves all the others.
