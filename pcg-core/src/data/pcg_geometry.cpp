#include "data/pcg_geometry.hpp"

#include "geometry/bmesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

PcgMeshData triangulate_geometry_shared(const PcgGeometry& geometry)
{
    PcgMeshData mesh;
    for (const auto& p : geometry.points())
        mesh.add_vertex({p.x, p.y, p.z});
    for (const auto& face : geometry.faces()) {
        if (face.size() < 3)
            continue;
        const int i0 = face[0];
        for (size_t i = 1; i + 1 < face.size(); ++i)
            mesh.add_triangle(i0, face[i], face[i + 1]);
    }
    return mesh;
}

namespace {

struct CornerInfo {
    int point_index = 0;
    int face_index = 0;
};

struct EdgeIncidence {
    int face_index = 0;
    int corner_min = 0; // corner at the smaller point index
    int corner_max = 0; // corner at the larger point index
};

} // namespace

PcgMeshData compute_split_normals(const PcgGeometry& geometry, const NormalComputeOptions& options)
{
    PcgMeshData mesh;
    const auto& points = geometry.points();
    const auto& faces = geometry.faces();

    // --- Step 1: Build polygon corner topology ---

    std::vector<CornerInfo> corners;
    std::vector<int> face_corner_offset;
    face_corner_offset.reserve(faces.size());

    for (size_t fi = 0; fi < faces.size(); ++fi) {
        face_corner_offset.push_back(static_cast<int>(corners.size()));
        const auto& face = faces[fi];
        for (int pt_idx : face)
            corners.push_back({pt_idx, static_cast<int>(fi)});
    }

    // edge_key -> incidences
    std::unordered_map<int64_t, std::vector<EdgeIncidence>> edge_incidences;
    int total_corners = static_cast<int>(corners.size());

    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const auto& face = faces[fi];
        const int n = static_cast<int>(face.size());
        const int base = face_corner_offset[fi];
        for (int ci = 0; ci < n; ++ci) {
            const int p0 = face[static_cast<size_t>(ci)];
            const int p1 = face[static_cast<size_t>((ci + 1) % n)];
            if (p0 == p1)
                continue; // zero-length edge
            const int corner0 = base + ci;
            const int corner1 = base + (ci + 1) % n;
            const int64_t ekey = geometry::edge_key(p0, p1);
            if (p0 < p1)
                edge_incidences[ekey].push_back({static_cast<int>(fi), corner0, corner1});
            else
                edge_incidences[ekey].push_back({static_cast<int>(fi), corner1, corner0});
        }
    }

    // --- Step 2: Compute polygon (face) normals via Newell's method ---

    std::vector<PcgVec3> face_normals(faces.size());
    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const auto& face = faces[fi];
        const int n = static_cast<int>(face.size());
        PcgVec3 normal = {0.0, 0.0, 0.0};
        for (int i = 0; i < n; ++i) {
            const auto& p0 = points[static_cast<size_t>(face[static_cast<size_t>(i)])];
            const auto& p1 = points[static_cast<size_t>(face[static_cast<size_t>((i + 1) % n)])];
            normal.x += (p0.y - p1.y) * (p0.z + p1.z);
            normal.y += (p0.z - p1.z) * (p0.x + p1.x);
            normal.z += (p0.x - p1.x) * (p0.y + p1.y);
        }
        const double len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
        if (len > 1e-12) {
            face_normals[fi] = {normal.x / len, normal.y / len, normal.z / len};
        }
    }

    // --- Step 3: Build face group sets for boundary detection ---

    std::vector<std::vector<std::string>> face_group_sets(faces.size());
    if (options.hard_group_boundaries) {
        const auto group_names = geometry.groups().group_names(geometry::GroupDomain::Face);
        for (const auto& name : group_names) {
            const auto& members = geometry.groups().members(geometry::GroupDomain::Face, name);
            for (int face_idx : members) {
                if (face_idx >= 0 && face_idx < static_cast<int>(face_group_sets.size()))
                    face_group_sets[static_cast<size_t>(face_idx)].push_back(name);
            }
        }
        for (auto& s : face_group_sets)
            std::sort(s.begin(), s.end());
    }

    // --- Step 4: Union-Find on corners ---

    std::vector<int> parent(static_cast<size_t>(total_corners));
    for (int i = 0; i < total_corners; ++i)
        parent[static_cast<size_t>(i)] = i;

    auto find = [&parent](int x) -> int {
        while (parent[static_cast<size_t>(x)] != x) {
            parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
            x = parent[static_cast<size_t>(x)];
        }
        return x;
    };
    auto unite = [&find, &parent](int x, int y) {
        int px = find(x), py = find(y);
        if (px != py)
            parent[static_cast<size_t>(px)] = py;
    };

    const double cusp_rad = options.cusp_angle_deg * 3.14159265358979323846 / 180.0;
    const double cusp_cos = std::cos(cusp_rad);

    for (const auto& [ekey, incs] : edge_incidences) {
        if (incs.size() != 2)
            continue; // boundary (1) or non-manifold (!=2): always hard

        const auto& inc0 = incs[0];
        const auto& inc1 = incs[1];

        bool soft = false;

        if (options.shade_mode == ShadeMode::Smooth) {
            soft = true;
        } else if (options.shade_mode == ShadeMode::Flat) {
            soft = false;
        } else { // Auto
            // Check group boundary
            bool group_differs = false;
            if (options.hard_group_boundaries) {
                const auto& gs0 = face_group_sets[static_cast<size_t>(inc0.face_index)];
                const auto& gs1 = face_group_sets[static_cast<size_t>(inc1.face_index)];
                group_differs = (gs0 != gs1);
            }

            // Check cusp angle
            bool angle_exceeds = false;
            const auto& n0 = face_normals[static_cast<size_t>(inc0.face_index)];
            const auto& n1 = face_normals[static_cast<size_t>(inc1.face_index)];
            const double dot = n0.x * n1.x + n0.y * n1.y + n0.z * n1.z;
            const double clamped = std::max(-1.0, std::min(1.0, dot));
            if (clamped < cusp_cos)
                angle_exceeds = true;

            // Invalid face normal → hard
            const bool n0_valid = (n0.x != 0.0 || n0.y != 0.0 || n0.z != 0.0);
            const bool n1_valid = (n1.x != 0.0 || n1.y != 0.0 || n1.z != 0.0);
            if (!n0_valid || !n1_valid)
                angle_exceeds = true;

            soft = !group_differs && !angle_exceeds;
        }

        if (soft) {
            // Union corners at each endpoint
            unite(inc0.corner_min, inc1.corner_min);
            unite(inc0.corner_max, inc1.corner_max);
        }
    }

    // --- Step 5: Generate render mesh with split vertices + normals ---

    // Map (point_index, island_root) → render vertex index
    struct RenderKey {
        int point_index;
        int island_root;
        bool operator==(const RenderKey& o) const {
            return point_index == o.point_index && island_root == o.island_root;
        }
    };
    struct RenderKeyHash {
        size_t operator()(const RenderKey& k) const {
            return std::hash<int64_t>()((static_cast<int64_t>(k.point_index) << 32) | static_cast<uint32_t>(k.island_root));
        }
    };

    std::unordered_map<RenderKey, int, RenderKeyHash> render_vertex_map;
    std::vector<PcgVertex> render_positions;
    std::vector<PcgVertex> render_normals;
    std::vector<PcgVec3> normal_accumulators;

    auto get_render_vertex = [&](int corner_id) -> int {
        const auto& ci = corners[static_cast<size_t>(corner_id)];
        RenderKey key{ci.point_index, find(corner_id)};
        auto it = render_vertex_map.find(key);
        if (it != render_vertex_map.end())
            return it->second;
        const int idx = static_cast<int>(render_positions.size());
        const auto& p = points[static_cast<size_t>(ci.point_index)];
        render_positions.push_back({p.x, p.y, p.z});
        render_normals.push_back({0.0, 0.0, 0.0});
        normal_accumulators.push_back({0.0, 0.0, 0.0});
        render_vertex_map[key] = idx;
        return idx;
    };

    // Generate triangles in fan order (same as triangulate_geometry_shared)
    // and record source corner for each triangle vertex
    struct TriCorner {
        int triangle_index;
        int source_corner;
    };
    std::vector<std::array<TriCorner, 3>> triangle_corners;

    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const auto& face = faces[fi];
        if (face.size() < 3)
            continue;
        const int base = face_corner_offset[fi];
        const int i0 = face[0];
        const int corner0 = base + 0;
        for (size_t i = 1; i + 1 < face.size(); ++i) {
            const int v1 = face[i];
            const int v2 = face[i + 1];
            const int c1 = base + static_cast<int>(i);
            const int c2 = base + static_cast<int>(i + 1);

            const int ri0 = get_render_vertex(corner0);
            const int ri1 = get_render_vertex(c1);
            const int ri2 = get_render_vertex(c2);
            mesh.add_triangle(ri0, ri1, ri2);

            // Accumulate area-weighted normal (unnormalized cross product)
            const auto& p0 = render_positions[static_cast<size_t>(ri0)];
            const auto& p1 = render_positions[static_cast<size_t>(ri1)];
            const auto& p2 = render_positions[static_cast<size_t>(ri2)];
            const double ux = p1.x - p0.x, uy = p1.y - p0.y, uz = p1.z - p0.z;
            const double vx = p2.x - p0.x, vy = p2.y - p0.y, vz = p2.z - p0.z;
            const double cx = uy * vz - uz * vy;
            const double cy = uz * vx - ux * vz;
            const double cz = ux * vy - uy * vx;
            const double area2 = std::sqrt(cx * cx + cy * cy + cz * cz);
            if (area2 > 1e-20) {
                auto& na0 = normal_accumulators[static_cast<size_t>(ri0)];
                na0.x += cx; na0.y += cy; na0.z += cz;
                auto& na1 = normal_accumulators[static_cast<size_t>(ri1)];
                na1.x += cx; na1.y += cy; na1.z += cz;
                auto& na2 = normal_accumulators[static_cast<size_t>(ri2)];
                na2.x += cx; na2.y += cy; na2.z += cz;
            }

            triangle_corners.push_back({{{static_cast<int>(triangle_corners.size()), corner0},
                                         {static_cast<int>(triangle_corners.size()), c1},
                                         {static_cast<int>(triangle_corners.size()), c2}}});
        }
    }

    // Normalize accumulators
    for (size_t i = 0; i < normal_accumulators.size(); ++i) {
        const auto& na = normal_accumulators[i];
        const double len = std::sqrt(na.x * na.x + na.y * na.y + na.z * na.z);
        if (len > 1e-12) {
            render_normals[i] = {na.x / len, na.y / len, na.z / len};
        } else {
            // Fallback: use the polygon normal of the first face this vertex belongs to
            bool found = false;
            for (const auto& tc : triangle_corners) {
                for (const auto& t : tc) {
                    const auto& ci = corners[static_cast<size_t>(t.source_corner)];
                    // Check if this render vertex corresponds to this corner
                    RenderKey key{ci.point_index, find(t.source_corner)};
                    auto it = render_vertex_map.find(key);
                    if (it != render_vertex_map.end() && it->second == static_cast<int>(i)) {
                        const auto& fn = face_normals[static_cast<size_t>(ci.face_index)];
                        if (fn.x != 0.0 || fn.y != 0.0 || fn.z != 0.0) {
                            render_normals[i] = {fn.x, fn.y, fn.z};
                            found = true;
                            break;
                        }
                    }
                }
                if (found) break;
            }
            // If still zero, leave as {0,0,0}
        }
    }

    // Set vertices and normals on the mesh
    for (const auto& v : render_positions)
        mesh.add_vertex({v.x, v.y, v.z});
    mesh.set_normals(std::move(render_normals));

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
