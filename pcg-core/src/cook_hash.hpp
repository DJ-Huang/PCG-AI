#pragma once

#include "data/pcg_data_collection.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_texture_data.hpp"
#include "internal/graph_types.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace pcg::internal {

class MeshRuntime;
class SplineRuntime;
class TextureRuntime;

/** FNV-1a 64-bit (Blender-style content addressing for cook cache). */
constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

inline uint64_t hash_combine(uint64_t seed, uint64_t value)
{
    return seed ^ (value + kFnvPrime + (seed << 6) + (seed >> 2));
}

uint64_t hash_bytes(const void* data, std::size_t size, uint64_t seed = kFnvOffsetBasis);
uint64_t hash_string(const std::string& value);
uint64_t hash_json(const nlohmann::json& value);
uint64_t hash_mesh(const data::PcgMeshData& mesh);
uint64_t hash_geometry(const data::PcgGeometry& geometry);
uint64_t hash_points(const data::PcgPointData& points);
uint64_t hash_splines(const data::PcgSplineData& splines);
uint64_t hash_texture(const data::PcgTextureData& texture);
uint64_t hash_collection(const data::PcgDataCollection& collection);

/** Topology-only fingerprint — param edits do not invalidate the whole cache. */
uint64_t compute_graph_structure_hash(const Graph& graph);

/** Per-node input hash: type + params + seed + upstream output hashes + runtime slots. */
uint64_t compute_node_input_hash(const GraphNode& node,
                                 int seed,
                                 const std::vector<std::pair<std::string, uint64_t>>& upstream_hashes,
                                 const TextureRuntime* textures,
                                 const MeshRuntime* meshes,
                                 const SplineRuntime* splines);

uint64_t compute_output_hash(const data::PcgDataCollection& outputs);

} // namespace pcg::internal
