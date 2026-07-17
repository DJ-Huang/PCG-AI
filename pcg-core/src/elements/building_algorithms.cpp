#include "elements/building_algorithms.hpp"

#include "data/pcg_geometry.hpp"
#include "elements/element_utils.hpp"
#include "geometry/spline_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace pcg::internal::elements {
namespace {

using geometry::Vec3;

double random_unit(uint32_t& state)
{
    return static_cast<double>(next_rand(state)) /
           static_cast<double>(std::numeric_limits<uint32_t>::max());
}

double random_signed(uint32_t& state, double amplitude)
{
    return (random_unit(state) * 2.0 - 1.0) * std::abs(amplitude);
}

bool read_number(const nlohmann::json& object, const char* key, double& value)
{
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number())
        return false;
    value = it->get<double>();
    return std::isfinite(value);
}

bool read_vector3(const nlohmann::json& value, Vec3& vector)
{
    if (value.is_array() && value.size() >= 3 && value[0].is_number() &&
        value[1].is_number() && value[2].is_number()) {
        vector = {value[0].get<double>(), value[1].get<double>(), value[2].get<double>()};
        return true;
    }
    if (value.is_object()) {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        if (read_number(value, "x", x) && read_number(value, "y", y) &&
            read_number(value, "z", z)) {
            vector = {x, y, z};
            return true;
        }
    }
    return false;
}

Vec3 read_scale(const nlohmann::json& attributes)
{
    double uniform_scale = 1.0;
    double value = 1.0;
    if (read_number(attributes, "pscale", value))
        uniform_scale *= value;

    Vec3 result{uniform_scale, uniform_scale, uniform_scale};
    const auto scale_it = attributes.find("scale");
    if (scale_it != attributes.end()) {
        if (scale_it->is_number()) {
            const double scale = scale_it->get<double>();
            result = geometry::scale(result, scale);
        } else {
            Vec3 vector_scale;
            if (read_vector3(*scale_it, vector_scale)) {
                result.x *= vector_scale.x;
                result.y *= vector_scale.y;
                result.z *= vector_scale.z;
            }
        }
    }

    if (read_number(attributes, "scaleX", value)) result.x *= value;
    if (read_number(attributes, "scaleY", value)) result.y *= value;
    if (read_number(attributes, "scaleZ", value)) result.z *= value;
    return result;
}

Vec3 rotate_euler(Vec3 point, const nlohmann::json& attributes)
{
    double rx_deg = 0.0;
    double ry_deg = 0.0;
    double rz_deg = 0.0;
    if (!read_number(attributes, "rotationX", rx_deg))
        read_number(attributes, "rx", rx_deg);
    if (!read_number(attributes, "rotationY", ry_deg))
        read_number(attributes, "ry", ry_deg);
    if (!read_number(attributes, "rotationZ", rz_deg))
        read_number(attributes, "rz", rz_deg);

    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double rx = rx_deg * kDegToRad;
    const double ry = ry_deg * kDegToRad;
    const double rz = rz_deg * kDegToRad;

    const double cx = std::cos(rx);
    const double sx = std::sin(rx);
    point = {point.x, point.y * cx - point.z * sx, point.y * sx + point.z * cx};

    const double cy = std::cos(ry);
    const double sy = std::sin(ry);
    point = {point.x * cy + point.z * sy, point.y, -point.x * sy + point.z * cy};

    const double cz = std::cos(rz);
    const double sz = std::sin(rz);
    return {point.x * cz - point.y * sz, point.x * sz + point.y * cz, point.z};
}

bool read_quaternion(const nlohmann::json& attributes, double& x, double& y, double& z, double& w)
{
    const auto it = attributes.find("orient");
    if (it == attributes.end())
        return false;

    if (it->is_array() && it->size() >= 4 && (*it)[0].is_number() &&
        (*it)[1].is_number() && (*it)[2].is_number() && (*it)[3].is_number()) {
        x = (*it)[0].get<double>();
        y = (*it)[1].get<double>();
        z = (*it)[2].get<double>();
        w = (*it)[3].get<double>();
    } else if (it->is_object()) {
        if (!read_number(*it, "x", x) || !read_number(*it, "y", y) ||
            !read_number(*it, "z", z) || !read_number(*it, "w", w))
            return false;
    } else {
        return false;
    }

    const double length = std::sqrt(x * x + y * y + z * z + w * w);
    if (length <= 0.000000000001)
        return false;
    x /= length;
    y /= length;
    z /= length;
    w /= length;
    return true;
}

Vec3 rotate_quaternion(const Vec3& point, double x, double y, double z, double w)
{
    const Vec3 q{x, y, z};
    const Vec3 uv = geometry::cross(q, point);
    const Vec3 uuv = geometry::cross(q, uv);
    return geometry::add(point,
        geometry::add(geometry::scale(uv, 2.0 * w), geometry::scale(uuv, 2.0)));
}

bool read_frame(const nlohmann::json& attributes, geometry::Frame3& frame)
{
    double nx = 0.0;
    double ny = 0.0;
    double nz = 0.0;
    double tx = 0.0;
    double ty = 0.0;
    double tz = 0.0;
    if (!read_number(attributes, "nx", nx) || !read_number(attributes, "ny", ny) ||
        !read_number(attributes, "nz", nz) || !read_number(attributes, "tx", tx) ||
        !read_number(attributes, "ty", ty) || !read_number(attributes, "tz", tz))
        return false;

    Vec3 normal = geometry::normalize({nx, ny, nz});
    Vec3 tangent = geometry::normalize({tx, ty, tz});
    Vec3 binormal = geometry::normalize(geometry::cross(normal, tangent));
    if (geometry::length(normal) <= 0.000000000001 ||
        geometry::length(tangent) <= 0.000000000001 ||
        geometry::length(binormal) <= 0.000000000001)
        return false;
    tangent = geometry::normalize(geometry::cross(binormal, normal));
    frame = {{}, tangent, normal, binormal};
    return true;
}

data::PcgVec3 transform_for_point(const data::PcgVec3& point,
                                 const data::PcgPoint& placement)
{
    const nlohmann::json& attributes = placement.attributes;
    const Vec3 scale = read_scale(attributes);

    double qx = 0.0;
    double qy = 0.0;
    double qz = 0.0;
    double qw = 1.0;
    const bool has_orient = read_quaternion(attributes, qx, qy, qz, qw);
    geometry::Frame3 frame;
    const bool has_frame = !has_orient && read_frame(attributes, frame);

    Vec3 value{point.x * scale.x, point.y * scale.y, point.z * scale.z};
    value = rotate_euler(value, attributes);
    if (has_orient)
        value = rotate_quaternion(value, qx, qy, qz, qw);
    else if (has_frame)
        value = geometry::transform_local_to_world(frame, value);
    return {value.x + placement.x, value.y + placement.y, value.z + placement.z};
}

} // namespace

data::PcgPointData randomize_point_attributes(const data::PcgPointData& input,
                                              int graph_seed,
                                              const AttributeRandomizeOptions& options)
{
    data::PcgPointData output = input;
    uint32_t state = mix_seed(graph_seed, options.seed);
    const double scale_min = std::max(0.000001, std::min(options.scale_min, options.scale_max));
    const double scale_max = std::max(scale_min, std::max(options.scale_min, options.scale_max));
    const bool randomize_scale = std::abs(scale_min - 1.0) > 0.000000000001 ||
                                 std::abs(scale_max - 1.0) > 0.000000000001;

    for (auto& point : output.points_mut()) {
        point.x += random_signed(state, options.translate_x);
        point.y += random_signed(state, options.translate_y);
        point.z += random_signed(state, options.translate_z);

        if (std::abs(options.rotate_x_deg) > 0.000000000001)
            point.attributes["rotationX"] = random_signed(state, options.rotate_x_deg);
        if (std::abs(options.rotate_y_deg) > 0.000000000001)
            point.attributes["rotationY"] = random_signed(state, options.rotate_y_deg);
        if (std::abs(options.rotate_z_deg) > 0.000000000001)
            point.attributes["rotationZ"] = random_signed(state, options.rotate_z_deg);
        if (randomize_scale)
            point.attributes["scale"] = scale_min + random_unit(state) * (scale_max - scale_min);
    }
    return output;
}

data::PcgGeometry copy_geometry_to_points(const data::PcgGeometry& prototype,
                                          const data::PcgPointData& points)
{
    data::PcgGeometry output;
    output.detail() = prototype.detail();

    const size_t copy_count = points.points().size();
    output.points_mut().reserve(prototype.points().size() * copy_count);
    output.faces_mut().reserve(prototype.faces().size() * copy_count);

    for (const auto& placement : points.points()) {
        const int point_offset = static_cast<int>(output.points().size());
        const int face_offset = static_cast<int>(output.faces().size());

        for (const auto& point : prototype.points())
            output.points_mut().push_back(transform_for_point(point, placement));

        for (const auto& face : prototype.faces()) {
            std::vector<int> remapped;
            remapped.reserve(face.size());
            for (int index : face)
                remapped.push_back(index + point_offset);
            output.faces_mut().push_back(std::move(remapped));
        }

        for (geometry::GroupDomain domain : {geometry::GroupDomain::Point,
                                             geometry::GroupDomain::Face,
                                             geometry::GroupDomain::Edge}) {
            for (const auto& name : prototype.groups().group_names(domain)) {
                for (int id : prototype.groups().members(domain, name)) {
                    if (domain == geometry::GroupDomain::Point) {
                        output.groups().add(domain, name, id + point_offset);
                    } else if (domain == geometry::GroupDomain::Face) {
                        output.groups().add(domain, name, id + face_offset);
                    } else {
                        const int v0 = static_cast<int>(static_cast<int64_t>(id) / 1000000);
                        const int v1 = static_cast<int>(static_cast<int64_t>(id) % 1000000);
                        const int remapped_v0 = v0 + point_offset;
                        const int remapped_v1 = v1 + point_offset;
                        const int64_t edge = static_cast<int64_t>(std::min(remapped_v0, remapped_v1)) *
                                                 1000000 +
                                             std::max(remapped_v0, remapped_v1);
                        output.groups().add(domain, name, static_cast<int>(edge));
                    }
                }
            }
        }
    }

    if (prototype.has_colors()) {
        std::vector<data::PcgColor> colors;
        colors.reserve(prototype.colors().size() * copy_count);
        for (size_t copy = 0; copy < copy_count; ++copy)
            colors.insert(colors.end(), prototype.colors().begin(), prototype.colors().end());
        output.set_colors(std::move(colors));
    }

    if (prototype.has_uvs()) {
        std::vector<data::PcgVec2> uvs;
        uvs.reserve(prototype.uvs().size() * copy_count);
        for (size_t copy = 0; copy < copy_count; ++copy)
            uvs.insert(uvs.end(), prototype.uvs().begin(), prototype.uvs().end());
        output.set_uvs(std::move(uvs));
    }

    if (prototype.has_face_materials()) {
        std::vector<std::string> materials;
        materials.reserve(prototype.face_materials().size() * copy_count);
        for (size_t copy = 0; copy < copy_count; ++copy) {
            materials.insert(materials.end(), prototype.face_materials().begin(),
                             prototype.face_materials().end());
        }
        output.set_face_materials(std::move(materials));
    } else if (prototype.has_material()) {
        output.set_material_name(prototype.material_name());
    }

    return output;
}

} // namespace pcg::internal::elements
