// B3: Boolean binary serialization round-trip tests.
// Verifies that groups survive pcg_geometry_binary serialization.

#include "data/pcg_geometry.hpp"
#include "data/pcg_geometry_binary.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"
#include "geometry/arrangement.hpp"
#include "geometry/group_table.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace pcg::internal::data;
using namespace pcg::internal::elements;
using namespace pcg::internal::geometry;

namespace {

[[noreturn]] void fail(const char* msg)
{
    std::printf("FAIL: %s\n", msg);
    std::exit(1);
}

void test_geometry_binary_roundtrip()
{
    // Create geometry with groups
    PcgGeometry geo;
    geo.points_mut() = {{0,0,0}, {1,0,0}, {1,1,0}, {0,1,0}};
    geo.faces_mut() = {{0,1,2}, {0,2,3}};

    // Add some groups
    geo.groups().add(GroupDomain::Face, "a_outside_b", 0);
    geo.groups().add(GroupDomain::Face, "b_inside_a", 1);
    geo.groups().add(GroupDomain::Edge, "ab_seams", 12345);

    // Serialize
    int bsize = geometry_binary_size(geo);
    if (bsize <= 0)
        fail("geometry_binary: binary size should be positive");
    std::vector<uint8_t> binary(bsize);
    if (!write_geometry_binary(geo, binary.data(), bsize))
        fail("geometry_binary: write failed");

    // Deserialize
    PcgGeometry restored;
    if (!read_geometry_binary(binary.data(), bsize, restored))
        fail("geometry_binary: read failed");

    // Check points
    if (restored.points().size() != geo.points().size())
        fail("geometry_binary: point count mismatch after round-trip");

    // Check faces
    if (restored.faces().size() != geo.faces().size())
        fail("geometry_binary: face count mismatch after round-trip");

    // Check face groups
    auto original_face_groups = geo.groups().group_names(GroupDomain::Face);
    auto restored_face_groups = restored.groups().group_names(GroupDomain::Face);
    if (original_face_groups.size() != restored_face_groups.size())
        fail("geometry_binary: face group count mismatch");

    for (const auto& g : original_face_groups) {
        auto orig_members = geo.groups().members(GroupDomain::Face, g);
        auto rest_members = restored.groups().members(GroupDomain::Face, g);
        if (orig_members.size() != rest_members.size())
            fail("geometry_binary: group member count mismatch");
    }

    // Check edge groups
    auto original_edge_groups = geo.groups().group_names(GroupDomain::Edge);
    auto restored_edge_groups = restored.groups().group_names(GroupDomain::Edge);
    if (original_edge_groups.size() != restored_edge_groups.size())
        fail("geometry_binary: edge group count mismatch");
}

void test_boolean_result_roundtrip()
{
    PcgGeometry a = geometry_from_mesh(create_box_mesh(4.0, 4.0, 4.0));
    PcgGeometry b = geometry_from_mesh(create_box_mesh(2.0, 2.0, 2.0));

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Boolean failed: " + result.message).c_str());

    // Serialize the result
    int bsize = geometry_binary_size(result.geometry);
    if (bsize <= 0)
        fail("geometry_binary: boolean result binary size should be positive");
    std::vector<uint8_t> binary(bsize);
    if (!write_geometry_binary(result.geometry, binary.data(), bsize))
        fail("geometry_binary: boolean result write failed");

    // Deserialize
    PcgGeometry restored;
    if (!read_geometry_binary(binary.data(), bsize, restored))
        fail("geometry_binary: boolean result read failed");

    // Check geometry is intact
    if (restored.points().size() != result.geometry.points().size())
        fail("geometry_binary: boolean result point count mismatch");

    if (restored.faces().size() != result.geometry.faces().size())
        fail("geometry_binary: boolean result face count mismatch");

    // Check groups are intact
    auto orig_groups = result.geometry.groups().group_names(GroupDomain::Face);
    auto rest_groups = restored.groups().group_names(GroupDomain::Face);
    if (orig_groups.size() != rest_groups.size())
        fail("geometry_binary: boolean result group count mismatch");
}

} // namespace

int main()
{
    test_geometry_binary_roundtrip();
    test_boolean_result_roundtrip();

    std::printf("test_boolean_binary: all tests passed\n");
    return 0;
}
