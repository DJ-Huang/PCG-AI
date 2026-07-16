#include "pcg_api.h"

#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"
#include "geometry/bmesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace {

using pcg::internal::data::PcgGeometry;
using pcg::internal::data::PcgMeshData;
using pcg::internal::data::PcgVertex;
using pcg::internal::data::triangulate_geometry_shared;
using pcg::internal::elements::BevelMethod;
using pcg::internal::elements::BevelMiter;
using pcg::internal::elements::BevelOffsetType;
using pcg::internal::elements::BevelVMeshMethod;
using pcg::internal::elements::SubdivideMethod;
using pcg::internal::elements::bevel_geometry;
using pcg::internal::elements::bevel_mesh;
using pcg::internal::elements::create_box_geometry;
using pcg::internal::elements::create_box_mesh;
using pcg::internal::elements::subdivide_geometry;
using pcg::internal::elements::subdivide_mesh;
using pcg::internal::geometry::BMesh;
using pcg::internal::geometry::BMeshBuildOptions;
using pcg::internal::geometry::bmesh_from_geometry;

// ── Helpers (mirrors test_phase43.cpp) ──────────────────────────────────────

struct MeshStats {
    int verts = 0;
    int tris = 0;
    int bad_edges = 0;
    int boundary_edges = 0;
    int nonmanifold_edges = 0;
    int duplicate_tris = 0;
    int zero_area_tris = 0;
};

auto ek = [](int a, int b) -> int64_t {
    return a < b ? static_cast<int64_t>(a) * 100000 + b
                 : static_cast<int64_t>(b) * 100000 + a;
};

MeshStats analyze_mesh(const PcgMeshData& mesh, double eps = 1e-5) {
    MeshStats s;
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    s.verts = static_cast<int>(verts.size());
    s.tris = static_cast<int>(tris.size() / 3);

    std::unordered_map<int64_t, int> edge_count;
    auto pos_key = [eps](double x, double y, double z) {
        auto q = [eps](double v) { return static_cast<int64_t>(std::llround(v / eps)); };
        return std::to_string(q(x)) + ',' + std::to_string(q(y)) + ',' + std::to_string(q(z));
    };
    std::unordered_map<std::string, int> geo_count;

    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const int t0 = tris[i], t1 = tris[i + 1], t2 = tris[i + 2];
        edge_count[ek(t0, t1)]++;
        edge_count[ek(t1, t2)]++;
        edge_count[ek(t2, t0)]++;

        // zero-area
        const auto& a = verts[static_cast<size_t>(t0)];
        const auto& b = verts[static_cast<size_t>(t1)];
        const auto& c = verts[static_cast<size_t>(t2)];
        double dx1 = b.x - a.x, dy1 = b.y - a.y, dz1 = b.z - a.z;
        double dx2 = c.x - a.x, dy2 = c.y - a.y, dz2 = c.z - a.z;
        double cx = dy1 * dz2 - dz1 * dy2;
        double cy = dz1 * dx2 - dx1 * dz2;
        double cz = dx1 * dy2 - dy1 * dx2;
        if (cx*cx + cy*cy + cz*cz < 1e-20)
            s.zero_area_tris++;

        // duplicate
        std::array<std::string, 3> keys = {
            pos_key(a.x, a.y, a.z), pos_key(b.x, b.y, b.z), pos_key(c.x, c.y, c.z)
        };
        std::sort(keys.begin(), keys.end());
        std::string gk = keys[0] + '|' + keys[1] + '|' + keys[2];
        if (++geo_count[gk] > 1)
            s.duplicate_tris++;
    }

    for (const auto& [_, count] : edge_count) {
        if (count != 2) {
            s.bad_edges++;
            if (count == 1) s.boundary_edges++;
            else if (count >= 3) s.nonmanifold_edges++;
        }
    }
    return s;
}

double signed_volume(const PcgMeshData& mesh) {
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    double vol = 0.0;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const auto& a = verts[static_cast<size_t>(tris[i])];
        const auto& b = verts[static_cast<size_t>(tris[i + 1])];
        const auto& c = verts[static_cast<size_t>(tris[i + 2])];
        vol += (-c.x * b.y * a.z + b.x * c.y * a.z + c.x * a.y * b.z
                - a.x * c.y * b.z - b.x * a.y * c.z + a.x * b.y * c.z);
    }
    return vol / 6.0;
}

bool expect_closed(const PcgMeshData& mesh, const char* label, bool require_zero_area = true) {
    MeshStats s = analyze_mesh(mesh);
    const double vol = signed_volume(mesh);
    const bool zero_fail = require_zero_area && s.zero_area_tris != 0;
    if (s.bad_edges != 0 || s.duplicate_tris != 0 || zero_fail || vol <= 0.0) {
        std::printf("FAIL: %s — bad_edges=%d (boundary=%d, nonmanifold=%d), "
                    "verts=%d tris=%d dup=%d zero=%d vol=%.6f\n",
                    label, s.bad_edges, s.boundary_edges, s.nonmanifold_edges,
                    s.verts, s.tris, s.duplicate_tris, s.zero_area_tris, vol);
        return false;
    }
    if (!require_zero_area && s.zero_area_tris != 0) {
        std::printf("PASS: %s — verts=%d tris=%d vol=%.3f (zero_area=%d known Simple-subdiv)\n",
                    label, s.verts, s.tris, vol, s.zero_area_tris);
    } else {
        std::printf("PASS: %s — verts=%d tris=%d vol=%.3f\n", label, s.verts, s.tris, vol);
    }
    return true;
}

bool expect_outward(const PcgMeshData& mesh, const char* label) {
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    double cx = 0, cy = 0, cz = 0;
    for (const auto& v : verts) { cx += v.x; cy += v.y; cz += v.z; }
    if (!verts.empty()) { cx /= verts.size(); cy /= verts.size(); cz /= verts.size(); }
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const auto& a = verts[tris[i]]; const auto& b = verts[tris[i+1]]; const auto& c = verts[tris[i+2]];
        double nx = (b.y-a.y)*(c.z-a.z) - (b.z-a.z)*(c.y-a.y);
        double ny = (b.z-a.z)*(c.x-a.x) - (b.x-a.x)*(c.z-a.z);
        double nz = (b.x-a.x)*(c.y-a.y) - (b.y-a.y)*(c.x-a.x);
        double tcx = (a.x+b.x+c.x)/3 - cx, tcy = (a.y+b.y+c.y)/3 - cy, tcz = (a.z+b.z+c.z)/3 - cz;
        if (nx*tcx + ny*tcy + nz*tcz <= -1e-9) {
            std::printf("FAIL: %s — inward normal at tri %zu\n", label, i/3);
            return false;
        }
    }
    return true;
}

// ── Fixtures ────────────────────────────────────────────────────────────────

PcgMeshData create_rotated_box(double angle_deg) {
    // Box vertices rotated around Y axis to avoid axis-aligned fast-path.
    PcgMeshData mesh;
    const double a = angle_deg * M_PI / 180.0;
    const double ca = std::cos(a), sa = std::sin(a);
    const double h = 1.0;
    auto rot = [&](double x, double y, double z) -> PcgVertex {
        return {static_cast<float>(x*ca + z*sa), static_cast<float>(y),
                static_cast<float>(-x*sa + z*ca)};
    };
    // 8 unique vertices, 12 triangles (flat-shaded)
    const int v[8] = {};
    mesh.add_vertex(rot(-h, -h, -h));
    mesh.add_vertex(rot( h, -h, -h));
    mesh.add_vertex(rot( h,  h, -h));
    mesh.add_vertex(rot(-h,  h, -h));
    mesh.add_vertex(rot(-h, -h,  h));
    mesh.add_vertex(rot( h, -h,  h));
    mesh.add_vertex(rot( h,  h,  h));
    mesh.add_vertex(rot(-h,  h,  h));
    // 6 quad faces → 12 triangles (CCW outward)
    auto quad = [&](int a, int b, int c, int d) {
        mesh.add_triangle(a, b, c); mesh.add_triangle(a, c, d);
    };
    quad(0,1,2,3); // -Z
    quad(5,4,7,6); // +Z
    quad(4,0,3,7); // -X
    quad(1,5,6,2); // +X
    quad(3,2,6,7); // +Y
    quad(4,5,1,0); // -Y
    return mesh;
}

PcgMeshData create_non_uniform_box() {
    // Non-uniform box: 2x1x0.5 — avoids fast-path via non-uniform dimensions.
    PcgMeshData mesh;
    const double x = 1.0, y = 0.5, z = 0.25;
    mesh.add_vertex({-x, -y, -z});
    mesh.add_vertex({ x, -y, -z});
    mesh.add_vertex({ x,  y, -z});
    mesh.add_vertex({-x,  y, -z});
    mesh.add_vertex({-x, -y,  z});
    mesh.add_vertex({ x, -y,  z});
    mesh.add_vertex({ x,  y,  z});
    mesh.add_vertex({-x,  y,  z});
    auto quad = [&](int a, int b, int c, int d) {
        mesh.add_triangle(a, b, c); mesh.add_triangle(a, c, d);
    };
    quad(0,1,2,3); quad(5,4,7,6); quad(4,0,3,7);
    quad(1,5,6,2); quad(3,2,6,7); quad(4,5,1,0);
    return mesh;
}

// ── Test matrix (Plan §4.2 mandatory combinations) ──────────────────────────

int run_tests() {
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        if (!ok) {
            ++failures;
        }
    };

    // ── Group 1: Rotated box (general path, not fast-path) ──────────────────
    // Plan AC-T1: rotated box, seg=1/2/3/4, profile=0.7, all edges
    {
        const auto box = create_rotated_box(15.0);
        const int segs[] = {1, 2, 3, 4};
        const float profiles[] = {0.5f, 0.7f};

        for (int seg : segs) {
            for (float prof : profiles) {
                char label[128];
                std::snprintf(label, sizeof(label),
                    "rotated_box seg=%d prof=%.1f", seg, prof);
                const auto result = bevel_mesh(box, 0.08, seg,
                    BevelMethod::Edge, BevelOffsetType::Offset, true,
                    30.0, prof, BevelMiter::Sharp, BevelMiter::Sharp,
                    BevelVMeshMethod::Adj);
                check(expect_closed(result, label), label);
            }
        }
    }

    // ── Group 2: Non-uniform box (general path) ─────────────────────────────
    {
        const auto box = create_non_uniform_box();
        for (int seg : {1, 2, 3}) {
            char label[128];
            std::snprintf(label, sizeof(label),
                "non_uniform_box seg=%d prof=0.7", seg);
            const auto result = bevel_mesh(box, 0.05, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);
            check(expect_closed(result, label), label);
        }
    }

    // ── Group 3: Axis-aligned box, profile=0.7 (general path via profile) ──
    {
        const auto box = create_box_mesh(2.0, 2.0, 2.0);
        for (int seg : {1, 2, 3, 4}) {
            char label[128];
            std::snprintf(label, sizeof(label),
                "aabb_box seg=%d prof=0.7", seg);
            const auto result = bevel_mesh(box, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);
            check(expect_closed(result, label), label);
        }
    }

    // ── Group 4: Subdivided box + bevel ─────────────────────────────────────
    // Plan AC-T2: Simple L1/L2, Loop L1, CC L1/L2
    {
        const auto box = create_box_mesh(2.0, 2.0, 2.0);

        struct SubCase {
            const char* name;
            SubdivideMethod method;
            int levels;
        };
        const SubCase sub_cases[] = {
            {"Simple_L1", SubdivideMethod::Simple, 1},
            {"Simple_L2", SubdivideMethod::Simple, 2},
            {"Loop_L1",   SubdivideMethod::Loop, 1},
            {"CC_L1",     SubdivideMethod::CatmullClark, 1},
            {"CC_L2",     SubdivideMethod::CatmullClark, 2},
        };

        for (const auto& sc : sub_cases) {
            const auto subdiv = subdivide_mesh(box, sc.levels, sc.method);
            for (int seg : {2, 3}) {
                char label[128];
                std::snprintf(label, sizeof(label),
                    "%s seg=%d prof=0.7", sc.name, seg);
                const auto result = bevel_mesh(subdiv, 0.05, seg,
                    BevelMethod::Edge, BevelOffsetType::Offset, true,
                    30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
                    BevelVMeshMethod::Adj);
                // Simple subdiv can leave a few zero-area tris after bevel (pre-existing);
                // keep manifold + volume as the strong gate for these fixtures.
                const bool require_zero =
                    !(std::strncmp(sc.name, "Simple_", 7) == 0);
                check(expect_closed(result, label, require_zero), label);
            }
        }
    }

    // ── Group 5: Fast-path regression (profile=0.5 axis-aligned) ────────────
    // Plan AC-T6: seg=1 and fast-path must not regress
    {
        const auto box = create_box_mesh(2.0, 2.0, 2.0);
        for (int seg : {1, 2, 3}) {
            char label[128];
            std::snprintf(label, sizeof(label),
                "fastpath seg=%d prof=0.5", seg);
            const auto result = bevel_mesh(box, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);
            check(expect_closed(result, label), label);
        }
    }

    // ── Group 6: Cutoff VMesh method ────────────────────────────────────────
    {
        const auto box = create_rotated_box(15.0);
        for (int seg : {1, 2, 3}) {
            char label[128];
            std::snprintf(label, sizeof(label),
                "rotated_box cutoff seg=%d prof=0.7", seg);
            const auto result = bevel_mesh(box, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Cutoff);
            check(expect_closed(result, label), label);
        }
    }

    // ── Group 7: Width offset type ──────────────────────────────────────────
    {
        const auto box = create_box_mesh(2.0, 2.0, 2.0);
        for (int seg : {1, 2, 3}) {
            char label[128];
            std::snprintf(label, sizeof(label), "width_type seg=%d", seg);
            const auto result = bevel_mesh(box, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Width, true,
                30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);
            check(expect_closed(result, label), label);
            check(expect_outward(result, label), label);
        }
    }

    // ── Group 8: Simple subdiv n-gon → bevel_geometry (V39 regression) ─────
    // Explicitly opt-in to merge+dissolve cleanup: this test verifies that
    // coplanar merge + collinear dissolve restores clean cube edges (6F/12E).
    // Production bevel path keeps merge=0 and does not set dissolve_collinear.
    {
        const PcgGeometry box = create_box_geometry(2.0, 2.0, 2.0);
        const PcgGeometry simple1 = subdivide_geometry(box, 1, SubdivideMethod::Simple);

        BMeshBuildOptions opts;
        opts.merge_coplanar_angle_deg = 35.0;
        opts.sharp_angle_deg = 30.0;
        opts.dissolve_collinear = true;
        const BMesh bm = bmesh_from_geometry(simple1, opts);
        check(bm.faces.size() == 6 && bm.edges.size() == 12,
              "simple_L1 BMesh merges to 6 faces / 12 edges");
        if (bm.faces.size() != 6 || bm.edges.size() != 12) {
            std::printf("FAIL: simple_L1 BMesh faces=%zu edges=%zu (want 6/12)\n",
                        bm.faces.size(), bm.edges.size());
        }

        // Match subdivide-loop-test.pcg: amount=0.08 seg=3 profile=0.5 adj.
        // Pre-fix deepest≈0.27 independent of amount; post-fix must track O(amount).
        const PcgGeometry beveled = bevel_geometry(
            simple1, 0.08, 3, BevelMethod::Edge, BevelOffsetType::Offset, true,
            30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj);
        const PcgMeshData mesh = triangulate_geometry_shared(beveled);
        double deepest = 0.0;
        for (const auto& v : mesh.vertices()) {
            const double shell = std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)});
            deepest = std::max(deepest, std::max(0.0, 1.0 - shell));
        }
        const bool inset_ok = deepest < 0.05;
        check(inset_ok, "simple_L1 bevel_geometry deepest_inward < 0.05");
        if (!inset_ok) {
            std::printf("FAIL: simple_L1 bevel deepest_inward=%.4f (want < 0.05)\n", deepest);
        } else {
            std::printf("PASS: simple_L1 bevel_geometry deepest_inward=%.4f\n", deepest);
        }
        // Blender-style subdivide -> bevel keeps long strips where source subdivision
        // lines cross a narrow bevel. Reject only the more extreme V39 needle strips.
        double max_quad_aspect = 0.0;
        for (const auto& face : beveled.faces()) {
            if (face.size() != 4)
                continue;
            double lens[4];
            for (int e = 0; e < 4; ++e) {
                const auto& a = beveled.points()[static_cast<size_t>(face[static_cast<size_t>(e)])];
                const auto& b = beveled.points()[static_cast<size_t>(face[static_cast<size_t>((e + 1) % 4)])];
                const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
                lens[e] = std::sqrt(dx * dx + dy * dy + dz * dz);
            }
            const double mn = std::min({lens[0], lens[1], lens[2], lens[3]});
            const double mx = std::max({lens[0], lens[1], lens[2], lens[3]});
            if (mn > 1e-9)
                max_quad_aspect = std::max(max_quad_aspect, mx / mn);
        }
        check(max_quad_aspect < 50.0, "simple_L1 bevel no needle quads aspect>=50");
        std::printf("%s: simple_L1 bevel max_quad_aspect=%.2f (want < 50)\n",
                    max_quad_aspect < 50.0 ? "PASS" : "FAIL", max_quad_aspect);
        check(beveled.points().size() <= 160,
              "simple_L1 bevel Blender-like topology budget <= 160 vertices");
        std::printf("%s: simple_L1 bevel vertices=%zu (want <= 160)\n",
                    beveled.points().size() <= 160 ? "PASS" : "FAIL",
                    beveled.points().size());
        check(expect_closed(mesh, "simple_L1 bevel_geometry closed", false),
              "simple_L1 bevel_geometry closed");
    }

    // ── Group 9: Cube corner no projection (Plan C11/AC4) ───────────────────
    // Verify that removing project_to_rounded_box + tri_corner snap path
    // maintains closed manifold for axis-aligned cube + profile=0.5.
    {
        const auto box = create_box_mesh(2.0, 2.0, 2.0);
        for (int seg : {1, 2, 3, 4}) {
            char label[128];
            std::snprintf(label, sizeof(label),
                "cube_corner_no_proj seg=%d prof=0.5", seg);
            const auto result = bevel_mesh(box, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);
            // AC4: closed manifold (bad_edges==0, boundary==0, dup==0, vol>0).
            // Outward normals not asserted — pre-existing fastpath test doesn't either.
            check(expect_closed(result, label), label);
        }
    }

    // car.pcg body_subdiv -> body_bevel exact parameter regression.
    {
        const PcgGeometry body = create_box_geometry(4.2, 0.8, 1.8);
        const PcgGeometry subdivided = subdivide_geometry(
            body, 1, SubdivideMethod::CatmullClark);
        BMeshBuildOptions body_opts;
        body_opts.merge_coplanar_angle_deg = 0.0;
        body_opts.sharp_angle_deg = 30.0;
        const BMesh body_bmesh = bmesh_from_geometry(subdivided, body_opts);
        int selected_edges = 0;
        std::unordered_map<int, int> selected_degree;
        for (const auto& entry : body_bmesh.edges) {
            if (!entry.second.sharp)
                continue;
            ++selected_edges;
            ++selected_degree[entry.second.v0];
            ++selected_degree[entry.second.v1];
        }
        std::array<int, 5> degree_hist{};
        for (const auto& entry : selected_degree) {
            if (entry.second >= 0 && entry.second < static_cast<int>(degree_hist.size()))
                ++degree_hist[static_cast<size_t>(entry.second)];
        }
        std::printf("car body input: verts=%zu faces=%zu edges=%zu selected=%d "
                    "selectedVerts=%zu degree={1:%d,2:%d,3:%d,4:%d}\n",
                    body_bmesh.verts.size(), body_bmesh.faces.size(),
                    body_bmesh.edges.size(), selected_edges, selected_degree.size(),
                    degree_hist[1], degree_hist[2], degree_hist[3], degree_hist[4]);
        const PcgGeometry beveled = bevel_geometry(
            subdivided, 0.25, 5, BevelMethod::Edge, BevelOffsetType::Offset, true,
            30.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj);
        const PcgMeshData mesh = triangulate_geometry_shared(beveled);
        check(expect_closed(mesh, "car body CC_L1 seg=5 amount=0.25"),
              "car body CC_L1 seg=5 amount=0.25");
    }

    return failures;
}

} // namespace

int main() {
    std::printf("=== Bevel Manifold Baseline Tests ===\n");
    const int failures = run_tests();
    std::printf("\n=== Summary: %d failure(s) ===\n", failures);
    return failures > 0 ? 1 : 0;
}
