#include "elements/mesh_algorithms.hpp"
#include "graph_execution_result.hpp"
#include "graph_executor.hpp"
#include "graph_parser.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
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

void expect_normal_axis(const PcgGeometry& geo, char axis, double sign)
{
    expect(!geo.faces().empty() && geo.faces()[0].size() >= 3, "face exists");
    const auto& f = geo.faces()[0];
    const auto& p = geo.points();
    const auto& a = p[static_cast<size_t>(f[0])];
    const auto& b = p[static_cast<size_t>(f[1])];
    const auto& c = p[static_cast<size_t>(f[2])];
    const double ux = b.x - a.x, uy = b.y - a.y, uz = b.z - a.z;
    const double vx = c.x - a.x, vy = c.y - a.y, vz = c.z - a.z;
    const double nx = uy * vz - uz * vy;
    const double ny = uz * vx - ux * vz;
    const double nz = ux * vy - uy * vx;
    const double component = axis == 'x' ? nx : axis == 'y' ? ny : nz;
    expect(component * sign > 0.0, "grid face normal points outward");
}

void test_single_quad_xz()
{
    const auto geo = create_grid_geometry(48.0, 48.0, 1, 1, "xz");
    expect(geo.points().size() == 4, "1x1 grid has 4 points");
    expect(geo.faces().size() == 1, "1x1 grid has 1 face");
    expect(geo.faces()[0].size() == 4, "grid face is a quad");
    expect(std::abs(geo.points()[0].y) < 1e-9, "XZ grid lies on Y=0");
    expect_normal_axis(geo, 'y', 1.0);
}

void test_subdivided_grid()
{
    const auto geo = create_grid_geometry(10.0, 10.0, 4, 4, "xz");
    expect(geo.points().size() == 25, "4x4 grid has 5x5 points");
    expect(geo.faces().size() == 16, "4x4 grid has 16 quads");
}

void test_plane_orientations()
{
    const auto xy = create_grid_geometry(2.0, 3.0, 2, 2, "xy");
    expect(xy.points().size() == 9, "XY grid point count");
    expect(std::abs(xy.points()[0].z) < 1e-9, "XY grid lies on Z=0");
    expect_normal_axis(xy, 'z', 1.0);

    const auto yz = create_grid_geometry(2.0, 3.0, 2, 2, "yz");
    expect(yz.points().size() == 9, "YZ grid point count");
    expect(std::abs(yz.points()[0].x) < 1e-9, "YZ grid lies on X=0");
    expect_normal_axis(yz, 'x', 1.0);
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

void test_graph_node()
{
    const auto document = nlohmann::json{
        {"version", "2.0"},
        {"nodes",
         nlohmann::json::array({
             {{"id", "grid"},
              {"type", "CreateGridMesh"},
              {"position", {{"x", 0.0}, {"y", 0.0}}},
              {"data", {{"sizeX", 20.0}, {"sizeY", 20.0}, {"rows", 1}, {"cols", 1}, {"plane", "xz"}}}},
             {{"id", "output"},
              {"type", "Output"},
              {"position", {{"x", 0.0}, {"y", 160.0}}},
              {"data", nlohmann::json::object()}},
         })},
        {"edges",
         nlohmann::json::array({
             {{"id", "e0"},
              {"source", "grid"},
              {"target", "output"},
              {"sourceHandle", "out"},
              {"targetHandle", "in"}},
         })},
    };

    const auto geo = execute_geometry_graph(document);
    expect(geo.faces().size() == 1, "CreateGridMesh graph yields one quad");
}

} // namespace

int main()
{
    test_single_quad_xz();
    std::printf("PASS: create_grid_geometry single quad\n");
    test_subdivided_grid();
    std::printf("PASS: create_grid_geometry subdivided\n");
    test_plane_orientations();
    std::printf("PASS: create_grid_geometry plane orientations\n");
    test_graph_node();
    std::printf("PASS: CreateGridMesh graph execution\n");
    std::printf("ALL CreateGridMesh TESTS PASSED\n");
    return 0;
}
