#include "elements/assembly_algorithms.hpp"

#include "data/pcg_attribute_table.hpp"
#include "geometry/group_table.hpp"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pcg::internal::elements {
namespace {

constexpr double kEpsilon = 1.0e-9;
constexpr double kPi = 3.14159265358979323846;

data::PcgVec3 add(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

data::PcgVec3 subtract(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

data::PcgVec3 multiply(const data::PcgVec3& value, double scale)
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

double dot(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

data::PcgVec3 cross(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

double length(const data::PcgVec3& value)
{
    return std::sqrt(dot(value, value));
}

data::PcgVec3 normalize(const data::PcgVec3& value)
{
    const double magnitude = length(value);
    return magnitude <= kEpsilon ? data::PcgVec3{} : multiply(value, 1.0 / magnitude);
}

data::PcgVec3 convert_axis(data::PcgVec3 value, const std::string& conversion)
{
    if (conversion == "zUpToYUp")
        return {value.x, value.z, -value.y};
    if (conversion == "yUpToZUp")
        return {value.x, -value.z, value.y};
    return value;
}

std::string material_name(const aiScene& scene, unsigned material_index)
{
    if (material_index >= scene.mNumMaterials)
        return {};
    aiString name;
    if (scene.mMaterials[material_index]->Get(AI_MATKEY_NAME, name) != AI_SUCCESS)
        return {};
    return name.C_Str();
}

data::PcgVec3 transform_position(const aiMatrix4x4& matrix, const aiVector3D& value)
{
    const aiVector3D transformed = matrix * value;
    return {transformed.x, transformed.y, transformed.z};
}

data::PcgVec3 transform_normal(aiMatrix3x3 matrix, const aiVector3D& value)
{
    matrix.Inverse().Transpose();
    const aiVector3D transformed = matrix * value;
    return normalize({transformed.x, transformed.y, transformed.z});
}

data::PcgGeometry import_mesh_instance(const aiScene& scene,
                                       const aiMesh& mesh,
                                       const aiMatrix4x4& transform,
                                       const std::string& instance_name,
                                       const ImportMeshOptions& options)
{
    data::PcgGeometry result;
    result.points_mut().reserve(mesh.mNumVertices);
    for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
        auto point = convert_axis(transform_position(transform, mesh.mVertices[i]),
                                  options.axis_conversion);
        result.points_mut().push_back(multiply(point, options.scale));
    }

    result.faces_mut().reserve(mesh.mNumFaces);
    for (unsigned face_index = 0; face_index < mesh.mNumFaces; ++face_index) {
        const aiFace& face = mesh.mFaces[face_index];
        if (face.mNumIndices < 3)
            continue;
        std::vector<int> polygon;
        polygon.reserve(face.mNumIndices);
        for (unsigned corner = 0; corner < face.mNumIndices; ++corner)
            polygon.push_back(static_cast<int>(face.mIndices[corner]));
        result.faces_mut().push_back(std::move(polygon));
    }

    const std::string part_name = !instance_name.empty()
        ? instance_name
        : (mesh.mName.length > 0 ? std::string(mesh.mName.C_Str()) : std::string("mesh"));
    for (size_t face_index = 0; face_index < result.faces().size(); ++face_index)
        result.groups().add(geometry::GroupDomain::Face, part_name,
                            static_cast<geometry::GroupId>(face_index));

    auto& name_attribute = result.attributes().create_string(
        data::AttributeOwner::Primitive, "name", 1, {""});
    name_attribute.string_values_mut().assign(result.faces().size(), part_name);

    if (mesh.HasNormals()) {
        auto& normal_attribute = result.attributes().create_float(
            data::AttributeOwner::Point, "N", 3, {0.0, 1.0, 0.0},
            data::AttributeTransformRole::Normal);
        normal_attribute.float_values_mut().reserve(static_cast<size_t>(mesh.mNumVertices) * 3);
        const aiMatrix3x3 normal_transform(transform);
        for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
            const auto normal = convert_axis(transform_normal(normal_transform, mesh.mNormals[i]),
                                             options.axis_conversion);
            normal_attribute.float_values_mut().insert(
                normal_attribute.float_values_mut().end(), {normal.x, normal.y, normal.z});
        }
    }

    if (mesh.HasTextureCoords(0)) {
        std::vector<data::PcgVec2> uvs;
        uvs.reserve(mesh.mNumVertices);
        for (unsigned i = 0; i < mesh.mNumVertices; ++i)
            uvs.push_back({mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y});
        result.set_uvs(std::move(uvs));
        auto& uv_attribute = result.attributes().create_float(
            data::AttributeOwner::Point, "uv", 2, {0.0, 0.0});
        uv_attribute.float_values_mut().reserve(static_cast<size_t>(mesh.mNumVertices) * 2);
        for (const auto& uv : result.uvs())
            uv_attribute.float_values_mut().insert(uv_attribute.float_values_mut().end(),
                                                   {uv.u, uv.v});
    }

    if (mesh.HasVertexColors(0)) {
        std::vector<data::PcgColor> colors;
        colors.reserve(mesh.mNumVertices);
        for (unsigned i = 0; i < mesh.mNumVertices; ++i) {
            const auto& color = mesh.mColors[0][i];
            colors.push_back({color.r, color.g, color.b, color.a});
        }
        result.set_colors(std::move(colors));
    }

    const std::string material = material_name(scene, mesh.mMaterialIndex);
    if (!material.empty()) {
        result.set_material_name(material);
        result.set_face_materials(std::vector<std::string>(result.faces().size(), material));
    }
    return result;
}

void import_node_recursive(const aiScene& scene,
                           const aiNode& node,
                           const aiMatrix4x4& parent_transform,
                           const ImportMeshOptions& options,
                           data::PcgGeometry& output)
{
    const aiMatrix4x4 transform = parent_transform * node.mTransformation;
    for (unsigned i = 0; i < node.mNumMeshes; ++i) {
        const aiMesh& mesh = *scene.mMeshes[node.mMeshes[i]];
        auto instance = import_mesh_instance(scene, mesh, transform, node.mName.C_Str(), options);
        output = data::merge_geometries(output, instance);
    }
    for (unsigned i = 0; i < node.mNumChildren; ++i)
        import_node_recursive(scene, *node.mChildren[i], transform, options, output);
}

struct Bounds {
    data::PcgVec3 minimum{};
    data::PcgVec3 maximum{};
    bool valid = false;
};

Bounds bounds_of(const std::vector<data::PcgVec3>& points)
{
    Bounds bounds;
    if (points.empty())
        return bounds;
    const double maximum = std::numeric_limits<double>::max();
    bounds.minimum = {maximum, maximum, maximum};
    bounds.maximum = {-maximum, -maximum, -maximum};
    for (const auto& point : points) {
        bounds.minimum.x = std::min(bounds.minimum.x, point.x);
        bounds.minimum.y = std::min(bounds.minimum.y, point.y);
        bounds.minimum.z = std::min(bounds.minimum.z, point.z);
        bounds.maximum.x = std::max(bounds.maximum.x, point.x);
        bounds.maximum.y = std::max(bounds.maximum.y, point.y);
        bounds.maximum.z = std::max(bounds.maximum.z, point.z);
    }
    bounds.valid = true;
    return bounds;
}

double justify_value(const std::string& value)
{
    if (value == "min") return 0.0;
    if (value == "max") return 1.0;
    return 0.5;
}

std::string resolve_target_justify(const std::string& source_justify,
                                   const std::string& target_justify)
{
    if (target_justify == "same") {
        if (source_justify == "none")
            return "center";
        return source_justify;
    }
    return target_justify;
}

data::PcgVec3 bounds_size(const Bounds& bounds)
{
    return subtract(bounds.maximum, bounds.minimum);
}

data::PcgVec3 bounds_anchor_axis(const Bounds& bounds,
                                 const std::array<std::string, 3>& justify)
{
    const auto size = bounds_size(bounds);
    data::PcgVec3 anchor{};
    for (int axis = 0; axis < 3; ++axis) {
        const std::string& mode = justify[static_cast<size_t>(axis)];
        const double t = mode == "none" ? 0.5 : justify_value(mode);
        const double min_v = axis == 0 ? bounds.minimum.x
            : axis == 1 ? bounds.minimum.y : bounds.minimum.z;
        const double size_v = axis == 0 ? size.x : axis == 1 ? size.y : size.z;
        const double value = min_v + size_v * t;
        if (axis == 0) anchor.x = value;
        else if (axis == 1) anchor.y = value;
        else anchor.z = value;
    }
    return anchor;
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

std::vector<data::PcgVec3> points_for_bounds(const data::PcgGeometry& geometry,
                                             const std::string& group,
                                             const std::string& group_type,
                                             bool use_group)
{
    if (!use_group || group.empty())
        return geometry.points();
    const auto indices = collect_point_indices(geometry, group, group_type);
    std::vector<data::PcgVec3> points;
    points.reserve(indices.size());
    for (int index : indices) {
        if (index < 0 || static_cast<size_t>(index) >= geometry.points().size())
            continue;
        points.push_back(geometry.points()[static_cast<size_t>(index)]);
    }
    return points;
}

Bounds bounds_from_position_size(const data::PcgVec3& position,
                                 const data::PcgVec3& size,
                                 const std::array<std::string, 3>& target_justify)
{
    Bounds bounds;
    bounds.valid = true;
    for (int axis = 0; axis < 3; ++axis) {
        const std::string mode = target_justify[static_cast<size_t>(axis)] == "none"
            ? "center"
            : target_justify[static_cast<size_t>(axis)];
        const double pos = axis == 0 ? position.x : axis == 1 ? position.y : position.z;
        const double extent = axis == 0 ? size.x : axis == 1 ? size.y : size.z;
        double minimum = 0.0;
        double maximum = 0.0;
        if (mode == "min") {
            minimum = pos;
            maximum = pos + extent;
        } else if (mode == "max") {
            maximum = pos;
            minimum = pos - extent;
        } else {
            minimum = pos - extent * 0.5;
            maximum = pos + extent * 0.5;
        }
        if (axis == 0) {
            bounds.minimum.x = minimum;
            bounds.maximum.x = maximum;
        } else if (axis == 1) {
            bounds.minimum.y = minimum;
            bounds.maximum.y = maximum;
        } else {
            bounds.minimum.z = minimum;
            bounds.maximum.z = maximum;
        }
    }
    return bounds;
}

bool read_detail_matrix(const data::PcgGeometry& geometry,
                        const std::string& name,
                        data::GeometryAffineTransform& transform,
                        std::string& error)
{
    if (name.empty()) {
        error = "MatchSize restore attribute name is empty";
        return false;
    }
    const auto* attribute =
        geometry.attributes().find(data::AttributeOwner::Detail, name);
    if (!attribute || attribute->schema().type != data::AttributeType::Float ||
        attribute->float_values().size() < 16) {
        error = "MatchSize restore attribute '" + name + "' is missing or invalid";
        return false;
    }
    const auto& values = attribute->float_values();
    transform.linear = {
        values[0], values[1], values[2],
        values[4], values[5], values[6],
        values[8], values[9], values[10],
    };
    transform.translation = {values[3], values[7], values[11]};
    return true;
}

data::GeometryAffineTransform invert_affine(const data::GeometryAffineTransform& transform,
                                            bool& ok,
                                            std::string& error)
{
    ok = false;
    const auto& m = transform.linear;
    const double det =
        m[0] * (m[4] * m[8] - m[5] * m[7]) -
        m[1] * (m[3] * m[8] - m[5] * m[6]) +
        m[2] * (m[3] * m[7] - m[4] * m[6]);
    if (std::abs(det) <= kEpsilon) {
        error = "MatchSize restore transform is not invertible";
        return {};
    }
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
        inverse, {-transform.translation.x, -transform.translation.y,
                  -transform.translation.z});
    ok = true;
    return inverse;
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

struct BendFrame {
    data::PcgVec3 origin;
    data::PcgVec3 radial;
    data::PcgVec3 binormal;
    data::PcgVec3 axis;
    double length = 1.0;
    double angle = 0.0;
};

double bend_theta(const BendFrame& frame, const data::PcgVec3& point)
{
    const double t = dot(subtract(point, frame.origin), frame.axis);
    return frame.angle * std::clamp(t / frame.length, 0.0, 1.0);
}

data::PcgVec3 rotate_in_bend_plane(const BendFrame& frame,
                                   const data::PcgVec3& value,
                                   double theta)
{
    const double radial = dot(value, frame.radial);
    const double axis = dot(value, frame.axis);
    const double binormal = dot(value, frame.binormal);
    return add(add(multiply(frame.radial, radial * std::cos(theta) + axis * std::sin(theta)),
                   multiply(frame.axis, -radial * std::sin(theta) + axis * std::cos(theta))),
               multiply(frame.binormal, binormal));
}

data::PcgVec3 bend_position(const BendFrame& frame, const data::PcgVec3& point)
{
    const auto relative = subtract(point, frame.origin);
    const double x = dot(relative, frame.radial);
    const double y = dot(relative, frame.binormal);
    const double t = dot(relative, frame.axis);
    if (t <= 0.0 || std::abs(frame.angle) <= kEpsilon)
        return point;

    const double radius = frame.length / frame.angle;
    const double theta = frame.angle * std::min(t / frame.length, 1.0);
    double bent_x = radius * (1.0 - std::cos(theta)) + x * std::cos(theta);
    double bent_t = radius * std::sin(theta) - x * std::sin(theta);
    if (t > frame.length) {
        const double extension = t - frame.length;
        bent_x += extension * std::sin(frame.angle);
        bent_t += extension * std::cos(frame.angle);
    }
    return add(frame.origin,
               add(add(multiply(frame.radial, bent_x), multiply(frame.binormal, y)),
                   multiply(frame.axis, bent_t)));
}

std::vector<data::PcgVec3> owner_locations(const data::PcgGeometry& geometry,
                                           data::AttributeOwner owner)
{
    if (owner == data::AttributeOwner::Point)
        return geometry.points();
    std::vector<data::PcgVec3> result;
    if (owner == data::AttributeOwner::Vertex) {
        result.reserve(static_cast<size_t>(geometry.corner_count()));
        for (const auto& face : geometry.faces())
            for (int point : face)
                result.push_back(geometry.points()[static_cast<size_t>(point)]);
    } else if (owner == data::AttributeOwner::Primitive) {
        result.reserve(geometry.faces().size());
        for (const auto& face : geometry.faces()) {
            data::PcgVec3 center{};
            for (int point : face)
                center = add(center, geometry.points()[static_cast<size_t>(point)]);
            result.push_back(face.empty() ? center : multiply(center, 1.0 / face.size()));
        }
    }
    return result;
}

void transform_bend_attributes(data::PcgGeometry& geometry,
                               const data::PcgGeometry& rest,
                               const BendFrame& frame)
{
    for (data::AttributeOwner owner : {data::AttributeOwner::Point,
                                       data::AttributeOwner::Vertex,
                                       data::AttributeOwner::Primitive}) {
        const auto locations = owner_locations(rest, owner);
        for (const auto& name : geometry.attributes().names(owner)) {
            auto* attribute = geometry.attributes().find(owner, name);
            if (!attribute || attribute->schema().type != data::AttributeType::Float ||
                attribute->schema().tuple_size < 3 || attribute->size() != locations.size())
                continue;
            const auto role = attribute->schema().transform_role;
            if (role != data::AttributeTransformRole::Position &&
                role != data::AttributeTransformRole::Vector &&
                role != data::AttributeTransformRole::Normal)
                continue;
            auto& values = attribute->float_values_mut();
            const size_t width = static_cast<size_t>(attribute->schema().tuple_size);
            for (size_t i = 0; i < attribute->size(); ++i) {
                data::PcgVec3 value{values[i * width], values[i * width + 1],
                                    values[i * width + 2]};
                if (role == data::AttributeTransformRole::Position)
                    value = bend_position(frame, value);
                else
                    value = rotate_in_bend_plane(frame, value, bend_theta(frame, locations[i]));
                if (role == data::AttributeTransformRole::Normal)
                    value = normalize(value);
                values[i * width] = value.x;
                values[i * width + 1] = value.y;
                values[i * width + 2] = value.z;
            }
        }
    }
}

} // namespace

bool import_geometry_file(const std::filesystem::path& path,
                          const ImportMeshOptions& options,
                          data::PcgGeometry& output,
                          std::string& error)
{
    if (path.empty()) {
        error = "ImportMesh path is empty";
        return false;
    }
    if (!std::filesystem::exists(path)) {
        error = "ImportMesh file not found: " + path.string();
        return false;
    }
    if (!std::isfinite(options.scale) || options.scale <= kEpsilon) {
        error = "ImportMesh scale must be finite and greater than zero";
        return false;
    }
    if (options.axis_conversion != "none" && options.axis_conversion != "zUpToYUp" &&
        options.axis_conversion != "yUpToZUp") {
        error = "ImportMesh axisConversion must be none, zUpToYUp, or yUpToZUp";
        return false;
    }

    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    const aiScene* scene = importer.ReadFile(
        path.string(), aiProcess_JoinIdenticalVertices | aiProcess_SortByPType |
                           aiProcess_ValidateDataStructure);
    if (!scene || !scene->mRootNode) {
        error = "ImportMesh Assimp error: " + std::string(importer.GetErrorString());
        return false;
    }

    data::PcgGeometry imported;
    import_node_recursive(*scene, *scene->mRootNode, aiMatrix4x4{}, options, imported);
    if (imported.points().empty() || imported.faces().empty()) {
        error = "ImportMesh file contains no polygon geometry: " + path.string();
        return false;
    }
    imported.detail().shade_mode = data::ShadeMode::Smooth;
    output = std::move(imported);
    return true;
}

bool match_size_geometry(const data::PcgGeometry& source,
                         const data::PcgGeometry* reference,
                         const MatchSizeOptions& options,
                         data::PcgGeometry& output,
                         std::string& error)
{
    output = source;

    if (options.restore_transform) {
        data::GeometryAffineTransform stored;
        if (!read_detail_matrix(output, options.restore_attribute, stored, error))
            return false;
        bool ok = false;
        const auto inverse = invert_affine(stored, ok, error);
        if (!ok)
            return false;
        apply_affine_to_selected(output, inverse, nullptr);
    }

    const auto transform_points = collect_point_indices(
        output, options.group, options.group_type);
    if (transform_points.empty()) {
        error = "MatchSize group selected no points";
        return false;
    }

    const bool use_source_group =
        options.use_groups_for_bounds && !options.source_group.empty();
    const auto source_bound_points = points_for_bounds(
        output,
        use_source_group ? options.source_group : std::string{},
        use_source_group ? options.source_group_type : options.group_type,
        use_source_group);
    const Bounds source_bounds = bounds_of(source_bound_points);
    if (!source_bounds.valid) {
        error = "MatchSize source has no points";
        return false;
    }

    const std::array<std::string, 3> source_justify{
        options.justify_x, options.justify_y, options.justify_z};
    const std::array<std::string, 3> target_justify{
        resolve_target_justify(options.justify_x, options.target_justify_x),
        resolve_target_justify(options.justify_y, options.target_justify_y),
        resolve_target_justify(options.justify_z, options.target_justify_z)};

    const bool reference_available = reference != nullptr;
    bool use_reference = false;
    if (options.justify_with == "secondInput") {
        use_reference = true;
    } else if (options.justify_with == "inputIfWired") {
        use_reference = reference_available;
    } else if (options.justify_with == "originAndUnitSize") {
        use_reference = false;
    } else {
        use_reference = false; // locationAndSize
    }

    Bounds target_bounds;
    if (options.justify_with == "originAndUnitSize") {
        target_bounds = bounds_from_position_size(
            {0.0, 0.0, 0.0}, {1.0, 1.0, 1.0}, {"center", "center", "center"});
    } else if (use_reference) {
        if (!reference_available) {
            target_bounds.valid = true;
            target_bounds.minimum = {0.0, 0.0, 0.0};
            target_bounds.maximum = {0.0, 0.0, 0.0};
        } else {
            const bool use_target_group =
                options.use_groups_for_bounds && !options.target_group.empty();
            const auto target_bound_points = points_for_bounds(
                *reference,
                use_target_group ? options.target_group : std::string{},
                use_target_group ? options.target_group_type : options.group_type,
                use_target_group);
            target_bounds = bounds_of(target_bound_points);
            if (!target_bounds.valid) {
                error = "MatchSize reference has no points";
                return false;
            }
        }
    } else {
        if (options.target_size.x < 0.0 || options.target_size.y < 0.0 ||
            options.target_size.z < 0.0) {
            error = "MatchSize target size must be non-negative";
            return false;
        }
        target_bounds = bounds_from_position_size(
            options.target_position, options.target_size, target_justify);
    }

    const auto source_size = bounds_size(source_bounds);
    const auto target_size = bounds_size(target_bounds);
    data::PcgVec3 scale{1.0, 1.0, 1.0};
    if (options.scale_to_fit) {
        const std::array<double, 3> source_axes{source_size.x, source_size.y, source_size.z};
        const std::array<double, 3> target_axes{target_size.x, target_size.y, target_size.z};
        std::array<double, 3> ratios{1.0, 1.0, 1.0};
        std::vector<double> valid_ratios;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(source_axes[static_cast<size_t>(axis)]) > kEpsilon) {
                ratios[static_cast<size_t>(axis)] =
                    target_axes[static_cast<size_t>(axis)] /
                    source_axes[static_cast<size_t>(axis)];
                valid_ratios.push_back(ratios[static_cast<size_t>(axis)]);
            }
        }

        if (options.uniform_scale) {
            double uniform = 1.0;
            if (options.scale_axis == "x") {
                uniform = ratios[0];
            } else if (options.scale_axis == "y") {
                uniform = ratios[1];
            } else if (options.scale_axis == "z") {
                uniform = ratios[2];
            } else if (options.scale_axis == "fill" && !valid_ratios.empty()) {
                uniform = *std::max_element(valid_ratios.begin(), valid_ratios.end());
            } else if (!valid_ratios.empty()) {
                // bestFit (and legacy "fit")
                uniform = *std::min_element(valid_ratios.begin(), valid_ratios.end());
            }
            scale = {uniform, uniform, uniform};
        } else {
            scale = {
                options.scale_x ? ratios[0] : 1.0,
                options.scale_y ? ratios[1] : 1.0,
                options.scale_z ? ratios[2] : 1.0,
            };
        }
    }

    const auto source_anchor = bounds_anchor_axis(source_bounds, source_justify);
    const auto target_anchor = bounds_anchor_axis(target_bounds, target_justify);
    data::PcgVec3 translation{};
    for (int axis = 0; axis < 3; ++axis) {
        const double src = axis == 0 ? source_anchor.x
            : axis == 1 ? source_anchor.y : source_anchor.z;
        const double tgt = axis == 0 ? target_anchor.x
            : axis == 1 ? target_anchor.y : target_anchor.z;
        const double off = axis == 0 ? options.offset.x
            : axis == 1 ? options.offset.y : options.offset.z;
        const double s = axis == 0 ? scale.x : axis == 1 ? scale.y : scale.z;
        const std::string& mode = source_justify[static_cast<size_t>(axis)];
        const bool move = options.translate && mode != "none";
        const double value = move ? (tgt + off - s * src) : (src - s * src);
        if (axis == 0) translation.x = value;
        else if (axis == 1) translation.y = value;
        else translation.z = value;
    }

    data::GeometryAffineTransform affine;
    affine.linear = {scale.x, 0.0, 0.0,
                     0.0, scale.y, 0.0,
                     0.0, 0.0, scale.z};
    affine.translation = translation;

    const bool transform_all =
        options.group.empty() &&
        transform_points.size() == output.points().size();
    apply_affine_to_selected(
        output, affine, transform_all ? nullptr : &transform_points);

    if (options.stash_transform && !options.stash_attribute.empty()) {
        output.attributes().erase(data::AttributeOwner::Detail, options.stash_attribute);
        auto& matrix = output.attributes().create_float(
            data::AttributeOwner::Detail, options.stash_attribute, 16,
            std::vector<double>(16, 0.0), data::AttributeTransformRole::Matrix);
        matrix.float_values_mut() = {
            scale.x, 0.0, 0.0, translation.x,
            0.0, scale.y, 0.0, translation.y,
            0.0, 0.0, scale.z, translation.z,
            0.0, 0.0, 0.0, 1.0,
        };
    }
    return true;
}

bool bend_geometry(const data::PcgGeometry& source,
                   const data::PcgGeometry* rest,
                   const BendMeshOptions& options,
                   data::PcgGeometry& output,
                   std::string& error)
{
    if (source.points().empty()) {
        error = "BendMesh source has no points";
        return false;
    }
    if (!std::isfinite(options.capture_length) || options.capture_length <= kEpsilon) {
        error = "BendMesh captureLength must be greater than zero";
        return false;
    }
    if (rest && (rest->points().size() != source.points().size() ||
                 rest->faces() != source.faces())) {
        error = "BendMesh rest input must match source topology";
        return false;
    }
    const auto axis = normalize(options.capture_direction);
    auto radial = subtract(options.up_direction, multiply(axis, dot(options.up_direction, axis)));
    radial = normalize(radial);
    if (length(axis) <= kEpsilon || length(radial) <= kEpsilon) {
        error = "BendMesh captureDirection and upDirection must be non-parallel vectors";
        return false;
    }
    const BendFrame frame{options.capture_origin, radial, normalize(cross(axis, radial)), axis,
                          options.capture_length, options.angle_degrees * kPi / 180.0};
    const data::PcgGeometry& rest_geometry = rest ? *rest : source;

    output = source;
    for (size_t i = 0; i < source.points().size(); ++i) {
        const auto deformed_rest = bend_position(frame, rest_geometry.points()[i]);
        const auto offset = subtract(source.points()[i], rest_geometry.points()[i]);
        output.points_mut()[i] = add(
            deformed_rest,
            rotate_in_bend_plane(frame, offset,
                                 bend_theta(frame, rest_geometry.points()[i])));
    }
    transform_bend_attributes(output, rest_geometry, frame);

    if (!options.mask_attribute.empty()) {
        output.attributes().erase(data::AttributeOwner::Point, options.mask_attribute);
        auto& mask = output.attributes().create_float(
            data::AttributeOwner::Point, options.mask_attribute, 1, {0.0});
        mask.float_values_mut().reserve(rest_geometry.points().size());
        for (const auto& point : rest_geometry.points()) {
            const double t = dot(subtract(point, frame.origin), frame.axis);
            mask.float_values_mut().push_back(std::clamp(t / frame.length, 0.0, 1.0));
        }
    }
    return true;
}

} // namespace pcg::internal::elements
