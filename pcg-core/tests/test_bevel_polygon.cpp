// Strong polygon output assertions per Plan §7 (T1-T5).
// Verifies that bevel_geometry returns n-gon faces (not just triangles),
// polygon hygiene (no degenerate/duplicate/cyclic-dup faces), and
// fast-path face count formulas (AC-P4).

#include "pcg_api.h"

#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/bevel_blender.hpp"
#include "geometry/bmesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using pcg::internal::data::PcgGeometry;
using pcg::internal::data::PcgMeshData;
using pcg::internal::data::PcgVertex;
using pcg::internal::data::geometry_from_mesh;
using pcg::internal::elements::BevelMethod;
using pcg::internal::elements::BevelMiter;
using pcg::internal::elements::BevelOffsetType;
using pcg::internal::elements::BevelVMeshMethod;
using pcg::internal::elements::bevel_geometry;
using pcg::internal::elements::bevel_mesh;
using pcg::internal::elements::create_box_mesh;

// ── Helpers ──────────────────────────────────────────────────────────────────

PcgMeshData make_rotated_box(double deg)
{
    PcgMeshData mesh;
    const double ca = std::cos(deg * M_PI / 180.0);
    const double sa = std::sin(deg * M_PI / 180.0);
    double x = 1.0, y = 1.0, z = 1.0;
    auto rot = [&](double px, double py, double pz) {
        return PcgVertex{
            static_cast<float>(px * ca - py * sa),
            static_cast<float>(px * sa + py * ca),
            static_cast<float>(pz)};
    };
    mesh.add_vertex(rot(-x, -y, -z));
    mesh.add_vertex(rot( x, -y, -z));
    mesh.add_vertex(rot( x,  y, -z));
    mesh.add_vertex(rot(-x,  y, -z));
    mesh.add_vertex(rot(-x, -y,  z));
    mesh.add_vertex(rot( x, -y,  z));
    mesh.add_vertex(rot( x,  y,  z));
    mesh.add_vertex(rot(-x,  y,  z));
    auto quad = [&](int a, int b, int c, int d) {
        mesh.add_triangle(a, b, c);
        mesh.add_triangle(a, c, d);
    };
    quad(0,1,2,3); quad(5,4,7,6); quad(4,0,3,7);
    quad(1,5,6,2); quad(3,2,6,7); quad(4,5,1,0);
    return mesh;
}

PcgGeometry geometry_from_box_mesh(const PcgMeshData& mesh)
{
    return geometry_from_mesh(mesh);
}

// ── Polygon hygiene checks ──────────────────────────────────────────────────

struct FaceStats {
    int total_faces = 0;
    int tri_faces = 0;
    int quad_faces = 0;
    int ngon_faces = 0;
    int degenerate = 0;
    int zero_area = 0;
    int cyclic_dup = 0;
    int bad_index = 0;
};

std::string cyclic_canonical(const std::vector<int>& verts) {
    int n = static_cast<int>(verts.size());
    int min_start = 0;
    for (int i = 1; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            if (verts[(i + j) % n] < verts[(min_start + j) % n]) { min_start = i; break; }
            if (verts[(i + j) % n] > verts[(min_start + j) % n]) break;
        }
    }
    std::string key;
    for (int j = 0; j < n; ++j)
        key += std::to_string(verts[(min_start + j) % n]) + ",";
    return key;
}

FaceStats analyze_faces(const PcgGeometry& geom) {
    FaceStats s;
    std::set<std::string> seen_cyclic;

    for (const auto& face : geom.faces()) {
        if (face.size() < 3) { ++s.degenerate; continue; }
        ++s.total_faces;

        if (face.size() == 3) ++s.tri_faces;
        else if (face.size() == 4) ++s.quad_faces;
        else ++s.ngon_faces;

        // Check indices in range
        bool in_range = true;
        for (int idx : face) {
            if (idx < 0 || idx >= static_cast<int>(geom.points().size())) {
                in_range = false; break;
            }
        }
        if (!in_range) { ++s.bad_index; continue; }

        // Check consecutive/first-last duplicates
        bool has_dup = false;
        for (size_t i = 0; i < face.size(); ++i) {
            if (face[i] == face[(i + 1) % face.size()]) { has_dup = true; break; }
        }
        if (has_dup) { ++s.degenerate; continue; }

        // Check >= 3 distinct vertices
        std::unordered_set<int> distinct(face.begin(), face.end());
        if (distinct.size() < 3) { ++s.degenerate; continue; }

        // Newell area check
        const auto& pts = geom.points();
        double nx = 0, ny = 0, nz = 0;
        for (size_t i = 0; i < face.size(); ++i) {
            const auto& curr = pts[face[i]];
            const auto& next = pts[face[(i + 1) % face.size()]];
            nx += (curr.y - next.y) * (curr.z + next.z);
            ny += (curr.z - next.z) * (curr.x + next.x);
            nz += (curr.x - next.x) * (curr.y + next.y);
        }
        if (nx*nx + ny*ny + nz*nz < 1e-20) { ++s.zero_area; continue; }

        // Cyclic dedup
        std::string canon = cyclic_canonical(face);
        if (seen_cyclic.count(canon)) { ++s.cyclic_dup; continue; }
        seen_cyclic.insert(canon);
    }
    return s;
}

// ── Shared-vertex bad_edges check (per tip-mesh-closed-manifold-check) ────────

int count_bad_edges(const PcgGeometry& geom) {
    auto ek = [](int a, int b) -> int64_t {
        return a < b ? static_cast<int64_t>(a) * 100000 + b
                     : static_cast<int64_t>(b) * 100000 + a;
    };

    // Weld vertices at 1e-5 precision
    auto pos_key = [](const pcg::internal::data::PcgVec3& v) {
        auto q = [](double val) { return static_cast<int64_t>(std::llround(val / 1e-5)); };
        return std::to_string(q(v.x)) + ',' + std::to_string(q(v.y)) + ',' + std::to_string(q(v.z));
    };
    std::unordered_map<std::string, int> weld_map;
    std::vector<int> remap(geom.points().size(), -1);
    std::vector<pcg::internal::data::PcgVec3> welded;
    for (size_t vi = 0; vi < geom.points().size(); ++vi) {
        const std::string key = pos_key(geom.points()[vi]);
        auto it = weld_map.find(key);
        if (it != weld_map.end()) { remap[vi] = it->second; }
        else {
            int idx = static_cast<int>(welded.size());
            welded.push_back(geom.points()[vi]);
            weld_map[key] = idx;
            remap[vi] = idx;
        }
    }

    // Build edge counts from triangulated faces (fan)
    std::unordered_map<int64_t, int> edge_count;
    for (const auto& face : geom.faces()) {
        if (face.size() < 3) continue;
        std::vector<int> remapped;
        for (int idx : face) remapped.push_back(remap[idx]);
        // Fold dups
        std::vector<int> clean;
        for (int v : remapped) {
            if (!clean.empty() && v == clean.back()) continue;
            clean.push_back(v);
        }
        if (clean.size() > 1 && clean.front() == clean.back()) clean.pop_back();
        if (clean.size() < 3) continue;

        // Fan triangulate
        const int i0 = clean[0];
        for (size_t i = 1; i + 1 < clean.size(); ++i) {
            edge_count[ek(i0, clean[i])]++;
            edge_count[ek(clean[i], clean[i + 1])]++;
            edge_count[ek(clean[i + 1], i0)]++;
        }
    }

    int bad = 0;
    for (const auto& [key, count] : edge_count) {
        if (count != 2) ++bad;
    }
    return bad;
}

// ── Tests ───────────────────────────────────────────────────────────────────

int run_tests() {
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        if (!ok) {
            ++failures;
            std::printf("  FAIL: %s\n", label);
        }
    };

    // ── T1: AC-W1: bevel_geometry returns faces with arity >= 4 ─────────────
    {
        const auto box = make_rotated_box(15.0);
        const auto geom = geometry_from_box_mesh(box);

        for (int seg : {2, 3, 4}) {
            char label[128];
            std::snprintf(label, sizeof(label), "T1 seg=%d has ngon faces", seg);

            const auto result = bevel_geometry(geom, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);

            const auto stats = analyze_faces(result);
            std::printf("  T1 seg=%d: faces=%d tris=%d quads=%d ngons=%d\n",
                seg, stats.total_faces, stats.tri_faces, stats.quad_faces, stats.ngon_faces);
            check(stats.ngon_faces > 0 || stats.quad_faces > 0, label);
        }
    }

    // ── T2: AC-P1/P2/P3: Polygon hygiene on rotated box ─────────────────────
    {
        const auto box = make_rotated_box(15.0);
        const auto geom = geometry_from_box_mesh(box);

        for (int seg : {1, 2, 3, 4}) {
            char label[128];
            std::snprintf(label, sizeof(label), "T2 seg=%d polygon hygiene", seg);

            const auto result = bevel_geometry(geom, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);

            const auto stats = analyze_faces(result);
            check(stats.degenerate == 0, label);
            check(stats.zero_area == 0, label);
            check(stats.cyclic_dup == 0, label);
            check(stats.bad_index == 0, label);

            // AC-M1: shared-vertex bad_edges == 0 for closed fixture
            const int bad = count_bad_edges(result);
            std::printf("  T2 seg=%d: faces=%d bad_edges=%d\n", seg, stats.total_faces, bad);
            check(bad == 0, label);
        }
    }

    // ── T3: AC-P4: Fast-path face count formula ─────────────────────────────
    // Axis-aligned box with profile=0.5 triggers fast-path.
    // faces = 6 + 12s + 8s^2 for s in {1,2,3}
    {
        const auto box = create_box_mesh(2.0, 2.0, 2.0);
        const auto geom = geometry_from_box_mesh(box);

        for (int seg : {1, 2, 3}) {
            char label[128];
            std::snprintf(label, sizeof(label), "T3 fast-path seg=%d face count", seg);

            const auto result = bevel_geometry(geom, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);

            const auto stats = analyze_faces(result);
            const int expected_faces = 6 + 12 * seg + 8 * seg * seg;
            std::printf("  T3 seg=%d: faces=%d expected=%d\n",
                seg, stats.total_faces, expected_faces);
            check(stats.total_faces == expected_faces, label);
            check(stats.degenerate == 0, label);
            check(stats.zero_area == 0, label);
        }
    }

    // ── T4: AC-W2: Binary round-trip face consistency ──────────────────────
    // Geometry faces survive serialization round-trip (n-gon → tri → n-gon).
    // After round-trip through mesh (fan-triangulated), each original n-gon face
    // becomes (arity-2) triangle faces. Verify total derived triangle count matches.
    {
        const auto box = make_rotated_box(15.0);
        const auto geom = geometry_from_box_mesh(box);
        const auto result = bevel_geometry(geom, 0.08, 3,
            BevelMethod::Edge, BevelOffsetType::Offset, true,
            30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
            BevelVMeshMethod::Adj);

        // Count derived triangles from n-gon faces
        int original_tris = 0;
        for (const auto& face : result.faces()) {
            if (face.size() >= 3)
                original_tris += static_cast<int>(face.size()) - 2;
        }

        // Rebuild geometry from mesh (simulates binary round-trip)
        PcgMeshData mesh;
        for (const auto& p : result.points())
            mesh.add_vertex({p.x, p.y, p.z});
        for (const auto& face : result.faces()) {
            if (face.size() < 3) continue;
            const int i0 = face[0];
            for (size_t i = 1; i + 1 < face.size(); ++i)
                mesh.add_triangle(i0, face[i], face[i + 1]);
        }
        const auto rebuilt = geometry_from_mesh(mesh);

        int rebuilt_tris = 0;
        for (const auto& face : rebuilt.faces()) {
            if (face.size() >= 3)
                rebuilt_tris += static_cast<int>(face.size()) - 2;
        }

        check(original_tris == rebuilt_tris,
            "T4 binary round-trip triangle count");
    }

    // ── T5: AC-L1: polygon-derived vs bevel_mesh geometric compat ───────────
    {
        const auto box = make_rotated_box(15.0);

        const auto mesh_result = bevel_mesh(box, 0.08, 3,
            BevelMethod::Edge, BevelOffsetType::Offset, true,
            30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
            BevelVMeshMethod::Adj);

        const auto geom = geometry_from_box_mesh(box);
        const auto geom_result = bevel_geometry(geom, 0.08, 3,
            BevelMethod::Edge, BevelOffsetType::Offset, true,
            30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
            BevelVMeshMethod::Adj);

        // Vertex counts should match (both from same bevel output)
        check(mesh_result.vertices().size() == geom_result.points().size(),
            "T5 vertex count match");

        // Triangle count from mesh should equal fan-triangulated face count
        const auto& faces = geom_result.faces();
        int derived_tris = 0;
        for (const auto& face : faces) {
            if (face.size() >= 3)
                derived_tris += static_cast<int>(face.size()) - 2;
        }
        check(static_cast<int>(mesh_result.triangles().size() / 3) == derived_tris,
            "T5 triangle count match");
    }

    // ── T6: Cutoff vmesh_method ──────────────────────────────────────────────
    {
        const auto box = make_rotated_box(15.0);
        const auto geom = geometry_from_box_mesh(box);

        for (int seg : {2, 3}) {
            char label[128];
            std::snprintf(label, sizeof(label), "T6 cutoff seg=%d", seg);

            const auto result = bevel_geometry(geom, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Cutoff);

            const auto stats = analyze_faces(result);
            check(stats.degenerate == 0, label);
            check(stats.zero_area == 0, label);
            check(stats.bad_index == 0, label);
        }
    }

    return failures;
}

} // namespace

int main() {
    std::printf("=== Bevel Polygon Output Tests ===\n");
    const int failures = run_tests();
    std::printf("\n=== Summary: %d failure(s) ===\n", failures);
    return failures > 0 ? 1 : 0;
}
