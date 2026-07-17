#include "pcg_api.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

struct Result {
    PcgResultCode code = PCG_OK;
    int kind = PCG_RESULT_KIND_NONE;
    int vertices = 0;
    int indices = 0;
    char error[512] = {};
};

void expect(bool condition, const char* message)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition)
        std::exit(1);
}

Result execute(const char* graph)
{
    std::vector<char> json_output(4096);
    std::vector<unsigned char> mesh_output(2 * 1024 * 1024);
    Result result;
    result.code = pcg_execute_graph_v7(
        graph, 42, nullptr, 0, nullptr, 0, nullptr, 0, &result.kind,
        json_output.data(), static_cast<int>(json_output.size()),
        mesh_output.data(), static_cast<int>(mesh_output.size()), nullptr, 0,
        nullptr, nullptr, &result.vertices, &result.indices, nullptr, nullptr, 0,
        result.error, sizeof(result.error));
    return result;
}

} // namespace

int main()
{
    const char* body_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"p0","type":"CreateBezierSpline","data":{
          "closed":false,"subdivisions":2,
          "controlPoints":"[{\"x\":-1,\"y\":0,\"z\":0},{\"x\":-1,\"y\":0.4,\"z\":0.2},{\"x\":-1,\"y\":0.4,\"z\":0.8},{\"x\":-1,\"y\":0,\"z\":1}]"
        }},
        {"id":"p1","type":"CreateBezierSpline","data":{
          "closed":false,"subdivisions":2,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":0,\"y\":0.6,\"z\":0.2},{\"x\":0,\"y\":0.6,\"z\":0.8},{\"x\":0,\"y\":0,\"z\":1}]"
        }},
        {"id":"p2","type":"CreateBezierSpline","data":{
          "closed":false,"subdivisions":2,
          "controlPoints":"[{\"x\":1,\"y\":0,\"z\":0},{\"x\":1,\"y\":0.4,\"z\":0.2},{\"x\":1,\"y\":0.4,\"z\":0.8},{\"x\":1,\"y\":0,\"z\":1}]"
        }},
        {"id":"loft","type":"LoftMesh","data":{
          "columns":6,"sortAxis":"x","closedProfile":false,
          "capStart":false,"capEnd":false,"autoAlign":true
        }},
        {"id":"mirror","type":"MirrorMesh","data":{
          "axis":"z","offset":0,"mergeOriginal":true,
          "weldSeam":true,"weldTolerance":0.0001
        }},
        {"id":"fuse","type":"FuseMesh","data":{
          "tolerance":0.0001,"removeDegenerate":true
        }},
        {"id":"shell","type":"ShellMesh","data":{
          "thickness":0.02,"direction":"inward","closeBoundaries":true
        }},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e0","source":"p0","target":"loft","sourceHandle":"out","targetHandle":"profiles"},
        {"id":"e1","source":"p1","target":"loft","sourceHandle":"out","targetHandle":"profiles"},
        {"id":"e2","source":"p2","target":"loft","sourceHandle":"out","targetHandle":"profiles"},
        {"id":"e3","source":"loft","target":"mirror","sourceHandle":"out","targetHandle":"in"},
        {"id":"e4","source":"mirror","target":"fuse","sourceHandle":"out","targetHandle":"in"},
        {"id":"e5","source":"fuse","target":"shell","sourceHandle":"out","targetHandle":"in"},
        {"id":"e6","source":"shell","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    const Result body = execute(body_graph);
    expect(body.code == PCG_OK,
           body.error[0] == '\0' ? "compact vehicle body graph executes" : body.error);
    expect(body.kind == PCG_RESULT_KIND_MESH, "compact vehicle body returns a mesh");
    expect(body.vertices > 20 && body.indices > 60,
           "compact vehicle body produces non-trivial geometry");

    const char* detail_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"box","type":"CreateBoxMesh","data":{
          "sizeX":0.1,"sizeY":0.5,"sizeZ":0.08,
          "centerX":0,"centerY":0.65,"centerZ":0
        }},
        {"id":"extrude","type":"PolyExtrude","data":{
          "faceGroup":"","distance":0.01,"inset":0.01,"keepOriginal":false
        }},
        {"id":"copy","type":"CopyMesh","data":{
          "mode":"circular","count":4,"axis":"x","angle":360,
          "centerX":0,"centerY":0,"centerZ":0
        }},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e0","source":"box","target":"extrude","sourceHandle":"out","targetHandle":"in"},
        {"id":"e1","source":"extrude","target":"copy","sourceHandle":"out","targetHandle":"in"},
        {"id":"e2","source":"copy","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    const Result detail = execute(detail_graph);
    expect(detail.code == PCG_OK,
           detail.error[0] == '\0' ? "compact detail graph executes" : detail.error);
    expect(detail.kind == PCG_RESULT_KIND_MESH, "compact detail graph returns a mesh");
    expect(detail.vertices > 0 && detail.indices > 0,
           "compact detail graph produces geometry");

    std::printf("PASS: compact vehicle modeling graph regressions\n");
    return 0;
}
