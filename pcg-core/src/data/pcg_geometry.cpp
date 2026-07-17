#include "data/pcg_geometry.hpp"

#include "geometry/bmesh.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace pcg::internal::data {

namespace {

void apply_face_materials(PcgMeshData& mesh,
                          const PcgGeometry& geometry,
                          const std::vector<int>& triangle_faces)
{
    if (triangle_faces.size() != mesh.triangles().size() / 3)
        return;

    std::vector<std::string> face_materials;
    if (geometry.has_face_materials()) {
        face_materials = geometry.face_materials();
    } else if (geometry.has_material()) {
        face_materials.assign(geometry.faces().size(), geometry.material_name());
    } else {
        return;
    }

    std::vector<std::string> slots;
    std::unordered_map<std::string, uint32_t> slot_by_name;
    std::vector<uint32_t> triangle_materials;
    triangle_materials.reserve(triangle_faces.size());
    for (int face_index : triangle_faces) {
        std::string name;
        if (face_index >= 0 && static_cast<size_t>(face_index) < face_materials.size())
            name = face_materials[static_cast<size_t>(face_index)];
        auto [it, inserted] = slot_by_name.emplace(name, static_cast<uint32_t>(slots.size()));
        if (inserted)
            slots.push_back(name);
        triangle_materials.push_back(it->second);
    }
    mesh.set_materials(std::move(slots), std::move(triangle_materials));
    if (mesh.material_slots().size() == 1)
        mesh.metadata().set("material", nlohmann::json(mesh.material_slots().front()));
    mesh.metadata().set("material_slots", nlohmann::json(mesh.material_slots()));
}

size_t stable_fan_start(const PcgGeometry& geometry, const std::vector<int>& face)
{
    if (face.size() <= 3)
        return 0;

    const auto& points = geometry.points();
    size_t best_start = 0;
    double best_min_cross_sq = -1.0;
    for (size_t start = 0; start < face.size(); ++start) {
        const auto& p0 = points[static_cast<size_t>(face[start])];
        double min_cross_sq = std::numeric_limits<double>::max();
        for (size_t offset = 1; offset + 1 < face.size(); ++offset) {
            const auto& p1 = points[static_cast<size_t>(
                face[(start + offset) % face.size()])];
            const auto& p2 = points[static_cast<size_t>(
                face[(start + offset + 1) % face.size()])];
            const double ux = p1.x - p0.x;
            const double uy = p1.y - p0.y;
            const double uz = p1.z - p0.z;
            const double vx = p2.x - p0.x;
            const double vy = p2.y - p0.y;
            const double vz = p2.z - p0.z;
            const double cx = uy * vz - uz * vy;
            const double cy = uz * vx - ux * vz;
            const double cz = ux * vy - uy * vx;
            min_cross_sq = std::min(min_cross_sq, cx * cx + cy * cy + cz * cz);
        }
        if (min_cross_sq > best_min_cross_sq) {
            best_min_cross_sq = min_cross_sq;
            best_start = start;
        }
    }
    return best_start;
}

} // namespace

void PcgGeometry::set_material_name(std::string m)
{
    material_name_ = m;
    has_material_ = true;
    face_materials_.assign(faces_.size(), std::move(m));
}

std::vector<std::string>& PcgGeometry::face_materials_mut()
{
    face_materials_.resize(faces_.size());
    return face_materials_;
}

void PcgGeometry::set_face_materials(std::vector<std::string> materials)
{
    if (materials.size() != faces_.size()) {
        face_materials_.clear();
        has_material_ = false;
        material_name_.clear();
        return;
    }
    face_materials_ = std::move(materials);
    has_material_ = !face_materials_.empty();
    material_name_.clear();
    if (!face_materials_.empty()) {
        const std::string& first = face_materials_.front();
        if (std::all_of(face_materials_.begin(), face_materials_.end(),
                        [&first](const std::string& value) { return value == first; }))
            material_name_ = first;
    }
}

PcgMeshData triangulate_geometry(const PcgGeometry& geometry)
{
    PcgMeshData mesh;
    std::vector<int> triangle_faces;
    // Duplicate vertices per face for flat shading — each face gets its own
    // vertices so that Unity's RecalculateNormals() produces correct per-face
    // normals instead of smoothing across hard edges between adjacent faces.
    for (size_t face_index = 0; face_index < geometry.faces().size(); ++face_index) {
        const auto& face = geometry.faces()[face_index];
        if (face.size() < 3)
            continue;
        std::vector<int> local;
        local.reserve(face.size());
        for (int idx : face) {
            const auto& p = geometry.points()[static_cast<size_t>(idx)];
            local.push_back(static_cast<int>(mesh.vertices().size()));
            mesh.add_vertex({p.x, p.y, p.z});
        }
        const size_t start = stable_fan_start(geometry, face);
        const int i0 = local[start];
        for (size_t offset = 1; offset + 1 < local.size(); ++offset) {
            mesh.add_triangle(i0,
                              local[(start + offset) % local.size()],
                              local[(start + offset + 1) % local.size()]);
            triangle_faces.push_back(static_cast<int>(face_index));
        }
    }

    apply_face_materials(mesh, geometry, triangle_faces);

    return mesh;
}

PcgMeshData triangulate_geometry_shared(const PcgGeometry& geometry)
{
    PcgMeshData mesh;
    std::vector<int> triangle_faces;
    for (const auto& p : geometry.points())
        mesh.add_vertex({p.x, p.y, p.z});
    for (size_t face_index = 0; face_index < geometry.faces().size(); ++face_index) {
        const auto& face = geometry.faces()[face_index];
        if (face.size() < 3)
            continue;
        const size_t start = stable_fan_start(geometry, face);
        const int i0 = face[start];
        for (size_t offset = 1; offset + 1 < face.size(); ++offset) {
            mesh.add_triangle(i0,
                              face[(start + offset) % face.size()],
                              face[(start + offset + 1) % face.size()]);
            triangle_faces.push_back(static_cast<int>(face_index));
        }
    }
    apply_face_materials(mesh, geometry, triangle_faces);
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
    std::vector<int> render_vertex_first_face;  // first face index per render vertex (for degenerate fallback)

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
        render_vertex_first_face.push_back(ci.face_index);
        render_vertex_map[key] = idx;
        return idx;
    };

    // Generate triangles using the same stable fan as triangulate_geometry_shared.
    // and record source corner for each triangle vertex
    struct TriCorner {
        int triangle_index;
        int source_corner;
    };
    std::vector<std::array<TriCorner, 3>> triangle_corners;
    std::vector<int> triangle_faces;

    for (size_t fi = 0; fi < faces.size(); ++fi) {
        const auto& face = faces[fi];
        if (face.size() < 3)
            continue;
        const int base = face_corner_offset[fi];
        const size_t start = stable_fan_start(geometry, face);
        const int corner0 = base + static_cast<int>(start);
        for (size_t offset = 1; offset + 1 < face.size(); ++offset) {
            const size_t i1 = (start + offset) % face.size();
            const size_t i2 = (start + offset + 1) % face.size();
            const int c1 = base + static_cast<int>(i1);
            const int c2 = base + static_cast<int>(i2);

            const int ri0 = get_render_vertex(corner0);
            const int ri1 = get_render_vertex(c1);
            const int ri2 = get_render_vertex(c2);
            mesh.add_triangle(ri0, ri1, ri2);
            triangle_faces.push_back(static_cast<int>(fi));

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
            const int face_idx = render_vertex_first_face[i];
            const auto& fn = face_normals[static_cast<size_t>(face_idx)];
            if (fn.x != 0.0 || fn.y != 0.0 || fn.z != 0.0) {
                render_normals[i] = {fn.x, fn.y, fn.z};
            }
            // If still zero, leave as {0,0,0}
        }
    }

    // Set vertices and normals on the mesh
    for (const auto& v : render_positions)
        mesh.add_vertex({v.x, v.y, v.z});
    mesh.set_normals(std::move(render_normals));

    // Copy per-point colors to render vertices
    if (geometry.has_colors()) {
        const auto& geo_colors = geometry.colors();
        std::vector<PcgColor> render_colors(render_positions.size(), PcgColor{1,1,1,1});

        // Build render vertex → source point index mapping
        for (const auto& [key, idx] : render_vertex_map) {
            if (key.point_index >= 0 && static_cast<size_t>(key.point_index) < geo_colors.size())
                render_colors[static_cast<size_t>(idx)] = geo_colors[static_cast<size_t>(key.point_index)];
        }
        mesh.set_colors(std::move(render_colors));
    }

    // Copy per-point UVs to render vertices
    if (geometry.has_uvs()) {
        const auto& geo_uvs = geometry.uvs();
        std::vector<PcgVec2> render_uvs(render_positions.size(), PcgVec2{0,0});
        for (const auto& [key, idx] : render_vertex_map) {
            if (key.point_index >= 0 && static_cast<size_t>(key.point_index) < geo_uvs.size())
                render_uvs[static_cast<size_t>(idx)] = geo_uvs[static_cast<size_t>(key.point_index)];
        }
        mesh.set_uvs(std::move(render_uvs));
    }

    apply_face_materials(mesh, geometry, triangle_faces);

    return mesh;
}

PcgGeometry geometry_from_mesh(const PcgMeshData& mesh)
{
    geometry::BMeshBuildOptions opts;
    opts.merge_coplanar_angle_deg = 0.0;
    const geometry::BMesh bmesh = geometry::bmesh_from_mesh(mesh, opts);
    PcgGeometry geo = geometry::geometry_from_bmesh(bmesh);

    // Map mesh vertex colors back to geometry points. bmesh_from_mesh welds
    // positions, so multiple mesh vertices may map to one geometry point;
    // first writer wins (consistent with weld_mesh's first-wins semantics).
    //
    // WARNING: The quantization and key format below MUST stay in sync with
    // bmesh.cpp::weld_mesh (same eps, same llround + "x,y,z" string key).
    // Changing either side without the other will silently break color mapping.
    if (mesh.has_colors()) {
        const auto& mesh_verts = mesh.vertices();
        const auto& mesh_colors = mesh.colors();
        std::vector<PcgColor> pt_colors(geo.points().size(), PcgColor{1,1,1,1});
        // Build position → geometry point index (reuse bmesh weld result)
        // bmesh.verts are welded positions; we need the reverse map.
        // weld_mesh inside bmesh_from_mesh does first-wins, so we replicate:
        std::unordered_map<std::string, int> pos_to_pt;
        const double eps = opts.weld_eps;
        const auto quantize = [eps](double v) -> int64_t {
            return static_cast<int64_t>(std::llround(v / eps));
        };
        for (size_t i = 0; i < mesh_verts.size() && i < mesh_colors.size(); ++i) {
            const auto& v = mesh_verts[i];
            const std::string key = std::to_string(quantize(v.x)) + ',' +
                                    std::to_string(quantize(v.y)) + ',' +
                                    std::to_string(quantize(v.z));
            auto it = pos_to_pt.find(key);
            if (it == pos_to_pt.end()) {
                // Find matching bmesh vert index
                // bmesh.verts should be in the same order as first-wins weld
                const int idx = static_cast<int>(pos_to_pt.size());
                pos_to_pt[key] = idx;
                if (static_cast<size_t>(idx) < pt_colors.size())
                    pt_colors[static_cast<size_t>(idx)] = mesh_colors[i];
            }
        }
        geo.set_colors(std::move(pt_colors));
    }

    // Map mesh UVs back to geometry points (same weld pattern as colors)
    if (mesh.has_uvs()) {
        const auto& mesh_verts = mesh.vertices();
        const auto& mesh_uvs = mesh.uvs();
        std::vector<PcgVec2> pt_uvs(geo.points().size(), PcgVec2{0,0});
        std::unordered_map<std::string, int> pos_to_pt;
        const double eps = opts.weld_eps;
        const auto quantize = [eps](double v) -> int64_t {
            return static_cast<int64_t>(std::llround(v / eps));
        };
        for (size_t i = 0; i < mesh_verts.size() && i < mesh_uvs.size(); ++i) {
            const auto& v = mesh_verts[i];
            const std::string key = std::to_string(quantize(v.x)) + ',' +
                                    std::to_string(quantize(v.y)) + ',' +
                                    std::to_string(quantize(v.z));
            auto it = pos_to_pt.find(key);
            if (it == pos_to_pt.end()) {
                const int idx = static_cast<int>(pos_to_pt.size());
                pos_to_pt[key] = idx;
                if (static_cast<size_t>(idx) < pt_uvs.size())
                    pt_uvs[static_cast<size_t>(idx)] = mesh_uvs[i];
            }
        }
        geo.set_uvs(std::move(pt_uvs));
    }

    if (mesh.has_materials()) {
        std::vector<std::string> face_materials;
        face_materials.reserve(mesh.triangle_materials().size());
        for (uint32_t slot : mesh.triangle_materials())
            face_materials.push_back(mesh.material_slots()[slot]);
        if (face_materials.size() == geo.faces().size())
            geo.set_face_materials(std::move(face_materials));
    } else if (mesh.metadata().has("material")) {
        const nlohmann::json& mat = mesh.metadata().get("material");
        if (mat.is_string())
            geo.set_material_name(mat.get<std::string>());
    }

    return geo;
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

    // Merge per-point colors
    if (a.has_colors() || b.has_colors()) {
        std::vector<PcgColor> merged_colors = a.has_colors() ? a.colors() : std::vector<PcgColor>();
        merged_colors.resize(merged.points().size(), PcgColor{1.0, 1.0, 1.0, 1.0});
        for (size_t i = 0; i < b.points().size(); ++i) {
            const size_t dst = static_cast<size_t>(point_offset) + i;
            if (b.has_colors() && i < b.colors().size())
                merged_colors[dst] = b.colors()[i];
        }
        merged.set_colors(std::move(merged_colors));
    }

    // Merge per-point UVs
    if (a.has_uvs() || b.has_uvs()) {
        std::vector<PcgVec2> merged_uvs = a.has_uvs() ? a.uvs() : std::vector<PcgVec2>();
        merged_uvs.resize(merged.points().size(), PcgVec2{0.0, 0.0});
        for (size_t i = 0; i < b.points().size(); ++i) {
            const size_t dst = static_cast<size_t>(point_offset) + i;
            if (b.has_uvs() && i < b.uvs().size())
                merged_uvs[dst] = b.uvs()[i];
        }
        merged.set_uvs(std::move(merged_uvs));
    }

    if (a.has_material() || b.has_material() || a.has_face_materials() || b.has_face_materials()) {
        std::vector<std::string> merged_materials;
        merged_materials.reserve(merged.faces().size());
        if (a.has_face_materials())
            merged_materials.insert(merged_materials.end(), a.face_materials().begin(), a.face_materials().end());
        else
            merged_materials.insert(merged_materials.end(), a.faces().size(), a.has_material() ? a.material_name() : "");
        if (b.has_face_materials())
            merged_materials.insert(merged_materials.end(), b.face_materials().begin(), b.face_materials().end());
        else
            merged_materials.insert(merged_materials.end(), b.faces().size(), b.has_material() ? b.material_name() : "");
        merged.set_face_materials(std::move(merged_materials));
    }

    return merged;
}

} // namespace pcg::internal::data
