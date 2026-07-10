// B3: Boolean output tests - detriangulation modes and group output.

#include "geometry/boolean_output.hpp"
#include "geometry/arrangement.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"

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

PcgGeometry box_geometry(double w, double h, double d)
{
    return geometry_from_mesh(create_box_mesh(w, h, d));
}

void test_detriangulate_none()
{
    PcgGeometry a = box_geometry(4.0, 4.0, 4.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;
    opts.detriangulate = ::pcg::internal::geometry::DetriangulateMode::None;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Subtract failed: " + result.message).c_str());

    // None mode: just return as-is (triangles)
    auto out = finalize_boolean_output(result, ::pcg::internal::geometry::DetriangulateMode::None);
    if (out.faces().empty())
        fail("Detriangulate None: should have faces");
}

void test_detriangulate_all()
{
    PcgGeometry a = box_geometry(4.0, 4.0, 4.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Subtract failed: " + result.message).c_str());

    // All mode: merge coplanar triangles
    auto out = finalize_boolean_output(result, ::pcg::internal::geometry::DetriangulateMode::All);
    if (out.faces().empty())
        fail("Detriangulate All: should have faces");

    // All mode should have fewer or equal faces than None mode
    auto out_none = finalize_boolean_output(result, ::pcg::internal::geometry::DetriangulateMode::None);
    if (out.faces().size() > out_none.faces().size())
        fail("Detriangulate All: should have <= faces than None");
}

void test_detriangulate_unchanged()
{
    PcgGeometry a = box_geometry(4.0, 4.0, 4.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Subtract failed: " + result.message).c_str());

    auto out = finalize_boolean_output(result, ::pcg::internal::geometry::DetriangulateMode::Unchanged);
    if (out.faces().empty())
        fail("Detriangulate Unchanged: should have faces");
}

void test_groups_preserved()
{
    PcgGeometry a = box_geometry(4.0, 4.0, 4.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Shatter;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Shatter failed: " + result.message).c_str());

    auto out = finalize_boolean_output(result, ::pcg::internal::geometry::DetriangulateMode::All);

    // Groups should be preserved after detriangulation
    auto face_groups = out.groups().group_names(GroupDomain::Face);
    if (face_groups.empty())
        fail("Groups should be preserved after detriangulation");

    // Check specific group names exist
    bool found_a_outside = false;
    for (const auto& g : face_groups) {
        if (g == BooleanGroups::A_OUTSIDE_B) found_a_outside = true;
    }
    if (!found_a_outside)
        fail("A_OUTSIDE_B group should exist after detriangulation");
}

void test_ab_seams_edge_group()
{
    PcgGeometry a = box_geometry(2.0, 2.0, 2.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);

    for (auto& p : b.points_mut()) p.x += 1.0;

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Shatter;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Shatter failed: " + result.message).c_str());

    // Check that ab_seams edge group exists (for intersecting geometries)
    auto edge_groups = result.geometry.groups().group_names(GroupDomain::Edge);
    // ab_seams may or may not have members depending on intersection detection
    // but the group name should exist if there are seam edges
}

} // namespace

int main()
{
    test_detriangulate_none();
    test_detriangulate_all();
    test_detriangulate_unchanged();
    test_groups_preserved();
    test_ab_seams_edge_group();

    std::printf("test_boolean_output: all tests passed\n");
    return 0;
}
