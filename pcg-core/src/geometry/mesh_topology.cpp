#include "geometry/mesh_topology.hpp"

namespace pcg::internal::geometry {

MeshTriTopology::MeshTriTopology(const IMesh& mesh)
{
    const int nverts = static_cast<int>(mesh.verts.size());
    vert_edges_.resize(static_cast<size_t>(nverts));

    for (int t = 0; t < static_cast<int>(mesh.tris.size()); ++t) {
        const IMeshTri& tri = mesh.tris[t];
        const int verts[3] = {tri.v0, tri.v1, tri.v2};
        for (int i = 0; i < 3; ++i) {
            const int a = verts[i];
            const int b = verts[(i + 1) % 3];
            const int64_t ek = edge_key(a, b);

            auto& tris = edge_tris_[ek];
            if (tris.empty() || tris.back() != t) {
                tris.push_back(t);
            }

            auto& edges = vert_edges_[a];
            bool found = false;
            for (const auto& e : edges) {
                if (e.v0 == a && e.v1 == b) { found = true; break; }
            }
            if (!found) {
                edges.emplace_back(a, b);
            }
        }
    }
}

const std::vector<int>* MeshTriTopology::edge_tris(int64_t ek) const
{
    auto it = edge_tris_.find(ek);
    if (it == edge_tris_.end()) return nullptr;
    return &it->second;
}

int MeshTriTopology::other_tri_if_manifold(int64_t ek, int tri) const
{
    const auto* tris = edge_tris(ek);
    if (!tris || tris->size() != 2) return -1;
    return ((*tris)[0] == tri) ? (*tris)[1] : (*tris)[0];
}

const std::vector<TopoEdge>& MeshTriTopology::vert_edges(int v) const
{
    return vert_edges_.at(static_cast<size_t>(v));
}

} // namespace pcg::internal::geometry
