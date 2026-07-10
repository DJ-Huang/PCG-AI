// B0a: Robust predicates regression tests.
// Verifies Shewchuk adaptive orientation predicates against known cases.

#include "geometry/robust_predicates.hpp"
#include "geometry/wide_int.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace pcg::internal::geometry;

namespace {

[[noreturn]] void fail(const char* msg)
{
    std::printf("FAIL: %s\n", msg);
    std::exit(1);
}

void test_orient3d_basic()
{
    // Simple tetrahedron with positive volume
    Vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0}, d{0, 0, 1};
    if (orient3d(a, b, c, d) != OrientSign::Positive)
        fail("orient3d: positive volume tetrahedron should be Positive");

    // Swap d to the other side
    if (orient3d(a, b, c, Vec3{0, 0, -1}) != OrientSign::Negative)
        fail("orient3d: negative volume tetrahedron should be Negative");

    // Coplanar points → Zero
    if (orient3d(a, b, c, Vec3{0.5, 0.5, 0}) != OrientSign::Zero)
        fail("orient3d: coplanar points should be Zero");
}

void test_orient3d_coplanar_exact()
{
    // Points that are coplanar but would fool naive float arithmetic
    // Using coordinates that produce a zero determinant exactly
    Vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0}, d{1, 1, 0};
    if (orient3d(a, b, c, d) != OrientSign::Zero)
        fail("orient3d: exactly coplanar quad should be Zero");
}

void test_orient3d_near_coplanar()
{
    // Near-degenerate: slightly above the plane
    Vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};
    Vec3 d_above{0.5, 0.5, 1e-15};
    OrientSign s = orient3d(a, b, c, d_above);
    if (s != OrientSign::Positive)
        fail("orient3d: slightly above plane should be Positive (adaptive needed)");
}

void test_orient2d_basic()
{
    // CCW triangle
    Vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};
    if (orient2d(a, b, c) != OrientSign::Positive)
        fail("orient2d: CCW triangle should be Positive");

    // CW triangle
    if (orient2d(a, c, b) != OrientSign::Negative)
        fail("orient2d: CW triangle should be Negative");

    // Collinear
    if (orient2d(a, b, Vec3{2, 0, 0}) != OrientSign::Zero)
        fail("orient2d: collinear points should be Zero");
}

void test_orient3d_random_agreement()
{
    // 1000 random tetrahedra: fast and adaptive must agree on sign
    // (except when the fast version returns Zero and adaptive is also Zero,
    //  or when the result is truly degenerate)
    uint32_t rng = 12345;
    int disagreements = 0;
    for (int i = 0; i < 1000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        Vec3 a{(double)(rng % 1000) / 100.0 - 5.0,
               (double)((rng >> 8) % 1000) / 100.0 - 5.0,
               (double)((rng >> 16) % 1000) / 100.0 - 5.0};
        rng = rng * 1664525u + 1013904223u;
        Vec3 b{(double)(rng % 1000) / 100.0 - 5.0,
               (double)((rng >> 8) % 1000) / 100.0 - 5.0,
               (double)((rng >> 16) % 1000) / 100.0 - 5.0};
        rng = rng * 1664525u + 1013904223u;
        Vec3 c{(double)(rng % 1000) / 100.0 - 5.0,
               (double)((rng >> 8) % 1000) / 100.0 - 5.0,
               (double)((rng >> 16) % 1000) / 100.0 - 5.0};
        rng = rng * 1664525u + 1013904223u;
        Vec3 d{(double)(rng % 1000) / 100.0 - 5.0,
               (double)((rng >> 8) % 1000) / 100.0 - 5.0,
               (double)((rng >> 16) % 1000) / 100.0 - 5.0};

        OrientSign fast = orient3d_fast(a, b, c, d);
        OrientSign exact = orient3d(a, b, c, d);

        // They should agree on sign (fast may return Zero when exact is nonzero for near-degenerate)
        if (fast != exact) {
            // Allow fast=Zero when exact is nonzero (near-degenerate case)
            if (fast != OrientSign::Zero)
                ++disagreements;
        }
    }
    if (disagreements > 0)
        fail("orient3d: fast and adaptive disagree on sign for non-degenerate cases");
}

void test_orient3d_perturbation()
{
    // Perturbation test: start with coplanar, then perturb by decreasing amounts.
    // The sign must remain consistent with the perturbation direction.
    Vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};
    for (int k = 1; k <= 15; ++k) {
        const double eps = std::pow(10.0, -k);
        Vec3 d_pos{0.5, 0.5, eps};
        Vec3 d_neg{0.5, 0.5, -eps};
        if (orient3d(a, b, c, d_pos) != OrientSign::Positive)
            fail("orient3d: positive perturbation should be Positive");
        if (orient3d(a, b, c, d_neg) != OrientSign::Negative)
            fail("orient3d: negative perturbation should be Negative");
    }
}

void test_tri_tri_disjoint()
{
    Vec3 a[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    Vec3 b[3] = {{5, 5, 5}, {6, 5, 5}, {5, 6, 5}};
    auto r = tri_tri_intersect(a, b);
    if (r.kind != TriIntersectResult::Kind::Disjoint)
        fail("tri_tri_intersect: disjoint triangles should be Disjoint");
}

void test_tri_tri_segment()
{
    // Two triangles crossing each other
    Vec3 a[3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    Vec3 b[3] = {{1, -1, -1}, {1, -1, 1}, {1, 1, 0}};
    auto r = tri_tri_intersect(a, b);
    if (r.kind != TriIntersectResult::Kind::Segment)
        fail("tri_tri_intersect: crossing triangles should produce Segment");
}

void test_tri_tri_coplanar()
{
    Vec3 a[3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    Vec3 b[3] = {{1, 1, 0}, {3, 1, 0}, {1, 3, 0}};
    auto r = tri_tri_intersect(a, b);
    if (r.kind != TriIntersectResult::Kind::CoplanarOverlap)
        fail("tri_tri_intersect: coplanar overlapping triangles should be CoplanarOverlap");
}

void test_orient3d_volume_invariant()
{
    // The sign of orient3d should match the sign of the tetrahedron volume.
    // Volume = det / 6
    Vec3 a{1, 2, 3}, b{4, 1, 0}, c{2, 5, 1}, d{0, 0, 2};

    // Compute volume via cross product
    Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3 ad{d.x - a.x, d.y - a.y, d.z - a.z};
    Vec3 cross{ab.y * ac.z - ab.z * ac.y,
               ab.z * ac.x - ab.x * ac.z,
               ab.x * ac.y - ab.y * ac.x};
    double vol6 = cross.x * ad.x + cross.y * ad.y + cross.z * ad.z;

    OrientSign s = orient3d(a, b, c, d);
    if (vol6 > 0 && s != OrientSign::Positive)
        fail("orient3d: sign disagrees with positive volume");
    if (vol6 < 0 && s != OrientSign::Negative)
        fail("orient3d: sign disagrees with negative volume");
}

void test_wide_int_basic()
{
    // 2^32 * 2^32 = 2^64, which does NOT fit in int64 (max 2^63-1)
    UInt128 r = UInt128::mul(1ULL << 32, 1ULL << 32);
    if (r.fits_int64())
        fail("wide_int: 2^64 should not fit int64");

    // 3 * 5 = 15, fits fine
    UInt128 r2 = UInt128::mul(3, 5);
    if (!r2.fits_int64())
        fail("wide_int: 15 should fit int64");

    // INT64_MAX * 2 overflows
    UInt128 r3 = UInt128::mul(static_cast<uint64_t>(INT64_MAX), 2ULL);
    if (r3.fits_int64())
        fail("wide_int: INT64_MAX * 2 should not fit int64");
}

} // namespace

int main()
{
    test_orient3d_basic();
    test_orient3d_coplanar_exact();
    test_orient3d_near_coplanar();
    test_orient2d_basic();
    test_orient3d_random_agreement();
    test_orient3d_perturbation();
    test_tri_tri_disjoint();
    test_tri_tri_segment();
    test_tri_tri_coplanar();
    test_orient3d_volume_invariant();
    test_wide_int_basic();

    std::printf("test_robust_predicates: all tests passed\n");
    return 0;
}
