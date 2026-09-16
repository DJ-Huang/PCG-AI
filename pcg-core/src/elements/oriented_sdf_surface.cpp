#include "elements/oriented_sdf_surface.hpp"

#include "elements/assembly_algorithms.hpp"
#include "elements/element_utils.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace pcg::internal::elements {
namespace {

constexpr uint32_t kPayloadVersion = 2;
constexpr size_t kPayloadHeaderBytes = 40;
constexpr size_t kPayloadRecordBytesV1 = 16;
constexpr size_t kPayloadRecordBytes = 20;
constexpr size_t kMaximumBakedPointCount = 500000;
constexpr double kEpsilon = 1.0e-12;

data::PcgVec3 add(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

data::PcgVec3 subtract(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

data::PcgVec3 multiply(const data::PcgVec3& value, double amount)
{
    return {value.x * amount, value.y * amount, value.z * amount};
}

double dot(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

data::PcgVec3 normalize(const data::PcgVec3& value)
{
    const double length = std::sqrt(dot(value, value));
    if (length <= kEpsilon)
        return {};
    return multiply(value, 1.0 / length);
}

data::PcgVec3 cross(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

double clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

void write_u16(std::vector<uint8_t>& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<uint8_t>((value >> 8) & 0xffu));
}

void write_u32(std::vector<uint8_t>& bytes, uint32_t value)
{
    for (int shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<uint8_t>((value >> shift) & 0xffu));
}

void write_f32(std::vector<uint8_t>& bytes, float value)
{
    uint32_t raw = 0;
    static_assert(sizeof(raw) == sizeof(value), "float32 layout");
    std::memcpy(&raw, &value, sizeof(raw));
    write_u32(bytes, raw);
}

uint16_t read_u16(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint16_t>(bytes[offset]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
}

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

float read_f32(const std::vector<uint8_t>& bytes, size_t offset)
{
    const uint32_t raw = read_u32(bytes, offset);
    float value = 0.0f;
    std::memcpy(&value, &raw, sizeof(value));
    return value;
}

std::string encode_base64(const std::vector<uint8_t>& input)
{
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        const uint32_t a = input[i];
        const uint32_t b = i + 1 < input.size() ? input[i + 1] : 0;
        const uint32_t c = i + 2 < input.size() ? input[i + 2] : 0;
        const uint32_t value = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(value >> 18) & 63u]);
        output.push_back(alphabet[(value >> 12) & 63u]);
        output.push_back(i + 1 < input.size() ? alphabet[(value >> 6) & 63u] : '=');
        output.push_back(i + 2 < input.size() ? alphabet[value & 63u] : '=');
    }
    return output;
}

int base64_value(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool decode_base64(const std::string& input, std::vector<uint8_t>& output)
{
    output.clear();
    output.reserve(input.size() * 3 / 4);
    uint32_t value = 0;
    int bits = -8;
    bool padded = false;
    for (unsigned char c : input) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        if (c == '=') {
            padded = true;
            continue;
        }
        if (padded)
            return false;
        const int decoded = base64_value(c);
        if (decoded < 0)
            return false;
        value = (value << 6) | static_cast<uint32_t>(decoded);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<uint8_t>((value >> bits) & 0xffu));
            bits -= 8;
        }
    }
    return true;
}

data::PcgVec3 minimum_of(const std::vector<OrientedSurfacePoint>& points)
{
    const double maximum = std::numeric_limits<double>::max();
    data::PcgVec3 result{maximum, maximum, maximum};
    for (const auto& point : points) {
        result.x = std::min(result.x, point.position.x);
        result.y = std::min(result.y, point.position.y);
        result.z = std::min(result.z, point.position.z);
    }
    return result;
}

data::PcgVec3 maximum_of(const std::vector<OrientedSurfacePoint>& points)
{
    const double maximum = std::numeric_limits<double>::max();
    data::PcgVec3 result{-maximum, -maximum, -maximum};
    for (const auto& point : points) {
        result.x = std::max(result.x, point.position.x);
        result.y = std::max(result.y, point.position.y);
        result.z = std::max(result.z, point.position.z);
    }
    return result;
}

std::vector<data::PcgVec3> compute_point_normals(const data::PcgGeometry& geometry)
{
    std::vector<data::PcgVec3> normals(geometry.points().size());
    for (const auto& face : geometry.faces()) {
        if (face.size() < 3)
            continue;
        const int root = face[0];
        for (size_t corner = 1; corner + 1 < face.size(); ++corner) {
            const int b = face[corner];
            const int c = face[corner + 1];
            if (root < 0 || b < 0 || c < 0 ||
                static_cast<size_t>(root) >= geometry.points().size() ||
                static_cast<size_t>(b) >= geometry.points().size() ||
                static_cast<size_t>(c) >= geometry.points().size())
                continue;
            const auto normal = cross(
                subtract(geometry.points()[static_cast<size_t>(b)],
                         geometry.points()[static_cast<size_t>(root)]),
                subtract(geometry.points()[static_cast<size_t>(c)],
                         geometry.points()[static_cast<size_t>(root)]));
            normals[static_cast<size_t>(root)] = add(normals[static_cast<size_t>(root)], normal);
            normals[static_cast<size_t>(b)] = add(normals[static_cast<size_t>(b)], normal);
            normals[static_cast<size_t>(c)] = add(normals[static_cast<size_t>(c)], normal);
        }
    }
    data::PcgVec3 center{};
    for (const auto& point : geometry.points()) center = add(center, point);
    if (!geometry.points().empty()) center = multiply(center, 1.0 / geometry.points().size());
    for (size_t i = 0; i < normals.size(); ++i) {
        normals[i] = normalize(normals[i]);
        if (dot(normals[i], normals[i]) <= kEpsilon)
            normals[i] = normalize(subtract(geometry.points()[i], center));
    }
    return normals;
}

struct GridCoord {
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const GridCoord& other) const
    {
        return x == other.x && y == other.y && z == other.z;
    }
    bool operator<(const GridCoord& other) const
    {
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
        return z < other.z;
    }
};

struct GridCoordHash {
    size_t operator()(const GridCoord& key) const noexcept
    {
        uint64_t value = static_cast<uint32_t>(key.x) * 0x9e3779b185ebca87ULL;
        value ^= static_cast<uint32_t>(key.y) + 0x9e3779b9ULL + (value << 6) + (value >> 2);
        value ^= static_cast<uint32_t>(key.z) + 0x85ebca6bULL + (value << 6) + (value >> 2);
        return static_cast<size_t>(value);
    }
};

struct FieldAccum {
    float weight = 0.0f;
    float signed_distance = 0.0f;
    float nearest_distance_squared = std::numeric_limits<float>::infinity();
    uint32_t nearest_sample = std::numeric_limits<uint32_t>::max();
};

using FieldMap = std::unordered_map<GridCoord, FieldAccum, GridCoordHash>;

bool in_grid(const GridCoord& value, const std::array<int, 3>& dims)
{
    return value.x >= 0 && value.y >= 0 && value.z >= 0 &&
           value.x < dims[0] && value.y < dims[1] && value.z < dims[2];
}

bool in_cell_grid(const GridCoord& value, const std::array<int, 3>& dims)
{
    return value.x >= 0 && value.y >= 0 && value.z >= 0 &&
           value.x + 1 < dims[0] && value.y + 1 < dims[1] && value.z + 1 < dims[2];
}

float field_value(const FieldMap& field, const GridCoord& key, float outside)
{
    const auto found = field.find(key);
    if (found == field.end() || found->second.weight <= 1.0e-6f)
        return outside;
    return found->second.signed_distance / found->second.weight;
}

std::string embedded_texture_mime(const aiTexture& texture)
{
    std::string hint(texture.achFormatHint);
    std::transform(hint.begin(), hint.end(), hint.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    if (hint == "jpg" || hint == "jpeg") return "image/jpeg";
    if (hint == "webp") return "image/webp";
    if (hint == "ktx2") return "image/ktx2";
    return "image/png";
}

bool copy_embedded_texture(const aiScene& scene,
                           const aiMaterial& material,
                           std::initializer_list<aiTextureType> types,
                           EmbeddedTexturePayload& output)
{
    for (const aiTextureType type : types) {
        if (material.GetTextureCount(type) == 0)
            continue;
        aiString reference;
        if (material.GetTexture(type, 0, &reference) != AI_SUCCESS)
            continue;
        const aiTexture* texture = scene.GetEmbeddedTexture(reference.C_Str());
        if (!texture || texture->mHeight != 0 || texture->mWidth == 0 || !texture->pcData)
            continue;
        const auto* begin = reinterpret_cast<const uint8_t*>(texture->pcData);
        output.bytes.assign(begin, begin + texture->mWidth);
        output.mime_type = embedded_texture_mime(*texture);
        return true;
    }
    return false;
}

class OrientedSdfSurfaceElement final : public IPcgElement {
public:
    const char* type_name() const override { return "OrientedSdfSurface"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "OrientedSdfSurface missing node data");
        const std::string payload = ctx.node->data.value("pointCloud", std::string{});
        if (payload.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "OrientedSdfSurface has no baked oriented point cloud");

        std::vector<OrientedSurfacePoint> points;
        OrientedPointCloudStats stats;
        std::string error;
        if (!decode_oriented_point_cloud(payload, points, stats, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());

        OrientedSdfOptions options;
        options.cell_size = ctx.node->data.value("cellSize", 0.004);
        options.support_radius_cells = ctx.node->data.value("supportRadiusCells", 2.5);
        options.iso_offset = ctx.node->data.value("isoOffset", 0.0);
        const int64_t requested_max = ctx.node->data.value("maxActiveCells", int64_t{3000000});
        options.max_active_cells = static_cast<size_t>(std::max<int64_t>(1000, requested_max));
        options.transfer_colors = ctx.node->data.value("transferColors", true);
        options.transfer_uvs = ctx.node->data.value("transferUvs", true);
        options.flip_uv_v = ctx.node->data.value("flipUvV", false);

        data::PcgGeometry geometry;
        const auto cancelled = [&ctx]() {
            return ctx.is_cancel_requested && ctx.is_cancel_requested();
        };
        if (!reconstruct_oriented_sdf_surface(points, options, geometry, error, cancelled))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
};

} // namespace

bool encode_oriented_point_cloud(const std::vector<OrientedSurfacePoint>& points,
                                 bool has_source_colors,
                                 bool has_source_uvs,
                                 std::string& base64,
                                 OrientedPointCloudStats& stats,
                                 std::string& error)
{
    base64.clear();
    stats = {};
    if (points.empty()) {
        error = "oriented point cloud is empty";
        return false;
    }
    if (points.size() > std::numeric_limits<uint32_t>::max()) {
        error = "oriented point cloud exceeds the OPC1 point limit";
        return false;
    }
    for (const auto& point : points) {
        const auto& p = point.position;
        const auto& n = point.normal;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) ||
            !std::isfinite(n.x) || !std::isfinite(n.y) || !std::isfinite(n.z) ||
            (has_source_uvs && (!std::isfinite(point.uv.u) || !std::isfinite(point.uv.v)))) {
            error = "oriented point cloud contains a non-finite position or normal";
            return false;
        }
        if (dot(n, n) <= kEpsilon) {
            error = "oriented point cloud contains a zero-length normal";
            return false;
        }
    }

    const data::PcgVec3 minimum = minimum_of(points);
    const data::PcgVec3 maximum = maximum_of(points);
    const data::PcgVec3 extent = subtract(maximum, minimum);
    std::vector<uint8_t> bytes;
    bytes.reserve(kPayloadHeaderBytes + points.size() * kPayloadRecordBytes);
    bytes.insert(bytes.end(), {'O', 'P', 'C', '1'});
    write_u32(bytes, kPayloadVersion);
    write_u32(bytes, (has_source_colors ? 1u : 0u) | (has_source_uvs ? 2u : 0u));
    write_u32(bytes, static_cast<uint32_t>(points.size()));
    for (double value : {minimum.x, minimum.y, minimum.z,
                         maximum.x, maximum.y, maximum.z})
        write_f32(bytes, static_cast<float>(value));

    const auto quantize_position = [](double value, double low, double span) {
        if (span <= kEpsilon) return uint16_t{0};
        return static_cast<uint16_t>(std::lround(clamp01((value - low) / span) * 65535.0));
    };
    const auto quantize_normal = [](double value) {
        const int rounded = static_cast<int>(std::lround(std::max(-1.0, std::min(1.0, value)) * 32767.0));
        return static_cast<uint16_t>(static_cast<int16_t>(rounded));
    };
    const auto quantize_color = [](double value) {
        return static_cast<uint8_t>(std::lround(clamp01(value) * 255.0));
    };
    for (const auto& sample : points) {
        const auto normal = normalize(sample.normal);
        write_u16(bytes, quantize_position(sample.position.x, minimum.x, extent.x));
        write_u16(bytes, quantize_position(sample.position.y, minimum.y, extent.y));
        write_u16(bytes, quantize_position(sample.position.z, minimum.z, extent.z));
        write_u16(bytes, quantize_normal(normal.x));
        write_u16(bytes, quantize_normal(normal.y));
        write_u16(bytes, quantize_normal(normal.z));
        bytes.push_back(quantize_color(sample.color.r));
        bytes.push_back(quantize_color(sample.color.g));
        bytes.push_back(quantize_color(sample.color.b));
        bytes.push_back(quantize_color(sample.color.a));
        write_u16(bytes, quantize_position(sample.uv.u, 0.0, 1.0));
        write_u16(bytes, quantize_position(sample.uv.v, 0.0, 1.0));
    }
    base64 = encode_base64(bytes);
    stats.point_count = points.size();
    stats.minimum = minimum;
    stats.maximum = maximum;
    stats.has_source_colors = has_source_colors;
    stats.has_source_uvs = has_source_uvs;
    stats.payload_bytes = bytes.size();
    return true;
}

bool decode_oriented_point_cloud(const std::string& base64,
                                 std::vector<OrientedSurfacePoint>& points,
                                 OrientedPointCloudStats& stats,
                                 std::string& error)
{
    points.clear();
    stats = {};
    std::vector<uint8_t> bytes;
    if (!decode_base64(base64, bytes)) {
        error = "OrientedSdfSurface pointCloud is not valid base64";
        return false;
    }
    if (bytes.size() < kPayloadHeaderBytes || bytes[0] != 'O' || bytes[1] != 'P' ||
        bytes[2] != 'C' || bytes[3] != '1') {
        error = "OrientedSdfSurface pointCloud has invalid OPC1 magic";
        return false;
    }
    const uint32_t version = read_u32(bytes, 4);
    if (version != 1 && version != kPayloadVersion) {
        error = "OrientedSdfSurface pointCloud uses an unsupported OPC1 version";
        return false;
    }
    const uint32_t flags = read_u32(bytes, 8);
    const uint32_t count = read_u32(bytes, 12);
    const size_t record_bytes = version == 1 ? kPayloadRecordBytesV1 : kPayloadRecordBytes;
    const size_t expected = kPayloadHeaderBytes + static_cast<size_t>(count) * record_bytes;
    if (bytes.size() != expected || count == 0) {
        error = "OrientedSdfSurface pointCloud byte count does not match its header";
        return false;
    }
    const data::PcgVec3 minimum{read_f32(bytes, 16), read_f32(bytes, 20), read_f32(bytes, 24)};
    const data::PcgVec3 maximum{read_f32(bytes, 28), read_f32(bytes, 32), read_f32(bytes, 36)};
    const data::PcgVec3 extent = subtract(maximum, minimum);
    const auto dequantize_position = [](uint16_t value, double low, double span) {
        return low + static_cast<double>(value) / 65535.0 * span;
    };
    const auto dequantize_normal = [](uint16_t value) {
        const auto signed_value = static_cast<int16_t>(value);
        return std::max(-1.0, static_cast<double>(signed_value) / 32767.0);
    };
    points.reserve(count);
    size_t offset = kPayloadHeaderBytes;
    for (uint32_t i = 0; i < count; ++i, offset += record_bytes) {
        OrientedSurfacePoint point;
        point.position = {
            dequantize_position(read_u16(bytes, offset), minimum.x, extent.x),
            dequantize_position(read_u16(bytes, offset + 2), minimum.y, extent.y),
            dequantize_position(read_u16(bytes, offset + 4), minimum.z, extent.z),
        };
        point.normal = normalize({
            dequantize_normal(read_u16(bytes, offset + 6)),
            dequantize_normal(read_u16(bytes, offset + 8)),
            dequantize_normal(read_u16(bytes, offset + 10)),
        });
        point.color = {
            bytes[offset + 12] / 255.0,
            bytes[offset + 13] / 255.0,
            bytes[offset + 14] / 255.0,
            bytes[offset + 15] / 255.0,
        };
        if (version >= 2) {
            point.uv = {
                static_cast<double>(read_u16(bytes, offset + 16)) / 65535.0,
                static_cast<double>(read_u16(bytes, offset + 18)) / 65535.0,
            };
        }
        if (dot(point.normal, point.normal) <= kEpsilon) {
            error = "OrientedSdfSurface pointCloud decoded a zero-length normal";
            points.clear();
            return false;
        }
        points.push_back(point);
    }
    stats.point_count = count;
    stats.minimum = minimum;
    stats.maximum = maximum;
    stats.has_source_colors = (flags & 1u) != 0;
    stats.has_source_uvs = (flags & 2u) != 0;
    stats.payload_bytes = bytes.size();
    return true;
}

bool sample_oriented_geometry(const data::PcgGeometry& geometry,
                              double sample_spacing,
                              std::vector<OrientedSurfacePoint>& points,
                              OrientedPointCloudStats& stats,
                              std::string& error)
{
    points.clear();
    stats = {};
    if (geometry.points().empty()) {
        error = "source geometry has no points";
        return false;
    }

    std::vector<data::PcgVec3> normals;
    const auto* normal_attribute = geometry.attributes().find(data::AttributeOwner::Point, "N");
    if (normal_attribute && normal_attribute->schema().type == data::AttributeType::Float &&
        normal_attribute->schema().tuple_size >= 3 &&
        normal_attribute->size() == geometry.points().size()) {
        normals.reserve(geometry.points().size());
        const auto& values = normal_attribute->float_values();
        const size_t width = static_cast<size_t>(normal_attribute->schema().tuple_size);
        for (size_t i = 0; i < geometry.points().size(); ++i)
            normals.push_back(normalize({values[i * width], values[i * width + 1], values[i * width + 2]}));
    } else {
        normals = compute_point_normals(geometry);
    }

    const bool has_colors = geometry.has_colors() &&
                            geometry.colors().size() == geometry.points().size();
    const bool has_uvs = geometry.has_uvs() &&
                         geometry.uvs().size() == geometry.points().size();
    data::PcgVec3 source_minimum = geometry.points().front();
    data::PcgVec3 source_maximum = geometry.points().front();
    for (const auto& point : geometry.points()) {
        source_minimum.x = std::min(source_minimum.x, point.x);
        source_minimum.y = std::min(source_minimum.y, point.y);
        source_minimum.z = std::min(source_minimum.z, point.z);
        source_maximum.x = std::max(source_maximum.x, point.x);
        source_maximum.y = std::max(source_maximum.y, point.y);
        source_maximum.z = std::max(source_maximum.z, point.z);
    }
    const double largest_extent = std::max({source_maximum.x - source_minimum.x,
                                             source_maximum.y - source_minimum.y,
                                             source_maximum.z - source_minimum.z});
    double effective_spacing = sample_spacing;
    if (!std::isfinite(effective_spacing) || effective_spacing <= 0.0)
        effective_spacing = std::max(0.00025, largest_extent / 444.0);

    struct Triangle {
        int a = 0;
        int b = 0;
        int c = 0;
    };
    std::vector<Triangle> triangles;
    triangles.reserve(geometry.faces().size() * 2);
    for (const auto& face : geometry.faces()) {
        const auto local_triangles = data::triangulate_face_corners(geometry.points(), face);
        for (const auto& local : local_triangles) {
            const int a = face[static_cast<size_t>(local[0])];
            const int b = face[static_cast<size_t>(local[1])];
            const int c = face[static_cast<size_t>(local[2])];
            if (a < 0 || b < 0 || c < 0 ||
                static_cast<size_t>(a) >= geometry.points().size() ||
                static_cast<size_t>(b) >= geometry.points().size() ||
                static_cast<size_t>(c) >= geometry.points().size())
                continue;
            triangles.push_back({a, b, c});
        }
    }

    const auto distance = [&](int lhs, int rhs) {
        const auto delta = subtract(geometry.points()[static_cast<size_t>(lhs)],
                                    geometry.points()[static_cast<size_t>(rhs)]);
        return std::sqrt(dot(delta, delta));
    };
    const auto triangle_sample_count = [&](const Triangle& triangle, double spacing) {
        const auto ab = subtract(geometry.points()[static_cast<size_t>(triangle.b)],
                                 geometry.points()[static_cast<size_t>(triangle.a)]);
        const auto ac = subtract(geometry.points()[static_cast<size_t>(triangle.c)],
                                 geometry.points()[static_cast<size_t>(triangle.a)]);
        const double area = 0.5 * std::sqrt(dot(cross(ab, ac), cross(ab, ac)));
        const double longest = std::max({distance(triangle.a, triangle.b),
                                         distance(triangle.b, triangle.c),
                                         distance(triangle.c, triangle.a)});
        const size_t area_samples = static_cast<size_t>(std::ceil(
            area / std::max(0.5 * spacing * spacing, kEpsilon)));
        const size_t span_samples = static_cast<size_t>(std::max(
            1.0, std::ceil(longest / spacing) - 1.0));
        return std::max(area_samples, span_samples);
    };
    const auto measured_count = [&](double spacing) {
        size_t count = geometry.points().size();
        for (const auto& triangle : triangles) {
            count += triangle_sample_count(triangle, spacing);
            if (count > kMaximumBakedPointCount * 4)
                break;
        }
        return count;
    };
    for (int attempt = 0; attempt < 8; ++attempt) {
        const size_t estimated = measured_count(effective_spacing);
        if (estimated <= kMaximumBakedPointCount)
            break;
        effective_spacing *= std::sqrt(static_cast<double>(estimated) /
                                       static_cast<double>(kMaximumBakedPointCount)) * 1.02;
    }
    const size_t total_count = measured_count(effective_spacing);
    if (total_count > kMaximumBakedPointCount) {
        error = "surface sampling exceeds the topology-free OPC1 safety limit";
        return false;
    }

    points.reserve(total_count);
    for (size_t i = 0; i < geometry.points().size(); ++i) {
        points.push_back({geometry.points()[i], normals[i],
                          has_colors ? geometry.colors()[i] : data::PcgColor{},
                          has_uvs ? geometry.uvs()[i] : data::PcgVec2{}});
    }
    const auto radical_inverse_base2 = [](uint32_t bits) {
        bits = (bits << 16u) | (bits >> 16u);
        bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
        bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
        bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
        bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
        return static_cast<double>(bits) * 2.3283064365386963e-10;
    };
    for (size_t triangle_index = 0; triangle_index < triangles.size(); ++triangle_index) {
        const auto& triangle = triangles[triangle_index];
        const size_t sample_count = triangle_sample_count(triangle, effective_spacing);
        const size_t ia = static_cast<size_t>(triangle.a);
        const size_t ib = static_cast<size_t>(triangle.b);
        const size_t ic = static_cast<size_t>(triangle.c);
        for (size_t sample_index = 0; sample_index < sample_count; ++sample_index) {
                // Deterministic Hammersley samples cover triangle interiors
                // without the quadratic over-sampling of a barycentric grid.
                const double u = (static_cast<double>(sample_index) + 0.5) /
                                 static_cast<double>(sample_count);
                const uint32_t sequence = static_cast<uint32_t>(sample_index) ^
                    static_cast<uint32_t>(triangle_index * 0x9e3779b9u);
                const double v = radical_inverse_base2(sequence);
                const double root_u = std::sqrt(u);
                const double a = 1.0 - root_u;
                const double b = root_u * (1.0 - v);
                const double c = root_u * v;
                const auto blend_vec3 = [&](const data::PcgVec3& va,
                                            const data::PcgVec3& vb,
                                            const data::PcgVec3& vc) {
                    return data::PcgVec3{va.x * a + vb.x * b + vc.x * c,
                                         va.y * a + vb.y * b + vc.y * c,
                                         va.z * a + vb.z * b + vc.z * c};
                };
                const data::PcgColor color{
                    (has_colors ? geometry.colors()[ia].r : 1.0) * a +
                        (has_colors ? geometry.colors()[ib].r : 1.0) * b +
                        (has_colors ? geometry.colors()[ic].r : 1.0) * c,
                    (has_colors ? geometry.colors()[ia].g : 1.0) * a +
                        (has_colors ? geometry.colors()[ib].g : 1.0) * b +
                        (has_colors ? geometry.colors()[ic].g : 1.0) * c,
                    (has_colors ? geometry.colors()[ia].b : 1.0) * a +
                        (has_colors ? geometry.colors()[ib].b : 1.0) * b +
                        (has_colors ? geometry.colors()[ic].b : 1.0) * c,
                    (has_colors ? geometry.colors()[ia].a : 1.0) * a +
                        (has_colors ? geometry.colors()[ib].a : 1.0) * b +
                        (has_colors ? geometry.colors()[ic].a : 1.0) * c,
                };
                const data::PcgVec2 uv{
                    (has_uvs ? geometry.uvs()[ia].u : 0.0) * a +
                        (has_uvs ? geometry.uvs()[ib].u : 0.0) * b +
                        (has_uvs ? geometry.uvs()[ic].u : 0.0) * c,
                    (has_uvs ? geometry.uvs()[ia].v : 0.0) * a +
                        (has_uvs ? geometry.uvs()[ib].v : 0.0) * b +
                        (has_uvs ? geometry.uvs()[ic].v : 0.0) * c,
                };
                points.push_back({blend_vec3(geometry.points()[ia], geometry.points()[ib], geometry.points()[ic]),
                                  normalize(blend_vec3(normals[ia], normals[ib], normals[ic])),
                                  color,
                                  uv});
        }
    }

    stats.point_count = points.size();
    stats.source_point_count = geometry.points().size();
    stats.minimum = source_minimum;
    stats.maximum = source_maximum;
    stats.has_source_colors = has_colors;
    stats.has_source_uvs = has_uvs;
    stats.sampling_spacing = effective_spacing;
    return true;
}

bool encode_oriented_point_cloud_file(const std::string& path,
                                      std::string& base64,
                                      OrientedPointCloudStats& stats,
                                      std::string& error,
                                      double sample_spacing)
{
    data::PcgGeometry geometry;
    ImportMeshOptions import_options;
    if (!import_geometry_file(path, import_options, geometry, error))
        return false;
    std::vector<OrientedSurfacePoint> points;
    OrientedPointCloudStats sampled;
    if (!sample_oriented_geometry(geometry, sample_spacing, points, sampled, error))
        return false;
    OrientedPointCloudStats encoded;
    if (!encode_oriented_point_cloud(points, sampled.has_source_colors,
                                     sampled.has_source_uvs, base64, encoded, error))
        return false;
    encoded.source_point_count = sampled.source_point_count;
    encoded.sampling_spacing = sampled.sampling_spacing;
    stats = encoded;
    return true;
}

bool extract_embedded_pbr_textures_file(const std::string& path,
                                        EmbeddedPbrTextures& textures,
                                        std::string& error)
{
    textures = {};
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path, aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                  aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode) {
        error = "Embedded texture Assimp error: " + std::string(importer.GetErrorString());
        return false;
    }
    if (scene->mNumMaterials == 0 || !scene->mMaterials[0])
        return true;
    const aiMaterial& material = *scene->mMaterials[0];
    copy_embedded_texture(*scene, material,
                          {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE},
                          textures.base_color);
    copy_embedded_texture(*scene, material,
                          {aiTextureType_NORMAL_CAMERA, aiTextureType_NORMALS},
                          textures.normal);
    copy_embedded_texture(*scene, material,
                          {aiTextureType_AMBIENT_OCCLUSION, aiTextureType_METALNESS,
                           aiTextureType_DIFFUSE_ROUGHNESS},
                          textures.orm);
    return true;
}

bool reconstruct_oriented_sdf_surface(const std::vector<OrientedSurfacePoint>& points,
                                      const OrientedSdfOptions& options,
                                      data::PcgGeometry& output,
                                      std::string& error,
                                      const std::function<bool()>& cancelled)
{
    output = {};
    if (points.empty()) {
        error = "OrientedSdfSurface has no samples";
        return false;
    }
    if (!std::isfinite(options.cell_size) || options.cell_size <= 0.0) {
        error = "OrientedSdfSurface cellSize must be finite and greater than zero";
        return false;
    }
    if (!std::isfinite(options.support_radius_cells) ||
        options.support_radius_cells < 1.25 || options.support_radius_cells > 6.0) {
        error = "OrientedSdfSurface supportRadiusCells must be in [1.25, 6]";
        return false;
    }
    if (options.max_active_cells < 1000) {
        error = "OrientedSdfSurface maxActiveCells must be at least 1000";
        return false;
    }

    const double radius = options.cell_size * options.support_radius_cells;
    if (!std::isfinite(options.iso_offset) || std::abs(options.iso_offset) >= radius * 0.95) {
        error = "OrientedSdfSurface isoOffset must stay inside the supported SDF band";
        return false;
    }
    const double padding = radius * 2.0;
    const data::PcgVec3 point_minimum = minimum_of(points);
    const data::PcgVec3 point_maximum = maximum_of(points);
    const data::PcgVec3 origin = subtract(point_minimum, {padding, padding, padding});
    const data::PcgVec3 high = add(point_maximum, {padding, padding, padding});
    const auto dimension = [&](double hi, double lo) {
        const double cells = std::ceil((hi - lo) / options.cell_size) + 1.0;
        if (!std::isfinite(cells) || cells < 2.0 || cells > 1000000.0)
            return -1;
        return static_cast<int>(cells);
    };
    const std::array<int, 3> dims{
        dimension(high.x, origin.x), dimension(high.y, origin.y), dimension(high.z, origin.z)};
    if (dims[0] < 2 || dims[1] < 2 || dims[2] < 2) {
        error = "OrientedSdfSurface grid dimensions are invalid; increase cellSize";
        return false;
    }

    FieldMap field;
    const size_t reserve_hint = std::min(options.max_active_cells * 2,
                                         std::max<size_t>(points.size() * 24, 1024));
    field.reserve(reserve_hint);
    const int kernel = static_cast<int>(std::ceil(options.support_radius_cells));
    const double radius_squared = radius * radius;
    for (size_t point_index = 0; point_index < points.size(); ++point_index) {
        if ((point_index & 1023u) == 0 && cancelled && cancelled()) {
            error = "OrientedSdfSurface reconstruction cancelled while splatting the SDF";
            return false;
        }
        const auto& sample = points[point_index];
        const auto normal = normalize(sample.normal);
        const GridCoord base{
            static_cast<int>(std::floor((sample.position.x - origin.x) / options.cell_size)),
            static_cast<int>(std::floor((sample.position.y - origin.y) / options.cell_size)),
            static_cast<int>(std::floor((sample.position.z - origin.z) / options.cell_size)),
        };
        for (int dx = -kernel; dx <= kernel; ++dx) {
            for (int dy = -kernel; dy <= kernel; ++dy) {
                for (int dz = -kernel; dz <= kernel; ++dz) {
                    const GridCoord key{base.x + dx, base.y + dy, base.z + dz};
                    if (!in_grid(key, dims))
                        continue;
                    const data::PcgVec3 cell_position{
                        origin.x + key.x * options.cell_size,
                        origin.y + key.y * options.cell_size,
                        origin.z + key.z * options.cell_size,
                    };
                    const auto delta = subtract(cell_position, sample.position);
                    const double distance_squared = dot(delta, delta);
                    if (distance_squared > radius_squared)
                        continue;
                    const double falloff = 1.0 - distance_squared / radius_squared;
                    const float weight = static_cast<float>(falloff * falloff);
                    auto& sum = field[key];
                    sum.weight += weight;
                    sum.signed_distance += static_cast<float>(weight * dot(delta, normal));
                    const float sample_distance_squared = static_cast<float>(distance_squared);
                    if (sample_distance_squared < sum.nearest_distance_squared ||
                        (sample_distance_squared == sum.nearest_distance_squared &&
                         point_index < sum.nearest_sample)) {
                        sum.nearest_distance_squared = sample_distance_squared;
                        sum.nearest_sample = static_cast<uint32_t>(point_index);
                    }
                }
            }
        }
    }
    if (field.empty()) {
        error = "OrientedSdfSurface SDF splat produced no supported grid points";
        return false;
    }

    const float outside = static_cast<float>(radius);
    const float iso = static_cast<float>(options.iso_offset);
    std::unordered_set<GridCoord, GridCoordHash> candidate_set;
    candidate_set.reserve(std::min(options.max_active_cells * 4, field.size() * 4));
    for (const auto& [key, sum] : field) {
        if (sum.weight <= 1.0e-6f || sum.signed_distance / sum.weight > iso)
            continue;
        for (int dx = 0; dx <= 1; ++dx)
            for (int dy = 0; dy <= 1; ++dy)
                for (int dz = 0; dz <= 1; ++dz) {
                    const GridCoord cell{key.x - dx, key.y - dy, key.z - dz};
                    if (in_cell_grid(cell, dims)) candidate_set.insert(cell);
                }
        if (candidate_set.size() > options.max_active_cells * 8) {
            error = "OrientedSdfSurface candidate grid exceeds the safety limit; increase cellSize";
            return false;
        }
    }

    std::vector<GridCoord> candidates(candidate_set.begin(), candidate_set.end());
    std::sort(candidates.begin(), candidates.end());
    std::unordered_map<GridCoord, int, GridCoordHash> active_vertex;
    active_vertex.reserve(std::min(candidates.size(), options.max_active_cells));
    std::vector<data::PcgColor> colors;
    colors.reserve(std::min(candidates.size(), options.max_active_cells));
    std::vector<data::PcgVec2> uvs;
    uvs.reserve(std::min(candidates.size(), options.max_active_cells));
    std::vector<double> transferred_normals;
    transferred_normals.reserve(std::min(candidates.size(), options.max_active_cells) * 3);

    static constexpr std::array<std::array<std::array<int, 3>, 2>, 12> edges{{
        {{{0, 0, 0}, {1, 0, 0}}}, {{{0, 1, 0}, {1, 1, 0}}},
        {{{0, 0, 1}, {1, 0, 1}}}, {{{0, 1, 1}, {1, 1, 1}}},
        {{{0, 0, 0}, {0, 1, 0}}}, {{{1, 0, 0}, {1, 1, 0}}},
        {{{0, 0, 1}, {0, 1, 1}}}, {{{1, 0, 1}, {1, 1, 1}}},
        {{{0, 0, 0}, {0, 0, 1}}}, {{{1, 0, 0}, {1, 0, 1}}},
        {{{0, 1, 0}, {0, 1, 1}}}, {{{1, 1, 0}, {1, 1, 1}}},
    }};
    const double guard = 1.0 / (255.0 * 4.0);
    for (size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
        if ((candidate_index & 4095u) == 0 && cancelled && cancelled()) {
            error = "OrientedSdfSurface reconstruction cancelled while contouring cells";
            return false;
        }
        const GridCoord cell = candidates[candidate_index];
        std::array<float, 8> corner_values{};
        int corner_index = 0;
        bool has_positive = false;
        bool has_negative = false;
        for (int dx = 0; dx <= 1; ++dx)
            for (int dy = 0; dy <= 1; ++dy)
                for (int dz = 0; dz <= 1; ++dz) {
                    const float value = field_value(field, {cell.x + dx, cell.y + dy, cell.z + dz}, outside);
                    corner_values[static_cast<size_t>(corner_index++)] = value;
                    has_positive |= value > iso;
                    has_negative |= value <= iso;
                }
        if (!has_positive || !has_negative)
            continue;
        if (active_vertex.size() >= options.max_active_cells) {
            error = "OrientedSdfSurface active cell limit exceeded; increase cellSize or maxActiveCells";
            return false;
        }

        data::PcgVec3 fraction{};
        int crossings = 0;
        for (const auto& edge : edges) {
            const auto& a = edge[0];
            const auto& b = edge[1];
            const GridCoord key_a{cell.x + a[0], cell.y + a[1], cell.z + a[2]};
            const GridCoord key_b{cell.x + b[0], cell.y + b[1], cell.z + b[2]};
            const float fa = field_value(field, key_a, outside);
            const float fb = field_value(field, key_b, outside);
            if ((fa > iso) == (fb > iso))
                continue;
            const double denominator = static_cast<double>(fa) - fb;
            const double t = std::max(0.0, std::min(1.0,
                std::abs(denominator) <= kEpsilon ? 0.5 :
                (static_cast<double>(fa) - iso) / denominator));
            fraction.x += a[0] + t * (b[0] - a[0]);
            fraction.y += a[1] + t * (b[1] - a[1]);
            fraction.z += a[2] + t * (b[2] - a[2]);
            ++crossings;
        }
        if (crossings == 0)
            continue;
        const double inverse = 1.0 / crossings;
        fraction = multiply(fraction, inverse);
        fraction.x = std::max(guard, std::min(1.0 - guard, fraction.x));
        fraction.y = std::max(guard, std::min(1.0 - guard, fraction.y));
        fraction.z = std::max(guard, std::min(1.0 - guard, fraction.z));
        const data::PcgVec3 vertex_position{
            origin.x + (cell.x + fraction.x) * options.cell_size,
            origin.y + (cell.y + fraction.y) * options.cell_size,
            origin.z + (cell.z + fraction.z) * options.cell_size,
        };
        const int vertex = static_cast<int>(output.points().size());
        output.points_mut().push_back(vertex_position);
        active_vertex.emplace(cell, vertex);

        // UV coordinates are discontinuous across atlas seams and must never be
        // averaged like the continuous SDF.  Pick the closest dense source
        // measurement from the cell corners, using the local SDF gradient to
        // reject nearby samples from an oppositely-facing sheet.
        const auto corner = [&](int dx, int dy, int dz) -> float {
            return corner_values[static_cast<size_t>(dx * 4 + dy * 2 + dz)];
        };
        const data::PcgVec3 gradient = normalize({
            0.25 * ((corner(1, 0, 0) + corner(1, 0, 1) + corner(1, 1, 0) + corner(1, 1, 1)) -
                    (corner(0, 0, 0) + corner(0, 0, 1) + corner(0, 1, 0) + corner(0, 1, 1))),
            0.25 * ((corner(0, 1, 0) + corner(0, 1, 1) + corner(1, 1, 0) + corner(1, 1, 1)) -
                    (corner(0, 0, 0) + corner(0, 0, 1) + corner(1, 0, 0) + corner(1, 0, 1))),
            0.25 * ((corner(0, 0, 1) + corner(0, 1, 1) + corner(1, 0, 1) + corner(1, 1, 1)) -
                    (corner(0, 0, 0) + corner(0, 1, 0) + corner(1, 0, 0) + corner(1, 1, 0))),
        });
        uint32_t nearest_index = std::numeric_limits<uint32_t>::max();
        double nearest_score = std::numeric_limits<double>::max();
        for (int dx = 0; dx <= 1; ++dx)
            for (int dy = 0; dy <= 1; ++dy)
                for (int dz = 0; dz <= 1; ++dz) {
                    const auto found = field.find({cell.x + dx, cell.y + dy, cell.z + dz});
                    if (found == field.end() || found->second.nearest_sample >= points.size())
                        continue;
                    const uint32_t sample_index = found->second.nearest_sample;
                    const auto& attribute_sample = points[sample_index];
                    const auto attribute_delta = subtract(vertex_position, attribute_sample.position);
                    const double distance_squared = dot(attribute_delta, attribute_delta);
                    const double alignment = dot(gradient, normalize(attribute_sample.normal));
                    const double facing_penalty = gradient.x == 0.0 && gradient.y == 0.0 && gradient.z == 0.0
                        ? 0.0
                        : radius_squared * 0.1 * std::pow(1.0 - std::max(0.0, alignment), 2.0);
                    const double score = distance_squared + facing_penalty;
                    if (score < nearest_score ||
                        (score == nearest_score && sample_index < nearest_index)) {
                        nearest_score = score;
                        nearest_index = sample_index;
                    }
                }
        const OrientedSurfacePoint fallback{};
        const auto& attribute_sample = nearest_index < points.size() ? points[nearest_index] : fallback;
        data::PcgVec3 transferred_normal = normalize(attribute_sample.normal);
        if (dot(transferred_normal, transferred_normal) <= kEpsilon)
            transferred_normal = gradient;
        transferred_normals.push_back(transferred_normal.x);
        transferred_normals.push_back(transferred_normal.y);
        transferred_normals.push_back(transferred_normal.z);
        colors.push_back({clamp01(attribute_sample.color.r),
                          clamp01(attribute_sample.color.g),
                          clamp01(attribute_sample.color.b),
                          clamp01(attribute_sample.color.a)});
        const double u = clamp01(attribute_sample.uv.u);
        const double source_v = clamp01(attribute_sample.uv.v);
        uvs.push_back({u, options.flip_uv_v ? 1.0 - source_v : source_v});
    }
    if (output.points().empty()) {
        error = "OrientedSdfSurface found no sign-changing cells; check normal orientation or increase supportRadiusCells";
        return false;
    }

    static constexpr std::array<std::array<std::array<int, 3>, 4>, 3> neighbours{{
        {{{0, -1, -1}, {0, 0, -1}, {0, 0, 0}, {0, -1, 0}}},
        {{{-1, 0, -1}, {-1, 0, 0}, {0, 0, 0}, {0, 0, -1}}},
        {{{-1, -1, 0}, {0, -1, 0}, {0, 0, 0}, {-1, 0, 0}}},
    }};
    std::vector<GridCoord> negative_grid;
    negative_grid.reserve(field.size() / 2);
    for (const auto& [key, sum] : field)
        if (sum.weight > 1.0e-6f && sum.signed_distance / sum.weight <= iso)
            negative_grid.push_back(key);
    std::sort(negative_grid.begin(), negative_grid.end());

    for (size_t grid_index = 0; grid_index < negative_grid.size(); ++grid_index) {
        if ((grid_index & 4095u) == 0 && cancelled && cancelled()) {
            error = "OrientedSdfSurface reconstruction cancelled while connecting Surface Nets quads";
            return false;
        }
        const GridCoord negative = negative_grid[grid_index];
        for (int axis = 0; axis < 3; ++axis) {
            for (int direction : {-1, 1}) {
                GridCoord other = negative;
                if (axis == 0) other.x += direction;
                if (axis == 1) other.y += direction;
                if (axis == 2) other.z += direction;
                if (!in_grid(other, dims) || field_value(field, other, outside) <= iso)
                    continue;
                const GridCoord base = direction > 0 ? negative : other;
                std::array<int, 4> quad{};
                bool complete = true;
                for (size_t corner = 0; corner < 4; ++corner) {
                    const auto& offset = neighbours[static_cast<size_t>(axis)][corner];
                    const GridCoord cell{base.x + offset[0], base.y + offset[1], base.z + offset[2]};
                    const auto found = active_vertex.find(cell);
                    if (found == active_vertex.end()) {
                        complete = false;
                        break;
                    }
                    quad[corner] = found->second;
                }
                if (!complete)
                    continue;
                const bool lower_is_positive = direction < 0;
                if (lower_is_positive)
                    std::reverse(quad.begin(), quad.end());
                output.faces_mut().push_back({quad[0], quad[1], quad[2], quad[3]});
            }
        }
    }
    if (output.faces().empty()) {
        error = "OrientedSdfSurface produced vertices but no connected quads";
        return false;
    }
    if (options.transfer_colors)
        output.set_colors(std::move(colors));
    if (options.transfer_uvs)
        output.set_uvs(std::move(uvs));
    auto& normal_attribute = output.attributes().create_float(
        data::AttributeOwner::Point, "N", 3, {0.0, 1.0, 0.0},
        data::AttributeTransformRole::Normal);
    normal_attribute.float_values_mut() = std::move(transferred_normals);
    output.detail().shade_mode = data::ShadeMode::Smooth;
    output.detail().cusp_angle_deg = 180.0;

    auto& method = output.attributes().create_string(
        data::AttributeOwner::Detail, "reconstruction_method", 1, {"oriented-mls-sdf-surface-nets"});
    method.resize(1);
    auto& source_count = output.attributes().create_int(
        data::AttributeOwner::Detail, "source_point_count", 1,
        {static_cast<int64_t>(points.size())});
    source_count.resize(1);
    auto& cell_size = output.attributes().create_float(
        data::AttributeOwner::Detail, "sdf_cell_size", 1, {options.cell_size});
    cell_size.resize(1);
    return true;
}

void register_oriented_sdf_surface_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("OrientedSdfSurface", std::make_unique<OrientedSdfSurfaceElement>());
}

} // namespace pcg::internal::elements
