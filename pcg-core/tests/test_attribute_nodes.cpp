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

    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
