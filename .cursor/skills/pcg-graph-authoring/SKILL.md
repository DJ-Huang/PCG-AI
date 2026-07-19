---
name: pcg-graph-authoring
description: >-
  Author PCG Graph JSON (.pcg) files for the PCG-AI repo with Houdini-style
  top-to-bottom layout, module-level Subgraphs, and graph-level Parameters
  (Blackboard) for Inspector controls. Use when creating, editing, or scaffolding
  .pcg graphs, bridge/spline/mesh/scatter/building demos, Subgraph/Subnet
  packaging, exposing Parameters, or when the user mentions PCG graph, node
  wiring, bridge-demo, or Houdini layout.
---

# PCG Graph Authoring (PCG-AI)

Create `.pcg` files for this repo. **Layout is Houdini-style: data flows top → bottom**, not left → right. Large multi-part assemblies use **Subgraphs for complete modules** (not for tiny stubs) — see [Subgraph modularization](#subgraph-modularization-p0--modules-first-not-count-first). When the graph needs Inspector / HDA-like controls, write root **`parameters[]` (Graph Parameters)** bound to node properties — see [Parameter exposure](#parameter-exposure-ask-user-before-writing).

## Before writing

1. Read `schema/node-manifest.json` — SSOT for node `type`, pin ids, pin types, property defaults.
2. **[Mandatory] Load PCG rules via vault RAG** — `rule_search(query=装配倒角 OR 模型类型关键词, domain=pcg, top_k=10)`. Always expect `pcg/assembly-bevel` + `pcg/index`; open matching type (`pcg/vehicle`, `pcg/bridge`, …) with `vault_get_chunk` or Read `VAULT_ROOT/Rules/pcg/<name>.md`. If no matching type, derive from shape analysis and note the gap.
3. Skim a similar example under `examples/` (see [examples.md](examples.md)) for **topology patterns** only (how nodes wire together), not for modeling strategy.
4. Never invent node types or pin ids; copy from manifest. `Subgraph` / `SubgraphInput` / `SubgraphOutput` are structural (see tutorial); ports come from the definition, not the manifest.

## Shape analysis (mandatory when user provides a reference image)

Before choosing any node, analyze the reference image and record the analysis in your reply:

| Aspect | What to identify | Affects |
|--------|-----------------|--------|
| **Body shape** | Box / variable-section / revolve / scatter assembly | Primary node choice |
| **Cross-section** | Constant / variable / symmetric / asymmetric | Sweep vs Box vs Loft |
| **Side profile** | Rectangular / trapezoidal / wedge / curved | Profile spline control points |
| **Surface quality** | Hard edges (low-poly) / light bevel / smooth curves | BevelMesh amount, SubdivisionMesh levels |
| **Component list** | Every visible part and its shape type | Per-part node selection |

Record the analysis as a short table in your reply before writing any nodes.

## Node selection guide

### By body shape

| Shape | Recommended nodes | Do NOT use |
|-------|-------------------|------------|
| Constant-section extrusion | `SweepAlongSpline` (circle/rectangle) + `CreateSpline` (line) | `CreateBoxMesh` |
| Variable-section body | `SweepAlongSpline` (crossSection) + `CreateSpline` (catmullRom, closed) | `CreateBoxMesh` ❌ |
| Perfect rectangular prism | `CreateBoxMesh` | `SweepAlongSpline` |
| Revolve body | `RevolveMesh` (profile spline) or `SweepAlongSpline` (circle) | `CreateBoxMesh` ❌ |
| Multi-part assembly | Choose per-part by above rules; do not default everything to Box | All `CreateBoxMesh` ❌ |

### Key decision rules

1. **Does the cross-section change along the length?**
   - Yes → `SweepAlongSpline` + `CreateSpline` (crossSection, catmullRom). Use `scaleStart`/`scaleEnd` if taper is uniform.
   - No, and the shape is a perfect rectangle → `CreateBoxMesh`.

2. **Does the reference image show curved / trapezoidal / non-rectangular cross-sections?**
   - Yes → Must use `CreateSpline` (catmullRom, closed) to define the section. `CreateBoxMesh` can only produce rectangular sections.
   - No → `CreateBoxMesh` is acceptable.

3. **Does the reference image show tapering (narrow at one end, wide at other)?**
   - Yes → `SweepAlongSpline` with `scaleStart` ≠ `scaleEnd`.
   - No → `scaleStart = scaleEnd = 1.0`.

4. **Is the part a small detail?**
   - If rectangular → `CreateBoxMesh` + `TransformMesh` (scale + translate) is fine.
   - If curved → `SweepAlongSpline` or `CreateSpline` profile.

5. **Anti-pattern: using `CreateBoxMesh` for everything in an assembly.**
   - This is the most common mistake. Even for low-poly styles, bodies with any taper or non-rectangular section must use `SweepAlongSpline`. Reserve `CreateBoxMesh` for parts that are genuinely rectangular.

6. **Anti-pattern: `MergeMesh → BevelMesh` on multi-part assemblies.**
   - See [Bevel placement](#bevel-placement-p0-for-assemblies). Never put a single final Bevel after merging heterogeneous parts.

### Bevel placement (P0 for assemblies)

Houdini hard-surface practice: **PolyBevel on one clean solid (or a Grouped selection on that solid), then Merge parts** — not the reverse. SideFX PolyBevel is topology-sensitive; forums commonly fail when beveling after Merge of separate objects without Fuse into a single manifold.

#### ❌ Forbidden default (high risk)

```text
partA / partB / hose / details → MergeMesh → BevelMesh → Output
```

| Risk | Why |
|------|-----|
| Scale mismatch | One `amount` cannot fit body (~meter) and hose/rim (~cm); small parts self-intersect or vanish |
| Multi-component soup | `MergeMesh` keeps disconnected solids; Bevel corner/mitre logic assumes local manifold neighborhood |
| Wrong edge selection | Angle/group limits applied globally pick noise edges on high-res sweeps and miss intended hard edges |
| Topology damage amplified | Attribute nodes that break n-gon topology before Merge make post-merge Bevel worse (see vault `pit-pcg-geometry-attribute-roundtrip`) |

**Historical bad example** (fixed 2026-07-16): `biohazard-canister.pcg` once ended with `merge → bevel → out`. Rule SSOT: `Rules/pcg/assembly-bevel.md` (`rule_id: pcg/assembly-bevel`).

#### ✅ Correct patterns

| Scenario | Topology |
|----------|----------|
| Single solid | `… → [GroupCreate] → BevelMesh → Output` |
| Multi-part assembly | **Per-part** `… → BevelMesh` (amount scaled to that part) → `MergeMesh` → Output |
| Main body + un-beveled details | `body → BevelMesh` → `MergeMesh ← details` → Output |
| Houdini Group style | `GroupCreate(angle)` → `BevelMesh(edgeGroup)` on **one** part, then Merge |

Strategy rules already follow this (`pcg/assembly-bevel`, `pcg/vehicle`, `pcg/bridge`: Bevel on deck/body **before** Merge of piers/wheels/details).

#### When post-merge Bevel is acceptable

Only if **all** are true:

1. Inputs were Boolean-unioned / fused into **one** connected manifold (not a visual stack of separate solids)
2. Feature sizes are homogeneous (same scale family)
3. Edge selection is explicit (`edgeGroup` / GroupCreate), not blind whole-mesh angle bevel

Otherwise: **bevel per part, then merge**.

#### Per-part amount rule

Scale `amount` to **that part’s** max dimension, not the whole assembly AABB. A hose with radius 0.06 needs ~0.005–0.015; a 3.7-tall canister body needs ~0.05–0.15. Sharing one post-merge amount is always wrong for mixed-scale assemblies.

## Subgraph modularization (P0 — modules first, not count-first)

Large assemblies must not stay as one flat spaghetti graph. Use **Subgraph** (Houdini Subnet style; see `docs/Tutorials/11-subgraphs.md`) to hide complete modules behind one node on the root canvas.

**Prime rule**: encapsulate a **complete functional / model module** — never wrap nodes just to shrink a counter. Node-count thresholds only decide *when to ask* “which modules should become Subgraphs?”, not *how to slice*.

### When to consider Subgraphs (soft triggers)

| Soft signal | Meaning | Action |
|-------------|---------|--------|
| Root visible nodes ≈ **40+** | Canvas hard to read; titles/lanes collide | Plan modules; package the largest complete parts |
| Root visible nodes ≈ **80+** | Flat authoring failure (e.g. sedan-scale) | **Must** package; root should mostly be Subgraph instances + final `MergeMesh` + `Output` |
| Same part pattern × **≥ 2** instances | Wheel ×4, pier prototype, door L/R | One **definition**, multiple `Subgraph` instances (`data.subgraphId`) |
| One lane tip already ≥ **~8–12** executable nodes | Natural module boundary (matches subsystem lanes) | Prefer that lane → one Subgraph |
| User asks for Subgraph / reuse / cleaner root | Explicit intent | Follow module rules below |

Counts are **heuristics**, not hard law. A clean 35-node single-spine graph can stay flat; a messy 30-node multi-part vehicle may still need modules.

### What counts as a “complete module” (must pass)

A candidate Subgraph should answer **yes** to most of these:

1. **Nameable product part** — you can title it like a part, not an operator: `"FL Wheel"`, `"Body Shell"`, `"Hose Assembly"`, `"Deck + Bevel"`. If the best name is `"Transform Box"`, it is not a module.
2. **Coherent I/O** — usually **0–2 inputs** (external mesh/spline/params) and **1 primary mesh output** (extra outs only when the parent truly needs them).
3. **Finished enough to merge** — includes that part’s generate → (subdiv) → **bevel / place** chain so the root only **merges** modules (aligns with [Bevel placement](#bevel-placement-p0-for-assemblies)).
4. **Independent edit unit** — an artist can open the Subgraph and tweak that part without touching unrelated geometry.
5. **Reuse or size** — either referenced **≥ 2×**, or large enough alone that flattening hurts root readability.

### ❌ Do not encapsulate (为封而封)

| Anti-pattern | Why |
|--------------|-----|
| 2–4 node linear stubs (`Box → Transform`, `Sweep → Bevel` alone with no part context) | Noise; hides nothing meaningful |
| Unrelated parts in one bag (`wheel + door + glass`) | Breaks module identity; edit/reuse becomes worse |
| Mid-pipeline cuts (body sweep outside, body bevel inside) | Crosses bevel/assembly boundaries; hard to reason |
| One mega-Subgraph holding “everything except Output” | Fake modularity — root still opaque |
| Nesting > 2 levels without reuse need | Harder navigation; flatten unless definition is shared |
| Splitting solely to meet a numeric quota | Thresholds trigger planning, not random cuts |

### ✅ Good module boundaries (examples)

| Model | Prefer Subgraph definitions | Keep on root |
|-------|----------------------------|--------------|
| Vehicle | `wheel` (tire+rim+spokes+origin), `body` (profiles→loft/sweep→bevel→arch), `doors`, `glass`, lights | Instances + `MergeMesh` → `Output` |
| Bridge | `deck` (path+profile+sweep+bevel), `pier_proto` (if instanced) | Path may stay shared; `InstanceAlongSpline` + merge |
| Canister | `body` (revolve+bevel), `hose` (path+sweep), `wheels` / fittings | Final assembly merge |
| Scatter | Prototype mesh Subgraph if complex; scatter chain can stay flat if short | Spawner / Output |

Reuse pattern: define `wheel` once; place `fl_wheel` / `fr_wheel` / … as separate `Subgraph` instances with different root-side `TransformMesh` **or** bake placement inside per-instance wrappers only when transforms differ and cannot be shared cleanly.

### Decision checklist (before writing nodes)

```text
1. List modules from shape analysis / strategy rule (parts, not operators).
2. Soft-trigger? (root size / reuse / lane size) → if no, stay flat.
3. Each candidate: nameable? coherent I/O? finished to merge? → else keep flat or enlarge scope.
4. Prefer fewer, larger modules over many tiny Subgraphs.
5. Root graph goal: readable assembly sketch (lanes of Subgraph tips → Merge → Output).
```

Record the module plan in the reply (short table: module → approx node count → reused?) **before** writing JSON when soft triggers fire.

### Subgraph JSON contract

Definitions live in root `subgraphs[]`. Instances are ordinary nodes with `type: "Subgraph"` and `data.subgraphId`. Runtime flattens before cook (no separate executor). `SubgraphInput` / `SubgraphOutput` exist **only inside** definitions.

```json
{
  "version": "1.0",
  "nodes": [
    {
      "id": "fl_wheel",
      "type": "Subgraph",
      "position": { "x": -120, "y": 320 },
      "data": { "__nodeTitle": "FL Wheel", "subgraphId": "wheel" }
    }
  ],
  "edges": [
    { "id": "e_wheel_to_merge", "source": "fl_wheel", "target": "vehicle_merge",
      "sourceHandle": "mesh", "targetHandle": "in" }
  ],
  "subgraphs": [
    {
      "id": "wheel",
      "name": "Wheel",
      "inputs": [],
      "outputs": [{ "id": "mesh", "name": "Mesh", "pinType": "Mesh" }],
      "nodes": [
        { "id": "tire_sweep", "type": "SweepAlongSpline", "position": { "x": 200, "y": 0 },
          "data": { "__nodeTitle": "Tire Sweep" } },
        { "id": "wheel_out", "type": "SubgraphOutput", "position": { "x": 200, "y": 160 },
          "data": { "__nodeTitle": "Wheel Out" } }
      ],
      "edges": [
        { "id": "e_tire_out", "source": "tire_sweep", "target": "wheel_out",
          "sourceHandle": "out", "targetHandle": "mesh" }
      ]
    }
  ]
}
```

(Illustration only — real wheel modules include tire/rim/spokes + placement; see examples.md for a full I/O wrapper.)

| Rule | Detail |
|------|--------|
| `subgraphs[].id` | Stable snake_case definition id; instances set `data.subgraphId` to this |
| Ports | `inputs[]` / `outputs[]` declare `id`, `name`, `pinType` (`Mesh`, `Spline`, … — match real pins) |
| Interface wiring (P0) | `SubgraphInput` **edge `sourceHandle`** = `inputs[].id`; `SubgraphOutput` **edge `targetHandle`** = `outputs[].id` (runtime maps via handles — see `graph_parser.cpp`) |
| Instance handles | Root edges use those same port ids as `targetHandle` / `sourceHandle` on the `Subgraph` node |
| `__nodeTitle` | Required on instance **and** interior nodes (same display rules as root) |
| Layout | Interior graphs use the same top-down + `COL_STEP_X` rules; one definition = one module spine/lanes |
| Recursion | Forbidden (A→B→A rejected by parser) |
| Storage | Inline in `.pcg` only — no external subgraph asset file |

Minimal worked example (transform wrapper) is in [examples.md](examples.md#subgraph-module-move-mesh) and `docs/Tutorials/11-subgraphs.md`. Prefer **part-sized** modules over that minimal pattern in real models.

### Strategy rules consultation (mandatory)

**Path**: `VAULT_ROOT/Rules/pcg/`（`rule_search domain=pcg`）

1. `rule_search` — always hit `pcg/assembly-bevel`; then match model type (`pcg/vehicle`, `pcg/bridge`, …).
2. Read full rule via `vault_get_chunk` or `Rules/pcg/<name>.md` — use per-part mapping as **reference**, not a copy.
3. Always validate against the current reference image; adapt if the image differs.
4. If no matching type exists, derive from shape analysis; consider adding `Rules/pcg/<type>.md` + `rules-vault-index` + reindex.

**Do not** blindly copy an existing `examples/*.pcg` strategy — examples may contain anti-patterns (marked ⚠️ in the strategy rules).

## Physical-world sizing (P0 — default unless user overrides)

**Default rule**: Unless the user explicitly states a stylized / non-physical scale (e.g. "low-poly toy", "abstract", "gameplay-scale"), every object's dimensions MUST match real-world physical sizes. PCG-AI units are **meters**; model 1 unit = 1 meter.

Rationale: graphs are imported into Unity scenes that also contain real-world-scale assets (vehicles, buildings, props). A canister modeled at 0.3 units tall would be 30 cm in scene — invisible next to a 4 m car. The canonical `examples/biohazard-canister.pcg` already follows this: ~3.7 m tall canister, hose radius 0.06 m, wheel rim radius 0.4 m.

### Quick reference — real-world anchors (meters)

Use these as sanity anchors when no reference image gives explicit dimensions. Do NOT invent sizes far outside the realistic range for the object class.

| Object class | Typical real size | Notes |
|--------------|-------------------|-------|
| Beverage can | Ø0.065 × 0.12 | radius 0.0325 |
| Biohazard / chemical canister | Ø0.4–0.8 × 0.8–1.5 (small) to 3.7 (large drum) | canister.pcg = 3.7 tall drum |
| Door | width 0.8–1.0, height 2.0–2.1 | |
| Human (standing) | height 1.6–1.85 | good scale reference to include |
| Car (sedan) | length 4.0–5.0, width 1.8–2.0, height 1.4–1.5 | wheel Ø0.6–0.7 |
| Truck / pickup | length 5.0–6.5, height 1.8–2.2 | |
| Bicycle | length 1.7, wheel Ø0.66 | |
| Table | height 0.75, top 1.2 × 0.8 | |
| Chair | height 0.45 (seat), 0.9 (back) | |
| Bridge (single span) | span 10–40, deck width 3–8 | see bridge-demo |
| Smartphone | 0.15 × 0.07 × 0.008 | |
| Coin | Ø0.02–0.026, thickness 0.002 | |

### Sizing checks (mandatory before writing)

1. **Overall dimension**: after shape analysis, pick a real-world target size for the primary object. State it in the reply (e.g. "canister ≈ 3.7 m tall, real-world chemical drum").
2. **Per-part consistency**: every sub-part's size must be consistent with the whole in real world. A wheel on a 4.5 m car cannot be Ø0.1 (that's a toy). Scale sub-parts relative to the body, not in arbitrary units.
3. **No unit drift**: keep ALL geometry nodes in meters. Do not mix cm-scale control points (e.g. `0.06`) with m-scale translations (e.g. `400`) — that is the #1 cause of "model vanished / huge" bugs. If a spline profile uses small numbers, the body transform and other parts must use the same scale family.
4. **User override**: if the user wants stylized / non-physical scale, record it explicitly in the reply ("stylized: 0.3-unit toy scale") and skip the real-world anchor — but still keep all parts internally consistent.

### Anti-pattern

- ❌ Modeling a "realistic car" at 0.5 units long, or a "human-scale prop" at 50 units. Always ask: *would this object look right standing next to a 1.8 m person in the scene?*
- ❌ Mixing scale families within one graph (cm spline + m body). Pick one and convert.

## Parameter quality baselines

### BevelMesh

Bevel parameters must be proportional to model size. Too-small bevels are invisible.

| Model max dimension | Min `amount` | Recommended `amount` | Recommended `segments` |
|---------------------|-------------|---------------------|----------------------|
| < 1.0               | 0.03        | 0.05–0.08           | 2                    |
| 1.0–3.0             | 0.05        | 0.08–0.15           | 2–3                  |
| 3.0–5.0             | 0.08        | 0.12–0.20           | 3                    |
| > 5.0               | 0.10        | 0.15–0.30           | 3–4                  |

**Rule**: `amount < modelMaxDim × 0.02` is invisible — treat as invalid and increase.

### SubdivideMesh (before BevelMesh)

`BevelMesh` with `segments ≥ 3` needs sufficient topology. `SubdivideMesh` levels=1 (4 quads per face) is not enough.

| Purpose | Min `levels` | Note |
|---------|-------------|------|
| Pre-BevelMesh (segments ≤ 2) | 1 | Barely sufficient |
| Pre-BevelMesh (segments ≥ 3) | 2 | Recommended |
| Future SubdivisionSurface | ≥ 2 | Provides base topology |

### SweepAlongSpline

| Property | Guideline |
|----------|-----------|
| `sampleSpacing` | ≤ 0.5 for smooth curves; > 1.0 produces visible facets |
| `columns` (circle) | ≥ 16 for wheels; ≥ 8 is minimum for any circle |
| `subdivisions` (CreateSpline catmullRom) | ≥ 8 for smooth profile curves |

## Parameter exposure (ask user before writing)

Two different concepts — do not conflate them:

| Concept | Where | Purpose |
|---------|-------|---------|
| **Authoring values** | Node `data` fields | Bake defaults into the graph while authoring |
| **Graph Parameters** | Root `parameters[]` (Unity Blackboard / Component Inspector) | Runtime/Inspector overrides bound to `targetNode` + `targetProperty` |

After filling `data` from defaults (Step 6): (1) review authoring values; (2) when the graph needs interactive controls, **create Graph Parameters** (see below) and `ask_user` which ones to add if ambiguous.

### A. Authoring value review (Tier 1–3)

Present Tier 1 + relevant Tier 2 via `ask_user` so the user can adjust baked defaults before write.

#### Tier 1 — Always review

| Parameter | Source node(s) | What to show | Default logic |
|-----------|---------------|-------------|---------------|
| **Overall scale** | All geometry nodes | Model max dimension (X/Y/Z range) + unit hint + **real-world target size** | Derived from shape analysis, matched to [Physical-world sizing](#physical-world-sizing-p0--default-unless-user-overrides) anchors; flag if user wants stylized scale |
| **Bevel amount + segments** | `BevelMesh` | Current `amount` / `segments` values | Auto from Parameter Quality Baselines table |
| **Surface quality** | `SweepAlongSpline` `shadeMode` | auto / flat / smooth | Default `auto` (group + angle) |

#### Tier 2 — Conditionally review

| Node type | Parameter | What to show | Default |
|-----------|-----------|-------------|---------|
| `RevolveMesh` | `segments` | Radial resolution (e.g. 32 = smooth, 8 = faceted) | 32 |
| `RevolveMesh` | `capStart` / `capEnd` | Whether ends are capped | true / true |
| `SweepAlongSpline` | `sampleSpacing` | Curve sampling density | 0.5 (smooth) or 1.0 (coarse) |
| `SweepAlongSpline` | `columns` (circle) or `radius` | Cross-section resolution / size | 16 / from profile |
| `CreateCylinderMesh` | `radialSegments` | Circle smoothness | 16 |
| `CreateSpline` (catmullRom) | `subdivisions` | Curve smoothness | 8–12 |
| `InstanceAlongSpline` | `spacing` | Instance density | From model size |
| `SampleAlongSpline` | `spacing` | Floor / instance density along path | From model size |
| `AttributeRandomize` | `seed`, `translate*`, `scaleMin`/`scaleMax` | Layout jitter | From shape analysis |
| `Switch` | `index` | Style / variant selector (0–3) | 0 |
| `CopyMeshToPoints` | (via upstream points / prototype) | Prefer exposing upstream `spacing` / box size / `Switch.index` | — |
| `SubdivideMesh` | `levels` | Subdivision depth pre-bevel | 1–2 |

#### Tier 3 — Optional (expose on request only)

| Node type | Parameter | What to show |
|-----------|-----------|-------------|
| `VertexColor` | `r, g, b, a` | RGBA values (0–1) |
| `AssignMaterial` | `materialName` | Material name string |
| `UVTexture` | `projection, axis, scaleU, scaleV` | UV mapping mode |
| `ProjectTexture` | `direction, scaleU, scaleV` | Projection direction |
| `BevelMesh` | `profile, miterOuter, miterInner` | Bevel profile shape |
| `SweepAlongSpline` | `twist, profileRoll` | Twist / roll degrees |
| `CreateSpiralSpline` | `radius, pitch, turns` | Spiral parameters |
| `MeshNoiseDeform` | `intensity, scale` | Noise displacement |
| `CreateBoxMesh` | `width`, `height`, `depth` | Module / part size |

### B. Graph Parameters (`parameters[]`) — Inspector controls (P0 when needed)

Unity Blackboard / `PcgGraphComponent` reads root `parameters[]` and at cook time writes override values into `nodes[targetNode].data[targetProperty]` (`ApplyOverridesToDocument`). Reference: `examples/stone-arch-bridge.pcg`, `examples/bridge-demo.pcg`.

#### When to create Graph Parameters (auto-detect)

Create (or propose) Graph Parameters when **any** of these hold:

| Signal | Examples |
|--------|----------|
| User asks for slider / Inspector / HDA-like controls | “要可调楼层数”“暴露参数” |
| Generator / multi-style graph | floors density, `Switch.index`, random `seed`, module size |
| Same property will be tweaked often after cook | `BevelMesh.amount`, `SampleAlongSpline.spacing`, `AttributeRandomize.translateX` |
| Demo meant for artists without opening the node graph | Bridge bevel / railing spacing |

**Do not** create Graph Parameters for one-shot static props with no expected tuning (empty `parameters: []` is fine, e.g. some canister demos).

#### JSON contract (must match Unity serializer)

```json
"parameters": [
  {
    "id": "p1",
    "name": "FloorSpacing",
    "type": "number",
    "default": 1.55,
    "exposed": true,
    "targetNode": "floors",
    "targetProperty": "spacing",
    "hasRange": true,
    "min": 0.8,
    "max": 3.0
  }
]
```

| Field | Rule |
|-------|------|
| `id` | Unique (`p1`, `p2`, … or semantic `floor_spacing`) |
| `name` | Inspector display label (Title Case / camelCase ok) |
| `type` | `integer` \| `number` \| `boolean` \| `string` — must match the manifest property type |
| `default` | Native JSON value (`1.55`, `true`, `"wall"`) — **and** keep the same value baked in `nodes[].data` |
| `exposed` | `true` for Component Inspector; `false` to keep binding without UI |
| `targetNode` | Existing root `nodes[].id` (not Subgraph interior ids unless that node is on root after flatten — prefer root) |
| `targetProperty` | Exact key in that node's `data` / manifest property name |
| `hasRange` / `min` / `max` | For `number`/`integer` sliders; set `hasRange: true` when a sensible range exists |

**Limits (current runtime):**

- One Graph Parameter → **one** `targetNode` + `targetProperty`. To drive two nodes (e.g. L/R railing), emit **two** parameter entries (see stone-arch-bridge `railingSpacing` / `railingSpacingR`).
- Parameters apply to **root** document nodes; do not invent multi-target or expression bindings.
- Keep `default` synchronized with the target node's baked `data` value at write time.

#### Candidate discovery (before ask_user)

Scan the planned / existing graph and build a candidate table:

| Candidate name | type | targetNode | targetProperty | default | range | Why |
|----------------|------|------------|----------------|---------|-------|-----|
| FloorSpacing | number | floors | spacing | 1.55 | 0.8–3.0 | tower density |
| ModuleStyle | integer | module_switch | index | 0 | 0–1 | Switch styles |
| JitterSeed | integer | jitter | seed | 17 | — | reproducible layout |

Prefer **few high-leverage** params (typically 2–6). Skip internal wiring constants (tiny offsets, one-off glass transforms).

#### `ask_user` — which Graph Parameters to add

When candidates exist and the user has not already listed exact params:

```
type: "choice"
header: "Graph Parameters"
question: "检测到以下可暴露为 Inspector 参数（写入 parameters[]）。要添加哪些？\n\n{candidate table}"
options:
  - label: "推荐集（默认）"
    description: "添加表格中标记为推荐的 2–6 个参数"
  - label: "全部候选"
    description: "表格中每一项都写入 parameters[]"
  - label: "不添加 Graph Parameters"
    description: "parameters: []；仅保留节点 data 默认值"
  - label: "自定义"
    description: "告诉我要哪些 name / 或要绑定的节点属性"
```

If the user already said e.g. “要 floors / seed / switch”，skip this choice and implement that list.

If “自定义”: follow-up `ask_user` text; map answers to real `targetNode`/`targetProperty` from the graph — never invent property names not in the manifest.

### Combined `ask_user` pattern (authoring + Graph Parameters)

Use **one** `ask_user` when possible (max 4 questions). Prefer:

```
type: "choice"
header: "参数确认"
question: "以下关键节点默认值已填好；并检测到 Graph Parameter 候选。\n\n{authoring summary}\n\n{parameter candidates}"
options:
  - label: "确认：用推荐 Graph Parameters 生成"
    description: "写入推荐 parameters[] + 当前 data 默认值"
  - label: "确认：不添加 Graph Parameters"
    description: "parameters: []，仅用节点默认值生成"
  - label: "调整节点默认值"
    description: "先改 Tier 1/2 数值，再决定 Graph Parameters"
  - label: "自定义 Graph Parameters / 暴露更多"
    description: "自选候选，或展开 Tier 3"
```

**If "调整节点默认值"**: text follow-up → apply → then Graph Parameters choice if still needed.

**If "暴露更多" / Tier 3**: second choice listing Tier 3 + remaining Graph Parameter candidates.

### Parameter summary format

```
部件: 罐体 (RevolveMesh)
  segments: 32  |  capStart: true  |  capEnd: true

部件: 软管 (SweepAlongSpline)
  sampleSpacing: 0.2  |  radius: 0.08  |  columns: 8

全局:
  模型尺寸: ~1.5 × 3.7 × 1.5
  Bevel: amount=0.08, segments=2
  shadeMode: auto

Graph Parameters（拟写入）:
  p1 FloorSpacing → floors.spacing = 1.55 [0.8, 3]
  p2 ModuleStyle → module_switch.index = 0 [0, 1]
```

### Rules

1. **Never skip the `ask_user` step** for new graph creation when Tier 1 applies **or** Graph Parameter candidates exist. For **editing** existing graphs, skip if the user only requested a specific change (unless they asked to add Parameters).
2. **Max 4 questions per `ask_user` call**. Batch authoring + Graph Parameter decisions when possible.
3. **Only propose parameters that exist on real nodes** in the graph. If no `BevelMesh`, don't mention bevel.
4. **Apply user adjustments** to node `data` **and** `parameters[].default` before writing; keep them in sync.
5. Re-validate Parameter Quality Baselines after adjustments; run `validate_pcg.py` (parameters target checks).

## Graph JSON contract

```json
{
  "version": "1.0",
  "nodes": [ { "id", "type", "position": { "x", "y" }, "data": { ... } } ],
  "edges": [ { "id", "source", "target", "sourceHandle", "targetHandle" } ],
  "parameters": [ { "id", "name", "type", "default", "exposed", "targetNode", "targetProperty", "hasRange", "min", "max" } ],
  "subgraphs": [ { "id", "name", "inputs", "outputs", "nodes", "edges" } ]
}
```

| Rule | Detail |
|------|--------|
| `version` | Always `"1.0"` |
| `id` | Semantic snake_case role name (`body_profile_nose`, `fl_wheel_rim`); avoid bare `n1` and type-only ids (`create_box_1`) |
| `data.__nodeTitle` | **Required display name** in Unity GraphView (see [Node display names](#node-display-names-p0--unity-graphview)); not a manifest property |
| `data` | Manifest keys + optional `__nodeTitle`; use manifest defaults for omitted fields |
| `edges` | `sourceHandle` / `targetHandle` = manifest pin `id` (e.g. `backbone`, `profile`, `a`, `b`) |
| Pin compatibility | Output `pinType` must match input `pinType` (or `Output.in` = `Any`) |
| Terminator | Every runnable graph ends with `Output` at the **bottom** |
| `parameters` | Optional Graph Parameters (Blackboard); see [Graph Parameters](#b-graph-parameters-parameters--inspector-controls-p0-when-needed). Use `[]` when none |
| `subgraphs` | Optional; required when any `type: "Subgraph"` instance exists — see [Subgraph modularization](#subgraph-modularization-p0--modules-first-not-count-first) |

## Node display names (P0 — Unity GraphView)

Unity does **not** show `node.id` on the canvas. Each pill uses:

1. `data.__nodeTitle` if non-empty (serialized by `PcgManifestNodeView` / `CollectData`)
2. Else manifest `displayName` for that `type` (e.g. every `CreateBoxMesh` → **"Create Box Mesh"**)

So without `__nodeTitle`, ten boxes all read identically — the overlap/confusion in the screenshot.

### Naming rules

1. **Every node** in a new/edited graph gets a unique `__nodeTitle` derived from **what the part does in this model**, not the node type.
2. Prefer short Title Case English (≤ ~24 chars so the right-side label stays readable): `"Nose Profile"`, `"FL Wheel Rim"`, `"Deck Sweep"`, `"Merge Body"`.
3. `id` = snake_case of the same meaning (`nose_profile`, `fl_wheel_rim`). Keep `id` and `__nodeTitle` aligned.
4. When multiple nodes share a `type`, titles **must** differ by role / location / stage — never leave them on the default displayName.
5. Disambiguators (use in order): body part → side/position → stage (`bevel`, `xform`, `profile`, `path`).

| Context | ❌ Bad (all look the same) | ✅ Good |
|---------|---------------------------|---------|
| Sedan loft sections | `"Create Bezier Spline"` × N | `"Nose Profile"`, `"Cabin Profile"`, `"Tail Profile"` |
| Wheels | `"Create Box Mesh"` × 4 | `"FL Hub"`, `"FR Hub"`, `"RL Hub"`, `"RR Hub"` |
| Bridge | `"Sweep Along Spline"` | `"Deck Sweep"`, `"Rail Sweep"` |
| Math / utility | `"Math"` × N | `"Wheel Track X"`, `"Body Half Width"` |

### JSON shape

```json
{
  "id": "nose_profile",
  "type": "CreateBezierSpline",
  "position": { "x": -280, "y": 0 },
  "data": {
    "__nodeTitle": "Nose Profile",
    "mode": "catmullRom",
    "closed": false
  }
}
```

`__nodeTitle` is editor-only metadata; the cook path ignores unknown keys safely. Do **not** invent a top-level `"title"` field — Unity only reads `data.__nodeTitle`.

## Layout: Houdini top → bottom (required)

Unity GraphView uses **vertical pins** (inputs top, outputs bottom). Position nodes so wires run downward.

```
        [source A]     [source B]     ← same row, branch on X
              \           /
            [operator]
                  |
            [downstream]
                  |
              [Output]              ← bottom center
```

### Unity geometry (why spacing must be large)

Houdini-style pills are narrow; the **name is a right-side overlay**, not inside the pill:

| Fact | Value | Source |
|------|-------|--------|
| Node body width | ≈ `61` (`NodeWidth = 92 × 2/3`) | `PcgGraphNodeBase` |
| Title starts | `node.x + body + 14` | overlay placement |
| Title width | `min 120` … `max 220` | `m_RightTitleLabel` |

**Same-row footprint** ≈ body + gap + title ≈ **300–295+** px. If `|Δx|` between siblings is only 60–220, titles paint over the next pill — the exact failure in dense parallel rows.

### Constants

| Constant | Value | Use |
|----------|-------|-----|
| `ROW_STEP_Y` | `160` | Vertical gap between rows |
| `SPINE_X` | `200` | Main chain X |
| `COL_STEP_X` | `320` | **Minimum** `|Δx|` between any two nodes on the same row (P0) |
| `BRANCH_X` | `±320` | Two parallel inputs: `SPINE_X ± COL_STEP_X` (was ±220 — too tight for titles) |
| Start Y | `0` | Top row |

### Placement algorithm (P0 — minimize wire crossings)

Spacing alone is not enough. **Do not** pack every topo-level into one global grid row — that mixes unrelated subsystems and creates spaghetti (door box on the right wired diagonally to door place on the left).

#### Preferred: subsystem lanes

For multi-part assemblies (vehicle, bridge+details, canister+hose+…):

1. Find the final `MergeMesh` / `Output` fan-in. Each **direct input** is a **lane tip** (body, wheels, doors, glass, …).
2. Assign every ancestor of a tip to that tip’s lane (exclusive; shared nodes stay on the primary consumer’s lane).
3. Lay lanes **left → right** in reading order; within a lane, data flows **top → bottom**.
4. Single-chain nodes in a lane share one `lane_x` (wires stay nearly vertical).
5. True siblings that feed one consumer (e.g. 7 loft profiles → `body_loft`) sit on one row **inside that lane only**, centered on `lane_x`, `|Δx| ≥ COL_STEP_X`.
6. Parallel sub-chains inside a lane (tire / rim / spokes → `wheel_origin`) get **sub-lanes** `lane_x ± k*COL_STEP_X`, then merge on the lane spine.
7. Final `MergeMesh` + `Output` sit on the bottom row at the **center of all lane tips**.
8. After placing, prefer parent and child with similar `x` (barycenter of parents for multi-parent nodes). Long diagonal edges across many lanes = failed layout — re-lane.
9. When [Subgraph modularization](#subgraph-modularization-p0--modules-first-not-count-first) applies, each **lane tip** that is a complete module becomes one `Subgraph` instance on the root; interior nodes move into `subgraphs[]` with their own top-down layout.

```text
  [body profiles…]     [tire][rim][spoke]    [door boxes]
         |                  \  |  /                |
     Body Loft            Wheel Origin         Doors Left
         |                     |                   |
        …                     …                   …
         \                     |                  /
                    Vehicle Assembly → Output
```

#### Simple graphs (one spine)

1. Topological sort (sources first).
2. Row `r`: `y = r * ROW_STEP_Y`; single chain at `x = SPINE_X`.
3. N siblings → one consumer: siblings on row `r` centered on spine with `COL_STEP_X`; consumer on `r+1` at spine.
4. Overflow: if N > 8 **siblings of the same consumer**, split sibling rows — never shrink `COL_STEP_X`, and never mix unrelated nodes into that row.

#### Crossing check (must-fix on new / relayout graphs)

Approximate geometric crossings for straight parent→child segments. Target: **near-zero long diagonals**. Treat “many edges spanning ≥ 2 lane widths” as a layout failure even if titles do not overlap.

### Do NOT (legacy anti-patterns)

```text
❌  [A] ——→ [B] ——→ [C] ——→ [Out]     (same y, left-to-right pipeline)
❌  same row, x += 60 / 100 / 220       (titles overlap neighbour pills)
❌  global topo-row grid of all nodes   (unrelated parts share a row → wire spaghetti)
❌  omit __nodeTitle when type repeats  (all labels identical)
✅  one vertical lane per assembly part; siblings only beside their consumer
✅  same row only for true siblings, |Δx| ≥ COL_STEP_X, unique __nodeTitle
```

When **editing** old examples that use horizontal layout, dense X packing, or spaghetti lanes, **rewrite positions** to lane top-down + `COL_STEP_X`; keep topology and geometry `data` unchanged, and add `__nodeTitle` for every node.

## Common pin wiring

| Pattern | sourceHandle → targetHandle |
|---------|----------------------------|
| Mesh chain | `out` → `in` |
| Point chain | `out` → `in` |
| SweepAlongSpline | spline `out` → `backbone`; profile `out` → `profile` |
| InstanceAlongSpline | spline `out` → `spline`; mesh `out` → `mesh` |
| RevolveMesh | profile `out` → `profile` |
| MergeMesh | `out` → `in` (variadic — all inputs use `in`) |
| Output | `out` → `in` |
| Assembly Bevel | part → `BevelMesh` → `MergeMesh` (not Merge → Bevel) |
| Subgraph instance | upstream `out` → instance **input port id**; instance **output port id** → downstream `in` |
| Subgraph interior | `SubgraphInput` port id → first op; last op `out` → `SubgraphOutput` port id |

## Workflow

```
- [ ] 0. Shape analysis (mandatory with reference image — record in reply)
- [ ] 0b. Physical-world sizing (mandatory: pick real-world target size + per-part consistency; flag if user wants stylized/non-physical scale — see [Physical-world sizing](#physical-world-sizing-p0--default-unless-user-overrides))
- [ ] 1. Strategy rules check (mandatory: `rule_search domain=pcg` → `pcg/assembly-bevel` + matching type under `Rules/pcg/`)
- [ ] 2. Node selection (follow Node Selection Guide + strategy file; justify any deviation)
- [ ] 2b. Bevel placement check — no MergeMesh→BevelMesh on multi-part assemblies; bevel per part then merge
- [ ] 2c. **Subgraph module plan** — if soft triggers fire (root ≈40+/80+, reuse ≥2, fat lanes): list complete modules first; do **not** wrap stubs 为封而封 (see [Subgraph modularization](#subgraph-modularization-p0--modules-first-not-count-first))
- [ ] 3. Parameter quality check (follow Parameter Quality Baselines; per-part bevel amounts)
- [ ] 4. List nodes + edges from manifest (root instances + each `subgraphs[]` definition)
- [ ] 5. Assign semantic ids, unique `__nodeTitle`, and **lane-based** top-down positions (`COL_STEP_X ≥ 320`; keep part chains vertical; Subgraph interiors use the same rules)
- [ ] 5b. Overlap + crossing check — no same-row `|Δx| < 320`; no duplicate `__nodeTitle`; no global topo-grid spaghetti across subsystems
- [ ] 6. Fill `data` from manifest defaults + user params + `__nodeTitle` (+ `subgraphId` on instances)
- [ ] 6b. **Graph Parameter candidates** — scan nodes for Inspector-worthy properties; build candidate table (see [Graph Parameters](#b-graph-parameters-parameters--inspector-controls-p0-when-needed))
- [ ] 7. Parameter review with user (ask_user — Tier 1/2 authoring values **and** which Graph Parameters to write into `parameters[]`)
- [ ] 7b. Save directory confirmation (ask_user — glob for .pcg / PCGDemo / Assets dirs, default to detected)
- [ ] 8. Write file to confirmed directory (include `parameters` array; keep defaults synced with target `data`)
- [ ] 9. Run validation script (treat Merge→Bevel assembly warnings, “tiny Subgraph / flat mega-graph”, and **broken parameter bindings** as must-fix on new graphs)
- [ ] 10. If graph is a regression fixture, wire into pcg-core ctest
```

### File placement (mandatory `ask_user`)

**Never assume the save directory.** Before writing, auto-detect candidate directories and confirm with the user.

#### Auto-detection order

1. **Glob for existing `.pcg` files** — `glob(pattern="**/*.pcg")` in the workspace. The directory containing the most `.pcg` files is the primary candidate.
2. **Glob for Unity PCG directories** — `glob(pattern="**/Assets/PCG*/**")` or `glob(pattern="**/Assets/*PCG*/**")`. Matches `PCGDemo`, `PcgPlugin`, etc.
3. **Glob for Unity `Assets/` folders** — `glob(pattern="**/Assets")` to locate Unity project roots.
4. If step 1–2 finds results → propose the deepest matched directory as default.
5. If nothing found → fall back to `examples/` relative to the PCG repo root, or the workspace root.

#### `ask_user` pattern

Use a **single `ask_user` call** with the detected directory as the default option:

```
type: "choice"
header: "保存目录"
question: "检测到以下目录，选择 .pcg 文件保存位置：\n\n{detected path summary}"
options:
  - label: "{detected default path}"
    description: "自动检测到的 PCG 目录"
  - label: "examples/"
    description: "PCG 仓库 examples/ 目录（仅仓库 demo 用）"
  - label: "自定义路径"
    description: "手动输入保存目录"
```

**If "自定义路径"**: follow up with `type: "text"` to get the path from the user.

#### Save targets

| Purpose | Path (after confirmation) |
|---------|---------------------------|
| Canonical demo | `examples/<name>.pcg` |
| Unity import | `{confirmed_dir}/<name>.pcg` (default: `Unity/Assets/PCGDemo/`) |
| Schema sample | `schema/example.pcg` (minimal only) |

Keep `examples/` and Unity copies **identical** for paired demos.

### Special fields

- **CreateSpline `controlPoints`**: JSON **string** with escaped quotes:
  `"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":10,\"y\":0,\"z\":0}]"`
- **GetMeshData / GetSplineData**: use `bindingKey` + `source: "Binding"`; never serialize scene object refs in `.pcg`
- **Numbers in manifest** (not `.pcg`): avoid `1e-8` in `node-manifest.json` — breaks Unity `PcgMiniJson`; use `0.00000001`

## Validation (required)

From repo root:

```bash
python3 ~/.codely-cli/skills/pcg-graph-authoring/scripts/validate_pcg.py examples/your-graph.pcg
```

Fix all errors; treat layout warnings, same-row spacing warnings, and duplicate/missing `__nodeTitle` warnings on new graphs as must-fix.

## Quick templates

See [examples.md](examples.md) for full top-down graphs: linear mesh chain, scatter, spline bridge.

## Reference files

| File | Role |
|------|------|
| `schema/node-manifest.json` | Node + pin definitions |
| `schema/graph-schema.json` | v1 envelope (legacy enum; manifest is authoritative for types) |
| `docs/Tutorials/11-subgraphs.md` | Subgraph JSON + Unity editor packaging |
| `pcg-core/tests/test_subgraph.cpp` | Flatten / I/O / recursion guard |
| `pcg-core/tests/test_phase45_spline.cpp` | Spline graph execution tests |
| `Unity/Assets/PcgPlugin/Editor/Graph/PcgConnectionValidator.cs` | Pin type rules in editor |
