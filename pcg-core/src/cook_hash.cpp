#include "cook_hash.hpp"

#include "geometry/group_table.hpp"
#include "mesh_runtime.hpp"
#include "asset_path.hpp"
#include "spline_runtime.hpp"
#include "texture_runtime.hpp"
#include "heightfield_runtime.hpp"

#include <algorithm>
#include <vector>

namespace pcg::internal {
namespace {

uint64_t fnv1a(const void* data, std::size_t size, uint64_t seed)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i)
        seed = (seed ^ static_cast<uint64_t>(bytes[i])) * kFnvPrime;
    return seed;
}

} // namespace

uint64_t hash_bytes(const void* data, std::size_t size, uint64_t seed)
{
    return fnv1a(data, size, seed);
}

uint64_t hash_string(const std::string& value)
{
    return fnv1a(value.data(), value.size(), kFnvOffsetBasis);
}

uint64_t hash_json(const nlohmann::json& value)
{
    const std::string dumped = value.dump();
    return hash_string(dumped);
}

uint64_t hash_mesh(const data::PcgMeshData& mesh)
{
    uint64_t h = kFnvOffsetBasis;
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    h = hash_combine(h, static_cast<uint64_t>(verts.size()));
    h = hash_combine(h, static_cast<uint64_t>(tris.size()));

    for (const auto& v : verts) {
        h = hash_bytes(&v.x, sizeof(double), h);
        h = hash_bytes(&v.y, sizeof(double), h);
        h = hash_bytes(&v.z, sizeof(double), h);
    }
    if (!tris.empty())
        h = hash_bytes(tris.data(), tris.size() * sizeof(int), h);

    h = hash_combine(h, mesh.has_normals() ? 1u : 0u);
    if (mesh.has_normals()) {
        for (const auto& n : mesh.normals()) {
            h = hash_bytes(&n.x, sizeof(double), h);
            h = hash_bytes(&n.y, sizeof(double), h);
            h = hash_bytes(&n.z, sizeof(double), h);
        }
    }

    h = hash_combine(h, mesh.has_colors() ? 1u : 0u);
    if (mesh.has_colors()) {
        for (const auto& c : mesh.colors()) {
            h = hash_bytes(&c.r, sizeof(double), h);
            h = hash_bytes(&c.g, sizeof(double), h);
            h = hash_bytes(&c.b, sizeof(double), h);
            h = hash_bytes(&c.a, sizeof(double), h);
        }
    }

    h = hash_combine(h, mesh.has_uvs() ? 1u : 0u);
    if (mesh.has_uvs()) {
        for (const auto& uv : mesh.uvs()) {
            h = hash_bytes(&uv.u, sizeof(double), h);
            h = hash_bytes(&uv.v, sizeof(double), h);
        }
    }

    h = hash_combine(h, mesh.has_materials() ? 1u : 0u);
    if (mesh.has_materials()) {
        for (const std::string& name : mesh.material_slots())
            h = hash_combine(h, hash_string(name));
        if (!mesh.triangle_materials().empty()) {
            h = hash_bytes(mesh.triangle_materials().data(),
                           mesh.triangle_materials().size() * sizeof(uint32_t), h);
        }
    }

    h = hash_combine(h, hash_json(mesh.metadata().raw()));
    return h;
}

uint64_t hash_geometry(const data::PcgGeometry& geometry)
{
    uint64_t h = kFnvOffsetBasis;
    const auto& points = geometry.points();
    const auto& faces = geometry.faces();
    h = hash_combine(h, static_cast<uint64_t>(points.size()));
    h = hash_combine(h, static_cast<uint64_t>(faces.size()));

    for (const auto& p : points) {
        h = hash_bytes(&p.x, sizeof(double), h);
        h = hash_bytes(&p.y, sizeof(double), h);
        h = hash_bytes(&p.z, sizeof(double), h);
    }
    for (const auto& face : faces) {
        h = hash_combine(h, static_cast<uint64_t>(face.size()));
        if (!face.empty())
            h = hash_bytes(face.data(), face.size() * sizeof(int), h);
    }

    for (geometry::GroupDomain domain :
         {geometry::GroupDomain::Point, geometry::GroupDomain::Edge,
          geometry::GroupDomain::Face, geometry::GroupDomain::Vertex}) {
        for (const std::string& name : geometry.groups().group_names(domain)) {
            h = hash_combine(h, hash_string(name));
            const auto& members = geometry.groups().members(domain, name);
            std::vector<geometry::GroupId> sorted(members.begin(), members.end());
            std::sort(sorted.begin(), sorted.end());
            if (!sorted.empty())
                h = hash_bytes(sorted.data(), sorted.size() * sizeof(geometry::GroupId), h);
        }
    }

    const auto& detail = geometry.detail();
    h = hash_combine(h, static_cast<uint64_t>(detail.shade_mode));
    double cusp = detail.cusp_angle_deg;
    h = hash_bytes(&cusp, sizeof(double), h);
    h = hash_combine(h, hash_json(geometry.metadata().raw()));

    if (geometry.has_colors() && !geometry.colors().empty())
        h = hash_bytes(geometry.colors().data(),
                       geometry.colors().size() * sizeof(data::PcgColor), h);
    if (geometry.has_uvs() && !geometry.uvs().empty())
        h = hash_bytes(geometry.uvs().data(),
                       geometry.uvs().size() * sizeof(data::PcgVec2), h);
    if (geometry.has_corner_uvs() && !geometry.corner_uvs().empty())
        h = hash_bytes(geometry.corner_uvs().data(),
                       geometry.corner_uvs().size() * sizeof(data::PcgVec2), h);

    for (data::AttributeOwner owner :
         {data::AttributeOwner::Point, data::AttributeOwner::Vertex,
          data::AttributeOwner::Primitive, data::AttributeOwner::Detail}) {
        for (const std::string& name : geometry.attributes().names(owner)) {
            const data::AttributeArray* attribute = geometry.attributes().find(owner, name);
            if (!attribute)
                continue;
            const auto& schema = attribute->schema();
            h = hash_combine(h, hash_string(name));
            h = hash_combine(h, static_cast<uint64_t>(schema.owner));
            h = hash_combine(h, static_cast<uint64_t>(schema.type));
            h = hash_combine(h, static_cast<uint64_t>(schema.tuple_size));
            h = hash_combine(h, static_cast<uint64_t>(schema.transform_role));
            if (!attribute->default_int().empty())
                h = hash_bytes(attribute->default_int().data(),
                               attribute->default_int().size() * sizeof(int64_t), h);
            if (!attribute->default_float().empty())
                h = hash_bytes(attribute->default_float().data(),
                               attribute->default_float().size() * sizeof(double), h);
            for (const std::string& value : attribute->default_string())
                h = hash_combine(h, hash_string(value));
            if (!attribute->int_values().empty())
                h = hash_bytes(attribute->int_values().data(),
                               attribute->int_values().size() * sizeof(int64_t), h);
            if (!attribute->float_values().empty())
                h = hash_bytes(attribute->float_values().data(),
                               attribute->float_values().size() * sizeof(double), h);
            for (const std::string& value : attribute->string_values())
                h = hash_combine(h, hash_string(value));
        }
    }

    h = hash_combine(h, geometry.has_face_materials() ? 1u : 0u);
    if (geometry.has_face_materials()) {
        for (const std::string& name : geometry.face_materials())
            h = hash_combine(h, hash_string(name));
    } else if (geometry.has_material()) {
        h = hash_combine(h, hash_string(geometry.material_name()));
    }

    return h;
}

uint64_t hash_heightfield(const data::PcgHeightField& heightfield)
{
    uint64_t h = kFnvOffsetBasis;
    h = hash_combine(h, static_cast<uint64_t>(heightfield.resolution_x()));
    h = hash_combine(h, static_cast<uint64_t>(heightfield.resolution_z()));
    const double size_x = heightfield.size_x();
    const double size_z = heightfield.size_z();
    h = hash_bytes(&size_x, sizeof(double), h);
    h = hash_bytes(&size_z, sizeof(double), h);
    const auto& center = heightfield.center();
    h = hash_bytes(&center.x, sizeof(double), h);
    h = hash_bytes(&center.y, sizeof(double), h);
    h = hash_bytes(&center.z, sizeof(double), h);
    h = hash_combine(h, static_cast<uint64_t>(heightfield.sampling()));
    h = hash_combine(h, static_cast<uint64_t>(heightfield.orientation()));

    for (const auto& [name, layer] : heightfield.layers()) {
        h = hash_combine(h, hash_string(name));
        h = hash_combine(h, static_cast<uint64_t>(layer.tuple_size));
        h = hash_combine(h, static_cast<uint64_t>(layer.border_type));
        h = hash_bytes(&layer.border_value, sizeof(float), h);
        if (!layer.values.empty())
            h = hash_bytes(layer.values.data(), layer.values.size() * sizeof(float), h);
    }
    return h;
}

uint64_t hash_points(const data::PcgPointData& points)
{
    uint64_t h = kFnvOffsetBasis;
    h = hash_combine(h, static_cast<uint64_t>(points.points().size()));
    for (const auto& point : points.points()) {
        h = hash_bytes(&point.x, sizeof(double), h);
        h = hash_bytes(&point.y, sizeof(double), h);
        h = hash_bytes(&point.z, sizeof(double), h);
        h = hash_combine(h, hash_json(point.attributes));
    }
    h = hash_combine(h, hash_json(points.metadata().raw()));
    return h;
}

uint64_t hash_splines(const data::PcgSplineData& splines)
{
    uint64_t h = kFnvOffsetBasis;
    h = hash_combine(h, static_cast<uint64_t>(splines.splines().size()));
    for (const auto& spline : splines.splines()) {
        h = hash_combine(h, static_cast<uint64_t>(spline.points.size()));
        for (const auto& point : spline.points) {
            h = hash_bytes(&point.x, sizeof(double), h);
            h = hash_bytes(&point.y, sizeof(double), h);
            h = hash_bytes(&point.z, sizeof(double), h);
        }
        h = hash_combine(h, static_cast<uint64_t>(spline.closed));
        h = hash_combine(h, hash_json(spline.attributes));
    }
    h = hash_combine(h, hash_json(splines.metadata().raw()));
    return h;
}

uint64_t hash_texture(const data::PcgTextureData& texture)
{
    uint64_t h = kFnvOffsetBasis;
    h = hash_combine(h, static_cast<uint64_t>(texture.width()));
    h = hash_combine(h, static_cast<uint64_t>(texture.height()));
    const auto& rgba = texture.rgba();
    if (!rgba.empty())
        h = hash_bytes(rgba.data(), rgba.size() * sizeof(float), h);
    return h;
}

uint64_t hash_collection(const data::PcgDataCollection& collection)
{
    uint64_t h = kFnvOffsetBasis;
    for (const auto& item : collection.items()) {
        h = hash_combine(h, hash_string(item.tag));
        h = hash_combine(h, static_cast<uint64_t>(item.type));
        if (item.mesh)
            h = hash_combine(h, hash_mesh(*item.mesh));
        else if (item.geometry)
            h = hash_combine(h, hash_geometry(*item.geometry));
        else if (item.heightfield)
            h = hash_combine(h, hash_heightfield(*item.heightfield));
        else if (item.points) {
            h = hash_combine(h, hash_points(*item.points));
            h = hash_combine(h, hash_json(item.payload));
        }
        else if (item.splines)
            h = hash_combine(h, hash_splines(*item.splines));
        else
            h = hash_combine(h, hash_json(item.payload));
    }
    return h;
}

uint64_t compute_graph_structure_hash(const Graph& graph)
{
    uint64_t h = kFnvOffsetBasis;
    h = hash_combine(h, hash_string(graph.version));

    std::vector<std::string> node_keys;
    node_keys.reserve(graph.nodes.size());
    for (const auto& node : graph.nodes)
        node_keys.push_back(node.id + "\0" + node.type);
    std::sort(node_keys.begin(), node_keys.end());
    for (const auto& key : node_keys)
        h = hash_combine(h, hash_string(key));

    std::vector<std::string> edge_keys;
    edge_keys.reserve(graph.edges.size());
    for (const auto& edge : graph.edges) {
        edge_keys.push_back(edge.id + "\0" + edge.source + "\0" + edge.target + "\0" +
                            edge.source_handle + "\0" + edge.target_handle);
    }
    std::sort(edge_keys.begin(), edge_keys.end());
    for (const auto& key : edge_keys)
        h = hash_combine(h, hash_string(key));

    return h;
}

uint64_t compute_node_input_hash(const GraphNode& node,
                                 int seed,
                                 const std::vector<std::pair<std::string, uint64_t>>& upstream_hashes,
                                 const TextureRuntime* textures,
                                 const MeshRuntime* meshes,
                                 const SplineRuntime* splines,
                                 const HeightFieldRuntime* heightfields)
{
    uint64_t h = hash_string(node.type);
    h = hash_combine(h, hash_json(node.data));
    h = hash_combine(h, static_cast<uint64_t>(seed));

    for (const auto& [pin, up_hash] : upstream_hashes) {
        h = hash_combine(h, hash_string(pin));
        h = hash_combine(h, up_hash);
    }

    if (node.type == "GetMeshData" && meshes) {
        if (const data::PcgMeshData* mesh = meshes->find(node.id))
            h = hash_combine(h, hash_mesh(*mesh));
    }

    if (node.type == "ImportMesh" || node.type == "PreserveGltfRig" ||
        node.type == "Meshy3DGenerator" || node.type == "Tripo3DGenerator" ||
        node.type == "MeshyTextTo3D" || node.type == "MeshyMeshOps" || node.type == "MeshyRetexture") {
        if (meshes) {
            if (const data::PcgMeshData* mesh = meshes->find(node.id))
                h = hash_combine(h, hash_mesh(*mesh));
        }
        h = hash_combine(h, hash_asset_dependency(resolve_asset_path(node.data)));
    }

    if (node.type == "GetSplineData" && splines) {
        if (const data::PcgSplineData* spline = splines->find(node.id))
            h = hash_combine(h, hash_splines(*spline));
    }

    if ((node.type == "ImageTexture" || node.type == "MeshyImageGen") && textures) {
        if (const data::PcgTextureData* tex = textures->find(node.id))
            h = hash_combine(h, hash_texture(*tex));
    }

    if (node.type == "GetTerrainData" && heightfields) {
        if (const data::PcgHeightField* heightfield = heightfields->find(node.id))
            h = hash_combine(h, hash_heightfield(*heightfield));
    }

    return h;
}

uint64_t compute_output_hash(const data::PcgDataCollection& outputs)
{
    return hash_collection(outputs);
}

} // namespace pcg::internal
