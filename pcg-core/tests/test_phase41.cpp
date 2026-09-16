#include "pcg_api.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

void expect_code(PcgResultCode actual, PcgResultCode expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL: %s expected %d got %d\n", label, static_cast<int>(expected), static_cast<int>(actual));
        std::exit(1);
    }
}

} // namespace

int main()
{
    char err[512] = {};
    char out[65536] = {};

    const char* terrain_pipeline = R"({
      "version": "1.0",
      "nodes": [
        {"id": "terrain", "type": "GetTerrainData", "position": {"x":0,"y":0},
         "data": {"gridSize": 4, "cellSize": 2.0, "amplitude": 3.0, "seed": 11}},
        {"id": "grid", "type": "CreatePointGrid", "position": {"x":0,"y":0},
         "data": {"pointCountX": 3, "pointCountY": 3, "spacing": 2.0}},
        {"id": "sample", "type": "SampleSurface", "position": {"x":0,"y":0}, "data": {}},
        {"id": "spawn", "type": "StaticMeshSpawner", "position": {"x":0,"y":0},
         "data": {"prefab": "Pine", "mesh": "SM_Pine", "scale": 1.5}}
      ],
      "edges": [
        {"id": "e1", "source": "grid", "target": "sample", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e2", "source": "terrain", "target": "sample", "sourceHandle": "out", "targetHandle": "terrain"},
        {"id": "e3", "source": "sample", "target": "spawn", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    expect_code(pcg_validate_graph(terrain_pipeline, err, sizeof(err)), PCG_OK, "phase41 pipeline validate");
    expect_code(pcg_execute_graph(terrain_pipeline, 42, out, sizeof(out)), PCG_OK, "phase41 pipeline execute");
    assert(std::strstr(out, "\"pointCount\"") != nullptr);
    assert(std::strstr(out, "\"prefab\"") != nullptr);
    assert(std::strstr(out, "\"attributes\"") != nullptr);
    std::printf("PASS: terrain + grid + sample + spawner pipeline\n");

    const char* filter_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "grid", "type": "CreatePointGrid", "position": {"x":0,"y":0},
         "data": {"pointCountX": 4, "pointCountY": 4, "spacing": 1.0}},
        {"id": "attrs", "type": "CopyAttributes", "position": {"x":0,"y":0},
         "data": {"attributeNames": ["tag"], "tag": "rock"}},
        {"id": "filter", "type": "AttributeFilter", "position": {"x":0,"y":0},
         "data": {"attributeName": "tag", "matchValue": "rock"}},
        {"id": "density", "type": "DensityFilter", "position": {"x":0,"y":0},
         "data": {"density": 1.0}}
      ],
      "edges": [
        {"id": "e1", "source": "grid", "target": "attrs"},
        {"id": "e2", "source": "attrs", "target": "filter"},
        {"id": "e3", "source": "filter", "target": "density"}
      ]
    })";

    expect_code(pcg_execute_graph(filter_graph, 7, out, sizeof(out)), PCG_OK, "metadata filter pipeline");
    assert(std::strstr(out, "\"points\"") != nullptr);
    std::printf("PASS: metadata + filter pipeline\n");

    return 0;
}
