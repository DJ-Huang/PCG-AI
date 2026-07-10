#pragma once

// Triangle splitting along intersection segments.
// Given a triangle and an intersection segment, split the triangle into
// sub-triangles, welding intersection points into the vertex array.

#include "geometry/bmesh.hpp"
#include "geometry/imesh.hpp"
#include "geometry/robust_predicates.hpp"

#include <utility>
#include <vector>

namespace pcg::internal::geometry {

/// Result of splitting a triangle by a segment.
struct TriSplitResult {
    std::vector<int> sub_tri_verts; // groups of 3 vertex indices
    std::vector<int> new_vert_indices; // indices of newly inserted vertices
};

/// Split a triangle by an intersection segment.
/// The segment endpoints are inserted into the IMesh vertex array (welded).
/// @param mesh The IMesh to add new vertices to
/// @param tri_index Index of the triangle to split
/// @param seg_p0, seg_p1 The intersection segment endpoints
/// @param weld_eps Welding epsilon
/// @return List of sub-triangle vertex index triples
std::vector<std::array<int, 3>> split_triangle_by_segment(
    IMesh& mesh, int tri_index,
    const Vec3& seg_p0, const Vec3& seg_p1,
    double weld_eps);

/// Subdivide one triangle by all constraints in a single constrained
/// Delaunay triangulation, matching Blender's per-triangle CDT stage.
std::vector<std::array<int, 3>> split_triangle_by_constraints(
    IMesh& mesh, int tri_index,
    const std::vector<std::pair<Vec3, Vec3>>& segments,
    double weld_eps);

/// Check if a point lies on or inside a triangle (3D, using barycentric).
bool point_in_triangle_3d(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c);

/// Compute barycentric coordinates of point p w.r.t. triangle abc.
/// Returns true if p is inside or on the boundary of the triangle.
bool barycentric(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c,
                 double& u, double& v, double& w);

} // namespace pcg::internal::geometry
