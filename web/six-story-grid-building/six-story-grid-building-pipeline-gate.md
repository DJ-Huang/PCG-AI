# PCG pipeline gate — six-story-grid-building

```text
PCG Pipeline: REWORK
layout=PASS |
structure=PASS | parameters=PASS | seed=PASS |
boundaries=BASELINE | regeneration=PASS |
performance=BASELINE | outputContract=FAIL
demands=8 covered=5 gap=0 inefficient=3
evidence: graph=web/six-story-grid-building/six-story-grid-building.pcg | reviewPage=http://127.0.0.1:5173/review?graph=web/six-story-grid-building/six-story-grid-building.pcg | reports=web/six-story-grid-building/six-story-grid-building-pipeline-gate.md
```

## Demand inventory

| Demand | Class | Evidence |
|---|---|---|
| 24×21×16 m rectangular mass | covered | `gnd_box` 24×5×16 + `upr_box` 24×16×16; cook OK |
| 5 m podium + 4 m typical floors | covered | podium height 5; window `translateY=4`, count=4 |
| 4-bay front grid + 3-pane windows | covered | `front_win_copy_x` count=4; mullions in `upper_window` |
| Ground storefront glazing | approximate-inefficient | Boolean openings + glass overlay; clay ortho still reads as solid panels |
| Side window column | covered | `side_window` + CopyMesh on ±X |
| Roof bulkhead at rear | covered | penthouse at z=-4.2 |
| Named PBR slots bound | covered | cook `material_slots`: podium/upper/roof/frame/glass |
| Interior stair/elevator rooms | approximate-inefficient | penthouse volume only; top drawing core not modeled as interior |

## Tests

- Structure: `validate_pcg.py --check-server` OK; 8 subgraphs; top-down layout PASS (161 nodes, position-only).
- Parameters: `BayCount` → `front_win_copy_x.count`; `TypicalFloorCount` → `front_win_copy_y.count`; `BaySpacing` → `translateX`. Defaults match baked data.
- Seed: two `/v1/cook` seed=42 both `code=0`.
- Boundaries: not fully swept in live editor this session (BASELINE).
- Output contract: front 4-bay window grid is recognizable; ground storefronts and top-plan core/entrance recess fail the drawing.

## Rework

Earliest repair: **storefront openings** (boolean pocket depth + glass inset so clay ortho shows cavities) then **top footprint** (front recess, rear core inset). Do not start textures until those silhouettes pass.
