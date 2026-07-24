#include "elements/element_utils.hpp"

#include "internal/error_util.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace pcg::internal::elements {
namespace {

constexpr int kPerlinPermSize = 256;

void build_perlin_perm(int seed, int perm[kPerlinPermSize * 2])
{
    int base[kPerlinPermSize];
    for (int i = 0; i < kPerlinPermSize; ++i)
        base[i] = i;

    uint32_t state = static_cast<uint32_t>(seed) * 1664525u + 1013904223u;
    for (int i = kPerlinPermSize - 1; i > 0; --i) {
        state = state * 1664525u + 1013904223u;
        const int j = static_cast<int>(state % static_cast<uint32_t>(i + 1));
        const int tmp = base[i];
        base[i] = base[j];
        base[j] = tmp;
    }

    for (int i = 0; i < kPerlinPermSize; ++i) {
        perm[i] = base[i];
        perm[i + kPerlinPermSize] = base[i];
    }
}

double fade(double t)
{
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
}

double lerp(double a, double b, double t)
{
    return a + t * (b - a);
}

double grad(int hash, double x, double y, double z)
{
    const int h = hash & 15;
    const double u = h < 8 ? x : y;
    const double v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

} // namespace

double perlin_noise_3d(double x, double y, double z, int seed)
{
    int perm[kPerlinPermSize * 2];
    build_perlin_perm(seed, perm);

    const int xi = static_cast<int>(std::floor(x)) & (kPerlinPermSize - 1);
    const int yi = static_cast<int>(std::floor(y)) & (kPerlinPermSize - 1);
    const int zi = static_cast<int>(std::floor(z)) & (kPerlinPermSize - 1);
    const double xf = x - std::floor(x);
    const double yf = y - std::floor(y);
    const double zf = z - std::floor(z);
    const double u = fade(xf);
    const double v = fade(yf);
    const double w = fade(zf);

    const int a = perm[xi] + yi;
    const int aa = perm[a] + zi;
    const int ab = perm[a + 1] + zi;
    const int b = perm[xi + 1] + yi;
    const int ba = perm[b] + zi;
    const int bb = perm[b + 1] + zi;

    const double x1 = lerp(grad(perm[aa], xf, yf, zf), grad(perm[ba], xf - 1.0, yf, zf), u);
    const double x2 = lerp(grad(perm[ab], xf, yf - 1.0, zf), grad(perm[bb], xf - 1.0, yf - 1.0, zf), u);
    const double y1 = lerp(x1, x2, v);
    const double x3 = lerp(grad(perm[aa + 1], xf, yf, zf - 1.0), grad(perm[ba + 1], xf - 1.0, yf, zf - 1.0), u);
    const double x4 = lerp(grad(perm[ab + 1], xf, yf - 1.0, zf - 1.0),
                          grad(perm[bb + 1], xf - 1.0, yf - 1.0, zf - 1.0), u);
    const double y2 = lerp(x3, x4, v);
    return lerp(y1, y2, w);
}

PcgResultCode fail_ctx(PcgContext& ctx, PcgResultCode code, const char* message)
{
    write_error(ctx.err_buf, ctx.err_buf_size, message);
    return code;
}

const nlohmann::json* require_input_json(PcgContext& ctx, const char* pin, const char* node_label)
{
    const nlohmann::json* input = ctx.inputs.find_json(pin);
    if (!input)
        fail_ctx(ctx, PCG_ERR_EXECUTION, node_label);
    return input;
}

data::PcgPointData get_points_input(PcgContext& ctx, const char* pin, const char* label)
{
    if (const data::PcgPointData* points = ctx.inputs.find_points(pin))
        return *points;

    const nlohmann::json* input = require_input_json(ctx, pin, label);
    if (!input)
        return {};
    return parse_point_input(*input);
}

data::PcgSplineData get_splines_input(PcgContext& ctx, const char* pin, const char* label)
{
    if (const data::PcgSplineData* splines = ctx.inputs.find_splines(pin))
        return *splines;

    const nlohmann::json* input = require_input_json(ctx, pin, label);
    if (!input)
        return {};
    return parse_spline_input(*input);
}

data::PcgPointData parse_point_input(const nlohmann::json& json)
{
    return data::PcgPointData::from_json(json);
}

data::PcgSplineData parse_spline_input(const nlohmann::json& json)
{
    return data::PcgSplineData::from_json(json);
}

data::PcgMeshData parse_mesh_input(const nlohmann::json& json)
{
    return data::PcgMeshData::from_json(json);
}

data::PcgMeshData get_mesh_input(PcgContext& ctx, const char* pin, const char* label)
{
    if (const data::PcgMeshData* mesh = ctx.inputs.find_mesh(pin))
        return *mesh;

    if (const data::PcgGeometry* geometry = ctx.inputs.find_geometry(pin))
        return data::compute_split_normals(*geometry,
            data::NormalComputeOptions{geometry->detail().shade_mode,
                                        geometry->detail().cusp_angle_deg, true});

    const nlohmann::json* input = require_input_json(ctx, pin, label);
    if (!input)
        return {};
    return parse_mesh_input(*input);
}

data::PcgGeometry get_geometry_input(PcgContext& ctx, const char* pin, const char* label)
{
    if (const data::PcgGeometry* geometry = ctx.inputs.find_geometry(pin))
        return *geometry;

    const data::PcgMeshData mesh = get_mesh_input(ctx, pin, label);
    if (!mesh.vertices().empty())
        return data::geometry_from_mesh(mesh);

    return {};
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

data::PcgHeightField get_heightfield_input(PcgContext& ctx, const char* pin, const char* label)
{
    if (const data::PcgHeightField* heightfield = ctx.inputs.find_heightfield(pin))
        return *heightfield;

    fail_ctx(ctx, PCG_ERR_EXECUTION, label);
    return {};
}

nlohmann::json point_data_to_json(const data::PcgPointData& data)
{
    return data.to_json();
}

void emit_points(PcgContext& ctx, data::PcgPointData data)
{
    ctx.outputs.add_points("out", std::move(data));
}

void emit_points_with_meta(PcgContext& ctx, data::PcgPointData data, nlohmann::json sidecar)
{
    ctx.outputs.add_points_with_meta("out", std::move(data), std::move(sidecar));
}

void emit_points_shared_with_meta(PcgContext& ctx,
                                  std::shared_ptr<const data::PcgPointData> points,
                                  nlohmann::json sidecar)
{
    ctx.outputs.add_points_shared_with_meta("out", std::move(points), std::move(sidecar));
}

void emit_splines(PcgContext& ctx, data::PcgSplineData data)
{
    ctx.outputs.add_splines("out", std::move(data));
}

void emit_mesh(PcgContext& ctx, data::PcgMeshData data)
{
    ctx.outputs.add_mesh("out", std::move(data));
}

void emit_geometry(PcgContext& ctx, data::PcgGeometry data)
{
    ctx.outputs.add_geometry("out", std::move(data));
}

void emit_geometry_shared(PcgContext& ctx,
                          const std::string& tag,
                          std::shared_ptr<const data::PcgGeometry> geometry)
{
    ctx.outputs.add_geometry_shared(tag, std::move(geometry));
}

void emit_heightfield(PcgContext& ctx, data::PcgHeightField data)
{
    ctx.outputs.add_heightfield("out", std::move(data));
}

void emit_heightfield_shared(PcgContext& ctx,
                             const std::string& tag,
                             std::shared_ptr<const data::PcgHeightField> heightfield)
{
    ctx.outputs.add_heightfield_shared(tag, std::move(heightfield));
}

void emit_mesh_shared(PcgContext& ctx,
                      const std::string& tag,
                      std::shared_ptr<const data::PcgMeshData> mesh)
{
    ctx.outputs.add_mesh_shared(tag, std::move(mesh));
}

uint32_t mix_seed(int a, int b)
{
    return static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b * 2654435761);
}

int normalize_seed_number(double seed)
{
    if (!std::isfinite(seed))
        return 0;

    const double truncated = std::trunc(seed);
    if (seed == truncated &&
        truncated >= static_cast<double>(std::numeric_limits<int>::lowest()) &&
        truncated <= static_cast<double>(std::numeric_limits<int>::max())) {
        return static_cast<int>(truncated);
    }

    // SplitMix64-style mix of IEEE bits so nearby floats diverge.
    static_assert(sizeof(double) == sizeof(std::uint64_t), "unexpected double size");
    std::uint64_t bits = 0;
    std::memcpy(&bits, &seed, sizeof(bits));
    bits ^= bits >> 30;
    bits *= 0xbf58476d1ce4e5b9ULL;
    bits ^= bits >> 27;
    bits *= 0x94d049bb133111ebULL;
    bits ^= bits >> 31;
    return static_cast<int>(static_cast<std::uint32_t>(bits));
}

double read_seed_param_number(const nlohmann::json& data, const char* key, double default_value)
{
    if (key == nullptr || !data.is_object() || !data.contains(key) || data[key].is_null())
        return default_value;

    const auto& value = data.at(key);
    // Accept both integer and float JSON numbers without truncating 2.3 → 2.
    if (value.is_number())
        return value.get<double>();
    return default_value;
}

int read_seed_param(const nlohmann::json& data, const char* key, int default_value)
{
    if (key == nullptr || !data.is_object() || !data.contains(key) || data[key].is_null())
        return default_value;
    return normalize_seed_number(read_seed_param_number(data, key, static_cast<double>(default_value)));
}

uint32_t rng_state_from_seed(double node_seed, int graph_seed)
{
    const int mixed = normalize_seed_number(node_seed) ^ graph_seed;
    uint32_t state = static_cast<uint32_t>(mixed) * 747796405u + 2891336453u;
    if (state == 0)
        state = 0xA5A5A5A5u;
    return state;
}

uint32_t next_rand(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

double simple_noise(double x, double z, int seed)
{
    const double nx = x * 0.1 + static_cast<double>(seed) * 0.013;
    const double nz = z * 0.1 + static_cast<double>(seed) * 0.017;
    return std::sin(nx * 1.7) * std::cos(nz * 1.3) * 0.5 +
           std::sin(nx * 3.1 + nz * 2.4) * 0.25;
}

std::vector<std::string> parse_name_list(const nlohmann::json& data, const char* key)
{
    std::vector<std::string> names;
    if (data.contains(key) && data[key].is_array()) {
        for (const auto& item : data[key]) {
            if (item.is_string())
                names.push_back(item.get<std::string>());
        }
        return names;
    }

    const std::string csv = data.value(key, "");
    std::string current;
    for (char ch : csv) {
        if (ch == ',' || ch == ';') {
            if (!current.empty())
                names.push_back(current);
            current.clear();
        } else if (ch != ' ') {
            current.push_back(ch);
        }
    }
    if (!current.empty())
        names.push_back(current);
    return names;
}

data::PcgVec3 read_vector_param(const nlohmann::json& data,
                                const char* key,
                                const data::PcgVec3& fallback)
{
    if (data.contains(key)) {
        const auto& value = data[key];
        if (value.is_array() && value.size() >= 3 && value[0].is_number() &&
            value[1].is_number() && value[2].is_number()) {
            return {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
        }
        if (value.is_object()) {
            return {value.value("x", fallback.x),
                    value.value("y", fallback.y),
                    value.value("z", fallback.z)};
        }
    }
    const std::string prefix(key);
    return {data.value(prefix + "X", fallback.x),
            data.value(prefix + "Y", fallback.y),
            data.value(prefix + "Z", fallback.z)};
}

} // namespace pcg::internal::elements
