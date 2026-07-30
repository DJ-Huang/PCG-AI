#include "graph_executor.hpp"
#include "graph_parser.hpp"
#include "data/pcg_attribute_table.hpp"
#include "data/pcg_geometry.hpp"
#include "elements/facade_foundation_algorithms.hpp"
#include "elements/topology_parity_algorithms.hpp"
#include "elements/node_contracts.hpp"
#include "geometry/spline_geometry.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

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
    const PcgResultCode execute_code =
        pcg::internal::execute_graph(graph, graph_seed, result, error, sizeof(error));
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
            {"id":"grid","type":"CreateGridMesh","data":{"sizeX":2.0,"sizeY":2.0,"rows":1,"cols":1}},
            {"id":"measure","type":"MeasureMesh","data":{
              "elementType":"primitives",
              "measure":"perimeter",
              "accumulate":"perElement",
              "attributeName":"length",
              "useTotalAttribute":true,
              "totalAttributeName":"totalperimeter"
            }},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"grid","target":"measure","sourceHandle":"out","targetHandle":"in"},
            {"source":"measure","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "MeasureMesh keeps geometry");
        if (result.source_geometry) {
            const auto* prim = result.source_geometry->attributes().find(
                pcg::internal::data::AttributeOwner::Primitive, "length");
            expect(prim != nullptr && prim->schema().type ==
                       pcg::internal::data::AttributeType::Float &&
                       !prim->float_values().empty(),
                   "MeasureMesh writes primitive length");
            if (prim && !prim->float_values().empty())
                expect(prim->float_values()[0] > 7.0, "grid face perimeter ~8");
            const auto* total = result.source_geometry->attributes().find(
                pcg::internal::data::AttributeOwner::Detail, "totalperimeter");
            expect(total != nullptr && !total->float_values().empty() &&
                       total->float_values()[0] > 7.0,
                   "MeasureMesh writes detail totalperimeter");
        }
    }

    {
        // Direct API: ConvertLine-style polylines → Measure length attributes.
        using pcg::internal::data::PcgGeometry;
        using pcg::internal::elements::ConvertLineOptions;
        using pcg::internal::elements::MeasureMeshOptions;
        using pcg::internal::elements::convert_line_geometry;
        using pcg::internal::elements::measure_spline_data;

        PcgGeometry grid;
        grid.points_mut() = {{0, 0, 0}, {2, 0, 0}, {2, 0, 2}, {0, 0, 2}};
        grid.faces_mut() = {{0, 1, 2, 3}};
        ConvertLineOptions convert_opts;
        convert_opts.mode = "unshared";
        convert_opts.connect_path = true;
        const auto lines = convert_line_geometry(grid, convert_opts);
        expect(!lines.splines().empty(), "ConvertLine produces splines");

        MeasureMeshOptions measure_opts;
        measure_opts.measure = "perimeter";
        measure_opts.attribute_name = "length";
        const auto measured = measure_spline_data(lines, measure_opts);
        expect(!measured.splines().empty(), "Measure keeps splines");
        bool has_length = false;
        for (const auto& spline : measured.splines()) {
            if (spline.attributes.contains("length") &&
                spline.attributes["length"].is_number() &&
                spline.attributes["length"].get<double>() > 0.0) {
                has_length = true;
                break;
            }
        }
        expect(has_length, "ConvertLine→Measure writes spline length");
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"grid","type":"CreateGridMesh","data":{"sizeX":2.0,"sizeY":2.0,"rows":1,"cols":1}},
            {"id":"lines","type":"ConvertLine","data":{"connectPath":true,"mode":"unshared"}},
            {"id":"measure","type":"MeasureMesh","data":{"measure":"perimeter","attributeName":"length"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"grid","target":"lines","sourceHandle":"out","targetHandle":"in"},
            {"source":"lines","target":"measure","sourceHandle":"out","targetHandle":"in"},
            {"source":"measure","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.kind == pcg::internal::GraphResultKind::Json ||
                   result.source_geometry != nullptr || !result.json.is_null(),
               "ConvertLine→Measure graph cooks");
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":2.0,"height":2.0,"depth":2.0}},
            {"id":"measure","type":"MeasureMesh","data":{
              "measure":"area",
              "accumulate":"throughout",
              "attributeName":"area",
              "useTotalAttribute":true,
              "totalAttributeName":"totalarea"
            }},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"measure","sourceHandle":"out","targetHandle":"in"},
            {"source":"measure","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "MeasureMesh area throughout keeps geometry");
        if (result.source_geometry) {
            const auto* prim = result.source_geometry->attributes().find(
                pcg::internal::data::AttributeOwner::Primitive, "area");
            expect(prim != nullptr && prim->size() == 6, "box has 6 face areas");
            if (prim && prim->size() >= 2) {
                expect(std::abs(prim->float_values()[0] - prim->float_values()[1]) < 1e-6,
                       "throughout writes same area on all faces");
                expect(prim->float_values()[0] > 20.0, "box surface area ~24");
            }
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
            {"id":"conn","type":"Connectivity","data":{"attributeName":"class"}},
            {"id":"assemble","type":"Assemble","data":{"pieceAttribute":"piece","classAttribute":"class"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"conn","sourceHandle":"out","targetHandle":"in"},
            {"source":"conn","target":"assemble","sourceHandle":"out","targetHandle":"in"},
            {"source":"assemble","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "Connectivity/Assemble keep geometry");
        if (result.source_geometry) {
            const auto* piece = result.source_geometry->attributes().find(
                pcg::internal::data::AttributeOwner::Primitive, "piece");
            expect(piece != nullptr && piece->size() == result.source_geometry->faces().size(),
                   "Assemble writes piece attribute");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
            {"id":"meta","type":"AttributeWrangle","data":{
              "runOver":"detail",
              "expression":"@numFloors = 3;"
            }},
            {"id":"begin","type":"ForEachBegin","data":{
              "method":"count",
              "iterations":1,
              "iterationsAttribute":"numFloors"
            }},
            {"id":"read","type":"AttributeWrangle","data":{
              "runOver":"detail",
              "expression":"@saw = @iteration + 1;"
            }},
            {"id":"xform","type":"TransformMesh","data":{"translateY":1.0}},
            {"id":"end","type":"ForEachEnd","data":{"gatherMethod":"feedback"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"meta","sourceHandle":"out","targetHandle":"in"},
            {"source":"meta","target":"begin","sourceHandle":"out","targetHandle":"in"},
            {"source":"begin","target":"read","sourceHandle":"out","targetHandle":"in"},
            {"source":"read","target":"xform","sourceHandle":"out","targetHandle":"in"},
            {"source":"xform","target":"end","sourceHandle":"out","targetHandle":"in"},
            {"source":"end","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "ForEach metadata+attr iterations cook");
        if (result.source_geometry) {
            double min_y = 1e9;
            double max_y = -1e9;
            for (const auto& p : result.source_geometry->points()) {
                min_y = std::min(min_y, p.y);
                max_y = std::max(max_y, p.y);
            }
            expect(max_y > 2.4, "attr-driven feedback stacked 3 floors");
            const auto* saw = result.source_geometry->attributes().find(
                pcg::internal::data::AttributeOwner::Detail, "saw");
            expect(saw != nullptr && !saw->float_values().empty() &&
                       saw->float_values()[0] >= 3.0,
                   "iteration metadata visible to wrangle");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"a","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
            {"id":"b","type":"CreateBoxMesh","data":{"width":2.0,"height":2.0,"depth":2.0}},
            {"id":"sw","type":"SwitchIf","data":{"condition":true}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"a","target":"sw","sourceHandle":"out","targetHandle":"true"},
            {"source":"b","target":"sw","sourceHandle":"out","targetHandle":"false"},
            {"source":"sw","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "SwitchIf keeps geometry");
        if (result.source_geometry) {
            double max_abs = 0.0;
            for (const auto& p : result.source_geometry->points()) {
                max_abs = std::max(max_abs, std::abs(p.x));
                max_abs = std::max(max_abs, std::abs(p.y));
                max_abs = std::max(max_abs, std::abs(p.z));
            }
            expect(max_abs < 0.6, "SwitchIf selected true input (1x box)");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"grid","type":"CreateGridMesh","data":{"sizeX":4.0,"sizeY":4.0,"rows":1,"cols":1,"plane":"xz"}},
            {"id":"lots","type":"LotSubdivision","data":{"iterations":1,"minSize":0.1,"seed":1}},
            {"id":"meta","type":"AttributeWrangle","data":{
              "runOver":"primitives",
              "expression":"@numFloors = 3;"
            }},
            {"id":"obegin","type":"ForEachBegin","data":{"method":"primitive"}},
            {"id":"ibegin","type":"ForEachBegin","data":{
              "method":"count",
              "iterations":1,
              "iterationsAttribute":"numFloors"
            }},
            {"id":"lift","type":"TransformMesh","data":{"translateY":1.0}},
            {"id":"iend","type":"ForEachEnd","data":{"gatherMethod":"feedback"}},
            {"id":"oend","type":"ForEachEnd","data":{"gatherMethod":"merge"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"grid","target":"lots","sourceHandle":"out","targetHandle":"in"},
            {"source":"lots","target":"meta","sourceHandle":"out","targetHandle":"in"},
            {"source":"meta","target":"obegin","sourceHandle":"out","targetHandle":"in"},
            {"source":"obegin","target":"ibegin","sourceHandle":"out","targetHandle":"in"},
            {"source":"ibegin","target":"lift","sourceHandle":"out","targetHandle":"in"},
            {"source":"lift","target":"iend","sourceHandle":"out","targetHandle":"in"},
            {"source":"iend","target":"oend","sourceHandle":"out","targetHandle":"in"},
            {"source":"oend","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "nested ForEach cooks");
        if (result.source_geometry) {
            double max_y = -1e9;
            for (const auto& p : result.source_geometry->points())
                max_y = std::max(max_y, p.y);
            expect(max_y > 2.4, "nested floor feedback stacks 3 lifts");
        }
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"box","type":"CreateBoxMesh","data":{"width":1.0,"height":1.0,"depth":1.0}},
            {"id":"bound","type":"BoundMesh","data":{"padding":0.1}},
            {"id":"normals","type":"ComputeNormals","data":{"writePointN":true}},
            {"id":"rev","type":"ReverseMesh","data":{}},
            {"id":"smooth","type":"SmoothMesh","data":{"iterations":1,"strength":0.2}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"box","target":"bound","sourceHandle":"out","targetHandle":"in"},
            {"source":"bound","target":"normals","sourceHandle":"out","targetHandle":"in"},
            {"source":"normals","target":"rev","sourceHandle":"out","targetHandle":"in"},
            {"source":"rev","target":"smooth","sourceHandle":"out","targetHandle":"in"},
            {"source":"smooth","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "Bound/Normals/Reverse/Smooth chain");
        if (result.source_geometry) {
            const auto* n = result.source_geometry->attributes().find(
                pcg::internal::data::AttributeOwner::Point, "N");
            expect(n != nullptr && n->schema().tuple_size == 3, "ComputeNormals writes N");
        }
    }

    {
        using pcg::internal::data::AttributeOwner;
        using pcg::internal::data::AttributeType;
        using pcg::internal::data::PcgGeometry;
        using pcg::internal::data::PcgVec3;
        using pcg::internal::elements::SortGeometryOptions;
        using pcg::internal::elements::sort_geometry;

        PcgGeometry geo;
        geo.points_mut() = {
            PcgVec3{2.0, 0.0, 0.0},
            PcgVec3{0.0, 0.0, 0.0},
            PcgVec3{1.0, 0.0, 0.0},
        };
        geo.faces_mut() = {{0, 1, 2}};
        auto& id_attr = geo.attributes().create_int(AttributeOwner::Point, "id", 1);
        id_attr.resize(3);
        id_attr.int_values_mut() = {20, 10, 30};
        geo.groups().add(pcg::internal::geometry::GroupDomain::Point, "sel", 0);
        geo.groups().add(pcg::internal::geometry::GroupDomain::Point, "sel", 2);

        SortGeometryOptions by_x;
        by_x.points.method = "x";
        auto sorted = sort_geometry(geo, by_x);
        expect(sorted.points().size() == 3, "Sort by X keeps point count");
        expect(sorted.points()[0].x == 0.0 && sorted.points()[1].x == 1.0 &&
                   sorted.points()[2].x == 2.0,
               "Sort by X orders positions");
        const auto* sorted_id =
            sorted.attributes().find(AttributeOwner::Point, "id");
        expect(sorted_id != nullptr && sorted_id->int_values().size() == 3 &&
                   sorted_id->int_values()[0] == 10 && sorted_id->int_values()[1] == 30 &&
                   sorted_id->int_values()[2] == 20,
               "Sort by X remaps point attributes");
        expect(sorted.groups().contains(pcg::internal::geometry::GroupDomain::Point, "sel",
                                        1) &&
                   sorted.groups().contains(pcg::internal::geometry::GroupDomain::Point, "sel",
                                            2),
               "Sort by X remaps point groups");

        SortGeometryOptions by_attr;
        by_attr.points.method = "attribute";
        by_attr.points.attribute_name = "id";
        auto by_id = sort_geometry(geo, by_attr);
        expect(by_id.points()[0].x == 0.0 && by_id.points()[1].x == 2.0 &&
                   by_id.points()[2].x == 1.0,
               "Sort by attribute uses int keys");

        SortGeometryOptions prox;
        prox.points.method = "proximity";
        prox.points.proximity_point = {1.0, 0.0, 0.0};
        auto by_prox = sort_geometry(geo, prox);
        expect(by_prox.points()[0].x == 1.0, "Proximity sort puts nearest first");

        SortGeometryOptions along;
        along.points.method = "vector";
        along.points.vector = {1.0, 0.0, 0.0};
        auto by_vec = sort_geometry(geo, along);
        expect(by_vec.points()[0].x == 0.0 && by_vec.points()[2].x == 2.0,
               "Along Vector matches X for (1,0,0)");

        SortGeometryOptions shift;
        shift.points.method = "shift";
        shift.points.offset = 1;
        auto shifted = sort_geometry(geo, shift);
        expect(shifted.points()[0].x == 1.0 && shifted.points()[1].x == 2.0 &&
                   shifted.points()[2].x == 0.0,
               "Shift wraps point numbers forward");

        SortGeometryOptions indices;
        indices.points.method = "x";
        indices.points.sort_indices = true;
        indices.points.ordering_attribute = "sort_index";
        auto ranked = sort_geometry(geo, indices);
        expect(ranked.points()[0].x == 2.0 && ranked.points()[1].x == 0.0,
               "Sort Indices leaves geometry order unchanged");
        const auto* rank = ranked.attributes().find(AttributeOwner::Point, "sort_index");
        expect(rank != nullptr && rank->int_values().size() == 3 &&
                   rank->int_values()[0] == 2 && rank->int_values()[1] == 0 &&
                   rank->int_values()[2] == 1,
               "Sort Indices writes argsort attribute");

        SortGeometryOptions reorder;
        reorder.points.method = "reorder";
        reorder.points.ordering_attribute = "sort_index";
        auto reordered = sort_geometry(ranked, reorder);
        expect(reordered.points()[0].x == 0.0 && reordered.points()[1].x == 1.0 &&
                   reordered.points()[2].x == 2.0,
               "Reorder by Index Attribute applies Sort Indices");

        SortGeometryOptions rev;
        rev.points.method = "reverse";
        auto reversed = sort_geometry(geo, rev);
        expect(reversed.points()[0].x == 1.0 && reversed.points()[2].x == 2.0,
               "Reverse method reverses current order");

        PcgGeometry prims;
        prims.points_mut() = {
            PcgVec3{0.0, 0.0, 0.0},
            PcgVec3{1.0, 0.0, 0.0},
            PcgVec3{0.0, 1.0, 0.0},
            PcgVec3{2.0, 0.0, 0.0},
            PcgVec3{3.0, 0.0, 0.0},
            PcgVec3{2.0, 1.0, 0.0},
        };
        prims.faces_mut() = {{3, 4, 5}, {0, 1, 2}};
        auto& piece = prims.attributes().create_float(AttributeOwner::Primitive, "score", 1);
        piece.resize(2);
        piece.float_values_mut() = {2.0, 1.0};
        SortGeometryOptions prim_sort;
        prim_sort.primitives.method = "attribute";
        prim_sort.primitives.attribute_name = "score";
        auto prim_out = sort_geometry(prims, prim_sort);
        expect(prim_out.faces().size() == 2 && prim_out.faces()[0][0] == 0 &&
                   prim_out.faces()[1][0] == 3,
               "Primitive attribute sort reorders faces");
        const auto* score =
            prim_out.attributes().find(AttributeOwner::Primitive, "score");
        expect(score != nullptr && score->float_values()[0] == 1.0 &&
                   score->float_values()[1] == 2.0,
               "Primitive sort remaps primitive attributes");
    }

    {
        using pcg::internal::data::PcgSpline;
        using pcg::internal::data::PcgSplineData;
        using pcg::internal::data::PcgSplinePoint;
        using pcg::internal::elements::SortGeometryOptions;
        using pcg::internal::elements::sort_spline_data;

        PcgSplineData input;
        auto make_spline = [](double length, double score, const char* label) {
            PcgSpline spline;
            spline.points = {
                PcgSplinePoint{0.0, 0.0, 0.0},
                PcgSplinePoint{length, 0.0, 0.0},
            };
            spline.attributes["length"] = length;
            spline.attributes["score"] = score;
            spline.attributes["label"] = label;
            return spline;
        };
        input.add_spline(make_spline(3.0, 30.0, "c"));
        input.add_spline(make_spline(1.0, 10.0, "a"));
        input.add_spline(make_spline(2.0, 20.0, "b"));
        input.metadata().set("detail_tag", "keep");

        SortGeometryOptions by_length;
        by_length.primitives.method = "attribute";
        by_length.primitives.attribute_name = "length";
        const auto sorted = sort_spline_data(input, by_length);
        expect(sorted.splines().size() == 3, "Spline sort keeps primitive count");
        expect(sorted.splines()[0].attributes["length"].get<double>() == 1.0 &&
                   sorted.splines()[1].attributes["length"].get<double>() == 2.0 &&
                   sorted.splines()[2].attributes["length"].get<double>() == 3.0,
               "Spline attribute sort orders by length");
        expect(sorted.splines()[0].attributes["label"].get<std::string>() == "a",
               "Spline sort preserves primitive attributes");
        expect(sorted.metadata().raw().contains("detail_tag"),
               "Spline sort preserves metadata");

        SortGeometryOptions reverse;
        reverse.primitives.method = "reverse";
        const auto reversed = sort_spline_data(sorted, reverse);
        expect(reversed.splines().front().attributes["length"].get<double>() == 3.0,
               "Spline reverse primitive sort");

        SortGeometryOptions bad_point;
        bad_point.points.method = "x";
        std::string point_error;
        sort_spline_data(input, bad_point, &point_error);
        expect(!point_error.empty(), "Spline point sort other than No Change errors");

        SortGeometryOptions missing_attr;
        missing_attr.primitives.method = "attribute";
        missing_attr.primitives.attribute_name = "missing";
        std::string attr_error;
        sort_spline_data(input, missing_attr, &attr_error);
        expect(!attr_error.empty(), "Spline missing sort attribute errors");
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"grid","type":"CreateGridMesh","data":{"sizeX":4.0,"sizeY":2.0,"rows":1,"cols":2}},
            {"id":"lines","type":"ConvertLine","data":{"connectPath":true,"mode":"unshared"}},
            {"id":"measure","type":"MeasureMesh","data":{"measure":"perimeter","attributeName":"length"}},
            {"id":"sort","type":"SortGeometry","data":{"pointMethod":"nochange","primitiveMethod":"attribute","primitiveAttributeName":"length"}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"grid","target":"lines","sourceHandle":"out","targetHandle":"in"},
            {"source":"lines","target":"measure","sourceHandle":"out","targetHandle":"in"},
            {"source":"measure","target":"sort","sourceHandle":"out","targetHandle":"in"},
            {"source":"sort","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.json.contains("node_stats"), "ConvertLine→Measure→Sort graph reports node_stats");
        if (result.json.contains("node_stats") && result.json["node_stats"].is_array()) {
            bool sort_nonzero = false;
            for (const auto& entry : result.json["node_stats"]) {
                if (entry.value("node_id", std::string()) == "sort") {
                    sort_nonzero = entry.value("point_count", 0) > 0 &&
                                   entry.value("face_count", 0) > 0;
                }
            }
            expect(sort_nonzero, "SortGeometry spline chain keeps non-zero stats");
        }
    }

    {
        using pcg::internal::elements::pin_types_compatible;
        expect(pin_types_compatible("SpatialSpline", "SpatialGeometry"),
               "SpatialSpline connects to SpatialGeometry");
        expect(pin_types_compatible("SpatialMesh", "SpatialGeometry"),
               "SpatialMesh connects to SpatialGeometry");
        expect(!pin_types_compatible("SpatialPoint", "SpatialGeometry"),
               "SpatialPoint cannot connect to SpatialGeometry");
        expect(!pin_types_compatible("HeightField", "SpatialGeometry"),
               "HeightField cannot connect to SpatialGeometry");
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"grid","type":"CreateGridMesh","data":{"sizeX":2.0,"sizeY":1.0,"rows":1,"cols":2}},
            {"id":"sort","type":"SortGeometry","data":{"pointMethod":"x","reversePoints":true}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"grid","target":"sort","sourceHandle":"out","targetHandle":"in"},
            {"source":"sort","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "SortGeometry graph cooks");
        if (result.source_geometry && result.source_geometry->points().size() >= 2) {
            expect(result.source_geometry->points().front().x >=
                       result.source_geometry->points().back().x,
                   "SortGeometry reverse By X decreases X");
        }
    }

    {
        using pcg::internal::data::PcgSpline;
        using pcg::internal::data::PcgSplineData;
        using pcg::internal::data::PcgSplinePoint;
        using pcg::internal::elements::CarveSplineOptions;
        using pcg::internal::elements::carve_spline_data;

        PcgSplineData input;
        PcgSpline spline;
        spline.points = {
            PcgSplinePoint{0.0, 0.0, 0.0},
            PcgSplinePoint{10.0, 0.0, 0.0},
            PcgSplinePoint{30.0, 0.0, 0.0},
            PcgSplinePoint{40.0, 0.0, 0.0},
        };
        input.add_spline(spline);

        CarveSplineOptions breakpoints;
        breakpoints.u_start = 0.0;
        breakpoints.u_end = 1.0;
        breakpoints.location = "breakpoints";
        breakpoints.cut_at_all_internal_u_breakpoints = true;
        breakpoints.keep_inside = true;
        breakpoints.keep_outside = false;
        const auto split = carve_spline_data(input, breakpoints);
        expect(split.splines().size() == 3, "Carve breakpoints splits internal edges");
        if (split.splines().size() == 3) {
            expect(split.splines()[0].points.size() == 2 &&
                       split.splines()[0].points[0].x == 0.0 &&
                       split.splines()[0].points[1].x == 10.0,
                   "Carve first edge spans 0-10");
            expect(split.splines()[1].points.size() == 2 &&
                       split.splines()[1].points[0].x == 10.0 &&
                       split.splines()[1].points[1].x == 30.0,
                   "Carve middle edge spans 10-30");
            expect(split.splines()[2].points.size() == 2 &&
                       split.splines()[2].points[0].x == 30.0 &&
                       split.splines()[2].points[1].x == 40.0,
                   "Carve last edge spans 30-40");
        }

        CarveSplineOptions slice;
        slice.u_start = 0.25;
        slice.u_end = 0.75;
        slice.location = "breakpoints";
        slice.cut_at_all_internal_u_breakpoints = false;
        slice.keep_inside = true;
        const auto mid = carve_spline_data(input, slice);
        expect(mid.splines().size() == 1, "Carve slice without breakpoints yields one spline");
        if (!mid.splines().empty()) {
            expect(mid.splines()[0].points.size() == 2 &&
                       mid.splines()[0].points[0].x == 10.0 &&
                       mid.splines()[0].points[1].x == 30.0,
                   "Carve arc-length U=0.25..0.75 trims to 10-30");
        }

        CarveSplineOptions outside;
        outside.u_start = 0.25;
        outside.u_end = 0.75;
        outside.location = "breakpoints";
        outside.cut_at_all_internal_u_breakpoints = false;
        outside.keep_inside = false;
        outside.keep_outside = true;
        const auto outer = carve_spline_data(input, outside);
        expect(outer.splines().size() == 2, "Carve keep outside yields two edge segments");

        CarveSplineOptions dice;
        dice.location = "divisions";
        dice.u_divisions = 2;
        dice.cut_at_all_internal_u_breakpoints = false;
        const auto diced = carve_spline_data(input, dice);
        expect(diced.splines().size() == 2, "Carve divisions=2 splits at midpoint");
        if (diced.splines().size() == 2) {
            expect(diced.splines()[0].points.back().x == 20.0 &&
                       diced.splines()[1].points.front().x == 20.0,
                   "Carve divisions=2 cut lands at arc-length midpoint 20");
        }

        using pcg::internal::elements::carve_spline_extract_points;

        CarveSplineOptions extract;
        extract.operation = "extract";
        extract.u_start = 0.25;
        extract.u_end = 0.75;
        extract.location = "breakpoints";
        extract.cut_at_all_internal_u_breakpoints = false;
        const auto extracted = carve_spline_extract_points(input, extract);
        expect(extracted.points().size() == 2, "Carve extract yields points at First/Second U");
        if (extracted.points().size() == 2) {
            expect(extracted.points()[0].x == 10.0 && extracted.points()[1].x == 30.0,
                   "Carve extract points sit at arc-length U=0.25/0.75");
        }

        CarveSplineOptions extract_keep = extract;
        extract_keep.keep_original = true;
        const auto extracted_keep = carve_spline_extract_points(input, extract_keep);
        expect(extracted_keep.points().size() == 6,
               "Carve extract Keep Original appends source vertices");

        CarveSplineOptions extract_bp = extract;
        extract_bp.u_start = 0.5;
        extract_bp.u_end = 1.0;
        extract_bp.cut_at_all_internal_u_breakpoints = true;
        extract_bp.only_at_breakpoints = true;
        const auto extracted_bp = carve_spline_extract_points(input, extract_bp);
        expect(extracted_bp.points().size() == 2 && extracted_bp.points()[0].x == 30.0 &&
                   extracted_bp.points()[1].x == 40.0,
               "Carve extract Only At Breakpoints drops non-vertex U");

        PcgSplineData grouped_input;
        PcgSpline in_group = spline;
        in_group.attributes = nlohmann::json::object({{"roads", true}});
        PcgSpline out_group = spline;
        grouped_input.add_spline(in_group);
        grouped_input.add_spline(out_group);

        CarveSplineOptions grouped;
        grouped.group = "roads";
        grouped.u_start = 0.25;
        grouped.u_end = 0.75;
        grouped.location = "breakpoints";
        grouped.cut_at_all_internal_u_breakpoints = false;
        const auto grouped_out = carve_spline_data(grouped_input, grouped);
        expect(grouped_out.splines().size() == 2,
               "Carve group trims member and passes non-member through");
        if (grouped_out.splines().size() == 2) {
            expect(grouped_out.splines()[0].points.size() == 2 &&
                       grouped_out.splines()[0].points[0].x == 10.0,
                   "Carve group member is trimmed");
            expect(grouped_out.splines()[1].points.size() == 4,
                   "Carve non-member spline passes through untouched");
        }

        CarveSplineOptions attrib;
        attrib.u_start = 0.5;
        attrib.u_end = 1.0;
        attrib.u_start_attrib = "carve_start";
        attrib.location = "breakpoints";
        attrib.cut_at_all_internal_u_breakpoints = false;
        PcgSplineData attrib_input;
        PcgSpline attrib_spline = spline;
        attrib_spline.attributes = nlohmann::json::object({{"carve_start", 0.5}});
        attrib_input.add_spline(attrib_spline);
        const auto attrib_out = carve_spline_data(attrib_input, attrib);
        expect(attrib_out.splines().size() == 1 &&
                   attrib_out.splines()[0].points.front().x == 10.0,
               "Carve First U Attrib scales start to U=0.25");
    }

    {
        using pcg::internal::elements::ResampleOptions;
        using pcg::internal::geometry::PolylineResampleOptions;
        using pcg::internal::geometry::resample_polyline_houdini;

        std::vector<pcg::internal::geometry::Vec3> line{{0.0, 0.0, 0.0},
                                                          {40.0, 0.0, 0.0}};
        PolylineResampleOptions opts;
        opts.use_max_segments = true;
        opts.max_segments = 2;
        opts.even_last_segment_same_length = true;
        const auto result = resample_polyline_houdini(line, opts);
        expect(result.points.size() == 3, "Resample max segments=2 yields 3 points");
        if (result.points.size() == 3) {
            expect(std::abs(result.points[1].x - 20.0) < 1e-6,
                   "Resample middle point at half length");
        }

        pcg::internal::data::PcgGeometry curve;
        curve.points_mut() = {{0.0, 0.0, 0.0}, {10.0, 0.0, 0.0}, {30.0, 0.0, 0.0}};
        curve.faces_mut().push_back({0, 1, 2});
        auto& open_attr =
            curve.attributes().create_int(pcg::internal::data::AttributeOwner::Primitive,
                                          "closed", 1);
        open_attr.resize(1);
        open_attr.int_values_mut()[0] = 0;
        ResampleOptions geom_opts;
        geom_opts.use_max_segments = true;
        geom_opts.max_segments = 2;
        const auto geometry = pcg::internal::elements::resample_geometry(curve, geom_opts);
        expect(geometry.faces().size() == 1, "Resample geometry keeps one curve");
        expect(geometry.faces()[0].size() == 3, "Resample geometry curve has 3 points");
    }

    {
        using pcg::internal::data::AttributeOwner;
        using pcg::internal::data::PcgGeometry;
        using pcg::internal::elements::ResampleOptions;
        using pcg::internal::elements::resample_geometry;

        // Mesh quad (no duplicated first point) must resample the closing edge too.
        PcgGeometry quad;
        quad.points_mut() = {
            {0.0, 0.0, 0.0},
            {4.0, 0.0, 0.0},
            {4.0, 0.0, 2.0},
            {0.0, 0.0, 2.0},
        };
        quad.faces_mut().push_back({0, 1, 2, 3});

        ResampleOptions by_length;
        by_length.use_max_segments = false;
        by_length.use_max_segment_length = true;
        by_length.max_segment_length = 1.0;
        by_length.even_last_segment_same_length = true;
        by_length.resample_by_polygon_edge = false;
        const auto whole = resample_geometry(quad, by_length);
        expect(whole.faces().size() == 1, "Closed quad resample keeps one face");
        expect(whole.faces()[0].size() >= 8,
               "Closed quad whole-perimeter includes all four edges");

        by_length.resample_by_polygon_edge = true;
        const auto by_edge = resample_geometry(quad, by_length);
        expect(by_edge.faces().size() == 1, "By-edge closed quad keeps one face");
        // Each side length 4 or 2 with max 1.0 → at least 4+2+4+2 = 12 segments → 12 verts
        // (corners shared). Expect no bare side: vertex count matches full ring.
        expect(by_edge.faces()[0].size() >= 12,
               "Resample by polygon edge covers all four sides");

        // Count points near the bottom edge (y=0,z=0,x in (0,4)) — must have intermediates.
        int bottom_mids = 0;
        int top_mids = 0;
        for (int pi : by_edge.faces()[0]) {
            const auto& p = by_edge.points()[static_cast<size_t>(pi)];
            if (std::abs(p.z) < 1e-6 && std::abs(p.y) < 1e-6 && p.x > 0.1 && p.x < 3.9)
                ++bottom_mids;
            if (std::abs(p.z - 2.0) < 1e-6 && std::abs(p.y) < 1e-6 && p.x > 0.1 && p.x < 3.9)
                ++top_mids;
        }
        expect(bottom_mids >= 3 && top_mids >= 3,
               "Opposite equal-length edges both get intermediate points");
    }

    {
        const std::string graph = R"({
          "version":"1.0",
          "nodes":[
            {"id":"line","type":"CreateSpline","data":{"startX":0,"startY":0,"startZ":0,"endX":40,"endY":0,"endZ":0}},
            {"id":"resample","type":"Resample","data":{"useMaxSegments":true,"maxSegments":2}},
            {"id":"out","type":"Output","data":{}}
          ],
          "edges":[
            {"source":"line","target":"resample","sourceHandle":"out","targetHandle":"in"},
            {"source":"resample","target":"out","sourceHandle":"out","targetHandle":"in"}
          ]
        })";
        const auto result = execute(graph);
        expect(result.source_geometry != nullptr, "Resample graph outputs geometry");
        if (result.source_geometry)
            expect(result.source_geometry->faces().size() >= 1, "Resample graph has curve face");
    }

    std::printf("failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
