#include "pcg_api.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

void expect_code(PcgResultCode actual, PcgResultCode expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL: %s expected %d got %d\n", label, static_cast<int>(expected), static_cast<int>(actual));
        std::exit(1);
    }
}

std::string read_file(const char* path)
{
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

} // namespace

int main()
{
    char err[512] = {};
    char out[65536] = {};

    const char* hull_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "grid", "type": "CreatePointGrid", "position": {"x":0,"y":0},
         "data": {"pointCountX": 4, "pointCountY": 4, "spacing": 2.0}},
        {"id": "hull", "type": "ConvexHull", "position": {"x":0,"y":0}, "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "grid", "target": "hull", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    expect_code(pcg_validate_graph(hull_graph, err, sizeof(err)), PCG_OK, "convex hull validate");
    expect_code(pcg_execute_graph(hull_graph, 42, out, sizeof(out)), PCG_OK, "convex hull execute");
    assert(std::strstr(out, "\"splines\"") != nullptr);
    assert(std::strstr(out, "\"closed\":true") != nullptr);
    std::printf("PASS: convex hull pipeline\n");

    const char* connect_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "pts", "type": "CreatePoints", "position": {"x":0,"y":0},
         "data": {"x": 0, "y": 0, "z": 0, "count": 5, "jitter": 3.0}},
        {"id": "connect", "type": "ConnectNearest", "position": {"x":0,"y":0},
         "data": {"k": 2, "maxDistance": -1}}
      ],
      "edges": [
        {"id": "e1", "source": "pts", "target": "connect"}
      ]
    })";

    expect_code(pcg_execute_graph(connect_graph, 7, out, sizeof(out)), PCG_OK, "connect nearest execute");
    assert(std::strstr(out, "\"splines\"") != nullptr);
    std::printf("PASS: connect nearest pipeline\n");

    const char* delaunay_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "pts", "type": "CreatePoints", "position": {"x":0,"y":0},
         "data": {"x": 0, "y": 0, "z": 0, "count": 8, "jitter": 5.0}},
        {"id": "delaunay", "type": "Delaunay", "position": {"x":0,"y":0}, "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "pts", "target": "delaunay"}
      ]
    })";

    expect_code(pcg_execute_graph(delaunay_graph, 11, out, sizeof(out)), PCG_OK, "delaunay execute");
    assert(std::strstr(out, "\"splines\"") != nullptr);
    std::printf("PASS: delaunay pipeline\n");

    const char* mst_astar_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "grid", "type": "CreatePointGrid", "position": {"x":0,"y":0},
         "data": {"pointCountX": 4, "pointCountY": 4, "spacing": 2.0}},
        {"id": "connect", "type": "ConnectNearest", "position": {"x":0,"y":0},
         "data": {"k": 2, "maxDistance": -1}},
        {"id": "mst", "type": "MST", "position": {"x":0,"y":0}, "data": {}},
        {"id": "path", "type": "AStarPathfinding", "position": {"x":0,"y":0},
         "data": {"startIndex": 0, "endIndex": 15}}
      ],
      "edges": [
        {"id": "e1", "source": "grid", "target": "connect"},
        {"id": "e2", "source": "connect", "target": "mst", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e3", "source": "grid", "target": "mst", "sourceHandle": "out", "targetHandle": "points"},
        {"id": "e4", "source": "mst", "target": "path", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e5", "source": "grid", "target": "path", "sourceHandle": "out", "targetHandle": "points"}
      ]
    })";

    expect_code(pcg_validate_graph(mst_astar_graph, err, sizeof(err)), PCG_OK, "mst+astar validate");
    expect_code(pcg_execute_graph(mst_astar_graph, 42, out, sizeof(out)), PCG_OK, "mst+astar execute");
    assert(std::strstr(out, "\"splines\"") != nullptr);
    std::printf("PASS: mst + astar pipeline\n");

    const char* voronoi_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "pts", "type": "CreatePoints", "position": {"x":0,"y":0},
         "data": {"x": 0, "y": 0, "z": 0, "count": 6, "jitter": 4.0}},
        {"id": "voronoi", "type": "Voronoi", "position": {"x":0,"y":0}, "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "pts", "target": "voronoi"}
      ]
    })";

    expect_code(pcg_execute_graph(voronoi_graph, 3, out, sizeof(out)), PCG_OK, "voronoi execute");
    assert(std::strstr(out, "\"splines\"") != nullptr);
    std::printf("PASS: voronoi pipeline\n");

    const std::string demo_graph = read_file("../../examples/phase42-demo.pcg.json");
    assert(!demo_graph.empty());
    expect_code(pcg_validate_graph(demo_graph.c_str(), err, sizeof(err)), PCG_OK, "phase42 demo validate");
    expect_code(pcg_execute_graph(demo_graph.c_str(), 42, out, sizeof(out)), PCG_OK, "phase42 demo execute");
    std::printf("PASS: examples/phase42-demo.pcg.json\n");

    return 0;
}
