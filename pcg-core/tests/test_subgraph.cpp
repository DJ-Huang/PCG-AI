#include "pcg_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

} // namespace

int main()
{
    constexpr const char* graph = R"JSON({
      "version":"1.0",
      "nodes":[
        {"id":"box","type":"CreateBoxMesh","position":{"x":0,"y":0},"data":{"sizeX":2,"sizeY":2,"sizeZ":2}},
        {"id":"move_subgraph","type":"Subgraph","position":{"x":0,"y":160},"data":{"subgraphId":"move"}},
        {"id":"output","type":"Output","position":{"x":0,"y":320},"data":{}}
      ],
      "edges":[
        {"id":"e1","source":"box","target":"move_subgraph","sourceHandle":"out","targetHandle":"geometry"},
        {"id":"e2","source":"move_subgraph","target":"output","sourceHandle":"geometry","targetHandle":"in"}
      ],
      "subgraphs":[{
        "id":"move","name":"Move Geometry",
        "inputs":[{"id":"geometry","name":"Geometry","pinType":"Mesh"}],
        "outputs":[{"id":"geometry","name":"Geometry","pinType":"Mesh"}],
        "nodes":[
          {"id":"input","type":"SubgraphInput","position":{"x":0,"y":0},"data":{}},
          {"id":"transform","type":"TransformMesh","position":{"x":0,"y":160},"data":{"translateX":3,"translateY":0,"translateZ":0}},
          {"id":"output","type":"SubgraphOutput","position":{"x":0,"y":320},"data":{}}
        ],
        "edges":[
          {"id":"ie1","source":"input","target":"transform","sourceHandle":"geometry","targetHandle":"in"},
          {"id":"ie2","source":"transform","target":"output","sourceHandle":"out","targetHandle":"geometry"}
        ]
      }]
    })JSON";

    std::vector<unsigned char> mesh(1024 * 1024);
    char out[16384] = {};
    char error[1024] = {};
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    const PcgResultCode code = pcg_execute_graph_v7(
        graph, 42, nullptr, 0, nullptr, 0, nullptr, 0,
        &kind, out, sizeof(out), mesh.data(), static_cast<int>(mesh.size()),
        nullptr, 0, nullptr, nullptr, &vertex_count, &index_count,
        nullptr, nullptr, 0, error, sizeof(error));
    require(code == PCG_OK, error);
    require(kind == PCG_RESULT_KIND_MESH, "subgraph graph did not produce a mesh result");
    require(vertex_count > 0 && index_count == 36, "subgraph mesh counts are incorrect");

    constexpr const char* recursive = R"JSON({
      "version":"1.0",
      "nodes":[{"id":"s","type":"Subgraph","position":{"x":0,"y":0},"data":{"subgraphId":"loop"}}],
      "edges":[],
      "subgraphs":[{"id":"loop","name":"Loop","inputs":[],"outputs":[],"nodes":[{"id":"self","type":"Subgraph","position":{"x":0,"y":0},"data":{"subgraphId":"loop"}}],"edges":[]}]
    })JSON";
    std::memset(error, 0, sizeof(error));
    require(pcg_validate_graph(recursive, error, sizeof(error)) == PCG_ERR_INVALID_JSON,
            "recursive subgraph must be rejected");

    std::puts("PASS: subgraph flattening, input/output mapping, and recursion guard");
    return 0;
}
