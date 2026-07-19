#include "elements/assembly_algorithms.hpp"

#include "data/pcg_attribute_table.hpp"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>

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

data::PcgVec3 bounds_size(const Bounds& bounds)
{
    return subtract(bounds.maximum, bounds.minimum);
}

data::PcgVec3 bounds_anchor(const Bounds& bounds,
                            const std::array<std::string, 3>& justify)
{
    const auto size = bounds_size(bounds);
    return {bounds.minimum.x + size.x * justify_value(justify[0]),
            bounds.minimum.y + size.y * justify_value(justify[1]),
            bounds.minimum.z + size.z * justify_value(justify[2])};
}

data::PcgVec3 apply_scale(const data::PcgVec3& value,
                          const data::PcgVec3& scale)
{
    return {value.x * scale.x, value.y * scale.y, value.z * scale.z};
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
    const Bounds source_bounds = bounds_of(source.points());
    if (!source_bounds.valid) {
        error = "MatchSize source has no points";
        return false;
    }
    Bounds target_bounds;
    if (reference) {
        target_bounds = bounds_of(reference->points());
        if (!target_bounds.valid) {
            error = "MatchSize reference has no points";
            return false;
        }
    } else {
        if (options.target_size.x < 0.0 || options.target_size.y < 0.0 ||
            options.target_size.z < 0.0) {
            error = "MatchSize target size must be non-negative";
            return false;
        }
        target_bounds.valid = true;
        target_bounds.minimum = subtract(options.target_center, multiply(options.target_size, 0.5));
        target_bounds.maximum = add(options.target_center, multiply(options.target_size, 0.5));
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
            if (std::abs(source_axes[axis]) > kEpsilon) {
                ratios[axis] = target_axes[axis] / source_axes[axis];
                valid_ratios.push_back(ratios[axis]);
            }
        }
        scale = {ratios[0], ratios[1], ratios[2]};
        if (options.uniform_scale && !valid_ratios.empty()) {
            const double uniform = options.uniform_scale_mode == "fill"
                ? *std::max_element(valid_ratios.begin(), valid_ratios.end())
                : *std::min_element(valid_ratios.begin(), valid_ratios.end());
            scale = {uniform, uniform, uniform};
        }
    }

    const auto source_anchor = bounds_anchor(
        source_bounds, {options.source_justify_x, options.source_justify_y,
                        options.source_justify_z});
    const auto target_anchor = bounds_anchor(
        target_bounds, {options.target_justify_x, options.target_justify_y,
                        options.target_justify_z});
    const auto translation = subtract(target_anchor, apply_scale(source_anchor, scale));

    output = source;
    for (auto& point : output.points_mut())
        point = add(apply_scale(point, scale), translation);
    data::GeometryAffineTransform affine;
    affine.linear = {scale.x, 0.0, 0.0,
                     0.0, scale.y, 0.0,
                     0.0, 0.0, scale.z};
    affine.translation = translation;
    data::transform_geometry_attributes(output, affine);
    output.attributes().erase(data::AttributeOwner::Detail, "pcg_match_xform");
    auto& matrix = output.attributes().create_float(
        data::AttributeOwner::Detail, "pcg_match_xform", 16,
        std::vector<double>(16, 0.0), data::AttributeTransformRole::Matrix);
    matrix.float_values_mut() = {
        scale.x, 0.0, 0.0, translation.x,
        0.0, scale.y, 0.0, translation.y,
        0.0, 0.0, scale.z, translation.z,
        0.0, 0.0, 0.0, 1.0,
    };
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
