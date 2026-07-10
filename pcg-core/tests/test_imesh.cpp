// B1: IMesh tests.

#include "geometry/imesh.hpp"
#include "geometry/tri_split.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace pcg::internal::geometry;
using namespace pcg::internal::data;
using namespace pcg::internal::elements;

namespace {

[[noreturn]] void fail(const char* msg)
{
    std::printf("FAIL: %s\n", msg);
    std::exit(1);
}

void test_from_geometry_box()
{
    // Create a box mesh and convert to geometry
    PcgMeshData mesh = create_box_mesh(2.0, 2.0, 2.0);
    PcgGeometry geo = geometry_from_mesh(mesh);

    Vec3 min, max;
    geo.points(); // just access
    // Build IMesh
    double qs = quantize_scale_for_extent(2.0);
    IMesh im = IMesh::from_geometry(geo, 0, Mat4::identity(), qs);

    if (im.verts.empty())
        fail("IMesh: should have vertices from box");
    if (im.tris.empty())
        fail("IMesh: should have triangles from box");

    // Box has 6 quads → 12 triangles after fan triangulation
    if (im.tris.size() != 12)
        fail("IMesh: box should have 12 triangles");

    // Check quantization
    for (const auto& v : im.verts) {
        if (v.qx == 0 && v.qy == 0 && v.qz == 0)
            fail("IMesh: vertex should have non-zero quantized coords (unless at origin)");
    }

    // Check AABB
    Vec3 aabb_min, aabb_max;
    im.compute_aabb(aabb_min, aabb_max);
    if (aabb_min.x > -0.5 || aabb_max.x < 0.5)
        fail("IMesh: box AABB should span [-1, 1]");
}

void test_from_geometry_weld()
{
    // Two adjacent quads sharing an edge → should weld shared vertices
    PcgGeometry geo;
    geo.points_mut() = {
        {0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},  // quad 1
        {2, 0, 0}, {2, 1, 0},                          // extra verts for quad 2
    };
    geo.faces_mut() = {
        {0, 1, 2, 3},  // quad 1
        {1, 4, 5, 2},  // quad 2 (shares edge 1-2)
    };

    double qs = quantize_scale_for_extent(2.0);
    IMesh im = IMesh::from_geometry(geo, 0, Mat4::identity(), qs);

    // 6 unique vertices (not 8) because vertices 1 and 2 are shared
    if (im.verts.size() != 6)
        fail("IMesh: shared vertices should be welded");

    // 4 triangles (2 per quad)
    if (im.tris.size() != 4)
        fail("IMesh: two quads should give 4 triangles");
}

void test_quantize_scale()
{
    double s1 = quantize_scale_for_extent(1.0);
    double s2 = quantize_scale_for_extent(100.0);
    double s3 = quantize_scale_for_extent(0.001);

    // Larger extent → smaller scale
    if (s2 >= s1)
        fail("quantize_scale: larger extent should give smaller scale");

    // Very small extent → large scale
    if (s3 <= s1)
        fail("quantize_scale: smaller extent should give larger scale");

    // Quantized values should fit in int32
    int64_t q = quantize_coord(1.0, s1);
    if (q > INT32_MAX || q < INT32_MIN)
        fail("quantize: value at max extent should fit int32");
}

void test_transform_baking()
{
    PcgGeometry geo;
    geo.points_mut() = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    geo.faces_mut() = {{0, 1, 2}};

    // Apply translation
    Mat4 xform = Mat4::identity();
    xform.m[0][3] = 10.0; // translate X by 10
    xform.m[1][3] = 20.0; // translate Y by 20
    xform.m[2][3] = 30.0; // translate Z by 30

    double qs = quantize_scale_for_extent(1.0);
    IMesh im = IMesh::from_geometry(geo, 0, xform, qs);

    // Check that transform was applied
    bool found_translated = false;
    for (const auto& v : im.verts) {
        if (std::fabs(v.co.x - 10.0) < 1e-6 && std::fabs(v.co.y - 20.0) < 1e-6 && std::fabs(v.co.z - 30.0) < 1e-6)
            found_translated = true;
    }
    if (!found_translated)
        fail("IMesh: transform should translate origin vertex to (10, 20, 30)");
}

void test_find_or_insert_vert()
{
    IMesh mesh;
    int v0 = mesh.find_or_insert_vert({1, 2, 3}, 1e-4);
    int v1 = mesh.find_or_insert_vert({1, 2, 3}, 1e-4); // same position → weld
    int v2 = mesh.find_or_insert_vert({4, 5, 6}, 1e-4);

    if (v0 != v1)
        fail("IMesh: identical positions should weld to same index");
    if (v0 == v2)
        fail("IMesh: different positions should get different indices");
    if (mesh.verts.size() != 2)
        fail("IMesh: should have 2 unique vertices after 3 inserts");
}

void test_barycentric_basic()
{
    Vec3 a{0, 0, 0}, b{1, 0, 0}, c{0, 1, 0};
    double u, v, w;

    // Center of triangle
    if (!barycentric({1.0/3, 1.0/3, 0}, a, b, c, u, v, w))
        fail("barycentric: centroid should be inside");
    if (std::fabs(u - 1.0/3) > 1e-10 || std::fabs(v - 1.0/3) > 1e-10 || std::fabs(w - 1.0/3) > 1e-10)
        fail("barycentric: centroid coords should be (1/3, 1/3, 1/3)");

    // Outside point
    if (barycentric({2, 2, 0}, a, b, c, u, v, w))
        fail("barycentric: point outside should return false");
}

} // namespace

int main()
{
    test_from_geometry_box();
    test_from_geometry_weld();
    test_quantize_scale();
    test_transform_baking();
    test_find_or_insert_vert();
    test_barycentric_basic();

    std::printf("test_imesh: all tests passed\n");
    return 0;
}
