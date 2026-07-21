#include "pcg_api.h"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string spawn_graph_json()
{
    return R"({
      "version": "1.0",
      "nodes": [
        { "id": "box", "type": "CreateBoxMesh", "data": { "width": 1, "height": 1, "depth": 1 } },
        { "id": "grid", "type": "CreatePointGrid", "data": { "pointCountX": 2, "pointCountY": 2, "spacing": 1 } },
        { "id": "spawner", "type": "StaticMeshSpawner", "data": { "scale": 1 } },
        { "id": "output", "type": "Output", "data": {} }
      ],
      "edges": [
        { "id": "e1", "source": "grid", "target": "spawner", "sourceHandle": "out", "targetHandle": "in" },
        { "id": "e2", "source": "box", "target": "spawner", "sourceHandle": "out", "targetHandle": "mesh" },
        { "id": "e3", "source": "spawner", "target": "output", "sourceHandle": "out", "targetHandle": "in" }
      ]
    })";
}

} // namespace

int main()
{
    const std::string json = spawn_graph_json();
    char err[1024] = {};
    int kind = 0;
    int point_count = 0;
    uint32_t flags = 0;
    int vertex_count = 0;
    int index_count = 0;
    std::vector<uint8_t> points_buf(1024 * 1024);
    std::vector<uint8_t> mesh_buf(1024 * 1024);
    char out_json[64] = {};

    const PcgResultCode rc = pcg_execute_graph_v6(
        json.c_str(),
        42,
        nullptr,
        0,
        nullptr,
        0,
        &kind,
        out_json,
        sizeof(out_json),
        mesh_buf.data(),
        static_cast<int>(mesh_buf.size()),
        points_buf.data(),
        static_cast<int>(points_buf.size()),
        &point_count,
        &flags,
        &vertex_count,
        &index_count,
        nullptr,
        nullptr,
        0,
        err,
        sizeof(err));

    if (rc != PCG_OK) {
        std::fprintf(stderr, "FAIL: pcg_execute_graph_v6: %s\n", err);
        return 1;
    }
    if (kind != PCG_RESULT_KIND_POINTS) {
        std::fprintf(stderr, "FAIL: expected Points kind, got %d\n", kind);
        return 1;
    }
    if (point_count != 4) {
        std::fprintf(stderr, "FAIL: expected 4 points, got %d\n", point_count);
        return 1;
    }
    if (vertex_count <= 0 || index_count < 3) {
        std::fprintf(stderr, "FAIL: spawn mesh empty verts=%d indices=%d\n",
                     vertex_count, index_count);
        return 1;
    }
    if (out_json[0] != '\0') {
        std::fprintf(stderr, "FAIL: unexpected out_json\n");
        return 1;
    }

    std::printf("spawn mesh binary test passed: points=%d verts=%d indices=%d\n",
                point_count,
                vertex_count,
                index_count);
    return 0;
}
