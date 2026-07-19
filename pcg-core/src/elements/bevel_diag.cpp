#include "elements/bevel_diag.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace pcg::internal::elements::bevel {

// --- CapExtents helpers (used by both production and diagnostic code) ---

CapExtents mesh_cap_extents(const std::vector<Vec3>& positions)
{
    CapExtents cap;
    if (positions.empty())
        return cap;
    cap.x0 = cap.x1 = positions.front().x;
    for (const Vec3& p : positions) {
        cap.x0 = std::min(cap.x0, p.x);
        cap.x1 = std::max(cap.x1, p.x);
    }
    return cap;
}

bool on_cap_plane_x(const Vec3& p, const CapExtents& cap, double tol)
{
    return std::abs(p.x - cap.x0) <= tol || std::abs(p.x - cap.x1) <= tol;
}

// --- PCG_BEVEL_DIAG staging topology (Phase 2.1 / 2.2) ---

bool bevel_diag_enabled()
{
    const char* v = std::getenv("PCG_BEVEL_DIAG");
    return v && v[0] && v[0] != '0';
}

BevelTopoStats analyze_bevel_output(const BevelParams::OutputMesh& out, const CapExtents& cap, double tol)
{
    // Ensure derived triangle cache is up-to-date.
    const_cast<BevelParams::OutputMesh&>(out).rebuild_triangles_from_faces();

    BevelTopoStats stats;
    stats.verts = static_cast<int>(out.vertices.size());
    stats.tris = static_cast<int>(out.triangles.size() / 3);

    auto edge_key = [](int a, int b) -> uint64_t {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        return (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
               static_cast<uint32_t>(hi);
    };

    std::unordered_map<uint64_t, int> counts;
    for (size_t i = 0; i + 2 < out.triangles.size(); i += 3) {
        const int ia = out.triangles[i];
        const int ib = out.triangles[i + 1];
        const int ic = out.triangles[i + 2];
        if (ia == ib || ib == ic || ia == ic) {
            ++stats.index_degenerate;
            continue;
        }
        const int tri[3] = {ia, ib, ic};
        for (int e = 0; e < 3; ++e)
            ++counts[edge_key(tri[e], tri[(e + 1) % 3])];
    }

    std::unordered_map<int, std::vector<int>> adjacency;
    for (const auto& [key, count] : counts) {
        if (count == 1) {
            ++stats.boundary;
            const int a = static_cast<int>(key >> 32);
            const int b = static_cast<int>(key & 0xffffffffu);
            const Vec3& pa = out.vertices[static_cast<size_t>(a)];
            const Vec3& pb = out.vertices[static_cast<size_t>(b)];
            if (on_cap_plane_x(pa, cap, tol) && on_cap_plane_x(pb, cap, tol))
                ++stats.cap_boundary;
            adjacency[a].push_back(b);
            adjacency[b].push_back(a);
        } else if (count > 2) {
            ++stats.nonmanifold;
        }
    }

    std::unordered_map<int, bool> visited;
    for (const auto& [start, _] : adjacency) {
        if (visited[start])
            continue;
        std::vector<int> loop_verts;
        int current = start;
        int previous = -1;
        do {
            visited[current] = true;
            loop_verts.push_back(current);
            int next = -1;
            for (int candidate : adjacency[current]) {
                if (candidate != previous) {
                    next = candidate;
                    break;
                }
            }
            previous = current;
            current = next;
        } while (current >= 0 && current != start &&
                 static_cast<int>(loop_verts.size()) <= static_cast<int>(adjacency.size()));

        ++stats.loops;
        if (static_cast<int>(loop_verts.size()) == 3) {
            ++stats.loops_size_3;
            bool cap_loop = true;
            for (int vi : loop_verts) {
                if (!on_cap_plane_x(out.vertices[static_cast<size_t>(vi)], cap, tol)) {
                    cap_loop = false;
                    break;
                }
            }
            if (cap_loop)
                ++stats.cap_loops_size_3;
        }
    }

    return stats;
}

void log_bevel_stage(const char* stage, const BevelParams::OutputMesh& out, const CapExtents& cap)
{
    if (!bevel_diag_enabled())
        return;
    // Ensure derived triangle cache is up-to-date for topology analysis.
    const_cast<BevelParams::OutputMesh&>(out).rebuild_triangles_from_faces();
    const BevelTopoStats stats = analyze_bevel_output(out, cap, 0.05);
    std::fprintf(stderr,
                 "[PCG_BEVEL_DIAG] stage=%s verts=%d tris=%d boundary=%d nonmanifold=%d "
                 "cap_boundary=%d loops=%d loops3=%d cap_loops3=%d index_degen=%d\n",
                 stage, stats.verts, stats.tris, stats.boundary, stats.nonmanifold,
                 stats.cap_boundary, stats.loops, stats.loops_size_3, stats.cap_loops_size_3,
                 stats.index_degenerate);
}

void diag_terminal_verts_after_vmesh(BevelParams& bp)
{
    if (!bevel_diag_enabled() || !bp.positions)
        return;

    const CapExtents cap = mesh_cap_extents(*bp.positions);
    const double tol = 0.05;

    for (BevVert& bv : bp.bevverts) {
        if (bv.selcount != 1 || bv.edgecount < 3 || !bv.vmesh || !bv.vmesh->boundstart)
            continue;

        const Vec3 vco = (*bp.positions)[static_cast<size_t>(bv.v_idx)];
        if (!on_cap_plane_x(vco, cap, tol))
            continue;

        BoundVert* bstart = bv.vmesh->boundstart;
        BoundVert* ebev_bnd = nullptr;
        BoundVert* walk = bstart;
        do {
            if (walk->ebev) {
                ebev_bnd = walk;
                break;
            }
            walk = walk->next;
        } while (walk != bstart);

        if (!ebev_bnd || !ebev_bnd->ebev)
            continue;

        EdgeHalf* ebev = ebev_bnd->ebev;
        BoundVert* lv = ebev->leftv;
        BoundVert* rv = ebev->rightv;
        if (!lv || !rv)
            continue;

        const NewVert& p0 = bv.vmesh->at(lv->index, 0, 0);
        const NewVert& pns = bv.vmesh->at(lv->index, 0, bp.seg);
        const NewVert& pmid = bv.vmesh->at(lv->index, 0, bp.seg / 2);
        const bool prof_collapsed =
            length_squared(sub(p0.co, pns.co)) <= 1e-12;

        std::fprintf(stderr,
                     "[PCG_BEVEL_DIAG] terminal_vm v=%d vm_count=%d kind=%d "
                     "lv=(%.4f,%.4f,%.4f) rv=(%.4f,%.4f,%.4f) profile0==profile_ns=%d "
                     "pmid_valid=%d pmid=(%.4f,%.4f,%.4f) prof_start=(%.4f,%.4f,%.4f) prof_end=(%.4f,%.4f,%.4f) "
                     "ebev_fprev=%d ebev_fnext=%d profile_idx=%d\n",
                     bv.v_idx, bv.vmesh->count, static_cast<int>(bv.vmesh->mesh_kind),
                     lv->nv.co.x, lv->nv.co.y, lv->nv.co.z,
                     rv->nv.co.x, rv->nv.co.y, rv->nv.co.z,
                     prof_collapsed ? 1 : 0,
                     pmid.valid ? 1 : 0, pmid.co.x, pmid.co.y, pmid.co.z,
                     ebev_bnd->profile.start.x, ebev_bnd->profile.start.y, ebev_bnd->profile.start.z,
                     ebev_bnd->profile.end.x, ebev_bnd->profile.end.y, ebev_bnd->profile.end.z,
                     ebev->fprev, ebev->fnext, ebev->profile_index);
    }
}

} // namespace pcg::internal::elements::bevel
