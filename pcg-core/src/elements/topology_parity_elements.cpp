#include "elements/topology_parity_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/expression.hpp"
#include "elements/facade_foundation_algorithms.hpp"
#include "elements/topology_parity_algorithms.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

void load_detail_params(const data::PcgGeometry& geometry,
                        std::unordered_map<std::string, double>& parameters)
{
    auto try_add = [&](data::AttributeOwner owner, const std::string& name) {
        if (parameters.count(name) > 0)
            return;
        const data::AttributeArray* attr = geometry.attributes().find(owner, name);
        if (!attr || attr->size() == 0)
            return;
        if (attr->schema().type == data::AttributeType::Int)
            parameters[name] = static_cast<double>(attr->int_values()[0]);
        else if (attr->schema().type == data::AttributeType::Float)
            parameters[name] = attr->float_values()[0];
    };

    for (const auto& name : geometry.attributes().names(data::AttributeOwner::Detail))
        try_add(data::AttributeOwner::Detail, name);
    // Also expose first primitive attrs (per-lot ForEach pieces).
    for (const auto& name : geometry.attributes().names(data::AttributeOwner::Primitive))
        try_add(data::AttributeOwner::Primitive, name);
}

class MeasureMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MeasureMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        MeasureMeshOptions options;
        options.group = ctx.node->data.value("group", std::string());
        options.element_type = ctx.node->data.value("elementType", std::string("primitives"));
        options.measure = ctx.node->data.value("measure", std::string("perimeter"));
        options.accumulate = ctx.node->data.value("accumulate", std::string("perElement"));
        options.piece_attribute = ctx.node->data.value("pieceAttribute", std::string());
        if (options.piece_attribute.empty())
            options.piece_attribute = ctx.node->data.value("classAttribute", std::string());
        // Legacy: non-empty pieceAttribute without accumulate → perPiece.
        if (!options.piece_attribute.empty() &&
            !ctx.node->data.contains("accumulate"))
            options.accumulate = "perPiece";
        options.refine_to_connected = ctx.node->data.value("refineToConnected", true);

        options.use_position_attribute = ctx.node->data.value("usePositionAttribute", false);
        options.position_attribute =
            ctx.node->data.value("positionAttribute", std::string("P"));

        options.use_minimum = ctx.node->data.value("useMinimum", false);
        options.minimum = ctx.node->data.value("minimum", -1.0);
        options.use_maximum = ctx.node->data.value("useMaximum", false);
        options.maximum = ctx.node->data.value("maximum", 1.0);
        options.use_width = ctx.node->data.value("useWidth", true);
        options.width = ctx.node->data.value("width", 6.0);
        options.width_scale = ctx.node->data.value("widthScale", std::string("mad"));
        options.center_type = ctx.node->data.value("centerType", std::string("median"));
        options.center_fixed = ctx.node->data.value("centerFixed", 0.0);

        options.attribute_name = ctx.node->data.value("attributeName", std::string("length"));
        options.use_total_attribute = ctx.node->data.value("useTotalAttribute", false);
        options.total_attribute_name =
            ctx.node->data.value("totalAttributeName", std::string("totalperimeter"));
        options.use_range_group = ctx.node->data.value("useRangeGroup", false);
        options.range_group = ctx.node->data.value("rangeGroup", std::string("inrange"));
        options.bake_visualized_range = ctx.node->data.value("bakeVisualizedRange", false);
        options.use_remap_range = ctx.node->data.value("useRemapRange", false);
        options.remap_min = ctx.node->data.value("remapMin", 0.0);
        options.remap_max = ctx.node->data.value("remapMax", 1.0);

        // ConvertLine / CreateSpline → SpatialSpline; mesh nodes → SpatialMesh.
        if (ctx.inputs.find_splines("in") != nullptr ||
            (ctx.inputs.find("in") && ctx.inputs.find("in")->splines)) {
            const auto input =
                get_splines_input(ctx, "in", "MeasureMesh missing spline/mesh input");
            emit_splines(ctx, measure_spline_data(input, options));
            return PCG_OK;
        }

        const auto input = get_geometry_input(ctx, "in", "MeasureMesh missing mesh input");
        emit_geometry(ctx, measure_mesh_geometry(input, options));
        return PCG_OK;
    }
};

class BoundMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "BoundMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "BoundMesh missing mesh input");
        BoundMeshOptions options;
        options.oriented = ctx.node->data.value("oriented", false);
        options.padding = ctx.node->data.value("padding", 0.0);
        emit_geometry(ctx, bound_mesh_geometry(input, options));
        return PCG_OK;
    }
};

class ComputeNormalsElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ComputeNormals"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "ComputeNormals missing mesh input");
        ComputeNormalsOptions options;
        options.shade_mode = ctx.node->data.value("shadeMode", std::string("auto"));
        options.cusp_angle_deg = ctx.node->data.value("cuspAngle", 30.0);
        options.write_point_n = ctx.node->data.value("writePointN", true);
        emit_geometry(ctx, compute_normals_geometry(input, options));
        return PCG_OK;
    }
};

class SmoothMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SmoothMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "SmoothMesh missing mesh input");
        SmoothMeshOptions options;
        options.iterations = ctx.node->data.value("iterations", 5);
        options.strength = ctx.node->data.value("strength", 0.5);
        options.fix_boundary = ctx.node->data.value("fixBoundary", true);
        emit_geometry(ctx, smooth_mesh_geometry(input, options));
        return PCG_OK;
    }
};

class ReverseMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ReverseMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "ReverseMesh missing mesh input");
        emit_geometry(ctx, reverse_mesh_geometry(input));
        return PCG_OK;
    }
};

class ThickenMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ThickenMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "ThickenMesh missing mesh input");
        ThickenMeshOptions options;
        options.depth = ctx.node->data.value("depth", 0.2);
        options.direction = ctx.node->data.value("direction", std::string("both"));
        options.dissolve_middle_edge = ctx.node->data.value("dissolveMiddleEdge", true);
        emit_geometry(ctx, thicken_mesh_geometry(input, options));
        return PCG_OK;
    }
};

class PolySliceElement final : public IPcgElement {
public:
    const char* type_name() const override { return "PolySlice"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "PolySlice missing mesh input");
        PolySliceOptions options;
        options.origin_x = ctx.node->data.value("originX", 0.0);
        options.origin_y = ctx.node->data.value("originY", 0.0);
        options.origin_z = ctx.node->data.value("originZ", 0.0);
        options.normal_x = ctx.node->data.value("normalX", 0.0);
        options.normal_y = ctx.node->data.value("normalY", 1.0);
        options.normal_z = ctx.node->data.value("normalZ", 0.0);
        emit_geometry(ctx, poly_slice_geometry(input, options));
        return PCG_OK;
    }
};

class PolyWireElement final : public IPcgElement {
public:
    const char* type_name() const override { return "PolyWire"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto splines = get_splines_input(ctx, "in", "PolyWire missing spline input");
        if (splines.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "PolyWire missing spline input");
        PolyWireOptions options;
        options.radius = ctx.node->data.value("radius", 0.05);
        options.columns = ctx.node->data.value("columns", 8);
        options.cap_start = ctx.node->data.value("capStart", true);
        options.cap_end = ctx.node->data.value("capEnd", true);
        emit_geometry(ctx, poly_wire_geometry(splines, options));
        return PCG_OK;
    }
};

class ConnectivityElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Connectivity"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "Connectivity missing mesh input");
        ConnectivityOptions options;
        options.attribute_name = ctx.node->data.value("attributeName", std::string("class"));
        options.connectivity = ctx.node->data.value("connectivity", std::string("face"));
        emit_geometry(ctx, connectivity_geometry(input, options));
        return PCG_OK;
    }
};

class AssembleElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Assemble"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "Assemble missing mesh input");
        AssembleOptions options;
        options.piece_attribute = ctx.node->data.value("pieceAttribute", std::string("piece"));
        options.class_attribute = ctx.node->data.value("classAttribute", std::string("class"));
        options.create_if_missing = ctx.node->data.value("createIfMissing", true);
        emit_geometry(ctx, assemble_geometry(input, options));
        return PCG_OK;
    }
};

class SortGeometryElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SortGeometry"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto& data = ctx.node->data;

        auto read_domain = [&](const char* prefix) {
            SortDomainOptions domain;
            const std::string method_key = std::string(prefix) + "Method";
            const std::string group_key = std::string(prefix) + "Group";
            const std::string seed_key = std::string(prefix) + "Seed";
            const std::string offset_key = std::string(prefix) + "Offset";
            const std::string px = std::string(prefix) + "ProximityX";
            const std::string py = std::string(prefix) + "ProximityY";
            const std::string pz = std::string(prefix) + "ProximityZ";
            const std::string vx = std::string(prefix) + "VectorX";
            const std::string vy = std::string(prefix) + "VectorY";
            const std::string vz = std::string(prefix) + "VectorZ";
            const std::string attr_key = std::string(prefix) + "AttributeName";
            const std::string comp_key = std::string(prefix) + "Component";
            const std::string order_key = std::string(prefix) + "OrderingAttribute";
            const std::string reverse_key =
                std::string(prefix) == "point" ? "reversePoints" : "reversePrimitives";
            const std::string indices_key = std::string(prefix) + "SortIndices";
            const std::string combine_key = std::string(prefix) + "CombineSortIndices";

            domain.method = data.value(method_key, std::string("nochange"));
            domain.group = data.value(group_key, std::string());
            domain.seed = data.value(seed_key, ctx.graph_seed);
            domain.offset = data.value(offset_key, 1);
            domain.proximity_point.x = data.value(px, 0.0);
            domain.proximity_point.y = data.value(py, 0.0);
            domain.proximity_point.z = data.value(pz, 0.0);
            domain.vector.x = data.value(vx, 0.0);
            domain.vector.y = data.value(vy, 1.0);
            domain.vector.z = data.value(vz, 0.0);
            domain.attribute_name = data.value(attr_key, std::string());
            domain.component = data.value(comp_key, 0);
            domain.ordering_attribute = data.value(order_key, std::string("sort_index"));
            domain.reverse = data.value(reverse_key, false);
            domain.sort_indices = data.value(indices_key, false);
            domain.combine_sort_indices = data.value(combine_key, true);
            return domain;
        };

        SortGeometryOptions options;
        const bool has_dual = data.contains("pointMethod") || data.contains("primitiveMethod");
        if (has_dual) {
            options.points = read_domain("point");
            options.primitives = read_domain("primitive");
        } else {
            // Legacy single-domain graphs (domain + method + ...).
            SortDomainOptions legacy;
            legacy.method = data.value("method", std::string("nochange"));
            legacy.axis = data.value("axis", std::string("x"));
            legacy.group = data.value("group", std::string());
            legacy.seed = data.value("seed", ctx.graph_seed);
            legacy.offset = data.value("offset", 1);
            legacy.proximity_point.x = data.value("proximityX", 0.0);
            legacy.proximity_point.y = data.value("proximityY", 0.0);
            legacy.proximity_point.z = data.value("proximityZ", 0.0);
            legacy.vector.x = data.value("vectorX", 0.0);
            legacy.vector.y = data.value("vectorY", 1.0);
            legacy.vector.z = data.value("vectorZ", 0.0);
            legacy.attribute_name = data.value("attributeName", std::string());
            legacy.component = data.value("component", 0);
            legacy.ordering_attribute =
                data.value("orderingAttribute", std::string("sort_index"));
            legacy.reverse = data.value("reverse", false);
            legacy.sort_indices = data.value("sortIndices", false);
            legacy.combine_sort_indices = data.value("combineSortIndices", true);
            if (data.value("domain", std::string("points")) == "primitives")
                options.primitives = std::move(legacy);
            else
                options.points = std::move(legacy);
        }
        options.optimize_vertex_order = data.value("optimizeVertexOrder", true);

        if (ctx.inputs.find_heightfield("in") != nullptr)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "SortGeometry does not support HeightField input");

        if (ctx.inputs.find_points("in") != nullptr ||
            (ctx.inputs.find("in") && ctx.inputs.find("in")->points)) {
            const auto input =
                get_points_input(ctx, "in", "SortGeometry missing points input");
            std::string error;
            auto sorted = sort_point_data(input, options.points, &error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
            emit_points(ctx, std::move(sorted));
            return PCG_OK;
        }

        if (ctx.inputs.find_splines("in") != nullptr ||
            (ctx.inputs.find("in") && ctx.inputs.find("in")->splines)) {
            const auto input =
                get_splines_input(ctx, "in", "SortGeometry missing geometry input");
            std::string error;
            auto sorted = sort_spline_data(input, options, &error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
            emit_splines(ctx, std::move(sorted));
            return PCG_OK;
        }

        if (ctx.inputs.find_geometry("in") != nullptr || ctx.inputs.find_mesh("in") != nullptr) {
            const auto input = get_geometry_input(ctx, "in", "SortGeometry missing geometry input");
            std::string error;
            auto sorted = sort_geometry(input, options, &error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
            emit_geometry(ctx, std::move(sorted));
            return PCG_OK;
        }

        return fail_ctx(ctx, PCG_ERR_EXECUTION, "SortGeometry missing geometry input");
    }
};

ResampleOptions load_resample_options(const PcgContext& ctx)
{
    ResampleOptions options;
    if (!ctx.node)
        return options;
    const auto& data = ctx.node->data;
    options.group = data.value("group", std::string());
    options.maintain_primitive_order = data.value("maintainPrimitiveOrder", false);
    options.level_of_detail = data.value("levelOfDetail", 1);
    options.resample_by_polygon_edge = data.value("resampleByPolygonEdge", false);
    options.method = data.value("method", std::string("evenLength"));
    options.measure = data.value("measure", std::string("arc"));
    options.use_max_segment_length = data.value("useMaxSegmentLength", false);
    options.max_segment_length = data.value("maxSegmentLength", 0.1);
    options.use_max_segments = data.value("useMaxSegments", true);
    options.max_segments = data.value("maxSegments", 2);
    options.allow_attribute_override = data.value("allowAttributeOverride", true);
    options.even_last_segment_same_length = data.value("evenLastSegmentSameLength", true);
    options.maintain_last_vertex = data.value("maintainLastVertex", false);
    options.randomize_first_segment_length = data.value("randomizeFirstSegmentLength", false);
    options.create_only_points = data.value("createOnlyPoints", false);
    options.treat_polygons_as = data.value("treatPolygonsAs", std::string("straight"));
    options.output_as_subdivision_curves = data.value("outputAsSubdivisionCurves", false);
    options.write_distance_attr = data.value("writeDistanceAttr", false);
    options.distance_attribute = data.value("distanceAttribute", std::string("ptdist"));
    options.write_tangent_attr = data.value("writeTangentAttr", false);
    options.tangent_attribute = data.value("tangentAttribute", std::string("tangentu"));
    options.write_curve_u_attr = data.value("writeCurveUAttr", false);
    options.curve_u_attribute = data.value("curveUAttribute", std::string("curveu"));
    options.write_curve_num_attr = data.value("writeCurveNumAttr", false);
    options.curve_num_attribute = data.value("curveNumAttribute", std::string("curvenum"));
    options.graph_seed = ctx.graph_seed;
    return options;
}

data::PcgGeometry splines_to_curve_geometry(const data::PcgSplineData& splines)
{
    data::PcgGeometry geometry;
    std::vector<int64_t> closed_flags;
    closed_flags.reserve(splines.splines().size());
    for (const auto& spline : splines.splines()) {
        if (spline.points.size() < 2)
            continue;
        std::vector<int> face;
        face.reserve(spline.points.size() + 1);
        for (const auto& p : spline.points) {
            const int index = static_cast<int>(geometry.points().size());
            geometry.points_mut().push_back({p.x, p.y, p.z});
            face.push_back(index);
        }
        if (spline.closed && !face.empty())
            face.push_back(face.front());
        closed_flags.push_back(spline.closed ? 1 : 0);
        geometry.faces_mut().push_back(std::move(face));
    }
    if (!closed_flags.empty()) {
        auto& closed_attr =
            geometry.attributes().create_int(data::AttributeOwner::Primitive, "closed", 1);
        closed_attr.resize(closed_flags.size());
        closed_attr.int_values_mut() = std::move(closed_flags);
    }
    return geometry;
}

class ResampleElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Resample"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        ResampleOptions options = load_resample_options(ctx);
        if (const data::PcgGeometry* geometry = ctx.inputs.find_geometry("in")) {
            emit_geometry(ctx, resample_geometry(*geometry, options));
            return PCG_OK;
        }
        if (const data::PcgSplineData* splines = ctx.inputs.find_splines("in")) {
            if (splines->splines().empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, "Resample missing curve input");
            emit_geometry(ctx, resample_geometry(splines_to_curve_geometry(*splines), options));
            return PCG_OK;
        }
        if (ctx.inputs.find_mesh("in") != nullptr) {
            const auto geometry = get_geometry_input(ctx, "in", "Resample missing geometry input");
            emit_geometry(ctx, resample_geometry(geometry, options));
            return PCG_OK;
        }
        return fail_ctx(ctx, PCG_ERR_EXECUTION, "Resample missing geometry input");
    }
};

class CarveElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Carve"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const data::PcgTaggedData* source = ctx.inputs.find("in");
        if (!source)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Carve missing input");

        data::PcgSplineData input;
        if (source->splines) {
            input = *source->splines;
        } else if (source->geometry) {
            input = convert_geometry_primitives_to_splines(*source->geometry);
        } else if (source->mesh) {
            input = convert_geometry_primitives_to_splines(
                data::geometry_from_mesh(*source->mesh));
        } else {
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "Carve supports Spline, Geometry, or Mesh input");
        }
        CarveSplineOptions options;
        options.group = ctx.node->data.value("group", std::string());
        options.u_start = ctx.node->data.value("uStart", 0.0);
        options.u_end = ctx.node->data.value("uEnd", 1.0);
        options.use_first_u = ctx.node->data.value("useFirstU", true);
        options.use_second_u = ctx.node->data.value("useSecondU", true);
        options.u_start_attrib = ctx.node->data.value("uStartAttrib", std::string());
        options.u_end_attrib = ctx.node->data.value("uEndAttrib", std::string());
        options.arc_length_u = ctx.node->data.value("arcLengthU", true);
        options.v_start = ctx.node->data.value("vStart", 0.25);
        options.v_end = ctx.node->data.value("vEnd", 0.75);
        options.use_first_v = ctx.node->data.value("useFirstV", true);
        options.use_second_v = ctx.node->data.value("useSecondV", true);
        options.v_start_attrib = ctx.node->data.value("vStartAttrib", std::string());
        options.v_end_attrib = ctx.node->data.value("vEndAttrib", std::string());
        options.location = ctx.node->data.value("location", std::string("divisions"));
        options.u_divisions = ctx.node->data.value("uDivisions", 2);
        options.v_divisions = ctx.node->data.value("vDivisions", 2);
        options.cut_at_all_internal_u_breakpoints =
            ctx.node->data.value("cutAtAllInternalUBreakpoints", true);
        options.cut_at_all_internal_v_breakpoints =
            ctx.node->data.value("cutAtAllInternalVBreakpoints", true);
        options.operation = ctx.node->data.value("operation", std::string("cut"));
        options.extract_type = ctx.node->data.value("extractType", std::string("curves3d"));
        options.keep_original = ctx.node->data.value("keepOriginal", false);
        options.only_at_breakpoints = ctx.node->data.value("onlyAtBreakpoints", false);
        options.keep_inside = ctx.node->data.value("keepInside", true);
        options.keep_outside = ctx.node->data.value("keepOutside", false);
        if (options.operation == "extract") {
            emit_points(ctx, carve_spline_extract_points(input, options));
            return PCG_OK;
        }
        emit_splines(ctx, carve_spline_data(input, options));
        return PCG_OK;
    }
};

class FindShortestPathElement final : public IPcgElement {
public:
    const char* type_name() const override { return "FindShortestPath"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "FindShortestPath missing mesh input");
        FindShortestPathOptions options;
        options.start_point = ctx.node->data.value("startPoint", 0);
        options.end_point = ctx.node->data.value("endPoint", 1);
        options.start_group = ctx.node->data.value("startGroup", std::string());
        options.end_group = ctx.node->data.value("endGroup", std::string());
        auto path = find_shortest_path_on_mesh(input, options);
        if (path.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "FindShortestPath found no path");
        emit_splines(ctx, std::move(path));
        return PCG_OK;
    }
};

class TreeSimpleLeafElement final : public IPcgElement {
public:
    const char* type_name() const override { return "TreeSimpleLeaf"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto points = get_points_input(ctx, "in", "TreeSimpleLeaf missing points input");
        TreeSimpleLeafOptions options;
        options.leaf_width = ctx.node->data.value("leafWidth", 0.08);
        options.leaf_height = ctx.node->data.value("leafHeight", 0.12);
        options.leaf_thickness = ctx.node->data.value("leafThickness", 0.005);
        options.seed = ctx.node->data.value("seed", ctx.graph_seed);
        options.scale_min = ctx.node->data.value("scaleMin", 0.7);
        options.scale_max = ctx.node->data.value("scaleMax", 1.3);
        emit_geometry(ctx, tree_simple_leaf_geometry(points, options));
        return PCG_OK;
    }
};

class SwitchIfElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SwitchIf"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SwitchIf missing node");

        bool condition = ctx.node->data.value("condition", false);
        const std::string expression =
            ctx.node->data.value("conditionExpression", std::string());

        const data::PcgTaggedData* probe = ctx.inputs.find("true");
        if (!probe)
            probe = ctx.inputs.find("false");
        if (!probe)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SwitchIf requires true or false input");

        if (!expression.empty()) {
            expression::Program program;
            std::string error;
            if (!expression::Program::compile_expression(expression, program, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("SwitchIf conditionExpression parse error: " + error).c_str());
            expression::EvalContext eval;
            eval.parameters["condition"] = condition ? 1.0 : 0.0;
            // Load from both sides (false first). Branch outputs often lose upstream
            // prim attrs after Boolean/Extrude; the unprocessed side still carries them.
            if (const data::PcgTaggedData* side = ctx.inputs.find("false");
                side && side->geometry)
                load_detail_params(*side->geometry, eval.parameters);
            if (const data::PcgTaggedData* side = ctx.inputs.find("true");
                side && side->geometry)
                load_detail_params(*side->geometry, eval.parameters);
            eval.read_variable = [&eval](const std::string& name, double& value) {
                // AttributeWrangle-style tokens keep the '@'; detail/prim params do not.
                std::string key = name;
                if (!key.empty() && key.front() == '@')
                    key = key.substr(1);
                const auto it = eval.parameters.find(key);
                if (it == eval.parameters.end()) {
                    // Missing lot flags after topology ops → treat as 0 (false branch).
                    value = 0.0;
                    return true;
                }
                value = it->second;
                return true;
            };
            double value = 0.0;
            if (!program.evaluate(eval, value, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("SwitchIf conditionExpression error: " + error).c_str());
            condition = value != 0.0;
        }

        const char* pin = condition ? "true" : "false";
        const data::PcgTaggedData* selected = ctx.inputs.find(pin);
        if (!selected)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SwitchIf selected input is not connected");

        if (selected->geometry) {
            ctx.outputs.add_geometry_shared("out", selected->geometry);
        } else if (selected->mesh) {
            ctx.outputs.add_mesh_shared("out", selected->mesh);
        } else if (selected->points) {
            ctx.outputs.add_points_shared_with_meta("out", selected->points, selected->payload);
        } else if (const data::PcgSplineData* splines = ctx.inputs.find_splines(pin)) {
            ctx.outputs.add_splines("out", *splines);
        } else {
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SwitchIf selected input type unsupported");
        }
        return PCG_OK;
    }
};

} // namespace

void register_topology_parity_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("MeasureMesh", std::make_unique<MeasureMeshElement>());
    map.emplace("BoundMesh", std::make_unique<BoundMeshElement>());
    map.emplace("ComputeNormals", std::make_unique<ComputeNormalsElement>());
    map.emplace("SmoothMesh", std::make_unique<SmoothMeshElement>());
    map.emplace("ReverseMesh", std::make_unique<ReverseMeshElement>());
    map.emplace("ThickenMesh", std::make_unique<ThickenMeshElement>());
    map.emplace("PolySlice", std::make_unique<PolySliceElement>());
    map.emplace("PolyWire", std::make_unique<PolyWireElement>());
    map.emplace("Connectivity", std::make_unique<ConnectivityElement>());
    map.emplace("Assemble", std::make_unique<AssembleElement>());
    map.emplace("SortGeometry", std::make_unique<SortGeometryElement>());
    map.emplace("Resample", std::make_unique<ResampleElement>());
    map.emplace("Carve", std::make_unique<CarveElement>());
    map.emplace("FindShortestPath", std::make_unique<FindShortestPathElement>());
    map.emplace("TreeSimpleLeaf", std::make_unique<TreeSimpleLeafElement>());
    map.emplace("SwitchIf", std::make_unique<SwitchIfElement>());
}

} // namespace pcg::internal::elements
