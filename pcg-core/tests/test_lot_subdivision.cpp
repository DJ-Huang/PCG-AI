#include "elements/geometry_algorithms.hpp"
#include "elements/lot_subdivision_algorithms.hpp"
#include "elements/element_utils.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/vehicle_modeling_algorithms.hpp"
#include "data/pcg_point_binary.hpp"
#include "graph_execution_result.hpp"
#include "graph_executor.hpp"
#include "graph_parser.hpp"
#include "pcg_api.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

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
    if (!condition)
        fail(message);
}

PcgGeometry make_ground_quad(double width, double depth)
{
    PcgGeometry geo;
    const double hx = width * 0.5;
    const double hz = depth * 0.5;
    geo.points_mut() = {
        {-hx, 0.0, -hz},
        {hx, 0.0, -hz},
        {hx, 0.0, hz},
        {-hx, 0.0, hz},
    };
    geo.faces_mut() = {{0, 3, 2, 1}};
    return geo;
}

GraphExecutionResult execute_graph_document(const nlohmann::json& document)
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
    return result;
}

PcgGeometry execute_geometry_graph(const nlohmann::json& document)
{
    const auto result = execute_graph_document(document);
    expect(result.source_geometry != nullptr, "graph did not preserve source geometry");
    return *result.source_geometry;
}

void test_rectangle_iterations()
{
    LotSubdivisionOptions options;
    options.min_size = 0.1;
    options.iterations = 2;
    options.irregularity = 0.0;
    options.seed = 1;
    options.alignment = "boundingBox";

    const auto lots = lot_subdivide_geometry(make_ground_quad(10.0, 10.0), options);
    expect(lots.faces().size() == 4, "iterations=2 should yield 4 lots on a rectangle");
    expect(lots.points().size() >= 4, "lots keep polygon points");
    const auto* lotid = lots.attributes().find(AttributeOwner::Primitive, "lotid");
    expect(lotid != nullptr && lotid->size() == lots.faces().size(), "lotid primitive attr");
    expect(lots.groups().members(geometry::GroupDomain::Face, "lots").size() ==
               lots.faces().size(),
           "lots face group");
}

void test_longest_edge_create_grid_mesh()
{
    // Demo graph default: CreateGridMesh xz + LotSubdivision longestEdge.
    // Regression: cut normal was 90° wrong, so bipartition always failed → 1 lot.
    LotSubdivisionOptions options;
    options.min_size = 0.1;
    options.iterations = 3;
    options.irregularity = 0.35;
    options.seed = 2;
    options.alignment = "longestEdge";

    const auto ground = create_grid_geometry(48.0, 48.0, 1, 1, "xz");
    expect(ground.faces().size() == 1, "ground is one quad");
    const auto lots = lot_subdivide_geometry(ground, options);
    expect(lots.faces().size() == 8, "longestEdge iterations=3 should yield 8 lots");
    expect(lots.groups().members(geometry::GroupDomain::Face, "lots").size() == 8,
           "lots face group count matches");
}

void test_min_size_stops_cutting()
{
    LotSubdivisionOptions options;
    options.min_size = 100.0;
    options.iterations = 5;
    options.irregularity = 0.5;
    options.seed = 3;
    options.alignment = "longestEdge";

    const auto lots = lot_subdivide_geometry(make_ground_quad(8.0, 8.0), options);
    expect(lots.faces().size() == 1, "minSize above face size should keep one lot");
}

void test_irregularity_changes_layout()
{
    LotSubdivisionOptions base;
    base.min_size = 0.1;
    base.iterations = 3;
    base.seed = 9;
    base.alignment = "boundingBox";

    base.irregularity = 0.0;
    const auto regular = lot_subdivide_geometry(make_ground_quad(16.0, 16.0), base);
    base.irregularity = 1.0;
    const auto irregular = lot_subdivide_geometry(make_ground_quad(16.0, 16.0), base);
    expect(regular.faces().size() == irregular.faces().size(),
           "irregularity should not change lot count for same iterations");

    bool differ = false;
    for (size_t i = 0; i < regular.points().size() && i < irregular.points().size(); ++i) {
        const auto& a = regular.points()[i];
        const auto& b = irregular.points()[i];
        if (std::abs(a.x - b.x) > 1.0e-6 || std::abs(a.z - b.z) > 1.0e-6) {
            differ = true;
            break;
        }
    }
    expect(differ || regular.points().size() != irregular.points().size(),
           "irregularity should change cut placement");
}

void test_poly_extrude_chain()
{
    LotSubdivisionOptions options;
    options.min_size = 0.5;
    options.iterations = 2;
    options.irregularity = 0.2;
    options.seed = 5;
    options.alignment = "boundingBox";
    const auto lots = lot_subdivide_geometry(make_ground_quad(12.0, 12.0), options);

    PolyExtrudeOptions extrude;
    extrude.distance = 2.0;
    const auto extruded = poly_extrude_geometry(lots, extrude);
    expect(extruded.faces().size() > lots.faces().size(),
           "LotSubdivision lots should extrude into buildings/pads");
}

void test_poly_extrude_discards_unselected()
{
    // Pad: extrude all lot faces, then extrude only tops into a building shell.
    LotSubdivisionOptions lot_opts;
    lot_opts.min_size = 0.5;
    lot_opts.iterations = 1;
    lot_opts.irregularity = 0.0;
    lot_opts.seed = 1;
    lot_opts.alignment = "boundingBox";
    const auto lots = lot_subdivide_geometry(make_ground_quad(8.0, 8.0), lot_opts);

    PolyExtrudeOptions pad;
    pad.distance = 0.2;
    pad.keep_original = false;
    pad.top_group = "extrude_top";
    pad.side_group = "extrude_side";
    const auto pads = poly_extrude_geometry(lots, pad);
    const auto top_count =
        pads.groups().members(geometry::GroupDomain::Face, "extrude_top").size();
    expect(top_count >= 1, "pads expose extrude_top faces");

    PolyExtrudeOptions building;
    building.face_group = "extrude_top";
    building.distance = 2.0;
    building.inset = 0.1;
    building.keep_original = false;
    building.top_group = "bldg_top";
    building.side_group = "bldg_side";
    const auto buildings = poly_extrude_geometry(pads, building);

    expect(buildings.groups().members(geometry::GroupDomain::Face, "bldg_side").size() >= 3,
           "building shell has side faces");
    expect(buildings.groups().members(geometry::GroupDomain::Face, "bldg_top").size() >= 1,
           "building shell has top faces");
    // Unselected pad sides/bottoms must not remain when faceGroup is set.
    expect(buildings.groups().members(geometry::GroupDomain::Face, "extrude_side").empty(),
           "discard unselected: pad side group not carried into building-only output");
}

void test_graph_node()
{
    // Build ground via CreateGridMesh (Houdini Grid equivalent; single quad for lots).
    const auto document = nlohmann::json{
        {"version", "2.0"},
        {"nodes",
         nlohmann::json::array({
             {{"id", "grid"},
              {"type", "CreateGridMesh"},
              {"position", {{"x", 0.0}, {"y", 0.0}}},
              {"data",
               {{"sizeX", 20.0},
                {"sizeY", 20.0},
                {"rows", 1},
                {"cols", 1},
                {"plane", "xz"}}}},
             {{"id", "lots"},
              {"type", "LotSubdivision"},
              {"position", {{"x", 0.0}, {"y", 160.0}}},
              {"data",
               {{"minSize", 2.0},
                {"iterations", 2},
                {"irregularity", 0.0},
                {"seed", 1},
                {"alignment", "boundingBox"}}}},
             {{"id", "extrude"},
              {"type", "PolyExtrude"},
              {"position", {{"x", 0.0}, {"y", 320.0}}},
              {"data", {{"distance", 1.5}, {"inset", 0.0}, {"keepOriginal", false}}}},
             {{"id", "output"},
              {"type", "Output"},
              {"position", {{"x", 0.0}, {"y", 480.0}}},
              {"data", nlohmann::json::object()}},
         })},
        {"edges",
         nlohmann::json::array({
             {{"id", "e0"},
              {"source", "grid"},
              {"target", "lots"},
              {"sourceHandle", "out"},
              {"targetHandle", "in"}},
             {{"id", "e1"},
              {"source", "lots"},
              {"target", "extrude"},
              {"sourceHandle", "out"},
              {"targetHandle", "in"}},
             {{"id", "e2"},
              {"source", "extrude"},
              {"target", "output"},
              {"sourceHandle", "out"},
              {"targetHandle", "in"}},
         })},
    };

    const auto cooked = execute_geometry_graph(document);
    expect(cooked.faces().size() >= 4, "graph LotSubdivision+PolyExtrude yields multi-face mesh");
    expect(!cooked.points().empty(), "graph output has points");
}

void test_example_graphs()
{
    const std::filesystem::path root =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    for (const char* rel : {"examples/graphs/lot-extrude-demo.pcg", "examples/graphs/lot-city-demo.pcg"}) {
        const auto path = root / rel;
        std::ifstream in(path);
        expect(static_cast<bool>(in), std::string("open example: ") + path.string());
        const std::string json((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        char error[1024] = {};
        Graph graph;
        expect(parse_graph(json.c_str(), graph, error, sizeof(error)) == PCG_OK,
               std::string(rel) + " parse: " + error);
        expect(validate_graph_structure(graph, error, sizeof(error)) == PCG_OK,
               std::string(rel) + " validate: " + error);
        GraphExecutionResult result;
        expect(execute_graph(graph, 17, result, error, sizeof(error)) == PCG_OK,
               std::string(rel) + " execute: " + error);
        expect(result.source_geometry != nullptr &&
                   !result.source_geometry->faces().empty() &&
                   !result.source_geometry->points().empty(),
               std::string(rel) + " should emit non-empty geometry");
        std::printf("%s: faces=%zu points=%zu\n",
                    rel,
                    result.source_geometry->faces().size(),
                    result.source_geometry->points().size());
    }
}

nlohmann::json load_example_json(const std::filesystem::path& root, const char* rel)
{
    const auto path = root / rel;
    std::ifstream in(path);
    expect(static_cast<bool>(in), std::string("open example: ") + path.string());
    return nlohmann::json::parse(in);
}

void shrink_lot_params(nlohmann::json& document, int iterations, double min_size)
{
    for (auto& node : document.at("nodes")) {
        if (node.value("type", "") != "LotSubdivision")
            continue;
        auto& data = node["data"];
        data["iterations"] = iterations;
        data["minSize"] = min_size;
        data["irregularity"] = 0.0;
        data["seed"] = 11;
    }
}

size_t count_node_type(const nlohmann::json& document, const std::string& type)
{
    size_t count = 0;
    auto count_nodes = [&](const nlohmann::json& nodes) {
        for (const auto& node : nodes) {
            if (node.value("type", "") == type)
                ++count;
        }
    };
    count_nodes(document.at("nodes"));
    if (document.contains("subgraphs")) {
        for (const auto& sg : document.at("subgraphs")) {
            if (sg.contains("nodes"))
                count_nodes(sg.at("nodes"));
        }
    }
    return count;
}

size_t count_root_node_type(const nlohmann::json& document, const std::string& type)
{
    size_t count = 0;
    for (const auto& node : document.at("nodes")) {
        if (node.value("type", "") == type)
            ++count;
    }
    return count;
}

void test_building_asset_wrapper(const std::filesystem::path& root, const char* rel)
{
    auto definition = load_example_json(root, rel);
    definition["id"] = "building_asset";
    const nlohmann::json document = {
        {"version", "1.0"},
        {"nodes",
         nlohmann::json::array({
             {{"id", "asset"},
              {"type", "Subgraph"},
              {"data", {{"subgraphId", "building_asset"}}}},
             {{"id", "out"}, {"type", "Output"}, {"data", nlohmann::json::object()}},
         })},
        {"edges",
         nlohmann::json::array({
             {{"id", "asset_out"},
              {"source", "asset"},
              {"target", "out"},
              {"sourceHandle", "mesh"},
              {"targetHandle", "in"}},
         })},
        {"subgraphs", nlohmann::json::array({std::move(definition)})},
    };

    const auto result = execute_graph_document(document);
    expect(result.kind == GraphResultKind::Mesh, std::string(rel) + " wrapper returns Mesh");
    expect(!result.mesh.vertices().empty() && result.mesh.triangles().size() >= 3,
           std::string(rel) + " wrapper cooks non-empty mesh");

    double min_y = result.mesh.vertices().front().y;
    for (const auto& vertex : result.mesh.vertices())
        min_y = std::min(min_y, vertex.y);
    expect(std::abs(min_y) < 1.0e-6, std::string(rel) + " mesh base is Y=0");

    const std::set<std::string> allowed = {
        "brick", "plaster_cream", "plaster_teal", "trim", "window_glow",
        "roof_tile", "roof_green",
    };
    expect(result.mesh.has_materials(), std::string(rel) + " has material slots");
    for (const auto& slot : result.mesh.material_slots())
        expect(allowed.count(slot) != 0, std::string(rel) + " uses allowed material: " + slot);
}

void test_building_classification_and_scale_micrograph()
{
    const nlohmann::json document = {
        {"version", "1.0"},
        {"nodes",
         nlohmann::json::array({
             {{"id", "sites"}, {"type", "CreatePoints"}, {"data", {{"count", 18}}}},
             {{"id", "sizes"},
              {"type", "AttributeWrangle"},
              {"data", {{"runOver", "points"},
                         {"expression",
                          "@rotationY = (@ptnum % 2) * 90.0; "
                          "@wantWide = @ptnum >= 9; "
                          "@targetX = 6.0 + 3.0 * @wantWide; @targetZ = 6.0; "
                          "@odd = abs(round(@rotationY / 90.0)) % 2; "
                          "@lotSizeX = (1 - @odd) * @targetX + @odd * @targetZ; "
                          "@lotSizeZ = (1 - @odd) * @targetZ + @odd * @targetX;"}}}},
             {{"id", "classify"},
              {"type", "AttributeWrangle"},
              {"data", {{"runOver", "points"},
                         {"expression",
                          "@bucket = @ptnum % 9; "
                          "@btype = (@bucket >= 2) + (@bucket >= 7); "
                          "@odd = abs(round(@rotationY / 90.0)) % 2; "
                          "@sxLot = (1 - @odd) * @lotSizeX + @odd * @lotSizeZ; "
                          "@szLot = (1 - @odd) * @lotSizeZ + @odd * @lotSizeX; "
                          "@wide = @sxLot >= 1.35 * @szLot; "
                          "@variant = @btype * 2 + @wide;"}}}},
             {{"id", "scale"},
              {"type", "AttributeWrangle"},
              {"data", {{"runOver", "points"},
                         {"expression",
                          "@protoX = (@variant == 0) * 3.7 + (@variant == 1) * 6.0 + "
                          "(@variant == 2) * 3.8 + (@variant == 3) * 6.2 + "
                          "(@variant == 4) * 4.5 + (@variant == 5) * 6.5; "
                          "@protoZ = (@variant == 0) * 3.7 + (@variant == 1) * 3.7 + "
                          "(@variant == 2) * 3.8 + (@variant == 3) * 3.8 + "
                          "(@variant == 4) * 4.5 + (@variant == 5) * 4.2; "
                          "@s = min(@sxLot * 0.88 / max(@protoX, 0.001), "
                          "@szLot * 0.88 / max(@protoZ, 0.001)); "
                          "@scaleX = @s; @scaleY = 1.0; @scaleZ = @s;"}}}},
             {{"id", "out"}, {"type", "Output"}, {"data", nlohmann::json::object()}},
         })},
        {"edges",
         nlohmann::json::array({
             {{"source", "sites"}, {"target", "sizes"}},
             {{"source", "sizes"}, {"target", "classify"}},
             {{"source", "classify"}, {"target", "scale"}},
             {{"source", "scale"}, {"target", "out"}},
         })},
    };

    const auto result = execute_graph_document(document);
    expect(result.kind == GraphResultKind::Points && result.points != nullptr,
           "classification micrograph returns points");
    expect(result.points->points().size() == 18, "classification preserves 18 points");
    std::array<int, 6> variant_counts{};
    for (const auto& point : result.points->points()) {
        expect(point.attributes.contains("rotationY") && point.attributes["rotationY"].is_number(),
               "classification point has rotation");
        expect(point.attributes.contains("scaleX") && point.attributes.contains("scaleY") &&
                   point.attributes.contains("scaleZ"),
               "classification point has axis scales");
        const double sx = point.attributes.value("scaleX", 0.0);
        const double sy = point.attributes.value("scaleY", 0.0);
        const double sz = point.attributes.value("scaleZ", 0.0);
        expect(std::abs(sx - sz) < 1.0e-6 && std::abs(sy - 1.0) < 1.0e-6,
               "classification scale is horizontal-isotropic with Y=1");
        const int variant = point.attributes.value("variant", -1);
        expect(variant >= 0 && variant < 6, "classification variant is in range");
        ++variant_counts[static_cast<size_t>(variant)];
    }
    expect(variant_counts == std::array<int, 6>({2, 2, 5, 5, 2, 2}),
           "classification reaches six variants with 2:5:2 square/wide buckets");
    const uint32_t flags = detect_point_attr_flags(*result.points);
    expect((flags & PCG_POINT_ATTR_ROTATION) != 0,
           "classification binary advertises rotation");
    expect((flags & PCG_POINT_ATTR_SCALE) != 0,
           "classification binary advertises scale");
}

bool has_foreach(const nlohmann::json& document)
{
    return count_node_type(document, "ForEachBegin") > 0 ||
           count_node_type(document, "ForEachEnd") > 0;
}

void test_instanced_city_example_graphs()
{
    const std::filesystem::path root =
        std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();

    // --- Buildings: multi-prototype GPU Points via MergeSpawnPoints ---
    {
        auto document = load_example_json(root, "examples/graphs/lot-city-buildings-instanced.pcg");
        test_building_classification_and_scale_micrograph();
        expect(!has_foreach(document), "buildings graph must not use ForEach");
        expect(count_root_node_type(document, "StaticMeshSpawner") == 6,
               "buildings must spawn six square/wide prototypes");
        expect(count_root_node_type(document, "Blast") == 7,
               "buildings have one road blast plus six variant blasts");
        expect(count_node_type(document, "MergeSpawnPoints") == 1,
               "buildings must merge spawn streams");
        expect(count_root_node_type(document, "AttributeWrangle") >= 2,
               "buildings must tag types and face roads via AttributeWrangle");
        bool has_face_road = false;
        for (const auto& node : document.at("nodes")) {
            if (node.value("id", "") == "face_road") {
                has_face_road = true;
                break;
            }
        }
        expect(has_face_road, "buildings must include face_road wrangle");
        expect(count_root_node_type(document, "CopyMeshToPoints") == 0,
               "buildings root must not bake city via CopyMeshToPoints");
        expect(count_root_node_type(document, "MergeMesh") == 0,
               "buildings root must not MergeMesh the city");
        expect(count_root_node_type(document, "SubgraphAsset") == 6,
               "buildings use six linked SubgraphAsset prototypes (Unity bakes before cook)");

        std::set<int> variant_lanes;
        for (const auto& node : document.at("nodes")) {
            if (node.value("type", "") != "Blast")
                continue;
            const std::string expression = node.value("data", nlohmann::json::object())
                                               .value("expression", "");
            const std::string marker = "@variant != ";
            const auto pos = expression.find(marker);
            if (pos == std::string::npos)
                continue;
            variant_lanes.insert(std::stoi(expression.substr(pos + marker.size())));

            bool feeds_spawner = false;
            for (const auto& edge : document.at("edges")) {
                if (edge.value("source", "") != node.value("id", ""))
                    continue;
                for (const auto& target : document.at("nodes")) {
                    if (target.value("id", "") == edge.value("target", "") &&
                        target.value("type", "") == "StaticMeshSpawner")
                        feeds_spawner = true;
                }
            }
            expect(feeds_spawner, "each variant Blast feeds a StaticMeshSpawner");
        }
        expect(variant_lanes == std::set<int>({0, 1, 2, 3, 4, 5}),
               "variant Blast lanes cover six variants");

        // Micrograph: AttributeWrangle faces nearest road and emits rotationY for point binary.
        {
            const nlohmann::json face_doc = {
                {"version", "1.0"},
                {"nodes",
                 nlohmann::json::array({
                     {{"id", "grid"},
                      {"type", "CreatePointGrid"},
                      {"data",
                       {{"pointCountX", 2},
                        {"pointCountY", 2},
                        {"spacing", 10.0},
                        {"__nodeTitle", "Sites"}}}},
                     {{"id", "face_road"},
                      {"type", "AttributeWrangle"},
                      {"data",
                       {{"runOver", "points"},
                        {"expression",
                         "@cxA = clamp(@P.x, chf(\"roadAMinX\"), chf(\"roadAMaxX\")); "
                         "@czA = chf(\"roadAz\"); @cxB = chf(\"roadBx\"); "
                         "@czB = clamp(@P.z, chf(\"roadBMinZ\"), chf(\"roadBMaxZ\")); "
                         "@dxA = @cxA - @P.x; @dzA = @czA - @P.z; "
                         "@dxB = @cxB - @P.x; @dzB = @czB - @P.z; "
                         "@dA2 = @dxA * @dxA + @dzA * @dzA; "
                         "@dB2 = @dxB * @dxB + @dzB * @dzB; "
                         "@useA = @dA2 <= @dB2; "
                         "@fx = @useA * @dxA + (1 - @useA) * @dxB; "
                         "@fz = @useA * @dzA + (1 - @useA) * @dzB; "
                         "@rotationY = atan2(@fx, @fz) * 180.0 / PI;"},
                        {"parameters",
                         "{\"roadAz\":-8,\"roadBx\":6,\"roadAMinX\":-22,\"roadAMaxX\":22,"
                         "\"roadBMinZ\":-22,\"roadBMaxZ\":22}"},
                        {"__nodeTitle", "Face Nearest Road"}}}},
                     {{"id", "out"},
                      {"type", "Output"},
                      {"data", {{"__nodeTitle", "Out"}}}},
                 })},
                {"edges",
                 nlohmann::json::array({
                     {{"id", "e0"},
                      {"source", "grid"},
                      {"target", "face_road"},
                      {"sourceHandle", "out"},
                      {"targetHandle", "in"}},
                     {{"id", "e1"},
                      {"source", "face_road"},
                      {"target", "out"},
                      {"sourceHandle", "out"},
                      {"targetHandle", "in"}},
                 })},
            };
            char error[1024] = {};
            Graph graph;
            const std::string json = face_doc.dump();
            expect(parse_graph(json.c_str(), graph, error, sizeof(error)) == PCG_OK,
                   std::string("face_road parse: ") + error);
            expect(validate_graph_structure(graph, error, sizeof(error)) == PCG_OK,
                   std::string("face_road validate: ") + error);
            GraphExecutionResult result;
            expect(execute_graph(graph, 17, result, error, sizeof(error)) == PCG_OK,
                   std::string("face_road execute: ") + error);
            expect(result.points != nullptr && result.points->points().size() == 4,
                   "face_road micrograph emits 4 points");
            for (const auto& point : result.points->points()) {
                expect(point.attributes.contains("rotationY") &&
                           point.attributes["rotationY"].is_number(),
                       "face_road writes rotationY");
            }
            const uint32_t flags = detect_point_attr_flags(*result.points);
            expect((flags & PCG_POINT_ATTR_ROTATION) != 0,
                   "point binary advertises rotation from rotationY");
            std::vector<uint8_t> buffer(64 * 1024);
            uint32_t written_flags = 0;
            expect(write_point_binary(*result.points, buffer.data(),
                                      static_cast<int>(buffer.size()), &written_flags),
                   "write_point_binary with rotation succeeds");
            expect((written_flags & PCG_POINT_ATTR_ROTATION) != 0,
                   "written flags include rotation");
            std::printf("lot-city-buildings-instanced: structural ok; "
                        "face_road micrograph points=%zu\n",
                        result.points->points().size());
        }

        for (const char* asset : {
                 "Unity/Assets/Samples/PICG/Demos/GraphGallery/Graphs/lot-city-demo/subgraphs/building_tall_flat.pcgsubgraph",
                 "Unity/Assets/Samples/PICG/Demos/GraphGallery/Graphs/lot-city-demo/subgraphs/building_tall_flat_wide.pcgsubgraph",
                 "Unity/Assets/Samples/PICG/Demos/GraphGallery/Graphs/lot-city-demo/subgraphs/building_medium_pitched.pcgsubgraph",
                 "Unity/Assets/Samples/PICG/Demos/GraphGallery/Graphs/lot-city-demo/subgraphs/building_medium_pitched_wide.pcgsubgraph",
                 "Unity/Assets/Samples/PICG/Demos/GraphGallery/Graphs/lot-city-demo/subgraphs/building_short_flat.pcgsubgraph",
                 "Unity/Assets/Samples/PICG/Demos/GraphGallery/Graphs/lot-city-demo/subgraphs/building_short_flat_wide.pcgsubgraph",
             })
            test_building_asset_wrapper(root, asset);
    }
}

void test_pad_bevel_multi_lot_topology()
{
    PcgGeometry ground;
    const double half = 24.0;
    ground.points_mut() = {{-half, 0.0, -half},
                           {half, 0.0, -half},
                           {half, 0.0, half},
                           {-half, 0.0, half}};
    ground.faces_mut() = {{0, 3, 2, 1}};

    LotSubdivisionOptions lot_opts;
    lot_opts.min_size = 4.0;
    lot_opts.iterations = 4;
    lot_opts.irregularity = 0.4512821;
    lot_opts.seed = 11;
    lot_opts.alignment = "boundingBox";
    const auto lots = lot_subdivide_geometry(ground, lot_opts);

    PolyExtrudeOptions extrude;
    extrude.distance = 0.18;
    extrude.inset = 0.08;
    extrude.keep_original = false;
    extrude.top_group = "extrude_top";
    extrude.side_group = "extrude_side";
    const auto pads = poly_extrude_geometry(lots, extrude);

    GroupCreateOptions rim_opts;
    rim_opts.output_group = "pad_rim";
    rim_opts.domain = "edge";
    rim_opts.mode = "angle";
    rim_opts.min_edge_angle_deg = 30.0;
    rim_opts.from_face_groups = {"extrude_top"};
    const auto rim = group_create(pads, rim_opts);

    BevelEdgeSelection selection;
    selection.edge_group = "pad_rim";
    selection.exclude_unshared = true;
    selection.limit_method_explicit = true;
    selection.limit_method = bevel::BevelLimitMethod::None;

    const auto beveled = bevel_geometry(
        rim, 0.04, 2, BevelMethod::Edge, BevelOffsetType::Offset, true, 30.0, 0.5f,
        BevelMiter::Sharp, BevelMiter::Sharp, BevelVMeshMethod::Adj, nullptr, selection);

    auto max_edge_of = [](const PcgGeometry& geo) {
        const auto tri = triangulate_geometry_shared(geo);
        double max_edge = 0.0;
        for (size_t i = 0; i + 2 < tri.triangles().size(); i += 3) {
            const auto& a = tri.vertices()[static_cast<size_t>(tri.triangles()[i])];
            const auto& b = tri.vertices()[static_cast<size_t>(tri.triangles()[i + 1])];
            const auto& c = tri.vertices()[static_cast<size_t>(tri.triangles()[i + 2])];
            const auto edge_len = [&](const PcgVertex& p, const PcgVertex& q) {
                const double dx = p.x - q.x;
                const double dy = p.y - q.y;
                const double dz = p.z - q.z;
                return std::sqrt(dx * dx + dy * dy + dz * dz);
            };
            max_edge = std::max(max_edge, edge_len(a, b));
            max_edge = std::max(max_edge, edge_len(b, c));
            max_edge = std::max(max_edge, edge_len(c, a));
        }
        return max_edge;
    };

    const double input_max_edge = max_edge_of(rim);
    const double max_edge = max_edge_of(beveled);
    expect(beveled.faces().size() > pads.faces().size(),
           "multi-lot pad bevel should add faces");
    expect(max_edge <= input_max_edge * 1.05 + 1e-3,
           "multi-lot pad bevel should not create city-spanning triangle edges");
}

void test_float_seed_param()
{
    expect(normalize_seed_number(2.0) == 2, "whole float seed 2.0 keeps integer identity");
    expect(normalize_seed_number(-3.0) == -3, "whole float seed -3.0 keeps integer identity");
    expect(normalize_seed_number(2.3) != 2, "fractional seed 2.3 must differ from 2");
    expect(normalize_seed_number(2.3) != normalize_seed_number(2.4),
           "nearby fractional seeds must diverge");
    expect(rng_state_from_seed(2.3, 0) != rng_state_from_seed(2.0, 0),
           "rng state must differ for 2.3 vs 2.0");

    // Regression: nlohmann value("seed", 0) truncates float → int (2.3 becomes 2).
    const nlohmann::json data{{"seed", 2.3}};
    expect(std::abs(read_seed_param_number(data, "seed", 0.0) - 2.3) < 1e-12,
           "read_seed_param_number must keep fractional seed");
    expect(data.value("seed", 0) == 2,
           "sanity: nlohmann value<int> truncates 2.3 → 2 (the bug we avoid)");

    LotSubdivisionOptions a;
    a.min_size = 1.0;
    a.iterations = 3;
    a.irregularity = 0.35;
    a.seed = 2.0;
    a.alignment = "longestEdge";
    LotSubdivisionOptions b = a;
    b.seed = 2.3;

    const auto lots_a = lot_subdivide_geometry(make_ground_quad(20.0, 20.0), a);
    const auto lots_b = lot_subdivide_geometry(make_ground_quad(20.0, 20.0), b);
    expect(lots_a.faces().size() >= 2 && lots_b.faces().size() >= 2,
           "float-seed lots should subdivide");

    bool layout_differs = lots_a.faces().size() != lots_b.faces().size();
    if (!layout_differs) {
        const auto& pa = lots_a.points();
        const auto& pb = lots_b.points();
        if (pa.size() != pb.size()) {
            layout_differs = true;
        } else {
            for (size_t i = 0; i < pa.size(); ++i) {
                if (std::abs(pa[i].x - pb[i].x) > 1e-6 ||
                    std::abs(pa[i].z - pb[i].z) > 1e-6) {
                    layout_differs = true;
                    break;
                }
            }
        }
    }
    expect(layout_differs, "seed 2.3 should change lot layout vs seed 2.0");
}

void test_graph_float_seed_cook()
{
    auto make_doc = [](double seed) {
        return nlohmann::json{
            {"version", "2.0"},
            {"nodes",
             nlohmann::json::array({
                 {{"id", "grid"},
                  {"type", "CreateGridMesh"},
                  {"position", {{"x", 0.0}, {"y", 0.0}}},
                  {"data",
                   {{"sizeX", 20.0},
                    {"sizeY", 20.0},
                    {"rows", 1},
                    {"cols", 1},
                    {"plane", "xz"}}}},
                 {{"id", "lots"},
                  {"type", "LotSubdivision"},
                  {"position", {{"x", 0.0}, {"y", 160.0}}},
                  {"data",
                   {{"minSize", 0.1},
                    {"iterations", 4},
                    {"irregularity", 0.35},
                    {"seed", seed},
                    {"alignment", "longestEdge"}}}},
                 {{"id", "output"},
                  {"type", "Output"},
                  {"position", {{"x", 0.0}, {"y", 320.0}}},
                  {"data", nlohmann::json::object()}},
             })},
            {"edges",
             nlohmann::json::array({
                 {{"id", "e0"},
                  {"source", "grid"},
                  {"target", "lots"},
                  {"sourceHandle", "out"},
                  {"targetHandle", "in"}},
                 {{"id", "e1"},
                  {"source", "lots"},
                  {"target", "output"},
                  {"sourceHandle", "out"},
                  {"targetHandle", "in"}},
             })},
        };
    };

    auto cook = [](const nlohmann::json& doc) {
        const std::string json = doc.dump();
        Graph graph;
        char error[1024] = {};
        expect(parse_graph(json.c_str(), graph, error, sizeof(error)) == PCG_OK,
               std::string("float-seed parse: ") + error);
        GraphExecutionResult result;
        expect(execute_graph(graph, 0, result, error, sizeof(error)) == PCG_OK,
               std::string("float-seed execute: ") + error);
        expect(result.source_geometry != nullptr, "float-seed cook emits geometry");
        return *result.source_geometry;
    };

    const auto geo_a = cook(make_doc(2.0));
    const auto geo_b = cook(make_doc(2.3));
    expect(!geo_a.points().empty() && !geo_b.points().empty(), "float-seed cooks have points");

    bool differs = geo_a.faces().size() != geo_b.faces().size() ||
                   geo_a.points().size() != geo_b.points().size();
    if (!differs) {
        const auto& pa = geo_a.points();
        const auto& pb = geo_b.points();
        for (size_t i = 0; i < pa.size(); ++i) {
            if (std::abs(pa[i].x - pb[i].x) > 1e-6 || std::abs(pa[i].z - pb[i].z) > 1e-6) {
                differs = true;
                break;
            }
        }
    }
    expect(differs, "graph cook seed 2.3 must differ from seed 2.0 (not truncated)");
}

} // namespace

int main()
{
    test_rectangle_iterations();
    test_longest_edge_create_grid_mesh();
    test_min_size_stops_cutting();
    test_irregularity_changes_layout();
    test_poly_extrude_chain();
    test_poly_extrude_discards_unselected();
    test_graph_node();
    test_example_graphs();
    test_instanced_city_example_graphs();
    test_pad_bevel_multi_lot_topology();
    test_float_seed_param();
    test_graph_float_seed_cook();
    std::printf("test_lot_subdivision: OK\n");
    return 0;
}
