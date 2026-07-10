// B4: Boolean mesh graph integration test.
// Tests BooleanMesh via pcg_execute_graph_v7 (full graph execution).

#include "pcg_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

[[noreturn]] void fail(const char* msg)
{
    std::printf("FAIL: %s\n", msg);
    std::exit(1);
}

bool execute_graph(const char* graph_json, int seed, char* err_buf, int err_size)
{
    int kind = 0;
    std::vector<char> json_out(8 * 1024 * 1024);
    std::vector<uint8_t> mesh_buf(8 * 1024 * 1024);
    int vertex_count = 0, index_count = 0;

    PcgResultCode rc = pcg_execute_graph_v7(
        graph_json, seed,
        nullptr, 0,
        nullptr, 0,
        nullptr, 0,
        &kind, json_out.data(), static_cast<int>(json_out.size()),
        mesh_buf.data(), static_cast<int>(mesh_buf.size()),
        nullptr, 0,
        nullptr, nullptr, &vertex_count, &index_count,
        nullptr,
        nullptr, 0,
        err_buf, err_size);

    if (rc != PCG_OK)
        return false;

    return kind == PCG_RESULT_KIND_MESH;
}

void test_graph_boolean_subtract()
{
    const char* graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "box_a", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
         "data": {"width": 4.0, "height": 4.0, "depth": 4.0}},
        {"id": "box_b", "type": "CreateBoxMesh", "position": {"x":200,"y":0},
         "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
        {"id": "bool", "type": "BooleanMesh", "position": {"x":400,"y":0},
         "data": {"operation": "subtract"}},
        {"id": "out", "type": "Output", "position": {"x":600,"y":0},
         "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "box_a", "target": "bool", "sourceHandle": "out", "targetHandle": "a"},
        {"id": "e2", "source": "box_b", "target": "bool", "sourceHandle": "out", "targetHandle": "b"},
        {"id": "e3", "source": "bool", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    char err_buf[1024] = {};
    if (!execute_graph(graph, 42, err_buf, sizeof(err_buf)))
        fail(("BooleanMesh subtract graph failed: " + std::string(err_buf)).c_str());
}

void test_graph_boolean_union()
{
    const char* graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "a", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
         "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
        {"id": "b", "type": "CreateBoxMesh", "position": {"x":200,"y":0},
         "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
        {"id": "bool", "type": "BooleanMesh", "position": {"x":400,"y":0},
         "data": {"operation": "union"}},
        {"id": "out", "type": "Output", "position": {"x":600,"y":0},
         "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "a", "target": "bool", "sourceHandle": "out", "targetHandle": "a"},
        {"id": "e2", "source": "b", "target": "bool", "sourceHandle": "out", "targetHandle": "b"},
        {"id": "e3", "source": "bool", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    char err_buf[1024] = {};
    if (!execute_graph(graph, 42, err_buf, sizeof(err_buf)))
        fail(("BooleanMesh union graph failed: " + std::string(err_buf)).c_str());
}

} // namespace

int main()
{
    test_graph_boolean_subtract();
    test_graph_boolean_union();

    std::printf("test_boolean_graph: all tests passed\n");
    return 0;
}
