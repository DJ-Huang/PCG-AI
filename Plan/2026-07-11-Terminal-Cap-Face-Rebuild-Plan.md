# Terminal Cap Face Rebuild Plan

**Date**: 2026-07-11  
**Target**: `test_bridge_bevel` sedan body fixture — **boundary=0** (V127)  
**File**: `pcg-core/src/elements/bevel_blender.cpp`

## Problem

`sedan body` inline graph (spline 0→4.2, bevel on `profile_corner`, exclude `cap_start,cap_end`) produces **16 boundary edges** = 8 cap triangular holes at x=0 / x=4.2.

| Fixture | Status |
|---------|--------|
| bridge-demo | PASS |
| lowpoly-sedan full graph | PASS |
| sedan body | **FAIL opposite-winding=176**（boundary=0 ✅） |

## Root Cause (confirmed)

1. Terminal `offset_meet` collapses adjacent bound verts → `lv==rv` on ebev edge.
2. Holes first appear at **`after_face_rebuild`** (not dedup): 8× cap loops of 3 verts.
3. Cap face rebuild walks non-ebev ring `vstart_idx=1 → vend_idx=2`, skipping ebev ring profile arc.
4. `bevel_build_trifan` (PCG) used center fan only — unlike Blender which calls `bevel_build_poly` first (profile BMVerts shared with rebuild).

## Phase Status

### Phase 2.1 — Staging diagnostics ✅ DONE

- [x] `PCG_BEVEL_DIAG=1` stages: `after_edge_strips` / `after_face_rebuild` / `after_dedup`
- [x] `diag_terminal_verts_after_vmesh` (lv/rv, profile collapse, prof_start/end)
- [x] `cap_corner` / `cap_face` logging in `rebuild_faces_bmesh`

### Phase 2.2 — Boundary alignment 🔄 IN PROGRESS

- [x] Extract `build_boundary_terminal_edge` (Blender L3291)
- [x] Fix `is_outside_edge` + `offset_meet` clamp (EdgeHalf endpoints, Blender L1658/L2030)
- [x] Fix `set_profile_params` `plane_co` overwrite bug
- [x] Terminal cap **profile-only synthesis** via `slide_dist` on `e->prev` / `e->next` when `start≈end`
- [x] Prototype `fix_terminal_cap_collapse` (slide `e->next` + rewire `leftv→e_prev` slide) — **works for lv/rv separation** (`rv=prof_end`) but **not landed** (boundary 60 alone; 80–92 with rebuild; needs 3-layer atomic fix)
- [x] Symmetric terminal boundary (`slide_dist` on `e_prev`/`e_next`, 2-ring) — **boundary 16→0**
- [ ] Separate `lv/rv` without strip/rebuild mismatch — **boundary OK; winding 176 待修**
- [ ] Move `set_profile_params` + `move_profile_plane` into `build_boundary_terminal_edge` (Blender L3394) — deferred (namespace split; currently in `build_vmesh`)

### Phase 2.3 — VMesh / TriFan 🔄 IN PROGRESS

- [x] `NewVert.valid` (Blender `if (bmv)` equivalent) — boundary 24→16
- [x] `bevel_build_poly` inserts ebev profile k=1..ns-1 (Blender L6303)
- [x] `bevel_build_trifan`: terminal `selcount==1` keeps legacy center fan (profile poly caused strip winding clash → 84 bad winding)
- [ ] Full Blender trifan path: poly + fan-split when boundary + rebuild coordinated

### Phase 2.4 — Face rebuild ⏳ PENDING

- [x] Face rebuild short-path profile inject（CCW k=ns..0；修正 vend==ebev_bnd 检测）
- [ ] Strip↔cap 共享边 winding 一致（当前 opposite-winding 176）

### Phase 2.5 — Verification ⏳ PENDING

- [ ] `test_bridge_bevel` sedan body **boundary=0 且 winding bad=0**（boundary ✅ / winding ❌ 176）
- [ ] `scripts/build-pcg-core.sh --copy-to-unity` (V129)
- [ ] Update exec log

## Evidence (sedan body, 2026-07-11)

```
terminal_vm v=2: lv==rv (collapsed) OR after synthesis prof_start!=prof_end
after_edge_strips: boundary=96
after_face_rebuild: boundary=16, cap_loops3=8
cap_corner: vstart_idx=1 vend_idx=2 (skips ebev ring 0)
```

## Next Steps (priority)

1. **Atomic fix** (single changeset, all three layers):
   - `fix_terminal_cap_collapse` after final boundary pass (ring `ebev_bnd->next` collapse detect)
   - VMesh ebev ring k=0/k=ns ← `profile.start/end` when `special_params`
   - Face rebuild ebev detour when `vstart->next==vend`
2. **Validate** strip connects to rewired `leftv/rightv` (no orphan slide bndv in ring walk)
3. **TriFan**: profile-aware poly only after boundary=0 on sedan fixture

## Session 2026-07-11 PM — fix_terminal_cap_collapse learnings

- `clampOverlap=true` → boundary built twice (`construct` true/false); collapse detect on `leftv==rightv` fails on 2nd pass (slide vs corner)
- Ring detect `ebev_bnd->nv.co == meet_next->nv.co` is reliable
- Always-on rewire separates `lv/rv` (`rv=slide e_next`) but boundary **60** without rebuild; **80–92** with rebuild
- ebev detour alone: **24** (8→14 tri holes)
- **Current shipped state**: boundary=0, opposite-winding=176（对称 slide boundary + short-path rebuild）

## Reverted / Do Not Re-apply Alone

| Attempt | Result |
|---------|--------|
| `offset_in_plane` on boundary | 48+ boundary |
| face rebuild profile inject | 24–68 boundary |
| boundary `adjust_bound_vert` split | 32 (quad holes) |
| boundary pointer rewire only | 40 |
| `fix_terminal_cap_collapse` (rewire only) | 60 |
| fix + face rebuild + VMesh endpoints | 80–92 |
| ebev detour alone | 24 |
| terminal profile poly in trifan output | 84 opposite winding |
