#include "graph_executor.hpp"
#include "graph_parser.hpp"
#include "data/pcg_attribute_table.hpp"
#include "elements/facade_foundation_algorithms.hpp"

#include <cmath>
#include <cstdio>
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
    if (execute_code != PCG_OK && error[0] != '\0')
        std::printf("  error: %s\n", error);
    return result;
}

} // namespace

int main()
{
    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":2.0,"height":2.0,"depth":2.0}},
            {"id":"wrangle","type":"AttributeWrangle","data":{
              "runOver":"primitives",
              "expression":"@height = 1.5 + @primnum;"
            }},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"wrangle","sourceHandle":"out","targetHandle":"in"},
            {"source":"wrangle","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "wrangle primitives keeps geometry");
        if (result.source_geometry) {
            const auto* attr = result.source_geometry->attributes().find(
                pcg::internal::data::AttributeOwner::Primitive, "height");
            expect(attr != nullptr && attr->schema().type ==
                       pcg::internal::data::AttributeType::Float,
                   "prim height attribute created");
            if (attr && !attr->float_values().empty())
                expect(std::abs(attr->float_values()[0] - 1.5) < 1e-9,
                       "first prim height is 1.5");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"grid","type":"CreateGridMesh","data":{"sizeX":2.0,"sizeY":2.0,"rows":1,"cols":1}},
            {"id":"prim","type":"PrimitiveTransform","data":{"scale":0.5}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"grid","target":"prim","sourceHandle":"out","targetHandle":"in"},
            {"source":"prim","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "PrimitiveTransform keeps geometry");
        if (result.source_geometry && !result.source_geometry->points().empty()) {
            double max_abs = 0.0;
            for (const auto& p : result.source_geometry->points()) {
                max_abs = std::max(max_abs, std::abs(p.x));
                max_abs = std::max(max_abs, std::abs(p.z));
            }
            expect(max_abs < 0.6, "PrimitiveTransform shrinks about centroid");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
            {"id":"begin","type":"ForEachBegin","data":{"method":"primitive"}},
            {"id":"end","type":"ForEachEnd","data":{"gatherMethod":"merge"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"begin","sourceHandle":"out","targetHandle":"in"},
            {"source":"begin","target":"end","sourceHandle":"out","targetHandle":"in"},
            {"source":"end","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "ForEach primitive merge keeps geometry");
        if (result.source_geometry)
            expect(result.source_geometry->faces().size() == 6,
                   "ForEach merges six single-face pieces");
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
            {"id":"begin","type":"ForEachBegin","data":{"method":"count","iterations":3}},
            {"id":"xform","type":"TransformMesh","data":{"translateY":2.0}},
            {"id":"end","type":"ForEachEnd","data":{"gatherMethod":"feedback"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"begin","sourceHandle":"out","targetHandle":"in"},
            {"source":"begin","target":"xform","sourceHandle":"out","targetHandle":"in"},
            {"source":"xform","target":"end","sourceHandle":"out","targetHandle":"in"},
            {"source":"end","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "ForEach count feedback keeps geometry");
        if (result.source_geometry) {
            double min_y = 1e9;
            double max_y = -1e9;
            for (const auto& p : result.source_geometry->points()) {
                min_y = std::min(min_y, p.y);
                max_y = std::max(max_y, p.y);
            }
            expect(max_y > 5.0, "count feedback translated box upward");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":2.0,"height":2.0,"depth":2.0}},
            {"id":"lines","type":"ConvertLine","data":{"mode":"all"}},
            {"id":"resample","type":"ResampleSpline","data":{"mode":"count","pointCount":3}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"lines","sourceHandle":"out","targetHandle":"in"},
            {"source":"lines","target":"resample","sourceHandle":"out","targetHandle":"in"},
            {"source":"resample","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        char error[2048] = {};
        pcg::internal::Graph graph_obj;
        const PcgResultCode parse_code =
            pcg::internal::parse_graph(graph.c_str(), graph_obj, error, sizeof(error));
        expect(parse_code == PCG_OK, "ConvertLine graph parses");
        GraphExecutionResult result;
        const PcgResultCode execute_code = pcg::internal::execute_graph(
            graph_obj, 42, result, error, sizeof(error));
        expect(execute_code == PCG_OK, error[0] == '\0' ? "ConvertLine chain cooks" : error);
        if (execute_code == PCG_OK) {
            bool found_lines = false;
            for (const auto& entry : result.json["node_stats"]) {
                if (entry.value("node_id", std::string()) != "lines")
                    continue;
                found_lines = true;
                expect(entry.value("point_count", 0) > 0, "ConvertLine node_stats point_count > 0");
                expect(entry.value("face_count", 0) > 0, "ConvertLine node_stats face_count > 0");
                expect(entry.value("vertex_count", 0) > 0, "ConvertLine node_stats vertex_count > 0");
                expect(entry.value("has_bbox", false), "ConvertLine node_stats has_bbox");
                break;
            }
            expect(found_lines, "ConvertLine node_stats entry present");
        }
    }

    {
        using pcg::internal::data::PcgGeometry;
        using pcg::internal::elements::ConvertLineOptions;
        using pcg::internal::elements::convert_line_geometry;

        PcgGeometry grid;
        grid.points_mut() = {
            {0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}, {4.0, 0.0, 4.0}, {0.0, 0.0, 4.0}};
        grid.faces_mut() = {{0, 1, 2, 3}};

        ConvertLineOptions opts;
        opts.mode = "all";
        opts.connect_path = true;
        opts.compute_length = true;
        opts.length_attribute = "restlength";
        const auto splines = convert_line_geometry(grid, opts);
        expect(splines.splines().size() == 1,
               "ConvertLine connect path chains square boundary into one polyline");
        if (!splines.splines().empty()) {
            const auto& spline = splines.splines().front();
            expect(spline.points.size() >= 4, "ConvertLine connected polyline has corners");
            expect(spline.attributes.contains("restlength"),
                   "ConvertLine compute length writes attribute");
        }

        ConvertLineOptions legacy;
        legacy.mode = "unshared";
        const auto legacy_splines = convert_line_geometry(grid, legacy);
        expect(legacy_splines.splines().size() == 1,
               "legacy mode=unshared still filters to boundary loop");
    }

    {
        using pcg::internal::data::AttributeOwner;
        using pcg::internal::data::PcgGeometry;
        using pcg::internal::elements::ConvertLineOptions;
        using pcg::internal::elements::convert_line_geometry;

        PcgGeometry grid;
        grid.points_mut() = {
            {0.0, 0.0, 0.0}, {4.0, 0.0, 0.0}, {4.0, 0.0, 4.0}, {0.0, 0.0, 4.0}};
        grid.faces_mut() = {{0, 1, 2, 3}};

        auto& detail = grid.attributes().create_int(AttributeOwner::Detail, "numFloors", 1);
        detail.resize(1);
        detail.int_values_mut()[0] = 3;

        auto& piece = grid.attributes().create_int(AttributeOwner::Primitive, "piece", 1);
        piece.resize(1);
        piece.int_values_mut()[0] = 7;

        ConvertLineOptions opts;
        opts.mode = "all";
        opts.connect_path = true;
        const auto splines = convert_line_geometry(grid, opts);
        expect(splines.splines().size() == 1, "ConvertLine attribute propagation emits spline");
        expect(splines.metadata().has("numFloors"), "ConvertLine copies detail attributes to metadata");
        if (splines.metadata().has("numFloors"))
            expect(splines.metadata().get("numFloors").get<int64_t>() == 3,
                   "ConvertLine detail value preserved");
        if (!splines.splines().empty()) {
            const auto& attrs = splines.splines().front().attributes;
            expect(attrs.contains("piece") && attrs["piece"].get<int64_t>() == 7,
                   "ConvertLine copies primitive attributes to spline");
            expect(attrs.contains("primnum") && attrs["primnum"].get<int>() == 0,
                   "ConvertLine writes primnum from source face");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":2.0,"height":2.0,"depth":2.0}},
            {"id":"wr","type":"AttributeWrangle","data":{"runOver":"detail","expression":"@numFloors = 2;"}},
            {"id":"lines","type":"ConvertLine","data":{"mode":"all"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"wr","sourceHandle":"out","targetHandle":"in"},
            {"source":"wr","target":"lines","sourceHandle":"out","targetHandle":"in"},
            {"source":"lines","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.json.contains("node_attrs"), "ConvertLine graph reports node_attrs");
        bool lines_has_detail = false;
        if (result.json.contains("node_attrs") && result.json["node_attrs"].is_array()) {
            for (const auto& attr : result.json["node_attrs"]) {
                if (attr.value("node_id", "") == "lines" && attr.value("owner", "") == "detail" &&
                    attr.value("name", "") == "numFloors")
                    lines_has_detail = true;
            }
        }
        expect(lines_has_detail, "ConvertLine graph keeps detail attrs visible on spline output");
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":2.0,"height":2.0,"depth":2.0}},
            {"id":"clip","type":"Clip","data":{"originY":0.0,"normalY":1.0,"keepPositive":true}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"clip","sourceHandle":"out","targetHandle":"in"},
            {"source":"clip","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "Clip keeps geometry");
        if (result.source_geometry) {
            for (const auto& p : result.source_geometry->points())
                expect(p.y >= -1e-9, "Clip keeps positive side");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"grid","type":"CreateGridMesh","data":{"sizeX":8.0,"sizeY":6.0,"rows":1,"cols":1,"plane":"xz"}},
            {"id":"cent","type":"ExtractCentroid","data":{"method":"primitives"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"grid","target":"cent","sourceHandle":"out","targetHandle":"in"},
            {"source":"cent","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.points != nullptr && result.points->points().size() == 1,
               "ExtractCentroid emits one point for single face");
        if (result.points && !result.points->points().empty()) {
            const auto& attrs = result.points->points()[0].attributes;
            expect(attrs.contains("lotSizeX") && attrs.contains("lotSizeZ"),
                   "ExtractCentroid writes lotSizeX/Z");
            if (attrs.contains("lotSizeX") && attrs.contains("lotSizeZ")) {
                expect(std::abs(attrs["lotSizeX"].get<double>() - 8.0) < 1e-6,
                       "lotSizeX matches grid sizeX");
                expect(std::abs(attrs["lotSizeZ"].get<double>() - 6.0) < 1e-6,
                       "lotSizeZ matches grid sizeY on xz");
            }
        }
    }

    {
        // Houdini-style GroupTransfer: nearest face within threshold inherits source group.
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"target","type":"CreateGridMesh","data":{"sizeX":2.0,"sizeY":2.0,"rows":1,"cols":2,"plane":"xz"}},
            {"id":"source","type":"CreateGridMesh","data":{"sizeX":1.0,"sizeY":2.0,"rows":1,"cols":1,"plane":"xz"}},
            {"id":"tag","type":"GroupCreate","data":{
              "outputGroup":"near","domain":"face","initialMerge":"replace",
              "enableBaseGroup":false,"enableBounding":false,"enableNormals":false,
              "enableEdges":false,"enableRandom":true,"randomChance":1.0,"randomSeed":0
            }},
            {"id":"xfer","type":"GroupTransfer","data":{
              "transferPrimitiveGroups":true,"primitiveGroups":"near",
              "transferPointGroups":false,"transferEdgeGroups":false,
              "groupNameConflict":"overwrite",
              "enableDistanceThreshold":true,"distanceThreshold":0.6,
              "createEmptyGroups":true
            }},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"source","target":"tag","sourceHandle":"out","targetHandle":"in"},
            {"source":"target","target":"xfer","sourceHandle":"out","targetHandle":"target"},
            {"source":"tag","target":"xfer","sourceHandle":"out","targetHandle":"source"},
            {"source":"xfer","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "GroupTransfer keeps geometry");
        if (result.source_geometry) {
            const auto members = result.source_geometry->groups().members(
                pcg::internal::geometry::GroupDomain::Face, "near");
            expect(!members.empty(), "GroupTransfer copies nearest face group");
            expect(result.source_geometry->groups().has_group(
                       pcg::internal::geometry::GroupDomain::Face, "near"),
                   "GroupTransfer creates destination group");
        }
    }

    {
        // Legacy groupName/domain/distance still cooks.
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"target","type":"CreateGridMesh","data":{"sizeX":2.0,"sizeY":2.0,"rows":1,"cols":1,"plane":"xz"}},
            {"id":"source","type":"CreateGridMesh","data":{"sizeX":2.0,"sizeY":2.0,"rows":1,"cols":1,"plane":"xz"}},
            {"id":"tag","type":"GroupCreate","data":{
              "outputGroup":"legacy","domain":"face","initialMerge":"replace",
              "enableBaseGroup":false,"enableBounding":false,"enableNormals":false,
              "enableEdges":false,"enableRandom":true,"randomChance":1.0,"randomSeed":0
            }},
            {"id":"xfer","type":"GroupTransfer","data":{
              "groupName":"legacy","domain":"face","distance":1.0
            }},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"source","target":"tag","sourceHandle":"out","targetHandle":"in"},
            {"source":"target","target":"xfer","sourceHandle":"out","targetHandle":"target"},
            {"source":"tag","target":"xfer","sourceHandle":"out","targetHandle":"source"},
            {"source":"xfer","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "legacy GroupTransfer keeps geometry");
        if (result.source_geometry) {
            expect(result.source_geometry->groups().has_group(
                       pcg::internal::geometry::GroupDomain::Face, "legacy"),
                   "legacy GroupTransfer still transfers named face group");
        }
    }

    {
        // Distance threshold must change transferred point-group membership:
        // outer-ring source vs dense target grid (Houdini proximity filter).
        const auto run_xfer = [](double threshold) {
            char thr[64];
            std::snprintf(thr, sizeof(thr), "%.8g", threshold);
            const std::string graph =
                std::string(R"JSON({
              "version":"1.0",
              "nodes":[
                {"id":"src","type":"CreateGridMesh","data":{"sizeX":10.0,"sizeY":10.0,"rows":1,"cols":1,"plane":"xz"}},
                {"id":"outline","type":"ConvertLine","data":{"mode":"unshared","edgeGroup":""}},
                {"id":"rs","type":"Resample","data":{
                  "useMaxSegmentLength":true,"maxSegmentLength":0.5,"useMaxSegments":false,
                  "method":"evenLength","measure":"arc","treatPolygonsAs":"straight","levelOfDetail":1
                }},
                {"id":"tag","type":"GroupCreate","data":{
                  "outputGroup":"ring","domain":"point","initialMerge":"replace",
                  "enableBaseGroup":true,"baseGroup":"",
                  "enableBounding":false,"enableNormals":false,"enableEdges":false,"enableRandom":false
                }},
                {"id":"tgt","type":"CreateGridMesh","data":{"sizeX":10.0,"sizeY":10.0,"rows":4,"cols":4,"plane":"xz"}},
                {"id":"xfer","type":"GroupTransfer","data":{
                  "transferPrimitiveGroups":false,"transferPointGroups":true,"pointGroups":"ring",
                  "transferEdgeGroups":false,"groupNameConflict":"overwrite",
                  "enableDistanceThreshold":true,"distanceThreshold":)JSON") +
                thr +
                R"JSON(,"createEmptyGroups":true}},
                {"id":"out","type":"Output","data":{}}
              ],
              "edges":[
                {"source":"src","target":"outline","sourceHandle":"out","targetHandle":"in"},
                {"source":"outline","target":"rs","sourceHandle":"out","targetHandle":"in"},
                {"source":"rs","target":"tag","sourceHandle":"out","targetHandle":"in"},
                {"source":"tgt","target":"xfer","sourceHandle":"out","targetHandle":"target"},
                {"source":"tag","target":"xfer","sourceHandle":"out","targetHandle":"source"},
                {"source":"xfer","target":"out","sourceHandle":"out","targetHandle":"in"}
              ]
            })JSON";
            return execute(graph);
        };

        const auto tight = run_xfer(0.25);
        const auto loose = run_xfer(6.0);
        expect(tight.source_geometry != nullptr && loose.source_geometry != nullptr,
               "GroupTransfer distance probe keeps geometry");
        if (tight.source_geometry && loose.source_geometry) {
            const auto tight_n = tight.source_geometry->groups()
                                     .members(pcg::internal::geometry::GroupDomain::Point, "ring")
                                     .size();
            const auto loose_n = loose.source_geometry->groups()
                                     .members(pcg::internal::geometry::GroupDomain::Point, "ring")
                                     .size();
            std::printf("  distance probe members: tight(0.25)=%zu loose(6)=%zu\n", tight_n,
                        loose_n);
            expect(tight_n < loose_n,
                   "GroupTransfer distanceThreshold filters point membership");
            expect(loose_n > 0, "loose threshold still transfers some points");
        }
    }

    {
        // Mimics Unity node-preview upstream cook: ForEachBegin without End, sink id
        // matches PcgGraphPreviewSubgraph.PreviewSinkNodeId.
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
            {"id":"begin","type":"ForEachBegin","data":{"method":"primitive"}},
            {"id":"prim","type":"PrimitiveTransform","data":{"scale":0.5}},
            {"id":"__pcg_preview_sink__","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"begin","sourceHandle":"out","targetHandle":"in"},
            {"source":"begin","target":"prim","sourceHandle":"out","targetHandle":"in"},
            {"source":"prim","target":"__pcg_preview_sink__","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr,
               "preview open ForEach cooks first primitive piece");
        if (result.source_geometry)
            expect(result.source_geometry->faces().size() == 1,
                   "preview open ForEach uses first iteration only");
    }

    {
        // Nested open outer + closed inner, mimicking lot-city floor ring under lot ForEach.
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
            {"id":"obegin","type":"ForEachBegin","data":{"method":"primitive"}},
            {"id":"prim","type":"PrimitiveTransform","data":{"scale":0.8}},
            {"id":"ibegin","type":"ForEachBegin","data":{"method":"count","iterations":3}},
            {"id":"xform","type":"TransformMesh","data":{"translateY":1.0}},
            {"id":"iend","type":"ForEachEnd","data":{"gatherMethod":"feedback"}},
            {"id":"__pcg_preview_sink__","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"obegin","sourceHandle":"out","targetHandle":"in"},
            {"source":"obegin","target":"prim","sourceHandle":"out","targetHandle":"in"},
            {"source":"prim","target":"ibegin","sourceHandle":"out","targetHandle":"in"},
            {"source":"ibegin","target":"xform","sourceHandle":"out","targetHandle":"in"},
            {"source":"xform","target":"iend","sourceHandle":"out","targetHandle":"in"},
            {"source":"iend","target":"__pcg_preview_sink__","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr,
               "nested preview open outer ForEach cooks");
        if (result.source_geometry) {
            expect(result.source_geometry->faces().size() == 1,
                   "nested preview open outer still uses first outer piece");
            double max_y = -1e9;
            for (const auto& p : result.source_geometry->points())
                max_y = std::max(max_y, p.y);
            expect(max_y > 2.5, "inner count feedback still runs fully under open outer");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
            {"id":"begin","type":"ForEachBegin","data":{"method":"primitive"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"begin","sourceHandle":"out","targetHandle":"in"},
            {"source":"begin","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        char error[2048] = {};
        pcg::internal::Graph graph_obj;
        const PcgResultCode parse_code =
            pcg::internal::parse_graph(graph.c_str(), graph_obj, error, sizeof(error));
        expect(parse_code == PCG_OK, "unmatched ForEachBegin graph parses");
        GraphExecutionResult result;
        const PcgResultCode execute_code = pcg::internal::execute_graph(
            graph_obj, 42, result, error, sizeof(error));
        expect(execute_code != PCG_OK,
               "unmatched ForEachBegin without preview sink still fails");
        expect(std::string(error).find("ForEachBegin has no matching ForEachEnd") !=
                   std::string::npos,
               "unmatched ForEachBegin reports pairing error");
    }

    std::printf("failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
