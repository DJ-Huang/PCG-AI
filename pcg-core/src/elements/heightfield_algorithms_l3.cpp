#include "elements/heightfield_algorithms.hpp"

#include "data/pcg_mesh_data.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace pcg::internal::elements {
namespace {

constexpr int kMaxGridSamples = 2049;

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

bool external_mask_weight(const data::PcgHeightField* mask,
                          const std::string& layer_name,
                          const data::PcgVec3& position,
                          double& out_weight)
{
    out_weight = 1.0;
    if (!mask)
        return true;
    if (layer_name.empty() || !mask->find_layer(layer_name))
        return false;
    double sampled = 0.0;
    if (!mask->sample_scalar_world(
            layer_name, position.x, position.y, position.z, sampled)) {
        return false;
    }
    out_weight = std::clamp(sampled, 0.0, 1.0);
    return true;
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

void blur_scalar_inplace(data::PcgHeightField& heightfield,
                         const std::string& layer_name,
                         HeightFieldBlurMethod method,
                         double radius_meters)
{
    if (radius_meters <= 0.0)
        return;
    HeightFieldBlurOptions blur;
    blur.blur_layer = layer_name;
    blur.mask_layer = "";
    blur.method = method;
    blur.iterations = 1;
    blur.radius_meters = radius_meters;
    blur.mask_aware = false;
    apply_heightfield_blur(heightfield, nullptr, blur);
}

double pattern_coordinate(double plane_x,
                          double plane_z,
                          const HeightFieldPatternOptions& options,
                          double& out_u,
                          double& out_v)
{
    const double radians = options.rotate_degrees * 0.0174532925199432957692;
    const double cos_r = std::cos(radians);
    const double sin_r = std::sin(radians);
    const double dx = plane_x - options.center_x;
    const double dz = plane_z - options.center_z;
    const double rx = dx * cos_r - dz * sin_r;
    const double rz = dx * sin_r + dz * cos_r;
    const double sx = std::max(std::abs(options.scale_x), 1e-6);
    const double sz = std::max(std::abs(options.scale_z), 1e-6);
    const double size = std::max(std::abs(options.size), 1e-6);
    out_u = rx / (size * sx);
    out_v = rz / (size * sz);
    return size;
}

double evaluate_pattern(double plane_x,
                        double plane_z,
                        const HeightFieldPatternOptions& options)
{
    double u = 0.0;
    double v = 0.0;
    pattern_coordinate(plane_x, plane_z, options, u, v);
    u += options.phase;
    v += options.phase;

    auto wrap01 = [&](double value) {
        if (!options.ramp_repeat)
            return std::clamp(value, 0.0, 1.0);
        double wrapped = value - std::floor(value);
        if (options.ramp_mirror) {
            const int cell = static_cast<int>(std::floor(value));
            if ((cell & 1) != 0)
                wrapped = 1.0 - wrapped;
        }
        return wrapped;
    };

    double amount = 0.0;
    switch (options.pattern) {
    case HeightFieldPatternKind::ExponentialRamp:
    case HeightFieldPatternKind::Ramp: {
        double t = 0.0;
        if (options.ramp_mode == HeightFieldRampMode::Concentric) {
            t = std::hypot(u, v);
        } else if (options.ramp_mode == HeightFieldRampMode::Radial) {
            t = (std::atan2(v, u) / (2.0 * 3.14159265358979323846)) + 0.5;
        } else {
            t = u + 0.5;
        }
        t = wrap01(t);
        if (options.pattern == HeightFieldPatternKind::ExponentialRamp)
            t = 1.0 - std::exp(-3.0 * t);
        amount = t;
        break;
    }
    case HeightFieldPatternKind::Step: {
        const double run = std::max(std::abs(options.rise_over_run), 1e-6);
        const double along = (u + 0.5) * std::max(std::abs(options.size), 1e-6);
        const double stepped =
            std::floor((along + options.phase) / std::max(options.step_height / run, 1e-6));
        amount = (stepped * options.step_height + options.step_reference_height -
                  options.base_height) /
                 std::max(std::abs(options.height), 1e-6);
        break;
    }
    case HeightFieldPatternKind::Stripes: {
        const double stripe = wrap01(u);
        const double width = std::clamp(options.stripe_width, 0.0, 1.0);
        amount = stripe < width ? 1.0 : 0.0;
        break;
    }
    }
    return options.base_height + amount * options.height;
}

bool load_pgm_scalar(const std::string& path,
                     int& out_width,
                     int& out_height,
                     std::vector<float>& out_values)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    std::string magic;
    input >> magic;
    if (magic != "P2" && magic != "P5")
        return false;
    auto skip_comments = [&]() {
        while (input) {
            const int peek = input.peek();
            if (peek == '#') {
                std::string line;
                std::getline(input, line);
                continue;
            }
            if (std::isspace(peek)) {
                input.get();
                continue;
            }
            break;
        }
    };
    skip_comments();
    int width = 0;
    int height = 0;
    int max_value = 0;
    input >> width >> height;
    skip_comments();
    input >> max_value;
    if (width < 2 || height < 2 || width > kMaxGridSamples || height > kMaxGridSamples ||
        max_value <= 0) {
        return false;
    }
    out_width = width;
    out_height = height;
    out_values.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0.0f);
    if (magic == "P2") {
        for (float& value : out_values) {
            int sample = 0;
            if (!(input >> sample))
                return false;
            value = static_cast<float>(sample) / static_cast<float>(max_value);
        }
        return true;
    }
    input.get();
    if (max_value <= 255) {
        for (float& value : out_values) {
            const int sample = input.get();
            if (sample == std::char_traits<char>::eof())
                return false;
            value = static_cast<float>(sample) / static_cast<float>(max_value);
        }
        return true;
    }
    for (float& value : out_values) {
        const int hi = input.get();
        const int lo = input.get();
        if (hi == std::char_traits<char>::eof() || lo == std::char_traits<char>::eof())
            return false;
        const int sample = (hi << 8) | lo;
        value = static_cast<float>(sample) / static_cast<float>(max_value);
    }
    return true;
}

bool load_raw_float32(const std::string& path,
                      int width,
                      int height,
                      std::vector<float>& out_values)
{
    if (width < 2 || height < 2 || width > kMaxGridSamples || height > kMaxGridSamples)
        return false;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return false;
    const std::size_t count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    out_values.resize(count);
    input.read(reinterpret_cast<char*>(out_values.data()),
               static_cast<std::streamsize>(count * sizeof(float)));
    return static_cast<std::size_t>(input.gcount()) == count * sizeof(float);
}

} // namespace

bool apply_heightfield_mask_by_object(data::PcgHeightField& heightfield,
                                      const data::PcgGeometry& geometry,
                                      const HeightFieldMaskByObjectOptions& options)
{
    const data::PcgHeightFieldLayer* height = heightfield.find_layer(options.height_layer);
    if (!heightfield.valid() || !height || height->tuple_size != 1 ||
        options.output_layer.empty() || geometry.points().empty() ||
        geometry.faces().empty() || !std::isfinite(options.max_ray_distance) ||
        options.max_ray_distance <= 0.0 || !std::isfinite(options.value)) {
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

    data::PcgHeightFieldLayer* output = heightfield.find_layer_mut(options.output_layer);
    if (!output)
        output = &heightfield.create_layer(options.output_layer, 1, 0.0f);
    if (output->tuple_size != 1)
        return false;

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
            const double terrain_height = height->values[sample];
            bool hit = false;
            double chosen = 0.0;
            for (std::size_t triangle = 0; triangle < mesh.triangles().size(); triangle += 3) {
                const ProjectedVertex& a =
                    projected[static_cast<std::size_t>(mesh.triangles()[triangle])];
                const ProjectedVertex& b =
                    projected[static_cast<std::size_t>(mesh.triangles()[triangle + 1])];
                const ProjectedVertex& c =
                    projected[static_cast<std::size_t>(mesh.triangles()[triangle + 2])];
                const double denominator =
                    (b.v - c.v) * (a.u - c.u) + (c.u - b.u) * (a.v - c.v);
                if (std::abs(denominator) <= kBarycentricEpsilon)
                    continue;
                const double wa =
                    ((b.v - c.v) * (sample_u - c.u) + (c.u - b.u) * (sample_v - c.v)) /
                    denominator;
                const double wb =
                    ((c.v - a.v) * (sample_u - c.u) + (a.u - c.u) * (sample_v - c.v)) /
                    denominator;
                const double wc = 1.0 - wa - wb;
                if (wa < -kBarycentricEpsilon || wb < -kBarycentricEpsilon ||
                    wc < -kBarycentricEpsilon) {
                    continue;
                }
                const double projected_height = wa * a.height + wb * b.height + wc * c.height;
                const double distance = std::abs(projected_height - terrain_height);
                if (distance > options.max_ray_distance)
                    continue;
                bool side_ok = true;
                if (options.side == HeightFieldMaskByObjectSide::Above)
                    side_ok = projected_height >= terrain_height;
                else if (options.side == HeightFieldMaskByObjectSide::Below)
                    side_ok = projected_height <= terrain_height;
                if (!side_ok)
                    continue;
                if (!hit || projected_height > chosen) {
                    hit = true;
                    chosen = projected_height;
                }
            }
            double generated = hit ? options.value : 0.0;
            if (options.invert)
                generated = options.value - generated;
            output->values[sample] = static_cast<float>(combine_value(
                output->values[sample], generated, options.combine, options.blend));
        }
    }

    blur_scalar_inplace(
        heightfield, options.output_layer, options.blur_method, options.blur_radius_meters);
    return heightfield.valid();
}

bool apply_heightfield_pattern(data::PcgHeightField& heightfield,
                               const data::PcgHeightField* mask,
                               const HeightFieldPatternOptions& options)
{
    if (!heightfield.valid() || options.pattern_layer.empty() ||
        !std::isfinite(options.height) || !std::isfinite(options.size) ||
        options.size == 0.0) {
        return false;
    }
    data::PcgHeightFieldLayer* layer = heightfield.find_layer_mut(options.pattern_layer);
    if (!layer)
        layer = &heightfield.create_layer(options.pattern_layer, 1, 0.0f);
    if (layer->tuple_size != 1)
        return false;

    for (int z = 0; z < heightfield.resolution_z(); ++z) {
        for (int x = 0; x < heightfield.resolution_x(); ++x) {
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            double plane_x = 0.0;
            double plane_z = 0.0;
            plane_coordinates(heightfield, position, plane_x, plane_z);
            const std::size_t index = static_cast<std::size_t>(z) *
                                          static_cast<std::size_t>(heightfield.resolution_x()) +
                                      static_cast<std::size_t>(x);
            const double generated = evaluate_pattern(plane_x, plane_z, options);
            const double combined = combine_value(
                layer->values[index], generated, options.combine, options.blend);
            double weight = 1.0;
            if (!external_mask_weight(mask, options.mask_layer, position, weight))
                return false;
            layer->values[index] = static_cast<float>(
                layer->values[index] + (combined - layer->values[index]) * weight);
        }
    }

    if (options.post_blur_radius > 0.0) {
        blur_scalar_inplace(heightfield,
                            options.pattern_layer,
                            HeightFieldBlurMethod::Gaussian,
                            options.post_blur_radius);
    }
    return true;
}

bool apply_heightfield_flow_field(data::PcgHeightField& heightfield,
                                  const HeightFieldFlowFieldOptions& options)
{
    data::PcgHeightFieldLayer* height = heightfield.find_layer_mut(options.height_layer);
    if (!heightfield.valid() || !height || height->tuple_size != 1 ||
        options.spread_iterations < 1 || options.spread_iterations > 500 ||
        options.smoothing_iterations < 0 || options.smoothing_iterations > 64 ||
        !std::isfinite(options.rain_amount) || options.rain_amount < 0.0 ||
        !std::isfinite(options.rain_density) || options.rain_density < 0.0 ||
        options.rain_density > 1.0 || options.flow_layer.empty() ||
        options.flow_direction_layer.empty() || options.water_layer.empty() ||
        options.flow_layer == options.flow_direction_layer) {
        return false;
    }

    const int resolution_x = heightfield.resolution_x();
    const int resolution_z = heightfield.resolution_z();
    const std::size_t count = heightfield.sample_count();
    std::vector<double> heights(height->values.begin(), height->values.end());
    std::vector<double> water(count, 0.0);
    std::vector<double> flow(count, 0.0);
    std::vector<double> flow_x(count, 0.0);
    std::vector<double> flow_z(count, 0.0);

    static constexpr int kDirections[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };

    for (int z = 0; z < resolution_z; ++z) {
        for (int x = 0; x < resolution_x; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                          static_cast<std::size_t>(resolution_x) +
                                      static_cast<std::size_t>(x);
            const double chance =
                static_cast<double>(hash_lattice(x, z, options.seed)) /
                static_cast<double>(std::numeric_limits<uint32_t>::max());
            if (chance <= options.rain_density)
                water[index] = options.rain_amount;
        }
    }

    const double fan_out =
        options.slump_mode == HeightFieldSlumpMode::Smooth ? 0.55 : 0.95;
    for (int iteration = 0; iteration < options.spread_iterations; ++iteration) {
        std::vector<double> next_water(count, 0.0);
        for (int z = 0; z < resolution_z; ++z) {
            for (int x = 0; x < resolution_x; ++x) {
                const std::size_t index = static_cast<std::size_t>(z) *
                                              static_cast<std::size_t>(resolution_x) +
                                          static_cast<std::size_t>(x);
                if (water[index] <= 1e-12) {
                    next_water[index] += water[index];
                    continue;
                }

                struct Candidate {
                    int x = 0;
                    int z = 0;
                    double drop = 0.0;
                    double distance = 1.0;
                };
                std::vector<Candidate> downhill;
                downhill.reserve(8);
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
                    if (drop > 0.0)
                        downhill.push_back({nx, nz, drop, distance});
                }
                if (downhill.empty()) {
                    next_water[index] += water[index];
                    continue;
                }

                if (options.slump_mode == HeightFieldSlumpMode::Granular) {
                    auto best = std::max_element(
                        downhill.begin(), downhill.end(),
                        [](const Candidate& a, const Candidate& b) {
                            return (a.drop / a.distance) < (b.drop / b.distance);
                        });
                    downhill = {*best};
                }

                double slope_sum = 0.0;
                for (const Candidate& candidate : downhill)
                    slope_sum += candidate.drop / candidate.distance;
                if (slope_sum <= 1e-12) {
                    next_water[index] += water[index];
                    continue;
                }

                const double moved = water[index] * fan_out;
                next_water[index] += water[index] - moved;
                for (const Candidate& candidate : downhill) {
                    const double share =
                        (candidate.drop / candidate.distance) / slope_sum * moved;
                    const std::size_t neighbor =
                        static_cast<std::size_t>(candidate.z) *
                            static_cast<std::size_t>(resolution_x) +
                        static_cast<std::size_t>(candidate.x);
                    next_water[neighbor] += share;
                    flow[index] += share;
                    const double length = std::hypot(
                        static_cast<double>(candidate.x - x),
                        static_cast<double>(candidate.z - z));
                    flow_x[index] +=
                        static_cast<double>(candidate.x - x) / length * share;
                    flow_z[index] +=
                        static_cast<double>(candidate.z - z) / length * share;
                }
            }
        }
        water = std::move(next_water);
    }

    data::PcgHeightFieldLayer& water_layer =
        heightfield.find_layer_mut(options.water_layer)
            ? *heightfield.find_layer_mut(options.water_layer)
            : heightfield.create_layer(options.water_layer, 1, 0.0f);
    data::PcgHeightFieldLayer& flow_layer =
        heightfield.find_layer_mut(options.flow_layer)
            ? *heightfield.find_layer_mut(options.flow_layer)
            : heightfield.create_layer(options.flow_layer, 1, 0.0f);
    data::PcgHeightFieldLayer& flowdir_layer =
        heightfield.find_layer_mut(options.flow_direction_layer)
            ? *heightfield.find_layer_mut(options.flow_direction_layer)
            : heightfield.create_layer(options.flow_direction_layer, 2, 0.0f);
    if (water_layer.tuple_size != 1 || flow_layer.tuple_size != 1 ||
        flowdir_layer.tuple_size != 2) {
        return false;
    }

    double max_flow = 0.0;
    for (double value : flow)
        max_flow = std::max(max_flow, value);

    for (std::size_t i = 0; i < count; ++i) {
        water_layer.values[i] = static_cast<float>(water[i]);
        flow_layer.values[i] = static_cast<float>(flow[i]);
        const double length = std::hypot(flow_x[i], flow_z[i]);
        flowdir_layer.values[i * 2] =
            static_cast<float>(length > 1e-12 ? flow_x[i] / length : 0.0);
        flowdir_layer.values[i * 2 + 1] =
            static_cast<float>(length > 1e-12 ? flow_z[i] / length : 0.0);
        if (options.adjust_height) {
            height->values[i] = static_cast<float>(
                heights[i] - flow[i] * options.adjust_height_scale);
        }
    }

    if (options.copy_to_mask) {
        data::PcgHeightFieldLayer& mask_layer =
            heightfield.find_layer_mut(options.mask_layer)
                ? *heightfield.find_layer_mut(options.mask_layer)
                : heightfield.create_layer(options.mask_layer, 1, 0.0f);
        if (mask_layer.tuple_size != 1)
            return false;
        for (std::size_t i = 0; i < count; ++i) {
            const double normalized =
                max_flow > 1e-12 ? flow[i] / max_flow * options.mask_scale : 0.0;
            mask_layer.values[i] = static_cast<float>(std::clamp(normalized, 0.0, 1.0));
        }
        if (options.smoothing_iterations > 0) {
            HeightFieldBlurOptions blur;
            blur.blur_layer = options.mask_layer;
            blur.iterations = options.smoothing_iterations;
            blur.radius_meters =
                std::max(heightfield.spacing_x(), heightfield.spacing_z()) * 0.75;
            blur.method = HeightFieldBlurMethod::Box;
            blur.mask_aware = false;
            apply_heightfield_blur(heightfield, nullptr, blur);
        }
    } else if (options.smoothing_iterations > 0) {
        HeightFieldBlurOptions blur;
        blur.blur_layer = options.flow_layer;
        blur.iterations = options.smoothing_iterations;
        blur.radius_meters =
            std::max(heightfield.spacing_x(), heightfield.spacing_z()) * 0.75;
        blur.method = HeightFieldBlurMethod::Box;
        blur.mask_aware = false;
        apply_heightfield_blur(heightfield, nullptr, blur);
    }
    return heightfield.valid();
}

bool apply_heightfield_slump(data::PcgHeightField& heightfield,
                             const data::PcgHeightField* mask,
                             const HeightFieldSlumpOptions& options)
{
    data::PcgHeightFieldLayer* height = heightfield.find_layer_mut(options.height_layer);
    if (!heightfield.valid() || !height || height->tuple_size != 1 ||
        options.spread_iterations < 1 || options.spread_iterations > 500 ||
        options.repose_angle_degrees < 0.0 || options.repose_angle_degrees >= 90.0 ||
        !std::isfinite(options.spread_rate) || options.spread_rate < 0.0 ||
        options.material_layer.empty()) {
        return false;
    }

    data::PcgHeightFieldLayer* material = heightfield.find_layer_mut(options.material_layer);
    if (!material)
        material = &heightfield.create_layer(options.material_layer, 1, 0.0f);
    if (material->tuple_size != 1)
        return false;

    const int resolution_x = heightfield.resolution_x();
    const int resolution_z = heightfield.resolution_z();
    const std::size_t count = heightfield.sample_count();
    std::vector<double> bedrock(height->values.begin(), height->values.end());
    std::vector<double> loose(material->values.begin(), material->values.end());
    std::vector<double> flow(count, 0.0);
    std::vector<double> flow_x(count, 0.0);
    std::vector<double> flow_z(count, 0.0);
    std::vector<double> mask_weights(count, 1.0);
    for (int z = 0; z < resolution_z; ++z) {
        for (int x = 0; x < resolution_x; ++x) {
            const std::size_t index = static_cast<std::size_t>(z) *
                                          static_cast<std::size_t>(resolution_x) +
                                      static_cast<std::size_t>(x);
            const data::PcgVec3 position = heightfield.sample_position(x, z, 0.0);
            if (!external_mask_weight(mask, options.mask_layer, position, mask_weights[index]))
                return false;
            bedrock[index] *= options.height_factor;
        }
    }

    static constexpr int kDirections[8][2] = {
        {-1, 0}, {1, 0}, {0, -1}, {0, 1},
        {-1, -1}, {1, -1}, {-1, 1}, {1, 1},
    };
    const double repose_slope =
        std::tan(options.repose_angle_degrees * 0.0174532925199432957692);
    const double rate = std::clamp(options.spread_rate, 0.0, 1.0);

    for (int iteration = 0; iteration < options.spread_iterations; ++iteration) {
        std::vector<double> bedrock_delta(count, 0.0);
        std::vector<double> loose_delta(count, 0.0);
        for (int z = 0; z < resolution_z; ++z) {
            for (int x = 0; x < resolution_x; ++x) {
                const std::size_t index = static_cast<std::size_t>(z) *
                                              static_cast<std::size_t>(resolution_x) +
                                          static_cast<std::size_t>(x);
                if (mask_weights[index] <= 1e-12)
                    continue;
                const double surface = bedrock[index] + loose[index];
                struct Candidate {
                    int x = 0;
                    int z = 0;
                    double excess = 0.0;
                    double distance = 1.0;
                };
                std::vector<Candidate> moves;
                for (const auto& direction : kDirections) {
                    const int nx = x + direction[0];
                    const int nz = z + direction[1];
                    if (nx < 0 || nx >= resolution_x || nz < 0 || nz >= resolution_z) {
                        if (options.allow_material_outflow && loose[index] > 0.0) {
                            const double outflow = loose[index] * rate * 0.05 *
                                                   mask_weights[index];
                            loose_delta[index] -= outflow;
                        }
                        continue;
                    }
                    const std::size_t neighbor = static_cast<std::size_t>(nz) *
                                                     static_cast<std::size_t>(resolution_x) +
                                                 static_cast<std::size_t>(nx);
                    const double distance = std::hypot(
                        direction[0] * heightfield.spacing_x(),
                        direction[1] * heightfield.spacing_z());
                    const double neighbor_surface = bedrock[neighbor] + loose[neighbor];
                    const double drop = surface - neighbor_surface;
                    const double excess = drop - repose_slope * distance;
                    if (excess > 0.0)
                        moves.push_back({nx, nz, excess, distance});
                }
                if (moves.empty())
                    continue;

                if (options.slump_mode == HeightFieldSlumpMode::Granular) {
                    auto best = std::max_element(
                        moves.begin(), moves.end(),
                        [](const Candidate& a, const Candidate& b) {
                            return a.excess < b.excess;
                        });
                    moves = {*best};
                }

                double excess_sum = 0.0;
                for (const Candidate& move : moves)
                    excess_sum += move.excess;
                if (excess_sum <= 1e-12)
                    continue;

                double transferable = std::max(0.0, loose[index]);
                if (transferable <= 1e-12 && options.add_to_bedrock) {
                    transferable = excess_sum * 0.05 * mask_weights[index];
                    bedrock_delta[index] -= transferable;
                    loose[index] += transferable;
                }
                transferable = std::min(transferable, excess_sum) * rate * mask_weights[index];
                if (options.quantization > 0.0) {
                    transferable =
                        std::floor(transferable / options.quantization) * options.quantization;
                }
                if (transferable <= 1e-12)
                    continue;

                for (const Candidate& move : moves) {
                    const double share = move.excess / excess_sum * transferable;
                    const std::size_t neighbor = static_cast<std::size_t>(move.z) *
                                                     static_cast<std::size_t>(resolution_x) +
                                                 static_cast<std::size_t>(move.x);
                    loose_delta[index] -= share;
                    loose_delta[neighbor] += share;
                    flow[index] += share;
                    const double length = std::hypot(
                        static_cast<double>(move.x - x), static_cast<double>(move.z - z));
                    flow_x[index] += static_cast<double>(move.x - x) / length * share;
                    flow_z[index] += static_cast<double>(move.z - z) / length * share;
                }
            }
        }
        for (std::size_t i = 0; i < count; ++i) {
            bedrock[i] += bedrock_delta[i];
            loose[i] = std::max(0.0, loose[i] + loose_delta[i]);
        }
    }

    for (std::size_t i = 0; i < count; ++i) {
        if (options.add_to_bedrock) {
            height->values[i] = static_cast<float>(bedrock[i] + loose[i]);
            material->values[i] = 0.0f;
        } else {
            height->values[i] = static_cast<float>(bedrock[i]);
            material->values[i] = static_cast<float>(loose[i]);
        }
    }

    if (options.calculate_flow_fields) {
        data::PcgHeightFieldLayer& flow_layer =
            heightfield.find_layer_mut(options.flow_layer)
                ? *heightfield.find_layer_mut(options.flow_layer)
                : heightfield.create_layer(options.flow_layer, 1, 0.0f);
        data::PcgHeightFieldLayer& flowdir_layer =
            heightfield.find_layer_mut(options.flow_direction_layer)
                ? *heightfield.find_layer_mut(options.flow_direction_layer)
                : heightfield.create_layer(options.flow_direction_layer, 2, 0.0f);
        if (flow_layer.tuple_size != 1 || flowdir_layer.tuple_size != 2)
            return false;
        for (std::size_t i = 0; i < count; ++i) {
            flow_layer.values[i] = static_cast<float>(flow[i]);
            const double length = std::hypot(flow_x[i], flow_z[i]);
            flowdir_layer.values[i * 2] =
                static_cast<float>(length > 1e-12 ? flow_x[i] / length : 0.0);
            flowdir_layer.values[i * 2 + 1] =
                static_cast<float>(length > 1e-12 ? flow_z[i] / length : 0.0);
        }
        if (options.flow_smoothing_iterations > 0) {
            HeightFieldBlurOptions blur;
            blur.blur_layer = options.flow_layer;
            blur.iterations = options.flow_smoothing_iterations;
            blur.radius_meters =
                std::max(heightfield.spacing_x(), heightfield.spacing_z()) * 0.75;
            blur.method = HeightFieldBlurMethod::Box;
            blur.mask_aware = false;
            apply_heightfield_blur(heightfield, nullptr, blur);
        }
    }
    return heightfield.valid();
}

bool apply_heightfield_copy_layer(data::PcgHeightField& heightfield,
                                  const HeightFieldCopyLayerOptions& options)
{
    if (!heightfield.valid() || options.source.empty() || options.destination.empty())
        return false;
    const data::PcgHeightFieldLayer* source = heightfield.find_layer(options.source);
    if (!source)
        return false;
    data::PcgHeightFieldLayer* destination = heightfield.find_layer_mut(options.destination);
    if (destination) {
        if (!options.replace_existing)
            return true;
        if (destination->tuple_size != source->tuple_size)
            return false;
    } else {
        destination = &heightfield.create_layer(
            options.destination, source->tuple_size, 0.0f);
    }
    destination->border_type = source->border_type;
    destination->border_value = source->border_value;
    if (options.copy_source_data)
        destination->values = source->values;
    else
        std::fill(destination->values.begin(), destination->values.end(), 0.0f);
    return true;
}

bool apply_heightfield_layer_clear(data::PcgHeightField& heightfield,
                                   const HeightFieldLayerClearOptions& options)
{
    if (!heightfield.valid() || options.layer.empty())
        return false;
    data::PcgHeightFieldLayer* layer = heightfield.find_layer_mut(options.layer);
    if (!layer)
        layer = &heightfield.create_layer(options.layer, 1, options.value);
    std::fill(layer->values.begin(), layer->values.end(), options.value);
    return true;
}

bool apply_heightfield_layer_properties(
    data::PcgHeightField& heightfield,
    const HeightFieldLayerPropertiesOptions& options)
{
    if (!heightfield.valid() || options.layer.empty())
        return false;
    data::PcgHeightFieldLayer* layer = heightfield.find_layer_mut(options.layer);
    if (!layer)
        return false;
    if (options.set_border) {
        layer->border_type = options.border_type;
        layer->border_value = options.border_value;
    }
    return true;
}

bool apply_heightfield_isolate_layer(data::PcgHeightField& heightfield,
                                     const HeightFieldIsolateLayerOptions& options)
{
    if (!heightfield.valid() || options.layer.empty())
        return false;
    const data::PcgHeightFieldLayer* source = heightfield.find_layer(options.layer);
    if (!source || source->tuple_size != 1)
        return false;
    if (options.overwrite_mask) {
        data::PcgHeightFieldLayer& mask =
            heightfield.find_layer_mut("mask")
                ? *heightfield.find_layer_mut("mask")
                : heightfield.create_layer("mask", 1, 0.0f);
        if (mask.tuple_size != 1)
            return false;
        mask.values = source->values;
    }
    if (options.overwrite_height) {
        data::PcgHeightFieldLayer& height =
            heightfield.find_layer_mut("height")
                ? *heightfield.find_layer_mut("height")
                : heightfield.create_layer("height", 1, 0.0f);
        if (height.tuple_size != 1)
            return false;
        height.values = source->values;
    }
    return true;
}

data::PcgHeightField create_heightfield_from_file(const HeightFieldFileOptions& options)
{
    if (options.file_path.empty() || options.layer_type.empty())
        return {};

    int width = options.raw_resolution_x;
    int height = options.raw_resolution_z;
    std::vector<float> values;
    auto ends_with_ci = [](const std::string& value, const char* suffix) {
        const std::size_t suffix_len = std::char_traits<char>::length(suffix);
        if (value.size() < suffix_len)
            return false;
        for (std::size_t i = 0; i < suffix_len; ++i) {
            const char left = static_cast<char>(
                std::tolower(static_cast<unsigned char>(value[value.size() - suffix_len + i])));
            const char right = static_cast<char>(
                std::tolower(static_cast<unsigned char>(suffix[i])));
            if (left != right)
                return false;
        }
        return true;
    };
    const bool looks_raw = ends_with_ci(options.file_path, ".raw") ||
                           ends_with_ci(options.file_path, ".r32") ||
                           ends_with_ci(options.file_path, ".f32");
    if (looks_raw) {
        if (!load_raw_float32(options.file_path, width, height, values))
            return {};
    } else if (!load_pgm_scalar(options.file_path, width, height, values)) {
        if (width > 0 && height > 0 &&
            load_raw_float32(options.file_path, width, height, values)) {
            // raw fallback with explicit resolution
        } else {
            return {};
        }
    }

    double size_x = options.size;
    double size_z = options.size;
    if (options.size_method == HeightFieldFileSizeMethod::GridSpacing) {
        const bool corner = options.sampling == data::HeightFieldSampling::Corner;
        const int cells_x = width - (corner ? 1 : 0);
        const int cells_z = height - (corner ? 1 : 0);
        if (cells_x <= 0 || cells_z <= 0)
            return {};
        size_x = options.grid_spacing * static_cast<double>(cells_x);
        size_z = options.grid_spacing * static_cast<double>(cells_z);
    } else {
        const double largest = static_cast<double>(std::max(width, height));
        const double scale = options.size / largest;
        size_x = scale * static_cast<double>(width);
        size_z = scale * static_cast<double>(height);
    }
    size_x *= options.uniform_scale;
    size_z *= options.uniform_scale;

    data::PcgHeightField field(width, height, size_x, size_z, options.center,
                               options.sampling, options.orientation);
    field.create_layer("height", 1, 0.0f);
    field.create_layer("mask", 1, 0.0f);
    data::PcgHeightFieldLayer* target =
        field.find_layer_mut(options.layer_type == "mask" ? "mask" : "height");
    if (!target)
        return {};
    for (std::size_t i = 0; i < values.size(); ++i) {
        double sample = static_cast<double>(values[i]) * options.height_scale;
        if (options.clamp_minimum)
            sample = std::max(sample, options.minimum);
        if (options.clamp_maximum)
            sample = std::min(sample, options.maximum);
        target->values[i] = static_cast<float>(sample);
    }
    return field.valid() ? field : data::PcgHeightField{};
}

} // namespace pcg::internal::elements
