#include "elements/heightfield_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/heightfield_algorithms.hpp"
#include "elements/pcg_element.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

bool parse_sampling(const std::string& value, data::HeightFieldSampling& out)
{
    if (value == "corner") {
        out = data::HeightFieldSampling::Corner;
        return true;
    }
    if (value == "center") {
        out = data::HeightFieldSampling::Center;
        return true;
    }
    return false;
}

bool parse_orientation(const std::string& value, data::HeightFieldOrientation& out)
{
    if (value == "zx") {
        out = data::HeightFieldOrientation::ZX;
        return true;
    }
    if (value == "xy") {
        out = data::HeightFieldOrientation::XY;
        return true;
    }
    if (value == "yz") {
        out = data::HeightFieldOrientation::YZ;
        return true;
    }
    return false;
}

bool parse_fractal(const std::string& value, HeightFieldFractalMode& out)
{
    if (value == "none") {
        out = HeightFieldFractalMode::None;
        return true;
    }
    if (value == "standard") {
        out = HeightFieldFractalMode::Standard;
        return true;
    }
    if (value == "terrain") {
        out = HeightFieldFractalMode::Terrain;
        return true;
    }
    if (value == "hybridTerrain") {
        out = HeightFieldFractalMode::HybridTerrain;
        return true;
    }
    return false;
}

bool parse_combine(const std::string& value, HeightFieldCombineMode& out)
{
    if (value == "replace") out = HeightFieldCombineMode::Replace;
    else if (value == "add") out = HeightFieldCombineMode::Add;
    else if (value == "subtract") out = HeightFieldCombineMode::Subtract;
    else if (value == "difference") out = HeightFieldCombineMode::Difference;
    else if (value == "multiply") out = HeightFieldCombineMode::Multiply;
    else if (value == "maximum") out = HeightFieldCombineMode::Maximum;
    else if (value == "minimum") out = HeightFieldCombineMode::Minimum;
    else if (value == "blend") out = HeightFieldCombineMode::Blend;
    else return false;
    return true;
}

bool parse_layer_mode(const std::string& value, HeightFieldLayerMode& out)
{
    if (value == "replace") out = HeightFieldLayerMode::Replace;
    else if (value == "add") out = HeightFieldLayerMode::Add;
    else if (value == "subtract") out = HeightFieldLayerMode::Subtract;
    else if (value == "multiply") out = HeightFieldLayerMode::Multiply;
    else if (value == "maximum") out = HeightFieldLayerMode::Maximum;
    else if (value == "minimum") out = HeightFieldLayerMode::Minimum;
    else if (value == "blend") out = HeightFieldLayerMode::Blend;
    else return false;
    return true;
}

bool parse_project_mode(const std::string& value, HeightFieldProjectMode& out)
{
    if (value == "replace") out = HeightFieldProjectMode::Replace;
    else if (value == "add") out = HeightFieldProjectMode::Add;
    else if (value == "maximum") out = HeightFieldProjectMode::Maximum;
    else if (value == "minimum") out = HeightFieldProjectMode::Minimum;
    else return false;
    return true;
}

bool parse_noise_options(const nlohmann::json& data,
                         int graph_seed,
                         HeightFieldNoiseOptions& options,
                         bool mask_defaults)
{
    options.noise_layer = data.value("noiseLayer", mask_defaults ? "mask" : "height");
    options.mask_layer = data.value("maskLayer", "mask");
    options.noise_type = data.value("noiseType", "perlin");
    options.center_noise = data.value("centerNoise", !mask_defaults);
    options.amplitude = data.value("amplitude", mask_defaults ? 1.0 : 30.0);
    options.element_size = data.value("elementSize", 64.0);
    options.scale_x = data.value("scaleX", 1.0);
    options.scale_z = data.value("scaleZ", 1.0);
    options.offset_x = data.value("offsetX", 0.0);
    options.offset_z = data.value("offsetZ", 0.0);
    options.max_octaves = data.value("maxOctaves", 5);
    options.lacunarity = data.value("lacunarity", 2.0);
    options.roughness = data.value("roughness", 0.5);
    options.seed = data.value("seed", graph_seed);
    return parse_fractal(data.value("fractal", mask_defaults ? "standard" : "terrain"),
                         options.fractal);
}

class HeightFieldElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightField"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightField missing node");

        HeightFieldCreateOptions options;
        options.size_x = ctx.node->data.value("sizeX", 256.0);
        options.size_z = ctx.node->data.value("sizeZ", 256.0);
        options.center = data::PcgVec3{
            ctx.node->data.value("centerX", 0.0),
            ctx.node->data.value("centerY", 0.0),
            ctx.node->data.value("centerZ", 0.0),
        };
        options.grid_samples = ctx.node->data.value("gridSamples", 257);
        options.grid_spacing = ctx.node->data.value("gridSpacing", 1.0);
        options.initial_height = static_cast<float>(ctx.node->data.value("initialHeight", 0.0));
        options.initial_mask = static_cast<float>(ctx.node->data.value("initialMask", 0.0));

        const std::string division = ctx.node->data.value("divisionMode", "bySize");
        if (division == "bySize")
            options.division_mode = HeightFieldDivisionMode::BySize;
        else if (division == "byAxis")
            options.division_mode = HeightFieldDivisionMode::ByAxis;
        else
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightField invalid divisionMode");

        if (!parse_sampling(ctx.node->data.value("sampling", "corner"), options.sampling))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightField invalid sampling");
        if (!parse_orientation(ctx.node->data.value("orientation", "zx"), options.orientation))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightField invalid orientation");

        data::PcgHeightField heightfield = create_heightfield(options);
        if (!heightfield.valid())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightField invalid size or resolution");
        emit_heightfield(ctx, std::move(heightfield));
        return PCG_OK;
    }
};

class HeightFieldNoiseElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldNoise"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldNoise missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldNoise missing heightfield input");

        HeightFieldNoiseOptions options;
        if (!parse_noise_options(ctx.node->data, ctx.graph_seed, options, false))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldNoise invalid fractal");

        data::PcgHeightField output = *input;
        const data::PcgHeightField* mask = ctx.inputs.find_heightfield("mask");
        if (!apply_heightfield_noise(output, mask, options))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldNoise invalid layer or parameters");
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldMaskNoiseElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldMaskNoise"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldMaskNoise missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldMaskNoise missing heightfield input");
        HeightFieldMaskNoiseOptions options;
        if (!parse_noise_options(ctx.node->data, ctx.graph_seed, options.noise, true) ||
            !parse_combine(ctx.node->data.value("combine", "replace"), options.combine)) {
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldMaskNoise invalid mode");
        }
        options.output_layer = ctx.node->data.value("outputLayer", "mask");
        options.blend = ctx.node->data.value("blend", 1.0);
        options.invert = ctx.node->data.value("invert", false);
        data::PcgHeightField output = *input;
        if (!apply_heightfield_mask_noise(
                output, ctx.inputs.find_heightfield("mask"), options)) {
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldMaskNoise invalid parameters");
        }
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldMaskByFeatureElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldMaskByFeature"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldMaskByFeature missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldMaskByFeature missing heightfield input");
        HeightFieldMaskByFeatureOptions options;
        options.height_layer = ctx.node->data.value("heightLayer", "height");
        options.output_layer = ctx.node->data.value("outputLayer", "mask");
        options.mask_layer = ctx.node->data.value("maskLayer", "mask");
        options.blend = ctx.node->data.value("blend", 1.0);
        options.invert = ctx.node->data.value("invert", false);
        options.mask_by_height = ctx.node->data.value("maskByHeight", true);
        options.min_height = ctx.node->data.value("minHeight", 0.0);
        options.max_height = ctx.node->data.value("maxHeight", 100.0);
        options.height_feather = ctx.node->data.value("heightFeather", 0.0);
        options.mask_by_slope = ctx.node->data.value("maskBySlope", false);
        options.min_slope_degrees = ctx.node->data.value("minSlopeAngle", 0.0);
        options.max_slope_degrees = ctx.node->data.value("maxSlopeAngle", 90.0);
        options.slope_feather_degrees = ctx.node->data.value("slopeFeather", 0.0);
        options.smooth_radius_samples = ctx.node->data.value("smoothRadius", 0);
        if (!parse_combine(ctx.node->data.value("combine", "replace"), options.combine))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldMaskByFeature invalid combine");
        data::PcgHeightField output = *input;
        if (!apply_heightfield_mask_by_feature(
                output, ctx.inputs.find_heightfield("mask"), options)) {
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldMaskByFeature invalid parameters");
        }
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldClipElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldClip"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldClip missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldClip missing heightfield input");
        HeightFieldClipOptions options;
        options.height_layer = ctx.node->data.value("heightLayer", "height");
        options.mask_layer = ctx.node->data.value("maskLayer", "mask");
        options.min_clip_enabled = ctx.node->data.value("minClipEnabled", false);
        options.min_clip = ctx.node->data.value("minClip", 0.0);
        options.max_clip_enabled = ctx.node->data.value("maxClipEnabled", true);
        options.max_clip = ctx.node->data.value("maxClip", 25.0);
        options.output_clipped_layer = ctx.node->data.value("outputClippedLayer", "mesa");
        options.output_edge_layer = ctx.node->data.value("outputEdgeLayer", "cliffs");
        options.generate_mask_from = ctx.node->data.value("generateMaskFrom", "none");
        options.edge_radius_samples = ctx.node->data.value("edgeMaskRadius", 1);
        data::PcgHeightField output = *input;
        if (!apply_heightfield_clip(output, ctx.inputs.find_heightfield("mask"), options))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldClip invalid parameters");
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldTerraceElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldTerrace"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldTerrace missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldTerrace missing heightfield input");
        HeightFieldTerraceOptions options;
        options.height_layer = ctx.node->data.value("heightLayer", "height");
        options.mask_layer = ctx.node->data.value("maskLayer", "mask");
        options.min_height = ctx.node->data.value("minHeight", -1000000.0);
        options.max_height = ctx.node->data.value("maxHeight", 1000000.0);
        options.fade = ctx.node->data.value("fade", 0.15);
        options.max_step_size = ctx.node->data.value("maxStepSize", 6.0);
        options.step_offset = ctx.node->data.value("stepOffset", 0.0);
        options.smooth_edges = ctx.node->data.value("smoothEdges", 0.2);
        options.output_mesa_layer = ctx.node->data.value("outputMesaLayer", "mesa");
        options.output_cliff_layer = ctx.node->data.value("outputCliffLayer", "cliffs");
        data::PcgHeightField output = *input;
        if (!apply_heightfield_terrace(output, ctx.inputs.find_heightfield("mask"), options))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldTerrace invalid parameters");
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldBlurElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldBlur"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldBlur missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldBlur missing heightfield input");
        HeightFieldBlurOptions options;
        options.blur_layer = ctx.node->data.value("blurLayer", "height");
        options.mask_layer = ctx.node->data.value("maskLayer", "mask");
        options.iterations = ctx.node->data.value("iterations", 1);
        options.radius_meters = ctx.node->data.value("radius", 2.0);
        options.mask_aware = ctx.node->data.value("maskAware", true);
        const std::string method = ctx.node->data.value("method", "gaussian");
        if (method == "gaussian") options.method = HeightFieldBlurMethod::Gaussian;
        else if (method == "box") options.method = HeightFieldBlurMethod::Box;
        else return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldBlur invalid method");
        data::PcgHeightField output = *input;
        if (!apply_heightfield_blur(output, ctx.inputs.find_heightfield("mask"), options))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldBlur invalid parameters");
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldResampleElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldResample"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldResample missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldResample missing heightfield input");
        HeightFieldResampleOptions options;
        options.specify_exact_resolution =
            ctx.node->data.value("specifyExactResolution", false);
        options.resolution_scale = ctx.node->data.value("resolutionScale", 2.0);
        options.grid_samples = ctx.node->data.value("gridSamples", 257);
        options.grid_spacing = ctx.node->data.value("gridSpacing", 1.0);
        const std::string division = ctx.node->data.value("divisionMode", "byAxis");
        if (division == "byAxis") options.division_mode = HeightFieldDivisionMode::ByAxis;
        else if (division == "bySize") options.division_mode = HeightFieldDivisionMode::BySize;
        else return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldResample invalid divisionMode");
        data::PcgHeightField output = resample_heightfield(*input, options);
        if (!output.valid())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldResample invalid parameters");
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldLayerElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldLayer"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldLayer missing node");
        const data::PcgHeightField* base = ctx.inputs.find_heightfield("base");
        const data::PcgHeightField* layer = ctx.inputs.find_heightfield("layer");
        if (!base || !layer)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldLayer missing base or layer input");
        HeightFieldLayerOptions options;
        if (!parse_layer_mode(ctx.node->data.value("layerMode", "replace"), options.mode))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldLayer invalid mode");
        options.blend = ctx.node->data.value("blend", 1.0);
        options.layers = ctx.node->data.value("layers", "*");
        options.mask_layer = ctx.node->data.value("maskLayer", "mask");
        options.mask_strength = ctx.node->data.value("maskStrength", 1.0);
        options.invert_mask = ctx.node->data.value("invertMask", false);
        options.base_offset = ctx.node->data.value("baseOffset", 0.0);
        options.base_scale = ctx.node->data.value("baseScale", 1.0);
        options.layer_offset = ctx.node->data.value("layerOffset", 0.0);
        options.layer_scale = ctx.node->data.value("layerScale", 1.0);
        options.final_offset = ctx.node->data.value("finalOffset", 0.0);
        options.final_scale = ctx.node->data.value("finalScale", 1.0);
        options.clamp_minimum = ctx.node->data.value("clampMinimum", false);
        options.minimum = ctx.node->data.value("minimum", 0.0);
        options.clamp_maximum = ctx.node->data.value("clampMaximum", false);
        options.maximum = ctx.node->data.value("maximum", 1.0);
        data::PcgHeightField output = composite_heightfields(
            *base, *layer, ctx.inputs.find_heightfield("mask"), options);
        if (!output.valid())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldLayer failed to composite");
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldErodeElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldErode"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldErode missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldErode missing heightfield input");
        HeightFieldErodeOptions options;
        options.height_layer = ctx.node->data.value("heightLayer", "height");
        options.mask_layer = ctx.node->data.value("maskLayer", "mask");
        options.debris_layer = ctx.node->data.value("debrisLayer", "debris");
        options.sediment_layer = ctx.node->data.value("sedimentLayer", "sediment");
        options.flow_layer = ctx.node->data.value("flowLayer", "flow");
        options.flow_direction_layer = ctx.node->data.value("flowDirectionLayer", "flowdir");
        options.iterations = ctx.node->data.value("iterations", 30);
        options.seed = ctx.node->data.value("seed", ctx.graph_seed);
        options.erodability = ctx.node->data.value("erodability", 1.0);
        options.rainfall = ctx.node->data.value("rainfallCoverage", 0.04);
        options.flow_force = ctx.node->data.value("flowForce", 1.0);
        options.erosion_rate = ctx.node->data.value("erosionRate", 0.18);
        options.deposition_rate = ctx.node->data.value("depositionRate", 0.12);
        options.sediment_capacity = ctx.node->data.value("sedimentCapacity", 1.4);
        options.evaporation_rate = ctx.node->data.value("evaporationRate", 0.12);
        options.weathering_force = ctx.node->data.value("weatheringForce", 0.08);
        options.cut_angle_degrees = ctx.node->data.value("cutAngle", 35.0);
        options.repose_angle_degrees = ctx.node->data.value("reposeAngle", 28.0);
        options.add_debris_to_height = ctx.node->data.value("addDebrisToHeight", true);
        options.add_sediment_to_height = ctx.node->data.value("addSedimentToHeight", true);
        data::PcgHeightField output = *input;
        if (!apply_heightfield_erode(output, ctx.inputs.find_heightfield("mask"), options))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldErode invalid parameters");
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldDistortByNoiseElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldDistortByNoise"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldDistortByNoise missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldDistortByNoise missing heightfield input");
        HeightFieldDistortByNoiseOptions options;
        options.distort_layers = ctx.node->data.value("distortLayers", "height");
        options.mask_layer = ctx.node->data.value("maskLayer", "mask");
        options.noise_type = ctx.node->data.value("noiseType", "simplex");
        options.amplitude = ctx.node->data.value("amplitude", 8.0);
        options.element_size = ctx.node->data.value("elementSize", 64.0);
        options.scale_x = ctx.node->data.value("scaleX", 1.0);
        options.scale_z = ctx.node->data.value("scaleZ", 1.0);
        options.offset_x = ctx.node->data.value("offsetX", 0.0);
        options.offset_z = ctx.node->data.value("offsetZ", 0.0);
        options.roughness = ctx.node->data.value("roughness", 0.5);
        options.max_octaves = ctx.node->data.value("maxOctaves", 4);
        options.substeps = ctx.node->data.value("substeps", 2);
        options.seed = ctx.node->data.value("seed", ctx.graph_seed);
        data::PcgHeightField output = *input;
        if (!apply_heightfield_distort_by_noise(
                output, ctx.inputs.find_heightfield("mask"), options)) {
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldDistortByNoise invalid parameters");
        }
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldProjectElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldProject"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldProject missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("heightfield");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldProject missing heightfield input");
        const data::PcgGeometry geometry = get_geometry_input(
            ctx, "geometry", "HeightFieldProject missing geometry input");
        if (geometry.points().empty() || geometry.faces().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldProject geometry has no polygon surface");
        HeightFieldProjectOptions options;
        options.height_layer = ctx.node->data.value("heightLayer", "height");
        options.hit_farthest = ctx.node->data.value("hitFarthest", true);
        options.max_ray_distance = ctx.node->data.value("maxRayDistance", 1000.0);
        if (!parse_project_mode(
                ctx.node->data.value("combineMethod", "maximum"), options.combine)) {
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldProject invalid combineMethod");
        }
        data::PcgHeightField output = *input;
        if (!apply_heightfield_project(output, geometry, options))
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldProject invalid parameters or excessive work");
        emit_heightfield(ctx, std::move(output));
        return PCG_OK;
    }
};

class HeightFieldScatterElement final : public IPcgElement {
public:
    const char* type_name() const override { return "HeightFieldScatter"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "HeightFieldScatter missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldScatter missing heightfield input");
        HeightFieldScatterOptions options;
        options.height_layer = ctx.node->data.value("heightLayer", "height");
        options.scatter_layer = ctx.node->data.value("scatterAmountLayer", "mask");
        options.use_exact_point_count = ctx.node->data.value("useExactPointCount", true);
        options.point_count = ctx.node->data.value("pointCount", 1000);
        options.density = ctx.node->data.value("density", 0.01);
        options.seed = ctx.node->data.value("globalSeed", ctx.graph_seed);
        options.max_points = ctx.node->data.value("maxPoints", 100000);
        options.candidates_per_point = ctx.node->data.value("candidatesPerPoint", 4);
        options.is_cancel_requested = ctx.is_cancel_requested;
        if (!input->find_layer(options.height_layer) ||
            !input->find_layer(options.scatter_layer) || options.point_count < 0 ||
            !std::isfinite(options.density) || options.density < 0.0 ||
            options.max_points < 0 || options.max_points > 1000000 ||
            options.candidates_per_point < 1 || options.candidates_per_point > 32) {
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "HeightFieldScatter invalid layers or parameters");
        }
        emit_points(ctx, scatter_heightfield(*input, options));
        return PCG_OK;
    }
};

class ConvertHeightFieldElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ConvertHeightField"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ConvertHeightField missing node");
        const data::PcgHeightField* input = ctx.inputs.find_heightfield("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ConvertHeightField missing heightfield input");

        ConvertHeightFieldOptions options;
        options.height_layer = ctx.node->data.value("heightLayer", "height");
        options.density = ctx.node->data.value("density", 1.0);
        data::PcgGeometry geometry = convert_heightfield_to_geometry(*input, options);
        if (geometry.points().empty() || geometry.faces().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ConvertHeightField failed to create polygon surface");
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
};

} // namespace

void register_heightfield_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("HeightField", std::make_unique<HeightFieldElement>());
    map.emplace("HeightFieldNoise", std::make_unique<HeightFieldNoiseElement>());
    map.emplace("HeightFieldMaskNoise", std::make_unique<HeightFieldMaskNoiseElement>());
    map.emplace("HeightFieldMaskByFeature", std::make_unique<HeightFieldMaskByFeatureElement>());
    map.emplace("HeightFieldClip", std::make_unique<HeightFieldClipElement>());
    map.emplace("HeightFieldTerrace", std::make_unique<HeightFieldTerraceElement>());
    map.emplace("HeightFieldBlur", std::make_unique<HeightFieldBlurElement>());
    map.emplace("HeightFieldResample", std::make_unique<HeightFieldResampleElement>());
    map.emplace("HeightFieldLayer", std::make_unique<HeightFieldLayerElement>());
    map.emplace("HeightFieldErode", std::make_unique<HeightFieldErodeElement>());
    map.emplace("HeightFieldDistortByNoise",
                std::make_unique<HeightFieldDistortByNoiseElement>());
    map.emplace("HeightFieldProject", std::make_unique<HeightFieldProjectElement>());
    map.emplace("HeightFieldScatter", std::make_unique<HeightFieldScatterElement>());
    map.emplace("ConvertHeightField", std::make_unique<ConvertHeightFieldElement>());
}

} // namespace pcg::internal::elements
