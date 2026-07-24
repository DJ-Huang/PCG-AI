#include "elements/assembly_algorithms.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/vehicle_modeling_algorithms.hpp"
#include "cook_hash.hpp"
#include "graph_execution_result.hpp"
#include "graph_executor.hpp"
#include "graph_parser.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace pcg::internal;
using namespace pcg::internal::data;
using namespace pcg::internal::elements;

namespace {

[[noreturn]] void fail(const std::string& message)
{
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    std::exit(1);
}

void expect(bool condition, const std::string& message)
{
    if (!condition) fail(message);
}

bool near(double a, double b, double tolerance = 1.0e-6)
{
    return std::abs(a - b) <= tolerance;
}

PcgVec3 size_of(const PcgGeometry& geometry)
{
    PcgVec3 minimum = geometry.points().front();
    PcgVec3 maximum = geometry.points().front();
    for (const auto& point : geometry.points()) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }
    return {maximum.x - minimum.x, maximum.y - minimum.y, maximum.z - minimum.z};
}

PcgGeometry execute_geometry_graph(const nlohmann::json& document)
{
    char error[1024] = {};
    Graph graph;
    const std::string json = document.dump();
    expect(parse_graph(json.c_str(), graph, error, sizeof(error)) == PCG_OK,
           std::string("parse graph: ") + error);
    expect(validate_graph_structure(graph, error, sizeof(error)) == PCG_OK,
           std::string("validate graph: ") + error);
    GraphExecutionResult result;
    expect(execute_graph(graph, 17, result, error, sizeof(error)) == PCG_OK,
           std::string("execute graph: ") + error);
    expect(result.source_geometry != nullptr, "graph did not preserve source geometry");
    return *result.source_geometry;
}

void test_import_mesh(const std::filesystem::path& fixture)
{
    PcgGeometry imported;
    std::string error;
    expect(import_geometry_file(fixture, ImportMeshOptions{}, imported, error), error);
    expect(imported.points().size() == 4, "ImportMesh point count");
    expect(imported.faces().size() == 1, "ImportMesh face count");
    expect(imported.has_uvs(), "ImportMesh point UVs");
    expect(imported.attributes().find(AttributeOwner::Point, "N") != nullptr,
           "ImportMesh normal attribute");
    expect(imported.attributes().find(AttributeOwner::Primitive, "name") != nullptr,
           "ImportMesh primitive name attribute");
    const auto render_mesh = compute_split_normals(imported, NormalComputeOptions{});
    expect(render_mesh.has_normals() && near(render_mesh.normals().front().z, 1.0),
           "ImportMesh authored point normals reach render mesh");

    ImportMeshOptions converted_options;
    converted_options.axis_conversion = "zUpToYUp";
    PcgGeometry converted;
    expect(import_geometry_file(fixture, converted_options, converted, error), error);
    const auto converted_extent = size_of(converted);
    expect(near(converted_extent.x, 2.0) && near(converted_extent.y, 0.0) &&
               near(converted_extent.z, 2.0),
           "ImportMesh axis conversion changes geometry basis");
    const auto* converted_normal =
        converted.attributes().find(AttributeOwner::Point, "N");
    expect(converted_normal && near(converted_normal->float_values()[1], 1.0),
           "ImportMesh axis conversion changes normal basis");

    PcgGeometry missing;
    const auto missing_path = fixture.parent_path() / "missing-import-mesh.obj";
    expect(!import_geometry_file(missing_path, ImportMeshOptions{}, missing, error) &&
               error.find("file not found") != std::string::npos,
           "ImportMesh missing file reports a clear error");

    const auto graph = nlohmann::json{
        {"version", "2.0"},
        {"nodes", nlohmann::json::array({
            {{"id", "import"}, {"type", "ImportMesh"},
             {"position", {{"x", 0.0}, {"y", 0.0}}},
             {"data", {{"path", fixture.string()}, {"scale", 2.0}}}},
            {{"id", "output"}, {"type", "Output"},
             {"position", {{"x", 200.0}, {"y", 0.0}}},
             {"data", nlohmann::json::object()}},
        })},
        {"edges", nlohmann::json::array({
            {{"id", "import-output"}, {"source", "import"}, {"target", "output"},
             {"sourceHandle", "out"},
             {"targetHandle", "in"}, {"sourcePinType", "SpatialMesh"},
             {"targetPinType", "Any"}},
        })},
    };
    const auto graph_result = execute_geometry_graph(graph);
    expect(near(size_of(graph_result).x, 4.0), "ImportMesh graph scale");
}

void test_match_size()
{
    auto source = geometry_from_mesh(create_box_mesh(2.0, 4.0, 6.0));
    source.groups().add(geometry::GroupDomain::Face, "keep", 0);
    auto& id = source.attributes().create_int(AttributeOwner::Point, "id", 1, {-1});
    id.int_values_mut() = {0, 1, 2, 3, 4, 5, 6, 7};
    auto& position = source.attributes().create_float(
        AttributeOwner::Point, "customP", 3, {0.0, 0.0, 0.0},
        AttributeTransformRole::Position);
    for (const auto& point : source.points())
        position.float_values_mut().insert(position.float_values_mut().end(),
                                           {point.x, point.y, point.z});

    MatchSizeOptions options;
    options.target_position = {10.0, 20.0, 30.0};
    options.target_size = {4.0, 8.0, 12.0};
    options.uniform_scale = false;
    PcgGeometry matched;
    std::string error;
    expect(match_size_geometry(source, nullptr, options, matched, error), error);
    const auto extent = size_of(matched);
    expect(near(extent.x, 4.0) && near(extent.y, 8.0) && near(extent.z, 12.0),
           "MatchSize target extents");
    expect(matched.faces() == source.faces(), "MatchSize topology changed");
    expect(matched.groups() == source.groups(), "MatchSize groups changed");
    expect(matched.attributes().find(AttributeOwner::Detail, "xform") != nullptr,
           "MatchSize transform detail attribute");
    expect(matched.attributes().find(AttributeOwner::Point, "id")->int_values() ==
               id.int_values(),
           "MatchSize non-transform attribute changed");

    auto reference = geometry_from_mesh(create_box_mesh(8.0, 10.0, 12.0));
    for (auto& point : reference.points_mut()) {
        point.x += 5.0;
        point.y -= 2.0;
        point.z += 3.0;
    }
    PcgGeometry reference_matched;
    MatchSizeOptions reference_options;
    reference_options.uniform_scale = false;
    expect(match_size_geometry(source, &reference, reference_options,
                               reference_matched, error),
           error);
    const auto reference_extent = size_of(reference_matched);
    expect(near(reference_extent.x, 8.0) && near(reference_extent.y, 10.0) &&
               near(reference_extent.z, 12.0),
           "MatchSize optional reference controls target bounds");
    const auto reference_bounds_center = [&]() {
        PcgVec3 minimum = reference_matched.points().front();
        PcgVec3 maximum = minimum;
        for (const auto& point : reference_matched.points()) {
            minimum.x = std::min(minimum.x, point.x);
            minimum.y = std::min(minimum.y, point.y);
            minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x);
            maximum.y = std::max(maximum.y, point.y);
            maximum.z = std::max(maximum.z, point.z);
        }
        return PcgVec3{(minimum.x + maximum.x) * 0.5,
                       (minimum.y + maximum.y) * 0.5,
                       (minimum.z + maximum.z) * 0.5};
    }();
    expect(near(reference_bounds_center.x, 5.0) &&
               near(reference_bounds_center.y, -2.0) &&
               near(reference_bounds_center.z, 3.0),
           "MatchSize optional reference controls target alignment");

    // Uniform best-fit keeps aspect ratio and fits inside target.
    MatchSizeOptions uniform_options;
    uniform_options.target_position = {0.0, 0.0, 0.0};
    uniform_options.target_size = {4.0, 8.0, 12.0};
    uniform_options.uniform_scale = true;
    uniform_options.scale_axis = "bestFit";
    PcgGeometry uniform_matched;
    expect(match_size_geometry(source, nullptr, uniform_options, uniform_matched, error),
           error);
    const auto uniform_extent = size_of(uniform_matched);
    expect(near(uniform_extent.x, 4.0) && near(uniform_extent.y, 8.0) &&
               near(uniform_extent.z, 12.0),
           "MatchSize uniform best-fit for proportional source");

    // Restore undoes a previous stash.
    MatchSizeOptions restore_options;
    restore_options.scale_to_fit = false;
    restore_options.translate = false;
    restore_options.restore_transform = true;
    restore_options.restore_attribute = "xform";
    restore_options.stash_transform = false;
    PcgGeometry restored;
    expect(match_size_geometry(matched, nullptr, restore_options, restored, error), error);
    const auto restored_extent = size_of(restored);
    expect(near(restored_extent.x, 2.0) && near(restored_extent.y, 4.0) &&
               near(restored_extent.z, 6.0),
           "MatchSize restore transform");

    const auto graph = nlohmann::json{
        {"version", "1.0"},
        {"nodes", nlohmann::json::array({
            {{"id", "box"}, {"type", "CreateBoxMesh"},
             {"data", {{"width", 2.0}, {"height", 2.0}, {"depth", 2.0}}}},
            {{"id", "match"}, {"type", "MatchSize"},
             {"data", {{"targetSize", nlohmann::json::array({4.0, 6.0, 8.0})},
                       {"uniformScale", false}}}},
            {{"id", "output"}, {"type", "Output"}, {"data", nlohmann::json::object()}},
        })},
        {"edges", nlohmann::json::array({
            {{"source", "box"}, {"target", "match"}, {"targetHandle", "source"}},
            {{"source", "match"}, {"target", "output"}},
        })},
    };
    const auto graph_result = execute_geometry_graph(graph);
    const auto graph_extent = size_of(graph_result);
    expect(near(graph_extent.x, 4.0) && near(graph_extent.y, 6.0) &&
               near(graph_extent.z, 8.0),
           "MatchSize graph result");
}

void test_bend_mesh()
{
    PcgGeometry strip;
    strip.points_mut() = {{-0.1, 0.0, 0.0}, {0.1, 0.0, 0.0},
                          {0.1, 1.0, 0.0}, {-0.1, 1.0, 0.0}};
    strip.faces_mut() = {{0, 1, 2, 3}};
    strip.groups().add(geometry::GroupDomain::Face, "panel", 0);
    auto& normal = strip.attributes().create_float(
        AttributeOwner::Point, "flow", 3, {0.0, 1.0, 0.0},
        AttributeTransformRole::Vector);
    normal.float_values_mut() = {0.0, 1.0, 0.0, 0.0, 1.0, 0.0,
                                 0.0, 1.0, 0.0, 0.0, 1.0, 0.0};

    BendMeshOptions options;
    options.capture_length = 1.0;
    options.angle_degrees = 90.0;
    PcgGeometry bent;
    std::string error;
    expect(bend_geometry(strip, nullptr, options, bent, error), error);
    expect(bent.faces() == strip.faces(), "BendMesh topology changed");
    expect(bent.groups() == strip.groups(), "BendMesh groups changed");
    const double top_center_x = (bent.points()[2].x + bent.points()[3].x) * 0.5;
    const double top_center_y = (bent.points()[2].y + bent.points()[3].y) * 0.5;
    expect(near(top_center_x, 2.0 / 3.14159265358979323846, 1.0e-5),
           "BendMesh radial endpoint");
    expect(near(top_center_y, 2.0 / 3.14159265358979323846, 1.0e-5),
           "BendMesh axial endpoint");
    const auto* mask = bent.attributes().find(AttributeOwner::Point, "bendmask");
    expect(mask && near(mask->float_values().front(), 0.0) &&
               near(mask->float_values().back(), 1.0),
           "BendMesh mask attribute");
    const auto* flow = bent.attributes().find(AttributeOwner::Point, "flow");
    expect(flow && near(flow->float_values()[6], 1.0, 1.0e-5) &&
               near(flow->float_values()[7], 0.0, 1.0e-5),
           "BendMesh vector transform role");

    const auto graph = nlohmann::json{
        {"version", "1.0"},
        {"nodes", nlohmann::json::array({
            {{"id", "box"}, {"type", "CreateBoxMesh"},
             {"data", {{"width", 0.2}, {"height", 1.0}, {"depth", 0.2},
                       {"centerY", 0.5}}}},
            {{"id", "bend"}, {"type", "BendMesh"},
             {"data", {{"captureLength", 1.0}, {"angle", 45.0}}}},
            {{"id", "output"}, {"type", "Output"}, {"data", nlohmann::json::object()}},
        })},
        {"edges", nlohmann::json::array({
            {{"source", "box"}, {"target", "bend"}, {"targetHandle", "source"}},
            {{"source", "bend"}, {"target", "output"}},
        })},
    };
    const auto graph_result = execute_geometry_graph(graph);
    expect(graph_result.attributes().find(AttributeOwner::Point, "bendmask") != nullptr,
           "BendMesh graph mask");
}

PcgGeometry make_semantic_quad()
{
    PcgGeometry geometry;
    geometry.points_mut() = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0},
                             {1.0, 1.0, 0.0}, {0.0, 1.0, 0.0}};
    geometry.faces_mut() = {{0, 1, 2, 3}};
    geometry.set_colors({{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {1, 1, 1, 1}});
    geometry.set_uvs({{0, 0}, {1, 0}, {1, 1}, {0, 1}});
    geometry.set_corner_uvs({{0, 0}, {1, 0}, {1, 1}, {0, 1}});
    geometry.set_face_materials({"panel"});
    geometry.groups().add(geometry::GroupDomain::Point, "points", 2);
    geometry.groups().add(geometry::GroupDomain::Vertex, "corners", 3);
    geometry.groups().add(geometry::GroupDomain::Face, "faces", 0);
    geometry.groups().add(geometry::GroupDomain::Edge, "edges",
                          geometry::edge_group_id(0, 1));
    auto& point = geometry.attributes().create_int(AttributeOwner::Point, "id", 1, {-1});
    point.int_values_mut() = {10, 11, 12, 13};
    auto& point_position = geometry.attributes().create_float(
        AttributeOwner::Point, "customP", 3, {0.0, 0.0, 0.0},
        AttributeTransformRole::Position);
    auto& direction = geometry.attributes().create_float(
        AttributeOwner::Point, "direction", 3, {1.0, 0.0, 0.0},
        AttributeTransformRole::Vector);
    for (const auto& value : geometry.points()) {
        point_position.float_values_mut().insert(point_position.float_values_mut().end(),
                                                 {value.x, value.y, value.z});
        direction.float_values_mut().insert(direction.float_values_mut().end(),
                                            {1.0, 0.0, 0.0});
    }
    auto& vertex = geometry.attributes().create_float(AttributeOwner::Vertex, "weight", 1, {0.0});
    vertex.float_values_mut() = {0.1, 0.2, 0.3, 0.4};
    auto& primitive = geometry.attributes().create_string(
        AttributeOwner::Primitive, "part", 1, {"unset"});
    primitive.string_values_mut() = {"panel"};
    auto& detail = geometry.attributes().create_int(AttributeOwner::Detail, "revision", 1, {0});
    detail.int_values_mut() = {7};
    return geometry;
}

void expect_semantic_cardinality(const PcgGeometry& geometry, const char* label)
{
    std::string error;
    expect(geometry.validate_attributes(&error), std::string(label) + ": " + error);
    expect(!geometry.has_colors() || geometry.colors().size() == geometry.points().size(),
           std::string(label) + ": color cardinality");
    expect(!geometry.has_uvs() || geometry.uvs().size() == geometry.points().size(),
           std::string(label) + ": point UV cardinality");
    expect(!geometry.has_corner_uvs() ||
               geometry.corner_uvs().size() == static_cast<size_t>(geometry.corner_count()),
           std::string(label) + ": corner UV cardinality");
    expect(!geometry.has_face_materials() ||
               geometry.face_materials().size() == geometry.faces().size(),
           std::string(label) + ": material cardinality");
}

void test_topology_remap_contract()
{
    const auto source = make_semantic_quad();
    expect_semantic_cardinality(source, "source");

    const auto transformed = transform_geometry(
        source, 2.0, 3.0, 4.0, 0.0, 0.0, 90.0, 2.0, 1.0, 1.0);
    expect_semantic_cardinality(transformed, "TransformMesh");
    const auto* transformed_position =
        transformed.attributes().find(AttributeOwner::Point, "customP");
    const auto* transformed_direction =
        transformed.attributes().find(AttributeOwner::Point, "direction");
    expect(transformed_position && near(transformed_position->float_values()[0], 2.0) &&
               near(transformed_position->float_values()[1], 3.0),
           "TransformMesh position role uses full affine transform");
    expect(transformed_direction && near(transformed_direction->float_values()[0], 0.0) &&
               near(transformed_direction->float_values()[1], 2.0),
           "TransformMesh applies scale before rotation to vector role");

    CopyMeshOptions copy_options;
    copy_options.mode = "linear";
    copy_options.count = 2;
    copy_options.translate_x = 2.0;
    const auto copied = copy_geometry(source, copy_options);
    expect_semantic_cardinality(copied, "CopyMesh");
    const auto* copied_position = copied.attributes().find(
        AttributeOwner::Point, "customP");
    expect(copied.points().size() == source.points().size() * 2 && copied_position &&
               near(copied_position->float_values()[source.points().size() * 3], 2.0),
           "CopyMesh transforms position role for every instance");
    expect(copied.attributes().find(AttributeOwner::Detail, "revision") != nullptr,
           "CopyMesh preserves detail attributes");
    expect(copied.groups().members(geometry::GroupDomain::Face, "faces").size() == 2 &&
               copied.groups().members(geometry::GroupDomain::Point, "points").size() == 2 &&
               copied.groups().members(geometry::GroupDomain::Edge, "edges").size() == 2,
           "CopyMesh unions same-named groups across instances");

    MirrorMeshOptions mirror_options;
    mirror_options.axis = "x";
    mirror_options.merge_original = true;
    mirror_options.weld_seam = false;
    const auto mirrored = mirror_geometry(source, mirror_options);
    expect_semantic_cardinality(mirrored, "MirrorMesh");
    expect(mirrored.attributes().find(AttributeOwner::Point, "id")->size() ==
               mirrored.points().size(),
           "MirrorMesh point attribute propagation");
    const auto* mirrored_position = mirrored.attributes().find(AttributeOwner::Point, "customP");
    const auto* mirrored_direction = mirrored.attributes().find(AttributeOwner::Point, "direction");
    expect(mirrored_position && near(mirrored_position->float_values()[15], -1.0),
           "MirrorMesh position transform role");
    expect(mirrored_direction && near(mirrored_direction->float_values()[12], -1.0),
           "MirrorMesh vector transform role");

    FuseMeshOptions fuse_options;
    fuse_options.tolerance = 0.00001;
    const auto fused = fuse_geometry(mirrored, fuse_options);
    expect_semantic_cardinality(fused, "FuseMesh");
    expect(fused.points().size() < mirrored.points().size(), "FuseMesh welded seam points");

    PolyExtrudeOptions extrude_options;
    extrude_options.distance = 0.25;
    extrude_options.keep_original = false;
    const auto extruded = poly_extrude_geometry(source, extrude_options);
    expect_semantic_cardinality(extruded, "PolyExtrude");
    expect(extruded.faces().size() > source.faces().size(), "PolyExtrude generated faces");
    expect(extruded.attributes().find(AttributeOwner::Primitive, "part")->size() ==
               extruded.faces().size(),
           "PolyExtrude primitive attribute propagation");

    ShellMeshOptions shell_options;
    shell_options.thickness = 0.1;
    const auto shelled = shell_geometry(source, shell_options);
    expect_semantic_cardinality(shelled, "ShellMesh");
    expect(shelled.attributes().find(AttributeOwner::Vertex, "weight")->size() ==
               static_cast<size_t>(shelled.corner_count()),
           "ShellMesh vertex attribute propagation");
}

void test_import_dependency_hash()
{
    const auto path = std::filesystem::temp_directory_path() /
        "pcg_import_mesh_dependency_test.obj";
    {
        std::ofstream stream(path, std::ios::trunc);
        stream << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    GraphNode node;
    node.id = "import";
    node.type = "ImportMesh";
    node.data = {{"path", path.string()}};
    const auto first = compute_node_input_hash(
        node, 17, {}, nullptr, nullptr, nullptr, nullptr);
    {
        std::ofstream stream(path, std::ios::app);
        stream << "# dependency changed\n";
    }
    const auto second = compute_node_input_hash(
        node, 17, {}, nullptr, nullptr, nullptr, nullptr);
    std::error_code cleanup_error;
    std::filesystem::remove(path, cleanup_error);
    expect(first != second, "ImportMesh external file invalidates cook hash");
}

void test_attribute_default_hash()
{
    PcgGeometry first;
    first.points_mut().push_back({0.0, 0.0, 0.0});
    auto& first_attribute = first.attributes().create_int(
        AttributeOwner::Point, "class", 1, {1});
    first_attribute.int_values_mut() = {7};

    PcgGeometry second;
    second.points_mut().push_back({0.0, 0.0, 0.0});
    auto& second_attribute = second.attributes().create_int(
        AttributeOwner::Point, "class", 1, {2});
    second_attribute.int_values_mut() = {7};
    expect(hash_geometry(first) != hash_geometry(second),
           "attribute defaults participate in cook hash");
}

} // namespace

int main()
{
    const auto fixture = std::filesystem::absolute("fixtures/import_quad.obj");
    test_import_mesh(fixture);
    test_match_size();
    test_bend_mesh();
    test_topology_remap_contract();
    test_import_dependency_hash();
    test_attribute_default_hash();
    std::printf("test_assembly_nodes: all tests passed\n");
    return 0;
}
