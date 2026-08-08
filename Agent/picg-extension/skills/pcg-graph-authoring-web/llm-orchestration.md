# LLM Orchestration for PCG Graph Authoring (web)

Adapted from [img2threejs](https://github.com/img2threejs/img2threejs) orchestration patterns. This doc defines **how the agent should think and loop** before writing `.pcg` JSON. Runnable scripts: [`../shared/pcg-scripts/`](../shared/pcg-scripts/) · cheatsheet: [`scripts.md`](scripts.md) · **web clean-page review (P0):** [`web-review.md`](web-review.md).

Skill id: **`pcg-graph-authoring-web`**.

## Core philosophy

| Principle | img2threejs | PCG-AI equivalent |
|-----------|-------------|-------------------|
| Output is code, not a neural mesh | TypeScript `THREE.Group` factory | `.pcg` graph JSON + pcg-server cook |
| Scripts enforce structure | `validate_sculpt_spec.py`, gates | `validate_pcg.py`, manifest |
| LLM judges visuals | Agent vision on comparison sheet | Agent vision on web screenshot |
| Spec before codegen | `ObjectSculptSpec` JSON | Graph Authoring Plan + module table |
| Staged passes | blockout → material → … | module-plan → blockout → assembly → materials → validate |
| Local knowledge, not memory | BM25 on `docs/specs/vocabulary/*.jsonl` | `rule_search(domain=pcg)` + `vault_get_chunk` |
| Controlled vocabulary | `grimoire/glossary/3d_vocabulary.md` | PCG AI Rule + manifest property names |
| One correction action per cycle | `continue \| refine-spec \| refine-code \| …` | Same, mapped to graph/spec/cook |

**Division of labor:** `validate_pcg.py` checks JSON contract, pins, layout, parameter bindings, Merge→Bevel warnings. It does **not** judge silhouette fidelity. The agent inspects web screenshots for that.

## The loop (agent runs this; scripts gate structure)

**P0 — do not skip steps 0–4.** Skipping `new_authoring_plan.py` / `archive_reference.py` / `validate_plan.py` and editing an existing graph directly caused brickify web-dev regressions (2026-08-08). Always start from a plan; run `vault_search` at step 2 before the first node write.

```text
0. pcg-server + Vite dev server: check_server.py (web-review.md) — required before visual review
   → start with Shell block_until_ms: 0 (NOT nohup/&); re-check_server before every cook cycle
0.5. new_authoring_plan.py → archive_reference.py  (reference to disk BEFORE anything visual)
1. Layered observation → write observation.layers + visualTokens INTO *-plan.json (not chat)
2. rule_search + vault_search (local spec evidence — mandatory; record hits in plan.localRuleHits)
3. Fill Graph Authoring Plan (*-plan.json)
4. validate_plan.py --strict-quality  (blocks shallow plans AND unarchived references)
5. report_pass.py --resume → current unlocked pass + next command + RESUME.md
6. Author .pcg for CURRENT PASS ONLY (do not one-shot 80+ nodes when modularizing)
7. validate_pcg.py --check-server http://127.0.0.1:17890 → fix structural + server parity errors
8. setup_web_review.py → review URL (http://localhost:5173/review?graph=...) (fixed; no ask)
9. Vite /review route: load graph → cook via pcg-server → PreviewViewport → __pcgReady
10. capture_webview_png.py → Playwright screenshots the WebGL canvas
11. make_comparison_sheet.py → one side-by-side PNG (no score)
12. Agent vision → append_review.py (exactly ONE action; reference + vision notes required)
13. report_pass.py --resume / orchestrate_passes.py sync → next pass or stop
```

Run validation after every substantive edit, not only at the end. Script flags: [`scripts.md`](scripts.md). Never screenshot a cluttered editor page — see [`web-review.md`](web-review.md).

## Reference persistence and re-hydration (P0)

The reference image is **not** durable memory. Context compaction drops pasted
images; URLs rot; late stages (material, texture, final acceptance) are too far
from step 1 to trust recall. Three rules close the gap:

1. **Archive first (P0).** Immediately after `new_authoring_plan.py`, run
   `archive_reference.py --image <path|URL|data-URI> --plan <plan.json>`. The
   plan's `sourceImage` is rewritten to `ref_<slug>.<ext>` on local disk; the
   original source stays in `referenceArchive.originalSource`. Never point
   `make_comparison_sheet.py --reference` at a chat attachment or URL.
2. **Observation lives in the plan, not in chat.** Fill `observation.layers`
   (all 8 layers) and distill `visualTokens` (hex colors, roughness/metalness
   ranges, key ratios, named dimensions) into `*-plan.json` before authoring.
   `validate_plan.py --strict-quality` blocks authoring when the reference is
   unarchived or fewer than 6 layers are filled. Material/final stages consult
   `visualTokens` data instead of recalling the image.
3. **Re-hydrate at every stage entry.** Before geometry, UV, material, texture,
   asset export, and final-acceptance work — and after any context compaction —
   re-read in this order: `*-RESUME.md` → `*-plan.json` (observation +
   visualTokens + reviewHistory) → archived reference → latest `cmp_*.png`.
   Regenerate the resume with `report_pass.py <plan> --resume` at each stage
   transition and after each review cycle.

Every visual-pass `continue` requires `--reference-screenshot` (the archived
file) and `--ai-vision-notes` in `append_review.py`; entries without them do
not unlock the next pass.

## Layered image observation (before shape analysis table)

Use **observation before inference**. Run layers in order; each feeds the plan and node choice.

| Layer | Observe (controlled terms) | Feeds |
|-------|---------------------------|-------|
| 1 Identification | object class, `primaryDomain` (prop/vehicle/building/weapon/character) | strategy rule (`pcg/vehicle`, …) |
| 2 Form & silhouette | primitives, symmetry, aspect vs named dimension | overall scale, primary node family |
| 3 Macro → meso → micro | assemblies → sub-parts → feature groups | `componentTree` → lanes / Subgraphs |
| 4 Spatial relationships | attached-to, flush-with, embed, overlap (3D object-space) | `TransformMesh`, attachment offsets |
| 5 Materials & surface | metalness, roughness, normal relief, translucency | `AssignMaterial`, `VertexColor`, texture nodes |
| 6 Color & finish | hue/value/saturation, gradient stops, matte/gloss zones | material names, `ProjectTexture` crops |
| 7 Identity features | wear, fasteners, seams, decals, engraved lines | detail inventory → nodes |
| 8 Uncertainty | occluded, hidden back-face, blurry | `unknowns` (infer best-effort; prefer `refine-graph` over `request-input`) |

**Forbidden:** nice / sleek / aggressive / high-quality. **Required:** manifest-aligned terms (`roughness`, `bevel amount`, `sweep crossSection`, `instanced rivet row`).

After this pass, write the results into `observation.layers` + `observation.shapeAnalysis` in `*-plan.json` (the Shape analysis table from `SKILL.md` goes there too), and distill colors/finish into `visualTokens`. Do not leave them only in the reply.

## Graph Authoring Plan (mandatory with reference image)

Before writing `.pcg` nodes, emit a short plan in the reply (or optional sidecar `*-plan.json` next to the `.pcg`):

```json
{
  "targetName": "Ghost Protocol Glock",
  "sourceImage": "ref_ghost-protocol-glock.png (archived local file, never a URL)",
  "referenceArchive": { "archivedPath": "…/ref_ghost-protocol-glock.png", "originalSource": "https://…" },
  "observation": { "layers": { "identification": "compact pistol", "…": "…" }, "shapeAnalysis": "…" },
  "visualTokens": { "proportions": ["slide length ≈ 2× grip height"], "materialPalette": ["grip candy-ruby #8e1230, roughness 0.25-0.35"] },
  "complexity": "complex",
  "objectClass": { "primaryType": "pistol", "primaryDomain": "object" },
  "qualityContract": {
    "definitionOfDone": [
      "Silhouette matches reference at primary view",
      "Every listed macro part has a node chain or Subgraph",
      "No MergeMesh→BevelMesh on heterogeneous assembly"
    ],
    "minimumMacroParts": 5,
    "minimumMesoParts": 8,
    "reviewViewpoints": ["primary", "three-quarter"]
  },
  "detailInventory": [
    {
      "id": "slide-serrations",
      "kind": "linework",
      "region": "rear slide top",
      "mapsTo": { "nodeId": "slide_serration_inst", "property": "spacing" },
      "confidence": 0.85
    }
  ],
  "unknownsToResolve": ["underside trigger guard curve"],
  "localRuleHits": ["pcg/assembly-bevel", "pcg/vehicle"],
  "buildPasses": [
    { "id": "module-plan", "status": "done" },
    { "id": "blockout", "status": "pending", "componentRefs": ["body", "slide", "grip"] }
  ]
}
```

Shallow plans block authoring: a complex assembly with one `CreateBoxMesh` root and prose-only details is **not** implementation-ready.

## Detail inventory → node mapping (P0)

Every identity-defining detail must map to a **real graph target**, not prose only.

| Detail kind | PCG expression (examples) |
|-------------|---------------------------|
| bevel / chamfer | per-part `BevelMesh` before `MergeMesh` |
| fastener / rivet | `InstanceAlongSpline` or `CopyMeshToPoints` |
| groove / seam | profile spline + narrow `SweepAlongSpline` or bevel on edge group |
| taper | `SweepAlongSpline` `scaleStart` ≠ `scaleEnd` |
| stain / wear | `VertexColor` region or `AssignMaterial` variant |
| decal / painted line | `ProjectTexture` / `UVTexture` on named material |
| hole / socket | extrude with holes, or boolean-style cut via profile holes |
| gloss zone | separate material with lower roughness (Three.js material) |

Prose-only details are gate failures — if the inventory says "row of slide serrations" but no instancing/sweep node exists, fix the graph or downgrade the claim.

## Staged build passes (PCG)

Unlock passes in order; do not dump the full graph when modularizing.

| Pass | Scope | Agent delivers |
|------|-------|----------------|
| `module-plan` | Subgraph boundaries, lane list | Module table in reply |
| `blockout` | Macro parts, coarse proportions | Skeleton nodes + `MergeMesh` stub |
| `structural` | Meso parts, attachments | Per-lane chains wired |
| `form-refinement` | Profiles, sweeps, subdiv pre-bevel | Correct primitives per part |
| `bevel-pass` | Per-part `BevelMesh` | No post-merge blind bevel |
| `assembly` | `MergeMesh` → `Output` | Full topology |
| `material-pass` | `AssignMaterial`, colors, UV | Materials on named parts |
| `parameters` | `parameters[]` auto recommended / user-listed / `[]` | Inspector bindings synced |
| `validation` | `validate_pcg.py` clean | Zero errors; warnings addressed |

For simple single-spine graphs (<15 nodes), passes may collapse — still run observation + plan + validate.

## Quality contract (before first node)

Define **definition of done** specific to the reference:

- Weak: `make it look like the photo`
- Strong: `slide length ≈ 2× grip height; rear sight is a distinct box on slide top; candy-ruby finish on grip uses projected reference crop, not flat albedo`

Complexity tiers (align `detailInventory` minimums):

| Tier | Macro | Meso | Min details |
|------|-------|------|-------------|
| simple | 1 | 0–2 | 3 |
| moderate | 2 | 3+ | 6 |
| complex | 3+ | 8+ | 10 |
| ultra-complex | 5+ | 16+ | 16 |

Weapon/skin subjects with patterned finishes: treat as **complex+** even if bare geometry looks simple.

## Local spec search (mandatory)

**Reference hygiene:** curated graphs live in Vault `PCG AI Rule/Golden Graphs/` (**.ragignore** — Glob/Read disk only, not RAG). Do **not** load workspace `examples/**/*.pcg` as local specs. Skill `examples.md` is wiring-only.

Mirror img2threejs `localSpecSearch` — **pipeline stage, not optional memory**:

1. `rule_search(query="编图 + 模型类型 + 意图", domain=pcg, top_k=10)`
2. `vault_get_chunk` for `pcg/graph-contract`, `pcg/assembly-bevel`, matching type rule
3. Record `rule_id` hits in the plan (`localRuleHits`)
4. Build graph from returned evidence; do not invent domain topology when a rule exists
5. If `rule_search` fails: read `VAULT_ROOT/PCG AI Rule/Graph Authoring/` files directly (fallback paths in `SKILL.md`)

## Self-correction (one action per review cycle — autonomous)

After cook + screenshot: pick **one** action, apply it, and **immediately** run the next cook/review cycle. Do **not** pause for user approval between cycles.

| Action | When |
|--------|------|
| `continue` | Current pass goals met; advance to next pass |
| `refine-plan` | Wrong module split, missing part in inventory, wrong strategy rule |
| `refine-graph` | Plan sound but nodes/wiring/values wrong |
| `refine-cook` | Graph correct but web preview wrong (cook result, material binding, scale) |
| `request-input` | **Last resort only** — reference unusable, or pcg-server/Vite blocked (see SKILL Autonomous mode). Prefer infer + refine |
| `stop` | Fidelity ≥ **0.9** and DoD met; **or** 12 refine cycles with no measurable improvement; **or** pipeline GAP (dev skill) |

Root-cause guide (img2threejs-aligned):

- **refine-plan:** wrong part list, underestimated complexity, detail only in prose, wrong node family in strategy
- **refine-graph:** spec clear but wrong spline points, bevel on merged soup, scale drift, missing `__nodeTitle`
- **refine-cook:** `.pcg` validates but web preview shows wrong geometry / zero scale / wrong cook result

**Anti-pattern:** emitting `request-input` / `stop` after the first mediocre sheet "to ask the user whether to continue." Keep refining until the stop criteria above.

Record each cycle briefly:

```text
Pass: form-refinement | Action: refine-graph
Changed: slide profile catmullRom points; added meso rear sight box
Evidence: reference shows stepped sight, not flat slide top
Still wrong: grip candy gradient — needs material-pass + reference crop
Fidelity~: 0.55 → next cycle (no ask)
```

## Transparency (do not over-claim)

From img2threejs production lessons:

- List what changed each pass with evidence (node ids, property values)
- Name what still does not match the reference
- Never report "done" when only "improved"
- `validate_pcg.py` PASS does not prove visual fidelity
- 2D screenshot match does not prove 3D thickness/bevel realism — check a three-quarter view for hard-surface props

## Fidelity scale (reference-image jobs)

| Score | Meaning |
|-------|---------|
| 0.2 | Placeholder boxes only |
| 0.4 | Silhouette recognizable, structure incomplete |
| 0.6 | Macro/meso mostly correct, materials weak |
| 0.75 | Reads correctly in web view, details approximate — **not done**; keep refining |
| 0.85 | Strong real-time match — still refine toward 0.9 on reference-image jobs |
| 0.9 | Near-reference match — **default autonomous stop target** |
| 0.95+ | Usually needs multi-view or manual art |

Do not claim 0.95+ from one ambiguous photo unless the object is simple and symmetric. Under autonomous mode: if score < 0.9, choose `refine-*` and continue; do not ask the user to green-light another round. Do not `stop` at 0.75–0.85 and call it finished.

## Scripts (shared + web-specific)

| img2threejs | PCG-AI script | Role |
|-------------|---------------|------|
| `new_pre_spec_assessment` / `new_sculpt_spec` | `SHARED_SCRIPTS_DIR/new_authoring_plan.py` | Starter `*-plan.json` |
| `validate_sculpt_spec --strict-quality` | `SHARED_SCRIPTS_DIR/validate_plan.py --strict-quality` | Block shallow plans |
| `forge/next.py` | `SHARED_SCRIPTS_DIR/report_pass.py` | Current pass + next command |
| `orchestrate_passes.py` | `SHARED_SCRIPTS_DIR/orchestrate_passes.py` | status / check / sync |
| Browser render | **pcg-server** + Vite `/review` route + `scripts/web/setup_web_review.py` | Clean cook + Three.js PreviewViewport PNG |
| `make_comparison_sheet.py` | `SHARED_SCRIPTS_DIR/make_comparison_sheet.py` | Reference vs web screenshot sheet |
| `append_review.py` | `SHARED_SCRIPTS_DIR/append_review.py` | Review history + pipeline advance |
| (graph validate) | `SHARED_SCRIPTS_DIR/validate_pcg.py` | `.pcg` JSON / layout / pins; `--check-server` for manifest-server parity |
| (cook decode) | `SHARED_SCRIPTS_DIR/parse_pcgr.py` | Decode PCGR binary from `/v1/cook` (error string, counts) |

Still optional / future: BM25 PCG vocabulary (`search_specs`-style), bounded `correction_loop.py`.

Reference clone: `/Users/djhuang/img2threejs` (or user's path).
