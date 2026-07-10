// Arrangement + CSG implementation for boolean mesh operations.
// Split phase: BVH intersection + triangle subdivision.
// Classify/extract: Blender-aligned BFS winding (boolean_csg.cpp).

#include "geometry/arrangement.hpp"
#include "geometry/boolean_csg.hpp"
#include "geometry/bvh.hpp"
#include "geometry/robust_predicates.hpp"
#include "geometry/tri_split.hpp"
#include "geometry/coplanar_partition.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace pcg::internal::geometry {

void Arrangement::build(const IMesh& mesh_a, const IMesh& mesh_b, const BooleanOptions& opts)
{
    (void)opts;
    const int total_tris = static_cast<int>(mesh_a.tris.size() + mesh_b.tris.size());
    triangles.resize(static_cast<size_t>(total_tris));
    for (int i = 0; i < static_cast<int>(mesh_a.tris.size()); ++i) {
        triangles[static_cast<size_t>(i)].source = 0;
    }
    const int offset_b = static_cast<int>(mesh_a.tris.size());
    for (int i = 0; i < static_cast<int>(mesh_b.tris.size()); ++i) {
        triangles[static_cast<size_t>(i + offset_b)].source = 1;
    }
}

void Arrangement::classify(const BooleanOptions& opts)
{
    (void)opts;
}

bool Arrangement::check_manifold(const IMesh& combined) const
{
    (void)combined;
    for (const auto& [key, adj] : edge_adjacency) {
        (void)key;
        if (adj.size() > 2) return false;
    }
    return true;
}

namespace {

struct SplitResult {
    IMesh combined;
};

void conform_triangle_edges(IMesh& mesh, double weld_epsilon)
{
    const size_t candidate_vertex_count = mesh.verts.size();
    std::vector<IMeshTri> conformed;
    conformed.reserve(mesh.tris.size());
    const double eps2 = weld_epsilon * weld_epsilon;

    for (const IMeshTri& tri : mesh.tris) {
        const int corners[3] = {tri.v0, tri.v1, tri.v2};
        std::vector<int> boundary;
        boundary.reserve(8);

        for (int edge = 0; edge < 3; ++edge) {
            const int va = corners[edge];
            const int vb = corners[(edge + 1) % 3];
            boundary.push_back(va);
            const Vec3& a = mesh.verts[va].co;
            const Vec3& b = mesh.verts[vb].co;
            const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
            const double len2 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
            if (len2 <= eps2) continue;

            std::vector<std::pair<double, int>> points_on_edge;
            for (size_t vi = 0; vi < candidate_vertex_count; ++vi) {
                if (static_cast<int>(vi) == va || static_cast<int>(vi) == vb) continue;
                const Vec3& p = mesh.verts[vi].co;
                const Vec3 ap{p.x - a.x, p.y - a.y, p.z - a.z};
                const double t = (ap.x * ab.x + ap.y * ab.y + ap.z * ab.z) / len2;
                if (t <= 1e-10 || t >= 1.0 - 1e-10) continue;
                const Vec3 closest{
                    a.x + t * ab.x,
                    a.y + t * ab.y,
                    a.z + t * ab.z};
                const double dx = p.x - closest.x;
                const double dy = p.y - closest.y;
                const double dz = p.z - closest.z;
                if (dx * dx + dy * dy + dz * dz <= eps2) {
                    points_on_edge.emplace_back(t, static_cast<int>(vi));
                }
            }
            std::sort(points_on_edge.begin(), points_on_edge.end());
            for (const auto& [t, vi] : points_on_edge) {
                (void)t;
                if (boundary.back() != vi) boundary.push_back(vi);
            }
        }

        if (boundary.size() == 3) {
            conformed.push_back(tri);
            continue;
        }

        Vec3 centroid{};
        for (int vi : boundary) {
            centroid.x += mesh.verts[vi].co.x;
            centroid.y += mesh.verts[vi].co.y;
            centroid.z += mesh.verts[vi].co.z;
        }
        const double inv_count = 1.0 / static_cast<double>(boundary.size());
        centroid.x *= inv_count;
        centroid.y *= inv_count;
        centroid.z *= inv_count;
        const int center = mesh.find_or_insert_vert(centroid, weld_epsilon);

        for (size_t i = 0; i < boundary.size(); ++i) {
            IMeshTri sub = tri;
            sub.v0 = center;
            sub.v1 = boundary[i];
            sub.v2 = boundary[(i + 1) % boundary.size()];
            sub.split_by_seam = true;
            conformed.push_back(sub);
        }
    }
    mesh.tris = std::move(conformed);
}

SplitResult build_split_mesh(const data::PcgGeometry& geo_a,
                             const data::PcgGeometry& geo_b,
                             const BooleanOptions& opts,
                             BooleanErrorType& error,
                             std::string& error_msg)
{
    error = BooleanErrorType::Ok;

    double max_extent = 0;
    for (const auto& p : geo_a.points()) {
        max_extent = std::max(max_extent, std::max(std::fabs(p.x), std::max(std::fabs(p.y), std::fabs(p.z))));
    }
    for (const auto& p : geo_b.points()) {
        max_extent = std::max(max_extent, std::max(std::fabs(p.x), std::max(std::fabs(p.y), std::fabs(p.z))));
    }
    const double qs = quantize_scale_for_extent(max_extent);

    IMesh mesh_a = IMesh::from_geometry(geo_a, 0, Mat4::identity(), qs);
    IMesh mesh_b = IMesh::from_geometry(geo_b, 1, Mat4::identity(), qs);

    if (static_cast<int>(mesh_a.tris.size() + mesh_b.tris.size()) > opts.triangle_budget) {
        error = BooleanErrorType::TriangleBudgetExceeded;
        error_msg = "Triangle budget exceeded";
        return {};
    }

    auto build_bvh_triangles = [](const IMesh& mesh) {
        std::vector<BVHTriangle> tris;
        for (int i = 0; i < static_cast<int>(mesh.tris.size()); ++i) {
            const auto& tri = mesh.tris[static_cast<size_t>(i)];
            BVHTriangle bt;
            bt.tri_index = i;
            bt.bounds.expand(mesh.verts[tri.v0].co);
            bt.bounds.expand(mesh.verts[tri.v1].co);
            bt.bounds.expand(mesh.verts[tri.v2].co);
            tris.push_back(bt);
        }
        return tris;
    };

    BVH bvh_a, bvh_b;
    bvh_a.build(build_bvh_triangles(mesh_a));
    bvh_b.build(build_bvh_triangles(mesh_b));
    auto candidate_pairs = bvh_a.find_overlaps(bvh_b);

    IMesh combined = mesh_a;
    combined.quantize_scale = qs;

    std::vector<int> b_vert_map(mesh_b.verts.size(), -1);
    for (size_t i = 0; i < mesh_b.verts.size(); ++i) {
        b_vert_map[i] = combined.find_or_insert_vert(
            mesh_b.verts[i].co, opts.weld_epsilon);
    }
    for (const auto& tri : mesh_b.tris) {
        IMeshTri t = tri;
        t.v0 = b_vert_map[static_cast<size_t>(tri.v0)];
        t.v1 = b_vert_map[static_cast<size_t>(tri.v1)];
        t.v2 = b_vert_map[static_cast<size_t>(tri.v2)];
        combined.tris.push_back(t);
    }

    struct SegmentIntersection {
        int tri_a_idx;
        int tri_b_idx;
        Vec3 overlap_p0, overlap_p1;
        Vec3 a_p0, a_p1;
        Vec3 b_p0, b_p1;
    };
    std::vector<SegmentIntersection> seg_intersections;

    struct CoplanarPair {
        int tri_a_idx;
        int tri_b_idx;
    };
    std::vector<CoplanarPair> coplanar_pairs;

    for (const auto& [a_idx, b_idx] : candidate_pairs) {
        const auto& tri_a = mesh_a.tris[static_cast<size_t>(a_idx)];
        const auto& tri_b = mesh_b.tris[static_cast<size_t>(b_idx)];

        Vec3 va[3] = {mesh_a.verts[tri_a.v0].co, mesh_a.verts[tri_a.v1].co, mesh_a.verts[tri_a.v2].co};
        Vec3 vb[3] = {mesh_b.verts[tri_b.v0].co, mesh_b.verts[tri_b.v1].co, mesh_b.verts[tri_b.v2].co};

        auto result = tri_tri_intersect(va, vb);
        if (result.kind == TriIntersectResult::Kind::Segment) {
            seg_intersections.push_back({a_idx, b_idx,
                result.p0, result.p1,
                result.a_p0, result.a_p1,
                result.b_p0, result.b_p1});
        } else if (result.kind == TriIntersectResult::Kind::CoplanarOverlap) {
            coplanar_pairs.push_back({a_idx, b_idx});
        }
    }

    std::unordered_map<int, std::vector<std::pair<Vec3, Vec3>>> segments_per_tri;
    for (const auto& inter : seg_intersections) {
        int combined_a = inter.tri_a_idx;
        int combined_b = inter.tri_b_idx + static_cast<int>(mesh_a.tris.size());
        segments_per_tri[combined_a].push_back({inter.a_p0, inter.a_p1});
        segments_per_tri[combined_b].push_back({inter.b_p0, inter.b_p1});
    }

    // Blender groups coplanar triangles into planar clusters and runs one CDT
    // arrangement over all triangle boundaries. Supplying the other triangle's
    // three edges as constraints gives each source triangle the same planar
    // subdivision without the invalid centroid fan used previously.
    for (const auto& cp : coplanar_pairs) {
        const int combined_a = cp.tri_a_idx;
        const int combined_b = cp.tri_b_idx + static_cast<int>(mesh_a.tris.size());
        const IMeshTri& tri_a = combined.tris[static_cast<size_t>(combined_a)];
        const IMeshTri& tri_b = combined.tris[static_cast<size_t>(combined_b)];
        const int av[3] = {tri_a.v0, tri_a.v1, tri_a.v2};
        const int bv[3] = {tri_b.v0, tri_b.v1, tri_b.v2};
        for (int edge = 0; edge < 3; ++edge) {
            segments_per_tri[combined_a].push_back({
                combined.verts[bv[edge]].co,
                combined.verts[bv[(edge + 1) % 3]].co});
            segments_per_tri[combined_b].push_back({
                combined.verts[av[edge]].co,
                combined.verts[av[(edge + 1) % 3]].co});
        }
    }

    for (const auto& inter : seg_intersections) {
        int combined_a = inter.tri_a_idx;
        int combined_b = inter.tri_b_idx + static_cast<int>(mesh_a.tris.size());
        int ip0 = combined.find_or_insert_vert(inter.overlap_p0, opts.weld_epsilon);
        int ip1 = combined.find_or_insert_vert(inter.overlap_p1, opts.weld_epsilon);
        if (ip0 != ip1) {
            IntersectEdgeRecord rec;
            rec.edge_key = edge_key(ip0, ip1);
            rec.tri_a = inter.tri_a_idx;
            rec.tri_b = inter.tri_b_idx + static_cast<int>(mesh_a.tris.size());
            combined.seam_edges.push_back(rec);
        }
    }

    std::vector<IMeshTri> new_tris;
    new_tris.reserve(combined.tris.size());

    for (int i = 0; i < static_cast<int>(combined.tris.size()); ++i) {
        auto seg_it = segments_per_tri.find(i);
        if (seg_it == segments_per_tri.end() || seg_it->second.empty()) {
            new_tris.push_back(combined.tris[static_cast<size_t>(i)]);
            continue;
        }

        const IMeshTri& orig_tri = combined.tris[static_cast<size_t>(i)];
        const int orig_source = orig_tri.source;
        const int orig_face = orig_tri.orig_face;

        const std::vector<std::array<int, 3>> sub_tris =
            split_triangle_by_constraints(combined, i, seg_it->second, opts.weld_epsilon);
        if (sub_tris.empty()) {
            new_tris.push_back(orig_tri);
            continue;
        }

        for (const auto& sub : sub_tris) {
            IMeshTri t;
            t.v0 = sub[0];
            t.v1 = sub[1];
            t.v2 = sub[2];
            t.source = orig_source;
            t.orig_face = orig_face;
            t.parent_tri = i;
            t.split_by_seam = true;
            new_tris.push_back(t);
        }
    }

    combined.tris = std::move(new_tris);
    conform_triangle_edges(combined, opts.weld_epsilon);
    return {combined};
}

} // anonymous namespace

BooleanResult execute_boolean(const data::PcgGeometry& a,
                              const data::PcgGeometry& b,
                              const BooleanOptions& opts)
{
    BooleanResult result;

    if (a.points().empty() || b.points().empty()) {
        result.error = BooleanErrorType::InvalidInput;
        result.message = "Empty input geometry";
        return result;
    }

    BooleanErrorType error;
    std::string error_msg;
    auto split = build_split_mesh(a, b, opts, error, error_msg);
    if (error != BooleanErrorType::Ok) {
        result.error = error;
        result.message = error_msg;
        return result;
    }

    result.geometry = classify_and_extract(split.combined, opts.operation);
    return result;
}

} // namespace pcg::internal::geometry
