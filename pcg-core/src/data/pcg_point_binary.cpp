#include "data/pcg_point_binary.hpp"

#include "pcg_api.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>

namespace pcg::internal::data {
namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

void write_u32(uint8_t* dst, uint32_t value)
{
    std::memcpy(dst, &value, sizeof(value));
}

bool has_attr_num(const nlohmann::json& attributes, const char* key)
{
    return attributes.is_object() && attributes.contains(key) && attributes[key].is_number();
}

bool read_orient_quaternion(const nlohmann::json& attributes,
                            float& qx,
                            float& qy,
                            float& qz,
                            float& qw)
{
    const auto it = attributes.find("orient");
    if (it == attributes.end())
        return false;

    if (it->is_array() && it->size() >= 4 && (*it)[0].is_number() && (*it)[1].is_number() &&
        (*it)[2].is_number() && (*it)[3].is_number()) {
        qx = static_cast<float>((*it)[0].get<double>());
        qy = static_cast<float>((*it)[1].get<double>());
        qz = static_cast<float>((*it)[2].get<double>());
        qw = static_cast<float>((*it)[3].get<double>());
        return true;
    }

    if (it->is_object()) {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double w = 1.0;
        if (!it->contains("x") || !(*it)["x"].is_number() || !it->contains("y") ||
            !(*it)["y"].is_number() || !it->contains("z") || !(*it)["z"].is_number() ||
            !it->contains("w") || !(*it)["w"].is_number())
            return false;
        x = (*it)["x"].get<double>();
        y = (*it)["y"].get<double>();
        z = (*it)["z"].get<double>();
        w = (*it)["w"].get<double>();
        qx = static_cast<float>(x);
        qy = static_cast<float>(y);
        qz = static_cast<float>(z);
        qw = static_cast<float>(w);
        return true;
    }

    return false;
}

bool has_rotation_attr(const nlohmann::json& attributes)
{
    float qx = 0.0f;
    float qy = 0.0f;
    float qz = 0.0f;
    float qw = 1.0f;
    if (read_orient_quaternion(attributes, qx, qy, qz, qw))
        return true;
    return has_attr_num(attributes, "rotationY") || has_attr_num(attributes, "ry") ||
           has_attr_num(attributes, "rotationX") || has_attr_num(attributes, "rx") ||
           has_attr_num(attributes, "rotationZ") || has_attr_num(attributes, "rz");
}

bool read_point_rotation_quaternion(const nlohmann::json& attributes,
                                    float& qx,
                                    float& qy,
                                    float& qz,
                                    float& qw)
{
    if (read_orient_quaternion(attributes, qx, qy, qz, qw))
        return true;

    double rx_deg = 0.0;
    double ry_deg = 0.0;
    double rz_deg = 0.0;
    if (has_attr_num(attributes, "rotationX"))
        rx_deg = attributes.value("rotationX", 0.0);
    else if (has_attr_num(attributes, "rx"))
        rx_deg = attributes.value("rx", 0.0);
    if (has_attr_num(attributes, "rotationY"))
        ry_deg = attributes.value("rotationY", 0.0);
    else if (has_attr_num(attributes, "ry"))
        ry_deg = attributes.value("ry", 0.0);
    if (has_attr_num(attributes, "rotationZ"))
        rz_deg = attributes.value("rotationZ", 0.0);
    else if (has_attr_num(attributes, "rz"))
        rz_deg = attributes.value("rz", 0.0);

    // Match CopyMeshToPoints / Unity Euler order (XYZ intrinsic ≈ ZXY extrinsic for
    // yaw-dominant building facing). Prefer pure Y when only yaw is authored.
    const double hx = rx_deg * kDegToRad * 0.5;
    const double hy = ry_deg * kDegToRad * 0.5;
    const double hz = rz_deg * kDegToRad * 0.5;
    const double cx = std::cos(hx);
    const double sx = std::sin(hx);
    const double cy = std::cos(hy);
    const double sy = std::sin(hy);
    const double cz = std::cos(hz);
    const double sz = std::sin(hz);

    // Quaternion from intrinsic XYZ Euler (same composition as rotate_euler X then Y then Z).
    qx = static_cast<float>(sx * cy * cz + cx * sy * sz);
    qy = static_cast<float>(cx * sy * cz - sx * cy * sz);
    qz = static_cast<float>(cx * cy * sz - sx * sy * cz);
    qw = static_cast<float>(cx * cy * cz + sx * sy * sz);
    return true;
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
    bool has_rotation = true;
    for (const auto& point : points.points()) {
        const auto& attributes = point.attributes;
        has_normals = has_normals && has_attr_num(attributes, "nx") && has_attr_num(attributes, "ny") &&
                      has_attr_num(attributes, "nz");
        has_uv = has_uv && has_attr_num(attributes, "u") && has_attr_num(attributes, "v");
        has_tri = has_tri && has_attr_num(attributes, "triIndex");
        has_scale = has_scale && has_attr_num(attributes, "scale");
        has_rotation = has_rotation && has_rotation_attr(attributes);
    }

    if (has_normals)
        flags |= PCG_POINT_ATTR_NORMAL;
    if (has_uv)
        flags |= PCG_POINT_ATTR_UV;
    if (has_tri)
        flags |= PCG_POINT_ATTR_TRI_INDEX;
    if (has_scale)
        flags |= PCG_POINT_ATTR_SCALE;
    if (has_rotation)
        flags |= PCG_POINT_ATTR_ROTATION;
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

    if (flags & PCG_POINT_ATTR_ROTATION) {
        for (const auto& point : points.points()) {
            float qx = 0.0f;
            float qy = 0.0f;
            float qz = 0.0f;
            float qw = 1.0f;
            read_point_rotation_quaternion(point.attributes, qx, qy, qz, qw);
            const float q[4] = {qx, qy, qz, qw};
            std::memcpy(bytes + offset, q, sizeof(q));
            offset += static_cast<int>(sizeof(q));
        }
    }

    if (out_flags)
        *out_flags = flags;
    return true;
}

} // namespace pcg::internal::data
