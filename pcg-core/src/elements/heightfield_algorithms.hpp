#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_heightfield.hpp"
#include "data/pcg_point_data.hpp"

#include <string>

namespace pcg::internal::elements {

enum class HeightFieldDivisionMode {
    ByAxis,
    BySize,
};

enum class HeightFieldFractalMode {
    None,
    Standard,
    Terrain,
    HybridTerrain,
};

enum class HeightFieldCombineMode {
    Replace,
    Add,
    Subtract,
    Difference,
    Multiply,
    Maximum,
    Minimum,
    Blend,
};

enum class HeightFieldBlurMethod {
    Gaussian,
    Box,
};

enum class HeightFieldLayerMode {
    Replace,
    Add,
    Subtract,
    Multiply,
    Maximum,
    Minimum,
    Blend,
};

enum class HeightFieldProjectMode {
    Replace,
    Add,
    Maximum,
    Minimum,
};

struct HeightFieldCreateOptions {
    double size_x = 256.0;
    double size_z = 256.0;
    data::PcgVec3 center{};
    data::HeightFieldSampling sampling = data::HeightFieldSampling::Corner;
    data::HeightFieldOrientation orientation = data::HeightFieldOrientation::ZX;
    HeightFieldDivisionMode division_mode = HeightFieldDivisionMode::BySize;
    int grid_samples = 257;
    double grid_spacing = 1.0;
    float initial_height = 0.0f;
    float initial_mask = 0.0f;
};

struct HeightFieldNoiseOptions {
    std::string noise_layer = "height";
    std::string mask_layer = "mask";
    std::string noise_type = "perlin";
    HeightFieldFractalMode fractal = HeightFieldFractalMode::Terrain;
    bool center_noise = true;
    double amplitude = 30.0;
    double element_size = 64.0;
    double scale_x = 1.0;
    double scale_z = 1.0;
    double offset_x = 0.0;
    double offset_z = 0.0;
    int max_octaves = 5;
    double lacunarity = 2.0;
    double roughness = 0.5;
    int seed = 0;
};

struct ConvertHeightFieldOptions {
    std::string height_layer = "height";
    double density = 1.0;
};

struct HeightFieldMaskNoiseOptions {
    HeightFieldNoiseOptions noise;
    std::string output_layer = "mask";
    HeightFieldCombineMode combine = HeightFieldCombineMode::Replace;
    double blend = 1.0;
    bool invert = false;
};

struct HeightFieldMaskByFeatureOptions {
    std::string height_layer = "height";
    std::string output_layer = "mask";
    std::string mask_layer = "mask";
    HeightFieldCombineMode combine = HeightFieldCombineMode::Replace;
    double blend = 1.0;
    bool invert = false;
    bool mask_by_height = true;
    double min_height = 0.0;
    double max_height = 100.0;
    double height_feather = 0.0;
    bool mask_by_slope = false;
    double min_slope_degrees = 0.0;
    double max_slope_degrees = 90.0;
    double slope_feather_degrees = 0.0;
    int smooth_radius_samples = 0;
};

struct HeightFieldClipOptions {
    std::string height_layer = "height";
    std::string mask_layer = "mask";
    bool min_clip_enabled = false;
    double min_clip = 0.0;
    bool max_clip_enabled = true;
    double max_clip = 25.0;
    std::string output_clipped_layer = "mesa";
    std::string output_edge_layer = "cliffs";
    std::string generate_mask_from = "none";
    int edge_radius_samples = 1;
};

struct HeightFieldTerraceOptions {
    std::string height_layer = "height";
    std::string mask_layer = "mask";
    double min_height = -1000000.0;
    double max_height = 1000000.0;
    double fade = 0.15;
    double max_step_size = 6.0;
    double step_offset = 0.0;
    double smooth_edges = 0.2;
    std::string output_mesa_layer = "mesa";
    std::string output_cliff_layer = "cliffs";
};

struct HeightFieldBlurOptions {
    std::string blur_layer = "height";
    std::string mask_layer = "mask";
    HeightFieldBlurMethod method = HeightFieldBlurMethod::Gaussian;
    int iterations = 1;
    double radius_meters = 2.0;
    bool mask_aware = true;
};

struct HeightFieldResampleOptions {
    bool specify_exact_resolution = false;
    double resolution_scale = 2.0;
    HeightFieldDivisionMode division_mode = HeightFieldDivisionMode::ByAxis;
    int grid_samples = 257;
    double grid_spacing = 1.0;
};

struct HeightFieldLayerOptions {
    HeightFieldLayerMode mode = HeightFieldLayerMode::Replace;
    double blend = 1.0;
    std::string layers = "*";
    std::string mask_layer = "mask";
    double mask_strength = 1.0;
    bool invert_mask = false;
    double base_offset = 0.0;
    double base_scale = 1.0;
    double layer_offset = 0.0;
    double layer_scale = 1.0;
    double final_offset = 0.0;
    double final_scale = 1.0;
    bool clamp_minimum = false;
    double minimum = 0.0;
    bool clamp_maximum = false;
    double maximum = 1.0;
};

struct HeightFieldErodeOptions {
    std::string height_layer = "height";
    std::string mask_layer = "mask";
    std::string debris_layer = "debris";
    std::string sediment_layer = "sediment";
    std::string flow_layer = "flow";
    std::string flow_direction_layer = "flowdir";
    int iterations = 30;
    int seed = 0;
    double erodability = 1.0;
    double rainfall = 0.04;
    double flow_force = 1.0;
    double erosion_rate = 0.18;
    double deposition_rate = 0.12;
    double sediment_capacity = 1.4;
    double evaporation_rate = 0.12;
    double weathering_force = 0.08;
    double cut_angle_degrees = 35.0;
    double repose_angle_degrees = 28.0;
    bool add_debris_to_height = true;
    bool add_sediment_to_height = true;
};

struct HeightFieldDistortByNoiseOptions {
    std::string distort_layers = "height";
    std::string mask_layer = "mask";
    std::string noise_type = "simplex";
    double amplitude = 8.0;
    double element_size = 64.0;
    double scale_x = 1.0;
    double scale_z = 1.0;
    double offset_x = 0.0;
    double offset_z = 0.0;
    double roughness = 0.5;
    int max_octaves = 4;
    int substeps = 2;
    int seed = 0;
};

struct HeightFieldProjectOptions {
    std::string height_layer = "height";
    HeightFieldProjectMode combine = HeightFieldProjectMode::Maximum;
    bool hit_farthest = true;
    double max_ray_distance = 1000.0;
};

struct HeightFieldScatterOptions {
    std::string height_layer = "height";
    std::string scatter_layer = "mask";
    bool use_exact_point_count = true;
    int point_count = 1000;
    double density = 0.01;
    int seed = 0;
    int max_points = 100000;
    int candidates_per_point = 4;
    bool (*is_cancel_requested)() = nullptr;
};

bool resolve_heightfield_resolution(const HeightFieldCreateOptions& options,
                                    int& out_resolution_x,
                                    int& out_resolution_z);

data::PcgHeightField create_heightfield(const HeightFieldCreateOptions& options);

bool apply_heightfield_noise(data::PcgHeightField& heightfield,
                             const data::PcgHeightField* mask,
                             const HeightFieldNoiseOptions& options);

bool apply_heightfield_mask_noise(data::PcgHeightField& heightfield,
                                  const data::PcgHeightField* mask,
                                  const HeightFieldMaskNoiseOptions& options);

bool apply_heightfield_mask_by_feature(data::PcgHeightField& heightfield,
                                       const data::PcgHeightField* mask,
                                       const HeightFieldMaskByFeatureOptions& options);

bool apply_heightfield_clip(data::PcgHeightField& heightfield,
                            const data::PcgHeightField* mask,
                            const HeightFieldClipOptions& options);

bool apply_heightfield_terrace(data::PcgHeightField& heightfield,
                               const data::PcgHeightField* mask,
                               const HeightFieldTerraceOptions& options);

bool apply_heightfield_blur(data::PcgHeightField& heightfield,
                            const data::PcgHeightField* mask,
                            const HeightFieldBlurOptions& options);

data::PcgHeightField resample_heightfield(
    const data::PcgHeightField& heightfield,
    const HeightFieldResampleOptions& options);

data::PcgHeightField composite_heightfields(
    const data::PcgHeightField& base,
    const data::PcgHeightField& layer,
    const data::PcgHeightField* mask,
    const HeightFieldLayerOptions& options);

bool apply_heightfield_erode(data::PcgHeightField& heightfield,
                             const data::PcgHeightField* mask,
                             const HeightFieldErodeOptions& options);

bool apply_heightfield_distort_by_noise(
    data::PcgHeightField& heightfield,
    const data::PcgHeightField* mask,
    const HeightFieldDistortByNoiseOptions& options);

bool apply_heightfield_project(data::PcgHeightField& heightfield,
                               const data::PcgGeometry& geometry,
                               const HeightFieldProjectOptions& options);

data::PcgPointData scatter_heightfield(
    const data::PcgHeightField& heightfield,
    const HeightFieldScatterOptions& options);

data::PcgGeometry convert_heightfield_to_geometry(
    const data::PcgHeightField& heightfield,
    const ConvertHeightFieldOptions& options = {});

} // namespace pcg::internal::elements
