#include "pcg_api.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message)
{
    if (condition) {
        std::printf("PASS: %s\n", message);
    } else {
        std::printf("FAIL: %s\n", message);
        ++g_failures;
    }
}

json execute_json(const char* graph, PcgResultCode expected = PCG_OK)
{
    std::vector<char> output(1024 * 1024, 0);
    const PcgResultCode code = pcg_execute_graph(graph, 42, output.data(),
                                                 static_cast<int>(output.size()));
    expect(code == expected, "graph returned expected result code");
    if (code != PCG_OK)
        return json::object();
    return json::parse(output.data());
}

} // namespace

int main()
{
    const char* wrangle_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"path","type":"CreateSpline","data":{
          "mode":"polyline","closed":false,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":2.5,\"y\":0,\"z\":0},{\"x\":5,\"y\":0,\"z\":0},{\"x\":7.5,\"y\":0,\"z\":0},{\"x\":10,\"y\":0,\"z\":0}]"}},
        {"id":"sag","type":"AttributeWrangle","data":{
          "runOver":"points",
          "expression":"@P.y -= chf(\"sag\") * 4.0 * @curveu * (1.0 - @curveu);",
          "parameters":"{\"sag\":3.0}"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"path","target":"sag","sourceHandle":"out","targetHandle":"in"},
        {"id":"e2","source":"sag","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    const json sag = execute_json(wrangle_graph);
    expect(sag.contains("splines") && sag["splines"].size() == 1,
           "AttributeWrangle preserves one spline");
    if (sag.contains("splines") && !sag["splines"].empty()) {
        const auto& points = sag["splines"][0]["points"];
        expect(points.size() == 5, "AttributeWrangle preserves spline point count");
        expect(std::abs(points.front()["y"].get<double>()) < 0.000001 &&
               std::abs(points.back()["y"].get<double>()) < 0.000001,
               "sag expression preserves endpoints");
        expect(std::abs(points[2]["y"].get<double>() + 3.0) < 0.000001,
               "sag expression reaches requested midpoint depth");
    }

    const char* blast_spline_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"path","type":"CreateSpline","data":{
          "mode":"polyline","closed":false,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":2.5,\"y\":0,\"z\":0},{\"x\":5,\"y\":0,\"z\":0},{\"x\":7.5,\"y\":0,\"z\":0},{\"x\":10,\"y\":0,\"z\":0}]"}},
        {"id":"blast","type":"Blast","data":{
          "entity":"points","expression":"@curveu >= 0.4 && @curveu <= 0.6"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"path","target":"blast","sourceHandle":"out","targetHandle":"in"},
        {"id":"e2","source":"blast","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    const json broken = execute_json(blast_spline_graph);
    expect(broken.contains("splines") && broken["splines"].size() == 2,
           "Blast splits a spline across a deleted U interval");
    if (broken.contains("splines") && broken["splines"].size() == 2) {
        expect(broken["splines"][0]["points"].size() == 2 &&
               broken["splines"][1]["points"].size() == 2,
               "Blast keeps both valid spline runs");
        expect(broken["splines"][0]["closed"] == false &&
               broken["splines"][1]["closed"] == false,
               "Blast emits open runs after breaking a spline");
    }

    const char* point_attribute_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"points","type":"CreatePoints","data":{"count":5}},
        {"id":"wrangle","type":"AttributeWrangle","data":{
          "expression":"@weight = @curveu; @P.y = @weight * 2.0;"}},
        {"id":"blast","type":"Blast","data":{
          "expression":"@weight > 0.4 && @weight < 0.8"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"points","target":"wrangle"},
        {"id":"e2","source":"wrangle","target":"blast"},
        {"id":"e3","source":"blast","target":"out"}
      ]
    })";
    const json filtered_points = execute_json(point_attribute_graph);
    expect(filtered_points.contains("points") && filtered_points["points"].size() == 3,
           "Wrangle custom point attributes drive Blast selection");
    if (filtered_points.contains("points") && filtered_points["points"].size() == 3) {
        expect(filtered_points["points"][1]["attributes"]["weight"] == 0.25,
               "custom numeric attributes survive filtering");
        expect(filtered_points["points"][2]["y"] == 2.0,
               "Wrangle position assignment survives filtering");
    }

    const char* primitive_group_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"top","type":"FaceGroupByNormal","data":{
          "outputGroup":"top","directionX":0,"directionY":1,"directionZ":0,"spreadAngle":1}},
        {"id":"blast","type":"Blast","data":{
          "entity":"primitives","group":"top","expression":"","removeUnusedPoints":true}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"top"},
        {"id":"e2","source":"top","target":"blast"},
        {"id":"e3","source":"blast","target":"out"}
      ]
    })";
    std::vector<char> json_buffer(1024 * 1024, 0);
    std::vector<unsigned char> mesh_buffer(1024 * 1024, 0);
    char error[1024] = {};
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    const PcgResultCode primitive_code = pcg_execute_graph_v2(
        primitive_group_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(primitive_code == PCG_OK, "Blast primitive group graph executes");
    expect(kind == PCG_RESULT_KIND_MESH, "Blast primitive group outputs mesh");
    expect(index_count == 30, "Blast removes one quad face from a box");

    const char* split_group_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"top","type":"FaceGroupByNormal","data":{
          "outputGroup":"top","directionX":0,"directionY":1,"directionZ":0,"spreadAngle":1}},
        {"id":"split","type":"Split","data":{
          "groupType":"guess","group":"top","invertSelection":false}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"top"},
        {"id":"e2","source":"top","target":"split"},
        {"id":"e3","source":"split","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode split_selected_code = pcg_execute_graph_v2(
        split_group_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(split_selected_code == PCG_OK, "Split selected output graph executes");
    expect(kind == PCG_RESULT_KIND_MESH, "Split selected output is mesh");
    expect(index_count == 6, "Split out keeps the top face only");

    const char* split_rest_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"top","type":"FaceGroupByNormal","data":{
          "outputGroup":"top","directionX":0,"directionY":1,"directionZ":0,"spreadAngle":1}},
        {"id":"split","type":"Split","data":{
          "groupType":"guess","group":"top","invertSelection":false}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"top"},
        {"id":"e2","source":"top","target":"split"},
        {"id":"e3","source":"split","target":"out","sourceHandle":"rest","targetHandle":"in"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode split_rest_code = pcg_execute_graph_v2(
        split_rest_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(split_rest_code == PCG_OK, "Split remainder output graph executes");
    expect(index_count == 30, "Split rest keeps the five non-top faces");

    const char* split_invert_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"top","type":"FaceGroupByNormal","data":{
          "outputGroup":"top","directionX":0,"directionY":1,"directionZ":0,"spreadAngle":1}},
        {"id":"split","type":"Split","data":{
          "groupType":"guess","group":"top","invertSelection":true}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"top"},
        {"id":"e2","source":"top","target":"split"},
        {"id":"e3","source":"split","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode split_invert_code = pcg_execute_graph_v2(
        split_invert_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(split_invert_code == PCG_OK, "Split invertSelection graph executes");
    expect(index_count == 30, "Split invert puts non-group on first output");

    const char* group_wrangle_split_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"grid","type":"CreateGridMesh","data":{
          "sizeX":5,"sizeY":4,"rows":1,"cols":5,"plane":"xz"}},
        {"id":"park","type":"AttributeWrangle","data":{
          "runOver":"primitives",
          "expression":"@group.park = ((@primnum + 1) % 5 == 0);"}},
        {"id":"split","type":"Split","data":{
          "groupType":"guess","group":"park","invertSelection":true,"deleteUnusedGroups":true}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"grid","target":"park"},
        {"id":"e2","source":"park","target":"split"},
        {"id":"e3","source":"split","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode group_wrangle_code = pcg_execute_graph_v2(
        group_wrangle_split_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(group_wrangle_code == PCG_OK, "@group.park wrangle + Split executes");
    expect(index_count == 24, "Invert Split keeps 4 of 5 grid faces");

    const char* split_empty_group_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"split","type":"Split","data":{
          "groupType":"primitives","group":"","invertSelection":false}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"split"},
        {"id":"e2","source":"split","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode split_empty_code = pcg_execute_graph_v2(
        split_empty_group_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(split_empty_code == PCG_OK, "Split empty group selects all");
    expect(index_count == 36, "Empty group Split out keeps all box faces");

    const char* split_legacy_entity_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"top","type":"FaceGroupByNormal","data":{
          "outputGroup":"top","directionX":0,"directionY":1,"directionZ":0,"spreadAngle":1}},
        {"id":"split","type":"Split","data":{
          "entity":"primitives","group":"top","invertSelection":false,"removeUnusedPoints":true}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"top"},
        {"id":"e2","source":"top","target":"split"},
        {"id":"e3","source":"split","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode split_legacy_code = pcg_execute_graph_v2(
        split_legacy_entity_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(split_legacy_code == PCG_OK, "Legacy Split entity field still works");
    expect(index_count == 6, "Legacy entity Split keeps top face");

    const char* invalid_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"points","type":"CreatePoints","data":{"count":2}},
        {"id":"wrangle","type":"AttributeWrangle","data":{"expression":"@P.y = (1 + ;"}}
      ],
      "edges":[{"id":"e1","source":"points","target":"wrangle"}]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode invalid_code = pcg_execute_graph_v2(
        invalid_graph, 42, &kind, json_buffer.data(), static_cast<int>(json_buffer.size()),
        mesh_buffer.data(), static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(invalid_code == PCG_ERR_EXECUTION, "invalid Wrangle expression fails execution");
    expect(std::strstr(error, "parse error") != nullptr,
           "invalid Wrangle expression reports parse context");

    const char* palette_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
        {"id":"palette","type":"AttributeWrangle","data":{
          "runOver":"detail",
          "expression":"int buildingIter = chi(\"buildingIter\"); float pct = rand(buildingIter * 12); pct = chramp(\"remapPct\", pct); v@brickCd = vector(chramp(\"brick\", pct)); v@paint1Cd = vector(chramp(\"paint1\", pct));",
          "parameters":"{\"buildingIter\":3}",
          "ramps":"{\"remapPct\":{\"interpolation\":\"linear\",\"keys\":[{\"t\":0,\"color\":[0.0,0.0,0.0]},{\"t\":1,\"color\":[1.0,1.0,1.0]}]},\"brick\":{\"interpolation\":\"linear\",\"keys\":[{\"t\":0,\"color\":[0.2,0.1,0.05]},{\"t\":1,\"color\":[0.6,0.3,0.1]}]},\"paint1\":{\"interpolation\":\"constant\",\"keys\":[{\"t\":0,\"color\":[0.1,0.2,0.3]},{\"t\":0.5,\"color\":[0.4,0.5,0.6]},{\"t\":1,\"color\":[0.7,0.8,0.9]}]}}"
        }},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e1","source":"box","target":"palette"},
        {"id":"e2","source":"palette","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode palette_code = pcg_execute_graph_v2(
        palette_graph, 42, &kind, json_buffer.data(), static_cast<int>(json_buffer.size()),
        mesh_buffer.data(), static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(palette_code == PCG_OK, "palette wrangle graph executes");
    if (palette_code == PCG_OK) {
        const json palette = json::parse(json_buffer.data());
        bool found_brick = false;
        bool found_paint = false;
        if (palette.contains("node_attrs") && palette["node_attrs"].is_array()) {
            for (const auto& attr : palette["node_attrs"]) {
                if (attr.value("node_id", std::string()) != "palette")
                    continue;
                if (attr.value("name", std::string()) == "brickCd" &&
                    attr.value("owner", std::string()) == "detail" &&
                    attr.value("tuple_size", 0) == 3)
                    found_brick = true;
                if (attr.value("name", std::string()) == "paint1Cd" &&
                    attr.value("owner", std::string()) == "detail" &&
                    attr.value("tuple_size", 0) == 3)
                    found_paint = true;
            }
        }
        expect(found_brick, "palette writes brickCd detail vector attribute");
        expect(found_paint, "palette writes paint1Cd detail vector attribute");
    }

    const char* delete_passthrough_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"del","type":"Delete","data":{}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"del"},
        {"id":"e2","source":"del","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode delete_pass_code = pcg_execute_graph_v2(
        delete_passthrough_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(delete_pass_code == PCG_OK, "Delete default passthrough executes");
    expect(index_count == 36, "Delete default passthrough keeps box mesh");

    const char* delete_number_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"del","type":"Delete","data":{
          "entity":"primitives","numberEnable":true,"numberMode":"pattern","numberPattern":"0"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"del"},
        {"id":"e2","source":"del","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode delete_number_code = pcg_execute_graph_v2(
        delete_number_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(delete_number_code == PCG_OK, "Delete number pattern graph executes");
    expect(index_count == 30, "Delete number pattern removes one primitive");

    const char* delete_legacy_string_params_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"del","type":"Delete","data":{
          "entity":"primitives","deleteNonSelected":"false",
          "numberEnable":"true","numberMode":"pattern","numberPattern":"0",
          "numberRangeStart":"0","numberRangeEnd":"0","numberSelectOf":"1",
          "numberSelectOffset":"0","boundingEnable":"false",
          "boundingCenterX":"0","boundingCenterY":"0","boundingCenterZ":"0",
          "boundingSizeX":"1","boundingSizeY":"1","boundingSizeZ":"1",
          "boundingRadius":"1","normalEnable":"false","normalDirX":"0",
          "normalDirY":"1","normalDirZ":"0","normalSpread":"180",
          "degenerateDuplicatePoints":"false","degenerateZeroArea":"false",
          "degenerateOpenFacePerimeter":"false","degenerateTolerance":"0.0001",
          "randomEnable":"false","randomSeed":"0","randomPercent":"100",
          "keepPoints":"false","deleteUnusedGroups":"false"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"del"},
        {"id":"e2","source":"del","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode delete_legacy_string_params_code = pcg_execute_graph_v2(
        delete_legacy_string_params_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(delete_legacy_string_params_code == PCG_OK,
           "Delete accepts legacy string-serialized numeric and boolean parameters");
    expect(index_count == 30,
           "Delete legacy string parameters preserve number-pattern behavior");

    const char* delete_inverse_spline_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"path","type":"CreateSpline","data":{
          "mode":"polyline","closed":false,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":1,\"y\":0,\"z\":0},{\"x\":2,\"y\":0,\"z\":0},{\"x\":3,\"y\":0,\"z\":0}]"}},
        {"id":"del","type":"Delete","data":{
          "numberEnable":true,"numberMode":"pattern","numberPattern":"!*","deleteNonSelected":true}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"path","target":"del"},
        {"id":"e2","source":"del","target":"out"}
      ]
    })";
    const json inverse_spline = execute_json(delete_inverse_spline_graph);
    expect(inverse_spline.contains("splines") && inverse_spline["splines"].empty(),
           "Delete non-selected with !* yields empty splines");

    const char* delete_spline_keep_prim0_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"path","type":"CreateSpline","data":{
          "mode":"polyline","closed":false,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":1,\"y\":0,\"z\":0},{\"x\":2,\"y\":0,\"z\":0},{\"x\":3,\"y\":0,\"z\":0}]"}},
        {"id":"del","type":"Delete","data":{
          "entity":"primitives","group":"0","deleteNonSelected":true,
          "numberEnable":true,"numberMode":"pattern","numberPattern":"!*"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"path","target":"del"},
        {"id":"e2","source":"del","target":"out"}
      ]
    })";
    const json keep_prim0 = execute_json(delete_spline_keep_prim0_graph);
    expect(keep_prim0.contains("splines") && keep_prim0["splines"].is_array() &&
               keep_prim0["splines"].size() == 1,
           "Delete spline primitive group 0 with number !* keeps first primitive");

    const char* delete_group_number_union_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"del","type":"Delete","data":{
          "entity":"primitives","group":"0","deleteNonSelected":true,
          "numberEnable":true,"numberMode":"pattern","numberPattern":"!*"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"del"},
        {"id":"e2","source":"del","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode delete_group_number_union_code = pcg_execute_graph_v2(
        delete_group_number_union_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(delete_group_number_union_code == PCG_OK,
           "Delete group 0 with number !* and invert executes");
    expect(index_count == 6,
           "Delete group 0 with number !* keeps primitive 0 only");

    const char* delete_spline_point_group0_dns_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"path","type":"CreateSpline","data":{
          "mode":"polyline","closed":false,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":1,\"y\":0,\"z\":0},{\"x\":2,\"y\":0,\"z\":0},{\"x\":3,\"y\":0,\"z\":0}]"}},
        {"id":"del","type":"Delete","data":{
          "entity":"points","group":"0","deleteNonSelected":true,
          "numberEnable":true,"numberMode":"pattern","numberPattern":"!*" }},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"path","target":"del"},
        {"id":"e2","source":"del","target":"out"}
      ]
    })";
    const json keep_point0 = execute_json(delete_spline_point_group0_dns_graph);
    expect(keep_point0.contains("splines") && keep_point0["splines"].is_array() &&
               keep_point0["splines"].empty(),
           "Delete spline points group 0 DNS keeps only pt0 (dropped as <2 pts)");

    const char* delete_spline_point_group_last_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"path","type":"CreateSpline","data":{
          "mode":"polyline","closed":false,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":1,\"y\":0,\"z\":0},{\"x\":2,\"y\":0,\"z\":0},{\"x\":3,\"y\":0,\"z\":0}]"}},
        {"id":"del","type":"Delete","data":{
          "entity":"points","group":"3","deleteNonSelected":false}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"path","target":"del"},
        {"id":"e2","source":"del","target":"out"}
      ]
    })";
    const json drop_last = execute_json(delete_spline_point_group_last_graph);
    expect(drop_last.contains("splines") && drop_last["splines"].is_array() &&
               drop_last["splines"].size() == 1 &&
               drop_last["splines"][0].contains("points") &&
               drop_last["splines"][0]["points"].size() == 3,
           "Delete spline points group 3 removes last point");

    const char* delete_spline_point_group1_keep_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"path","type":"CreateSpline","data":{
          "mode":"polyline","closed":false,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":1,\"y\":0,\"z\":0},{\"x\":2,\"y\":0,\"z\":0}]"}},
        {"id":"del","type":"Delete","data":{
          "entity":"points","group":"1","deleteNonSelected":true,
          "numberEnable":true,"numberMode":"pattern","numberPattern":"!*" }},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"path","target":"del"},
        {"id":"e2","source":"del","target":"out"}
      ]
    })";
    const json keep_mid = execute_json(delete_spline_point_group1_keep_graph);
    expect(keep_mid.contains("splines") && keep_mid["splines"].is_array() &&
               keep_mid["splines"].empty(),
           "Delete spline points group 1 DNS keeps only middle point (dropped)");

    const char* delete_multi_edge_prim_group_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":4,"height":0.01,"depth":2}},
        {"id":"cl","type":"ConvertLine","data":{"group":"","connectPath":false,"removeUnusedPoints":true}},
        {"id":"ms","type":"MeasureMesh","data":{
          "group":"","elementType":"primitives","measure":"perimeter","attributeName":"length",
          "useWidth":false}},
        {"id":"sort","type":"SortGeometry","data":{
          "pointMethod":"nochange","primitiveMethod":"attribute",
          "primitiveAttributeName":"length","reversePrimitives":false}},
        {"id":"del","type":"Delete","data":{
          "entity":"primitives","group":"0","deleteNonSelected":true,
          "numberEnable":true,"numberMode":"pattern","numberPattern":"!*" }},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"cl"},
        {"id":"e2","source":"cl","target":"ms"},
        {"id":"e3","source":"ms","target":"sort"},
        {"id":"e4","source":"sort","target":"del"},
        {"id":"e5","source":"del","target":"out"}
      ]
    })";
    const json keep_shortest = execute_json(delete_multi_edge_prim_group_graph);
    expect(keep_shortest.contains("splines") && keep_shortest["splines"].is_array() &&
               keep_shortest["splines"].size() == 1 &&
               keep_shortest["splines"][0].contains("points") &&
               keep_shortest["splines"][0]["points"].size() == 2,
           "ConvertLine+Sort+Delete group 0 keeps one shortest edge");

    const char* param_expr_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":1,"height":1,"depth":1}},
        {"id":"seed","type":"AttributeWrangle","data":{
          "runOver":"detail",
          "expression":"@iteration = 7;"}},
        {"id":"bind","type":"AttributeWrangle","data":{
          "runOver":"points",
          "expression":"@P.y = chi(\"buildingIter\");",
          "parameters":"{\"buildingIter\":{\"expr\":\"@iteration\"}}"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"seed"},
        {"id":"e2","source":"seed","target":"bind"},
        {"id":"e3","source":"bind","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode param_expr_code = pcg_execute_graph_v2(
        param_expr_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(param_expr_code == PCG_OK, "param expr wrangle graph executes");
    if (param_expr_code == PCG_OK && vertex_count > 0) {
        // Mesh binary remaps geometry (x,y,z) → buffer (y,z,x).
        const float* verts = reinterpret_cast<const float*>(
            mesh_buffer.data() + PCG_MESH_BINARY_HEADER_SIZE);
        expect(std::abs(verts[0] - 7.0f) < 1e-4f,
               "param expr @iteration binds to chi(buildingIter) on @P.y");
    }

    const char* group_filter_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"grid","type":"CreateGridMesh","data":{
          "sizeX":2,"sizeY":2,"rows":1,"cols":1,"plane":"xz"}},
        {"id":"mark","type":"AttributeWrangle","data":{
          "runOver":"points",
          "expression":"@group.half = @ptnum < 2;"}},
        {"id":"move","type":"AttributeWrangle","data":{
          "runOver":"points",
          "group":"half",
          "expression":"@P.y = 10;"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"grid","target":"mark"},
        {"id":"e2","source":"mark","target":"move"},
        {"id":"e3","source":"move","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode group_filter_code = pcg_execute_graph_v2(
        group_filter_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(group_filter_code == PCG_OK, "group filter wrangle graph executes");
    if (group_filter_code == PCG_OK && vertex_count >= 4) {
        const float* verts = reinterpret_cast<const float*>(
            mesh_buffer.data() + PCG_MESH_BINARY_HEADER_SIZE);
        int raised = 0;
        int unraised = 0;
        for (int i = 0; i < vertex_count; ++i) {
            const float y = verts[i * 3 + 0]; // geometry Y → mesh buffer X
            if (std::abs(y - 10.0f) < 1e-4f) ++raised;
            else if (std::abs(y) < 1e-4f) ++unraised;
        }
        expect(raised == 2 && unraised == 2, "group filter raises only half of points");
    }

    const char* if_bbox_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"box","type":"CreateBoxMesh","data":{"width":2,"height":4,"depth":6}},
        {"id":"edges","type":"GroupCreate","data":{
          "outputGroup":"all_edges","domain":"edge","mode":"all","enableEdges":true}},
        {"id":"wr","type":"AttributeWrangle","data":{
          "runOver":"points",
          "expression":"float len = length(getbbox_size(0)); float n = nedgesgroup(0, \"all_edges\"); if (len > 1 && n > 0) { @P.y = 9; } else { @P.y = 0; }"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"box","target":"edges"},
        {"id":"e2","source":"edges","target":"wr"},
        {"id":"e3","source":"wr","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode if_bbox_code = pcg_execute_graph_v2(
        if_bbox_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(if_bbox_code == PCG_OK, "if/else + getbbox_size/nedgesgroup graph executes");
    if (if_bbox_code == PCG_OK && vertex_count > 0) {
        const float* verts = reinterpret_cast<const float*>(
            mesh_buffer.data() + PCG_MESH_BINARY_HEADER_SIZE);
        expect(std::abs(verts[0] - 9.0f) < 1e-4f,
               "if/else + getbbox_size/length/nedgesgroup sets @P.y");
    }

    // Second input (pin 1): getbbox_size(1) reads the optional geometry on in1.
    const char* multi_input_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"main","type":"CreateBoxMesh","data":{"width":1,"height":1,"depth":1}},
        {"id":"ref","type":"CreateBoxMesh","data":{"width":10,"height":2,"depth":4}},
        {"id":"wr","type":"AttributeWrangle","data":{
          "runOver":"points",
          "expression":"if (length(getbbox_size(1)) > 5) { @P.y = 7; } else { @P.y = 0; }"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"main","target":"wr","sourceHandle":"out","targetHandle":"in"},
        {"id":"e2","source":"ref","target":"wr","sourceHandle":"out","targetHandle":"in1"},
        {"id":"e3","source":"wr","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode multi_input_code = pcg_execute_graph_v2(
        multi_input_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(multi_input_code == PCG_OK, "multi-input wrangle graph executes");
    if (multi_input_code == PCG_OK && vertex_count > 0) {
        const float* verts = reinterpret_cast<const float*>(
            mesh_buffer.data() + PCG_MESH_BINARY_HEADER_SIZE);
        // Input 0 is 1×1×1 (len≈1.7); input 1 is 10×2×4 (len≈11). Must use pin 1.
        expect(std::abs(verts[0] - 7.0f) < 1e-4f,
               "getbbox_size(1) uses second geometry input");
    }

    // Secondary spline input (ConvertLine-style): getbbox_size(1) must accept splines.
    const char* spline_bbox_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"main","type":"CreateBoxMesh","data":{"width":1,"height":1,"depth":1}},
        {"id":"path","type":"CreateSpline","data":{
          "type":"polyline","closed":false,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":10,\"y\":0,\"z\":0}]"}},
        {"id":"wr","type":"AttributeWrangle","data":{
          "runOver":"detail",
          "expression":"if (length(getbbox_size(1)) > 5) { @hasFE = 1; } else { @hasFE = 0; }"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"main","target":"wr","sourceHandle":"out","targetHandle":"in"},
        {"id":"e2","source":"path","target":"wr","sourceHandle":"out","targetHandle":"in1"},
        {"id":"e3","source":"wr","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode spline_bbox_code = pcg_execute_graph_v2(
        spline_bbox_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(spline_bbox_code == PCG_OK, "getbbox_size(1) accepts spline secondary input");

    // Missing secondary input must not hard-fail (empty bbox → length 0).
    const char* missing_in1_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id":"main","type":"CreateBoxMesh","data":{"width":1,"height":1,"depth":1}},
        {"id":"wr","type":"AttributeWrangle","data":{
          "runOver":"detail",
          "expression":"float lengthSide = length(getbbox_size(1)); if (lengthSide < 1.25) { @hasFE = 0; } else { @hasFE = 1; }"}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges": [
        {"id":"e1","source":"main","target":"wr"},
        {"id":"e2","source":"wr","target":"out"}
      ]
    })";
    std::memset(error, 0, sizeof(error));
    const PcgResultCode missing_in1_code = pcg_execute_graph_v2(
        missing_in1_graph, 42, &kind, json_buffer.data(),
        static_cast<int>(json_buffer.size()), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), &vertex_count, &index_count,
        error, sizeof(error));
    expect(missing_in1_code == PCG_OK, "getbbox_size(1) with missing in1 returns empty bbox");

    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
