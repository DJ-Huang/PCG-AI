// B1: BVH tests.

#include "geometry/bvh.hpp"

#include <cstdio>
#include <cstdlib>

using namespace pcg::internal::geometry;

namespace {

[[noreturn]] void fail(const char* msg)
{
    std::printf("FAIL: %s\n", msg);
    std::exit(1);
}

void test_aabb_basic()
{
    AABB a;
    a.expand({0, 0, 0});
    a.expand({2, 2, 2});
    if (a.min.x != 0 || a.min.y != 0 || a.min.z != 0)
        fail("AABB: min should be (0,0,0)");
    if (a.max.x != 2 || a.max.y != 2 || a.max.z != 2)
        fail("AABB: max should be (2,2,2)");

    AABB b;
    b.expand({1, 1, 1});
    b.expand({3, 3, 3});
    if (!a.overlaps(b))
        fail("AABB: overlapping boxes should overlap");

    AABB c;
    c.expand({5, 5, 5});
    c.expand({6, 6, 6});
    if (a.overlaps(c))
        fail("AABB: non-overlapping boxes should not overlap");
}

void test_bvh_build_and_query()
{
    std::vector<BVHTriangle> tris;
    for (int i = 0; i < 10; ++i) {
        BVHTriangle t;
        t.tri_index = i;
        t.bounds.expand({static_cast<double>(i), 0, 0});
        t.bounds.expand({static_cast<double>(i) + 1, 1, 0});
        t.bounds.expand({static_cast<double>(i), 1, 1});
        tris.push_back(t);
    }

    BVH bvh;
    bvh.build(tris);
    if (bvh.empty())
        fail("BVH: should not be empty after build");
    if (bvh.node_count() == 0)
        fail("BVH: should have nodes");

    // Query for overlapping with box [3, 5] in X
    AABB box;
    box.expand({3, -1, -1});
    box.expand({5, 2, 2});
    auto hits = bvh.query_box(box);
    if (hits.empty())
        fail("BVH: query should find overlapping triangles");
}

void test_bvh_cross_query()
{
    // Two sets of triangles that partially overlap
    std::vector<BVHTriangle> tris_a, tris_b;

    for (int i = 0; i < 5; ++i) {
        BVHTriangle t;
        t.tri_index = i;
        t.bounds.expand({static_cast<double>(i), 0, 0});
        t.bounds.expand({static_cast<double>(i) + 1, 1, 1});
        tris_a.push_back(t);
    }

    for (int i = 3; i < 8; ++i) {
        BVHTriangle t;
        t.tri_index = i;
        t.bounds.expand({static_cast<double>(i), 0, 0});
        t.bounds.expand({static_cast<double>(i) + 1, 1, 1});
        tris_b.push_back(t);
    }

    BVH a, b;
    a.build(tris_a);
    b.build(tris_b);

    auto pairs = a.find_overlaps(b);
    if (pairs.empty())
        fail("BVH: cross query should find overlapping pairs");

    // All pairs should have overlapping X ranges
    for (const auto& [ia, ib] : pairs) {
        // tris_a[ia] has X range [ia, ia+1], tris_b[ib] has X range [ib, ib+1]
        // They overlap if |ia - ib| <= 1
        if (std::abs(ia - ib) > 1)
            fail("BVH: cross query returned false positive");
    }
}

void test_bvh_disjoint()
{
    std::vector<BVHTriangle> tris_a, tris_b;

    BVHTriangle ta;
    ta.tri_index = 0;
    ta.bounds.expand({0, 0, 0});
    ta.bounds.expand({1, 1, 1});
    tris_a.push_back(ta);

    BVHTriangle tb;
    tb.tri_index = 0;
    tb.bounds.expand({10, 10, 10});
    tb.bounds.expand({11, 11, 11});
    tris_b.push_back(tb);

    BVH a, b;
    a.build(tris_a);
    b.build(tris_b);

    auto pairs = a.find_overlaps(b);
    if (!pairs.empty())
        fail("BVH: disjoint BVHs should have no overlapping pairs");
}

void test_bvh_empty()
{
    BVH bvh;
    if (!bvh.empty())
        fail("BVH: default constructed should be empty");

    auto pairs = bvh.find_overlaps(bvh);
    if (!pairs.empty())
        fail("BVH: empty BVH cross query should return empty");

    AABB box;
    box.expand({0, 0, 0});
    box.expand({1, 1, 1});
    auto hits = bvh.query_box(box);
    if (!hits.empty())
        fail("BVH: empty BVH box query should return empty");
}

} // namespace

int main()
{
    test_aabb_basic();
    test_bvh_build_and_query();
    test_bvh_cross_query();
    test_bvh_disjoint();
    test_bvh_empty();

    std::printf("test_bvh: all tests passed\n");
    return 0;
}
