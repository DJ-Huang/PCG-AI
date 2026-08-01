// B2: Arrangement + CSG tests.
// Tests winding number computation and CSG classification for boolean operations.

#include "geometry/arrangement.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"

#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

using namespace pcg::internal::geometry;
using namespace pcg::internal::data;
using namespace pcg::internal::elements;

namespace {

[[noreturn]] void fail(const char* msg)
{
    std::printf("FAIL: %s\n", msg);
    std::exit(1);
}

/// Signed volume of a triangulated mesh.
double signed_volume(const PcgMeshData& mesh)
{
    double vol = 0.0;
    const auto& v = mesh.vertices();
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const auto& a = v[static_cast<size_t>(mesh.triangles()[i])];
        const auto& b = v[static_cast<size_t>(mesh.triangles()[i + 1])];
        const auto& c = v[static_cast<size_t>(mesh.triangles()[i + 2])];
        vol += (a.x * (b.y * c.z - b.z * c.y)
              + a.y * (b.z * c.x - b.x * c.z)
              + a.z * (b.x * c.y - b.y * c.x)) / 6.0;
    }
    return vol;
}

/// Manifold winding check: count edges where both triangles have same winding direction.
int manifold_winding_bad_count(const PcgMeshData& mesh)
{
    struct DirCount { int ab = 0; int ba = 0; };
    std::unordered_map<uint64_t, DirCount> edges;
    auto key = [](int a, int b) -> uint64_t {
        return (static_cast<uint64_t>(std::min(a, b)) << 32) | static_cast<uint32_t>(std::max(a, b));
    };
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const int verts[3] = {mesh.triangles()[i], mesh.triangles()[i + 1], mesh.triangles()[i + 2]};
        for (int e = 0; e < 3; ++e) {
            const int a = verts[e];
            const int b = verts[(e + 1) % 3];
            DirCount& dc = edges[key(a, b)];
            if (a < b) ++dc.ab; else ++dc.ba;
        }
    }
    int bad = 0;
    for (const auto& entry : edges) {
        if (entry.second.ab + entry.second.ba != 2) continue;
        if (entry.second.ab != 1 || entry.second.ba != 1) ++bad;
    }
    return bad;
}

int closed_manifold_bad_count(const PcgGeometry& geometry)
{
    std::unordered_map<uint64_t, int> edge_use;
    auto key = [](int a, int b) -> uint64_t {
        return (static_cast<uint64_t>(std::min(a, b)) << 32) |
               static_cast<uint32_t>(std::max(a, b));
    };
    for (const auto& face : geometry.faces()) {
        for (size_t i = 0; i < face.size(); ++i) {
            ++edge_use[key(face[i], face[(i + 1) % face.size()])];
        }
    }
    int bad = 0;
    for (const auto& [edge, count] : edge_use) {
        (void)edge;
        if (count != 2) ++bad;
    }
    return bad;
}

PcgGeometry box_geometry(double w, double h, double d)
{
    PcgMeshData mesh = create_box_mesh(w, h, d);
    return geometry_from_mesh(mesh);
}

void test_subtract_basic()
{
    // Subtract a small box from a larger one
    PcgGeometry a = box_geometry(4.0, 4.0, 4.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Subtract failed: " + result.message).c_str());

    // Should have triangles
    if (result.geometry.faces().empty())
        fail("Subtract: result should have faces");

    // Triangulate and check volume
    PcgMeshData out_mesh = triangulate_geometry(result.geometry);
    double vol = std::fabs(signed_volume(out_mesh));

    // Box A volume = 64, Box B volume = 8
    // Subtract result volume should be 64 - 8 = 56
    if (std::fabs(vol - 56.0) > 2.0)
        fail(("Subtract: volume should be ~56, got " + std::to_string(vol)).c_str());
    if (const int bad = closed_manifold_bad_count(result.geometry); bad != 0)
        fail(("Subtract: result is not closed, bad edges=" + std::to_string(bad)).c_str());
}

void test_union_disjoint()
{
    // Two disjoint boxes
    PcgGeometry a = box_geometry(2.0, 2.0, 2.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    // Translate B away from A
    for (auto& p : b.points_mut()) {
        p.x += 10.0;
    }

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Union;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Union failed: " + result.message).c_str());

    // Should have 24 triangular faces (two boxes, no intersection, each 12 tris)
    // or 12 if n-gon faces are preserved
    if (result.geometry.faces().size() != 12 && result.geometry.faces().size() != 24)
        fail(("Union disjoint: expected 12 or 24 faces, got " + std::to_string(result.geometry.faces().size())).c_str());
}

void test_union_overlap()
{
    // Two 50% overlapping boxes
    PcgGeometry a = box_geometry(2.0, 2.0, 2.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    // Translate B by 1.0 in X (50% overlap)
    for (auto& p : b.points_mut()) {
        p.x += 1.0;
    }

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Union;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Union overlap failed: " + result.message).c_str());

    PcgMeshData out_mesh = triangulate_geometry(result.geometry);
    double vol = std::fabs(signed_volume(out_mesh));

    // Box A = 8, Box B = 8, overlap = 1*2*2 = 4
    // Union volume = 8 + 8 - 4 = 12
    // Tolerance accounts for coplanar partition approximation
    if (std::fabs(vol - 12.0) > 2.0)
        fail(("Union overlap: volume should be ~12, got " + std::to_string(vol)).c_str());
    if (const int bad = closed_manifold_bad_count(result.geometry); bad != 0) {
        for (size_t i = 0; i < result.geometry.points().size(); ++i) {
            const auto& p = result.geometry.points()[i];
            std::printf("v %zu %.6f %.6f %.6f\n", i, p.x, p.y, p.z);
        }
        for (size_t i = 0; i < result.geometry.faces().size(); ++i) {
            const auto& f = result.geometry.faces()[i];
            std::printf("f %zu", i);
            for (int v : f) std::printf(" %d", v);
            std::printf("\n");
        }
        fail(("Union overlap: result is not closed, bad edges=" + std::to_string(bad)).c_str());
    }
}

void test_intersect()
{
    // Two 50% overlapping boxes
    PcgGeometry a = box_geometry(2.0, 2.0, 2.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    for (auto& p : b.points_mut()) {
        p.x += 1.0;
    }

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Intersect;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Intersect failed: " + result.message).c_str());

    PcgMeshData out_mesh = triangulate_geometry(result.geometry);
    double vol = std::fabs(signed_volume(out_mesh));

    // Intersection volume = 1*2*2 = 4
    // Tolerance accounts for coplanar partition approximation
    if (std::fabs(vol - 4.0) > 1e-6)
        fail(("Intersect: volume should be 4, got " + std::to_string(vol) +
              ", bad edges=" + std::to_string(closed_manifold_bad_count(result.geometry))).c_str());
    if (const int bad = closed_manifold_bad_count(result.geometry); bad != 0)
        fail(("Intersect: result is not closed, bad edges=" + std::to_string(bad)).c_str());
}

void test_invalid_input()
{
    PcgGeometry empty;
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    ::pcg::internal::geometry::BooleanOptions opts;
    auto result = execute_boolean(empty, b, opts);
    if (result.error != BooleanErrorType::InvalidInput)
        fail("execute_boolean: empty input should return InvalidInput");
}

void test_groups_present()
{
    // Test that output groups are created for intersecting boxes
    PcgGeometry a = box_geometry(2.0, 2.0, 2.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    for (auto& p : b.points_mut()) {
        p.x += 1.0;
    }

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Shatter;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Shatter failed: " + result.message).c_str());

    // Check that at least some groups exist
    auto face_groups = result.geometry.groups().group_names(GroupDomain::Face);
    if (face_groups.empty())
        fail("Shatter: should have face groups");
}

bool always_cancel()
{
    return true;
}

void test_cancel_observable()
{
    PcgGeometry a = box_geometry(2.0, 2.0, 2.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);
    for (auto& p : b.points_mut())
        p.x += 0.5;

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;
    opts.is_cancel_requested = &always_cancel;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Cancelled)
        fail(("cancel should return Cancelled, got " + result.message).c_str());
    if (result.message.find("cancel") == std::string::npos)
        fail("cancel message should mention cancel");
}

bool sleep_once_no_cancel()
{
    static bool slept = false;
    if (!slept) {
        slept = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void test_timeout_observable()
{
    PcgGeometry a = box_geometry(2.0, 2.0, 2.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);
    for (auto& p : b.points_mut())
        p.x += 0.5;

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;
    opts.timeout_ms = 1;
    // First cancel-poll sleeps past the 1 ms deadline; timeout check runs on pair_i%64==0.
    opts.is_cancel_requested = &sleep_once_no_cancel;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Timeout)
        fail(("timeout should return Timeout, got error=" +
              std::to_string(static_cast<int>(result.error)) + " msg=" + result.message)
                 .c_str());
    if (result.message.find("timed out") == std::string::npos)
        fail("timeout message should mention timed out");
}

} // namespace

int main()
{
    test_subtract_basic();
    test_union_disjoint();
    test_union_overlap();
    test_intersect();
    test_invalid_input();
    test_groups_present();
    test_cancel_observable();
    test_timeout_observable();

    std::printf("test_arrangement: all tests passed\n");
    return 0;
}
