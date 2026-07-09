#include "geometry/bmesh.hpp"

#include "data/pcg_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

namespace pcg::internal::geometry {
namespace {

constexpr double kPi = 3.14159265358979323846;

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& v, double s) { return {v.x * s, v.y * s, v.z * s}; }
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(const Vec3& a, const Vec3& b) {
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}
double length_squared(const Vec3& v) { return dot(v, v); }
double length(const Vec3& v) { return std::sqrt(length_squared(v)); }

Vec3 normalize(const Vec3& v) {
    const double len = length(v);
    if (len <= 1e-15)
        return {0.0, 0.0, 0.0};
    return scale(v, 1.0 / len);
}

Vec3 tri_normal(const std::vector<Vec3>& verts, int a, int b, int c) {
    return normalize(cross(sub(verts[static_cast<size_t>(b)], verts[static_cast<size_t>(a)]),
                             sub(verts[static_cast<size_t>(c)], verts[static_cast<size_t>(a)])));
}

struct WeldedInput {
    std::vector<Vec3> positions;
    std::vector<std::array<int, 3>> triangles;
};

WeldedInput weld_mesh(const data::PcgMeshData& mesh, double weld_eps) {
    WeldedInput welded;
    std::unordered_map<std::string, int> index_by_key;
    std::vector<int> remap(mesh.vertices().size(), -1);

    const auto quantize = [weld_eps](double v) -> int64_t {
        return static_cast<int64_t>(std::llround(v / weld_eps));
    };

    for (size_t i = 0; i < mesh.vertices().size(); ++i) {
        const auto& v = mesh.vertices()[i];
        const Vec3 p{v.x, v.y, v.z};
        const std::string key = std::to_string(quantize(p.x)) + ',' + std::to_string(quantize(p.y)) +
                                ',' + std::to_string(quantize(p.z));
        const auto it = index_by_key.find(key);
        if (it == index_by_key.end()) {
            const int idx = static_cast<int>(welded.positions.size());
            welded.positions.push_back(p);
            index_by_key[key] = idx;
            remap[i] = idx;
        } else {
            remap[i] = it->second;
        }
    }

    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        welded.triangles.push_back({
            remap[static_cast<size_t>(mesh.triangles()[i])],
            remap[static_cast<size_t>(mesh.triangles()[i + 1])],
            remap[static_cast<size_t>(mesh.triangles()[i + 2])],
        });
    }

    return welded;
}

struct UnionFind {
    std::vector<int> parent;

    explicit UnionFind(int n) : parent(n) {
        for (int i = 0; i < n; ++i)
            parent[static_cast<size_t>(i)] = i;
    }

    int find(int x) {
        while (parent[static_cast<size_t>(x)] != x) {
            parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
            x = parent[static_cast<size_t>(x)];
        }
        return x;
    }

    void unite(int a, int b) {
        a = find(a);
        b = find(b);
        if (a != b)
            parent[static_cast<size_t>(b)] = a;
    }
};

std::vector<int> trace_boundary_loop(const std::unordered_map<int, std::vector<int>>& boundary_adj) {
    if (boundary_adj.empty())
        return {};

    const int start = boundary_adj.begin()->first;
    std::vector<int> loop = {start};
    int prev = -1;
    int current = start;

    while (true) {
        const auto it = boundary_adj.find(current);
        if (it == boundary_adj.end())
            break;

        int next = -1;
        for (int cand : it->second) {
            if (cand != prev) {
                next = cand;
                break;
            }
        }
        if (next < 0 || next == start)
            break;

        loop.push_back(next);
        prev = current;
        current = next;
        if (loop.size() > boundary_adj.size() + 1)
            break;
    }

    return loop.size() >= 3 ? loop : std::vector<int>{};
}

void build_edges(BMesh& mesh) {
    mesh.edges.clear();
    for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
        const auto& face = mesh.faces[static_cast<size_t>(fi)];
        const int n = static_cast<int>(face.verts.size());
        for (int i = 0; i < n; ++i) {
            const int a = face.verts[static_cast<size_t>(i)];
            const int b = face.verts[static_cast<size_t>((i + 1) % n)];
            const int64_t key = edge_key(a, b);
            auto& edge = mesh.edges[key];
            edge.v0 = std::min(a, b);
            edge.v1 = std::max(a, b);
            if (edge.face0 < 0)
                edge.face0 = fi;
            else if (edge.face1 < 0 && edge.face0 != fi)
                edge.face1 = fi;
        }
    }
}

void mark_sharp_edges(BMesh& mesh, double sharp_angle_deg) {
    const double cos_limit = std::cos(sharp_angle_deg * kPi / 180.0);
    for (auto& entry : mesh.edges) {
        BMeshEdge& edge = entry.second;
        if (edge.face0 < 0)
            continue;
        if (edge.face1 < 0) {
            edge.sharp = true;
            continue;
        }
        const Vec3 n0 = face_normal(mesh, edge.face0);
        const Vec3 n1 = face_normal(mesh, edge.face1);
        edge.sharp = dot(n0, n1) < cos_limit;
    }
}

} // namespace

int64_t edge_key(int a, int b) {
    if (a > b)
        std::swap(a, b);
    return static_cast<int64_t>(a) * 1000000 + b;
}

Vec3 face_normal_from_loop(const BMesh& mesh, const std::vector<int>& loop) {
    if (loop.size() < 3)
        return {0.0, 0.0, 0.0};

    const Vec3& a = mesh.verts[static_cast<size_t>(loop[0])];
    for (size_t i = 1; i + 1 < loop.size(); ++i) {
        const Vec3& b = mesh.verts[static_cast<size_t>(loop[i])];
        const Vec3& c = mesh.verts[static_cast<size_t>(loop[i + 1])];
        const Vec3 n = cross(sub(b, a), sub(c, a));
        if (length_squared(n) > 1e-20)
            return normalize(n);
    }
    return {0.0, 0.0, 0.0};
}

Vec3 face_normal(const BMesh& mesh, int face_index) {
    if (face_index < 0 || face_index >= static_cast<int>(mesh.faces.size()))
        return {0.0, 0.0, 0.0};
    return face_normal_from_loop(mesh, mesh.faces[static_cast<size_t>(face_index)].verts);
}

BMesh bmesh_from_mesh(const data::PcgMeshData& mesh, const BMeshBuildOptions& options) {
    BMesh result;
    if (mesh.vertices().empty() || mesh.triangles().size() < 3)
        return result;

    const WeldedInput welded = weld_mesh(mesh, options.weld_eps);
    const int tri_count = static_cast<int>(welded.triangles.size());
    if (tri_count == 0)
        return result;

    result.verts = welded.positions;

    const double cos_merge = std::cos(options.merge_coplanar_angle_deg * kPi / 180.0);

    std::unordered_map<int64_t, std::vector<int>> edge_tris;
    for (int ti = 0; ti < tri_count; ++ti) {
        const auto& tri = welded.triangles[static_cast<size_t>(ti)];
        for (int e = 0; e < 3; ++e) {
            const int a = tri[static_cast<size_t>(e)];
            const int b = tri[static_cast<size_t>((e + 1) % 3)];
            edge_tris[edge_key(a, b)].push_back(ti);
        }
    }

    UnionFind uf(tri_count);
    for (const auto& entry : edge_tris) {
        const std::vector<int>& tris = entry.second;
        if (tris.size() != 2)
            continue;
        const int t0 = tris[0];
        const int t1 = tris[1];
        const auto& tri0 = welded.triangles[static_cast<size_t>(t0)];
        const auto& tri1 = welded.triangles[static_cast<size_t>(t1)];
        const Vec3 n0 = tri_normal(welded.positions, tri0[0], tri0[1], tri0[2]);
        const Vec3 n1 = tri_normal(welded.positions, tri1[0], tri1[1], tri1[2]);
        if (dot(n0, n1) >= cos_merge)
            uf.unite(t0, t1);
    }

    std::unordered_map<int, std::vector<int>> groups;
    for (int ti = 0; ti < tri_count; ++ti)
        groups[uf.find(ti)].push_back(ti);

    for (auto& entry : groups) {
        std::vector<int>& tris = entry.second;

        std::unordered_map<int64_t, int> edge_use;
        for (int ti : tris) {
            const auto& tri = welded.triangles[static_cast<size_t>(ti)];
            for (int e = 0; e < 3; ++e) {
                const int a = tri[static_cast<size_t>(e)];
                const int b = tri[static_cast<size_t>((e + 1) % 3)];
                edge_use[edge_key(a, b)]++;
            }
        }

        std::unordered_map<int, std::vector<int>> boundary_adj;
        for (int ti : tris) {
            const auto& tri = welded.triangles[static_cast<size_t>(ti)];
            for (int e = 0; e < 3; ++e) {
                const int a = tri[static_cast<size_t>(e)];
                const int b = tri[static_cast<size_t>((e + 1) % 3)];
                if (edge_use[edge_key(a, b)] == 1) {
                    boundary_adj[a].push_back(b);
                    boundary_adj[b].push_back(a);
                }
            }
        }

        std::vector<int> loop = trace_boundary_loop(boundary_adj);
        if (loop.size() < 3) {
            for (int ti : tris) {
                const auto& tri = welded.triangles[static_cast<size_t>(ti)];
                BMeshFace face;
                face.verts = {tri[0], tri[1], tri[2]};
                face.triangle_indices = {ti};
                result.faces.push_back(std::move(face));
            }
            continue;
        }

        const auto& ref = welded.triangles[static_cast<size_t>(tris.front())];
        const Vec3 ref_n = tri_normal(welded.positions, ref[0], ref[1], ref[2]);
        const Vec3 loop_n = face_normal_from_loop(result, loop);
        if (dot(ref_n, loop_n) < 0.0)
            std::reverse(loop.begin(), loop.end());

        BMeshFace face;
        face.verts = std::move(loop);
        face.triangle_indices = std::move(tris);
        result.faces.push_back(std::move(face));
    }

    build_edges(result);
    mark_sharp_edges(result, options.sharp_angle_deg);
    return result;
}

data::PcgGeometry geometry_from_bmesh(const BMesh& mesh) {
    data::PcgGeometry geometry;
    for (const auto& v : mesh.verts)
        geometry.points_mut().push_back({v.x, v.y, v.z});

    for (size_t fi = 0; fi < mesh.faces.size(); ++fi) {
        const auto& face = mesh.faces[fi];
        if (face.verts.size() < 3)
            continue;
        geometry.faces_mut().push_back(face.verts);
        for (const std::string& group : face.groups)
            geometry.groups().add(GroupDomain::Face, group, static_cast<int>(fi));
    }

    for (const auto& entry : mesh.edges) {
        for (const std::string& group : entry.second.groups)
            geometry.groups().add(GroupDomain::Edge, group, static_cast<int>(entry.first));
    }

    return geometry;
}

data::PcgMeshData mesh_from_bmesh(const BMesh& mesh) {
    data::PcgMeshData out;
    for (const auto& v : mesh.verts)
        out.add_vertex({v.x, v.y, v.z});

    for (const auto& face : mesh.faces) {
        if (face.verts.size() < 3)
            continue;
        const int i0 = face.verts[0];
        for (size_t i = 1; i + 1 < face.verts.size(); ++i)
            out.add_triangle(i0, static_cast<int>(face.verts[i]), static_cast<int>(face.verts[i + 1]));
    }

    return out;
}

BMesh bmesh_from_geometry(const data::PcgGeometry& geometry, const BMeshBuildOptions& options) {
    BMesh result;
    if (geometry.points().empty() || geometry.faces().empty())
        return result;

    result.verts.reserve(geometry.points().size());
    for (const auto& p : geometry.points())
        result.verts.push_back({p.x, p.y, p.z});

    for (const auto& face : geometry.faces()) {
        if (face.size() < 3)
            continue;
        BMeshFace bm_face;
        bm_face.verts = face;
        result.faces.push_back(std::move(bm_face));
    }

    build_edges(result);

    for (const std::string& group_name : geometry.groups().group_names(GroupDomain::Face)) {
        const auto& members = geometry.groups().members(GroupDomain::Face, group_name);
        for (int fi : members) {
            if (fi < 0 || fi >= static_cast<int>(result.faces.size()))
                continue;
            result.faces[static_cast<size_t>(fi)].groups.insert(group_name);
        }
    }

    for (const std::string& group_name : geometry.groups().group_names(GroupDomain::Edge)) {
        const auto& members = geometry.groups().members(GroupDomain::Edge, group_name);
        for (int64_t key : members) {
            const auto it = result.edges.find(key);
            if (it != result.edges.end())
                it->second.groups.insert(group_name);
        }
    }

    mark_sharp_edges(result, options.sharp_angle_deg);
    return result;
}

} // namespace pcg::internal::geometry
