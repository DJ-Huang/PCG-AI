#include "pcg_api.h"
#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_spline_data.hpp"
#include "elements/vehicle_modeling_algorithms.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace pcg::internal::data;
using namespace pcg::internal::elements;

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

double signed_volume(const PcgMeshData& mesh)
{
    double vol = 0.0;
    const auto& v = mesh.vertices();
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const auto& a = v[static_cast<size_t>(mesh.triangles()[i])];
        const auto& b = v[static_cast<size_t>(mesh.triangles()[i + 1])];
        const auto& c = v[static_cast<size_t>(mesh.triangles()[i + 2])];
        vol += (a.x * (b.y * c.z - b.z * c.y) + a.y * (b.z * c.x - b.x * c.z) +
                a.z * (b.x * c.y - b.y * c.x)) /
               6.0;
    }
    return vol;
}

void test_outline_solid_outward_normals()
{
    // CCW rectangle in XY; thickness along Z. Outward windings → positive volume.
    PcgSpline outline;
    outline.closed = true;
    outline.points = {
        {0.0, 0.0, 0.0},
        {0.1, 0.0, 0.0},
        {0.1, 0.04, 0.0},
        {0.0, 0.04, 0.0},
    };
    OutlineSolidOptions opts;
    opts.thickness = 0.008;
    opts.thickness_axis = "z";
    auto geo = outline_solid_from_spline(outline, opts);
    expect(!geo.points().empty(), "outline solid produces geometry");
    expect(geo.faces().size() == 6, "front+back+4 rim faces");
    const double vol = signed_volume(triangulate_geometry(geo));
    expect(vol > 0.0, "outline solid rim/front windings are outward (positive volume)");
    const double expected = 0.1 * 0.04 * 0.008;
    expect(std::fabs(vol - expected) < 1e-6, "outline solid volume matches L*W*T");
}

} // namespace

int main()
{
    test_outline_solid_outward_normals();

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

    const char* outline_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"outline","type":"CreateSpline","data":{
          "mode":"polyline","closed":true,"subdivisions":1,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":0.1,\"y\":0,\"z\":0},{\"x\":0.1,\"y\":0.04,\"z\":0},{\"x\":0,\"y\":0.04,\"z\":0}]"
        }},
        {"id":"solid","type":"OutlineSolid","data":{
          "thickness":0.008,"thicknessAxis":"z",
          "frontGroup":"front","backGroup":"back","rimGroup":"rim"
        }},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e0","source":"outline","target":"solid","sourceHandle":"out","targetHandle":"outline"},
        {"id":"e1","source":"solid","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    const Result outline = execute(outline_graph);
    expect(outline.code == PCG_OK,
           outline.error[0] == '\0' ? "outline solid graph executes" : outline.error);
    expect(outline.kind == PCG_RESULT_KIND_MESH, "outline solid returns a mesh");
    // 4 outline pts × 2 rings = 8 verts; front+back+4 rim quads → triangulated > 0
    expect(outline.vertices >= 8 && outline.indices >= 12,
           "outline solid produces welded plate geometry");

    std::printf("PASS: compact vehicle modeling graph regressions\n");
    return 0;
}
