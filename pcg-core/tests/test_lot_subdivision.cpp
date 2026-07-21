#include "elements/geometry_algorithms.hpp"
#include "elements/lot_subdivision_algorithms.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/vehicle_modeling_algorithms.hpp"
#include "graph_execution_result.hpp"
#include "graph_executor.hpp"
#include "graph_parser.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    for (const char* rel : {"examples/lot-extrude-demo.pcg", "examples/lot-city-demo.pcg"}) {
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

} // namespace

int main()
{
    test_rectangle_iterations();
    test_min_size_stops_cutting();
    test_irregularity_changes_layout();
    test_poly_extrude_chain();
    test_poly_extrude_discards_unselected();
    test_graph_node();
    test_example_graphs();
    test_pad_bevel_multi_lot_topology();
    std::printf("test_lot_subdivision: OK\n");
    return 0;
}
