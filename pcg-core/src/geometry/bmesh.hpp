#pragma once

// Minimal BMesh-style polygon mesh for PCG geometry ops (bevel tier-1).
// Faces may be n-gons; edges carry manifold adjacency, sharp flags, and groups.

#include "data/pcg_mesh_data.hpp"

#include <cstdint>

namespace pcg::internal::data {
class PcgGeometry;
}
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pcg::internal::geometry {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct BMeshFace {
    /// CCW boundary vertex indices.
    std::vector<int> verts;
    /// Welded triangle indices that were merged into this face.
    std::vector<int> triangle_indices;
    std::unordered_set<std::string> groups;
};

struct BMeshEdge {
    int v0 = 0;
    int v1 = 0;
    int face0 = -1;
    int face1 = -1;
    bool sharp = false;
    std::unordered_set<std::string> groups;
};

/// One entry in a vertex's disk cycle — an ordered edge around the vertex.
/// Built from face loop topology, not normal-projection sorting.
struct BMeshDiskEntry {
    int other_v = -1;   ///< The other vertex of this edge
    int fprev = -1;     ///< Face between this edge and the previous edge (CCW)
    int fnext = -1;     ///< Face between this edge and the next edge (CCW)
};

struct BMeshBuildOptions {
    double weld_eps = 1e-6;
    /// Merge coplanar faces within this angle (degrees).
    /// Applied by both bmesh_from_mesh (tris) and bmesh_from_geometry (n-gons).
    double merge_coplanar_angle_deg = 2.0;
    /// Mark edges sharper than this as bevel candidates (degrees).
    double sharp_angle_deg = 30.0;
    /// Dissolve valence-2 collinear boundary vertices (Simple-subdiv edge mids).
    /// Only effective when merge_coplanar_angle_deg > 0. Default false:
    /// dissolve is opt-in because it removes topologically meaningful vertices
    /// from Boolean CSG output and other non-bevel paths.
    bool dissolve_collinear = false;
    /// Optional per-face reconstruction key. When one key is supplied for
    /// every input face, adjacent faces merge only when their non-negative keys
    /// match. This is used by Boolean output to reconstruct one source polygon
    /// without dissolving unrelated coplanar faces.
    std::vector<int64_t> face_merge_keys;
    /// Do not merge across edges that belong to any input edge group.
    bool preserve_grouped_edges = false;
};

struct BMesh {
    std::vector<Vec3> verts;
    std::vector<BMeshFace> faces;
    std::unordered_map<int64_t, BMeshEdge> edges;
    /// Per-vertex ordered edge list (disk cycle). Key = vertex index.
    std::unordered_map<int, std::vector<BMeshDiskEntry>> disk_cycles;
};

int64_t edge_key(int a, int b);

/// Build a polygon mesh from triangle soup (weld + coplanar merge).
BMesh bmesh_from_mesh(const data::PcgMeshData& mesh, const BMeshBuildOptions& options = {});

/// Build BMesh from canonical geometry; preserves edge/face groups.
/// Merges adjacent coplanar n-gons (merge_coplanar_angle_deg).
/// Dissolves valence-2 collinear boundary verts only when dissolve_collinear=true.
BMesh bmesh_from_geometry(const data::PcgGeometry& geometry,
                            const BMeshBuildOptions& options = {});

/// Convert BMesh back to canonical geometry (groups preserved).
data::PcgGeometry geometry_from_bmesh(const BMesh& mesh);

/// Triangulate polygon faces back to render mesh.
data::PcgMeshData mesh_from_bmesh(const BMesh& mesh);

Vec3 face_normal(const BMesh& mesh, int face_index);
Vec3 face_normal_from_loop(const BMesh& mesh, const std::vector<int>& loop);

/// Build disk cycles from face loops (call after build_edges + mark_sharp_edges).
void build_disk_cycles(BMesh& mesh);

} // namespace pcg::internal::geometry
