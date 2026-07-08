#include "data/pcg_point_binary.hpp"

#include "pcg_api.h"

#include <nlohmann/json.hpp>

#include <cstring>

namespace pcg::internal::data {
namespace {

void write_u32(uint8_t* dst, uint32_t value)
{
    std::memcpy(dst, &value, sizeof(value));
}

bool has_attr_num(const nlohmann::json& attributes, const char* key)
{
    return attributes.is_object() && attributes.contains(key) && attributes[key].is_number();
}

} // namespace

uint32_t detect_point_attr_flags(const PcgPointData& points)
{
    uint32_t flags = PCG_POINT_ATTR_NONE;
    if (points.points().empty())
        return flags;

    bool has_normals = true;
    bool has_uv = true;
    bool has_tri = true;
    bool has_scale = true;
    for (const auto& point : points.points()) {
        const auto& attributes = point.attributes;
        has_normals = has_normals && has_attr_num(attributes, "nx") && has_attr_num(attributes, "ny") &&
                      has_attr_num(attributes, "nz");
        has_uv = has_uv && has_attr_num(attributes, "u") && has_attr_num(attributes, "v");
        has_tri = has_tri && has_attr_num(attributes, "triIndex");
        has_scale = has_scale && has_attr_num(attributes, "scale");
    }

    if (has_normals)
        flags |= PCG_POINT_ATTR_NORMAL;
    if (has_uv)
        flags |= PCG_POINT_ATTR_UV;
    if (has_tri)
        flags |= PCG_POINT_ATTR_TRI_INDEX;
    if (has_scale)
        flags |= PCG_POINT_ATTR_SCALE;
    return flags;
}

int point_binary_size(const PcgPointData& points, uint32_t flags)
{
    const int point_count = static_cast<int>(points.points().size());
    int required = 0;
    if (pcg_point_binary_size_for_counts(point_count, flags, &required) != PCG_OK)
        return 0;
    return required;
}

bool write_point_binary(const PcgPointData& points, void* buffer, int buffer_size, uint32_t* out_flags)
{
    if (!buffer || buffer_size <= 0)
        return false;

    const int point_count = static_cast<int>(points.points().size());
    if (point_count <= 0)
        return false;

    const uint32_t flags = detect_point_attr_flags(points);
    const int required = point_binary_size(points, flags);
    if (required <= 0 || buffer_size < required)
        return false;

    auto* bytes = static_cast<uint8_t*>(buffer);
    write_u32(bytes + 0, PCG_POINT_BINARY_MAGIC);
    write_u32(bytes + 4, PCG_POINT_BINARY_VERSION);
    write_u32(bytes + 8, static_cast<uint32_t>(point_count));
    write_u32(bytes + 12, flags);

    int offset = PCG_POINT_BINARY_HEADER_SIZE;
    for (const auto& point : points.points()) {
        const float xyz[3] = {
            static_cast<float>(point.x),
            static_cast<float>(point.y),
            static_cast<float>(point.z),
        };
        std::memcpy(bytes + offset, xyz, sizeof(xyz));
        offset += static_cast<int>(sizeof(xyz));
    }

    if (flags & PCG_POINT_ATTR_NORMAL) {
        for (const auto& point : points.points()) {
            const auto& attributes = point.attributes;
            const float n[3] = {
                static_cast<float>(attributes.value("nx", 0.0)),
                static_cast<float>(attributes.value("ny", 0.0)),
                static_cast<float>(attributes.value("nz", 0.0)),
            };
            std::memcpy(bytes + offset, n, sizeof(n));
            offset += static_cast<int>(sizeof(n));
        }
    }

    if (flags & PCG_POINT_ATTR_UV) {
        for (const auto& point : points.points()) {
            const auto& attributes = point.attributes;
            const float uv[2] = {
                static_cast<float>(attributes.value("u", 0.0)),
                static_cast<float>(attributes.value("v", 0.0)),
            };
            std::memcpy(bytes + offset, uv, sizeof(uv));
            offset += static_cast<int>(sizeof(uv));
        }
    }

    if (flags & PCG_POINT_ATTR_TRI_INDEX) {
        for (const auto& point : points.points()) {
            const auto& attributes = point.attributes;
            const uint32_t tri = static_cast<uint32_t>(attributes.value("triIndex", 0));
            std::memcpy(bytes + offset, &tri, sizeof(tri));
            offset += static_cast<int>(sizeof(tri));
        }
    }

    if (flags & PCG_POINT_ATTR_SCALE) {
        for (const auto& point : points.points()) {
            const auto& attributes = point.attributes;
            const float scale = static_cast<float>(attributes.value("scale", 1.0));
            std::memcpy(bytes + offset, &scale, sizeof(scale));
            offset += static_cast<int>(sizeof(scale));
        }
    }

    if (out_flags)
        *out_flags = flags;
    return true;
}

} // namespace pcg::internal::data
