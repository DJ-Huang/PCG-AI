// B4: Boolean → Bevel chain test.
// Verifies that BooleanMesh output can be consumed by BevelMesh downstream.

#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/boolean_elements.hpp"
#include "geometry/bmesh.hpp"
#include "geometry/group_table.hpp"

#include <cstdio>
#include <cstdlib>

using namespace pcg::internal::data;
using namespace pcg::internal::elements;
using namespace pcg::internal::geometry;

namespace {

[[noreturn]] void fail(const char* msg)
{
    std::printf("FAIL: %s\n", msg);
    std::exit(1);
}

void test_boolean_then_bevel()
{
    // Create two boxes
    auto a = geometry_from_mesh(create_box_mesh(4.0, 4.0, 4.0));
    auto b = geometry_from_mesh(create_box_mesh(2.0, 2.0, 2.0));

    // Boolean subtract
    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;
    auto bool_result = boolean_geometry(a, b, opts);

    if (bool_result.points().empty())
        fail("Boolean: result should not be empty");

    // Bevel the result
    auto bevel_result = bevel_geometry(bool_result, 0.1, 1);

    if (bevel_result.points().empty())
        fail("Bevel: result should not be empty");
    if (bevel_result.faces().empty())
        fail("Bevel: result should have faces");
}

void test_ab_seams_as_bevel_group()
{
    // Test that ab_seams edge group can be used as bevel edge selection
    auto a = geometry_from_mesh(create_box_mesh(2.0, 2.0, 2.0));
    auto b = geometry_from_mesh(create_box_mesh(2.0, 2.0, 2.0));
    for (auto& p : b.points_mut()) p.x += 1.0;

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Shatter;
    auto bool_result = boolean_geometry(a, b, opts);

    if (bool_result.points().empty())
        fail("Shatter: result should not be empty");

    // Check if ab_seams group exists
    auto edge_groups = bool_result.groups().group_names(GroupDomain::Edge);
    bool has_seams = false;
    for (const auto& g : edge_groups) {
        if (g == "ab_seams") has_seams = true;
    }

    // Even if ab_seams is empty, bevel should work with edge groups
    BevelEdgeSelection edge_sel;
    edge_sel.edge_group = "ab_seams";
    edge_sel.exclude_unshared = false;

    auto bevel_result = bevel_geometry(bool_result, 0.05, 1,
                                        BevelMethod::Edge, BevelOffsetType::Offset,
                                        true, 30.0, 0.5,
                                        BevelMiter::Sharp, BevelMiter::Sharp,
                                        BevelVMeshMethod::Adj,
                                        nullptr, edge_sel);

    if (bevel_result.points().empty())
        fail("Bevel with ab_seams: result should not be empty");
}

} // namespace

int main()
{
    test_boolean_then_bevel();
    test_ab_seams_as_bevel_group();

    std::printf("test_boolean_bevel_chain: all tests passed\n");
    return 0;
}
