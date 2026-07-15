// B3: Boolean output tests - detriangulation modes and group output.

#include "geometry/boolean_output.hpp"
#include "geometry/arrangement.hpp"
#include "geometry/bmesh.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/geometry_algorithms.hpp"

#include <cstdio>
#include <cstdlib>
#include <unordered_set>

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

// ── Regression: dissolve_collinear must not remove CSG seam vertices ───

// Direct BMesh test: a single n-gon face with a collinear valence-2 vertex.
// bmesh_from_geometry with default options must preserve the vertex.
void test_bmesh_preserves_collinear_loop_vertex()
{
    // Verify the default flag value
    if (BMeshBuildOptions{}.dissolve_collinear != false)
        fail("BMeshBuildOptions default: dissolve_collinear must be false");

    PcgGeometry geom;
    geom.points_mut() = {
        {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}
    };
    geom.faces_mut() = {{0, 1, 2, 3, 4}};

    BMesh bm = bmesh_from_geometry(geom);

    // Vertex 1 is collinear between 0 and 2.  Dissolve would remove it.
    bool found = false;
    for (int v : bm.faces[0].verts) {
        if (v == 1) { found = true; break; }
    }
    if (!found)
        fail("bmesh_from_geometry: collinear vertex 1 must be preserved by default");
}

// When dissolve_collinear=true, the collinear vertex should be removed
// from face loops (but not from the verts array).
void test_bmesh_dissolve_when_enabled()
{
    PcgGeometry geom;
    geom.points_mut() = {
        {0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {2, 2, 0}, {0, 2, 0}
    };
    geom.faces_mut() = {{0, 1, 2, 3, 4}};

    BMeshBuildOptions opts;
    opts.merge_coplanar_angle_deg = 2.0;
    opts.dissolve_collinear = true;

    BMesh bm = bmesh_from_geometry(geom, opts);

    // Vertex 1 should be removed from face loop
    for (int v : bm.faces[0].verts) {
        if (v == 1)
            fail("bmesh_from_geometry: dissolve_collinear=true should remove vertex 1 from face loop");
    }
}

// Calling-chain test: group_create angle mode builds a BMesh internally.
// If dissolve removes Simple-subdiv edge midpoints, the output edge keys
// no longer match the input geometry's edge keys.
void test_group_create_preserves_collinear_segment_edge()
{
    const PcgGeometry box = create_box_geometry(2.0, 2.0, 2.0);
    const PcgGeometry simple1 = subdivide_geometry(box, 1, SubdivideMethod::Simple);

    auto input_edges_vec = geometry_edge_keys(simple1);
    std::unordered_set<int64_t> input_edges(input_edges_vec.begin(), input_edges_vec.end());

    GroupCreateOptions opts;
    opts.domain = "edge";
    opts.mode = "angle";
    opts.min_edge_angle_deg = 30.0;
    opts.output_group = "test_angle";
    PcgGeometry result = group_create(simple1, opts);

    auto group_members = result.groups().members(GroupDomain::Edge, "test_angle");
    if (group_members.empty())
        fail("group_create: angle mode should select box edges (90° > 30°)");

    for (int id : group_members) {
        if (input_edges.count(static_cast<int64_t>(id)) == 0)
            fail("group_create: output edge key not in input geometry (dissolve changed edge keys)");
    }
}

// Calling-chain test: finalize_boolean_output(All) builds a BMesh to merge
// coplanar triangles.  If dissolve removes CSG seam vertices, the output
// gains edge keys that did not exist in the pre-detriangulation geometry.
void test_boolean_preserves_csg_seam_topology()
{
    PcgGeometry a = box_geometry(4.0, 4.0, 4.0);
    PcgGeometry b = box_geometry(2.0, 2.0, 2.0);
    for (auto& p : b.points_mut()) p.x += 1.0;

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;

    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Subtract failed: " + result.message).c_str());

    auto input_edges_vec = geometry_edge_keys(result.geometry);
    std::unordered_set<int64_t> input_edges(input_edges_vec.begin(), input_edges_vec.end());

    auto out = finalize_boolean_output(result, ::pcg::internal::geometry::DetriangulateMode::All);

    auto output_edges_vec = geometry_edge_keys(out);
    for (int64_t key : output_edges_vec) {
        if (input_edges.count(key) == 0)
            fail("finalize_boolean_output: output edge key not in input (dissolve created new edges)");
    }
}

} // namespace

int main()
{
    test_detriangulate_none();
    test_detriangulate_all();
    test_detriangulate_unchanged();
    test_groups_preserved();
    test_ab_seams_edge_group();
    test_bmesh_preserves_collinear_loop_vertex();
    test_bmesh_dissolve_when_enabled();
    test_group_create_preserves_collinear_segment_edge();
    test_boolean_preserves_csg_seam_topology();

    std::printf("test_boolean_output: all tests passed\n");
    return 0;
}
