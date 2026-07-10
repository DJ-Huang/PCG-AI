// Coplanar triangle partitioning implementation.
// Splits two coplanar overlapping triangles into non-overlapping sub-triangles,
// each tagged as inside or outside the other triangle.

#include "geometry/coplanar_partition.hpp"
#include "geometry/robust_predicates.hpp"

#include <algorithm>
#include <cmath>

namespace pcg::internal::geometry {

namespace {

struct Vec2 {
    double x, y;
};

Vec2 project(const Vec3& v, int axis)
{
    if (axis == 0) return {v.y, v.z};
    if (axis == 1) return {v.x, v.z};
    return {v.x, v.y};
}

double orient2d_raw(const Vec2& a, const Vec2& b, const Vec2& c)
{
    return (a.x - c.x) * (b.y - c.y) - (a.y - c.y) * (b.x - c.x);
}

bool point_in_tri(const Vec2& p, const Vec2& a, const Vec2& b, const Vec2& c)
{
    const double d1 = orient2d_raw(p, a, b);
    const double d2 = orient2d_raw(p, b, c);
    const double d3 = orient2d_raw(p, c, a);
    const bool has_neg = (d1 < 0) || (d2 < 0) || (d3 < 0);
    const bool has_pos = (d1 > 0) || (d2 > 0) || (d3 > 0);
    return !(has_neg && has_pos);
}

bool segment_segment_intersect_2d(const Vec2& p1, const Vec2& p2,
                                   const Vec2& p3, const Vec2& p4,
                                   Vec2& out)
{
    const double d1 = orient2d_raw(p3, p4, p1);
    const double d2 = orient2d_raw(p3, p4, p2);
    const double d3 = orient2d_raw(p1, p2, p3);
    const double d4 = orient2d_raw(p1, p2, p4);

    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) &&
        ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
        const double t = d1 / (d1 - d2);
        out.x = p1.x + t * (p2.x - p1.x);
        out.y = p1.y + t * (p2.y - p1.y);
        return true;
    }
    return false;
}

/// Collect all points for a triangle's partition: original vertices,
/// edge-edge intersection points, and other triangle's vertices inside.
/// Returns the 3D points and their 2D projections.
struct PartitionPoints {
    std::vector<Vec3> pts3d;
    std::vector<Vec2> pts2d;
};

PartitionPoints collect_partition_points(
    const Vec3 tri[3], const Vec3 other_tri[3],
    int axis,
    const std::vector<std::pair<Vec3, Vec2>>& intersections)
{
    PartitionPoints pp;

    // Original vertices
    for (int i = 0; i < 3; ++i) {
        pp.pts3d.push_back(tri[i]);
        pp.pts2d.push_back(project(tri[i], axis));
    }

    // Edge-edge intersection points (already on this triangle's edges)
    for (const auto& [pos3d, pos2d] : intersections) {
        pp.pts3d.push_back(pos3d);
        pp.pts2d.push_back(pos2d);
    }

    // Other triangle's vertices inside this triangle
    Vec2 a2[3] = {project(tri[0], axis), project(tri[1], axis), project(tri[2], axis)};
    for (int i = 0; i < 3; ++i) {
        Vec2 p2d = project(other_tri[i], axis);
        if (point_in_tri(p2d, a2[0], a2[1], a2[2])) {
            // Check it's not already added (duplicate with intersection point)
            bool dup = false;
            for (size_t j = 0; j < pp.pts2d.size(); ++j) {
                double dx = pp.pts2d[j].x - p2d.x;
                double dy = pp.pts2d[j].y - p2d.y;
                if (dx * dx + dy * dy < 1e-16) { dup = true; break; }
            }
            if (!dup) {
                pp.pts3d.push_back(other_tri[i]);
                pp.pts2d.push_back(p2d);
            }
        }
    }

    return pp;
}

/// Sort points angularly around centroid and fan-triangulate.
/// Each sub-triangle is tested against the other triangle to determine inside/outside.
std::vector<CoplanarSubTri> fan_triangulate_tagged(
    const PartitionPoints& pp,
    const Vec2 other_tri2d[3],
    int source,
    const Vec3 tri[3])
{
    if (pp.pts3d.size() <= 3) {
        // No splitting needed — single triangle
        // Test if inside or outside
        Vec2 centroid2d = {
            (pp.pts2d[0].x + pp.pts2d[1].x + pp.pts2d[2].x) / 3.0,
            (pp.pts2d[0].y + pp.pts2d[1].y + pp.pts2d[2].y) / 3.0
        };
        bool inside = point_in_tri(centroid2d, other_tri2d[0], other_tri2d[1], other_tri2d[2]);

        // Determine winding: use original triangle winding
        double orig_orient = orient2d_raw(pp.pts2d[0], pp.pts2d[1], pp.pts2d[2]);
        if (orig_orient >= 0) {
            return {{pp.pts3d[0], pp.pts3d[1], pp.pts3d[2], source, inside}};
        } else {
            return {{pp.pts3d[0], pp.pts3d[2], pp.pts3d[1], source, inside}};
        }
    }

    // Compute centroid
    double cx = 0, cy = 0;
    for (const auto& p : pp.pts2d) { cx += p.x; cy += p.y; }
    cx /= pp.pts2d.size();
    cy /= pp.pts2d.size();

    // Sort by angle around centroid
    std::vector<int> order(pp.pts2d.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);

    std::sort(order.begin(), order.end(), [&](int i, int j) {
        double ai = std::atan2(pp.pts2d[i].y - cy, pp.pts2d[i].x - cx);
        double aj = std::atan2(pp.pts2d[j].y - cy, pp.pts2d[j].x - cx);
        return ai < aj;
    });

    // Determine winding from first 3 points (original triangle)
    double orig_orient = orient2d_raw(pp.pts2d[0], pp.pts2d[1], pp.pts2d[2]);

    // Fan triangulate
    std::vector<CoplanarSubTri> result;
    for (size_t i = 1; i + 1 < order.size(); ++i) {
        Vec3 v0 = pp.pts3d[order[0]];
        Vec3 v1 = pp.pts3d[order[i]];
        Vec3 v2 = pp.pts3d[order[i + 1]];

        // Test sub-triangle centroid against other triangle
        Vec2 c2d = {
            (pp.pts2d[order[0]].x + pp.pts2d[order[i]].x + pp.pts2d[order[i + 1]].x) / 3.0,
            (pp.pts2d[order[0]].y + pp.pts2d[order[i]].y + pp.pts2d[order[i + 1]].y) / 3.0
        };
        bool inside = point_in_tri(c2d, other_tri2d[0], other_tri2d[1], other_tri2d[2]);

        // Ensure winding matches original
        double sub_orient = orient2d_raw(pp.pts2d[order[0]], pp.pts2d[order[i]], pp.pts2d[order[i + 1]]);
        if ((orig_orient >= 0 && sub_orient < 0) || (orig_orient < 0 && sub_orient >= 0)) {
            std::swap(v1, v2);
        }

        result.push_back({v0, v1, v2, source, inside});
    }

    return result;
}

} // anonymous namespace

int best_projection_axis(const Vec3& normal)
{
    const double ax = std::fabs(normal.x);
    const double ay = std::fabs(normal.y);
    const double az = std::fabs(normal.z);
    if (ax >= ay && ax >= az) return 0;
    if (ay >= az) return 1;
    return 2;
}

bool point_in_triangle_2d(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
    Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
    Vec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3 normal{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
    int axis = best_projection_axis(normal);
    Vec2 p2 = project(p, axis);
    Vec2 a2 = project(a, axis);
    Vec2 b2 = project(b, axis);
    Vec2 c2 = project(c, axis);
    return point_in_tri(p2, a2, b2, c2);
}

std::vector<CoplanarSubTri> coplanar_partition(const Vec3 tri_a[3], const Vec3 tri_b[3])
{
    // Compute shared plane normal
    Vec3 e1a{tri_a[1].x - tri_a[0].x, tri_a[1].y - tri_a[0].y, tri_a[1].z - tri_a[0].z};
    Vec3 e2a{tri_a[2].x - tri_a[0].x, tri_a[2].y - tri_a[0].y, tri_a[2].z - tri_a[0].z};
    Vec3 normal{e1a.y * e2a.z - e1a.z * e2a.y,
                e1a.z * e2a.x - e1a.x * e2a.z,
                e1a.x * e2a.y - e1a.y * e2a.x};

    const int axis = best_projection_axis(normal);

    // Project both triangles to 2D
    Vec2 a2[3] = {project(tri_a[0], axis), project(tri_a[1], axis), project(tri_a[2], axis)};
    Vec2 b2[3] = {project(tri_b[0], axis), project(tri_b[1], axis), project(tri_b[2], axis)};

    // Find all edge-edge intersection points
    std::vector<std::pair<Vec3, Vec2>> intersections;

    for (int ea = 0; ea < 3; ++ea) {
        const Vec3& a_p1 = tri_a[ea];
        const Vec3& a_p2 = tri_a[(ea + 1) % 3];

        for (int eb = 0; eb < 3; ++eb) {
            const Vec3& b_p1 = tri_b[eb];
            const Vec3& b_p2 = tri_b[(eb + 1) % 3];

            Vec2 hit2d;
            if (segment_segment_intersect_2d(a2[ea], a2[(ea + 1) % 3],
                                             b2[eb], b2[(eb + 1) % 3], hit2d)) {
                // Compute 3D intersection from edge A's parametric form
                double dx = a2[(ea + 1) % 3].x - a2[ea].x;
                double dy = a2[(ea + 1) % 3].y - a2[ea].y;
                double denom = (std::fabs(dx) > std::fabs(dy)) ? dx : dy;
                if (std::fabs(denom) < 1e-20) continue;
                double ta = (std::fabs(dx) > std::fabs(dy))
                    ? (hit2d.x - a2[ea].x) / dx
                    : (hit2d.y - a2[ea].y) / dy;

                Vec3 hit3d{
                    a_p1.x + ta * (a_p2.x - a_p1.x),
                    a_p1.y + ta * (a_p2.y - a_p1.y),
                    a_p1.z + ta * (a_p2.z - a_p1.z),
                };

                intersections.push_back({hit3d, hit2d});
            }
        }
    }

    // Partition triangle A: collect points and fan-triangulate
    PartitionPoints pp_a = collect_partition_points(tri_a, tri_b, axis, intersections);
    auto sub_a = fan_triangulate_tagged(pp_a, b2, 0, tri_a);

    // Partition triangle B: collect points and fan-triangulate
    PartitionPoints pp_b = collect_partition_points(tri_b, tri_a, axis, intersections);
    auto sub_b = fan_triangulate_tagged(pp_b, a2, 1, tri_b);

    // Combine results
    std::vector<CoplanarSubTri> result;
    result.reserve(sub_a.size() + sub_b.size());
    for (auto& s : sub_a) result.push_back(std::move(s));
    for (auto& s : sub_b) result.push_back(std::move(s));

    return result;
}

} // namespace pcg::internal::geometry
