#include "graph_executor.hpp"
#include "graph_parser.hpp"
#include "geometry/group_table.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using pcg::internal::GraphExecutionResult;

int failures = 0;

void expect(bool condition, const char* message)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition)
        ++failures;
}

GraphExecutionResult execute(const std::string& graph_json, int graph_seed = 42)
{
    char error[2048] = {};
    pcg::internal::Graph graph;
    const PcgResultCode parse_code =
        pcg::internal::parse_graph(graph_json.c_str(), graph, error, sizeof(error));
    expect(parse_code == PCG_OK, error[0] == '\0' ? "graph parses" : error);

    GraphExecutionResult result;
    if (parse_code != PCG_OK)
        return result;
    const PcgResultCode execute_code = pcg::internal::execute_graph(
        graph, graph_seed, result, error, sizeof(error));
    expect(execute_code == PCG_OK, error[0] == '\0' ? "graph executes" : error);
    return result;
}

double max_abs_coordinate(const pcg::internal::data::PcgGeometry& geometry)
{
    double value = 0.0;
    for (const auto& point : geometry.points()) {
        value = std::max(value, std::abs(point.x));
        value = std::max(value, std::abs(point.y));
        value = std::max(value, std::abs(point.z));
    }
    return value;
}

} // namespace

int main()
{
    const std::string copy_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"prototype","type":"CreateBoxMesh","data":{"width":1.0,"height":2.0,"depth":1.0}},
        {"id":"group","type":"GroupCreate","data":{"outputGroup":"facade_edges","domain":"edge","mode":"angle","minEdgeAngle":30.0}},
        {"id":"uv","type":"UVTexture","data":{"projection":"planar","axis":"y","scaleU":1.0,"scaleV":1.0,"offsetU":0.0,"offsetV":0.0}},
        {"id":"color","type":"VertexColor","data":{"r":0.2,"g":0.4,"b":0.8,"a":1.0}},
        {"id":"material","type":"AssignMaterial","data":{"materialName":"facade"}},
        {"id":"grid","type":"CreatePointGrid","data":{"pointCountX":2,"pointCountY":2,"spacing":4.0}},
        {"id":"random","type":"AttributeRandomize","data":{"seed":17,"translateY":0.5,"rotateY":30.0,"scaleMin":0.8,"scaleMax":1.2}},
        {"id":"copy","type":"CopyMeshToPoints","data":{}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"source":"prototype","target":"group","sourceHandle":"out","targetHandle":"in"},
        {"source":"group","target":"uv","sourceHandle":"out","targetHandle":"in"},
        {"source":"uv","target":"color","sourceHandle":"out","targetHandle":"in"},
        {"source":"color","target":"material","sourceHandle":"out","targetHandle":"in"},
        {"source":"grid","target":"random","sourceHandle":"out","targetHandle":"in"},
        {"source":"material","target":"copy","sourceHandle":"out","targetHandle":"prototype"},
        {"source":"random","target":"copy","sourceHandle":"out","targetHandle":"points"},
        {"source":"copy","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";

    const auto copied = execute(copy_graph);
    expect(copied.source_geometry != nullptr, "CopyMeshToPoints keeps Geometry output");
    if (copied.source_geometry) {
        expect(copied.source_geometry->points().size() == 32,
               "point count times prototype point count");
        expect(copied.source_geometry->faces().size() == 24,
               "point count times prototype face count");
        expect(copied.source_geometry->has_uvs() && copied.source_geometry->uvs().size() == 32,
               "CopyMeshToPoints preserves UVs");
        expect(copied.source_geometry->has_corner_uvs() &&
                   copied.source_geometry->corner_uvs().size() ==
                       static_cast<size_t>(copied.source_geometry->corner_count()),
               "CopyMeshToPoints preserves corner UVs");
        expect(copied.source_geometry->has_colors() && copied.source_geometry->colors().size() == 32,
               "CopyMeshToPoints preserves colors");
        expect(copied.source_geometry->has_face_materials() &&
                   copied.source_geometry->face_materials().size() == 24,
               "CopyMeshToPoints preserves face materials");
        const auto& edge_group = copied.source_geometry->groups().members(
            pcg::internal::geometry::GroupDomain::Edge, "facade_edges");
        expect(edge_group.size() == 48,
               "CopyMeshToPoints remaps and preserves edge groups for every copy");
    }

    const std::string random_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"grid","type":"CreatePointGrid","data":{"pointCountX":3,"pointCountY":2,"spacing":2.0}},
        {"id":"random","type":"AttributeRandomize","data":{"seed":9,"translateX":1.0,"translateY":0.5,"translateZ":2.0,"rotateX":10.0,"rotateY":20.0,"rotateZ":30.0,"scaleMin":0.75,"scaleMax":1.25}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"source":"grid","target":"random","sourceHandle":"out","targetHandle":"in"},
        {"source":"random","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";

    const auto random_a = execute(random_graph, 123);
    const auto random_b = execute(random_graph, 123);
    const auto random_c = execute(random_graph, 124);
    expect(random_a.points && random_b.points && random_c.points,
           "AttributeRandomize outputs points");
    if (random_a.points && random_b.points && random_c.points) {
        expect(random_a.points->to_json() == random_b.points->to_json(),
               "same graph seed and node seed are reproducible");
        expect(random_a.points->to_json() != random_c.points->to_json(),
               "different graph seed changes randomized points");
        bool has_transform_attributes = true;
        for (const auto& point : random_a.points->points()) {
            has_transform_attributes = has_transform_attributes &&
                point.attributes.contains("rotationX") &&
                point.attributes.contains("rotationY") &&
                point.attributes.contains("rotationZ") &&
                point.attributes.contains("scale");
        }
        expect(has_transform_attributes, "rotation and scale attributes are written");
    }

    const std::string color_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"prototype","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
        {"id":"grid","type":"CreatePointGrid","data":{"pointCountX":2,"pointCountY":1,"spacing":3.0}},
        {"id":"random","type":"AttributeRandomize","data":{
          "seed":3,
          "colorMinR":0.2,"colorMinG":0.4,"colorMinB":0.6,
          "colorMaxR":0.2,"colorMaxG":0.4,"colorMaxB":0.6,
          "materialNames":"brick,stucco"}},
        {"id":"copy","type":"CopyMeshToPoints","data":{}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"source":"grid","target":"random","sourceHandle":"out","targetHandle":"in"},
        {"source":"prototype","target":"copy","sourceHandle":"out","targetHandle":"prototype"},
        {"source":"random","target":"copy","sourceHandle":"out","targetHandle":"points"},
        {"source":"copy","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";

    const auto colored = execute(color_graph);
    expect(colored.source_geometry != nullptr, "Cd CopyMeshToPoints keeps Geometry");
    if (colored.source_geometry) {
        expect(colored.source_geometry->has_colors() &&
                   colored.source_geometry->colors().size() == 16,
               "point Cd fills copied vertex colors");
        if (colored.source_geometry->has_colors() &&
            !colored.source_geometry->colors().empty()) {
            const auto& c0 = colored.source_geometry->colors()[0];
            expect(std::abs(c0.r - 0.2) < 0.000001 &&
                       std::abs(c0.g - 0.4) < 0.000001 &&
                       std::abs(c0.b - 0.6) < 0.000001,
                   "point Cd RGB is applied to copy vertices");
        }
        expect(colored.source_geometry->has_face_materials() &&
                   colored.source_geometry->face_materials().size() == 12,
               "point material fills face materials");
        if (colored.source_geometry->has_face_materials() &&
            !colored.source_geometry->face_materials().empty()) {
            const std::string& mat = colored.source_geometry->face_materials()[0];
            expect(mat == "brick" || mat == "stucco",
                   "point material chooses from materialNames");
        }
    }

    const auto color_points = execute(R"({
      "version":"1.0",
      "nodes":[
        {"id":"grid","type":"CreatePointGrid","data":{"pointCountX":2,"pointCountY":1,"spacing":1.0}},
        {"id":"random","type":"AttributeRandomize","data":{
          "seed":11,
          "colorMinR":0.1,"colorMinG":0.2,"colorMinB":0.3,
          "colorMaxR":0.9,"colorMaxG":0.8,"colorMaxB":0.7}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"source":"grid","target":"random","sourceHandle":"out","targetHandle":"in"},
        {"source":"random","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })");
    expect(color_points.points != nullptr, "AttributeRandomize color mode outputs points");
    if (color_points.points) {
        bool has_cd = true;
        for (const auto& point : color_points.points->points())
            has_cd = has_cd && point.attributes.contains("Cd");
        expect(has_cd, "AttributeRandomize writes Cd attributes");
    }

    const std::string switch_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"small","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
        {"id":"large","type":"CreateBoxMesh","data":{"width":6.0,"height":4.0,"depth":2.0}},
        {"id":"switch","type":"Switch","data":{"index":1}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"source":"small","target":"switch","sourceHandle":"out","targetHandle":"in0"},
        {"source":"large","target":"switch","sourceHandle":"out","targetHandle":"in1"},
        {"source":"switch","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";

    const auto switched = execute(switch_graph);
    expect(switched.source_geometry != nullptr, "Switch preserves selected Geometry payload");
    if (switched.source_geometry)
        expect(std::abs(max_abs_coordinate(*switched.source_geometry) - 3.0) < 0.000000001,
               "Switch index selects the requested input");

    const std::string switch_clamp_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"last","type":"CreateBoxMesh","data":{"width":8.0,"height":2.0,"depth":2.0}},
        {"id":"switch","type":"Switch","data":{"index":99}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"source":"last","target":"switch","sourceHandle":"out","targetHandle":"in3"},
        {"source":"switch","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";

    const auto clamped = execute(switch_clamp_graph);
    expect(clamped.source_geometry != nullptr, "Switch clamps out-of-range index to input 3");
    if (clamped.source_geometry)
        expect(std::abs(max_abs_coordinate(*clamped.source_geometry) - 4.0) < 0.000000001,
               "Switch clamp preserves the selected geometry");

    const std::string emission_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
        {"id":"vc","type":"VertexColor","data":{
          "r":0.1,"g":0.1,"b":0.1,"a":1.0,
          "emission":0.75,
          "emissionColorR":1.0,"emissionColorG":0.8,"emissionColorB":0.2}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"source":"box","target":"vc","sourceHandle":"out","targetHandle":"in"},
        {"source":"vc","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    const auto emission = execute(emission_graph);
    expect(emission.source_geometry != nullptr, "VertexColor emission keeps Geometry");
    if (emission.source_geometry && emission.source_geometry->has_colors() &&
        !emission.source_geometry->colors().empty()) {
        const auto& c = emission.source_geometry->colors()[0];
        expect(std::abs(c.r - 1.0) < 0.000001 &&
                   std::abs(c.g - 0.8) < 0.000001 &&
                   std::abs(c.b - 0.2) < 0.000001 &&
                   std::abs(c.a - 0.75) < 0.000001,
               "emission overrides RGB and writes alpha mask");
    }

    if (failures != 0) {
        std::printf("%d building node test(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("All building node tests passed\n");
    return EXIT_SUCCESS;
}
