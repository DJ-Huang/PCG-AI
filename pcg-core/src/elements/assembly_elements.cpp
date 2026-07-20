#include "elements/assembly_elements.hpp"

#include "asset_path.hpp"
#include "elements/assembly_algorithms.hpp"
#include "elements/element_utils.hpp"
#include "elements/lot_subdivision_algorithms.hpp"
#include "mesh_runtime.hpp"

#include <algorithm>
#include <cmath>

namespace pcg::internal::elements {
namespace {

data::PcgVec3 read_vector(const nlohmann::json& value,
                          const char* prefix,
                          data::PcgVec3 fallback)
{
    return {value.value(std::string(prefix) + "X", fallback.x),
            value.value(std::string(prefix) + "Y", fallback.y),
            value.value(std::string(prefix) + "Z", fallback.z)};
}

const data::PcgGeometry* optional_geometry_input(PcgContext& ctx,
                                                 const char* pin,
                                                 data::PcgGeometry& storage)
{
    if (const auto* geometry = ctx.inputs.find_geometry(pin))
        return geometry;
    if (const auto* mesh = ctx.inputs.find_mesh(pin)) {
        storage = data::geometry_from_mesh(*mesh);
        return &storage;
    }
    return nullptr;
}

class ImportMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ImportMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ImportMesh missing node");

        ImportMeshOptions options;
        options.scale = ctx.node->data.value("scale", 1.0);
        options.axis_conversion = ctx.node->data.value("axisConversion", "none");
        if (!std::isfinite(options.scale) || options.scale <= 1.0e-9)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "ImportMesh scale must be finite and greater than zero");
        if (options.axis_conversion != "none" && options.axis_conversion != "zUpToYUp" &&
            options.axis_conversion != "yUpToZUp")
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "ImportMesh axisConversion is invalid");

        data::PcgGeometry geometry;
        if (ctx.meshes) {
            if (const auto* runtime_mesh = ctx.meshes->find(ctx.node->id)) {
                geometry = data::geometry_from_mesh(*runtime_mesh);
                data::GeometryAffineTransform transform;
                const double scale = options.scale;
                if (options.axis_conversion == "zUpToYUp") {
                    transform.linear = {scale, 0.0, 0.0,
                                        0.0, 0.0, scale,
                                        0.0, -scale, 0.0};
                } else if (options.axis_conversion == "yUpToZUp") {
                    transform.linear = {scale, 0.0, 0.0,
                                        0.0, 0.0, -scale,
                                        0.0, scale, 0.0};
                } else {
                    transform.linear = {scale, 0.0, 0.0,
                                        0.0, scale, 0.0,
                                        0.0, 0.0, scale};
                }
                for (auto& point : geometry.points_mut())
                    point = data::transform_position(transform, point);
                data::transform_geometry_attributes(geometry, transform);
                emit_geometry(ctx, std::move(geometry));
                return PCG_OK;
            }
        }

        std::string error;
        if (!import_geometry_file(resolve_asset_path(ctx.node->data), options, geometry, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
};

class MatchSizeElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MatchSize"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MatchSize missing node");
        const auto source = get_geometry_input(ctx, "source", "MatchSize missing source input");
        if (source.points().empty())
            return PCG_ERR_EXECUTION;

        data::PcgGeometry reference_storage;
        const auto* reference = optional_geometry_input(ctx, "reference", reference_storage);
        MatchSizeOptions options;
        options.scale_to_fit = ctx.node->data.value("scaleToFit", true);
        options.uniform_scale = ctx.node->data.value("uniformScale", false);
        options.uniform_scale_mode = ctx.node->data.value("uniformScaleMode", "fit");
        options.source_justify_x = ctx.node->data.value("sourceJustifyX", "center");
        options.source_justify_y = ctx.node->data.value("sourceJustifyY", "center");
        options.source_justify_z = ctx.node->data.value("sourceJustifyZ", "center");
        options.target_justify_x = ctx.node->data.value("targetJustifyX", "center");
        options.target_justify_y = ctx.node->data.value("targetJustifyY", "center");
        options.target_justify_z = ctx.node->data.value("targetJustifyZ", "center");
        options.target_center = read_vector(ctx.node->data, "targetCenter", {0.0, 0.0, 0.0});
        options.target_size = read_vector(ctx.node->data, "targetSize", {1.0, 1.0, 1.0});

        data::PcgGeometry output;
        std::string error;
        if (!match_size_geometry(source, reference, options, output, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
        emit_geometry(ctx, std::move(output));
        return PCG_OK;
    }
};

class LotSubdivisionElement final : public IPcgElement {
public:
    const char* type_name() const override { return "LotSubdivision"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision missing node");

        const auto input =
            get_geometry_input(ctx, "in", "LotSubdivision missing mesh input");
        if (input.faces().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision input has no faces");

        LotSubdivisionOptions options;
        options.min_size = ctx.node->data.value("minSize", 1.0);
        options.iterations = ctx.node->data.value("iterations", 3);
        options.irregularity = ctx.node->data.value("irregularity", 0.5);
        options.seed = ctx.node->data.value("seed", 0);
        options.alignment = ctx.node->data.value("alignment", "longestEdge");
        options.seed ^= static_cast<int>(ctx.graph_seed);

        if (!std::isfinite(options.min_size) || options.min_size < 0.0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision minSize must be >= 0");
        if (options.iterations < 0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision iterations must be >= 0");
        options.irregularity = std::clamp(options.irregularity, 0.0, 1.0);
        if (options.alignment != "longestEdge" && options.alignment != "boundingBox")
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision alignment is invalid");

        emit_geometry(ctx, lot_subdivide_geometry(input, options));
        return PCG_OK;
    }
};

class BendMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "BendMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BendMesh missing node");
        const auto source = get_geometry_input(ctx, "source", "BendMesh missing source input");
        if (source.points().empty())
            return PCG_ERR_EXECUTION;

        data::PcgGeometry rest_storage;
        const auto* rest = optional_geometry_input(ctx, "rest", rest_storage);
        BendMeshOptions options;
        options.capture_origin = read_vector(ctx.node->data, "captureOrigin", {0.0, 0.0, 0.0});
        options.capture_direction = read_vector(ctx.node->data, "captureDirection", {0.0, 1.0, 0.0});
        options.up_direction = read_vector(ctx.node->data, "upDirection", {1.0, 0.0, 0.0});
        options.capture_length = ctx.node->data.value("captureLength", 1.0);
        options.angle_degrees = ctx.node->data.value("angle", 0.0);
        options.mask_attribute = ctx.node->data.value("maskAttribute", "bendmask");

        data::PcgGeometry output;
        std::string error;
        if (!bend_geometry(source, rest, options, output, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
        emit_geometry(ctx, std::move(output));
        return PCG_OK;
    }
};

} // namespace

void register_assembly_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("ImportMesh", std::make_unique<ImportMeshElement>());
    map.emplace("MatchSize", std::make_unique<MatchSizeElement>());
    map.emplace("BendMesh", std::make_unique<BendMeshElement>());
    map.emplace("LotSubdivision", std::make_unique<LotSubdivisionElement>());
}

} // namespace pcg::internal::elements
