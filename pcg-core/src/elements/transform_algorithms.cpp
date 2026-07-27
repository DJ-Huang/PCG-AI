#include "elements/transform_algorithms.hpp"

#include "elements/element_utils.hpp"
#include "elements/topology_parity_algorithms.hpp"
#include "geometry/group_table.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <unordered_set>

namespace pcg::internal::elements {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

data::GeometryAffineTransform identity_transform()
{
    return {};
}

data::GeometryAffineTransform translation_transform(const data::PcgVec3& value)
{
    data::GeometryAffineTransform transform = identity_transform();
    transform.translation = value;
    return transform;
}

data::GeometryAffineTransform scale_transform(const data::PcgVec3& value)
{
    data::GeometryAffineTransform transform = identity_transform();
    transform.linear = {value.x, 0.0, 0.0,
                        0.0, value.y, 0.0,
                        0.0, 0.0, value.z};
    return transform;
}

data::GeometryAffineTransform shear_transform(const data::PcgVec3& value)
{
    data::GeometryAffineTransform transform = identity_transform();
    transform.linear = {1.0, 0.0, value.z,
                        0.0, 1.0, value.y,
                        value.x, 0.0, 1.0};
    return transform;
}

data::GeometryAffineTransform rotation_x_transform(double degrees)
{
    const double radians = degrees * kDegToRad;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    data::GeometryAffineTransform transform = identity_transform();
    transform.linear = {1.0, 0.0, 0.0,
                        0.0, c, -s,
                        0.0, s, c};
    return transform;
}

data::GeometryAffineTransform rotation_y_transform(double degrees)
{
    const double radians = degrees * kDegToRad;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    data::GeometryAffineTransform transform = identity_transform();
    transform.linear = {c, 0.0, s,
                        0.0, 1.0, 0.0,
                        -s, 0.0, c};
    return transform;
}

data::GeometryAffineTransform rotation_z_transform(double degrees)
{
    const double radians = degrees * kDegToRad;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    data::GeometryAffineTransform transform = identity_transform();
    transform.linear = {c, -s, 0.0,
                        s, c, 0.0,
                        0.0, 0.0, 1.0};
    return transform;
}

data::GeometryAffineTransform multiply_transform(const data::GeometryAffineTransform& left,
                                               const data::GeometryAffineTransform& right)
{
    data::GeometryAffineTransform result;
    const auto& a = left.linear;
    const auto& b = right.linear;
    result.linear = {
        a[0] * b[0] + a[1] * b[3] + a[2] * b[6],
        a[0] * b[1] + a[1] * b[4] + a[2] * b[7],
        a[0] * b[2] + a[1] * b[5] + a[2] * b[8],
        a[3] * b[0] + a[4] * b[3] + a[5] * b[6],
        a[3] * b[1] + a[4] * b[4] + a[5] * b[7],
        a[3] * b[2] + a[4] * b[5] + a[5] * b[8],
        a[6] * b[0] + a[7] * b[3] + a[8] * b[6],
        a[6] * b[1] + a[7] * b[4] + a[8] * b[7],
        a[6] * b[2] + a[7] * b[5] + a[8] * b[8],
    };
    const auto rotated = data::transform_vector(left, right.translation);
    result.translation = {rotated.x + left.translation.x,
                          rotated.y + left.translation.y,
                          rotated.z + left.translation.z};
    return result;
}

data::GeometryAffineTransform invert_transform(const data::GeometryAffineTransform& transform,
                                               bool& ok);

data::GeometryAffineTransform rotation_from_euler(const data::PcgVec3& degrees,
                                                const std::string& order)
{
    data::GeometryAffineTransform result = identity_transform();
    for (char axis : order) {
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(axis)));
        if (lower == 'x')
            result = multiply_transform(rotation_x_transform(degrees.x), result);
        else if (lower == 'y')
            result = multiply_transform(rotation_y_transform(degrees.y), result);
        else if (lower == 'z')
            result = multiply_transform(rotation_z_transform(degrees.z), result);
    }
    return result;
}

data::GeometryAffineTransform compose_ordered_transform(
    const std::string& order,
    const data::GeometryAffineTransform& scale,
    const data::GeometryAffineTransform& rotate,
    const data::GeometryAffineTransform& shear,
    const data::GeometryAffineTransform& translate)
{
    data::GeometryAffineTransform result = identity_transform();
    for (char op : order) {
        const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(op)));
        if (lower == 's')
            result = multiply_transform(shear, multiply_transform(scale, result));
        else if (lower == 'r')
            result = multiply_transform(rotate, result);
        else if (lower == 't')
            result = multiply_transform(translate, result);
    }
    return result;
}

data::GeometryAffineTransform build_component_transform(
    const data::PcgVec3& translate,
    const data::PcgVec3& rotation_deg,
    const data::PcgVec3& scale,
    const data::PcgVec3& shear,
    double uniform_scale,
    const std::string& transform_order,
    const std::string& rotate_order)
{
    const data::PcgVec3 scaled{
        scale.x * uniform_scale,
        scale.y * uniform_scale,
        scale.z * uniform_scale,
    };
    return compose_ordered_transform(
        transform_order,
        scale_transform(scaled),
        rotation_from_euler(rotation_deg, rotate_order),
        shear_transform(shear),
        translation_transform(translate));
}

data::GeometryAffineTransform around_pivot_transform(
    const data::GeometryAffineTransform& local,
    const data::PcgVec3& pivot_translate,
    const data::PcgVec3& pivot_rotate_deg)
{
    const auto to_pivot = translation_transform(pivot_translate);
    const auto from_pivot = translation_transform({
        -pivot_translate.x,
        -pivot_translate.y,
        -pivot_translate.z,
    });
    const auto pivot_rotate = rotation_from_euler(pivot_rotate_deg, "xyz");
    bool inverse_ok = false;
    const auto pivot_rotate_inverse = invert_transform(pivot_rotate, inverse_ok);
    if (!inverse_ok)
        return multiply_transform(to_pivot, multiply_transform(local, from_pivot));
    return multiply_transform(
        to_pivot,
        multiply_transform(
            pivot_rotate,
            multiply_transform(
                local,
                multiply_transform(pivot_rotate_inverse, from_pivot))));
}

data::GeometryAffineTransform build_total_transform(const TransformMeshOptions& options)
{
    const auto pre = build_component_transform(
        options.pre_translate,
        options.pre_rotation_deg,
        options.pre_scale,
        options.pre_shear,
        1.0,
        options.pre_transform_order,
        options.pre_rotate_order);
    const auto main = around_pivot_transform(
        build_component_transform(
            {0.0, 0.0, 0.0},
            options.rotation_deg,
            options.scale,
            options.shear,
            options.uniform_scale,
            options.transform_order,
            options.rotate_order),
        options.pivot_translate,
        options.pivot_rotate_deg);
    const auto translate = translation_transform(options.translate);
    const auto main_with_translate = multiply_transform(translate, main);
    if (options.output_multiply_order == "pre")
        return multiply_transform(pre, main_with_translate);
    return multiply_transform(main_with_translate, pre);
}

data::GeometryAffineTransform invert_transform(const data::GeometryAffineTransform& transform,
                                               bool& ok)
{
    ok = false;
    const auto& m = transform.linear;
    const double det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) +
        m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) <= 1.0e-12)
        return identity_transform();

    const double inv_det = 1.0 / det;
    data::GeometryAffineTransform inverse;
    inverse.linear = {
        (m[4] * m[8] - m[5] * m[7]) * inv_det,
        (m[2] * m[7] - m[1] * m[8]) * inv_det,
        (m[1] * m[5] - m[2] * m[4]) * inv_det,
        (m[5] * m[6] - m[3] * m[8]) * inv_det,
        (m[0] * m[8] - m[2] * m[6]) * inv_det,
        (m[2] * m[3] - m[0] * m[5]) * inv_det,
        (m[3] * m[7] - m[4] * m[6]) * inv_det,
        (m[1] * m[6] - m[0] * m[7]) * inv_det,
        (m[0] * m[4] - m[1] * m[3]) * inv_det,
    };
    inverse.translation = data::transform_vector(
        inverse,
        {-transform.translation.x, -transform.translation.y, -transform.translation.z});
    ok = true;
    return inverse;
}

void write_output_matrix(data::PcgGeometry& geometry,
                         const std::string& attribute_name,
                         const data::GeometryAffineTransform& transform)
{
    if (attribute_name.empty())
        return;
    geometry.attributes().erase(data::AttributeOwner::Detail, attribute_name);
    auto& matrix = geometry.attributes().create_float(
        data::AttributeOwner::Detail, attribute_name, 16,
        std::vector<double>(16, 0.0), data::AttributeTransformRole::Matrix);
    const auto& m = transform.linear;
    const auto& t = transform.translation;
    matrix.float_values_mut() = {
        m[0], m[1], m[2], t.x,
        m[3], m[4], m[5], t.y,
        m[6], m[7], m[8], t.z,
        0.0, 0.0, 0.0, 1.0,
    };
}

void recompute_normals_if_needed(data::PcgGeometry& geometry,
                                 const TransformMeshOptions& options)
{
    if (!options.recompute_point_normals)
        return;
    ComputeNormalsOptions normal_options;
    normal_options.shade_mode = "auto";
    normal_options.write_point_n = true;
    geometry = compute_normals_geometry(geometry, normal_options);
}

geometry::GroupDomain resolve_group_domain(const data::PcgGeometry& geometry,
                                           const std::string& group,
                                           const std::string& group_type)
{
    if (group_type == "points")
        return geometry::GroupDomain::Point;
    if (group_type == "primitives")
        return geometry::GroupDomain::Face;
    if (group_type == "edges")
        return geometry::GroupDomain::Edge;
    if (geometry.groups().has_group(geometry::GroupDomain::Point, group))
        return geometry::GroupDomain::Point;
    if (geometry.groups().has_group(geometry::GroupDomain::Face, group))
        return geometry::GroupDomain::Face;
    if (geometry.groups().has_group(geometry::GroupDomain::Edge, group))
        return geometry::GroupDomain::Edge;
    return geometry::GroupDomain::Point;
}

std::unordered_set<int> collect_point_indices(const data::PcgGeometry& geometry,
                                              const std::string& group,
                                              const std::string& group_type)
{
    std::unordered_set<int> selected;
    if (group.empty()) {
        for (size_t i = 0; i < geometry.points().size(); ++i)
            selected.insert(static_cast<int>(i));
        return selected;
    }

    const auto domain = resolve_group_domain(geometry, group, group_type);
    const auto members = geometry.groups().eval(domain, group);
    if (domain == geometry::GroupDomain::Point) {
        for (geometry::GroupId id : members)
            selected.insert(static_cast<int>(id));
        return selected;
    }
    if (domain == geometry::GroupDomain::Face) {
        for (geometry::GroupId id : members) {
            if (id < 0 || static_cast<size_t>(id) >= geometry.faces().size())
                continue;
            for (int point : geometry.faces()[static_cast<size_t>(id)])
                selected.insert(point);
        }
        return selected;
    }
    for (geometry::GroupId id : members) {
        const auto ends = geometry::edge_group_points(id);
        selected.insert(ends[0]);
        selected.insert(ends[1]);
    }
    return selected;
}

void apply_affine_to_selected(data::PcgGeometry& geometry,
                              const data::GeometryAffineTransform& affine,
                              const std::unordered_set<int>* selected_points)
{
    for (size_t i = 0; i < geometry.points().size(); ++i) {
        if (selected_points &&
            selected_points->count(static_cast<int>(i)) == 0)
            continue;
        geometry.points_mut()[i] =
            data::transform_position(affine, geometry.points()[i]);
    }

    if (!selected_points) {
        data::transform_geometry_attributes(geometry, affine);
        return;
    }

    for (const auto& name : geometry.attributes().names(data::AttributeOwner::Point)) {
        auto* attribute = geometry.attributes().find(data::AttributeOwner::Point, name);
        if (!attribute || attribute->schema().type != data::AttributeType::Float ||
            attribute->schema().tuple_size < 3)
            continue;
        const auto role = attribute->schema().transform_role;
        if (role != data::AttributeTransformRole::Position &&
            role != data::AttributeTransformRole::Vector &&
            role != data::AttributeTransformRole::Normal)
            continue;
        auto& values = attribute->float_values_mut();
        const size_t width = static_cast<size_t>(attribute->schema().tuple_size);
        for (int index : *selected_points) {
            if (index < 0 || static_cast<size_t>(index) >= attribute->size())
                continue;
            const size_t offset = static_cast<size_t>(index) * width;
            const data::PcgVec3 value{values[offset], values[offset + 1], values[offset + 2]};
            const data::PcgVec3 result = role == data::AttributeTransformRole::Position
                ? data::transform_position(affine, value)
                : role == data::AttributeTransformRole::Normal
                    ? data::transform_normal(affine, value)
                    : data::transform_vector(affine, value);
            values[offset] = result.x;
            values[offset + 1] = result.y;
            values[offset + 2] = result.z;
        }
    }
}

} // namespace

TransformMeshOptions parse_transform_mesh_options(const nlohmann::json& data)
{
    TransformMeshOptions options;
    options.group = data.value("group", "");
    options.group_type = data.value("groupType", "guess");
    options.transform_order = data.value("transformOrder", "srt");
    options.rotate_order = data.value("rotateOrder", "xyz");
    options.translate = read_vector_param(data, "translate", {0.0, 0.0, 0.0});
    options.rotation_deg = read_vector_param(data, "rotation", {0.0, 0.0, 0.0});
    options.scale = read_vector_param(data, "scale", {1.0, 1.0, 1.0});
    options.shear = read_vector_param(data, "shear", {0.0, 0.0, 0.0});
    options.uniform_scale = data.value("uniformScale", 1.0);
    options.pivot_translate = read_vector_param(data, "pivotTranslate", {0.0, 0.0, 0.0});
    options.pivot_rotate_deg = read_vector_param(data, "pivotRotate", {0.0, 0.0, 0.0});
    options.pre_transform_order = data.value("preTransformOrder", "srt");
    options.pre_rotate_order = data.value("preRotateOrder", "xyz");
    options.pre_translate = read_vector_param(data, "preTranslate", {0.0, 0.0, 0.0});
    options.pre_rotation_deg = read_vector_param(data, "preRotate", {0.0, 0.0, 0.0});
    options.pre_scale = read_vector_param(data, "preScale", {1.0, 1.0, 1.0});
    options.pre_shear = read_vector_param(data, "preShear", {0.0, 0.0, 0.0});
    options.recompute_point_normals = data.value("recomputePointNormals", false);
    options.preserve_normal_length = data.value("preserveNormalLength", true);
    options.invert_transform = data.value("invertTransform", false);
    options.output_transform = data.value("outputTransform", false);
    options.output_attribute = data.value("outputAttribute", "xform");
    options.output_multiply_order = data.value("outputMultiplyOrder", "post");
    return options;
}

data::PcgGeometry transform_geometry(const data::PcgGeometry& geometry,
                                     const TransformMeshOptions& options)
{
    data::PcgGeometry output = geometry;
    if (output.points().empty())
        return output;

    auto affine = build_total_transform(options);
    if (options.invert_transform) {
        bool ok = false;
        affine = invert_transform(affine, ok);
        if (!ok)
            return output;
    }

    const auto selected = collect_point_indices(output, options.group, options.group_type);
    const bool transform_all =
        options.group.empty() && selected.size() == output.points().size();
    apply_affine_to_selected(
        output, affine, transform_all ? nullptr : &selected);

    if (options.output_transform)
        write_output_matrix(output, options.output_attribute, affine);

    recompute_normals_if_needed(output, options);
    return output;
}

data::PcgMeshData transform_mesh(const data::PcgMeshData& mesh,
                                 const TransformMeshOptions& options)
{
    data::PcgMeshData out = mesh;
    if (out.vertices().empty())
        return out;

    auto affine = build_total_transform(options);
    if (options.invert_transform) {
        bool ok = false;
        affine = invert_transform(affine, ok);
        if (!ok)
            return out;
    }

    for (auto& vertex : out.vertices_mut()) {
        const data::PcgVec3 value{vertex.x, vertex.y, vertex.z};
        const auto transformed = data::transform_position(affine, value);
        vertex.x = static_cast<float>(transformed.x);
        vertex.y = static_cast<float>(transformed.y);
        vertex.z = static_cast<float>(transformed.z);
    }

    if (out.has_normals()) {
        std::vector<data::PcgVertex> normals = out.normals();
        for (auto& normal : normals) {
            const data::PcgVec3 value{normal.x, normal.y, normal.z};
            const double length = std::sqrt(value.x * value.x + value.y * value.y +
                                            value.z * value.z);
            auto transformed = data::transform_normal(affine, value);
            if (options.preserve_normal_length && length > 1.0e-12) {
                const double new_length = std::sqrt(
                    transformed.x * transformed.x + transformed.y * transformed.y +
                    transformed.z * transformed.z);
                if (new_length > 1.0e-12) {
                    const double scale = length / new_length;
                    transformed.x *= scale;
                    transformed.y *= scale;
                    transformed.z *= scale;
                }
            }
            normal.x = static_cast<float>(transformed.x);
            normal.y = static_cast<float>(transformed.y);
            normal.z = static_cast<float>(transformed.z);
        }
        out.set_normals(std::move(normals));
    }

    return out;
}

} // namespace pcg::internal::elements
