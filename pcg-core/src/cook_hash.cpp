#include "cook_hash.hpp"

#include "mesh_runtime.hpp"
#include "spline_runtime.hpp"
#include "texture_runtime.hpp"

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

    h = hash_combine(h, hash_json(mesh.metadata().raw()));
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
                                 const SplineRuntime* splines)
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

    if (node.type == "GetSplineData" && splines) {
        if (const data::PcgSplineData* spline = splines->find(node.id))
            h = hash_combine(h, hash_splines(*spline));
    }

    if (node.type == "ImageTexture" && textures) {
        if (const data::PcgTextureData* tex = textures->find(node.id))
            h = hash_combine(h, hash_texture(*tex));
    }

    return h;
}

uint64_t compute_output_hash(const data::PcgDataCollection& outputs)
{
    return hash_collection(outputs);
}

} // namespace pcg::internal
