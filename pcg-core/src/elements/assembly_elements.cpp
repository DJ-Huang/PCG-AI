#include "elements/assembly_elements.hpp"

#include "asset_path.hpp"
#include "elements/assembly_algorithms.hpp"
#include "elements/element_utils.hpp"
#include "elements/lot_subdivision_algorithms.hpp"
#include "mesh_runtime.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

namespace pcg::internal::elements {
namespace {

// Use shared optional_geometry_input from element_utils.hpp (Geometry/Mesh/Spline/Point).

PcgResultCode execute_import_like(PcgContext& ctx, const char* label)
{
    if (!ctx.node)
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (std::string(label) + " missing node").c_str());

    ImportMeshOptions options;
    options.scale = ctx.node->data.value("scale", 1.0);
    options.axis_conversion = ctx.node->data.value("axisConversion", "none");
    if (!std::isfinite(options.scale) || options.scale <= 1.0e-9)
        return fail_ctx(ctx, PCG_ERR_EXECUTION,
                        (std::string(label) + " scale must be finite and greater than zero").c_str());
    if (options.axis_conversion != "none" && options.axis_conversion != "zUpToYUp" &&
        options.axis_conversion != "yUpToZUp")
        return fail_ctx(ctx, PCG_ERR_EXECUTION,
                        (std::string(label) + " axisConversion is invalid").c_str());

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
    if (!import_geometry_file(resolve_asset_path(ctx.node->data), options, geometry, error)) {
        if (error.rfind("ImportMesh", 0) == 0)
            error.replace(0, std::strlen("ImportMesh"), label);
        return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
    }
    emit_geometry(ctx, std::move(geometry));
    return PCG_OK;
}

class ImportMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ImportMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        return execute_import_like(ctx, "ImportMesh");
    }
};

class Meshy3DGeneratorElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Meshy3DGenerator"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        // Unity PcgMeshyResolver downloads/caches GLB and injects absolute path before cook.
        return execute_import_like(ctx, "Meshy3DGenerator");
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
        const auto& data = ctx.node->data;
        MatchSizeOptions options;
        options.justify_with = data.value("justifyWith", "inputIfWired");
        options.group = data.value("group", "");
        options.group_type = data.value("groupType", "guess");
        options.use_groups_for_bounds = data.value("useGroupsForBounds", false);
        options.source_group = data.value("sourceGroup", "");
        options.source_group_type = data.value("sourceGroupType", "guess");
        options.target_group = data.value("targetGroup", "");
        options.target_group_type = data.value("targetGroupType", "guess");
        options.translate = data.value("translate", true);
        options.scale_to_fit = data.value("scaleToFit", true);
        options.uniform_scale = data.value("uniformScale", true);
        if (data.contains("scaleAxis")) {
            options.scale_axis = data.value("scaleAxis", "bestFit");
        } else {
            const auto legacy_mode = data.value("uniformScaleMode", "fit");
            if (legacy_mode == "fill")
                options.scale_axis = "fill";
            else
                options.scale_axis = "bestFit";
        }
        options.scale_x = data.value("scaleX", true);
        options.scale_y = data.value("scaleY", true);
        options.scale_z = data.value("scaleZ", true);
        options.justify_x = data.contains("justifyX")
            ? data.value("justifyX", "center")
            : data.value("sourceJustifyX", "center");
        options.justify_y = data.contains("justifyY")
            ? data.value("justifyY", "center")
            : data.value("sourceJustifyY", "center");
        options.justify_z = data.contains("justifyZ")
            ? data.value("justifyZ", "center")
            : data.value("sourceJustifyZ", "center");
        const bool legacy_justify =
            !data.contains("justifyX") && data.contains("sourceJustifyX");
        options.target_justify_x =
            data.value("targetJustifyX", legacy_justify ? "center" : "same");
        options.target_justify_y =
            data.value("targetJustifyY", legacy_justify ? "center" : "same");
        options.target_justify_z =
            data.value("targetJustifyZ", legacy_justify ? "center" : "same");
        options.offset = read_vector_param(data, "offset", {0.0, 0.0, 0.0});
        if (data.contains("targetPosition") || data.contains("targetPositionX")) {
            options.target_position = read_vector_param(data, "targetPosition", {0.0, 0.0, 0.0});
        } else {
            options.target_position = read_vector_param(data, "targetCenter", {0.0, 0.0, 0.0});
        }
        options.target_size = read_vector_param(data, "targetSize", {1.0, 1.0, 1.0});
        options.restore_transform = data.value("restoreTransform", false);
        options.restore_attribute = data.value("restoreAttribute", "xform");
        options.stash_transform = data.value("stashTransform", true);
        options.stash_attribute = data.value("stashAttribute", "xform");

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
        // Must use read_seed_param_number — data.value("seed", 0) truncates 2.3 → 2.
        options.seed = read_seed_param_number(ctx.node->data, "seed", 0.0);
        options.graph_seed = ctx.graph_seed;
        options.alignment = ctx.node->data.value("alignment", "longestEdge");

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
        options.capture_origin = read_vector_param(ctx.node->data, "captureOrigin", {0.0, 0.0, 0.0});
        options.capture_direction = read_vector_param(ctx.node->data, "captureDirection", {0.0, 1.0, 0.0});
        options.up_direction = read_vector_param(ctx.node->data, "upDirection", {1.0, 0.0, 0.0});
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
    map.emplace("Meshy3DGenerator", std::make_unique<Meshy3DGeneratorElement>());
    map.emplace("MatchSize", std::make_unique<MatchSizeElement>());
    map.emplace("BendMesh", std::make_unique<BendMeshElement>());
    map.emplace("LotSubdivision", std::make_unique<LotSubdivisionElement>());
}

} // namespace pcg::internal::elements
