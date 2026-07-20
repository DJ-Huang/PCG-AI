#include "elements/lot_subdivision_algorithms.hpp"
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
    geo.faces_mut() = {{0, 1, 2, 3}};
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

void test_graph_node()
{
    // Build a single ground quad via CreateBoxMesh top is not ideal; use Import-free
    // direct algorithm coverage above and a minimal Output graph with CreateBoxMesh
    // filtered by large minSize on thin box is weak. Prefer cooking LotSubdivision
    // through a hand-built geometry source is not exposed — use CreateBoxMesh and
    // accept all faces: with iterations=1 and high minSize only large faces cut.
    const auto document = nlohmann::json{
        {"version", "2.0"},
        {"nodes",
         nlohmann::json::array({
             {{"id", "box"},
              {"type", "CreateBoxMesh"},
              {"position", {{"x", 0.0}, {"y", 0.0}}},
              {"data", {{"width", 20.0}, {"height", 0.01}, {"depth", 20.0}}}},
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
              {"source", "box"},
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

} // namespace

int main()
{
    test_rectangle_iterations();
    test_min_size_stops_cutting();
    test_irregularity_changes_layout();
    test_poly_extrude_chain();
    test_graph_node();
    test_example_graphs();
    std::printf("test_lot_subdivision: OK\n");
    return 0;
}
