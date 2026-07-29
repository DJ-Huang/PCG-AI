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
        "inputs":[{"id":"geometry","name":"Geometry","pinType":"SpatialMesh"}],
        "outputs":[{"id":"geometry","name":"Geometry","pinType":"SpatialMesh"}],
        "nodes":[
          {"id":"input","type":"SubgraphInput","position":{"x":0,"y":0},"data":{}},
          {"id":"transform","type":"TransformMesh","position":{"x":0,"y":160},"data":{"translateX":3,"translateY":0,"translateZ":0}},
          {"id":"output","type":"Output","position":{"x":0,"y":320},"data":{}}
        ],
        "edges":[
          {"id":"ie1","source":"input","target":"transform","sourceHandle":"geometry","targetHandle":"in"},
          {"id":"ie2","source":"transform","target":"output","sourceHandle":"out","targetHandle":"in"}
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

    constexpr const char* passthrough = R"JSON({
      "version":"1.0",
      "nodes":[
        {"id":"box","type":"CreateBoxMesh","position":{"x":0,"y":0},"data":{"sizeX":2,"sizeY":2,"sizeZ":2}},
        {"id":"inst","type":"Subgraph","position":{"x":0,"y":160},"data":{"subgraphId":"passthrough"}},
        {"id":"output","type":"Output","position":{"x":0,"y":320},"data":{}}
      ],
      "edges":[
        {"id":"e1","source":"box","target":"inst","sourceHandle":"out","targetHandle":"in"},
        {"id":"e2","source":"inst","target":"output","sourceHandle":"out","targetHandle":"in"}
      ],
      "subgraphs":[{
        "id":"passthrough","name":"Passthrough",
        "inputs":[{"id":"in","name":"In","pinType":"SpatialMesh"}],
        "outputs":[{"id":"out","name":"Out","pinType":"SpatialMesh"}],
        "nodes":[
          {"id":"input","type":"SubgraphInput","position":{"x":0,"y":0},"data":{}},
          {"id":"output","type":"SubgraphOutput","position":{"x":0,"y":160},"data":{}}
        ],
        "edges":[
          {"id":"ie1","source":"input","target":"output","sourceHandle":"in","targetHandle":"out"}
        ]
      }]
    })JSON";
    std::memset(error, 0, sizeof(error));
    require(pcg_execute_graph_v7(
        passthrough, 42, nullptr, 0, nullptr, 0, nullptr, 0,
        &kind, out, sizeof(out), mesh.data(), static_cast<int>(mesh.size()),
        nullptr, 0, nullptr, nullptr, &vertex_count, &index_count,
        nullptr, nullptr, 0, error, sizeof(error)) == PCG_OK, error);
    require(kind == PCG_RESULT_KIND_MESH, "passthrough subgraph did not produce a mesh result");
    require(vertex_count > 0 && index_count == 36, "passthrough subgraph mesh counts are incorrect");

    constexpr const char* multiple_inputs = R"JSON({
      "version":"1.0",
      "nodes":[
        {"id":"box_a","type":"CreateBoxMesh","position":{"x":-120,"y":0},"data":{"sizeX":2,"sizeY":2,"sizeZ":2}},
        {"id":"box_b","type":"CreateBoxMesh","position":{"x":120,"y":0},"data":{"sizeX":1,"sizeY":1,"sizeZ":1}},
        {"id":"inst","type":"Subgraph","position":{"x":0,"y":160},"data":{"subgraphId":"merge"}},
        {"id":"output","type":"Output","position":{"x":0,"y":320},"data":{}}
      ],
      "edges":[
        {"id":"e1","source":"box_a","target":"inst","sourceHandle":"out","targetHandle":"in_a"},
        {"id":"e2","source":"box_b","target":"inst","sourceHandle":"out","targetHandle":"in_b"},
        {"id":"e3","source":"inst","target":"output","sourceHandle":"out","targetHandle":"in"}
      ],
      "subgraphs":[{
        "id":"merge","name":"Merge Inputs",
        "inputs":[
          {"id":"in_a","name":"A","pinType":"SpatialMesh"},
          {"id":"in_b","name":"B","pinType":"SpatialMesh"}
        ],
        "outputs":[{"id":"out","name":"Out","pinType":"SpatialMesh"}],
        "nodes":[
          {"id":"input","type":"SubgraphInput","position":{"x":0,"y":0},"data":{}},
          {"id":"merge","type":"MergeMesh","position":{"x":0,"y":160},"data":{}},
          {"id":"output","type":"Output","position":{"x":0,"y":320},"data":{}}
        ],
        "edges":[
          {"id":"ie1","source":"input","target":"merge","sourceHandle":"in_a","targetHandle":"in"},
          {"id":"ie2","source":"input","target":"merge","sourceHandle":"in_b","targetHandle":"in"},
          {"id":"ie3","source":"merge","target":"output","sourceHandle":"out","targetHandle":"in"}
        ]
      }]
    })JSON";
    std::memset(error, 0, sizeof(error));
    require(pcg_execute_graph_v7(
        multiple_inputs, 42, nullptr, 0, nullptr, 0, nullptr, 0,
        &kind, out, sizeof(out), mesh.data(), static_cast<int>(mesh.size()),
        nullptr, 0, nullptr, nullptr, &vertex_count, &index_count,
        nullptr, nullptr, 0, error, sizeof(error)) == PCG_OK, error);
    require(kind == PCG_RESULT_KIND_MESH, "multi-input subgraph did not produce a mesh result");
    require(index_count == 72, "multi-input subgraph did not merge both parent inputs");

    constexpr const char* recursive = R"JSON({
      "version":"1.0",
      "nodes":[{"id":"s","type":"Subgraph","position":{"x":0,"y":0},"data":{"subgraphId":"loop"}}],
      "edges":[],
      "subgraphs":[{
        "id":"loop","name":"Loop","inputs":[],
        "outputs":[{"id":"out","name":"Out","pinType":"Any"}],
        "nodes":[
          {"id":"self","type":"Subgraph","position":{"x":0,"y":0},"data":{"subgraphId":"loop"}},
          {"id":"output","type":"Output","position":{"x":0,"y":160},"data":{}}
        ],
        "edges":[]
      }]
    })JSON";
    std::memset(error, 0, sizeof(error));
    require(pcg_validate_graph(recursive, error, sizeof(error)) == PCG_ERR_INVALID_JSON,
            "recursive subgraph must be rejected");

    constexpr const char* parent_ref = R"JSON({
      "version":"1.0",
      "nodes":[
        {"id":"box","type":"CreateBoxMesh","position":{"x":0,"y":0},"data":{"sizeX":2,"sizeY":2,"sizeZ":2}},
        {"id":"inst","type":"Subgraph","position":{"x":0,"y":160},"data":{"subgraphId":"inner"}},
        {"id":"output","type":"Output","position":{"x":0,"y":320},"data":{}}
      ],
      "edges":[
        {"id":"e1","source":"inst","target":"output","sourceHandle":"out","targetHandle":"in"}
      ],
      "subgraphs":[{
        "id":"inner","name":"Inner",
        "outputs":[{"id":"out","name":"Out","pinType":"SpatialMesh"}],
        "nodes":[
          {"id":"pref","type":"SubgraphParentRef","position":{"x":0,"y":0},"data":{"parentNodeId":"box","parentHandle":"out"}},
          {"id":"out","type":"Output","position":{"x":0,"y":160},"data":{}}
        ],
        "edges":[
          {"id":"ie1","source":"pref","target":"out","sourceHandle":"out","targetHandle":"in"}
        ]
      }]
    })JSON";
    std::memset(error, 0, sizeof(error));
    require(pcg_validate_graph(parent_ref, error, sizeof(error)) == PCG_OK, error);
    std::memset(error, 0, sizeof(error));
    require(pcg_execute_graph_v7(
        parent_ref, 42, nullptr, 0, nullptr, 0, nullptr, 0,
        &kind, out, sizeof(out), mesh.data(), static_cast<int>(mesh.size()),
        nullptr, 0, nullptr, nullptr, &vertex_count, &index_count,
        nullptr, nullptr, 0, error, sizeof(error)) == PCG_OK, error);

    constexpr const char* multiple_outputs = R"JSON({
      "version":"1.0",
      "nodes":[{"id":"s","type":"Subgraph","position":{"x":0,"y":0},"data":{"subgraphId":"invalid"}}],
      "edges":[],
      "subgraphs":[{
        "id":"invalid","name":"Invalid","inputs":[],
        "outputs":[
          {"id":"a","name":"A","pinType":"Any"},
          {"id":"b","name":"B","pinType":"Any"}
        ],
        "nodes":[
          {"id":"a","type":"Output","position":{"x":0,"y":0},"data":{}},
          {"id":"b","type":"Output","position":{"x":0,"y":160},"data":{}}
        ],
        "edges":[]
      }]
    })JSON";
    std::memset(error, 0, sizeof(error));
    require(pcg_validate_graph(multiple_outputs, error, sizeof(error)) == PCG_ERR_INVALID_JSON,
            "subgraph with multiple outputs must be rejected");

    std::puts("PASS: single-output subgraph flattening, legacy passthrough, parent ref, and guards");
    return 0;
}
