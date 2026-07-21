#include "data/pcg_geometry.hpp"

#include "geometry/bmesh.hpp"

#include <CDT.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <limits>
#include <unordered_map>
#include <unordered_set>

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

size_t stable_fan_start(const std::vector<PcgVec3>& points,
                        const std::vector<int>& face)
{
    if (face.size() <= 3)
        return 0;

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

std::vector<std::array<int, 3>> triangulate_face_corners(
    const std::vector<PcgVec3>& points, const std::vector<int>& face)
{
    if (face.size() < 3)
        return {};
    if (face.size() == 3)
        return {{{0, 1, 2}}};

    auto stable_fan = [&]() {
        std::vector<std::array<int, 3>> triangles;
        const size_t start = stable_fan_start(points, face);
        triangles.reserve(face.size() - 2);
        for (size_t offset = 1; offset + 1 < face.size(); ++offset) {
            triangles.push_back({
                static_cast<int>(start),
                static_cast<int>((start + offset) % face.size()),
                static_cast<int>((start + offset + 1) % face.size())});
        }
        return triangles;
    };

    PcgVec3 normal{};
    for (size_t i = 0; i < face.size(); ++i) {
        const int current_index = face[i];
        const int next_index = face[(i + 1) % face.size()];
        if (current_index < 0 || next_index < 0 ||
            current_index >= static_cast<int>(points.size()) ||
            next_index >= static_cast<int>(points.size())) {
            return {};
        }
        const PcgVec3& current = points[static_cast<size_t>(current_index)];
        const PcgVec3& next = points[static_cast<size_t>(next_index)];
        normal.x += (current.y - next.y) * (current.z + next.z);
        normal.y += (current.z - next.z) * (current.x + next.x);
        normal.z += (current.x - next.x) * (current.y + next.y);
    }
    const double normal_length_sq =
        normal.x * normal.x + normal.y * normal.y + normal.z * normal.z;
    if (normal_length_sq <= 1e-24)
        return stable_fan();

    int drop_axis = 2;
    const double abs_x = std::fabs(normal.x);
    const double abs_y = std::fabs(normal.y);
    const double abs_z = std::fabs(normal.z);
    if (abs_x >= abs_y && abs_x >= abs_z) drop_axis = 0;
    else if (abs_y >= abs_z) drop_axis = 1;

    std::vector<CDT::V2d<double>> vertices;
    vertices.reserve(face.size());
    for (int point_index : face) {
        const PcgVec3& point = points[static_cast<size_t>(point_index)];
        if (drop_axis == 0) vertices.push_back({point.y, point.z});
        else if (drop_axis == 1) vertices.push_back({point.x, point.z});
        else vertices.push_back({point.x, point.y});
    }

    // Preserve the existing stable fan for convex polygons. It is valid there
    // and avoids invoking CDT for the overwhelmingly common quad path.
    double turn_sign = 0.0;
    bool convex = true;
    for (size_t i = 0; i < vertices.size(); ++i) {
        const auto& a = vertices[(i + vertices.size() - 1) % vertices.size()];
        const auto& b = vertices[i];
        const auto& c = vertices[(i + 1) % vertices.size()];
        const double turn =
            (b.x - a.x) * (c.y - b.y) -
            (b.y - a.y) * (c.x - b.x);
        if (std::fabs(turn) <= 1e-15)
            continue;
        if (turn_sign == 0.0)
            turn_sign = turn;
        else if (turn * turn_sign < 0.0) {
            convex = false;
            break;
        }
    }
    if (convex)
        return stable_fan();

    std::vector<CDT::Edge> edges;
    edges.reserve(face.size());
    for (CDT::VertInd i = 0; i < vertices.size(); ++i)
        edges.emplace_back(i, (i + 1) % vertices.size());

    try {
        CDT::Triangulation<double> cdt(
            CDT::VertexInsertionOrder::AsProvided,
            CDT::IntersectingConstraintEdges::NotAllowed, 0.0);
        cdt.insertVertices(vertices);
        cdt.insertEdges(edges);
        cdt.eraseOuterTrianglesAndHoles();
        if (cdt.vertices.size() != vertices.size() || cdt.triangles.empty())
            return stable_fan();

        std::vector<std::array<int, 3>> result;
        result.reserve(cdt.triangles.size());
        for (const CDT::Triangle& triangle : cdt.triangles) {
            std::array<int, 3> corners{
                static_cast<int>(triangle.vertices[0]),
                static_cast<int>(triangle.vertices[1]),
                static_cast<int>(triangle.vertices[2])};
            const PcgVec3& a =
                points[static_cast<size_t>(face[static_cast<size_t>(corners[0])])];
            const PcgVec3& b =
                points[static_cast<size_t>(face[static_cast<size_t>(corners[1])])];
            const PcgVec3& c =
                points[static_cast<size_t>(face[static_cast<size_t>(corners[2])])];
            const double ux = b.x - a.x;
            const double uy = b.y - a.y;
            const double uz = b.z - a.z;
            const double vx = c.x - a.x;
            const double vy = c.y - a.y;
            const double vz = c.z - a.z;
            const double dot_normal =
                (uy * vz - uz * vy) * normal.x +
                (uz * vx - ux * vz) * normal.y +
                (ux * vy - uy * vx) * normal.z;
            if (dot_normal < 0.0)
                std::swap(corners[1], corners[2]);
            result.push_back(corners);
        }
        return result;
    } catch (const std::exception&) {
        return stable_fan();
    }
}

void PcgGeometry::set_corner_uvs(std::vector<PcgVec2> u)
{
    const int expected = corner_count();
    if (static_cast<int>(u.size()) != expected) {
        corner_uvs_.clear();
        has_corner_uvs_ = false;
        return;
    }
    corner_uvs_ = std::move(u);
    has_corner_uvs_ = true;
}

int PcgGeometry::corner_count() const
{
    int count = 0;
    for (const auto& face : faces_)
        count += static_cast<int>(face.size());
    return count;
}

void PcgGeometry::expand_point_uvs_to_corners()
{
    if (!has_uvs_)
        return;
    const auto& pts_uv = uvs_;
    std::vector<PcgVec2> corners;
    corners.reserve(static_cast<size_t>(corner_count()));
    for (const auto& face : faces_) {
        for (int point_index : face) {
            if (point_index >= 0 && static_cast<size_t>(point_index) < pts_uv.size())
                corners.push_back(pts_uv[static_cast<size_t>(point_index)]);
            else
                corners.push_back(PcgVec2{0.0, 0.0});
        }
    }
    set_corner_uvs(std::move(corners));
}

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
        const auto triangles = triangulate_face_corners(geometry.points(), face);
        for (const auto& triangle : triangles) {
            mesh.add_triangle(
                local[static_cast<size_t>(triangle[0])],
                local[static_cast<size_t>(triangle[1])],
                local[static_cast<size_t>(triangle[2])]);
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
        const auto triangles = triangulate_face_corners(geometry.points(), face);
        for (const auto& triangle : triangles) {
            mesh.add_triangle(
                face[static_cast<size_t>(triangle[0])],
                face[static_cast<size_t>(triangle[1])],
                face[static_cast<size_t>(triangle[2])]);
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
        const auto triangles = triangulate_face_corners(points, face);
        for (const auto& triangle : triangles) {
            const int corner0 = base + triangle[0];
            const int c1 = base + triangle[1];
            const int c2 = base + triangle[2];

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

    // An authored Houdini-style point N attribute is authoritative over
    // generated shading normals. Vertex N remains a future split-key concern;
    // point N is lossless with the current render-vertex island mapping.
    const auto* authored_normals =
        geometry.attributes().find(AttributeOwner::Point, "N");
    if (authored_normals && authored_normals->schema().type == AttributeType::Float &&
        authored_normals->schema().tuple_size >= 3 &&
        authored_normals->size() == points.size()) {
        const auto& values = authored_normals->float_values();
        const size_t width = static_cast<size_t>(authored_normals->schema().tuple_size);
        for (const auto& [key, index] : render_vertex_map) {
            const size_t source = static_cast<size_t>(key.point_index);
            const double x = values[source * width];
            const double y = values[source * width + 1];
            const double z = values[source * width + 2];
            const double magnitude = std::sqrt(x * x + y * y + z * z);
            if (magnitude > 1.0e-12)
                render_normals[static_cast<size_t>(index)] =
                    {x / magnitude, y / magnitude, z / magnitude};
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

    // Copy UVs to render vertices: corner (vertex) domain wins over point UV.
    if (geometry.has_corner_uvs() &&
        static_cast<int>(geometry.corner_uvs().size()) == total_corners) {
        const auto& geo_corner_uvs = geometry.corner_uvs();
        std::vector<PcgVec2> render_uvs(render_positions.size(), PcgVec2{0, 0});
        for (int corner_id = 0; corner_id < total_corners; ++corner_id) {
            const auto& ci = corners[static_cast<size_t>(corner_id)];
            RenderKey key{ci.point_index, find(corner_id)};
            auto it = render_vertex_map.find(key);
            if (it != render_vertex_map.end())
                render_uvs[static_cast<size_t>(it->second)] =
                    geo_corner_uvs[static_cast<size_t>(corner_id)];
        }
        mesh.set_uvs(std::move(render_uvs));
    } else if (geometry.has_uvs()) {
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

    // Preserve legacy render-mesh normals as Houdini-style point N. The
    // compatibility bridge welds coincident positions, so first writer wins,
    // matching the existing color/UV policy below.
    if (mesh.has_normals()) {
        const auto& mesh_verts = mesh.vertices();
        const auto& mesh_normals = mesh.normals();
        auto& normals = geo.attributes().create_float(
            AttributeOwner::Point, "N", 3, {0.0, 1.0, 0.0},
            AttributeTransformRole::Normal);
        normals.float_values_mut().assign(geo.points().size() * 3, 0.0);
        std::unordered_map<std::string, int> position_to_point;
        const double eps = opts.weld_eps;
        const auto quantize = [eps](double value) -> int64_t {
            return static_cast<int64_t>(std::llround(value / eps));
        };
        for (size_t i = 0; i < mesh_verts.size() && i < mesh_normals.size(); ++i) {
            const auto& vertex = mesh_verts[i];
            const std::string key = std::to_string(quantize(vertex.x)) + ',' +
                                    std::to_string(quantize(vertex.y)) + ',' +
                                    std::to_string(quantize(vertex.z));
            const auto [it, inserted] = position_to_point.emplace(
                key, static_cast<int>(position_to_point.size()));
            if (!inserted || it->second < 0 ||
                static_cast<size_t>(it->second) >= geo.points().size())
                continue;
            const auto& normal = mesh_normals[i];
            const size_t offset = static_cast<size_t>(it->second) * 3;
            normals.float_values_mut()[offset] = normal.x;
            normals.float_values_mut()[offset + 1] = normal.y;
            normals.float_values_mut()[offset + 2] = normal.z;
        }
    }

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

void propagate_geometry_data(const PcgGeometry& source,
                             PcgGeometry& destination,
                             const GeometryElementRemap& remap)
{
    const size_t point_count = destination.points().size();
    const size_t vertex_count = static_cast<size_t>(destination.corner_count());
    const size_t primitive_count = destination.faces().size();
    const auto normalized = [](const std::vector<int>& values, size_t count) {
        std::vector<int> result = values;
        result.resize(count, -1);
        return result;
    };

    const std::vector<int> point_sources = normalized(remap.points, point_count);
    const std::vector<int> vertex_sources = normalized(remap.vertices, vertex_count);
    const std::vector<int> primitive_sources = normalized(remap.primitives, primitive_count);
    AttributeRemap attribute_remap;
    attribute_remap[static_cast<size_t>(AttributeOwner::Point)] = point_sources;
    attribute_remap[static_cast<size_t>(AttributeOwner::Vertex)] = vertex_sources;
    attribute_remap[static_cast<size_t>(AttributeOwner::Primitive)] = primitive_sources;
    attribute_remap[static_cast<size_t>(AttributeOwner::Detail)] = {0};
    destination.attributes() = AttributeTable::remap_from(source.attributes(), attribute_remap);
    destination.detail() = source.detail();

    if (source.has_colors() && source.colors().size() == source.points().size()) {
        std::vector<PcgColor> colors(point_count, PcgColor{1.0, 1.0, 1.0, 1.0});
        for (size_t i = 0; i < point_count; ++i) {
            const int source_index = point_sources[i];
            if (source_index >= 0 && static_cast<size_t>(source_index) < source.colors().size())
                colors[i] = source.colors()[static_cast<size_t>(source_index)];
        }
        destination.set_colors(std::move(colors));
    }

    if (source.has_uvs() && source.uvs().size() == source.points().size()) {
        std::vector<PcgVec2> uvs(point_count, PcgVec2{0.0, 0.0});
        for (size_t i = 0; i < point_count; ++i) {
            const int source_index = point_sources[i];
            if (source_index >= 0 && static_cast<size_t>(source_index) < source.uvs().size())
                uvs[i] = source.uvs()[static_cast<size_t>(source_index)];
        }
        destination.set_uvs(std::move(uvs));
    }

    if (source.has_corner_uvs() &&
        source.corner_uvs().size() == static_cast<size_t>(source.corner_count())) {
        std::vector<PcgVec2> uvs(vertex_count, PcgVec2{0.0, 0.0});
        for (size_t i = 0; i < vertex_count; ++i) {
            const int source_index = vertex_sources[i];
            if (source_index >= 0 && static_cast<size_t>(source_index) < source.corner_uvs().size())
                uvs[i] = source.corner_uvs()[static_cast<size_t>(source_index)];
        }
        destination.set_corner_uvs(std::move(uvs));
    }

    if (source.has_face_materials()) {
        std::vector<std::string> materials(primitive_count,
            source.has_material() ? source.material_name() : std::string{});
        for (size_t i = 0; i < primitive_count; ++i) {
            const int source_index = primitive_sources[i];
            if (source_index >= 0 &&
                static_cast<size_t>(source_index) < source.face_materials().size())
                materials[i] = source.face_materials()[static_cast<size_t>(source_index)];
        }
        destination.set_face_materials(std::move(materials));
    } else if (source.has_material()) {
        destination.set_material_name(source.material_name());
    }

    for (const auto domain_and_sources :
         {std::pair{geometry::GroupDomain::Point, &point_sources},
          std::pair{geometry::GroupDomain::Face, &primitive_sources},
          std::pair{geometry::GroupDomain::Vertex, &vertex_sources}}) {
        const auto domain = domain_and_sources.first;
        const auto& sources = *domain_and_sources.second;
        for (const std::string& name : source.groups().group_names(domain)) {
            for (size_t destination_index = 0; destination_index < sources.size();
                 ++destination_index) {
                const int source_index = sources[destination_index];
                if (source_index >= 0 && source.groups().contains(domain, name, source_index))
                    destination.groups().add(domain, name,
                                             static_cast<geometry::GroupId>(destination_index));
            }
        }
    }

    for (const std::string& name :
         source.groups().group_names(geometry::GroupDomain::Edge)) {
        for (const auto& face : destination.faces()) {
            for (size_t corner = 0; corner < face.size(); ++corner) {
                const int destination_a = face[corner];
                const int destination_b = face[(corner + 1) % face.size()];
                if (destination_a < 0 || destination_b < 0 ||
                    static_cast<size_t>(destination_a) >= point_sources.size() ||
                    static_cast<size_t>(destination_b) >= point_sources.size())
                    continue;
                const int source_a = point_sources[static_cast<size_t>(destination_a)];
                const int source_b = point_sources[static_cast<size_t>(destination_b)];
                if (source_a < 0 || source_b < 0)
                    continue;
                if (source.groups().contains(geometry::GroupDomain::Edge, name,
                                             geometry::edge_group_id(source_a, source_b))) {
                    destination.groups().add(geometry::GroupDomain::Edge, name,
                                             geometry::edge_group_id(destination_a,
                                                                     destination_b));
                }
            }
        }
    }
}

PcgVec3 transform_vector(const GeometryAffineTransform& transform, const PcgVec3& value)
{
    const auto& m = transform.linear;
    return {m[0] * value.x + m[1] * value.y + m[2] * value.z,
            m[3] * value.x + m[4] * value.y + m[5] * value.z,
            m[6] * value.x + m[7] * value.y + m[8] * value.z};
}

PcgVec3 transform_position(const GeometryAffineTransform& transform, const PcgVec3& value)
{
    const auto result = transform_vector(transform, value);
    return {result.x + transform.translation.x,
            result.y + transform.translation.y,
            result.z + transform.translation.z};
}

PcgVec3 transform_normal(const GeometryAffineTransform& transform, const PcgVec3& value)
{
    const auto& m = transform.linear;
    const double c00 = m[4] * m[8] - m[5] * m[7];
    const double c01 = m[5] * m[6] - m[3] * m[8];
    const double c02 = m[3] * m[7] - m[4] * m[6];
    const double c10 = m[2] * m[7] - m[1] * m[8];
    const double c11 = m[0] * m[8] - m[2] * m[6];
    const double c12 = m[1] * m[6] - m[0] * m[7];
    const double c20 = m[1] * m[5] - m[2] * m[4];
    const double c21 = m[2] * m[3] - m[0] * m[5];
    const double c22 = m[0] * m[4] - m[1] * m[3];
    const double determinant = m[0] * c00 + m[1] * c01 + m[2] * c02;
    PcgVec3 result;
    if (std::abs(determinant) <= 1.0e-15) {
        result = transform_vector(transform, value);
    } else {
        const double inverse_det = 1.0 / determinant;
        result = {(c00 * value.x + c01 * value.y + c02 * value.z) * inverse_det,
                  (c10 * value.x + c11 * value.y + c12 * value.z) * inverse_det,
                  (c20 * value.x + c21 * value.y + c22 * value.z) * inverse_det};
    }
    const double magnitude = std::sqrt(result.x * result.x + result.y * result.y +
                                       result.z * result.z);
    if (magnitude <= 1.0e-15)
        return {};
    return {result.x / magnitude, result.y / magnitude, result.z / magnitude};
}

void transform_geometry_attributes(PcgGeometry& geometry,
                                   const GeometryAffineTransform& transform)
{
    for (AttributeOwner owner : {AttributeOwner::Point, AttributeOwner::Vertex,
                                 AttributeOwner::Primitive, AttributeOwner::Detail}) {
        for (const auto& name : geometry.attributes().names(owner)) {
            auto* attribute = geometry.attributes().find(owner, name);
            if (!attribute || attribute->schema().type != AttributeType::Float ||
                attribute->schema().tuple_size < 3)
                continue;
            const auto role = attribute->schema().transform_role;
            if (role != AttributeTransformRole::Position &&
                role != AttributeTransformRole::Vector &&
                role != AttributeTransformRole::Normal)
                continue;
            auto& values = attribute->float_values_mut();
            const size_t width = static_cast<size_t>(attribute->schema().tuple_size);
            for (size_t index = 0; index < attribute->size(); ++index) {
                const PcgVec3 value{values[index * width], values[index * width + 1],
                                    values[index * width + 2]};
                const PcgVec3 result = role == AttributeTransformRole::Position
                    ? transform_position(transform, value)
                    : role == AttributeTransformRole::Normal
                        ? transform_normal(transform, value)
                        : transform_vector(transform, value);
                values[index * width] = result.x;
                values[index * width + 1] = result.y;
                values[index * width + 2] = result.z;
            }
        }
    }
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
            geometry.groups().add(geometry::GroupDomain::Edge, name, entry.first);
    }
}

PcgGeometry merge_geometries(const PcgGeometry& a, const PcgGeometry& b, const std::string& b_prefix)
{
    PcgGeometry merged = a;
    const AttributeCounts a_counts = a.attribute_counts();
    const AttributeCounts b_counts = b.attribute_counts();
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
         {geometry::GroupDomain::Point, geometry::GroupDomain::Face,
          geometry::GroupDomain::Edge, geometry::GroupDomain::Vertex}) {
        for (const std::string& name : b.groups().group_names(domain)) {
            const std::string out_name = b_prefix + name;
            for (geometry::GroupId id : b.groups().members(domain, name)) {
                if (domain == geometry::GroupDomain::Point)
                    merged.groups().add(domain, out_name, id + point_offset);
                else if (domain == geometry::GroupDomain::Face)
                    merged.groups().add(domain, out_name, id + face_offset);
                else if (domain == geometry::GroupDomain::Vertex)
                    merged.groups().add(domain, out_name, id + a_counts[1]);
                else {
                    const auto endpoints = geometry::edge_group_points(id);
                    const int64_t remapped =
                        geometry::edge_key(endpoints[0] + point_offset,
                                           endpoints[1] + point_offset);
                    merged.groups().add(domain, out_name, remapped);
                }
            }
        }
    }

    merged.attributes().append_from(b.attributes(), a_counts, b_counts);

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

    // Merge corner (vertex) UVs — concatenate in face order
    if (a.has_corner_uvs() || b.has_corner_uvs()) {
        std::vector<PcgVec2> merged_corner_uvs;
        merged_corner_uvs.reserve(static_cast<size_t>(merged.corner_count()));
        if (a.has_corner_uvs()) {
            merged_corner_uvs.insert(merged_corner_uvs.end(),
                                     a.corner_uvs().begin(), a.corner_uvs().end());
        } else {
            merged_corner_uvs.resize(static_cast<size_t>(a.corner_count()), PcgVec2{0.0, 0.0});
        }
        if (b.has_corner_uvs()) {
            merged_corner_uvs.insert(merged_corner_uvs.end(),
                                     b.corner_uvs().begin(), b.corner_uvs().end());
        } else {
            merged_corner_uvs.resize(merged_corner_uvs.size() + static_cast<size_t>(b.corner_count()),
                                     PcgVec2{0.0, 0.0});
        }
        merged.set_corner_uvs(std::move(merged_corner_uvs));
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

namespace {

const AttributeArray* primitive_int_cluster_attribute(const PcgGeometry& geometry)
{
    const size_t face_count = geometry.faces().size();
    if (const AttributeArray* lotid =
            geometry.attributes().find(AttributeOwner::Primitive, "lotid")) {
        if (lotid->schema().type == AttributeType::Int &&
            lotid->schema().tuple_size == 1 &&
            lotid->size() == face_count)
            return lotid;
    }

    const AttributeArray* best = nullptr;
    size_t best_unique = 1;
    for (const std::string& name : geometry.attributes().names(AttributeOwner::Primitive)) {
        const AttributeArray* attribute =
            geometry.attributes().find(AttributeOwner::Primitive, name);
        if (!attribute || attribute->schema().type != AttributeType::Int ||
            attribute->schema().tuple_size != 1 ||
            attribute->size() != face_count)
            continue;
        std::unordered_set<int64_t> unique;
        for (int64_t value : attribute->int_values())
            unique.insert(value);
        if (unique.size() > best_unique) {
            best_unique = unique.size();
            best = attribute;
        }
    }
    return best_unique > 1 ? best : nullptr;
}

std::vector<int> face_connected_component_ids(const PcgGeometry& geometry)
{
    const int face_count = static_cast<int>(geometry.faces().size());
    std::vector<int> component(face_count, -1);
    std::unordered_map<int64_t, std::vector<int>> edge_faces;
    for (int fi = 0; fi < face_count; ++fi) {
        const auto& face = geometry.faces()[static_cast<size_t>(fi)];
        for (size_t corner = 0; corner < face.size(); ++corner) {
            const int a = face[corner];
            const int b = face[(corner + 1) % face.size()];
            if (a < 0 || b < 0)
                continue;
            edge_faces[geometry::edge_key(a, b)].push_back(fi);
        }
    }

    int next_component = 0;
    for (int seed = 0; seed < face_count; ++seed) {
        if (component[static_cast<size_t>(seed)] >= 0)
            continue;
        std::vector<int> stack = {seed};
        component[static_cast<size_t>(seed)] = next_component;
        while (!stack.empty()) {
            const int fi = stack.back();
            stack.pop_back();
            const auto& face = geometry.faces()[static_cast<size_t>(fi)];
            for (size_t corner = 0; corner < face.size(); ++corner) {
                const int a = face[corner];
                const int b = face[(corner + 1) % face.size()];
                if (a < 0 || b < 0)
                    continue;
                for (int neighbor : edge_faces[geometry::edge_key(a, b)]) {
                    if (neighbor == fi || component[static_cast<size_t>(neighbor)] >= 0)
                        continue;
                    component[static_cast<size_t>(neighbor)] = next_component;
                    stack.push_back(neighbor);
                }
            }
        }
        ++next_component;
    }
    return component;
}

} // namespace

PcgGeometry extract_faces(const PcgGeometry& geometry,
                          const std::unordered_set<int>& face_indices)
{
    PcgGeometry output;
    if (face_indices.empty())
        return output;

    std::unordered_map<int, int> point_map;
    GeometryElementRemap remap;
    std::vector<int> old_face_indices;
    old_face_indices.reserve(face_indices.size());
    for (int fi : face_indices) {
        if (fi < 0 || static_cast<size_t>(fi) >= geometry.faces().size())
            continue;
        old_face_indices.push_back(fi);
    }
    std::sort(old_face_indices.begin(), old_face_indices.end());
    old_face_indices.erase(std::unique(old_face_indices.begin(), old_face_indices.end()),
                           old_face_indices.end());
    if (old_face_indices.empty())
        return output;

    for (int fi : old_face_indices) {
        const auto& source_face = geometry.faces()[static_cast<size_t>(fi)];
        std::vector<int> face;
        face.reserve(source_face.size());
        for (int point : source_face) {
            auto it = point_map.find(point);
            int mapped = -1;
            if (it == point_map.end()) {
                mapped = static_cast<int>(output.points().size());
                point_map.emplace(point, mapped);
                output.points_mut().push_back(geometry.points()[static_cast<size_t>(point)]);
                remap.points.push_back(point);
            } else {
                mapped = it->second;
            }
            face.push_back(mapped);
            remap.vertices.push_back(-1);
        }
        remap.primitives.push_back(fi);
        output.faces_mut().push_back(std::move(face));
    }

    propagate_geometry_data(geometry, output, remap);
    maintain_unshared_edge_group(output);
    return output;
}

std::vector<PcgGeometry> partition_geometry_bevel_shells(const PcgGeometry& geometry)
{
    const int face_count = static_cast<int>(geometry.faces().size());
    if (face_count == 0)
        return {};

    std::vector<int> cluster_ids(face_count, 0);
    if (const AttributeArray* cluster_attr = primitive_int_cluster_attribute(geometry)) {
        const auto& values = cluster_attr->int_values();
        for (int fi = 0; fi < face_count; ++fi)
            cluster_ids[static_cast<size_t>(fi)] = static_cast<int>(values[static_cast<size_t>(fi)]);
    } else {
        cluster_ids = face_connected_component_ids(geometry);
    }

    std::unordered_map<int, std::unordered_set<int>> clusters;
    for (int fi = 0; fi < face_count; ++fi)
        clusters[cluster_ids[static_cast<size_t>(fi)]].insert(fi);

    if (clusters.size() <= 1)
        return {geometry};

    std::vector<PcgGeometry> shells;
    shells.reserve(clusters.size());
    for (const auto& entry : clusters)
        shells.push_back(extract_faces(geometry, entry.second));
    return shells;
}

} // namespace pcg::internal::data
