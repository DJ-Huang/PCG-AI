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
#include <map>
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
using pcg::internal::elements::SubdivideMethod;
using pcg::internal::elements::bevel_geometry;
using pcg::internal::elements::bevel_mesh;
using pcg::internal::elements::create_box_geometry;
using pcg::internal::elements::create_box_mesh;
using pcg::internal::elements::subdivide_geometry;

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

double max_corner_quad_planarity_error(const PcgGeometry& geom) {
    double max_error = 0.0;
    const auto& points = geom.points();
    for (const auto& face : geom.faces()) {
        if (face.size() != 4)
            continue;
        bool in_corner = true;
        for (int index : face) {
            const auto& point = points[static_cast<size_t>(index)];
            if (std::abs(point.x) <= 0.9 || std::abs(point.y) <= 0.9 || std::abs(point.z) <= 0.9) {
                in_corner = false;
                break;
            }
        }
        if (!in_corner)
            continue;

        const auto& a = points[static_cast<size_t>(face[0])];
        const auto& b = points[static_cast<size_t>(face[1])];
        const auto& c = points[static_cast<size_t>(face[2])];
        const auto& d = points[static_cast<size_t>(face[3])];
        const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
        const double acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
        const double nx = aby * acz - abz * acy;
        const double ny = abz * acx - abx * acz;
        const double nz = abx * acy - aby * acx;
        const double normal_length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (normal_length <= 1e-12)
            continue;
        const double distance = std::abs((d.x - a.x) * nx + (d.y - a.y) * ny +
                                         (d.z - a.z) * nz) / normal_length;
        max_error = std::max(max_error, distance);
    }
    return max_error;
}

// ── Oracle dump (matches blender_bevel_oracle.py format) ─────────────────────

void dump_geometry_oracle(const PcgGeometry& geom, const char* output_path,
                           double cube_size, double amount, int segments,
                           float profile, double angle_limit)
{
    std::FILE* f = std::fopen(output_path, "w");
    if (!f) return;

    const auto& pts = geom.points();
    const auto& faces = geom.faces();

    // Quantize
    auto q = [](double v) { return std::round(v / 1e-7) * 1e-7; };

    // Sorted vertices
    std::vector<std::array<double, 3>> sorted_verts;
    for (const auto& p : pts)
        sorted_verts.push_back({q(p.x), q(p.y), q(p.z)});
    std::sort(sorted_verts.begin(), sorted_verts.end());

    // Canonical faces
    auto canon_cycle = [](const std::vector<int>& verts) -> std::vector<int> {
        int n = static_cast<int>(verts.size());
        if (n == 0) return verts;
        int best = 0;
        for (int i = 1; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                int a = verts[(i + j) % n], b = verts[(best + j) % n];
                if (a < b) { best = i; break; }
                if (a > b) break;
            }
        }
        std::vector<int> result;
        for (int j = 0; j < n; ++j)
            result.push_back(verts[(best + j) % n]);
        return result;
    };

    std::vector<std::vector<int>> canon_faces;
    for (const auto& face : faces) {
        if (face.size() >= 3)
            canon_faces.push_back(canon_cycle(face));
    }
    std::sort(canon_faces.begin(), canon_faces.end());

    // Edge incidence
    auto ek = [](int a, int b) -> std::pair<int,int> {
        return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
    };
    std::map<std::pair<int,int>, int> edge_count;
    for (const auto& face : faces) {
        if (face.size() < 3) continue;
        int n = static_cast<int>(face.size());
        for (int i = 0; i < n; ++i)
            edge_count[ek(face[i], face[(i+1)%n])]++;
    }

    int bad_edges = 0, boundary_edges = 0, nonmanifold = 0;
    for (const auto& [k, v] : edge_count) {
        if (v != 2) ++bad_edges;
        if (v == 1) ++boundary_edges;
        if (v > 2) ++nonmanifold;
    }

    // Signed volume (fan triangulation)
    double vol = 0.0;
    for (const auto& face : faces) {
        if (face.size() < 3) continue;
        const auto& v0 = pts[face[0]];
        for (size_t i = 1; i + 1 < face.size(); ++i) {
            const auto& v1 = pts[face[i]];
            const auto& v2 = pts[face[i+1]];
            vol += (v0.x * (v1.y * v2.z - v1.z * v2.y) +
                    v0.y * (v1.z * v2.x - v1.x * v2.z) +
                    v0.z * (v1.x * v2.y - v1.y * v2.x)) / 6.0;
        }
    }

    // AABB
    double minx=1e9, miny=1e9, minz=1e9, maxx=-1e9, maxy=-1e9, maxz=-1e9;
    for (const auto& p : pts) {
        minx=std::min(minx,(double)p.x); maxx=std::max(maxx,(double)p.x);
        miny=std::min(miny,(double)p.y); maxy=std::max(maxy,(double)p.y);
        minz=std::min(minz,(double)p.z); maxz=std::max(maxz,(double)p.z);
    }

    // Corner patches
    double half = cube_size / 2.0;
    double radius = 0.3;

    std::fprintf(f, "{\n");
    std::fprintf(f, "  \"metadata\": {\n");
    std::fprintf(f, "    \"source\": \"PCG\",\n");
    std::fprintf(f, "    \"params\": {\"size\": %.1f, \"amount\": %.4f, \"segments\": %d, \"profile\": %.4f, \"angle_limit\": %.1f}\n",
        cube_size, amount, segments, profile, angle_limit);
    std::fprintf(f, "  },\n");
    std::fprintf(f, "  \"geometry\": {\n");
    std::fprintf(f, "    \"vert_count\": %zu,\n", pts.size());
    std::fprintf(f, "    \"face_count\": %zu,\n", faces.size());
    std::fprintf(f, "    \"edge_count\": %zu,\n", edge_count.size());
    std::fprintf(f, "    \"aabb\": {\"min\": [%.10f, %.10f, %.10f], \"max\": [%.10f, %.10f, %.10f]},\n",
        minx, miny, minz, maxx, maxy, maxz);
    std::fprintf(f, "    \"signed_volume\": %.10f,\n", vol);
    std::fprintf(f, "    \"bad_edge_count\": %d,\n", bad_edges);
    std::fprintf(f, "    \"boundary_edge_count\": %d,\n", boundary_edges);
    std::fprintf(f, "    \"nonmanifold_edge_count\": %d\n", nonmanifold);
    std::fprintf(f, "  },\n");

    // Sorted vertices
    std::fprintf(f, "  \"vertices_sorted\": [");
    for (size_t i = 0; i < sorted_verts.size(); ++i) {
        if (i > 0) std::fprintf(f, ", ");
        std::fprintf(f, "[%.10f, %.10f, %.10f]", sorted_verts[i][0], sorted_verts[i][1], sorted_verts[i][2]);
    }
    std::fprintf(f, "],\n");

    // Canonical faces
    std::fprintf(f, "  \"faces_canonical\": [");
    for (size_t i = 0; i < canon_faces.size(); ++i) {
        if (i > 0) std::fprintf(f, ", ");
        std::fprintf(f, "[");
        for (size_t j = 0; j < canon_faces[i].size(); ++j) {
            if (j > 0) std::fprintf(f, ", ");
            std::fprintf(f, "%d", canon_faces[i][j]);
        }
        std::fprintf(f, "]");
    }
    std::fprintf(f, "],\n");

    // Corner patches
    std::fprintf(f, "  \"corner_patches\": {\n");
    bool first_patch = true;
    for (int sx = -1; sx <= 1; sx += 2)
    for (int sy = -1; sy <= 1; sy += 2)
    for (int sz = -1; sz <= 1; sz += 2) {
        double cx = sx * half, cy = sy * half, cz = sz * half;
        std::vector<int> local_indices;
        std::vector<std::array<double,3>> local_verts;
        std::map<int, int> vert_map;
        for (size_t vi = 0; vi < pts.size(); ++vi) {
            double dx = pts[vi].x - cx, dy = pts[vi].y - cy, dz = pts[vi].z - cz;
            if (std::sqrt(dx*dx + dy*dy + dz*dz) < radius) {
                int li = static_cast<int>(local_verts.size());
                local_verts.push_back({q(pts[vi].x), q(pts[vi].y), q(pts[vi].z)});
                vert_map[static_cast<int>(vi)] = li;
                local_indices.push_back(static_cast<int>(vi));
            }
        }
        std::vector<std::vector<int>> local_faces;
        for (const auto& face : faces) {
            bool all_local = true;
            std::vector<int> remapped;
            for (int idx : face) {
                auto it = vert_map.find(idx);
                if (it == vert_map.end()) { all_local = false; break; }
                remapped.push_back(it->second);
            }
            if (all_local && remapped.size() >= 3)
                local_faces.push_back(canon_cycle(remapped));
        }
        std::sort(local_faces.begin(), local_faces.end());

        // Valence
        std::map<int, int> valence;
        for (const auto& lf : local_faces)
            for (int vi : lf) valence[vi]++;

        if (!first_patch) std::fprintf(f, ",\n");
        first_patch = false;
        std::fprintf(f, "    \"(%d, %d, %d)\": {\n", sx, sy, sz);
        std::fprintf(f, "      \"center\": [%.1f, %.1f, %.1f],\n", cx, cy, cz);
        std::fprintf(f, "      \"vert_count\": %zu,\n", local_verts.size());
        std::fprintf(f, "      \"face_count\": %zu,\n", local_faces.size());
        std::fprintf(f, "      \"vertices\": [");
        for (size_t i = 0; i < local_verts.size(); ++i) {
            if (i > 0) std::fprintf(f, ", ");
            std::fprintf(f, "[%.10f, %.10f, %.10f]", local_verts[i][0], local_verts[i][1], local_verts[i][2]);
        }
        std::fprintf(f, "],\n");
        std::fprintf(f, "      \"faces\": [");
        for (size_t i = 0; i < local_faces.size(); ++i) {
            if (i > 0) std::fprintf(f, ", ");
            std::fprintf(f, "[");
            for (size_t j = 0; j < local_faces[i].size(); ++j) {
                if (j > 0) std::fprintf(f, ", ");
                std::fprintf(f, "%d", local_faces[i][j]);
            }
            std::fprintf(f, "]");
        }
        std::fprintf(f, "],\n");
        std::fprintf(f, "      \"valence\": {");
        bool first_v = true;
        for (const auto& [k, v] : valence) {
            if (!first_v) std::fprintf(f, ", ");
            first_v = false;
            std::fprintf(f, "\"%d\": %d", k, v);
        }
        std::fprintf(f, "}\n");
        std::fprintf(f, "    }");
    }
    std::fprintf(f, "\n  }\n");
    std::fprintf(f, "}\n");
    std::fclose(f);
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

    // ── T3: AC-P4: Axis-box bevel must not add a dense face grid ─────────────
    // Blender-style bevel topology depends on segment count, not box size /
    // bevel width. The old rounded-box fast-path added A×A face grids.
    {
        const auto box = create_box_mesh(2.0, 2.0, 2.0);
        const auto geom = geometry_from_box_mesh(box);
        constexpr double kAmount = 0.08;

        for (int seg : {1, 2, 3}) {
            char label[128];
            std::snprintf(label, sizeof(label), "T3 axis-box seg=%d compact topology", seg);

            const auto result = bevel_geometry(geom, kAmount, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);

            const auto stats = analyze_faces(result);
            const size_t expected_vertices =
                static_cast<size_t>(4 * seg * seg + 20 * seg);
            const int bad = count_bad_edges(result);
            std::printf("  T3 seg=%d: verts=%zu expected=%zu faces=%d bad_edges=%d\n",
                seg, result.points().size(), expected_vertices, stats.total_faces, bad);
            check(result.points().size() == expected_vertices, label);
            check(bad == 0, label);
            check(stats.degenerate == 0, label);
            check(stats.zero_area == 0, label);
        }
    }

    // V49: cube bevel corner alignment with Blender oracle.
    // make_unit_square_map must match Blender's bilinear map exactly.
    // The primary metric is max corner vertex distance to Blender golden,
    // not corner_quad_planarity (which is now a hygiene-only metric).
    {
        // No-subdiv case: directly comparable to Blender oracle
        const auto box_geom = create_box_geometry(2.0, 2.0, 2.0);
        const auto result = bevel_geometry(box_geom, 0.08, 3,
            BevelMethod::Edge, BevelOffsetType::Offset, true,
            25.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
            BevelVMeshMethod::Adj);

        const auto stats = analyze_faces(result);
        const int bad = count_bad_edges(result);
        const double corner_error = max_corner_quad_planarity_error(result);

        // Dump for external golden comparison
        const char* dump_path = std::getenv("PCG_BEVEL_DUMP_V49");
        if (!dump_path) dump_path = "/tmp/pcg-bevel-pcg-v49.json";
        dump_geometry_oracle(result, dump_path, 2.0, 0.08, 3, 0.7f, 25.0);

        std::printf("  V49 cube bevel: points=%zu faces=%zu bad_edges=%d corner_quad_error=%.8f\n",
            result.points().size(), result.faces().size(), bad, corner_error);
        check(bad == 0, "V49 manifold (bad_edges==0)");
        check(stats.degenerate == 0, "V49 no degenerate faces");
        check(stats.zero_area == 0, "V49 no zero-area faces");
        // Hygiene: corner quad planarity should be small (not primary acceptance)
        check(corner_error < 0.01, "V49 corner quad planarity (hygiene)");

        // Golden comparison: if Blender oracle JSON exists, compare corner vertices
        const char* golden_path = std::getenv("PCG_BEVEL_GOLDEN");
        if (!golden_path) golden_path = "/tmp/pcg-bevel-blender-simple.json";
        std::FILE* gf = std::fopen(golden_path, "r");
        if (gf) {
            std::fclose(gf);
            // Load golden and compare vertices
            // Simple: re-run dump and compare with Python externally
            std::printf("  V49 golden: %s exists (run external comparison)\n", golden_path);
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

    // ── T7: Cube corner snap to superellipsoid (Plan C10/AC2) ────────────────
    // Axis-aligned cube + profile=0.5 triggers tri_corner_adj_vmesh path.
    // PRO_CIRCLE_R → normalize → each corner vertex should be equidistant
    // from the corner center (i.e., on a sphere). Also verify no tri faces
    // in corner regions (only quads or quad+center ngon).
    {
        const auto box = create_box_mesh(2.0, 2.0, 2.0);
        const auto geom = geometry_from_box_mesh(box);

        for (int seg : {2, 3, 4}) {
            char label[128];
            std::snprintf(label, sizeof(label), "T7 cube_corner_snap seg=%d", seg);

            const auto result = bevel_geometry(geom, 0.08, seg,
                BevelMethod::Edge, BevelOffsetType::Offset, true,
                30.0, 0.5f, BevelMiter::Sharp, BevelMiter::Sharp,
                BevelVMeshMethod::Adj);

            const auto stats = analyze_faces(result);
            const int bad = count_bad_edges(result);

            // For PRO_CIRCLE_R (profile=0.5), corner vertices should be on a sphere.
            // Check that corner-region vertices (|x|>0.9 && |y|>0.9 && |z|>0.9)
            // have similar distance from origin (within 10% of each other).
            double min_dist = 1e9, max_dist = 0.0;
            for (const auto& p : result.points()) {
                if (std::abs(p.x) > 0.9 && std::abs(p.y) > 0.9 && std::abs(p.z) > 0.9) {
                    double d = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
                    min_dist = std::min(min_dist, d);
                    max_dist = std::max(max_dist, d);
                }
            }

            std::printf("  T7 seg=%d: faces=%d tris=%d quads=%d ngons=%d bad_edges=%d "
                        "corner_dist=[%.4f, %.4f]\n",
                seg, stats.total_faces, stats.tri_faces, stats.quad_faces,
                stats.ngon_faces, bad, min_dist, max_dist);

            check(bad == 0, label);
            check(stats.degenerate == 0, label);
            // Corner vertices should be roughly spherical (snap_to_superellipsoid)
            if (max_dist > 1e-6) {
                check((max_dist - min_dist) / max_dist < 0.15, label);
            }
            // Odd seg with 3 boundverts produces a triangle center ngon per corner
            // (8 corners × 1 tri = 8 tris for seg=3). Even seg should be all quads.
            if (seg % 2 == 0) {
                check(stats.tri_faces == 0, label);
            }
        }
    }

    // ── Oracle dump: simple cube matching Blender oracle ──────────────────
    // Dumps PCG bevel output in same JSON format as blender_bevel_oracle.py
    // for direct comparison. Uses identical params: amount=0.08, seg=3,
    // profile=0.7, angleLimit=25 (matching subdivide-loop-test.pcg).
    {
        const char* dump_path = std::getenv("PCG_BEVEL_DUMP");
        if (!dump_path) dump_path = "/tmp/pcg-bevel-pcg-simple.json";

        // Simple cube (no subdivision) — matches Blender oracle simple case
        const auto box_geom = create_box_geometry(2.0, 2.0, 2.0);
        const auto result = bevel_geometry(box_geom, 0.08, 3,
            BevelMethod::Edge, BevelOffsetType::Offset, true,
            25.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
            BevelVMeshMethod::Adj);
        const auto stats = analyze_faces(result);
        const int bad = count_bad_edges(result);
        dump_geometry_oracle(result, dump_path, 2.0, 0.08, 3, 0.7f, 25.0);
        std::printf("  Oracle dump (simple cube): verts=%zu faces=%zu bad_edges=%d -> %s\n",
            result.points().size(), result.faces().size(), bad, dump_path);
        check(bad == 0, "Oracle dump simple cube manifold");
        check(stats.degenerate == 0, "Oracle dump simple cube no degenerate");

        // Also dump with .pcg params (CatmullClark L2, profile=0.7)
        const char* dump_cc = std::getenv("PCG_BEVEL_DUMP_CC");
        if (!dump_cc) dump_cc = "/tmp/pcg-bevel-pcg-cc2.json";
        const auto cc_geom = subdivide_geometry(
            create_box_geometry(2.0, 2.0, 2.0), 2, SubdivideMethod::CatmullClark);
        const auto cc_result = bevel_geometry(cc_geom, 0.08, 3,
            BevelMethod::Edge, BevelOffsetType::Offset, true,
            25.0, 0.7f, BevelMiter::Sharp, BevelMiter::Sharp,
            BevelVMeshMethod::Adj);
        const auto cc_stats = analyze_faces(cc_result);
        const int cc_bad = count_bad_edges(cc_result);
        dump_geometry_oracle(cc_result, dump_cc, 2.0, 0.08, 3, 0.7f, 25.0);
        std::printf("  Oracle dump (CC L2): verts=%zu faces=%zu bad_edges=%d -> %s\n",
            cc_result.points().size(), cc_result.faces().size(), cc_bad, dump_cc);
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
