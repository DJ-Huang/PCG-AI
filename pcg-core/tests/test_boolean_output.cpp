// B3: Boolean output tests - detriangulation modes and group output.

#include "geometry/boolean_output.hpp"
#include "geometry/arrangement.hpp"
#include "geometry/bmesh.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/geometry_algorithms.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
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

double mesh_surface_area(const PcgMeshData& mesh)
{
    double area = 0.0;
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const auto& p0 = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i])];
        const auto& p1 = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 1])];
        const auto& p2 = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 2])];
        const double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
        const double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
        const double cx = uy * vz - uz * vy;
        const double cy = uz * vx - ux * vz;
        const double cz = ux * vy - uy * vx;
        area += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
    }
    return area;
}

double total_surface_area(const PcgGeometry& g)
{
    return mesh_surface_area(triangulate_geometry_shared(g));
}

int closed_manifold_bad_count(const PcgGeometry& geometry)
{
    std::unordered_map<int64_t, int> incidence;
    for (const auto& face : geometry.faces()) {
        for (size_t i = 0; i < face.size(); ++i)
            incidence[edge_key(face[i], face[(i + 1) % face.size()])]++;
    }
    int bad = 0;
    for (const auto& entry : incidence)
        bad += entry.second != 2 ? 1 : 0;
    return bad;
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

    // All mode must actually recombine coplanar CSG triangles into n-gons
    // (box minus contained box: untouched faces restore to quads).
    bool has_ngon = false;
    for (const auto& face : out.faces()) {
        if (face.size() > 3) { has_ngon = true; break; }
    }
    if (!has_ngon)
        fail("Detriangulate All: coplanar triangles should recombine into n-gons");
}

// ── Regression: coplanar merge must not fill boolean-pierced holes ──────

// A plate minus a through-cylinder produces coplanar top/bottom faces whose
// boundary has TWO loops (outer square + circular hole). Merging those
// triangles into one n-gon would fill the hole. Expected areas:
// hole preserved ≈ 2·(16−π·0.25) + 3.2 + 0.628 ≈ 34.3; filled ≈ 35.8.
void test_detriangulate_all_preserves_holes()
{
    PcgGeometry plate = box_geometry(4.0, 0.2, 4.0);
    PcgGeometry cyl = geometry_from_mesh(create_cylinder_mesh(0.5, 2.0, 16, 1, true, true));

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;

    auto result = execute_boolean(plate, cyl, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Plate minus cylinder failed: " + result.message).c_str());

    auto out = finalize_boolean_output(result, ::pcg::internal::geometry::DetriangulateMode::All);

    const double raw_area = total_surface_area(result.geometry);
    const double area = total_surface_area(out);
    if (std::fabs(area - raw_area) > 1e-6)
        fail("Detriangulate All: changed the boolean hole surface area");
    if (closed_manifold_bad_count(out) != 0)
        fail("Detriangulate All: boolean hole output is not closed manifold");
}

// A through-cutter entering from the plate edge creates one concave boundary
// loop (the same topology as an open wheel arch). The n-gon is valid, but a
// vertex fan fills the notch. Output triangulation must preserve the raw CSG.
void test_detriangulate_all_preserves_open_notch()
{
    PcgGeometry plate = box_geometry(4.0, 0.2, 4.0);
    PcgGeometry cutter = box_geometry(1.0, 2.0, 1.0);
    for (auto& point : cutter.points_mut())
        point.z += 1.75;

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;
    auto result = execute_boolean(plate, cutter, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Plate notch subtract failed: " + result.message).c_str());

    auto out = finalize_boolean_output(
        result, ::pcg::internal::geometry::DetriangulateMode::All);
    const double raw_area = total_surface_area(result.geometry);
    const double out_area = total_surface_area(out);
    if (std::fabs(out_area - raw_area) > 1e-6)
        fail("Detriangulate All: concave notch triangulation changed surface area");
    if (closed_manifold_bad_count(out) != 0)
        fail("Detriangulate All: open-notch solid is not closed manifold");

    bool has_concave_ngon = false;
    for (const auto& face : out.faces()) {
        if (face.size() > 4) {
            has_concave_ngon = true;
            break;
        }
    }
    if (!has_concave_ngon)
        fail("Detriangulate All: source-face notch should reconstruct an n-gon");
}

// Adjacent faces on a shallow curve are different source polygons. They must
// not merge merely because their normals differ by less than an angle epsilon.
void test_detriangulate_all_respects_source_faces()
{
    BooleanResult result;
    constexpr int segment_count = 5;
    constexpr double step = 0.08 * 3.14159265358979323846 / 180.0;
    for (int i = 0; i <= segment_count; ++i) {
        const double angle = static_cast<double>(i) * step;
        result.geometry.points_mut().push_back({std::sin(angle), 0.0, std::cos(angle)});
        result.geometry.points_mut().push_back({std::sin(angle), 1.0, std::cos(angle)});
    }
    for (int i = 0; i < segment_count; ++i) {
        const int v0 = i * 2;
        const int v1 = v0 + 2;
        const int v2 = v0 + 3;
        const int v3 = v0 + 1;
        result.geometry.faces_mut().push_back({v0, v1, v2});
        result.geometry.faces_mut().push_back({v0, v2, v3});
        result.face_origins.push_back({0, i, false});
        result.face_origins.push_back({0, i, false});
    }

    auto out = finalize_boolean_output(
        result, ::pcg::internal::geometry::DetriangulateMode::All);
    if (out.faces().size() != segment_count)
        fail("Detriangulate All: merged triangles across different source polygons");
    for (const auto& face : out.faces()) {
        if (face.size() != 4)
            fail("Detriangulate All: each curved-strip source polygon should restore to a quad");
    }
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

    bool has_ngon = false;
    for (const auto& face : out.faces()) {
        if (face.size() > 3) {
            has_ngon = true;
            break;
        }
    }
    if (!has_ngon)
        fail("Detriangulate Unchanged: untouched source polygons should be restored");
}

void test_detriangulate_unchanged_keeps_cut_faces()
{
    PcgGeometry plate = box_geometry(4.0, 0.2, 4.0);
    PcgGeometry cutter = box_geometry(1.0, 2.0, 1.0);
    for (auto& point : cutter.points_mut())
        point.z += 1.75;

    ::pcg::internal::geometry::BooleanOptions opts;
    opts.operation = ::pcg::internal::geometry::BooleanOp::Subtract;
    auto result = execute_boolean(plate, cutter, opts);
    if (result.error != BooleanErrorType::Ok)
        fail(("Unchanged notch subtract failed: " + result.message).c_str());
    if (result.face_origins.size() != result.geometry.faces().size())
        fail("Boolean output must provide one source origin per triangle");

    auto unchanged = finalize_boolean_output(
        result, ::pcg::internal::geometry::DetriangulateMode::Unchanged);
    bool has_triangle = false;
    bool has_ngon = false;
    for (const auto& face : unchanged.faces()) {
        has_triangle = has_triangle || face.size() == 3;
        has_ngon = has_ngon || face.size() > 3;
    }
    if (!has_triangle)
        fail("Detriangulate Unchanged: cut source polygons must remain triangulated");
    if (!has_ngon)
        fail("Detriangulate Unchanged: untouched source polygons must be restored");

    auto all = finalize_boolean_output(
        result, ::pcg::internal::geometry::DetriangulateMode::All);
    if (all.faces().size() >= unchanged.faces().size())
        fail("Detriangulate All should reconstruct more cut-face fragments than Unchanged");
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
    for (const auto& name : face_groups) {
        for (int member : out.groups().members(GroupDomain::Face, name)) {
            if (member < 0 || member >= static_cast<int>(out.faces().size()))
                fail("Face group contains a stale index after detriangulation");
        }
    }
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

    const auto& raw_seams = result.geometry.groups().members(
        GroupDomain::Edge, BooleanGroups::AB_SEAMS);
    if (raw_seams.empty())
        fail("Shatter should produce A-B seam edges for intersecting boxes");

    auto out = finalize_boolean_output(
        result, ::pcg::internal::geometry::DetriangulateMode::All);
    const auto& out_seams = out.groups().members(
        GroupDomain::Edge, BooleanGroups::AB_SEAMS);
    if (out_seams != raw_seams)
        fail("Detriangulate All must preserve the complete A-B seam edge group");
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
    test_detriangulate_all_preserves_holes();
    test_detriangulate_all_preserves_open_notch();
    test_detriangulate_all_respects_source_faces();
    test_detriangulate_unchanged();
    test_detriangulate_unchanged_keeps_cut_faces();
    test_groups_preserved();
    test_ab_seams_edge_group();
    test_bmesh_preserves_collinear_loop_vertex();
    test_bmesh_dissolve_when_enabled();
    test_group_create_preserves_collinear_segment_edge();
    test_boolean_preserves_csg_seam_topology();

    std::printf("test_boolean_output: all tests passed\n");
    return 0;
}
