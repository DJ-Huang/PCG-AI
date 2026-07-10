// B0b: Triangle intersection and coplanar partition tests.

#include "geometry/robust_predicates.hpp"
#include "geometry/coplanar_partition.hpp"

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

void test_tri_disjoint()
{
    Vec3 a[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    Vec3 b[3] = {{5, 5, 5}, {6, 5, 5}, {5, 6, 5}};
    auto r = tri_tri_intersect(a, b);
    if (r.kind != TriIntersectResult::Kind::Disjoint)
        fail("tri_intersect: disjoint triangles should be Disjoint");
}

void test_tri_segment_basic()
{
    // Triangle A in XY plane, Triangle B crossing it vertically
    Vec3 a[3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    Vec3 b[3] = {{1, -1, -1}, {1, -1, 1}, {1, 1, 0}};
    auto r = tri_tri_intersect(a, b);
    if (r.kind != TriIntersectResult::Kind::Segment)
        fail("tri_intersect: crossing triangles should produce Segment");
}

void test_tri_segment_endpoints()
{
    // Two unit triangles that share a common intersection line
    Vec3 a[3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    // B crosses A: the intersection should be along x=1, y from 0 to 1
    Vec3 b[3] = {{1, -1, -1}, {1, 3, -1}, {1, 1, 1}};
    auto r = tri_tri_intersect(a, b);
    if (r.kind != TriIntersectResult::Kind::Segment)
        fail("tri_intersect: crossing triangles should produce Segment (2)");
}

void test_tri_coplanar_overlap()
{
    Vec3 a[3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    Vec3 b[3] = {{1, 1, 0}, {3, 1, 0}, {1, 3, 0}};
    auto r = tri_tri_intersect(a, b);
    if (r.kind != TriIntersectResult::Kind::CoplanarOverlap)
        fail("tri_intersect: coplanar overlapping triangles should be CoplanarOverlap");
}

void test_tri_coplanar_disjoint()
{
    // Coplanar but non-overlapping
    Vec3 a[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    Vec3 b[3] = {{5, 5, 0}, {6, 5, 0}, {5, 6, 0}};
    auto r = tri_tri_intersect(a, b);
    if (r.kind != TriIntersectResult::Kind::Disjoint)
        fail("tri_intersect: coplanar non-overlapping triangles should be Disjoint");
}

void test_tri_touching_vertex()
{
    // Triangle B has a vertex exactly on the plane of A, but doesn't cross
    Vec3 a[3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    Vec3 b[3] = {{1, 0, 0}, {1, -1, 1}, {1, -1, -1}};
    auto r = tri_tri_intersect(a, b);
    // The vertex is on the plane but the triangle may or may not cross.
    // It should be either Disjoint or Segment (point contact → Disjoint).
    if (r.kind == TriIntersectResult::Kind::CoplanarOverlap)
        fail("tri_intersect: vertex touch should not be CoplanarOverlap");
}

void test_tri_degenerate()
{
    // Degenerate triangle (zero area)
    Vec3 a[3] = {{0, 0, 0}, {0, 0, 0}, {1, 0, 0}};
    Vec3 b[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    auto r = tri_tri_intersect(a, b);
    if (r.kind != TriIntersectResult::Kind::Disjoint)
        fail("tri_intersect: degenerate triangle should be Disjoint");
}

void test_coplanar_partition_basic()
{
    // Two coplanar triangles that partially overlap
    Vec3 tri_a[3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    Vec3 tri_b[3] = {{1, 1, 0}, {3, 1, 0}, {1, 3, 0}};

    auto subs = coplanar_partition(tri_a, tri_b);
    if (subs.empty())
        fail("coplanar_partition: should produce sub-triangles for overlapping case");

    // Each sub-triangle should have valid source
    for (const auto& s : subs) {
        if (s.source != 0 && s.source != 1)
            fail("coplanar_partition: sub-triangle source must be 0 or 1");
    }
}

void test_coplanar_partition_no_overlap()
{
    Vec3 tri_a[3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    Vec3 tri_b[3] = {{5, 5, 0}, {6, 5, 0}, {5, 6, 0}};

    auto subs = coplanar_partition(tri_a, tri_b);
    // Should return original triangles (no splitting)
    if (subs.size() != 2)
        fail("coplanar_partition: non-overlapping coplanar triangles should return 2 sub-tris");
}

void test_point_in_triangle_2d()
{
    Vec3 a{0, 0, 0}, b{2, 0, 0}, c{0, 2, 0};
    Vec3 inside{0.5, 0.5, 0};
    Vec3 outside{3, 3, 0};
    Vec3 on_edge{1, 0, 0};

    if (!point_in_triangle_2d(inside, a, b, c))
        fail("point_in_triangle_2d: interior point should be inside");
    if (point_in_triangle_2d(outside, a, b, c))
        fail("point_in_triangle_2d: exterior point should be outside");
    if (!point_in_triangle_2d(on_edge, a, b, c))
        fail("point_in_triangle_2d: edge point should be inside (inclusive)");
}

void test_best_projection_axis()
{
    if (best_projection_axis(Vec3{0, 0, 1}) != 2)
        fail("best_projection_axis: Z normal should project to XY (axis=2)");
    if (best_projection_axis(Vec3{1, 0, 0}) != 0)
        fail("best_projection_axis: X normal should project to YZ (axis=0)");
    if (best_projection_axis(Vec3{0, 1, 0}) != 1)
        fail("best_projection_axis: Y normal should project to XZ (axis=1)");
}

void test_t_junction()
{
    // T-junction: one triangle's vertex lies on the other's edge
    Vec3 a[3] = {{0, 0, 0}, {2, 0, 0}, {0, 2, 0}};
    Vec3 b[3] = {{1, 0, 0}, {1, 2, 0}, {0.5, 1, 1}};
    auto r = tri_tri_intersect(a, b);
    // Should detect intersection (Segment or at least not crash)
    if (r.kind == TriIntersectResult::Kind::Disjoint)
        fail("tri_intersect: T-junction should not be Disjoint");
}

} // namespace

int main()
{
    test_tri_disjoint();
    test_tri_segment_basic();
    test_tri_segment_endpoints();
    test_tri_coplanar_overlap();
    test_tri_coplanar_disjoint();
    test_tri_touching_vertex();
    test_tri_degenerate();
    test_coplanar_partition_basic();
    test_coplanar_partition_no_overlap();
    test_point_in_triangle_2d();
    test_best_projection_axis();
    test_t_junction();

    std::printf("test_tri_intersect: all tests passed\n");
    return 0;
}
