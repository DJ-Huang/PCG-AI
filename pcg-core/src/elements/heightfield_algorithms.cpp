#include "elements/heightfield_algorithms.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <random>
#include <set>
#include <sstream>
#include <vector>

namespace pcg::internal::elements {
namespace {

constexpr int kMaxGridSamples = 2049;
constexpr std::size_t kMaxHeightFieldSamples =
    static_cast<std::size_t>(kMaxGridSamples) * static_cast<std::size_t>(kMaxGridSamples);

double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

uint32_t hash_lattice(int x, int z, int seed)
{
    uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(z) * 0xd8163841u;
    h ^= static_cast<uint32_t>(seed) * 0xcb1ab31fu;
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

double gradient_noise(double x, double z, int seed)
{
    static constexpr double kInvSqrt2 = 0.70710678118654752440;
    static constexpr double gradients[8][2] = {
        {1.0, 0.0}, {-1.0, 0.0}, {0.0, 1.0}, {0.0, -1.0},
        {kInvSqrt2, kInvSqrt2}, {-kInvSqrt2, kInvSqrt2},
        {kInvSqrt2, -kInvSqrt2}, {-kInvSqrt2, -kInvSqrt2},
    };

    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const double tx = x - static_cast<double>(x0);
    const double tz = z - static_cast<double>(z0);

    auto dot = [&](int ix, int iz, double dx, double dz) {
        const auto& gradient = gradients[hash_lattice(ix, iz, seed) & 7u];
        return gradient[0] * dx + gradient[1] * dz;
    };

    const double n00 = dot(x0, z0, tx, tz);
    const double n10 = dot(x0 + 1, z0, tx - 1.0, tz);
    const double n01 = dot(x0, z0 + 1, tx, tz - 1.0);
    const double n11 = dot(x0 + 1, z0 + 1, tx - 1.0, tz - 1.0);
    const double u = fade(tx);
    const double v = fade(tz);
    const double nx0 = n00 + (n10 - n00) * u;
    const double nx1 = n01 + (n11 - n01) * u;
    return std::clamp((nx0 + (nx1 - nx0) * v) * 1.4142135623730951, -1.0, 1.0);
}

double fractal_noise(double x,
                     double z,
                     const HeightFieldNoiseOptions& options)
{
    const int octaves = options.fractal == HeightFieldFractalMode::None
        ? 1
        : std::clamp(options.max_octaves, 1, 12);
    const double lacunarity = std::max(0.01, std::abs(options.lacunarity));
    const double roughness = std::clamp(options.roughness, 0.0, 1.0);

    double sum = 0.0;
    double normalizer = 0.0;
    double frequency = 1.0;
    double weight = 1.0;
    for (int octave = 0; octave < octaves; ++octave) {
        const double signal = gradient_noise(x * frequency,
                                             z * frequency,
                                             options.seed + octave * 1013);
        sum += signal * weight;
        normalizer += weight;
        frequency *= lacunarity;
        weight *= roughness;
        if (weight <= std::numeric_limits<double>::epsilon())
            break;
    }

    double value = normalizer > 0.0 ? sum / normalizer : 0.0;
    if (options.fractal == HeightFieldFractalMode::Terrain && value < 0.0) {
        value *= 0.35;
    } else if (options.fractal == HeightFieldFractalMode::HybridTerrain && value < 0.0) {
        value = -std::pow(-value, 0.65);
    }
    return std::clamp(value, -1.0, 1.0);
}

void plane_coordinates(const data::PcgHeightField& heightfield,
                       const data::PcgVec3& position,
                       double& out_x,
                       double& out_z)
{
    out_x = position.x;
    out_z = position.z;
    if (heightfield.orientation() == data::HeightFieldOrientation::XY)
        out_z = position.y;
    else if (heightfield.orientation() == data::HeightFieldOrientation::YZ)
        out_x = position.y;
}

double noise_at_position(const data::PcgHeightField& heightfield,
                         const data::PcgVec3& position,
                         const HeightFieldNoiseOptions& options)
{
    double plane_x = 0.0;
    double plane_z = 0.0;
    plane_coordinates(heightfield, position, plane_x, plane_z);
    const double sx = std::max(std::abs(options.scale_x), 1e-6);
    const double sz = std::max(std::abs(options.scale_z), 1e-6);
    const double nx = (plane_x - options.offset_x) / (options.element_size * sx);
    const double nz = (plane_z - options.offset_z) / (options.element_size * sz);
    double noise = fractal_noise(nx, nz, options);
    if (!options.center_noise)
        noise = noise * 0.5 + 0.5;
    return noise;
}

bool external_mask_weight(const data::PcgHeightField* mask,
                          const std::string& layer_name,
                          const data::PcgVec3& position,
                          double& out_weight)
{
    out_weight = 1.0;
    if (!mask)
        return true;
    if (!mask->sample_scalar_world(
            layer_name, position.x, position.y, position.z, out_weight)) {
        return false;
    }
    out_weight = std::clamp(out_weight, 0.0, 1.0);
    return true;
}

double combine_value(double old_value,
                     double new_value,
                     HeightFieldCombineMode mode,
                     double blend)
{
    switch (mode) {
    case HeightFieldCombineMode::Add:
        return old_value + new_value;
    case HeightFieldCombineMode::Subtract:
        return old_value - new_value;
    case HeightFieldCombineMode::Difference:
        return std::abs(old_value - new_value);
    case HeightFieldCombineMode::Multiply:
        return old_value * new_value;
    case HeightFieldCombineMode::Maximum:
        return std::max(old_value, new_value);
    case HeightFieldCombineMode::Minimum:
        return std::min(old_value, new_value);
    case HeightFieldCombineMode::Blend:
        return old_value + (new_value - old_value) * std::clamp(blend, 0.0, 1.0);
    case HeightFieldCombineMode::Replace:
    default:
        return new_value;
    }
}

double smoothstep01(double value)
{
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

double range_weight(double value, double minimum, double maximum, double feather)
{
    if (minimum > maximum)
        return 0.0;
    if (feather <= 0.0)
        return value >= minimum && value <= maximum ? 1.0 : 0.0;
    const double lower = smoothstep01((value - (minimum - feather)) / feather);
    const double upper = 1.0 - smoothstep01((value - maximum) / feather);
    return std::clamp(lower * upper, 0.0, 1.0);
}

int border_index(int value, int count, data::HeightFieldBorderType border, bool& valid)
{
    valid = true;
    if (border == data::HeightFieldBorderType::Repeat) {
        const int wrapped = count > 0 ? value % count : 0;
        return wrapped < 0 ? wrapped + count : wrapped;
    }
    if (border == data::HeightFieldBorderType::Streak)
        return std::clamp(value, 0, count - 1);
    if (value < 0 || value >= count) {
        valid = false;
        return 0;
    }
    return value;
}

float layer_value_at(const data::PcgHeightFieldLayer& layer,
                     int resolution_x,
                     int resolution_z,
                     int x,
                     int z,
                     int component = 0)
{
    bool valid_x = true;
    bool valid_z = true;
    x = border_index(x, resolution_x, layer.border_type, valid_x);
    z = border_index(z, resolution_z, layer.border_type, valid_z);
    if (!valid_x || !valid_z)
        return layer.border_value;
    const std::size_t index =
        (static_cast<std::size_t>(z) * static_cast<std::size_t>(resolution_x) +
         static_cast<std::size_t>(x)) * static_cast<std::size_t>(layer.tuple_size) +
        static_cast<std::size_t>(component);
    return layer.values[index];
}

std::vector<float> box_blur_scalar(const data::PcgHeightFieldLayer& layer,
                                   int resolution_x,
                                   int resolution_z,
                                   int radius,
                                   int iterations)
{
    std::vector<float> current = layer.values;
    data::PcgHeightFieldLayer working = layer;
    radius = std::clamp(radius, 0, 256);
    for (int iteration = 0; iteration < std::max(1, iterations); ++iteration) {
        working.values = current;
        std::vector<float> horizontal(current.size(), 0.0f);
        for (int z = 0; z < resolution_z; ++z) {
            for (int x = 0; x < resolution_x; ++x) {
                double sum = 0.0;
                int count = 0;
                for (int dx = -radius; dx <= radius; ++dx) {
                    sum += layer_value_at(working, resolution_x, resolution_z, x + dx, z);
                    ++count;
                }
                horizontal[static_cast<std::size_t>(z) *
                               static_cast<std::size_t>(resolution_x) +
                           static_cast<std::size_t>(x)] = static_cast<float>(sum / count);
            }
        }
        working.values = horizontal;
        std::vector<float> vertical(current.size(), 0.0f);
        for (int z = 0; z < resolution_z; ++z) {
            for (int x = 0; x < resolution_x; ++x) {
                double sum = 0.0;
                int count = 0;
                for (int dz = -radius; dz <= radius; ++dz) {
                    sum += layer_value_at(working, resolution_x, resolution_z, x, z + dz);
                    ++count;
                }
                vertical[static_cast<std::size_t>(z) *
                             static_cast<std::size_t>(resolution_x) +
                         static_cast<std::size_t>(x)] = static_cast<float>(sum / count);
            }
        }
        current = std::move(vertical);
    }
    return current;
}

std::vector<float> gaussian_blur_scalar(const data::PcgHeightFieldLayer& layer,
                                        int resolution_x,
                                        int resolution_z,
                                        int radius,
                                        double sigma,
                                        int iterations)
{
    radius = std::clamp(radius, 0, 256);
    sigma = std::max(sigma, 0.5);
    std::vector<double> kernel(static_cast<std::size_t>(radius * 2 + 1), 0.0);
    double kernel_sum = 0.0;
    for (int offset = -radius; offset <= radius; ++offset) {
        const double weight = std::exp(
            -static_cast<double>(offset * offset) / (2.0 * sigma * sigma));
        kernel[static_cast<std::size_t>(offset + radius)] = weight;
        kernel_sum += weight;
    }
    for (double& weight : kernel)
        weight /= kernel_sum;

    std::vector<float> current = layer.values;
    data::PcgHeightFieldLayer working = layer;
    for (int iteration = 0; iteration < std::max(1, iterations); ++iteration) {
        working.values = current;
        std::vector<float> horizontal(current.size(), 0.0f);
        for (int z = 0; z < resolution_z; ++z) {
            for (int x = 0; x < resolution_x; ++x) {
                double sum = 0.0;
                for (int dx = -radius; dx <= radius; ++dx) {
                    sum += layer_value_at(working, resolution_x, resolution_z, x + dx, z) *
                           kernel[static_cast<std::size_t>(dx + radius)];
                }
                horizontal[static_cast<std::size_t>(z) *
                               static_cast<std::size_t>(resolution_x) +
                           static_cast<std::size_t>(x)] = static_cast<float>(sum);
            }
        }
        working.values = horizontal;
        std::vector<float> vertical(current.size(), 0.0f);
        for (int z = 0; z < resolution_z; ++z) {
            for (int x = 0; x < resolution_x; ++x) {
                double sum = 0.0;
                for (int dz = -radius; dz <= radius; ++dz) {
                    sum += layer_value_at(working, resolution_x, resolution_z, x, z + dz) *
                           kernel[static_cast<std::size_t>(dz + radius)];
                }
                vertical[static_cast<std::size_t>(z) *
                             static_cast<std::size_t>(resolution_x) +
                         static_cast<std::size_t>(x)] = static_cast<float>(sum);
            }
        }
        current = std::move(vertical);
    }
    return current;
}

void build_edge_layer(const std::vector<float>& region,
                      int resolution_x,
                      int resolution_z,
                      int radius,
                      data::PcgHeightFieldLayer& edge_layer)
{
    edge_layer.values.assign(region.size(), 0.0f);
    radius = std::max(1, radius);
    for (int z = 0; z < resolution_z; ++z) {
        for (int x = 0; x < resolution_x; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(resolution_x) +
                                      static_cast<std::size_t>(x);
            double edge = 0.0;
            for (int dz = -radius; dz <= radius; ++dz) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int nx = std::clamp(x + dx, 0, resolution_x - 1);
                    const int nz = std::clamp(z + dz, 0, resolution_z - 1);
                    const std::size_t neighbor = static_cast<std::size_t>(nz) *
                                                 static_cast<std::size_t>(resolution_x) +
                                                 static_cast<std::size_t>(nx);
                    edge = std::max(edge, std::abs(
                        static_cast<double>(region[index]) - region[neighbor]));
                }
            }
            edge_layer.values[index] = static_cast<float>(std::clamp(edge, 0.0, 1.0));
        }
    }
}

double layer_mode_value(double base_value,
                        double layer_value,
                        HeightFieldLayerMode mode,
                        double blend)
{
    switch (mode) {
    case HeightFieldLayerMode::Add:
        return base_value + layer_value;
    case HeightFieldLayerMode::Subtract:
        return base_value - layer_value;
    case HeightFieldLayerMode::Multiply:
        return base_value * layer_value;
    case HeightFieldLayerMode::Maximum:
        return std::max(base_value, layer_value);
    case HeightFieldLayerMode::Minimum:
        return std::min(base_value, layer_value);
    case HeightFieldLayerMode::Blend:
        return base_value + (layer_value - base_value) * std::clamp(blend, 0.0, 1.0);
    case HeightFieldLayerMode::Replace:
    default:
        return layer_value;
    }
}

int resolution_for_extent(double extent,
                          double spacing,
                          data::HeightFieldSampling sampling)
{
    if (!std::isfinite(extent) || !std::isfinite(spacing) || extent <= 0.0 || spacing <= 0.0)
        return 0;
    const double intervals = extent / spacing;
    const int samples = static_cast<int>(std::llround(intervals)) +
        (sampling == data::HeightFieldSampling::Corner ? 1 : 0);
    return std::clamp(samples, 2, kMaxGridSamples);
}

} // namespace

bool resolve_heightfield_resolution(const HeightFieldCreateOptions& options,
                                    int& out_resolution_x,
                                    int& out_resolution_z)
{
    out_resolution_x = 0;
    out_resolution_z = 0;
    if (!std::isfinite(options.size_x) || !std::isfinite(options.size_z) ||
        options.size_x <= 0.0 || options.size_z <= 0.0)
        return false;

    if (options.division_mode == HeightFieldDivisionMode::BySize) {
        out_resolution_x = resolution_for_extent(options.size_x, options.grid_spacing, options.sampling);
        out_resolution_z = resolution_for_extent(options.size_z, options.grid_spacing, options.sampling);
    } else {
        const int longest_samples = std::clamp(options.grid_samples, 2, kMaxGridSamples);
        const double longest_extent = std::max(options.size_x, options.size_z);
        const int longest_intervals = options.sampling == data::HeightFieldSampling::Corner
            ? longest_samples - 1
            : longest_samples;
        if (longest_intervals <= 0)
            return false;
        const double spacing = longest_extent / static_cast<double>(longest_intervals);
        out_resolution_x = resolution_for_extent(options.size_x, spacing, options.sampling);
        out_resolution_z = resolution_for_extent(options.size_z, spacing, options.sampling);
    }

    if (out_resolution_x < 2 || out_resolution_z < 2)
        return false;
    const std::size_t total = static_cast<std::size_t>(out_resolution_x) *
                              static_cast<std::size_t>(out_resolution_z);
    return total <= kMaxHeightFieldSamples;
}

data::PcgHeightField create_heightfield(const HeightFieldCreateOptions& options)
{
    int resolution_x = 0;
    int resolution_z = 0;
    if (!resolve_heightfield_resolution(options, resolution_x, resolution_z))
        return {};

    data::PcgHeightField result(resolution_x,
                                resolution_z,
                                options.size_x,
                                options.size_z,
                                options.center,
                                options.sampling,
                                options.orientation);
    result.create_layer("height", 1, options.initial_height);
    result.create_layer("mask", 1, options.initial_mask);
    return result;
}

bool apply_heightfield_noise(data::PcgHeightField& heightfield,
                             const data::PcgHeightField* mask,
                             const HeightFieldNoiseOptions& options)
{
    data::PcgHeightFieldLayer* layer = heightfield.find_layer_mut(options.noise_layer);
    if (!heightfield.valid() || !layer || layer->tuple_size != 1 ||
        !layer->valid_for(heightfield.resolution_x(), heightfield.resolution_z()) ||
        options.noise_type != "perlin" ||
        !std::isfinite(options.element_size) || options.element_size <= 0.0 ||
        !std::isfinite(options.amplitude)) {
        return false;
    }

    for (int z = 0; z < heightfield.resolution_z(); ++z) {
        for (int x = 0; x < heightfield.resolution_x(); ++x) {
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            const double noise = noise_at_position(heightfield, position, options);

            double mask_weight = 1.0;
            if (!external_mask_weight(mask, options.mask_layer, position, mask_weight))
                return false;

            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(heightfield.resolution_x()) +
                                      static_cast<std::size_t>(x);
            layer->values[index] += static_cast<float>(noise * options.amplitude * mask_weight);
        }
    }
    return true;
}

bool apply_heightfield_mask_noise(data::PcgHeightField& heightfield,
                                  const data::PcgHeightField* mask,
                                  const HeightFieldMaskNoiseOptions& options)
{
    if (!heightfield.valid() || options.output_layer.empty() ||
        options.noise.noise_type != "perlin" ||
        !std::isfinite(options.noise.element_size) || options.noise.element_size <= 0.0 ||
        !std::isfinite(options.noise.amplitude)) {
        return false;
    }

    data::PcgHeightFieldLayer* output = heightfield.find_layer_mut(options.output_layer);
    if (!output)
        output = &heightfield.create_layer(options.output_layer, 1, 0.0f);
    if (output->tuple_size != 1)
        return false;

    for (int z = 0; z < heightfield.resolution_z(); ++z) {
        for (int x = 0; x < heightfield.resolution_x(); ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(heightfield.resolution_x()) +
                                      static_cast<std::size_t>(x);
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            double generated = noise_at_position(heightfield, position, options.noise) *
                               options.noise.amplitude;
            if (options.invert)
                generated = 1.0 - generated;
            const double combined = combine_value(
                output->values[index], generated, options.combine, options.blend);
            double weight = 1.0;
            if (!external_mask_weight(mask, options.noise.mask_layer, position, weight))
                return false;
            output->values[index] = static_cast<float>(
                output->values[index] + (combined - output->values[index]) * weight);
        }
    }
    return true;
}

bool apply_heightfield_mask_by_feature(data::PcgHeightField& heightfield,
                                       const data::PcgHeightField* mask,
                                       const HeightFieldMaskByFeatureOptions& options)
{
    const data::PcgHeightFieldLayer* heights = heightfield.find_layer(options.height_layer);
    if (!heightfield.valid() || options.output_layer.empty() ||
        !heights || heights->tuple_size != 1 ||
        options.min_height > options.max_height ||
        options.min_slope_degrees > options.max_slope_degrees) {
        return false;
    }

    data::PcgHeightFieldLayer* output = heightfield.find_layer_mut(options.output_layer);
    if (!output)
        output = &heightfield.create_layer(options.output_layer, 1, 0.0f);
    if (output->tuple_size != 1)
        return false;

    std::vector<float> generated(heightfield.sample_count(), 1.0f);
    const double spacing_x = heightfield.spacing_x();
    const double spacing_z = heightfield.spacing_z();
    for (int z = 0; z < heightfield.resolution_z(); ++z) {
        for (int x = 0; x < heightfield.resolution_x(); ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(heightfield.resolution_x()) +
                                      static_cast<std::size_t>(x);
            double value = 1.0;
            if (options.mask_by_height) {
                value *= range_weight(heights->values[index],
                                      options.min_height,
                                      options.max_height,
                                      std::max(0.0, options.height_feather));
            }
            if (options.mask_by_slope) {
                const double dx = (layer_value_at(*heights,
                                                  heightfield.resolution_x(),
                                                  heightfield.resolution_z(),
                                                  x + 1,
                                                  z) -
                                   layer_value_at(*heights,
                                                  heightfield.resolution_x(),
                                                  heightfield.resolution_z(),
                                                  x - 1,
                                                  z)) /
                                  (2.0 * spacing_x);
                const double dz = (layer_value_at(*heights,
                                                  heightfield.resolution_x(),
                                                  heightfield.resolution_z(),
                                                  x,
                                                  z + 1) -
                                   layer_value_at(*heights,
                                                  heightfield.resolution_x(),
                                                  heightfield.resolution_z(),
                                                  x,
                                                  z - 1)) /
                                  (2.0 * spacing_z);
                constexpr double kRadiansToDegrees = 57.2957795130823208768;
                const double slope = std::atan(std::sqrt(dx * dx + dz * dz)) *
                                     kRadiansToDegrees;
                value *= range_weight(slope,
                                      options.min_slope_degrees,
                                      options.max_slope_degrees,
                                      std::max(0.0, options.slope_feather_degrees));
            }
            generated[index] = static_cast<float>(options.invert ? 1.0 - value : value);
        }
    }

    if (options.smooth_radius_samples > 0) {
        data::PcgHeightFieldLayer temporary = *output;
        temporary.values = generated;
        generated = box_blur_scalar(temporary,
                                    heightfield.resolution_x(),
                                    heightfield.resolution_z(),
                                    options.smooth_radius_samples,
                                    1);
    }

    for (int z = 0; z < heightfield.resolution_z(); ++z) {
        for (int x = 0; x < heightfield.resolution_x(); ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(heightfield.resolution_x()) +
                                      static_cast<std::size_t>(x);
            const double combined = combine_value(
                output->values[index], generated[index], options.combine, options.blend);
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            double weight = 1.0;
            if (!external_mask_weight(mask, options.mask_layer, position, weight))
                return false;
            output->values[index] = static_cast<float>(
                output->values[index] + (combined - output->values[index]) * weight);
        }
    }
    return true;
}

bool apply_heightfield_clip(data::PcgHeightField& heightfield,
                            const data::PcgHeightField* mask,
                            const HeightFieldClipOptions& options)
{
    data::PcgHeightFieldLayer* heights = heightfield.find_layer_mut(options.height_layer);
    if (!heightfield.valid() || !heights || heights->tuple_size != 1 ||
        options.output_clipped_layer.empty() || options.output_edge_layer.empty() ||
        options.output_clipped_layer == options.height_layer ||
        options.output_edge_layer == options.height_layer ||
        options.output_clipped_layer == options.output_edge_layer ||
        options.edge_radius_samples < 1 || options.edge_radius_samples > 64 ||
        (!options.min_clip_enabled && !options.max_clip_enabled) ||
        (options.min_clip_enabled && options.max_clip_enabled &&
         options.min_clip > options.max_clip)) {
        return false;
    }

    auto& clipped_layer = heightfield.create_layer(options.output_clipped_layer, 1, 0.0f);
    std::vector<float> clipped(heightfield.sample_count(), 0.0f);
    for (int z = 0; z < heightfield.resolution_z(); ++z) {
        for (int x = 0; x < heightfield.resolution_x(); ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(heightfield.resolution_x()) +
                                      static_cast<std::size_t>(x);
            const double original = heights->values[index];
            double candidate = original;
            if (options.min_clip_enabled)
                candidate = std::max(candidate, options.min_clip);
            if (options.max_clip_enabled)
                candidate = std::min(candidate, options.max_clip);
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            double weight = 1.0;
            if (!external_mask_weight(mask, options.mask_layer, position, weight))
                return false;
            const double result = original + (candidate - original) * weight;
            heights->values[index] = static_cast<float>(result);
            clipped[index] = std::abs(result - original) > 1e-6 ? static_cast<float>(weight) : 0.0f;
        }
    }
    clipped_layer.values = clipped;
    auto& edge_layer = heightfield.create_layer(options.output_edge_layer, 1, 0.0f);
    build_edge_layer(clipped,
                     heightfield.resolution_x(),
                     heightfield.resolution_z(),
                     options.edge_radius_samples,
                     edge_layer);
    if (options.generate_mask_from == "clipped")
        heightfield.find_layer_mut("mask")->values = clipped_layer.values;
    else if (options.generate_mask_from == "edge")
        heightfield.find_layer_mut("mask")->values = edge_layer.values;
    else if (options.generate_mask_from != "none")
        return false;
    return true;
}

bool apply_heightfield_terrace(data::PcgHeightField& heightfield,
                               const data::PcgHeightField* mask,
                               const HeightFieldTerraceOptions& options)
{
    data::PcgHeightFieldLayer* heights = heightfield.find_layer_mut(options.height_layer);
    if (!heightfield.valid() || !heights || heights->tuple_size != 1 ||
        options.output_mesa_layer.empty() || options.output_cliff_layer.empty() ||
        options.output_mesa_layer == options.height_layer ||
        options.output_cliff_layer == options.height_layer ||
        options.output_mesa_layer == options.output_cliff_layer ||
        !std::isfinite(options.max_step_size) || options.max_step_size <= 0.0 ||
        options.min_height > options.max_height) {
        return false;
    }

    std::vector<float> mesa(heightfield.sample_count(), 0.0f);
    std::vector<float> cliff_source(heightfield.sample_count(), 0.0f);
    const double fade = std::clamp(options.fade, 0.0, 1.0);
    const double smooth = std::clamp(options.smooth_edges, 0.0, 0.49);
    for (int z = 0; z < heightfield.resolution_z(); ++z) {
        for (int x = 0; x < heightfield.resolution_x(); ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(heightfield.resolution_x()) +
                                      static_cast<std::size_t>(x);
            const double original = heights->values[index];
            if (original < options.min_height || original > options.max_height)
                continue;
            const double level_coordinate =
                (original - options.step_offset) / options.max_step_size;
            const double level_index = std::floor(level_coordinate);
            const double fraction = level_coordinate - level_index;
            const double transition = smooth <= 0.0
                ? 0.0
                : smoothstep01((fraction - (1.0 - smooth)) / smooth);
            const double terraced = options.step_offset +
                (level_index + transition) * options.max_step_size;
            const double candidate = terraced + (original - terraced) * fade;
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            double weight = 1.0;
            if (!external_mask_weight(mask, options.mask_layer, position, weight))
                return false;
            const double result = original + (candidate - original) * weight;
            heights->values[index] = static_cast<float>(result);
            mesa[index] = std::abs(result - original) > 1e-6 ? static_cast<float>(weight) : 0.0f;
            cliff_source[index] = static_cast<float>(result / options.max_step_size);
        }
    }
    auto& mesa_layer = heightfield.create_layer(options.output_mesa_layer, 1, 0.0f);
    mesa_layer.values = mesa;
    auto& cliff_layer = heightfield.create_layer(options.output_cliff_layer, 1, 0.0f);
    build_edge_layer(cliff_source,
                     heightfield.resolution_x(),
                     heightfield.resolution_z(),
                     1,
                     cliff_layer);
    return true;
}

bool apply_heightfield_blur(data::PcgHeightField& heightfield,
                            const data::PcgHeightField* mask,
                            const HeightFieldBlurOptions& options)
{
    data::PcgHeightFieldLayer* layer = heightfield.find_layer_mut(options.blur_layer);
    if (!heightfield.valid() || !layer || layer->tuple_size != 1 ||
        options.iterations < 1 || !std::isfinite(options.radius_meters) ||
        options.radius_meters < 0.0) {
        return false;
    }
    const double spacing = std::min(heightfield.spacing_x(), heightfield.spacing_z());
    const double radius_samples = options.radius_meters / spacing;
    if (radius_samples > 256.0)
        return false;
    const int radius = static_cast<int>(std::ceil(radius_samples));
    if (radius == 0)
        return true;
    const std::vector<float> original = layer->values;
    auto filter = [&](const data::PcgHeightFieldLayer& source) {
        return options.method == HeightFieldBlurMethod::Gaussian
            ? gaussian_blur_scalar(source,
                                   heightfield.resolution_x(),
                                   heightfield.resolution_z(),
                                   radius,
                                   std::max(0.5, radius_samples * 0.5),
                                   options.iterations)
            : box_blur_scalar(source,
                              heightfield.resolution_x(),
                              heightfield.resolution_z(),
                              radius,
                              options.iterations);
    };
    std::vector<float> mask_weights(heightfield.sample_count(), 1.0f);
    for (int z = 0; z < heightfield.resolution_z(); ++z) {
        for (int x = 0; x < heightfield.resolution_x(); ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(heightfield.resolution_x()) +
                                      static_cast<std::size_t>(x);
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            double weight = 1.0;
            if (!external_mask_weight(mask, options.mask_layer, position, weight))
                return false;
            mask_weights[index] = static_cast<float>(weight);
        }
    }

    std::vector<float> blurred;
    if (mask && options.mask_aware) {
        data::PcgHeightFieldLayer weighted_values = *layer;
        data::PcgHeightFieldLayer weighted_mask = *layer;
        weighted_mask.values = mask_weights;
        for (std::size_t i = 0; i < weighted_values.values.size(); ++i)
            weighted_values.values[i] *= mask_weights[i];
        const std::vector<float> numerator = filter(weighted_values);
        const std::vector<float> denominator = filter(weighted_mask);
        blurred.resize(original.size());
        for (std::size_t i = 0; i < blurred.size(); ++i) {
            blurred[i] = denominator[i] > 1e-6f
                ? numerator[i] / denominator[i]
                : original[i];
        }
    } else {
        blurred = filter(*layer);
    }

    const double sub_sample_strength = std::min(1.0, radius_samples);
    for (std::size_t index = 0; index < original.size(); ++index) {
        const double weight = mask_weights[index] * sub_sample_strength;
        layer->values[index] = static_cast<float>(
            original[index] + (blurred[index] - original[index]) * weight);
    }
    return true;
}

data::PcgHeightField resample_heightfield(
    const data::PcgHeightField& heightfield,
    const HeightFieldResampleOptions& options)
{
    if (!heightfield.valid())
        return {};

    int resolution_x = 0;
    int resolution_z = 0;
    if (options.specify_exact_resolution) {
        HeightFieldCreateOptions create;
        create.size_x = heightfield.size_x();
        create.size_z = heightfield.size_z();
        create.sampling = heightfield.sampling();
        create.division_mode = options.division_mode;
        create.grid_samples = options.grid_samples;
        create.grid_spacing = options.grid_spacing;
        if (!resolve_heightfield_resolution(create, resolution_x, resolution_z))
            return {};
    } else {
        if (!std::isfinite(options.resolution_scale) || options.resolution_scale <= 0.0)
            return {};
        const bool corner = heightfield.sampling() == data::HeightFieldSampling::Corner;
        const int source_intervals_x = heightfield.resolution_x() - (corner ? 1 : 0);
        const int source_intervals_z = heightfield.resolution_z() - (corner ? 1 : 0);
        resolution_x = std::clamp(
            static_cast<int>(std::llround(source_intervals_x * options.resolution_scale)) +
                (corner ? 1 : 0),
            2,
            kMaxGridSamples);
        resolution_z = std::clamp(
            static_cast<int>(std::llround(source_intervals_z * options.resolution_scale)) +
                (corner ? 1 : 0),
            2,
            kMaxGridSamples);
        if (static_cast<std::size_t>(resolution_x) * static_cast<std::size_t>(resolution_z) >
            kMaxHeightFieldSamples) {
            return {};
        }
    }

    data::PcgHeightField result(resolution_x,
                                resolution_z,
                                heightfield.size_x(),
                                heightfield.size_z(),
                                heightfield.center(),
                                heightfield.sampling(),
                                heightfield.orientation());
    for (const auto& [name, source] : heightfield.layers()) {
        auto& destination = result.create_layer(name, source.tuple_size, source.border_value);
        destination.border_type = source.border_type;
        destination.border_value = source.border_value;
        for (int z = 0; z < resolution_z; ++z) {
            for (int x = 0; x < resolution_x; ++x) {
                const data::PcgVec3 position = result.sample_position(x, z, 0.0);
                for (int component = 0; component < source.tuple_size; ++component) {
                    double value = 0.0;
                    if (!heightfield.sample_component_world(
                            name, component, position.x, position.y, position.z, value)) {
                        return {};
                    }
                    const std::size_t index =
                        (static_cast<std::size_t>(z) * static_cast<std::size_t>(resolution_x) +
                         static_cast<std::size_t>(x)) *
                            static_cast<std::size_t>(source.tuple_size) +
                        static_cast<std::size_t>(component);
                    destination.values[index] = static_cast<float>(value);
                }
            }
        }
    }
    return result.valid() ? result : data::PcgHeightField{};
}

data::PcgHeightField composite_heightfields(
    const data::PcgHeightField& base,
    const data::PcgHeightField& layer,
    const data::PcgHeightField* mask,
    const HeightFieldLayerOptions& options)
{
    if (!base.valid() || !layer.valid() ||
        !std::isfinite(options.mask_strength) ||
        !std::isfinite(options.base_offset) || !std::isfinite(options.base_scale) ||
        !std::isfinite(options.layer_offset) || !std::isfinite(options.layer_scale) ||
        !std::isfinite(options.final_offset) || !std::isfinite(options.final_scale) ||
        (options.clamp_minimum && options.clamp_maximum &&
         options.minimum > options.maximum))
        return {};
    std::set<std::string> selected;
    bool all_layers = options.layers == "*";
    if (!all_layers) {
        std::istringstream stream(options.layers);
        for (std::string name; stream >> name;)
            selected.insert(std::move(name));
        if (selected.empty())
            return {};
    }

    data::PcgHeightField result = base;
    for (const auto& [name, source] : layer.layers()) {
        if (!all_layers && selected.find(name) == selected.end())
            continue;
        data::PcgHeightFieldLayer* destination = result.find_layer_mut(name);
        if (!destination) {
            destination = &result.create_layer(name, source.tuple_size, 0.0f);
            destination->border_type = source.border_type;
            destination->border_value = source.border_value;
        }
        if (destination->tuple_size != source.tuple_size)
            return {};

        for (int z = 0; z < result.resolution_z(); ++z) {
            for (int x = 0; x < result.resolution_x(); ++x) {
                const data::PcgVec3 position = result.sample_position(x, z, 0.0);
                double mask_weight = 1.0;
                if (!external_mask_weight(mask, options.mask_layer, position, mask_weight))
                    return {};
                mask_weight = std::clamp(mask_weight * options.mask_strength, 0.0, 1.0);
                if (options.invert_mask)
                    mask_weight = 1.0 - mask_weight;
                for (int component = 0; component < source.tuple_size; ++component) {
                    double source_value = 0.0;
                    if (!layer.sample_component_world(
                            name, component, position.x, position.y, position.z, source_value)) {
                        return {};
                    }
                    const std::size_t index =
                        (static_cast<std::size_t>(z) *
                             static_cast<std::size_t>(result.resolution_x()) +
                         static_cast<std::size_t>(x)) *
                            static_cast<std::size_t>(source.tuple_size) +
                        static_cast<std::size_t>(component);
                    const double original = destination->values[index];
                    const double mapped_base = original * options.base_scale + options.base_offset;
                    const double mapped_layer =
                        source_value * options.layer_scale + options.layer_offset;
                    double candidate = layer_mode_value(
                        mapped_base, mapped_layer, options.mode, options.blend);
                    candidate = candidate * options.final_scale + options.final_offset;
                    if (options.clamp_minimum)
                        candidate = std::max(candidate, options.minimum);
                    if (options.clamp_maximum)
                        candidate = std::min(candidate, options.maximum);
                    destination->values[index] = static_cast<float>(
                        original + (candidate - original) * mask_weight);
                }
            }
        }
    }
    return result.valid() ? result : data::PcgHeightField{};
}

bool apply_heightfield_erode(data::PcgHeightField& heightfield,
                             const data::PcgHeightField* mask,
                             const HeightFieldErodeOptions& options)
{
    data::PcgHeightFieldLayer* height_layer = heightfield.find_layer_mut(options.height_layer);
    const std::set<std::string> output_names = {
        options.debris_layer,
        options.sediment_layer,
        options.flow_layer,
        options.flow_direction_layer,
    };
    if (!heightfield.valid() || !height_layer || height_layer->tuple_size != 1 ||
        output_names.size() != 4 || output_names.count("") != 0 ||
        output_names.count(options.height_layer) != 0 ||
        options.iterations < 1 || options.iterations > 500 ||
        !std::isfinite(options.erodability) || options.erodability < 0.0 ||
        !std::isfinite(options.rainfall) || options.rainfall < 0.0 ||
        !std::isfinite(options.flow_force) || options.flow_force < 0.0 ||
        !std::isfinite(options.erosion_rate) || options.erosion_rate < 0.0 ||
        !std::isfinite(options.deposition_rate) || options.deposition_rate < 0.0 ||
        !std::isfinite(options.sediment_capacity) || options.sediment_capacity < 0.0 ||
        !std::isfinite(options.evaporation_rate) || options.evaporation_rate < 0.0 ||
        options.evaporation_rate > 1.0 ||
        !std::isfinite(options.weathering_force) || options.weathering_force < 0.0 ||
        options.cut_angle_degrees < 0.0 || options.cut_angle_degrees >= 90.0 ||
        options.repose_angle_degrees < 0.0 || options.repose_angle_degrees >= 90.0) {
        return false;
    }

    const int resolution_x = heightfield.resolution_x();
    const int resolution_z = heightfield.resolution_z();
    const std::size_t count = heightfield.sample_count();
    std::vector<double> heights(height_layer->values.begin(), height_layer->values.end());
    std::vector<double> water(count, 0.0);
    std::vector<double> carried(count, 0.0);
    std::vector<double> debris(count, 0.0);
    std::vector<double> sediment(count, 0.0);
    std::vector<double> flow(count, 0.0);
    std::vector<double> flow_direction_x(count, 0.0);
    std::vector<double> flow_direction_z(count, 0.0);
    std::vector<double> mask_weights(count, 1.0);
    for (int z = 0; z < resolution_z; ++z) {
        for (int x = 0; x < resolution_x; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(resolution_x) +
                                      static_cast<std::size_t>(x);
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            if (!external_mask_weight(mask, options.mask_layer, position, mask_weights[index]))
                return false;
        }
    }

    static constexpr int kDirections[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };
    constexpr double kDegreesToRadians = 0.0174532925199432957692;
    const double cut_slope = std::tan(options.cut_angle_degrees * kDegreesToRadians);
    const double repose_slope = std::tan(options.repose_angle_degrees * kDegreesToRadians);
    const double flow_fraction = std::clamp(options.flow_force * 0.45, 0.0, 0.95);
    const double erosion_rate = std::clamp(options.erosion_rate, 0.0, 1.0);
    const double deposition_rate = std::clamp(options.deposition_rate, 0.0, 1.0);
    const double weathering = std::clamp(options.weathering_force, 0.0, 1.0);

    for (int iteration = 0; iteration < options.iterations; ++iteration) {
        if (flow_fraction > 0.0 && options.rainfall > 0.0) {
            for (int z = 0; z < resolution_z; ++z) {
                for (int x = 0; x < resolution_x; ++x) {
                    const std::size_t index = static_cast<std::size_t>(z) *
                                              static_cast<std::size_t>(resolution_x) +
                                              static_cast<std::size_t>(x);
                    const double jitter = 0.75 +
                        static_cast<double>(hash_lattice(x, z, options.seed + iteration * 7919)) /
                            static_cast<double>(std::numeric_limits<uint32_t>::max()) * 0.5;
                    water[index] += options.rainfall * jitter * mask_weights[index];
                }
            }

            std::vector<double> next_water(count, 0.0);
            std::vector<double> next_carried(count, 0.0);
            std::vector<double> height_delta(count, 0.0);
            for (int z = 0; z < resolution_z; ++z) {
                for (int x = 0; x < resolution_x; ++x) {
                    const std::size_t index = static_cast<std::size_t>(z) *
                                              static_cast<std::size_t>(resolution_x) +
                                              static_cast<std::size_t>(x);
                    int best_x = x;
                    int best_z = z;
                    double best_drop = 0.0;
                    double best_distance = 1.0;
                    const double surface = heights[index] + water[index];
                    for (const auto& direction : kDirections) {
                        const int nx = x + direction[0];
                        const int nz = z + direction[1];
                        if (nx < 0 || nx >= resolution_x || nz < 0 || nz >= resolution_z)
                            continue;
                        const std::size_t neighbor = static_cast<std::size_t>(nz) *
                                                     static_cast<std::size_t>(resolution_x) +
                                                     static_cast<std::size_t>(nx);
                        const double distance = std::hypot(
                            direction[0] * heightfield.spacing_x(),
                            direction[1] * heightfield.spacing_z());
                        const double drop = surface - (heights[neighbor] + water[neighbor]);
                        if (drop / distance > best_drop / best_distance) {
                            best_drop = drop;
                            best_distance = distance;
                            best_x = nx;
                            best_z = nz;
                        }
                    }

                    double local_carried = carried[index];
                    if (best_drop > 0.0 && (best_x != x || best_z != z)) {
                        const std::size_t neighbor = static_cast<std::size_t>(best_z) *
                                                     static_cast<std::size_t>(resolution_x) +
                                                     static_cast<std::size_t>(best_x);
                        const double moved_water = water[index] * flow_fraction;
                        const double speed = best_drop / best_distance;
                        const double capacity = moved_water * speed *
                                                options.sediment_capacity;
                        if (local_carried < capacity) {
                            const double eroded = std::min(
                                (capacity - local_carried) * erosion_rate *
                                    options.erodability * mask_weights[index],
                                std::max(0.0, best_drop * 0.25));
                            height_delta[index] -= eroded;
                            local_carried += eroded;
                        } else {
                            const double deposited = std::min(
                                (local_carried - capacity) * deposition_rate,
                                local_carried);
                            local_carried -= deposited;
                            sediment[index] += deposited;
                            if (options.add_sediment_to_height)
                                height_delta[index] += deposited;
                        }

                        const double carried_fraction = water[index] > 1e-9
                            ? std::clamp(moved_water / water[index], 0.0, 1.0)
                            : 0.0;
                        const double moved_carried = local_carried * carried_fraction;
                        next_water[neighbor] += moved_water * (1.0 - options.evaporation_rate);
                        next_water[index] += (water[index] - moved_water) *
                                             (1.0 - options.evaporation_rate);
                        next_carried[neighbor] += moved_carried;
                        next_carried[index] += local_carried - moved_carried;
                        flow[index] += moved_water;
                        const double direction_length = std::hypot(
                            static_cast<double>(best_x - x),
                            static_cast<double>(best_z - z));
                        flow_direction_x[index] +=
                            static_cast<double>(best_x - x) / direction_length * moved_water;
                        flow_direction_z[index] +=
                            static_cast<double>(best_z - z) / direction_length * moved_water;
                    } else {
                        const double deposited = local_carried * deposition_rate;
                        local_carried -= deposited;
                        sediment[index] += deposited;
                        if (options.add_sediment_to_height)
                            height_delta[index] += deposited;
                        next_water[index] += water[index] * (1.0 - options.evaporation_rate);
                        next_carried[index] += local_carried;
                    }
                }
            }
            for (std::size_t i = 0; i < count; ++i)
                heights[i] += height_delta[i];
            water = std::move(next_water);
            carried = std::move(next_carried);
        }

        if (weathering > 0.0) {
            std::vector<double> height_delta(count, 0.0);
            std::vector<double> debris_delta(count, 0.0);
            for (int z = 0; z < resolution_z; ++z) {
                for (int x = 0; x < resolution_x; ++x) {
                    const std::size_t index = static_cast<std::size_t>(z) *
                                              static_cast<std::size_t>(resolution_x) +
                                              static_cast<std::size_t>(x);
                    int best_x = x;
                    int best_z = z;
                    double best_drop = 0.0;
                    double best_distance = 1.0;
                    for (const auto& direction : kDirections) {
                        const int nx = x + direction[0];
                        const int nz = z + direction[1];
                        if (nx < 0 || nx >= resolution_x || nz < 0 || nz >= resolution_z)
                            continue;
                        const std::size_t neighbor = static_cast<std::size_t>(nz) *
                                                     static_cast<std::size_t>(resolution_x) +
                                                     static_cast<std::size_t>(nx);
                        const double distance = std::hypot(
                            direction[0] * heightfield.spacing_x(),
                            direction[1] * heightfield.spacing_z());
                        const double drop = heights[index] - heights[neighbor];
                        if (drop / distance > best_drop / best_distance) {
                            best_drop = drop;
                            best_distance = distance;
                            best_x = nx;
                            best_z = nz;
                        }
                    }
                    if (best_x == x && best_z == z)
                        continue;
                    const std::size_t neighbor = static_cast<std::size_t>(best_z) *
                                                 static_cast<std::size_t>(resolution_x) +
                                                 static_cast<std::size_t>(best_x);
                    const double cut_excess = best_drop - cut_slope * best_distance;
                    if (cut_excess > 0.0) {
                        const double amount = cut_excess * weathering * 0.25 *
                                              options.erodability * mask_weights[index];
                        height_delta[index] -= amount;
                        debris_delta[neighbor] += amount;
                        if (options.add_debris_to_height)
                            height_delta[neighbor] += amount;
                    }
                    const double repose_excess = best_drop - repose_slope * best_distance;
                    if (repose_excess > 0.0 && debris[index] > 0.0) {
                        const double sliding = std::min(
                            debris[index], repose_excess * weathering * 0.25);
                        debris_delta[index] -= sliding;
                        debris_delta[neighbor] += sliding;
                        if (options.add_debris_to_height) {
                            height_delta[index] -= sliding;
                            height_delta[neighbor] += sliding;
                        }
                    }
                }
            }
            for (std::size_t i = 0; i < count; ++i) {
                heights[i] += height_delta[i];
                debris[i] = std::max(0.0, debris[i] + debris_delta[i]);
            }
        }
    }

    for (std::size_t i = 0; i < count; ++i)
        height_layer->values[i] = static_cast<float>(heights[i]);
    auto& debris_output = heightfield.create_layer(options.debris_layer, 1, 0.0f);
    auto& sediment_output = heightfield.create_layer(options.sediment_layer, 1, 0.0f);
    auto& flow_output = heightfield.create_layer(options.flow_layer, 1, 0.0f);
    auto& direction_output = heightfield.create_layer(options.flow_direction_layer, 2, 0.0f);
    for (std::size_t i = 0; i < count; ++i) {
        debris_output.values[i] = static_cast<float>(debris[i]);
        sediment_output.values[i] = static_cast<float>(sediment[i]);
        flow_output.values[i] = static_cast<float>(flow[i]);
        const double direction_length = std::hypot(flow_direction_x[i], flow_direction_z[i]);
        if (direction_length > 1e-9) {
            direction_output.values[i * 2] =
                static_cast<float>(flow_direction_x[i] / direction_length);
            direction_output.values[i * 2 + 1] =
                static_cast<float>(flow_direction_z[i] / direction_length);
        }
    }
    return heightfield.valid();
}

bool apply_heightfield_distort_by_noise(
    data::PcgHeightField& heightfield,
    const data::PcgHeightField* mask,
    const HeightFieldDistortByNoiseOptions& options)
{
    if (!heightfield.valid() ||
        (options.noise_type != "simplex" && options.noise_type != "curl") ||
        !std::isfinite(options.amplitude) ||
        !std::isfinite(options.element_size) || options.element_size <= 0.0 ||
        !std::isfinite(options.scale_x) || std::abs(options.scale_x) <= 1e-9 ||
        !std::isfinite(options.scale_z) || std::abs(options.scale_z) <= 1e-9 ||
        !std::isfinite(options.offset_x) || !std::isfinite(options.offset_z) ||
        !std::isfinite(options.roughness) || options.roughness < 0.0 ||
        options.roughness > 1.0 || options.max_octaves < 1 ||
        options.max_octaves > 12 || options.substeps < 1 || options.substeps > 64) {
        return false;
    }

    std::set<std::string> selected;
    const bool all_layers = options.distort_layers == "*";
    if (!all_layers) {
        std::istringstream stream(options.distort_layers);
        for (std::string name; stream >> name;)
            selected.insert(std::move(name));
        if (selected.empty())
            return false;
    }
    for (const std::string& name : selected) {
        if (!heightfield.find_layer(name))
            return false;
    }

    HeightFieldNoiseOptions noise;
    noise.fractal = HeightFieldFractalMode::Standard;
    noise.center_noise = true;
    noise.element_size = options.element_size;
    noise.scale_x = options.scale_x;
    noise.scale_z = options.scale_z;
    noise.offset_x = options.offset_x;
    noise.offset_z = options.offset_z;
    noise.max_octaves = options.max_octaves;
    noise.roughness = options.roughness;
    noise.lacunarity = 2.0;

    auto offset_on_plane = [&](data::PcgVec3 position, double du, double dv) {
        if (heightfield.orientation() == data::HeightFieldOrientation::XY) {
            position.x += du;
            position.y += dv;
        } else if (heightfield.orientation() == data::HeightFieldOrientation::YZ) {
            position.y += du;
            position.z += dv;
        } else {
            position.x += du;
            position.z += dv;
        }
        return position;
    };

    auto vector_at = [&](const data::PcgVec3& position, double& out_u, double& out_v) {
        if (options.noise_type == "curl") {
            const double epsilon = std::max(options.element_size * 0.01, 1e-4);
            noise.seed = options.seed;
            const double u_plus = noise_at_position(
                heightfield, offset_on_plane(position, epsilon, 0.0), noise);
            const double u_minus = noise_at_position(
                heightfield, offset_on_plane(position, -epsilon, 0.0), noise);
            const double v_plus = noise_at_position(
                heightfield, offset_on_plane(position, 0.0, epsilon), noise);
            const double v_minus = noise_at_position(
                heightfield, offset_on_plane(position, 0.0, -epsilon), noise);
            const double gradient_u = (u_plus - u_minus) / (2.0 * epsilon);
            const double gradient_v = (v_plus - v_minus) / (2.0 * epsilon);
            out_u = gradient_v * options.element_size;
            out_v = -gradient_u * options.element_size;
        } else {
            noise.seed = options.seed;
            out_u = noise_at_position(heightfield, position, noise);
            noise.seed = options.seed + 104729;
            out_v = noise_at_position(heightfield, position, noise);
        }
        const double length = std::hypot(out_u, out_v);
        if (length > 1.0) {
            out_u /= length;
            out_v /= length;
        }
    };

    const double step_amplitude = options.amplitude / static_cast<double>(options.substeps);
    for (int step = 0; step < options.substeps; ++step) {
        const data::PcgHeightField source = heightfield;
        for (auto& [name, destination] : heightfield.layers_mut()) {
            if (!all_layers && selected.find(name) == selected.end())
                continue;
            const data::PcgHeightFieldLayer* source_layer = source.find_layer(name);
            if (!source_layer || source_layer->tuple_size != destination.tuple_size)
                return false;
            for (int z = 0; z < heightfield.resolution_z(); ++z) {
                for (int x = 0; x < heightfield.resolution_x(); ++x) {
                    const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
                    double mask_weight = 1.0;
                    if (!external_mask_weight(mask, options.mask_layer, position, mask_weight))
                        return false;
                    double direction_u = 0.0;
                    double direction_v = 0.0;
                    vector_at(position, direction_u, direction_v);
                    const data::PcgVec3 source_position = offset_on_plane(
                        position,
                        -direction_u * step_amplitude * mask_weight,
                        -direction_v * step_amplitude * mask_weight);
                    const std::size_t sample =
                        static_cast<std::size_t>(z) *
                            static_cast<std::size_t>(heightfield.resolution_x()) +
                        static_cast<std::size_t>(x);
                    for (int component = 0; component < destination.tuple_size; ++component) {
                        double value = 0.0;
                        if (!source.sample_component_world(
                                name,
                                component,
                                source_position.x,
                                source_position.y,
                                source_position.z,
                                value)) {
                            return false;
                        }
                        destination.values[
                            sample * static_cast<std::size_t>(destination.tuple_size) +
                            static_cast<std::size_t>(component)] = static_cast<float>(value);
                    }
                }
            }
        }
    }
    return heightfield.valid();
}

bool apply_heightfield_project(data::PcgHeightField& heightfield,
                               const data::PcgGeometry& geometry,
                               const HeightFieldProjectOptions& options)
{
    data::PcgHeightFieldLayer* layer = heightfield.find_layer_mut(options.height_layer);
    if (!heightfield.valid() || !layer || layer->tuple_size != 1 ||
        geometry.points().empty() || geometry.faces().empty() ||
        !std::isfinite(options.max_ray_distance) || options.max_ray_distance <= 0.0) {
        return false;
    }
    const data::PcgMeshData mesh = data::triangulate_geometry_shared(geometry);
    if (mesh.vertices().empty() || mesh.triangles().empty() ||
        mesh.triangles().size() > 3000000) {
        return false;
    }
    const std::size_t work = heightfield.sample_count() * (mesh.triangles().size() / 3);
    if (work > static_cast<std::size_t>(250000000))
        return false;

    struct ProjectedVertex {
        double u = 0.0;
        double v = 0.0;
        double height = 0.0;
    };
    auto project_vertex = [&](const data::PcgVertex& vertex) {
        ProjectedVertex result;
        if (heightfield.orientation() == data::HeightFieldOrientation::XY) {
            result = {vertex.x, vertex.y, vertex.z - heightfield.center().z};
        } else if (heightfield.orientation() == data::HeightFieldOrientation::YZ) {
            result = {vertex.y, vertex.z, vertex.x - heightfield.center().x};
        } else {
            result = {vertex.x, vertex.z, vertex.y - heightfield.center().y};
        }
        return result;
    };
    std::vector<ProjectedVertex> projected;
    projected.reserve(mesh.vertices().size());
    for (const data::PcgVertex& vertex : mesh.vertices())
        projected.push_back(project_vertex(vertex));

    auto combine = [&](double original, double value) {
        switch (options.combine) {
        case HeightFieldProjectMode::Replace: return value;
        case HeightFieldProjectMode::Add: return original + value;
        case HeightFieldProjectMode::Minimum: return std::min(original, value);
        case HeightFieldProjectMode::Maximum:
        default: return std::max(original, value);
        }
    };
    constexpr double kBarycentricEpsilon = 1e-9;
    for (int z = 0; z < heightfield.resolution_z(); ++z) {
        for (int x = 0; x < heightfield.resolution_x(); ++x) {
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            double sample_u = 0.0;
            double sample_v = 0.0;
            plane_coordinates(heightfield, position, sample_u, sample_v);
            const std::size_t sample = static_cast<std::size_t>(z) *
                                           static_cast<std::size_t>(heightfield.resolution_x()) +
                                       static_cast<std::size_t>(x);
            const double original = layer->values[sample];
            bool hit = false;
            double chosen = 0.0;
            for (std::size_t triangle = 0; triangle < mesh.triangles().size(); triangle += 3) {
                const ProjectedVertex& a = projected[static_cast<std::size_t>(
                    mesh.triangles()[triangle])];
                const ProjectedVertex& b = projected[static_cast<std::size_t>(
                    mesh.triangles()[triangle + 1])];
                const ProjectedVertex& c = projected[static_cast<std::size_t>(
                    mesh.triangles()[triangle + 2])];
                const double denominator =
                    (b.v - c.v) * (a.u - c.u) + (c.u - b.u) * (a.v - c.v);
                if (std::abs(denominator) <= kBarycentricEpsilon)
                    continue;
                const double wa = ((b.v - c.v) * (sample_u - c.u) +
                                   (c.u - b.u) * (sample_v - c.v)) /
                                  denominator;
                const double wb = ((c.v - a.v) * (sample_u - c.u) +
                                   (a.u - c.u) * (sample_v - c.v)) /
                                  denominator;
                const double wc = 1.0 - wa - wb;
                if (wa < -kBarycentricEpsilon || wb < -kBarycentricEpsilon ||
                    wc < -kBarycentricEpsilon) {
                    continue;
                }
                const double projected_height =
                    wa * a.height + wb * b.height + wc * c.height;
                const double distance = std::abs(projected_height - original);
                if (distance > options.max_ray_distance)
                    continue;
                if (!hit || (options.hit_farthest ? projected_height > chosen
                                                  : projected_height < chosen)) {
                    hit = true;
                    chosen = projected_height;
                }
            }
            if (hit)
                layer->values[sample] = static_cast<float>(combine(original, chosen));
        }
    }
    return heightfield.valid();
}

data::PcgPointData scatter_heightfield(
    const data::PcgHeightField& heightfield,
    const HeightFieldScatterOptions& options)
{
    data::PcgPointData output;
    const data::PcgHeightFieldLayer* height = heightfield.find_layer(options.height_layer);
    const data::PcgHeightFieldLayer* amount = heightfield.find_layer(options.scatter_layer);
    if (!heightfield.valid() || !height || height->tuple_size != 1 ||
        !amount || amount->tuple_size != 1 || options.point_count < 0 ||
        !std::isfinite(options.density) || options.density < 0.0 ||
        options.max_points < 0 || options.max_points > 1000000 ||
        options.candidates_per_point < 1 || options.candidates_per_point > 32) {
        return output;
    }

    const bool corner = heightfield.sampling() == data::HeightFieldSampling::Corner;
    const int cells_x = heightfield.resolution_x() - (corner ? 1 : 0);
    const int cells_z = heightfield.resolution_z() - (corner ? 1 : 0);
    if (cells_x <= 0 || cells_z <= 0)
        return output;
    const double cell_size_x = heightfield.size_x() / static_cast<double>(cells_x);
    const double cell_size_z = heightfield.size_z() / static_cast<double>(cells_z);

    auto world_position = [&](double u, double v, double displacement) {
        const double plane_u = -heightfield.size_x() * 0.5 + u * heightfield.size_x();
        const double plane_v = -heightfield.size_z() * 0.5 + v * heightfield.size_z();
        const data::PcgVec3& center = heightfield.center();
        if (heightfield.orientation() == data::HeightFieldOrientation::XY)
            return data::PcgVec3{center.x + plane_u, center.y + plane_v, center.z + displacement};
        if (heightfield.orientation() == data::HeightFieldOrientation::YZ)
            return data::PcgVec3{center.x + displacement, center.y + plane_u, center.z + plane_v};
        return data::PcgVec3{center.x + plane_u, center.y + displacement, center.z + plane_v};
    };
    auto sample_height = [&](double u, double v, double& out_height) {
        const data::PcgVec3 position = world_position(u, v, 0.0);
        return heightfield.sample_scalar_world(
            options.height_layer, position.x, position.y, position.z, out_height);
    };
    auto sample_amount = [&](double u, double v, double& out_amount) {
        const data::PcgVec3 position = world_position(u, v, 0.0);
        if (!heightfield.sample_scalar_world(
                options.scatter_layer, position.x, position.y, position.z, out_amount)) {
            return false;
        }
        out_amount = std::max(0.0, out_amount);
        return true;
    };

    std::vector<double> cumulative;
    cumulative.reserve(static_cast<std::size_t>(cells_x) * static_cast<std::size_t>(cells_z));
    std::vector<double> cell_max_amounts;
    cell_max_amounts.reserve(
        static_cast<std::size_t>(cells_x) * static_cast<std::size_t>(cells_z));
    double total_weighted_area = 0.0;
    for (int z = 0; z < cells_z; ++z) {
        for (int x = 0; x < cells_x; ++x) {
            const double u = (static_cast<double>(x) + 0.5) / static_cast<double>(cells_x);
            const double v = (static_cast<double>(z) + 0.5) / static_cast<double>(cells_z);
            const double u0 = static_cast<double>(x) / static_cast<double>(cells_x);
            const double u1 = static_cast<double>(x + 1) / static_cast<double>(cells_x);
            const double v0 = static_cast<double>(z) / static_cast<double>(cells_z);
            const double v1 = static_cast<double>(z + 1) / static_cast<double>(cells_z);
            double amounts[5] = {};
            if (!sample_amount(u, v, amounts[0]) ||
                !sample_amount(u0, v0, amounts[1]) ||
                !sample_amount(u1, v0, amounts[2]) ||
                !sample_amount(u0, v1, amounts[3]) ||
                !sample_amount(u1, v1, amounts[4])) {
                return {};
            }
            const double weight =
                (amounts[0] * 4.0 + amounts[1] + amounts[2] + amounts[3] + amounts[4]) /
                8.0;
            const double cell_max_amount = *std::max_element(
                std::begin(amounts), std::end(amounts));
            const double du = 0.5 / static_cast<double>(cells_x);
            const double dv = 0.5 / static_cast<double>(cells_z);
            double h_left = 0.0;
            double h_right = 0.0;
            double h_down = 0.0;
            double h_up = 0.0;
            if (!sample_height(std::max(0.0, u - du), v, h_left) ||
                !sample_height(std::min(1.0, u + du), v, h_right) ||
                !sample_height(u, std::max(0.0, v - dv), h_down) ||
                !sample_height(u, std::min(1.0, v + dv), h_up)) {
                return {};
            }
            const double gradient_u = (h_right - h_left) / cell_size_x;
            const double gradient_v = (h_up - h_down) / cell_size_z;
            const double surface_area = cell_size_x * cell_size_z *
                std::sqrt(1.0 + gradient_u * gradient_u + gradient_v * gradient_v);
            total_weighted_area += weight * surface_area;
            cumulative.push_back(total_weighted_area);
            cell_max_amounts.push_back(cell_max_amount);
        }
    }
    if (total_weighted_area <= 1e-12 || options.max_points == 0)
        return output;
    const int requested = options.use_exact_point_count
        ? options.point_count
        : static_cast<int>(std::llround(options.density * total_weighted_area));
    const int count = std::clamp(requested, 0, options.max_points);
    if (count == 0)
        return output;

    uint32_t state = static_cast<uint32_t>(options.seed) ^ 0x9e3779b9u;
    auto random01 = [&]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return static_cast<double>(state) /
            static_cast<double>(std::numeric_limits<uint32_t>::max());
    };
    struct Candidate {
        double u = 0.0;
        double v = 0.0;
    };
    auto weighted_candidate = [&]() {
        Candidate fallback;
        for (int attempt = 0; attempt < 32; ++attempt) {
            const double pick = random01() * total_weighted_area;
            const auto it = std::lower_bound(cumulative.begin(), cumulative.end(), pick);
            const std::size_t cell = std::min<std::size_t>(
                static_cast<std::size_t>(std::distance(cumulative.begin(), it)),
                cumulative.size() - 1);
            const int cell_x = static_cast<int>(cell % static_cast<std::size_t>(cells_x));
            const int cell_z = static_cast<int>(cell / static_cast<std::size_t>(cells_x));
            fallback = Candidate{
                (static_cast<double>(cell_x) + random01()) / static_cast<double>(cells_x),
                (static_cast<double>(cell_z) + random01()) / static_cast<double>(cells_z),
            };
            double local_amount = 0.0;
            if (!sample_amount(fallback.u, fallback.v, local_amount))
                return Candidate{};
            const double cell_max = cell_max_amounts[cell];
            if (cell_max <= 1e-12 || random01() * cell_max <= local_amount)
                return fallback;
        }
        return fallback;
    };
    std::vector<Candidate> accepted;
    accepted.reserve(static_cast<std::size_t>(count));
    for (int point_index = 0; point_index < count; ++point_index) {
        if (options.is_cancel_requested && options.is_cancel_requested())
            return output;
        Candidate best;
        double best_distance = -1.0;
        const int candidates = accepted.empty() ? 1 : options.candidates_per_point;
        for (int candidate_index = 0; candidate_index < candidates; ++candidate_index) {
            const Candidate candidate = weighted_candidate();
            double minimum_distance = std::numeric_limits<double>::infinity();
            const std::size_t begin = accepted.size() > 128 ? accepted.size() - 128 : 0;
            for (std::size_t i = begin; i < accepted.size(); ++i) {
                const double dx = (candidate.u - accepted[i].u) * heightfield.size_x();
                const double dz = (candidate.v - accepted[i].v) * heightfield.size_z();
                minimum_distance = std::min(minimum_distance, dx * dx + dz * dz);
            }
            if (minimum_distance > best_distance) {
                best = candidate;
                best_distance = minimum_distance;
            }
        }
        accepted.push_back(best);
        double sampled_height = 0.0;
        if (!sample_height(best.u, best.v, sampled_height))
            return {};
        const double du = 0.5 / static_cast<double>(cells_x);
        const double dv = 0.5 / static_cast<double>(cells_z);
        double h_left = sampled_height;
        double h_right = sampled_height;
        double h_down = sampled_height;
        double h_up = sampled_height;
        sample_height(std::max(0.0, best.u - du), best.v, h_left);
        sample_height(std::min(1.0, best.u + du), best.v, h_right);
        sample_height(best.u, std::max(0.0, best.v - dv), h_down);
        sample_height(best.u, std::min(1.0, best.v + dv), h_up);
        const double gradient_u = (h_right - h_left) / cell_size_x;
        const double gradient_v = (h_up - h_down) / cell_size_z;
        data::PcgVec3 normal;
        if (heightfield.orientation() == data::HeightFieldOrientation::XY)
            normal = {-gradient_u, -gradient_v, 1.0};
        else if (heightfield.orientation() == data::HeightFieldOrientation::YZ)
            normal = {1.0, -gradient_u, -gradient_v};
        else
            normal = {-gradient_u, 1.0, -gradient_v};
        const double normal_length = std::sqrt(
            normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        normal.x /= normal_length;
        normal.y /= normal_length;
        normal.z /= normal_length;
        const data::PcgVec3 position = world_position(best.u, best.v, sampled_height);
        double amount_value = 0.0;
        sample_amount(best.u, best.v, amount_value);
        data::PcgPoint point{position.x, position.y, position.z};
        point.attributes["nx"] = normal.x;
        point.attributes["ny"] = normal.y;
        point.attributes["nz"] = normal.z;
        point.attributes["u"] = best.u;
        point.attributes["v"] = best.v;
        point.attributes["height"] = sampled_height;
        point.attributes["density"] = amount_value;
        output.add_point(point);
    }
    return output;
}

data::PcgGeometry convert_heightfield_to_geometry(
    const data::PcgHeightField& heightfield,
    const ConvertHeightFieldOptions& options)
{
    data::PcgGeometry geometry;
    const data::PcgHeightFieldLayer* height_layer = heightfield.find_layer(options.height_layer);
    if (!heightfield.valid() || !height_layer || height_layer->tuple_size != 1 ||
        !std::isfinite(options.density) || options.density <= 0.0)
        return geometry;

    const int source_x = heightfield.resolution_x();
    const int source_z = heightfield.resolution_z();
    const bool corner = heightfield.sampling() == data::HeightFieldSampling::Corner;
    const int intervals_x = corner ? source_x - 1 : source_x;
    const int intervals_z = corner ? source_z - 1 : source_z;
    const int output_x = std::clamp(
        static_cast<int>(std::llround(static_cast<double>(intervals_x) * options.density)) +
            (corner ? 1 : 0),
        2,
        kMaxGridSamples);
    const int output_z = std::clamp(
        static_cast<int>(std::llround(static_cast<double>(intervals_z) * options.density)) +
            (corner ? 1 : 0),
        2,
        kMaxGridSamples);
    const std::size_t output_count = static_cast<std::size_t>(output_x) *
                                     static_cast<std::size_t>(output_z);
    if (output_count > kMaxHeightFieldSamples)
        return geometry;

    data::PcgHeightField output_grid(output_x,
                                     output_z,
                                     heightfield.size_x(),
                                     heightfield.size_z(),
                                     heightfield.center(),
                                     heightfield.sampling(),
                                     heightfield.orientation());
    geometry.points_mut().reserve(output_count);
    std::vector<data::PcgVec2> point_uvs;
    point_uvs.reserve(output_count);
    for (int z = 0; z < output_z; ++z) {
        for (int x = 0; x < output_x; ++x) {
            const data::PcgVec3 base = output_grid.sample_position(x, z, 0.0);
            double height = 0.0;
            if (!heightfield.sample_scalar_world(options.height_layer,
                                                 base.x,
                                                 base.y,
                                                 base.z,
                                                 height)) {
                return {};
            }
            geometry.points_mut().push_back(output_grid.sample_position(x, z, height));
            point_uvs.push_back(data::PcgVec2{
                output_x > 1 ? static_cast<double>(x) / static_cast<double>(output_x - 1) : 0.0,
                output_z > 1 ? static_cast<double>(z) / static_cast<double>(output_z - 1) : 0.0,
            });
        }
    }

    geometry.faces_mut().reserve(
        static_cast<std::size_t>(output_x - 1) * static_cast<std::size_t>(output_z - 1));
    for (int z = 0; z + 1 < output_z; ++z) {
        for (int x = 0; x + 1 < output_x; ++x) {
            const int i00 = z * output_x + x;
            const int i10 = i00 + 1;
            const int i01 = (z + 1) * output_x + x;
            const int i11 = i01 + 1;
            if (heightfield.orientation() == data::HeightFieldOrientation::ZX)
                geometry.faces_mut().push_back({i00, i01, i11, i10});
            else
                geometry.faces_mut().push_back({i00, i10, i11, i01});
        }
    }

    geometry.set_uvs(std::move(point_uvs));
    geometry.expand_point_uvs_to_corners();
    geometry.detail().shade_mode = data::ShadeMode::Smooth;
    geometry.detail().cusp_angle_deg = 180.0;
    return geometry;
}

} // namespace pcg::internal::elements
