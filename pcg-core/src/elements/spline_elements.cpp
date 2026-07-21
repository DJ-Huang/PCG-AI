#include "elements/spline_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/facade_foundation_algorithms.hpp"
#include "elements/pcg_element.hpp"
#include "elements/spline_algorithms.hpp"
#include "spline_runtime.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

std::vector<geometry::Vec3> parse_control_points(const nlohmann::json& data)
{
    std::vector<geometry::Vec3> points;
    const std::string raw = data.value("controlPoints", std::string("[]"));
    try {
        const nlohmann::json parsed = nlohmann::json::parse(raw);
        if (!parsed.is_array())
            return points;
        for (const auto& item : parsed) {
            points.push_back(geometry::Vec3{
                item.value("x", 0.0),
                item.value("y", 0.0),
                item.value("z", 0.0),
            });
        }
    } catch (...) {
    }
    return points;
}

class CreateSplineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CreateSpline"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateSpline missing node");

        CreateSplineOptions opts;
        opts.mode = ctx.node->data.value("mode", std::string("catmullRom"));
        opts.closed = ctx.node->data.value("closed", false);
        opts.subdivisions = ctx.node->data.value("subdivisions", 8);
        opts.start_x = ctx.node->data.value("startX", 0.0);
        opts.start_y = ctx.node->data.value("startY", 0.0);
        opts.start_z = ctx.node->data.value("startZ", 0.0);
        opts.end_x = ctx.node->data.value("endX", 10.0);
        opts.end_y = ctx.node->data.value("endY", 0.0);
        opts.end_z = ctx.node->data.value("endZ", 0.0);
        opts.control_points = parse_control_points(ctx.node->data);

        emit_splines(ctx, create_spline_data(opts));
        return PCG_OK;
    }
};

class GetSplineDataElement final : public IPcgElement {
public:
    const char* type_name() const override { return "GetSplineData"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GetSplineData missing node");

        if (!ctx.splines)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GetSplineData missing spline runtime");

        const data::PcgSplineData* splines = ctx.splines->find(ctx.node->id);
        if (!splines || splines->splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GetSplineData missing spline slot");

        emit_splines(ctx, *splines);
        return PCG_OK;
    }
};

class ResampleSplineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ResampleSpline"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ResampleSpline missing node");

        data::PcgSplineData input;
        if (ctx.inputs.find_splines("in") != nullptr ||
            (ctx.inputs.find("in") && ctx.inputs.find("in")->splines)) {
            input = get_splines_input(ctx, "in", "ResampleSpline missing spline input");
        } else if (ctx.inputs.find_geometry("in") != nullptr ||
                   ctx.inputs.find_mesh("in") != nullptr) {
            // Accept mesh/geometry edges via ConvertLine (unshared by default).
            const auto geometry =
                get_geometry_input(ctx, "in", "ResampleSpline missing spline/mesh input");
            ConvertLineOptions convert_opts;
            convert_opts.mode = ctx.node->data.value("edgeMode", std::string("unshared"));
            convert_opts.edge_group = ctx.node->data.value("edgeGroup", std::string());
            if (!convert_opts.edge_group.empty())
                convert_opts.mode = "group";
            input = convert_line_geometry(geometry, convert_opts);
        } else {
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ResampleSpline missing spline input");
        }
        if (input.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ResampleSpline missing spline input");

        ResampleSplineOptions opts;
        opts.mode = ctx.node->data.value("mode", std::string("spacing"));
        opts.spacing = ctx.node->data.value("spacing", 1.0);
        opts.point_count = ctx.node->data.value("pointCount", 32);

        emit_splines(ctx, resample_spline_data(input, opts));
        return PCG_OK;
    }
};

class SampleAlongSplineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SampleAlongSpline"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SampleAlongSpline missing node");

        const data::PcgSplineData splines =
            get_splines_input(ctx, "in", "SampleAlongSpline missing spline input");
        if (splines.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SampleAlongSpline missing spline input");

        SampleAlongSplineOptions opts;
        opts.spacing = ctx.node->data.value("spacing", 5.0);
        opts.offset = ctx.node->data.value("offset", 0.0);
        opts.include_end = ctx.node->data.value("includeEnd", true);
        opts.align_to_tangent = ctx.node->data.value("alignToTangent", true);
        opts.seed = mix_seed(ctx.graph_seed, ctx.node->data.value("seed", 0));

        emit_points(ctx, sample_along_spline(splines, opts));
        return PCG_OK;
    }
};

} // namespace

class CreateSpiralSplineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CreateSpiralSpline"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateSpiralSpline missing node");

        CreateSpiralSplineOptions opts;
        opts.radius = ctx.node->data.value("radius", 1.0);
        opts.pitch = ctx.node->data.value("pitch", 0.5);
        opts.turns = ctx.node->data.value("turns", 3.0);
        opts.points_per_turn = ctx.node->data.value("pointsPerTurn", 24);
        opts.axis = ctx.node->data.value("axis", "y");

        data::PcgSplineData spline_data = create_spiral_spline_data(opts);
        if (spline_data.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateSpiralSpline invalid parameters");

        emit_splines(ctx, std::move(spline_data));
        return PCG_OK;
    }
};

class CreateArcSplineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CreateArcSpline"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateArcSpline missing node");

        CreateArcSplineOptions opts;
        opts.radius = ctx.node->data.value("radius", 1.0);
        opts.start_angle_deg = ctx.node->data.value("startAngle", 0.0);
        opts.end_angle_deg = ctx.node->data.value("endAngle", 180.0);
        opts.segments = ctx.node->data.value("segments", 16);
        opts.axis = ctx.node->data.value("axis", "z");

        data::PcgSplineData spline_data = create_arc_spline_data(opts);
        if (spline_data.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateArcSpline invalid parameters");

        emit_splines(ctx, std::move(spline_data));
        return PCG_OK;
    }
};

void register_spline_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("CreateSpline", std::make_unique<CreateSplineElement>());
    map.emplace("CreateSpiralSpline", std::make_unique<CreateSpiralSplineElement>());
    map.emplace("CreateArcSpline", std::make_unique<CreateArcSplineElement>());
    map.emplace("GetSplineData", std::make_unique<GetSplineDataElement>());
    map.emplace("ResampleSpline", std::make_unique<ResampleSplineElement>());
    map.emplace("SampleAlongSpline", std::make_unique<SampleAlongSplineElement>());
}

} // namespace pcg::internal::elements
