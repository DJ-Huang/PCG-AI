#pragma once

// Shewchuk adaptive robust predicates for exact geometric computation.
// Based on Jonathan Richard Shewchuk, "Adaptive Precision Floating-Point
// Arithmetic and Fast Robust Geometric Predicates" (1997).
//
// Public domain predicates.c adapted to C++ with namespace encapsulation.
// Used by boolean mesh operations for reliable orientation tests.

#include "geometry/bmesh.hpp" // Vec3

#include <array>

namespace pcg::internal::geometry {

/// Result of an orientation test.
enum class OrientSign {
    Negative = -1,
    Zero = 0,
    Positive = 1,
    Uncertain = 2, // Used internally when the fast stage is inconclusive
};

/// 2D orientation test (sign of the signed area of triangle abc).
/// Projects the 3D points onto the best axis-aligned plane before testing.
/// @return Positive if c is to the left of a→b (CCW), Negative if CW, Zero if collinear.
OrientSign orient2d(const Vec3& a, const Vec3& b, const Vec3& c);

/// 3D orientation test (sign of the signed volume of tetrahedron abcd).
/// @return Positive if d is above the plane abc (right-hand rule), Negative if below, Zero if coplanar.
OrientSign orient3d(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d);

/// Fast (non-adaptive) 3D orientation. Used for broad-phase rejection.
/// Returns the sign of the determinant using only double arithmetic.
/// May return Uncertain when the result is near zero.
OrientSign orient3d_fast(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d);

/// Result of a triangle-triangle intersection test.
struct TriIntersectResult {
    enum class Kind {
        Disjoint,        ///< No intersection
        Segment,         ///< Intersect along a line segment [p0, p1]
        CoplanarOverlap, ///< Triangles are coplanar and overlap
    };

    Kind kind = Kind::Disjoint;
    Vec3 p0; ///< Segment start point (overlap, when Kind::Segment)
    Vec3 p1; ///< Segment end point (overlap, when Kind::Segment)
    Vec3 a_p0; ///< Full crossing segment for triangle A (edge-to-edge)
    Vec3 a_p1; ///< Full crossing segment for triangle A (edge-to-edge)
    Vec3 b_p0; ///< Full crossing segment for triangle B (edge-to-edge)
    Vec3 b_p1; ///< Full crossing segment for triangle B (edge-to-edge)
};

/// Robust triangle-triangle intersection test using adaptive predicates.
/// @param tri_a Triangle A (3 vertices)
/// @param tri_b Triangle B (3 vertices)
/// @return Result describing the intersection type and segment endpoints.
TriIntersectResult tri_tri_intersect(const Vec3 tri_a[3], const Vec3 tri_b[3]);

// ── Internal expansion arithmetic (exposed for testing) ──

namespace detail {

/// Error-free transformation: split a double into two halves.
void split(double a, double& hi, double& lo);

/// Error-free transformation: two-sum (a + b = sum + err).
double two_sum(double a, double b, double& err);

/// Error-free transformation: two-product (a * b = prod + err).
double two_product(double a, double, double& err);

/// Grow an expansion by one term (expansion_sum_1).
void grow_expansion(const double* e, int elen, double b, double* h);

/// Expansion sum: h = e + f.
int expansion_sum(const double* e, int elen, const double* f, int flen, double* h);

/// Scale an expansion by a scalar.
int scale_expansion(const double* e, int elen, double b, double* h);

/// Estimate the sign of an expansion (returns last nonzero term's sign).
int expansion_sign(const double* e, int elen);

} // namespace detail

} // namespace pcg::internal::geometry
