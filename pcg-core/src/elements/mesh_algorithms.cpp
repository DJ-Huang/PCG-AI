#include "elements/mesh_algorithms.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pcg::internal::elements {
namespace {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

Vec3 to_vec3(const data::PcgVertex& v)
{
    return {v.x, v.y, v.z};
}

data::PcgVertex to_vertex(const Vec3& v)
{
    return {v.x, v.y, v.z};
}

Vec3 add(const Vec3& a, const Vec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 sub(const Vec3& a, const Vec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

Vec3 scale(const Vec3& v, double s)
{
    return {v.x * s, v.y * s, v.z * s};
}

double dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

double length(const Vec3& v)
{
    return std::sqrt(dot(v, v));
}

Vec3 normalize(const Vec3& v)
{
    const double len = length(v);
    if (len <= 1e-12)
        return {0.0, 0.0, 0.0};
    return scale(v, 1.0 / len);
}

int edge_key(int a, int b)
{
    return a < b ? a * 65536 + b : b * 65536 + a;
}

Vec3 triangle_normal(const Vec3& a, const Vec3& b, const Vec3& c)
{
    return normalize(cross(sub(b, a), sub(c, a)));
}

void add_quad(data::PcgMeshData& mesh, const data::PcgVertex& v0, const data::PcgVertex& v1,
              const data::PcgVertex& v2, const data::PcgVertex& v3)
{
    const int i0 = static_cast<int>(mesh.vertices().size());
    mesh.add_vertex(v0);
    mesh.add_vertex(v1);
    mesh.add_vertex(v2);
    mesh.add_vertex(v3);
    // Unity front faces: swap 2nd/3rd index so cross(b-a,c-a) points outward in LH coords.
    mesh.add_triangle(i0, i0 + 2, i0 + 1);
    mesh.add_triangle(i0, i0 + 3, i0 + 2);
}

std::string position_key(const Vec3& p, double eps = 1e-6)
{
    const auto quantize = [eps](double v) -> int64_t {
        return static_cast<int64_t>(std::llround(v / eps));
    };
    return std::to_string(quantize(p.x)) + ',' + std::to_string(quantize(p.y)) + ',' +
           std::to_string(quantize(p.z));
}

void accumulate_vertex_normals(const data::PcgMeshData& mesh, std::vector<Vec3>& accum)
{
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    accum.assign(verts.size(), {0.0, 0.0, 0.0});

    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const int ia = tris[i];
        const int ib = tris[i + 1];
        const int ic = tris[i + 2];
        if (ia < 0 || ib < 0 || ic < 0 || static_cast<size_t>(ia) >= verts.size() ||
            static_cast<size_t>(ib) >= verts.size() || static_cast<size_t>(ic) >= verts.size())
            continue;

        const Vec3 n = triangle_normal(to_vec3(verts[static_cast<size_t>(ia)]),
                                         to_vec3(verts[static_cast<size_t>(ib)]),
                                         to_vec3(verts[static_cast<size_t>(ic)]));
        accum[static_cast<size_t>(ia)] = add(accum[static_cast<size_t>(ia)], n);
        accum[static_cast<size_t>(ib)] = add(accum[static_cast<size_t>(ib)], n);
        accum[static_cast<size_t>(ic)] = add(accum[static_cast<size_t>(ic)], n);
    }
}

struct EdgeRecord {
    int v0 = 0;
    int v1 = 0;
    int tri0 = -1;
    int tri1 = -1;
};

struct EdgeBevelProfile {
    int tri0 = -1;
    int tri1 = -1;
    int v0 = 0;
    int v1 = 0;
    double offset = 0.0;
    Vec3 n0{};
    Vec3 n1{};
    std::vector<Vec3> side0;
    std::vector<Vec3> side1;
};

struct VertexCornerEntry {
    Vec3 meet;
    int in_edge_key;
    int out_edge_key;
    Vec3 profile_in;
    Vec3 profile_out;
    Vec3 face_normal;
};

struct MeshBuilder {
    data::PcgMeshData mesh;
    std::unordered_map<std::string, int> vertex_cache;

    int get_vertex(const Vec3& v)
    {
        const std::string key = position_key(v);
        const auto it = vertex_cache.find(key);
        if (it != vertex_cache.end())
            return it->second;

        const int index = static_cast<int>(mesh.vertices().size());
        mesh.add_vertex(to_vertex(v));
        vertex_cache[key] = index;
        return index;
    }

    void add_oriented_triangle(const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& desired_normal)
    {
        const Vec3 n = cross(sub(b, a), sub(c, a));
        const int ia = get_vertex(a);
        const int ib = get_vertex(b);
        const int ic = get_vertex(c);
        if (dot(n, desired_normal) < 0.0)
            mesh.add_triangle(ia, ic, ib);
        else
            mesh.add_triangle(ia, ib, ic);
    }

    void add_oriented_quad(const Vec3& v0, const Vec3& v1, const Vec3& v2, const Vec3& v3,
                           const Vec3& desired_normal)
    {
        const Vec3 n = cross(sub(v1, v0), sub(v2, v0));
        const int i0 = get_vertex(v0);
        const int i1 = get_vertex(v1);
        const int i2 = get_vertex(v2);
        const int i3 = get_vertex(v3);
        if (dot(n, desired_normal) < 0.0) {
            mesh.add_triangle(i0, i2, i1);
            mesh.add_triangle(i0, i3, i2);
        } else {
            mesh.add_triangle(i0, i1, i2);
            mesh.add_triangle(i0, i2, i3);
        }
    }
};

struct WeldedMesh {
    std::vector<Vec3> positions;
    std::vector<std::array<int, 3>> triangles;
};

WeldedMesh weld_mesh(const data::PcgMeshData& mesh, double eps = 1e-6)
{
    WeldedMesh welded;
    std::unordered_map<std::string, int> index_by_key;
    std::vector<int> remap(mesh.vertices().size(), -1);

    for (size_t i = 0; i < mesh.vertices().size(); ++i) {
        const Vec3 p = to_vec3(mesh.vertices()[i]);
        const std::string key = position_key(p, eps);
        const auto it = index_by_key.find(key);
        if (it == index_by_key.end()) {
            const int index = static_cast<int>(welded.positions.size());
            welded.positions.push_back(p);
            index_by_key[key] = index;
            remap[i] = index;
        } else {
            remap[i] = it->second;
        }
    }

    const auto& tris = mesh.triangles();
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        welded.triangles.push_back({remap[static_cast<size_t>(tris[i])],
                                    remap[static_cast<size_t>(tris[i + 1])],
                                    remap[static_cast<size_t>(tris[i + 2])]});
    }

    return welded;
}

Vec3 tri_normal(const WeldedMesh& mesh, int tri_index)
{
    const auto& tri = mesh.triangles[static_cast<size_t>(tri_index)];
    return triangle_normal(mesh.positions[static_cast<size_t>(tri[0])],
                           mesh.positions[static_cast<size_t>(tri[1])],
                           mesh.positions[static_cast<size_t>(tri[2])]);
}

std::unordered_map<int, EdgeRecord> build_edge_records(const WeldedMesh& mesh)
{
    std::unordered_map<int, EdgeRecord> edges;
    for (size_t tri_index = 0; tri_index < mesh.triangles.size(); ++tri_index) {
        const auto& tri = mesh.triangles[tri_index];
        for (int e = 0; e < 3; ++e) {
            const int a = tri[static_cast<size_t>(e)];
            const int b = tri[static_cast<size_t>((e + 1) % 3)];
            const int key = edge_key(a, b);
            auto& record = edges[key];
            if (record.tri0 < 0) {
                record.v0 = std::min(a, b);
                record.v1 = std::max(a, b);
                record.tri0 = static_cast<int>(tri_index);
            } else {
                record.tri1 = static_cast<int>(tri_index);
            }
        }
    }
    return edges;
}

bool is_hard_edge(const EdgeRecord& edge, const WeldedMesh& mesh, double cos_limit)
{
    if (edge.tri0 < 0 || edge.tri1 < 0)
        return true;

    const Vec3 n0 = tri_normal(mesh, edge.tri0);
    const Vec3 n1 = tri_normal(mesh, edge.tri1);
    return dot(n0, n1) < cos_limit;
}

double face_plane_angle(const Vec3& n0, const Vec3& n1)
{
    return std::acos(std::clamp(-dot(n0, n1), -1.0, 1.0));
}

double width_to_offset(double width, const Vec3& n0, const Vec3& n1)
{
    const double denom = 2.0 * std::sin(face_plane_angle(n0, n1) * 0.5);
    if (denom <= 1e-9)
        return width;
    return width / denom;
}

Vec3 face_centroid(const WeldedMesh& mesh, const std::vector<int>& tri_indices)
{
    Vec3 sum = {0.0, 0.0, 0.0};
    int count = 0;
    for (const int tri_index : tri_indices) {
        const auto& tri = mesh.triangles[static_cast<size_t>(tri_index)];
        for (int idx : tri) {
            sum = add(sum, mesh.positions[static_cast<size_t>(idx)]);
            ++count;
        }
    }
    if (count <= 0)
        return {0.0, 0.0, 0.0};
    return scale(sum, 1.0 / static_cast<double>(count));
}

Vec3 inward_edge_dir(const Vec3& face_normal, const Vec3& edge_start, const Vec3& edge_end,
                     const Vec3& face_centroid)
{
    Vec3 dir = cross(face_normal, normalize(sub(edge_end, edge_start)));
    const Vec3 mid = scale(add(edge_start, edge_end), 0.5);
    if (dot(dir, sub(face_centroid, mid)) < 0.0)
        dir = scale(dir, -1.0);
    return normalize(dir);
}

Vec3 offset_on_edge(const WeldedMesh& mesh, int edge_a, int edge_b, int vertex,
                    const Vec3& face_normal, const Vec3& centroid, double width)
{
    const Vec3 p0 = mesh.positions[static_cast<size_t>(edge_a)];
    const Vec3 p1 = mesh.positions[static_cast<size_t>(edge_b)];
    const Vec3 p = mesh.positions[static_cast<size_t>(vertex)];
    const Vec3 dir = inward_edge_dir(face_normal, p0, p1, centroid);
    return add(p, scale(dir, width));
}

bool nearly_collinear(const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 ab = normalize(sub(b, a));
    const Vec3 bc = normalize(sub(c, b));
    return length(cross(ab, bc)) <= 1e-6;
}

// Blender-inspired geometry_collide_offset: for a beveled edge B with adjacent
// beveled edges A (at v0 end) and C (at v1 end), compute the max offset before
// offset lines from A, B, C collide.
//   limit = len_B / ((1+cos(th_A))/sin(th_A) + (1+cos(th_C))/sin(th_C))
// where th_A = angle between A and B at v0, th_C = angle between B and C at v1.
double geometry_collide_offset_for_edge(
    const WeldedMesh& mesh, const EdgeRecord& edge,
    const std::unordered_set<int>& hard_edges,
    const std::unordered_map<int, EdgeRecord>& edges)
{
    const Vec3 p_v0 = mesh.positions[static_cast<size_t>(edge.v0)];
    const Vec3 p_v1 = mesh.positions[static_cast<size_t>(edge.v1)];
    const double len_b = length(sub(p_v1, p_v0));
    if (len_b <= 1e-9)
        return 0.0;

    // Find adjacent hard edges at v0 (edges sharing v0 that are hard, excluding edge B)
    // and at v1 (edges sharing v1 that are hard, excluding edge B).
    // For each endpoint, we need the angle between edge B and the adjacent hard edge
    // on the same face.
    auto find_adjacent_angle = [&](int vertex, int other_vertex) -> double {
        // Find all hard edges at this vertex (excluding the current edge)
        std::vector<int> adjacent_edges;
        for (const auto& [key, rec] : edges) {
            if (key == edge_key(edge.v0, edge.v1))
                continue;
            if (!hard_edges.count(key))
                continue;
            if (rec.v0 == vertex || rec.v1 == vertex)
                adjacent_edges.push_back(key);
        }
        if (adjacent_edges.empty())
            return M_PI / 2.0; // No adjacent hard edge → 90° (no constraint)

        // Find the adjacent edge that shares a face with edge B at this vertex
        // (i.e., shares a triangle with edge B)
        const auto& b_rec = edge;
        for (int adj_key : adjacent_edges) {
            const auto& adj_rec = edges.at(adj_key);
            // Check if adj_rec shares a triangle with b_rec
            bool shares_tri = false;
            for (int tri : {adj_rec.tri0, adj_rec.tri1}) {
                if (tri == b_rec.tri0 || tri == b_rec.tri1) {
                    shares_tri = true;
                    break;
                }
            }
            if (!shares_tri)
                continue;

            // Compute the angle between edge B and the adjacent edge at this vertex
            const int adj_other = (adj_rec.v0 == vertex) ? adj_rec.v1 : adj_rec.v0;
            const Vec3 p_vertex = mesh.positions[static_cast<size_t>(vertex)];
            const Vec3 p_b_other = mesh.positions[static_cast<size_t>(other_vertex)];
            const Vec3 p_adj_other = mesh.positions[static_cast<size_t>(adj_other)];

            const Vec3 dir_b = normalize(sub(p_b_other, p_vertex));
            const Vec3 dir_adj = normalize(sub(p_adj_other, p_vertex));
            double cos_angle = std::clamp(dot(dir_b, dir_adj), -1.0, 1.0);
            double angle = std::acos(cos_angle);
            return angle;
        }
        return M_PI / 2.0; // No adjacent hard edge on same face → no constraint
    };

    const double th_a = find_adjacent_angle(edge.v0, edge.v1);
    const double th_c = find_adjacent_angle(edge.v1, edge.v0);

    const double sin_a = std::sin(th_a);
    const double sin_c = std::sin(th_c);
    const double cos_a = std::cos(th_a);
    const double cos_c = std::cos(th_c);

    double offsets_projected = 0.0;
    if (std::abs(sin_a) > 1e-9)
        offsets_projected += (1.0 + cos_a) / sin_a;
    if (std::abs(sin_c) > 1e-9)
        offsets_projected += (1.0 + cos_c) / sin_c;

    if (offsets_projected <= 1e-9)
        return len_b * 0.5; // Fallback

    return len_b / offsets_projected;
}

void triangulate_convex_polygon(MeshBuilder& builder, const std::vector<Vec3>& polygon,
                                const Vec3& desired_normal)
{
    if (polygon.size() < 3)
        return;
    for (size_t i = 1; i + 1 < polygon.size(); ++i)
        builder.add_oriented_triangle(polygon[0], polygon[i], polygon[i + 1], desired_normal);
}

std::string normal_key(const Vec3& n, double eps = 1e-4)
{
    const auto quantize = [eps](double v) -> int64_t {
        return static_cast<int64_t>(std::llround(v / eps));
    };
    return std::to_string(quantize(n.x)) + ',' + std::to_string(quantize(n.y)) + ',' +
           std::to_string(quantize(n.z));
}

std::vector<int> boundary_loop(const WeldedMesh& mesh, const std::vector<int>& tri_indices)
{
    std::unordered_map<int, int> edge_use;
    for (const int tri_index : tri_indices) {
        const auto& tri = mesh.triangles[static_cast<size_t>(tri_index)];
        for (int e = 0; e < 3; ++e) {
            const int a = tri[static_cast<size_t>(e)];
            const int b = tri[static_cast<size_t>((e + 1) % 3)];
            ++edge_use[edge_key(a, b)];
        }
    }

    std::unordered_map<int, std::vector<int>> adjacency;
    for (const int tri_index : tri_indices) {
        const auto& tri = mesh.triangles[static_cast<size_t>(tri_index)];
        for (int e = 0; e < 3; ++e) {
            const int a = tri[static_cast<size_t>(e)];
            const int b = tri[static_cast<size_t>((e + 1) % 3)];
            if (edge_use[edge_key(a, b)] == 1) {
                adjacency[a].push_back(b);
                adjacency[b].push_back(a);
            }
        }
    }

    if (adjacency.empty())
        return {};

    int start = adjacency.begin()->first;
    std::vector<int> loop = {start};
    int prev = -1;
    int current = start;
    while (true) {
        const auto& neighbors = adjacency[current];
        int next = -1;
        for (const int candidate : neighbors) {
            if (candidate != prev) {
                next = candidate;
                break;
            }
        }
        if (next < 0 || next == start)
            break;
        loop.push_back(next);
        prev = current;
        current = next;
        if (loop.size() > mesh.positions.size() + 1)
            break;
    }
    return loop;
}

void ensure_ccw_loop(const WeldedMesh& mesh, std::vector<int>& loop, const Vec3& face_normal)
{
    if (loop.size() < 3)
        return;

    double signed_area = 0.0;
    for (size_t i = 0; i < loop.size(); ++i) {
        const Vec3 a = mesh.positions[static_cast<size_t>(loop[i])];
        const Vec3 b = mesh.positions[static_cast<size_t>(loop[(i + 1) % loop.size()])];
        signed_area += dot(face_normal, cross(a, b));
    }
    if (signed_area < 0.0)
        std::reverse(loop.begin(), loop.end());
}

Vec3 profile_point_on_face(const EdgeBevelProfile& profile, const std::vector<int>& face_tris,
                           int vertex)
{
    const bool on_tri0 =
        std::find(face_tris.begin(), face_tris.end(), profile.tri0) != face_tris.end();
    const bool at_v0 = (vertex == profile.v0);
    // side0 is at v0 end, side1 is at v1 end.
    // Index 0 is face 0 (tri0) side, index last is face 1 (tri1) side.
    const auto& rail = at_v0 ? profile.side0 : profile.side1;
    const size_t idx = on_tri0 ? 0 : rail.size() - 1;
    return rail[idx];
}

Vec3 meet_offset_corner(const WeldedMesh& mesh, int prev, int corner, int next,
                        const Vec3& face_normal, const Vec3& centroid, double width_in,
                        double width_out)
{
    const Vec3 p_prev = mesh.positions[static_cast<size_t>(prev)];
    const Vec3 p_corner = mesh.positions[static_cast<size_t>(corner)];
    const Vec3 p_next = mesh.positions[static_cast<size_t>(next)];
    const Vec3 d_in = inward_edge_dir(face_normal, p_prev, p_corner, centroid);
    const Vec3 d_out = inward_edge_dir(face_normal, p_corner, p_next, centroid);

    if (nearly_collinear(p_prev, p_corner, p_next))
        return add(p_corner, scale(d_out, width_out));

    return add(add(p_corner, scale(d_in, width_in)), scale(d_out, width_out));
}

EdgeBevelProfile build_edge_profile(const WeldedMesh& mesh, const EdgeRecord& edge,
                                    const std::unordered_map<std::string, Vec3>& face_centroids,
                                    const std::unordered_map<std::string, std::vector<int>>& face_groups,
                                    double amount, BevelOffsetType offset_type, int segments,
                                    bool clamp_overlap,
                                    const std::unordered_set<int>& hard_edges_set,
                                    const std::unordered_map<int, EdgeRecord>& edges_map)
{
    EdgeBevelProfile profile;
    profile.tri0 = edge.tri0;
    profile.tri1 = edge.tri1;
    profile.v0 = edge.v0;
    profile.v1 = edge.v1;

    const Vec3 n0 = tri_normal(mesh, edge.tri0);
    const Vec3 n1 = tri_normal(mesh, edge.tri1);
    const std::string key0 = normal_key(n0);
    const std::string key1 = normal_key(n1);
    const Vec3 c0 = face_centroids.at(key0);
    const Vec3 c1 = face_centroids.at(key1);

    double offset = offset_type == BevelOffsetType::Width ? width_to_offset(amount, n0, n1) : amount;
    if (clamp_overlap) {
        // Blender-style geometry_collide_offset: compute max offset based on
        // adjacent hard edge angles and edge length.
        const double collide_limit = geometry_collide_offset_for_edge(mesh, edge, hard_edges_set, edges_map);
        offset = std::min(offset, collide_limit);
    }
    profile.offset = offset;
    profile.n0 = n0;
    profile.n1 = n1;

    // Compute meet points using the face boundary loop (not triangle diagonal).
    // This ensures strip endpoints == face polygon vertices → shared edges → closed mesh.
    auto meet_at = [&](int vertex, const Vec3& face_normal,
                       const Vec3& centroid, const std::string& face_key) -> Vec3 {
        const int other = (vertex == edge.v0) ? edge.v1 : edge.v0;
        const Vec3 p_v = mesh.positions[static_cast<size_t>(vertex)];
        const Vec3 d_edge = inward_edge_dir(face_normal, p_v,
            mesh.positions[static_cast<size_t>(other)], centroid);

        // Find adjacent boundary vertex from face loop (not triangle diagonal).
        const auto loop_it = face_groups.find(face_key);
        if (loop_it == face_groups.end() || loop_it->second.size() < 3)
            return add(p_v, scale(d_edge, offset));

        const auto& loop = loop_it->second;
        int adj_vertex = -1;
        for (size_t i = 0; i < loop.size(); ++i) {
            if (loop[i] != vertex)
                continue;
            const int prev_v = loop[(i + loop.size() - 1) % loop.size()];
            const int next_v = loop[(i + 1) % loop.size()];
            // Pick the adjacent vertex that is NOT the other endpoint of this edge.
            adj_vertex = (prev_v != other) ? prev_v : next_v;
            break;
        }
        if (adj_vertex < 0)
            return add(p_v, scale(d_edge, offset));

        const Vec3 p_adj = mesh.positions[static_cast<size_t>(adj_vertex)];
        const Vec3 d_adj = inward_edge_dir(face_normal, p_v, p_adj, centroid);
        return add(add(p_v, scale(d_edge, offset)), scale(d_adj, offset));
    };

    const Vec3 a0 = meet_at(edge.v0, n0, c0, key0);
    const Vec3 b0 = meet_at(edge.v1, n0, c0, key0);
    const Vec3 a1 = meet_at(edge.v0, n1, c1, key1);
    const Vec3 b1 = meet_at(edge.v1, n1, c1, key1);

    // Chamfer normal = bisector of face normals (points outward along bevel surface).
    const Vec3 chamfer_dir = normalize(add(n0, n1));

    // Profile: superellipse-like arc, not flat line.
    // Linear interpolation would make rail points collinear with meet points,
    // causing corner mesh side fans to degenerate (zero-area triangles).
    // The sin(pi*t) bulge pushes rail points outward along the chamfer normal.
    profile.side0.resize(static_cast<size_t>(segments + 1));
    profile.side1.resize(static_cast<size_t>(segments + 1));
    for (int s = 0; s <= segments; ++s) {
        const double t = static_cast<double>(s) / static_cast<double>(segments);
        const double bulge = (segments > 1) ? std::sin(M_PI * t) * offset * 0.5 : 0.0;
        profile.side0[static_cast<size_t>(s)] =
            add(add(scale(a0, 1.0 - t), scale(a1, t)), scale(chamfer_dir, bulge));
        profile.side1[static_cast<size_t>(s)] =
            add(add(scale(b0, 1.0 - t), scale(b1, t)), scale(chamfer_dir, bulge));
    }

    return profile;
}

std::vector<Vec3> build_offset_face_polygon(
    const WeldedMesh& mesh, const std::vector<int>& loop, const Vec3& face_normal,
    const Vec3& centroid, const std::unordered_set<int>& hard_edges,
    const std::unordered_map<int, EdgeBevelProfile>& profiles, const std::vector<int>& face_tris,
    std::unordered_map<int, std::vector<VertexCornerEntry>>& vertex_corners)
{
    if (loop.size() < 3)
        return {};

    std::vector<Vec3> polygon;
    const size_t count = loop.size();
    auto push_distinct = [&](const Vec3& p) {
        if (polygon.empty() || length(sub(p, polygon.back())) > 1e-9)
            polygon.push_back(p);
    };

    for (size_t i = 0; i < count; ++i) {
        const int prev = loop[(i + count - 1) % count];
        const int curr = loop[i];
        const int next = loop[(i + 1) % count];
        const int edge_in = edge_key(prev, curr);
        const int edge_out = edge_key(curr, next);
        const bool hard_in = hard_edges.count(edge_in) > 0;
        const bool hard_out = hard_edges.count(edge_out) > 0;

        const auto get_offset = [&](int edge_key_value) -> double {
            const auto edge_it = profiles.find(edge_key_value);
            return edge_it != profiles.end() ? edge_it->second.offset : 0.0;
        };

        if (hard_in && hard_out &&
            !nearly_collinear(mesh.positions[static_cast<size_t>(prev)],
                              mesh.positions[static_cast<size_t>(curr)],
                              mesh.positions[static_cast<size_t>(next)])) {
            const auto in_it = profiles.find(edge_in);
            const auto out_it = profiles.find(edge_out);
            if (in_it != profiles.end() && out_it != profiles.end()) {
                const Vec3 meet_pt = meet_offset_corner(
                    mesh, prev, curr, next, face_normal, centroid, in_it->second.offset,
                    out_it->second.offset);
                push_distinct(meet_pt);
                vertex_corners[curr].push_back({
                    meet_pt,
                    edge_in,
                    edge_out,
                    profile_point_on_face(in_it->second, face_tris, curr),
                    profile_point_on_face(out_it->second, face_tris, curr),
                    face_normal,
                });
            }
        } else if (hard_out) {
            const auto edge_it = profiles.find(edge_out);
            if (edge_it != profiles.end())
                push_distinct(profile_point_on_face(edge_it->second, face_tris, curr));
            else
                push_distinct(offset_on_edge(mesh, curr, next, curr, face_normal, centroid,
                                             get_offset(edge_out)));
        } else if (hard_in) {
            const auto edge_it = profiles.find(edge_in);
            if (edge_it != profiles.end())
                push_distinct(profile_point_on_face(edge_it->second, face_tris, curr));
            else
                push_distinct(offset_on_edge(mesh, prev, curr, curr, face_normal, centroid,
                                             get_offset(edge_in)));
        } else {
            push_distinct(mesh.positions[static_cast<size_t>(curr)]);
        }
    }
    return polygon;
}

void add_bevel_strip(MeshBuilder& builder, const EdgeBevelProfile& profile)
{
    if (profile.side0.size() < 2 || profile.side1.size() < 2)
        return;

    Vec3 chamfer_normal = normalize(add(profile.n0, profile.n1));
    if (length(chamfer_normal) <= 1e-12)
        chamfer_normal = cross(sub(profile.side1[0], profile.side0[0]),
                               sub(profile.side0.back(), profile.side0[0]));

    const int segment_count = static_cast<int>(profile.side0.size()) - 1;
    for (int s = 0; s < segment_count; ++s) {
        const Vec3& a = profile.side0[static_cast<size_t>(s)];
        const Vec3& b = profile.side1[static_cast<size_t>(s)];
        const Vec3& c = profile.side1[static_cast<size_t>(s + 1)];
        const Vec3& d = profile.side0[static_cast<size_t>(s + 1)];
        builder.add_oriented_quad(a, b, c, d, chamfer_normal);
    }
}

data::PcgMeshData bevel_mesh_vertex_push(const data::PcgMeshData& mesh, double amount, int segments)
{
    if (mesh.vertices().empty() || mesh.triangles().size() < 3)
        return mesh;

    amount = std::max(amount, 0.0);
    segments = std::clamp(segments, 1, 8);
    if (amount <= 1e-9)
        return mesh;

    data::PcgMeshData out = mesh;
    const double step = amount / static_cast<double>(segments);

    for (int pass = 0; pass < segments; ++pass) {
        std::vector<Vec3> accum;
        accumulate_vertex_normals(out, accum);

        const auto& verts = out.vertices();
        std::unordered_map<std::string, std::vector<size_t>> groups;
        groups.reserve(verts.size());
        for (size_t i = 0; i < verts.size(); ++i)
            groups[position_key(to_vec3(verts[i]))].push_back(i);

        auto& verts_mut = out.vertices_mut();
        for (const auto& [key, indices] : groups) {
            if (indices.empty())
                continue;

            Vec3 group_normal = {0.0, 0.0, 0.0};
            for (const size_t index : indices)
                group_normal = add(group_normal, accum[index]);
            group_normal = normalize(group_normal);
            if (length(group_normal) <= 1e-9)
                continue;

            const Vec3 base = to_vec3(verts[indices.front()]);
            const Vec3 target = add(base, scale(group_normal, step));
            const data::PcgVertex target_vertex = to_vertex(target);
            for (const size_t index : indices)
                verts_mut[index] = target_vertex;
        }
    }

    return out;
}

void add_three_way_corner_mesh(
    MeshBuilder& builder,
    const WeldedMesh& mesh,
    const std::unordered_map<int, std::vector<VertexCornerEntry>>& vertex_corners,
    const std::unordered_map<int, EdgeBevelProfile>& profiles)
{
    for (const auto& [vid, entries] : vertex_corners) {
        if (entries.size() < 3)
            continue;

        // Vertex normal = sum of all adjacent face normals (outward for convex corners).
        Vec3 vert_normal = {0.0, 0.0, 0.0};
        for (const auto& e : entries)
            vert_normal = add(vert_normal, e.face_normal);
        vert_normal = normalize(vert_normal);
        if (length(vert_normal) <= 1e-12)
            continue;

        // Sort entries by angle around vert_normal (CCW from outside).
        auto project_to_plane = [&](const Vec3& v) -> Vec3 {
            return sub(v, scale(vert_normal, dot(v, vert_normal)));
        };

        const Vec3 ref = normalize(project_to_plane(entries[0].face_normal));
        if (length(ref) <= 1e-12)
            continue;

        std::vector<int> ordered(entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
            ordered[i] = static_cast<int>(i);

        std::sort(ordered.begin() + 1, ordered.end(), [&](int a, int b) {
            const Vec3 va = normalize(project_to_plane(entries[static_cast<size_t>(a)].face_normal));
            const Vec3 vb = normalize(project_to_plane(entries[static_cast<size_t>(b)].face_normal));
            const double angle_a = std::atan2(dot(vert_normal, cross(ref, va)), dot(ref, va));
            const double angle_b = std::atan2(dot(vert_normal, cross(ref, vb)), dot(ref, vb));
            return angle_a < angle_b;
        });

        // For each adjacent pair, fan through the shared edge's profile rail.
        for (size_t i = 0; i < ordered.size(); ++i) {
            const int idx_a = ordered[i];
            const int idx_b = ordered[(i + 1) % ordered.size()];
            const auto& ea = entries[static_cast<size_t>(idx_a)];
            const auto& eb = entries[static_cast<size_t>(idx_b)];

            // Find the shared edge key and which profile points are on each face's side.
            int shared_key = -1;
            Vec3 strip_a{}, strip_b{};

            if (ea.out_edge_key == eb.in_edge_key) {
                shared_key = ea.out_edge_key;
                strip_a = ea.profile_out;
                strip_b = eb.profile_in;
            } else if (ea.in_edge_key == eb.out_edge_key) {
                shared_key = ea.in_edge_key;
                strip_a = ea.profile_in;
                strip_b = eb.profile_out;
            } else if (ea.in_edge_key == eb.in_edge_key) {
                shared_key = ea.in_edge_key;
                strip_a = ea.profile_in;
                strip_b = eb.profile_in;
            } else if (ea.out_edge_key == eb.out_edge_key) {
                shared_key = ea.out_edge_key;
                strip_a = ea.profile_out;
                strip_b = eb.profile_out;
            }

            if (shared_key < 0)
                continue;

            const auto profile_it = profiles.find(shared_key);
            if (profile_it == profiles.end())
                continue;

            const auto& profile = profile_it->second;
            const bool at_v0 = (vid == profile.v0);
            const auto& rail = at_v0 ? profile.side0 : profile.side1;

            // Determine rail direction: rail[0] should be on face_a's side.
            bool forward = (length(sub(rail.front(), strip_a)) <= length(sub(rail.back(), strip_a)));

            Vec3 desired = normalize(add(add(ea.face_normal, eb.face_normal), vert_normal));
            if (length(desired) <= 1e-12)
                desired = vert_normal;

            // Fan: meet_a → rail[0] → rail[1] → ... → rail[last] → meet_b
            // Each triangle (meet_a, rail[s], rail[s+1]) shares edge (rail[s], rail[s+1]) with strip.
            // Final triangle (meet_a, rail[end], meet_b) shares edge (rail[end], meet_b) with face_b.
            // Edge (meet_a, rail[0]) shared with face_a polygon.
            // Edge (meet_a, meet_b) shared with inner cap.
            for (size_t s = 0; s + 1 < rail.size(); ++s) {
                const Vec3& p0 = forward ? rail[s] : rail[rail.size() - 1 - s];
                const Vec3& p1 = forward ? rail[s + 1] : rail[rail.size() - 2 - s];
                if (length(cross(sub(p0, ea.meet), sub(p1, ea.meet))) > 1e-12)
                    builder.add_oriented_triangle(ea.meet, p0, p1, desired);
            }
            const Vec3& rail_end = forward ? rail.back() : rail.front();
            if (length(sub(ea.meet, eb.meet)) > 1e-9 &&
                length(cross(sub(rail_end, ea.meet), sub(eb.meet, ea.meet))) > 1e-12)
                builder.add_oriented_triangle(ea.meet, rail_end, eb.meet, desired);
        }

        // Inner cap: fan triangulation of all meet points (faces outward).
        for (size_t i = 1; i + 1 < ordered.size(); ++i) {
            builder.add_oriented_triangle(
                entries[static_cast<size_t>(ordered[0])].meet,
                entries[static_cast<size_t>(ordered[i])].meet,
                entries[static_cast<size_t>(ordered[i + 1])].meet,
                vert_normal);
        }
    }
}

data::PcgMeshData bevel_mesh_edge(const data::PcgMeshData& mesh, double amount, int segments,
                                  BevelOffsetType offset_type, bool clamp_overlap,
                                  double angle_limit_deg)
{
    if (mesh.vertices().empty() || mesh.triangles().size() < 3)
        return mesh;

    amount = std::max(amount, 0.0);
    segments = std::clamp(segments, 1, 8);
    if (amount <= 1e-9)
        return mesh;

    const WeldedMesh welded = weld_mesh(mesh);
    if (welded.triangles.empty())
        return mesh;

    const auto edges = build_edge_records(welded);
    const double cos_limit = std::cos(angle_limit_deg * 3.14159265358979323846 / 180.0);

    std::unordered_set<int> hard_edges;
    for (const auto& [key, record] : edges) {
        if (is_hard_edge(record, welded, cos_limit))
            hard_edges.insert(key);
    }

    if (hard_edges.empty())
        return mesh;

    std::unordered_map<std::string, std::vector<int>> face_tri_groups;
    for (size_t tri_index = 0; tri_index < welded.triangles.size(); ++tri_index)
        face_tri_groups[normal_key(tri_normal(welded, static_cast<int>(tri_index)))].push_back(
            static_cast<int>(tri_index));

    std::unordered_map<std::string, Vec3> face_centroids;
    std::unordered_map<std::string, std::vector<int>> face_loops;
    for (const auto& [group_key, tri_indices] : face_tri_groups) {
        face_centroids[group_key] = face_centroid(welded, tri_indices);
        std::vector<int> loop = boundary_loop(welded, tri_indices);
        if (loop.size() >= 3) {
            ensure_ccw_loop(welded, loop, tri_normal(welded, tri_indices.front()));
            face_loops[group_key] = std::move(loop);
        }
    }

    std::unordered_map<int, EdgeBevelProfile> profiles;
    for (const int key : hard_edges) {
        const auto it = edges.find(key);
        if (it == edges.end() || it->second.tri0 < 0 || it->second.tri1 < 0)
            continue;
        profiles[key] = build_edge_profile(welded, it->second, face_centroids, face_loops, amount,
                                           offset_type, segments, clamp_overlap, hard_edges, edges);
    }

    MeshBuilder builder;
    std::unordered_map<int, std::vector<VertexCornerEntry>> vertex_corners;
    for (const auto& [group_key, tri_indices] : face_tri_groups) {
        if (tri_indices.empty())
            continue;

        const auto loop_it = face_loops.find(group_key);
        if (loop_it == face_loops.end() || loop_it->second.size() < 3)
            continue;

        const Vec3 face_normal = tri_normal(welded, tri_indices.front());
        const std::vector<Vec3> polygon = build_offset_face_polygon(
            welded, loop_it->second, face_normal, face_centroids[group_key], hard_edges, profiles,
            tri_indices, vertex_corners);
        triangulate_convex_polygon(builder, polygon, face_normal);
    }

    for (const auto& [key, profile] : profiles)
        add_bevel_strip(builder, profile);

    add_three_way_corner_mesh(builder, welded, vertex_corners, profiles);

    return builder.mesh;
}

} // namespace

data::PcgMeshData create_box_mesh(double width, double height, double depth)
{
    const double hx = std::max(width, 0.0) * 0.5;
    const double hy = std::max(height, 0.0) * 0.5;
    const double hz = std::max(depth, 0.0) * 0.5;

    data::PcgMeshData mesh;

    // Each face uses 4 unique vertices so flat shading / RecalculateNormals stay correct.
    // Winding: v0→v1→v2→v3 is clockwise when viewed from outside (Unity front-face).

    // +X
    add_quad(mesh, {hx, -hy, -hz}, {hx, -hy, hz}, {hx, hy, hz}, {hx, hy, -hz});
    // -X
    add_quad(mesh, {-hx, -hy, hz}, {-hx, -hy, -hz}, {-hx, hy, -hz}, {-hx, hy, hz});
    // +Y
    add_quad(mesh, {-hx, hy, -hz}, {hx, hy, -hz}, {hx, hy, hz}, {-hx, hy, hz});
    // -Y
    add_quad(mesh, {-hx, -hy, hz}, {hx, -hy, hz}, {hx, -hy, -hz}, {-hx, -hy, -hz});
    // +Z
    add_quad(mesh, {-hx, -hy, hz}, {-hx, hy, hz}, {hx, hy, hz}, {hx, -hy, hz});
    // -Z
    add_quad(mesh, {hx, -hy, -hz}, {hx, hy, -hz}, {-hx, hy, -hz}, {-hx, -hy, -hz});

    return mesh;
}

data::PcgMeshData subdivide_mesh(const data::PcgMeshData& mesh, int levels)
{
    data::PcgMeshData current = mesh;
    levels = std::clamp(levels, 0, 4);

    for (int level = 0; level < levels; ++level) {
        data::PcgMeshData next;
        for (const auto& v : current.vertices())
            next.add_vertex(v);

        std::unordered_map<int, int> edge_midpoints;
        const auto& verts = current.vertices();
        const auto& tris = current.triangles();

        auto midpoint = [&](int a, int b) -> int {
            const int key = edge_key(a, b);
            const auto it = edge_midpoints.find(key);
            if (it != edge_midpoints.end())
                return it->second;

            const Vec3 va = to_vec3(verts[static_cast<size_t>(a)]);
            const Vec3 vb = to_vec3(verts[static_cast<size_t>(b)]);
            const int index = static_cast<int>(next.vertices().size());
            next.add_vertex(to_vertex(scale(add(va, vb), 0.5)));
            edge_midpoints[key] = index;
            return index;
        };

        for (size_t i = 0; i + 2 < tris.size(); i += 3) {
            const int a = tris[i];
            const int b = tris[i + 1];
            const int c = tris[i + 2];
            const int ab = midpoint(a, b);
            const int bc = midpoint(b, c);
            const int ca = midpoint(c, a);

            next.add_triangle(a, ab, ca);
            next.add_triangle(ab, b, bc);
            next.add_triangle(ca, bc, c);
            next.add_triangle(ab, bc, ca);
        }

        current = std::move(next);
    }

    return current;
}

data::PcgMeshData bevel_mesh(const data::PcgMeshData& mesh, double amount, int segments,
                             BevelMethod method, BevelOffsetType offset_type, bool clamp_overlap,
                             double angle_limit_deg)
{
    switch (method) {
    case BevelMethod::VertexPush:
        return bevel_mesh_vertex_push(mesh, amount, segments);
    case BevelMethod::Edge:
    default:
        return bevel_mesh_edge(mesh, amount, segments, offset_type, clamp_overlap, angle_limit_deg);
    }
}

} // namespace pcg::internal::elements
