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

struct BMeshBuildOptions {
    double weld_eps = 1e-6;
    /// Merge coplanar triangles within this angle (degrees).
    double merge_coplanar_angle_deg = 2.0;
    /// Mark edges sharper than this as bevel candidates (degrees).
    double sharp_angle_deg = 30.0;
};

struct BMesh {
    std::vector<Vec3> verts;
    std::vector<BMeshFace> faces;
    std::unordered_map<int64_t, BMeshEdge> edges;
};

int64_t edge_key(int a, int b);

/// Build a polygon mesh from triangle soup (weld + coplanar merge).
BMesh bmesh_from_mesh(const data::PcgMeshData& mesh, const BMeshBuildOptions& options = {});

/// Build BMesh from canonical geometry; preserves edge/face groups.
BMesh bmesh_from_geometry(const data::PcgGeometry& geometry,
                            const BMeshBuildOptions& options = {});

/// Convert BMesh back to canonical geometry (groups preserved).
data::PcgGeometry geometry_from_bmesh(const BMesh& mesh);

/// Fan-triangulate n-gon faces back to render mesh.
data::PcgMeshData mesh_from_bmesh(const BMesh& mesh);

Vec3 face_normal(const BMesh& mesh, int face_index);
Vec3 face_normal_from_loop(const BMesh& mesh, const std::vector<int>& loop);

} // namespace pcg::internal::geometry
