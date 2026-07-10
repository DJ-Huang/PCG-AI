#include "data/pcg_geometry.hpp"

#include "geometry/bmesh.hpp"

#include <unordered_map>

namespace pcg::internal::data {

PcgMeshData triangulate_geometry(const PcgGeometry& geometry)
{
    PcgMeshData mesh;
    // Duplicate vertices per face for flat shading — each face gets its own
    // vertices so that Unity's RecalculateNormals() produces correct per-face
    // normals instead of smoothing across hard edges between adjacent faces.
    for (const auto& face : geometry.faces()) {
        if (face.size() < 3)
            continue;
        std::vector<int> local;
        local.reserve(face.size());
        for (int idx : face) {
            const auto& p = geometry.points()[static_cast<size_t>(idx)];
            local.push_back(static_cast<int>(mesh.vertices().size()));
            mesh.add_vertex({p.x, p.y, p.z});
        }
        const int i0 = local[0];
        for (size_t i = 1; i + 1 < local.size(); ++i)
            mesh.add_triangle(i0, local[i], local[i + 1]);
    }

    return mesh;
}

PcgGeometry geometry_from_mesh(const PcgMeshData& mesh)
{
    const geometry::BMesh bmesh = geometry::bmesh_from_mesh(mesh);
    return geometry::geometry_from_bmesh(bmesh);
}

std::vector<int64_t> geometry_edge_keys(const PcgGeometry& geometry)
{
    std::unordered_map<int64_t, int> face_count;
    for (const auto& face : geometry.faces()) {
        const int n = static_cast<int>(face.size());
        for (int i = 0; i < n; ++i) {
            const int a = face[static_cast<size_t>(i)];
            const int b = face[static_cast<size_t>((i + 1) % n)];
            face_count[geometry::edge_key(a, b)]++;
        }
    }

    std::vector<int64_t> keys;
    keys.reserve(face_count.size());
    for (const auto& entry : face_count)
        keys.push_back(entry.first);
    return keys;
}

void maintain_unshared_edge_group(PcgGeometry& geometry, const std::string& name)
{
    std::unordered_map<int64_t, int> use_count;
    for (const auto& face : geometry.faces()) {
        const int n = static_cast<int>(face.size());
        for (int i = 0; i < n; ++i) {
            const int a = face[static_cast<size_t>(i)];
            const int b = face[static_cast<size_t>((i + 1) % n)];
            use_count[geometry::edge_key(a, b)]++;
        }
    }

    geometry.groups().clear_group(geometry::GroupDomain::Edge, name);
    for (const auto& entry : use_count) {
        if (entry.second == 1)
            geometry.groups().add(geometry::GroupDomain::Edge, name, static_cast<int>(entry.first));
    }
}

PcgGeometry merge_geometries(const PcgGeometry& a, const PcgGeometry& b, const std::string& b_prefix)
{
    PcgGeometry merged = a;
    const int point_offset = static_cast<int>(merged.points().size());
    const int face_offset = static_cast<int>(merged.faces().size());

    for (const auto& p : b.points())
        merged.points_mut().push_back(p);

    for (const auto& face : b.faces()) {
        std::vector<int> remapped;
        remapped.reserve(face.size());
        for (int idx : face)
            remapped.push_back(idx + point_offset);
        merged.faces_mut().push_back(std::move(remapped));
    }

    for (geometry::GroupDomain domain :
         {geometry::GroupDomain::Point, geometry::GroupDomain::Face, geometry::GroupDomain::Edge}) {
        for (const std::string& name : b.groups().group_names(domain)) {
            const std::string out_name = b_prefix + name;
            for (int id : b.groups().members(domain, name)) {
                if (domain == geometry::GroupDomain::Point)
                    merged.groups().add(domain, out_name, id + point_offset);
                else if (domain == geometry::GroupDomain::Face)
                    merged.groups().add(domain, out_name, id + face_offset);
                else {
                    const int v0 = static_cast<int>(static_cast<int64_t>(id) / 1000000);
                    const int v1 = static_cast<int>(static_cast<int64_t>(id) % 1000000);
                    const int64_t remapped =
                        geometry::edge_key(v0 + point_offset, v1 + point_offset);
                    merged.groups().add(domain, out_name, static_cast<int>(remapped));
                }
            }
        }
    }

    return merged;
}

} // namespace pcg::internal::data
