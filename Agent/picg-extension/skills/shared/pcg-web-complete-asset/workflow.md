# PCG Web complete-asset workflow

This is the single production workflow for both `pcg-graph-authoring-web` and `pcg-graph-authoring-web-dev`. Do not treat a `.pcg`, a cooked mesh, or a white model as the finished result.

`AUTHORING_SKILL_DIR` means the sibling `pcg-graph-authoring-web` skill directory. `SHARED_DIR` means this directory. `SHARED_SCRIPTS_DIR` means the sibling `pcg-scripts` directory. Resolve these paths before reading any reference; do not recreate a local copy when a shared reference is unavailable.

## Production invariant

Finish at `FINAL_ACCEPTED`, not `GRAPH_WRITTEN` or `WHITE_MODEL_APPROVED`. Both variants use the same AssetSpec, production stages, final score, hard failures, deliverables, autonomous defaults, and recovery rules. The development variant inserts `PCG_PIPELINE_VALIDATION` after graph authoring and before the white model is accepted.

```text
Requirement / AssetSpec
→ PCG plan and graph authoring
→ independent root/Subgraph layout + graph validation
→ PCG_PIPELINE_VALIDATION (dev only)
→ white-model cook and geometry validation
→ UV / projection validation
→ material plan
→ texture production and web import
→ web material creation and binding
→ exported asset output (glTF / web scene)
→ final render and full-asset acceptance
```

## Required read order

1. Read [asset-contract.md](asset-contract.md).
2. Read [pcg-graph-authoring.md](pcg-graph-authoring.md) and the linked authoring-skill references before authoring or changing a graph.
3. Read the stage reference immediately before that stage:
   - [geometry-validation.md](geometry-validation.md) before approving a cook;
   - [material-workflow.md](material-workflow.md) before choosing material slots or shaders;
   - [texture-workflow.md](texture-workflow.md) before generating or importing textures;
   - [web-integration.md](web-integration.md) before creating material assets, bindings, an exported asset, or a final render;
   - [final-acceptance.md](final-acceptance.md) before reporting success.
4. Read [error-codes.md](error-codes.md) whenever a stage fails. Repair the named upstream stage; never mask the error downstream.

For a reference-image job, also read `AUTHORING_SKILL_DIR/llm-orchestration.md`, `AUTHORING_SKILL_DIR/scripts.md`, and `AUTHORING_SKILL_DIR/web-review.md` before the first graph write or visual review. For front/side/top input, also read `AUTHORING_SKILL_DIR/triview.md`.

## Stage contract

| Stage | Required result | May advance only when |
|---|---|---|
| `ASSET_SPECIFIED` | Normalized AssetSpec and reference observations; explicit facts separated from inferences | target scale, components, material intent, output path, and acceptance target are known or responsibly inferred |
| `PCG_AUTHORED` | Manifest-valid `.pcg`, authoring plan, independent root/Subgraph layout receipt, parameter receipt | graph validation has no errors, layout receipt is `PASS`, and all graph rules are met |
| `PCG_PIPELINE_VALIDATED` (dev only) | Passing development validation receipt | graph structure, parameter behavior, regeneration, boundary cases, and AssetSpec output match pass |
| `WHITE_MODEL_APPROVED` | Clean-scene cook and geometry receipt | silhouette, dimensions, transforms, topology, and assembly all pass |
| `UV_APPROVED` | UV/projection receipt per material slot | each textured surface has suitable non-stretched coordinates or an explicitly validated projection |
| `MATERIALS_BOUND` | Material plan, imported texture assets, web material assets, and bindings | no missing, default, or placeholder material remains |
| `ASSET_EXPORTED` | Re-openable exported asset (glTF / web scene) with stable hierarchy and asset references | exported asset regenerates without stale or duplicate output |
| `FINAL_ACCEPTED` | Fully textured render sheet and final acceptance report | shared score and every hard-failure check pass |

## Autonomous behavior

Apply practical defaults for graph parameters, save directory, material values, texture size, and review page. Continue through all stages without asking for confirmation. Record assumptions and receipts in the plan/final reply.

Ask only when an input is genuinely unusable, the pcg-server is absent or unreachable, a required source asset is unavailable, or the user explicitly requests interactive control. A real pipeline capability gap in the development variant is also a blocker; see its gate reference.

On reference-image work, refine after every clean-scene render until the final fully textured asset meets the shared acceptance target or twelve consecutive cycles yield no measurable geometry, material, or texture improvement. Report residual gaps at the ceiling; do not silently lower the target.

## Handover receipts

Keep one concise receipt per stage. The minimum chain is:

```text
AssetSpec: <slug> | scale=<m> | refs=<paths> | materialSlots=<n>
PCG: <path> | manifest=PASS | graphParams=<n> | layout=PASS | subgraphLayout=PASS
Geometry: cook=PASS | silhouette=PASS | scale=PASS | topology=PASS
UV: PASS | slots=<n> | projectionExceptions=<n>
Materials: PASS | textures=<n> | bindings=<n>
Asset: <path> | hierarchy=PASS | regeneration=PASS
Final: FINAL_ACCEPTED | score=<0..1> | render=<path>
```

The dev variant adds its PCG pipeline receipt but does not replace any receipt above.
