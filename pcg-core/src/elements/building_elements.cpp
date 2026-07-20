#include "elements/building_elements.hpp"

#include "elements/building_algorithms.hpp"
#include "elements/element_utils.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

class CopyMeshToPointsElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CopyMeshToPoints"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CopyMeshToPoints missing node");

        const data::PcgGeometry prototype =
            get_geometry_input(ctx, "prototype", "CopyMeshToPoints missing prototype input");
        if (prototype.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CopyMeshToPoints missing prototype input");

        const bool has_points = ctx.inputs.find_points("points") != nullptr ||
                                ctx.inputs.find_json("points") != nullptr;
        if (!has_points)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CopyMeshToPoints missing points input");

        const data::PcgPointData points =
            get_points_input(ctx, "points", "CopyMeshToPoints missing points input");
        emit_geometry(ctx, copy_geometry_to_points(prototype, points));
        return PCG_OK;
    }
};

class AttributeRandomizeElement final : public IPcgElement {
public:
    const char* type_name() const override { return "AttributeRandomize"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AttributeRandomize missing node");

        const bool has_points = ctx.inputs.find_points("in") != nullptr ||
                                ctx.inputs.find_json("in") != nullptr;
        if (!has_points)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AttributeRandomize missing points input");

        AttributeRandomizeOptions options;
        options.seed = ctx.node->data.value("seed", 0);
        options.translate_x = ctx.node->data.value("translateX", 0.0);
        options.translate_y = ctx.node->data.value("translateY", 0.0);
        options.translate_z = ctx.node->data.value("translateZ", 0.0);
        options.rotate_x_deg = ctx.node->data.value("rotateX", 0.0);
        options.rotate_y_deg = ctx.node->data.value("rotateY", 0.0);
        options.rotate_z_deg = ctx.node->data.value("rotateZ", 0.0);
        options.scale_min = ctx.node->data.value("scaleMin", 1.0);
        options.scale_max = ctx.node->data.value("scaleMax", 1.0);
        options.color_min_r = ctx.node->data.value("colorMinR", 1.0);
        options.color_min_g = ctx.node->data.value("colorMinG", 1.0);
        options.color_min_b = ctx.node->data.value("colorMinB", 1.0);
        options.color_max_r = ctx.node->data.value("colorMaxR", 1.0);
        options.color_max_g = ctx.node->data.value("colorMaxG", 1.0);
        options.color_max_b = ctx.node->data.value("colorMaxB", 1.0);
        options.material_names = parse_name_list(ctx.node->data, "materialNames");

        const data::PcgPointData input =
            get_points_input(ctx, "in", "AttributeRandomize missing points input");
        emit_points(ctx, randomize_point_attributes(input, ctx.graph_seed, options));
        return PCG_OK;
    }
};

class SwitchElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Switch"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Switch missing node");

        const int index = std::clamp(ctx.node->data.value("index", 0), 0, 3);
        const std::string pin = "in" + std::to_string(index);
        const data::PcgTaggedData* selected = ctx.inputs.find(pin);
        if (!selected)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Switch selected input is not connected");

        if (selected->geometry) {
            ctx.outputs.add_geometry_shared("out", selected->geometry);
        } else if (selected->mesh) {
            ctx.outputs.add_mesh_shared("out", selected->mesh);
        } else {
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Switch selected input is not Geometry or Mesh");
        }
        return PCG_OK;
    }
};

} // namespace

void register_building_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("CopyMeshToPoints", std::make_unique<CopyMeshToPointsElement>());
    map.emplace("AttributeRandomize", std::make_unique<AttributeRandomizeElement>());
    map.emplace("Switch", std::make_unique<SwitchElement>());
}

} // namespace pcg::internal::elements
