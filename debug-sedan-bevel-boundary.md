# Debug Session: sedan-bevel-boundary

Status: OPEN (96→24, remaining 8 triangular holes at cap end faces)

## Symptom

`SweepAlongSpline -> BevelMesh` on the sedan body produces 96 boundary edges. Bridge and full graph execution remain operational.

## Expected

The isolated sedan body bevel output is a closed manifold with zero boundary edges.

## Hypotheses

1. Terminal VMesh profile rows collapse before edge-strip construction. ✅ CONFIRMED
2. Edge-strip construction selects the wrong opposite-end BoundVert or VMesh row. ❌ EXCLUDED
3. Collapse is limited to cap-adjacent terminal vertices affected by edge exclusion. ❌ EXCLUDED
4. Face reconstruction and edge-strip construction consume different profile boundaries. ✅ CONFIRMED
5. Geometric deduplication removes required triangles after correct topology generation. ❌ EXCLUDED

## Confirmed Root Causes

### RC1: `next_bev()` premature ring termination
- `next_bev()` had extra condition `e != &bv->edges[0]` causing early exit when crossing array start
- `selcount=2` vertices only generated `VMesh count=1` (Blender expects 2/3)
- Fix: Remove `&& e != &bv->edges[0]`, keep only `e != start`
- Impact: boundary 96→152 (exposed second gap)

### RC2: Missing Blender weld profile for `selcount=2 && n==2`
- Two collinear bevel edges meeting at a vertex (straight sweep middle sections)
- Blender has `n==2` reverse-copy + dual bevel weld profile merge — was missing
- Fix: Added weld detection + reverse-copy + profile merging in `build_vmesh()`
- Impact: boundary 152→56

### RC3: Degenerate strip quad dropped instead of triangulated
- Terminal strip quads with `a==d || b==c` were silently dropped
- Fix: Generate degenerate triangle from non-collapse side
- Impact: boundary 56→24

### RC4: `build_edge_polygons()` used `get_profile_point()` instead of `vm->at()`
- `rebuild_faces_bmesh()` already used `vm->at()` for real boundary points
- Mismatch caused "nearly coincident but not welded" seams
- Fix: Changed `build_edge_polygons()` to use `vm->at()` consistently

## Excluded Hypotheses

- **Irregular last segment length**: spine末段 4.2 vs 4.0 对照，boundary 不变
- **Vertex near-coincidence**: 近邻边诊断排除容差焊接问题
- **Terminal-edge `offset_meet()` face selection**: 修正后无改善且扰动 bridge

## Reverted Attempts

- Terminal-edge offset_meet face selection change
- Simplified weld + terminal special case additions
- Generic triangle hole filler (caused winding regression)

## Current State

- `bridge-demo`: PASS
- `lowpoly-sedan` full graph: PASS
- sedan body isolated: 96→24, still FAIL
- Remaining 24 edges = 8 triangular holes at cap end faces (`x=0` and `x=4.2`)
- Debug diagnostics (`report_bevel_debug` curl calls) still in `build_edge_polygons()` — need cleanup

## Next Steps

1. Precisely align Blender `build_boundary_terminal_edge()` profile-plane alignment
2. Handle remaining 8 triangular holes at cap end faces
3. Clean up debug diagnostics from `build_edge_polygons()`
