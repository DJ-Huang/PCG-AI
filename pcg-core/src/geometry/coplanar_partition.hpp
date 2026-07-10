#pragma once

// Coplanar triangle partitioning for boolean mesh operations.
// When two triangles are coplanar and overlap, we need to split them into
// non-overlapping sub-triangles. This module implements a 2D arrangement
// for coplanar triangle pairs.

#include "geometry/bmesh.hpp"

#include <vector>

namespace pcg::internal::geometry {

/// A sub-triangle resulting from coplanar partition.
/// Vertices are in 3D (on the shared plane).
struct CoplanarSubTri {
    Vec3 v0, v1, v2;
    int source;   ///< 0 = from triangle A, 1 = from triangle B
    bool inside;  ///< true = inside the other triangle, false = outside
};

/// Partition two coplanar overlapping triangles into sub-triangles.
/// @param tri_a Triangle A (3 vertices, coplanar with B)
/// @param tri_b Triangle B (3 vertices, coplanar with A)
/// @return Vector of sub-triangles covering the union of A and B
std::vector<CoplanarSubTri> coplanar_partition(const Vec3 tri_a[3], const Vec3 tri_b[3]);

/// Check if a point is inside a 2D triangle (projected onto best plane).
/// @return true if the point is inside or on the boundary of the triangle
bool point_in_triangle_2d(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c);

/// Find the best 2D projection plane for a set of coplanar points.
/// Returns 0=X (YZ plane), 1=Y (XZ plane), 2=Z (XY plane)
int best_projection_axis(const Vec3& normal);

} // namespace pcg::internal::geometry
