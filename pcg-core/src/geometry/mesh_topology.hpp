#pragma once

// Triangle mesh topology for IMesh (edge → incident triangles).
// Used by boolean CSG patch/cell construction (Blender TriMeshTopology).

#include "geometry/imesh.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace pcg::internal::geometry {

/// Directed edge between two vertex indices.
struct TopoEdge {
    int v0 = 0;
    int v1 = 0;

    TopoEdge() = default;
    TopoEdge(int a, int b) : v0(a), v1(b) {}

    int64_t key() const { return edge_key(v0, v1); }

    bool operator==(const TopoEdge& o) const { return v0 == o.v0 && v1 == o.v1; }
};

struct TopoEdgeHash {
    size_t operator()(const TopoEdge& e) const {
        return static_cast<size_t>(e.key());
    }
};

/// Topology of a triangulated IMesh.
struct MeshTriTopology {
    explicit MeshTriTopology(const IMesh& mesh);

    /// Triangles sharing this undirected edge (0, 1, or 2+ entries).
    const std::vector<int>* edge_tris(int64_t ek) const;

    /// If edge is manifold (exactly 2 tris), return the other triangle index.
    int other_tri_if_manifold(int64_t ek, int tri) const;

    /// Edges incident on vertex v.
    const std::vector<TopoEdge>& vert_edges(int v) const;

    const std::unordered_map<int64_t, std::vector<int>>& edge_tris_map() const { return edge_tris_; }

private:
    std::unordered_map<int64_t, std::vector<int>> edge_tris_;
    std::vector<std::vector<TopoEdge>> vert_edges_;
};

} // namespace pcg::internal::geometry
