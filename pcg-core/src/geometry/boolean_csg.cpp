// Blender-aligned boolean CSG implementation.

#include "geometry/boolean_csg.hpp"
#include "geometry/bvh.hpp"
#include "geometry/robust_predicates.hpp"
#include "geometry/group_table.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <stack>

namespace pcg::internal::geometry {

namespace {

int64_t face_origin_key(int source, int original_face)
{
    return (static_cast<int64_t>(source) << 32) |
           static_cast<uint32_t>(original_face);
}

Vec3 tri_normal(const IMesh& mesh, int t)
{
    const IMeshTri& tri = mesh.tris[t];
    const Vec3& a = mesh.verts[tri.v0].co;
    const Vec3& b = mesh.verts[tri.v1].co;
    const Vec3& c = mesh.verts[tri.v2].co;
    Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
    Vec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
    return {e1.y * e2.z - e1.z * e2.y,
            e1.z * e2.x - e1.x * e2.z,
            e1.x * e2.y - e1.y * e2.x};
}

/// Flap vertex of tri relative to directed edge (a->b). Returns -1 if edge not in tri.
int flap_vertex(const IMeshTri& tri, int a, int b, bool& reversed)
{
    const int v[3] = {tri.v0, tri.v1, tri.v2};
    for (int i = 0; i < 3; ++i) {
        if (v[i] == a && v[(i + 1) % 3] == b) {
            reversed = false;
            return v[(i + 2) % 3];
        }
        if (v[i] == b && v[(i + 1) % 3] == a) {
            reversed = true;
            return v[(i + 2) % 3];
        }
    }
    reversed = false;
    return -1;
}

/// Classify tri relative to tri0 around shared edge e=(ea,eb).
int sort_tris_class(const IMesh& mesh, int tri, int tri0, int ea, int eb)
{
    const IMeshTri& t0 = mesh.tris[tri0];
    const IMeshTri& t = mesh.tris[tri];
    bool rev0 = false, rev = false;
    int flap0 = flap_vertex(t0, ea, eb, rev0);
    int flap = flap_vertex(t, ea, eb, rev);
    if (flap0 < 0 || flap < 0) return 3;

    const Vec3& a0 = mesh.verts[t0.v0].co;
    const Vec3& a1 = mesh.verts[t0.v1].co;
    const Vec3& a2 = mesh.verts[t0.v2].co;
    const Vec3& fp = mesh.verts[flap].co;

    OrientSign orient = orient3d(a0, a1, a2, fp);
    // Blender's mpq orient3d reports positive below the oriented plane.
    // PCG's predicate contract is the opposite (Positive = above), so the
    // class-3/class-4 mapping must be inverted when porting the sorter.
    if (orient == OrientSign::Positive) return rev0 ? 3 : 4;
    if (orient == OrientSign::Negative) return rev0 ? 4 : 3;
    return (flap == flap0) ? 1 : 2;
}

void sort_by_signed_triangle_index(const IMesh& mesh, int ea, int eb,
                                   std::vector<int>& tris)
{
    std::sort(tris.begin(), tris.end(), [&](int lhs, int rhs) {
        bool lhs_reversed = false;
        bool rhs_reversed = false;
        flap_vertex(mesh.tris[lhs], ea, eb, lhs_reversed);
        flap_vertex(mesh.tris[rhs], ea, eb, rhs_reversed);
        const int signed_lhs = lhs_reversed ? -lhs : lhs;
        const int signed_rhs = rhs_reversed ? -rhs : rhs;
        return signed_lhs < signed_rhs;
    });
}

std::vector<int> sort_tris_around_edge_recursive(const IMesh& mesh, int ea, int eb,
                                                 const std::vector<int>& tris, int top_tri)
{
    if (tris.size() <= 1) return tris;

    const int reference = tris[0];
    std::vector<int> g1{tris[0]}, g2, g3, g4;
    std::vector<std::vector<int>*> groups = {&g1, &g2, &g3, &g4};

    for (size_t i = 1; i < tris.size(); ++i) {
        int gn = sort_tris_class(mesh, tris[i], reference, ea, eb) - 1;
        if (gn >= 0 && gn < 4) groups[gn]->push_back(tris[i]);
    }

    if (g1.size() > 1) sort_by_signed_triangle_index(mesh, ea, eb, g1);
    if (g2.size() > 1) sort_by_signed_triangle_index(mesh, ea, eb, g2);
    if (g3.size() > 1) g3 = sort_tris_around_edge_recursive(mesh, ea, eb, g3, top_tri);
    if (g4.size() > 1) g4 = sort_tris_around_edge_recursive(mesh, ea, eb, g4, top_tri);

    std::vector<int> out;
    out.reserve(tris.size());
    auto append = [&](const std::vector<int>& group) {
        out.insert(out.end(), group.begin(), group.end());
    };
    if (reference == top_tri) {
        append(g1);
        append(g4);
        append(g2);
        append(g3);
    } else {
        append(g3);
        append(g1);
        append(g4);
        append(g2);
    }
    return out;
}

std::vector<int> sort_tris_around_edge(const IMesh& mesh, int ea, int eb,
                                       const std::vector<int>& tris)
{
    if (tris.empty()) return {};
    return sort_tris_around_edge_recursive(mesh, ea, eb, tris, tris[0]);
}

void merge_cells(int merge_to, int merge_from, CellsInfo& cinfo, PatchesInfo& pinfo)
{
    if (merge_to == merge_from) return;
    Cell& from = cinfo.cell(merge_from);
    Cell& to = cinfo.cell(merge_to);
    int final_to = merge_to;
    while (to.merged_to != kNoIndex) {
        final_to = to.merged_to;
        to = cinfo.cell(final_to);
    }
    for (int p : from.patches) {
        to.add_patch(p);
        Patch& patch = pinfo.patch(p);
        if (patch.cell_above == merge_from) patch.cell_above = final_to;
        if (patch.cell_below == merge_from) patch.cell_below = final_to;
    }
    from.merged_to = final_to;
}

void find_cells_from_edge(const IMesh& mesh, const MeshTriTopology& topo,
                          PatchesInfo& pinfo, CellsInfo& cinfo,
                          int ea, int eb)
{
    const int64_t ek = edge_key(ea, eb);
    const auto* edge_tris = topo.edge_tris(ek);
    if (!edge_tris || edge_tris->empty()) return;

    std::vector<int> sorted = sort_tris_around_edge(mesh, ea, eb, *edge_tris);
    const int n = static_cast<int>(sorted.size());
    std::vector<int> edge_patches(n);
    for (int i = 0; i < n; ++i) {
        edge_patches[i] = pinfo.patch_of_tri(sorted[i]);
    }

    for (int i = 0; i < n; ++i) {
        const int inext = (i + 1) % n;
        const int rp = edge_patches[i];
        const int rnextp = edge_patches[inext];
        Patch& r = pinfo.patch(rp);
        Patch& rnext = pinfo.patch(rnextp);

        bool rflip = false, rnextflip = false;
        flap_vertex(mesh.tris[sorted[i]], ea, eb, rflip);
        flap_vertex(mesh.tris[sorted[inext]], ea, eb, rnextflip);

        int* r_follow = rflip ? &r.cell_below : &r.cell_above;
        int* rnext_prev = rnextflip ? &rnext.cell_above : &rnext.cell_below;

        if (*r_follow == kNoIndex && *rnext_prev == kNoIndex) {
            int c = cinfo.add_cell();
            *r_follow = c;
            *rnext_prev = c;
            cinfo.cell(c).add_patch(rp);
            cinfo.cell(c).add_patch(rnextp);
        } else if (*r_follow != kNoIndex && *rnext_prev == kNoIndex) {
            int c = *r_follow;
            *rnext_prev = c;
            cinfo.cell(c).add_patch(rnextp);
        } else if (*r_follow == kNoIndex && *rnext_prev != kNoIndex) {
            int c = *rnext_prev;
            *r_follow = c;
            cinfo.cell(c).add_patch(rp);
        } else if (*r_follow != *rnext_prev) {
            const int c_follow = *r_follow;
            const int c_prev = *rnext_prev;
            if (static_cast<int>(cinfo.cell(c_follow).patches.size()) >=
                static_cast<int>(cinfo.cell(c_prev).patches.size())) {
                merge_cells(c_follow, c_prev, cinfo, pinfo);
            } else {
                merge_cells(c_prev, c_follow, cinfo, pinfo);
            }
        }
    }
}

bool apply_bool_op(BooleanOp op, const std::vector<int>& winding)
{
    if (winding.empty()) return false;
    switch (op) {
        case BooleanOp::Intersect:
            for (int w : winding) if (w == 0) return false;
            return true;
        case BooleanOp::Union:
            for (int w : winding) if (w != 0) return true;
            return false;
        case BooleanOp::Subtract:
            if (winding[0] == 0) return false;
            for (size_t i = 1; i < winding.size(); ++i) {
                if (winding[i] >= 1) return false;
            }
            return true;
        case BooleanOp::Shatter:
            return true;
    }
    return false;
}

bool patches_geometrically_equal(const IMesh& mesh, int p1, int p2, const PatchesInfo& pinfo)
{
    if (pinfo.patch(p1).tris.size() != pinfo.patch(p2).tris.size()) return false;
    if (pinfo.patch(p1).tris.size() != 1) return false;
    const IMeshTri& t1 = mesh.tris[pinfo.patch(p1).tris[0]];
    const IMeshTri& t2 = mesh.tris[pinfo.patch(p2).tris[0]];
    const int v1[3] = {t1.v0, t1.v1, t1.v2};
    const int v2[3] = {t2.v0, t2.v1, t2.v2};
    auto same = [&](int a, int b, int c, int x, int y, int z) {
        return a == x && b == y && c == z;
    };
    return same(v1[0], v1[1], v1[2], v2[0], v2[1], v2[2]) ||
           same(v1[0], v1[1], v1[2], v2[0], v2[2], v2[1]);
}

void check_zero_volume(Cell& cell, const IMesh& mesh, const PatchesInfo& pinfo)
{
    if (cell.patches.size() != 2) return;
    int p1 = cell.patches[0];
    int p2 = cell.patches[1];
    if (patches_geometrically_equal(mesh, p1, p2, pinfo)) {
        cell.zero_volume = true;
    }
}

int resolve_cell(const CellsInfo& cinfo, int c)
{
    while (c >= 0 && c < cinfo.tot_cell()) {
        int m = cinfo.cell(c).merged_to;
        if (m == kNoIndex) return c;
        c = m;
    }
    return c;
}

struct PatchAABB {
    Vec3 min{1e30, 1e30, 1e30};
    Vec3 max{-1e30, -1e30, -1e30};
    void expand(const Vec3& p) {
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
    bool contains(const PatchAABB& o) const {
        return min.x <= o.min.x && min.y <= o.min.y && min.z <= o.min.z &&
               max.x >= o.max.x && max.y >= o.max.y && max.z >= o.max.z;
    }
    Vec3 center() const {
        return {(min.x + max.x) * 0.5, (min.y + max.y) * 0.5, (min.z + max.z) * 0.5};
    }
};

PatchAABB patch_aabb(const IMesh& mesh, const PatchesInfo& pinfo, int p)
{
    PatchAABB bb;
    for (int t : pinfo.patch(p).tris) {
        const IMeshTri& tri = mesh.tris[static_cast<size_t>(t)];
        bb.expand(mesh.verts[tri.v0].co);
        bb.expand(mesh.verts[tri.v1].co);
        bb.expand(mesh.verts[tri.v2].co);
    }
    return bb;
}

int cell_on_side_of_tri(const IMesh& mesh, const Patch& patch, int tri_idx, const Vec3& test_pt)
{
    const IMeshTri& tri = mesh.tris[static_cast<size_t>(tri_idx)];
    OrientSign o = orient3d(mesh.verts[tri.v0].co, mesh.verts[tri.v1].co,
                            mesh.verts[tri.v2].co, test_pt);
    if (o == OrientSign::Positive) return patch.cell_above;
    if (o == OrientSign::Negative) return patch.cell_below;
    return patch.cell_below;
}

int patch_inside_cell(int ambient_cell, const CellsInfo& cinfo, const Patch& patch)
{
    ambient_cell = resolve_cell(cinfo, ambient_cell);
    int ca = resolve_cell(cinfo, patch.cell_above);
    int cb = resolve_cell(cinfo, patch.cell_below);
    if (ca == ambient_cell) return cb;
    if (cb == ambient_cell) return ca;
    return ca;
}

void fix_degenerate_patch_cells(PatchesInfo& pinfo, CellsInfo& cinfo)
{
    for (int p = 0; p < pinfo.tot_patch(); ++p) {
        Patch& patch = pinfo.patch(p);
        if (patch.cell_above != patch.cell_below) continue;
        int c = cinfo.add_cell();
        patch.cell_below = c;
        cinfo.cell(c).add_patch(p);
    }
}

} // anonymous namespace

std::vector<std::vector<int>> find_patch_components(const CellsInfo& cinfo, const PatchesInfo& pinfo)
{
    std::vector<int> comp(pinfo.tot_patch(), kNoIndex);
    std::vector<std::vector<int>> components;

    for (int pstart = 0; pstart < pinfo.tot_patch(); ++pstart) {
        if (comp[static_cast<size_t>(pstart)] != kNoIndex) continue;
        const int cid = static_cast<int>(components.size());
        components.emplace_back();
        std::stack<int> st;
        st.push(pstart);
        comp[static_cast<size_t>(pstart)] = cid;
        components.back().push_back(pstart);

        while (!st.empty()) {
            int p = st.top();
            st.pop();
            const Patch& patch = pinfo.patch(p);
            for (int c : {patch.cell_above, patch.cell_below}) {
                int rc = resolve_cell(cinfo, c);
                if (rc < 0 || rc >= cinfo.tot_cell()) continue;
                const Cell& cell = cinfo.cell(rc);
                for (int p2 : cell.patches) {
                    if (comp[static_cast<size_t>(p2)] != kNoIndex) continue;
                    comp[static_cast<size_t>(p2)] = cid;
                    components.back().push_back(p2);
                    st.push(p2);
                }
            }
        }
    }
    return components;
}

int ambient_cell_for_component(const IMesh& mesh, const PatchesInfo& pinfo,
                               const std::vector<int>& patch_ids, const PatchAABB& bb,
                               bool nested)
{
    if (patch_ids.empty()) return kNoIndex;
    const int p0 = patch_ids[0];
    const Patch& patch = pinfo.patch(p0);
    int tri_idx = patch.rep_tri();
    if (tri_idx < 0) return kNoIndex;

    Vec3 test_pt;
    if (nested) {
        test_pt = bb.center();
    } else {
        test_pt = {bb.max.x + 10.0, bb.center().y, bb.center().z};
    }
    int side = cell_on_side_of_tri(mesh, patch, tri_idx, test_pt);
    if (nested) {
        return side == patch.cell_above ? patch.cell_below : patch.cell_above;
    }
    return side;
}

int find_ambient_cell_for_patches(const IMesh& mesh, const PatchesInfo& pinfo,
                                  const std::vector<int>& patch_ids, bool nested)
{
    (void)nested;
    if (patch_ids.empty()) return kNoIndex;

    int best_patch = kNoIndex;
    double best_x = -std::numeric_limits<double>::max();
    double best_normal_x = -std::numeric_limits<double>::max();
    for (int p : patch_ids) {
        const Patch& patch = pinfo.patch(p);
        for (int t : patch.tris) {
            const IMeshTri& tri = mesh.tris[static_cast<size_t>(t)];
            const double tri_max_x = std::max({
                mesh.verts[tri.v0].co.x,
                mesh.verts[tri.v1].co.x,
                mesh.verts[tri.v2].co.x});
            const Vec3 normal = tri_normal(mesh, t);
            if (tri_max_x > best_x ||
                (tri_max_x == best_x && normal.x > best_normal_x)) {
                best_x = tri_max_x;
                best_normal_x = normal.x;
                best_patch = p;
            }
        }
    }
    if (best_patch == kNoIndex) return kNoIndex;

    const Patch& hull_patch = pinfo.patch(best_patch);
    return best_normal_x >= 0.0 ? hull_patch.cell_above : hull_patch.cell_below;
}

void finish_patch_cell_graph(const IMesh& mesh, CellsInfo& cinfo, PatchesInfo& pinfo,
                             const MeshTriTopology& topo)
{
    auto components = find_patch_components(cinfo, pinfo);
    if (components.size() <= 1) return;

    std::vector<int> ambient_cell(components.size(), kNoIndex);
    std::vector<PatchAABB> comp_bb(components.size());
    std::vector<bool> nested_comp(components.size(), false);
    for (size_t ci = 0; ci < components.size(); ++ci) {
        for (int p : components[ci]) {
            PatchAABB pb = patch_aabb(mesh, pinfo, p);
            comp_bb[ci].expand(pb.min);
            comp_bb[ci].expand(pb.max);
        }
    }
    for (size_t ci = 0; ci < components.size(); ++ci) {
        for (size_t other = 0; other < components.size(); ++other) {
            if (ci != other && comp_bb[other].contains(comp_bb[ci])) {
                nested_comp[ci] = true;
                break;
            }
        }
    }
    for (size_t ci = 0; ci < components.size(); ++ci) {
        ambient_cell[ci] = find_ambient_cell_for_patches(mesh, pinfo, components[ci],
                                                         nested_comp[ci]);
    }

    // Nested components: merge inner ambient into containing cell.
    for (size_t inner = 0; inner < components.size(); ++inner) {
        int best_outer = -1;
        double best_vol = std::numeric_limits<double>::max();
        for (size_t outer = 0; outer < components.size(); ++outer) {
            if (inner == outer) continue;
            if (!comp_bb[outer].contains(comp_bb[inner])) continue;
            double vol = (comp_bb[outer].max.x - comp_bb[outer].min.x) *
                         (comp_bb[outer].max.y - comp_bb[outer].min.y) *
                         (comp_bb[outer].max.z - comp_bb[outer].min.z);
            if (vol < best_vol) {
                best_vol = vol;
                best_outer = static_cast<int>(outer);
            }
        }
        if (best_outer < 0) continue;

        // Merge inner ambient into the outer cell that geometrically contains the inner
        // component (inside outer solid), not the outer ambient (infinity).
        int containing = kNoIndex;
        for (int p : components[static_cast<size_t>(best_outer)]) {
            const Patch& patch = pinfo.patch(p);
            for (int raw : {patch.cell_above, patch.cell_below}) {
                int c = resolve_cell(cinfo, raw);
                if (c < 0 || c == ambient_cell[static_cast<size_t>(best_outer)]) continue;
                containing = c;
                break;
            }
            if (containing >= 0) break;
        }
        if (containing < 0) {
            // Fallback: nearest non-ambient cell in outer component.
            Vec3 center = comp_bb[inner].center();
            double best_d2 = std::numeric_limits<double>::max();
            for (int p : components[static_cast<size_t>(best_outer)]) {
                const Patch& patch = pinfo.patch(p);
                for (int raw : {patch.cell_above, patch.cell_below}) {
                    int c = resolve_cell(cinfo, raw);
                    if (c < 0 || c == ambient_cell[static_cast<size_t>(best_outer)]) continue;
                    PatchAABB pb = patch_aabb(mesh, pinfo, p);
                    Vec3 pc = pb.center();
                    double d2 = (pc.x - center.x) * (pc.x - center.x) +
                                  (pc.y - center.y) * (pc.y - center.y) +
                                  (pc.z - center.z) * (pc.z - center.z);
                    if (d2 < best_d2) {
                        best_d2 = d2;
                        containing = c;
                    }
                }
            }
        }
        if (containing >= 0 && ambient_cell[inner] >= 0) {
            merge_cells(containing, ambient_cell[inner], cinfo, pinfo);
        }
    }

    // Merge ambient cells of disjoint outer components.
    std::vector<int> outer_comps;
    for (size_t ci = 0; ci < components.size(); ++ci) {
        bool nested = false;
        for (size_t other = 0; other < components.size(); ++other) {
            if (ci != other && comp_bb[other].contains(comp_bb[ci])) {
                nested = true;
                break;
            }
        }
        if (!nested) outer_comps.push_back(static_cast<int>(ci));
    }
    if (outer_comps.size() > 1) {
        int merged = ambient_cell[static_cast<size_t>(outer_comps[0])];
        for (size_t i = 1; i < outer_comps.size(); ++i) {
            merge_cells(merged, ambient_cell[static_cast<size_t>(outer_comps[i])], cinfo, pinfo);
        }
    }
}

PatchesInfo find_patches(const IMesh& mesh, const MeshTriTopology& topo)
{
    const int ntri = static_cast<int>(mesh.tris.size());
    PatchesInfo pinfo(ntri);
    std::vector<int> stack;

    for (int t = 0; t < ntri; ++t) {
        if (pinfo.tri_assigned(t)) continue;
        stack.push_back(t);
        const int patch_idx = pinfo.add_patch();
        while (!stack.empty()) {
            int tcand = stack.back();
            stack.pop_back();
            if (pinfo.tri_assigned(tcand)) continue;
            pinfo.grow_patch(patch_idx, tcand);

            const IMeshTri& tri = mesh.tris[tcand];
            const int verts[3] = {tri.v0, tri.v1, tri.v2};
            for (int i = 0; i < 3; ++i) {
                const int a = verts[i];
                const int b = verts[(i + 1) % 3];
                const int64_t ek = edge_key(a, b);
                int t_other = topo.other_tri_if_manifold(ek, tcand);
                if (t_other >= 0) {
                    if (!pinfo.tri_assigned(t_other)) {
                        stack.push_back(t_other);
                    }
                } else {
                    const auto* etris = topo.edge_tris(ek);
                    if (etris) {
                        for (int tother : *etris) {
                            if (tother != tcand && pinfo.tri_assigned(tother)) {
                                int p_other = pinfo.patch_of_tri(tother);
                                if (p_other != patch_idx) {
                                    const int64_t pk = (static_cast<int64_t>(std::min(patch_idx, p_other)) << 32) |
                                                       static_cast<uint32_t>(std::max(patch_idx, p_other));
                                    if (!pinfo.patch_patch_edges.count(pk)) {
                                        pinfo.add_patch_patch_edge(patch_idx, p_other, {a, b});
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    return pinfo;
}

CellsInfo find_cells(const IMesh& mesh, const MeshTriTopology& topo, PatchesInfo& pinfo)
{
    CellsInfo cinfo;
    std::unordered_set<int64_t> processed;

    // Blender only sorts the non-manifold edges that separate two patches.
    // Processing every manifold edge folds the two sides of a closed patch into
    // the same cell and destroys the winding graph.
    for (const auto& [patch_pair, edge] : pinfo.patch_patch_edges) {
        (void)patch_pair;
        const int64_t ek = edge_key(edge.v0, edge.v1);
        if (processed.count(ek)) continue;
        processed.insert(ek);
        find_cells_from_edge(mesh, topo, pinfo, cinfo, edge.v0, edge.v1);
    }

    for (int p = 0; p < pinfo.tot_patch(); ++p) {
        Patch& patch = pinfo.patch(p);
        if (patch.cell_above == kNoIndex) {
            int c = cinfo.add_cell();
            patch.cell_above = c;
            cinfo.cell(c).add_patch(p);
        }
        if (patch.cell_below == kNoIndex) {
            int c = cinfo.add_cell();
            patch.cell_below = c;
            cinfo.cell(c).add_patch(p);
        }
        check_zero_volume(cinfo.cell(patch.cell_above), mesh, pinfo);
        check_zero_volume(cinfo.cell(patch.cell_below), mesh, pinfo);
    }
    fix_degenerate_patch_cells(pinfo, cinfo);
    return cinfo;
}

int find_ambient_cell(const IMesh& mesh, const PatchesInfo& pinfo)
{
    if (mesh.verts.empty() || pinfo.tot_patch() == 0) return kNoIndex;

    int best_patch = kNoIndex;
    double best_x = -std::numeric_limits<double>::max();
    double best_normal_x = -std::numeric_limits<double>::max();
    for (int p = 0; p < pinfo.tot_patch(); ++p) {
        const Patch& patch = pinfo.patch(p);
        for (int t : patch.tris) {
            const IMeshTri& tri = mesh.tris[static_cast<size_t>(t)];
            const double tri_max_x = std::max({
                mesh.verts[tri.v0].co.x,
                mesh.verts[tri.v1].co.x,
                mesh.verts[tri.v2].co.x});
            const Vec3 normal = tri_normal(mesh, t);
            if (tri_max_x > best_x ||
                (tri_max_x == best_x && normal.x > best_normal_x)) {
                best_x = tri_max_x;
                best_normal_x = normal.x;
                best_patch = p;
            }
        }
    }
    if (best_patch == kNoIndex) return kNoIndex;

    const Patch& hull_patch = pinfo.patch(best_patch);
    return best_normal_x >= 0.0 ? hull_patch.cell_above : hull_patch.cell_below;
}

void propagate_windings(const IMesh& mesh, CellsInfo& cinfo, PatchesInfo& pinfo, int ambient_cell,
                        BooleanOp op, int nshapes,
                        const std::function<int(int)>& shape_fn,
                        const std::vector<int>& shape_ambient)
{
    (void)mesh;
    (void)shape_ambient;
    ambient_cell = resolve_cell(cinfo, ambient_cell);
    if (ambient_cell < 0 || ambient_cell >= cinfo.tot_cell()) return;

    cinfo.init_windings(nshapes);
    std::vector<bool> visited(static_cast<size_t>(cinfo.tot_cell()), false);

    Cell& amb = cinfo.cell(ambient_cell);
    amb.winding.assign(static_cast<size_t>(nshapes), 0);
    amb.winding_assigned = true;
    amb.in_output_volume = apply_bool_op(op, amb.winding);

    std::queue<int> queue;
    queue.push(ambient_cell);
    visited[static_cast<size_t>(ambient_cell)] = true;

    while (!queue.empty()) {
        int c = queue.front();
        queue.pop();
        Cell& cell = cinfo.cell(c);

        for (int p : cell.patches) {
            Patch& patch = pinfo.patch(p);
            const int ca = resolve_cell(cinfo, patch.cell_above);
            const int cb = resolve_cell(cinfo, patch.cell_below);

            int c_neighbor = kNoIndex;
            if (cb == c) {
                c_neighbor = ca;
            } else if (ca == c) {
                c_neighbor = cb;
            } else {
                continue;
            }

            if (c_neighbor < 0 || c_neighbor >= cinfo.tot_cell()) continue;
            if (visited[static_cast<size_t>(c_neighbor)]) continue;

            int t = patch.rep_tri();
            if (t < 0) continue;
            int shape = shape_fn(t);
            if (shape < 0 || shape >= nshapes) continue;

            const bool current_is_below = cb == c;
            const int winding_delta = current_is_below ? -1 : 1;

            Cell& neighbor = cinfo.cell(c_neighbor);
            neighbor.winding = cell.winding;
            neighbor.winding[shape] += winding_delta;
            neighbor.winding_assigned = true;
            neighbor.in_output_volume = apply_bool_op(op, neighbor.winding);
            visited[static_cast<size_t>(c_neighbor)] = true;
            queue.push(c_neighbor);
        }
    }

    // Expand to any cells still adjacent to assigned cells (handles merged-ambient ordering).
    for (bool progressed = true; progressed;) {
        progressed = false;
        for (int c = 0; c < cinfo.tot_cell(); ++c) {
            if (!visited[static_cast<size_t>(c)]) continue;
            Cell& cell = cinfo.cell(c);
            for (int p : cell.patches) {
                Patch& patch = pinfo.patch(p);
                const int ca = resolve_cell(cinfo, patch.cell_above);
                const int cb = resolve_cell(cinfo, patch.cell_below);
                int c_neighbor = kNoIndex;
                if (cb == c) c_neighbor = ca;
                else if (ca == c) c_neighbor = cb;
                else continue;
                if (c_neighbor < 0 || c_neighbor >= cinfo.tot_cell()) continue;
                if (visited[static_cast<size_t>(c_neighbor)]) continue;

                int t = patch.rep_tri();
                if (t < 0) continue;
                int shape = shape_fn(t);
                if (shape < 0 || shape >= nshapes) continue;
                const bool current_is_below = cb == c;
                const int winding_delta = current_is_below ? -1 : 1;

                Cell& neighbor = cinfo.cell(c_neighbor);
                neighbor.winding = cell.winding;
                neighbor.winding[shape] += winding_delta;
                neighbor.winding_assigned = true;
                neighbor.in_output_volume = apply_bool_op(op, neighbor.winding);
                visited[static_cast<size_t>(c_neighbor)] = true;
                queue.push(c_neighbor);
                progressed = true;
            }
        }
    }
}

data::PcgGeometry extract_boolean_geometry(const IMesh& mesh, const PatchesInfo& pinfo,
                                           const CellsInfo& cinfo, BooleanOp op,
                                           std::vector<BooleanFaceOrigin>* face_origins)
{
    data::PcgGeometry out;
    std::unordered_map<int, int> vert_map;

    auto get_vert = [&](int vi) -> int {
        auto it = vert_map.find(vi);
        if (it != vert_map.end()) return it->second;
        int ni = static_cast<int>(out.points().size());
        const Vec3& co = mesh.verts[vi].co;
        out.points_mut().push_back({co.x, co.y, co.z});
        vert_map[vi] = ni;
        return ni;
    };

    auto emit_tri = [&](int v0, int v1, int v2, int source, int original_face,
                        int w_a, int w_b, bool is_seam) {
        int ov0 = get_vert(v0);
        int ov1 = get_vert(v1);
        int ov2 = get_vert(v2);
        out.faces_mut().push_back({ov0, ov1, ov2});
        const int face_idx = static_cast<int>(out.faces().size()) - 1;
        if (face_origins)
            face_origins->push_back({source, original_face, false});

        if (source == 0) {
            if (w_b > 0)
                out.groups().add(geometry::GroupDomain::Face, BooleanGroups::A_INSIDE_B, face_idx);
            else
                out.groups().add(geometry::GroupDomain::Face, BooleanGroups::A_OUTSIDE_B, face_idx);
        } else {
            if (w_a > 0)
                out.groups().add(geometry::GroupDomain::Face, BooleanGroups::B_INSIDE_A, face_idx);
            else
                out.groups().add(geometry::GroupDomain::Face, BooleanGroups::B_OUTSIDE_A, face_idx);
        }
        if (is_seam) {
            int64_t ek = edge_key(ov0, ov1);
            out.groups().add(geometry::GroupDomain::Edge, BooleanGroups::AB_SEAMS, static_cast<int>(ek));
        }
    };

    if (op == BooleanOp::Shatter) {
        for (int t = 0; t < static_cast<int>(mesh.tris.size()); ++t) {
            const IMeshTri& tri = mesh.tris[t];
            emit_tri(tri.v0, tri.v1, tri.v2, tri.source, tri.orig_face,
                     0, 0, tri.split_by_seam);
        }
        return out;
    }

    std::unordered_set<int64_t> seam_edges;
    for (const auto& rec : mesh.seam_edges) {
        seam_edges.insert(rec.edge_key);
    }

    for (int t = 0; t < static_cast<int>(mesh.tris.size()); ++t) {
        const IMeshTri& tri = mesh.tris[t];
        int p = pinfo.patch_of_tri(t);
        if (p < 0) continue;
        const Patch& patch = pinfo.patch(p);
        const int ca = resolve_cell(cinfo, patch.cell_above);
        const int cb = resolve_cell(cinfo, patch.cell_below);
        const Cell& above = cinfo.cell(ca);
        const Cell& below = cinfo.cell(cb);

        bool adj_zv = above.zero_volume || below.zero_volume;
        bool keep = above.in_output_volume ^ below.in_output_volume;

        if (adj_zv) {
            // Zero-volume coplanar stack: keep one copy if in/out differ on stack sides.
            if (!above.zero_volume && !below.zero_volume) continue;
            const Cell& nz = above.zero_volume ? below : above;
            const Cell& zv = above.zero_volume ? above : below;
            if (nz.in_output_volume == zv.in_output_volume) continue;
            keep = true;
        }

        if (!keep) continue;

        const bool flip = above.in_output_volume;
        int w_a = above.winding_assigned ? above.winding[0] : 0;
        int w_b = above.winding_assigned ? (above.winding.size() > 1 ? above.winding[1] : 0) : 0;
        if (!above.winding_assigned && below.winding_assigned) {
            w_a = below.winding[0];
            w_b = below.winding.size() > 1 ? below.winding[1] : 0;
        }

        bool is_seam = false;
        for (int i = 0; i < 3 && !is_seam; ++i) {
            int a = (&tri.v0)[i];
            int b = (&tri.v0)[(i + 1) % 3];
            if (seam_edges.count(edge_key(a, b))) is_seam = true;
        }

        if (flip) {
            emit_tri(tri.v0, tri.v2, tri.v1, tri.source, tri.orig_face,
                     w_a, w_b, is_seam);
        } else {
            emit_tri(tri.v0, tri.v1, tri.v2, tri.source, tri.orig_face,
                     w_a, w_b, is_seam);
        }
    }

    return out;
}

namespace {

bool ray_hits_triangle(const IMesh& mesh, int tri_index, const Vec3& origin,
                       const Vec3& direction, double& out_t)
{
    const IMeshTri& tri = mesh.tris[static_cast<size_t>(tri_index)];
    const Vec3& a = mesh.verts[tri.v0].co;
    const Vec3& b = mesh.verts[tri.v1].co;
    const Vec3& c = mesh.verts[tri.v2].co;
    const Vec3 edge_ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 edge_ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3 pvec{
        direction.y * edge_ac.z - direction.z * edge_ac.y,
        direction.z * edge_ac.x - direction.x * edge_ac.z,
        direction.x * edge_ac.y - direction.y * edge_ac.x};
    const double determinant =
        edge_ab.x * pvec.x + edge_ab.y * pvec.y + edge_ab.z * pvec.z;
    if (std::fabs(determinant) <= 1e-14) return false;
    const double inv_determinant = 1.0 / determinant;
    const Vec3 tvec{origin.x - a.x, origin.y - a.y, origin.z - a.z};
    const double u =
        (tvec.x * pvec.x + tvec.y * pvec.y + tvec.z * pvec.z) * inv_determinant;
    if (u < -1e-12 || u > 1.0 + 1e-12) return false;
    const Vec3 qvec{
        tvec.y * edge_ab.z - tvec.z * edge_ab.y,
        tvec.z * edge_ab.x - tvec.x * edge_ab.z,
        tvec.x * edge_ab.y - tvec.y * edge_ab.x};
    const double v =
        (direction.x * qvec.x + direction.y * qvec.y + direction.z * qvec.z) *
        inv_determinant;
    if (v < -1e-12 || u + v > 1.0 + 1e-12) return false;
    out_t =
        (edge_ac.x * qvec.x + edge_ac.y * qvec.y + edge_ac.z * qvec.z) *
        inv_determinant;
    return out_t > 1e-12;
}

bool point_inside_source(const IMesh& mesh, const BVH& bvh, const Vec3& point)
{
    static const Vec3 kDirections[] = {
        {1.0, 0.3713906763541037, 0.6947465906068658},
        {0.5270462766947299, 1.0, 0.3183098861837907},
        {0.4142135623730950, 0.6180339887498948, 1.0},
    };
    int inside_votes = 0;
    for (const Vec3& direction : kDirections) {
        std::vector<double> hits;
        const auto candidates = bvh.query_ray(point, direction);
        hits.reserve(candidates.size());
        for (int tri_index : candidates) {
            double t = 0.0;
            if (ray_hits_triangle(mesh, tri_index, point, direction, t))
                hits.push_back(t);
        }
        std::sort(hits.begin(), hits.end());
        int unique_hits = 0;
        double previous = -std::numeric_limits<double>::infinity();
        for (double t : hits) {
            const double tolerance = 1e-10 * std::max(1.0, std::fabs(t));
            if (unique_hits == 0 || std::fabs(t - previous) > tolerance) {
                ++unique_hits;
                previous = t;
            }
        }
        if ((unique_hits & 1) != 0) ++inside_votes;
    }
    return inside_votes >= 2;
}

bool output_contains(BooleanOp op, bool in_a, bool in_b)
{
    switch (op) {
        case BooleanOp::Union: return in_a || in_b;
        case BooleanOp::Intersect: return in_a && in_b;
        case BooleanOp::Subtract: return in_a && !in_b;
        case BooleanOp::Shatter: return true;
    }
    return false;
}

data::PcgGeometry extract_sampled_boundary(
    const IMesh& mesh, BooleanOp op,
    std::vector<BooleanFaceOrigin>* face_origins)
{
    data::PcgGeometry out;
    std::unordered_map<int, int> vert_map;
    std::set<std::array<int, 3>> emitted;
    std::unordered_set<int64_t> seam_edges;
    for (const IntersectEdgeRecord& seam : mesh.seam_edges) {
        seam_edges.insert(seam.edge_key);
    }

    std::vector<BVHTriangle> source_triangles[2];
    for (int tri_index = 0; tri_index < static_cast<int>(mesh.tris.size()); ++tri_index) {
        const IMeshTri& tri = mesh.tris[static_cast<size_t>(tri_index)];
        if (tri.source < 0 || tri.source > 1) continue;
        BVHTriangle item;
        item.tri_index = tri_index;
        item.bounds.expand(mesh.verts[tri.v0].co);
        item.bounds.expand(mesh.verts[tri.v1].co);
        item.bounds.expand(mesh.verts[tri.v2].co);
        source_triangles[tri.source].push_back(std::move(item));
    }
    BVH source_bvh[2];
    source_bvh[0].build(source_triangles[0]);
    source_bvh[1].build(source_triangles[1]);

    double max_abs = 1.0;
    for (const IMeshVert& v : mesh.verts) {
        max_abs = std::max(max_abs, std::max({
            std::fabs(v.co.x), std::fabs(v.co.y), std::fabs(v.co.z)}));
    }
    const double offset = max_abs * 1e-7;

    auto get_vert = [&](int vi) {
        auto found = vert_map.find(vi);
        if (found != vert_map.end()) return found->second;
        const int out_index = static_cast<int>(out.points().size());
        const Vec3& p = mesh.verts[vi].co;
        out.points_mut().push_back({p.x, p.y, p.z});
        vert_map.emplace(vi, out_index);
        return out_index;
    };

    for (const IMeshTri& tri : mesh.tris) {
        const Vec3& a = mesh.verts[tri.v0].co;
        const Vec3& b = mesh.verts[tri.v1].co;
        const Vec3& c = mesh.verts[tri.v2].co;
        Vec3 normal{
            (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
            (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
            (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)};
        const double normal_len =
            std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (normal_len <= 1e-20) continue;
        normal.x /= normal_len;
        normal.y /= normal_len;
        normal.z /= normal_len;
        const Vec3 centroid{
            (a.x + b.x + c.x) / 3.0,
            (a.y + b.y + c.y) / 3.0,
            (a.z + b.z + c.z) / 3.0};
        const Vec3 above{
            centroid.x + normal.x * offset,
            centroid.y + normal.y * offset,
            centroid.z + normal.z * offset};
        const Vec3 below{
            centroid.x - normal.x * offset,
            centroid.y - normal.y * offset,
            centroid.z - normal.z * offset};

        const bool above_a = point_inside_source(mesh, source_bvh[0], above);
        const bool above_b = point_inside_source(mesh, source_bvh[1], above);
        const bool below_a = point_inside_source(mesh, source_bvh[0], below);
        const bool below_b = point_inside_source(mesh, source_bvh[1], below);
        const bool output_above = output_contains(op, above_a, above_b);
        const bool output_below = output_contains(op, below_a, below_b);
        if (output_above == output_below) continue;

        std::array<int, 3> canonical{tri.v0, tri.v1, tri.v2};
        std::sort(canonical.begin(), canonical.end());
        if (!emitted.insert(canonical).second) continue;

        const bool flip = output_above;
        const int source_vertices[3] = {
            tri.v0,
            flip ? tri.v2 : tri.v1,
            flip ? tri.v1 : tri.v2};
        const int ov0 = get_vert(source_vertices[0]);
        const int ov1 = get_vert(source_vertices[1]);
        const int ov2 = get_vert(source_vertices[2]);
        out.faces_mut().push_back({ov0, ov1, ov2});
        const int face_index = static_cast<int>(out.faces().size()) - 1;
        if (face_origins)
            face_origins->push_back({tri.source, tri.orig_face, false});

        if (tri.source == 0) {
            out.groups().add(
                geometry::GroupDomain::Face,
                (above_b || below_b) ? BooleanGroups::A_INSIDE_B : BooleanGroups::A_OUTSIDE_B,
                face_index);
        } else {
            out.groups().add(
                geometry::GroupDomain::Face,
                (above_a || below_a) ? BooleanGroups::B_INSIDE_A : BooleanGroups::B_OUTSIDE_A,
                face_index);
        }
        for (int edge = 0; edge < 3; ++edge) {
            const int va = source_vertices[edge];
            const int vb = source_vertices[(edge + 1) % 3];
            if (seam_edges.count(edge_key(va, vb))) {
                out.groups().add(
                    geometry::GroupDomain::Edge,
                    BooleanGroups::AB_SEAMS,
                    static_cast<int>(edge_key(
                        edge == 0 ? ov0 : (edge == 1 ? ov1 : ov2),
                        edge == 0 ? ov1 : (edge == 1 ? ov2 : ov0))));
            }
        }
    }
    return out;
}

} // anonymous namespace

data::PcgGeometry classify_and_extract(
    const IMesh& combined, BooleanOp op,
    std::vector<BooleanFaceOrigin>* face_origins)
{
    if (face_origins)
        face_origins->clear();

    auto finalize_origins = [&]() {
        if (!face_origins) return;
        std::unordered_set<int64_t> changed_origins;
        for (const IMeshTri& tri : combined.tris) {
            if (tri.split_by_seam && tri.source >= 0 && tri.orig_face >= 0)
                changed_origins.insert(face_origin_key(tri.source, tri.orig_face));
        }
        for (BooleanFaceOrigin& origin : *face_origins) {
            origin.changed = origin.source < 0 || origin.original_face < 0 ||
                changed_origins.count(face_origin_key(origin.source, origin.original_face)) != 0;
        }
    };

    if (op != BooleanOp::Shatter) {
        data::PcgGeometry out = extract_sampled_boundary(combined, op, face_origins);
        finalize_origins();
        return out;
    }

    MeshTriTopology topo(combined);
    PatchesInfo pinfo = find_patches(combined, topo);
    CellsInfo cinfo = find_cells(combined, topo, pinfo);

    const int nshapes = 2;
    auto shape_fn = [&](int t) -> int {
        return combined.tris[static_cast<size_t>(t)].source;
    };

    std::vector<int> shape_ambient(static_cast<size_t>(nshapes), kNoIndex);
    {
        auto components = find_patch_components(cinfo, pinfo);
        std::vector<PatchAABB> comp_bb(components.size());
        for (size_t ci = 0; ci < components.size(); ++ci) {
            for (int p : components[ci]) {
                PatchAABB pb = patch_aabb(combined, pinfo, p);
                comp_bb[ci].expand(pb.min);
                comp_bb[ci].expand(pb.max);
            }
        }
        for (size_t ci = 0; ci < components.size(); ++ci) {
            bool nested = false;
            for (size_t other = 0; other < components.size(); ++other) {
                if (ci != other && comp_bb[other].contains(comp_bb[ci])) {
                    nested = true;
                    break;
                }
            }
            int amb = find_ambient_cell_for_patches(combined, pinfo, components[ci], nested);
            for (int p : components[ci]) {
                int t = pinfo.patch(p).rep_tri();
                if (t < 0) continue;
                int s = shape_fn(t);
                if (s >= 0 && s < nshapes) {
                    shape_ambient[static_cast<size_t>(s)] = amb;
                }
            }
        }
    }

    finish_patch_cell_graph(combined, cinfo, pinfo, topo);

    int ambient = find_ambient_cell(combined, pinfo);
    if (ambient == kNoIndex) {
        auto components = find_patch_components(cinfo, pinfo);
        for (const auto& comp : components) {
            int a = find_ambient_cell_for_patches(combined, pinfo, comp, false);
            if (a != kNoIndex) { ambient = a; break; }
        }
    }
    if (ambient == kNoIndex) ambient = 0;
    ambient = resolve_cell(cinfo, ambient);

    propagate_windings(combined, cinfo, pinfo, ambient, op, nshapes, shape_fn, shape_ambient);

    data::PcgGeometry out =
        extract_boolean_geometry(combined, pinfo, cinfo, op, face_origins);
    finalize_origins();
    return out;
}

} // namespace pcg::internal::geometry
