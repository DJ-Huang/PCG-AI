#include "pcg_api.h"
#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_mesh_data.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fail(const char* message)
{
    std::fprintf(stderr, "FAIL: %s\n", message);
    std::exit(1);
}

void expect(bool condition, const char* message)
{
    if (!condition)
        fail(message);
}

pcg::internal::data::PcgMeshData execute_mesh(const char* graph, const char* label)
{
    std::vector<char> json(1 << 16);
    std::vector<unsigned char> mesh(1 << 20);
    char err[2048] = {};
    int kind = PCG_RESULT_KIND_NONE;
    int vertex_count = 0;
    int index_count = 0;
    const PcgResultCode code = pcg_execute_graph_v2(
        graph, 42, &kind, json.data(), static_cast<int>(json.size()), mesh.data(),
        static_cast<int>(mesh.size()), &vertex_count, &index_count, err, sizeof(err));
    if (code != PCG_OK) {
        std::fprintf(stderr, "FAIL: %s code=%d err=%s\n", label, static_cast<int>(code), err);
        std::exit(1);
    }
    expect(kind == PCG_RESULT_KIND_MESH, label);
    pcg::internal::data::PcgMeshData parsed;
    expect(pcg::internal::data::read_mesh_binary(mesh.data(), static_cast<int>(mesh.size()), parsed),
           "parse mesh binary");
    return parsed;
}

bool meshes_near(const pcg::internal::data::PcgMeshData& a,
                 const pcg::internal::data::PcgMeshData& b,
                 float eps = 1.0e-5f)
{
    if (a.vertices().size() != b.vertices().size())
        return false;
    for (size_t i = 0; i < a.vertices().size(); ++i) {
        if (std::abs(a.vertices()[i].x - b.vertices()[i].x) > eps ||
            std::abs(a.vertices()[i].y - b.vertices()[i].y) > eps ||
            std::abs(a.vertices()[i].z - b.vertices()[i].z) > eps)
            return false;
    }
    return true;
}

} // namespace

int main()
{
    // F1: partial spline range-group attrs must not crash C ABI stats.
    {
        const char* graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":2.0,"height":1.0,"depth":3.0}},
            {"id":"lines","type":"ConvertLine","data":{"connectPath":false}},
            {"id":"measure","type":"MeasureMesh","data":{
              "elementType":"primitives",
              "measure":"perimeter",
              "attributeName":"length",
              "useRangeGroup":true,
              "rangeGroup":"inrange",
              "useWidth":true,
              "width":0.01,
              "widthScale":"absolute",
              "centerType":"median"
            }},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"lines","sourceHandle":"out","targetHandle":"in"},
            {"source":"lines","target":"measure","sourceHandle":"out","targetHandle":"in"},
            {"source":"measure","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";

        std::vector<char> out(1 << 20);
        char err[2048] = {};
        int kind = PCG_RESULT_KIND_NONE;
        int vertex_count = 0;
        int index_count = 0;
        const PcgResultCode code = pcg_execute_graph_v2(
            graph, 42, &kind, out.data(), static_cast<int>(out.size()), nullptr, 0,
            &vertex_count, &index_count, err, sizeof(err));
        expect(code == PCG_OK, "F1 partial range-group graph returns PCG_OK");
        expect(kind == PCG_RESULT_KIND_JSON, "F1 result is spline JSON");
        const auto json = nlohmann::json::parse(out.data());
        expect(json.contains("splines") && json["splines"].is_array() &&
                   !json["splines"].empty(),
               "F1 emits splines");

        bool saw_length = false;
        bool saw_inrange = false;
        for (const auto& spline : json["splines"]) {
            if (!spline.contains("attributes") || !spline["attributes"].is_object())
                continue;
            if (spline["attributes"].contains("length"))
                saw_length = true;
            if (spline["attributes"].contains("inrange"))
                saw_inrange = true;
        }
        expect(saw_length, "F1 length attribute present");
        expect(saw_inrange, "F1 inrange attribute present on subset");
        std::printf("PASS: F1 partial spline range-group stats\n");
    }

    // F1/ABI: wrong JSON property type must return error, not terminate.
    {
        const char* graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":"not-a-number","height":1.0,"depth":1.0}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        std::vector<char> out(4096);
        char err[2048] = {};
        int kind = PCG_RESULT_KIND_NONE;
        int vertex_count = 0;
        int index_count = 0;
        const PcgResultCode code = pcg_execute_graph_v2(
            graph, 42, &kind, out.data(), static_cast<int>(out.size()), nullptr, 0,
            &vertex_count, &index_count, err, sizeof(err));
        expect(code == PCG_ERR_EXECUTION, "ABI catch returns PCG_ERR_EXECUTION");
        expect(err[0] != '\0', "ABI catch writes error message");
        std::printf("PASS: ABI exception catch returns error (%s)\n", err);
    }

    // F2: canonical default + legacy Y must resolve to [0,2,0].
    {
        const char* baseline = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1,"height":1,"depth":1}},
            {"id":"xform","type":"TransformMesh","data":{"translate":[0,0,0]}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"xform","sourceHandle":"out","targetHandle":"in"},
            {"source":"xform","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const char* legacy_only = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1,"height":1,"depth":1}},
            {"id":"xform","type":"TransformMesh","data":{"translateY":2}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"xform","sourceHandle":"out","targetHandle":"in"},
            {"source":"xform","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const char* mixed = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1,"height":1,"depth":1}},
            {"id":"xform","type":"TransformMesh","data":{"translate":[0,0,0],"translateY":2}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"xform","sourceHandle":"out","targetHandle":"in"},
            {"source":"xform","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";

        const auto base_mesh = execute_mesh(baseline, "F2 baseline");
        const auto legacy_mesh = execute_mesh(legacy_only, "F2 legacy-only");
        const auto mixed_mesh = execute_mesh(mixed, "F2 mixed");
        expect(!meshes_near(base_mesh, legacy_mesh), "F2 legacy-only moves geometry");
        expect(meshes_near(legacy_mesh, mixed_mesh),
               "F2 mixed canonical-default + legacyY matches legacy-only");
        std::printf("PASS: F2 vector3 legacy override precedence\n");
    }

    // F4: multi-axis pivot rotate with identity main transform is identity.
    {
        const char* baseline = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1,"height":1,"depth":1}},
            {"id":"xform","type":"TransformMesh","data":{}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"xform","sourceHandle":"out","targetHandle":"in"},
            {"source":"xform","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const char* pivot_only = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1,"height":1,"depth":1}},
            {"id":"xform","type":"TransformMesh","data":{"pivotRotate":[30,45,60]}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"xform","sourceHandle":"out","targetHandle":"in"},
            {"source":"xform","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto base_mesh = execute_mesh(baseline, "F4 baseline");
        const auto pivot_mesh = execute_mesh(pivot_only, "F4 pivot-only");
        expect(meshes_near(base_mesh, pivot_mesh, 1.0e-5f),
               "F4 pivot rotate with identity local leaves mesh unchanged");
        std::printf("PASS: F4 pivot rotate inverse identity\n");
    }

    std::printf("ALL PASS: review-2026-07-27 fixes\n");
    return 0;
}
