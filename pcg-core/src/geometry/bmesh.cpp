#include "geometry/bmesh.hpp"

#include "data/pcg_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

std::vector<int> trace_boundary_loop(
    const std::unordered_map<int, std::vector<int>>& boundary_adj,
    const std::vector<Vec3>& positions) {
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

        // Filter out prev to get valid candidates
        std::vector<int> valid;
        for (int cand : it->second) {
            if (cand != prev)
                valid.push_back(cand);
        }

        if (valid.empty())
            break;

        int next;
        if (valid.size() == 1) {
            next = valid[0];
        } else {
            // Degree > 2: pick the candidate with the smallest turn from
            // the incoming direction (highest dot product with incoming).
            // This follows the correct boundary loop at pinch points where
            // multiple boundary loops share a vertex.
            const Vec3 incoming = (prev >= 0)
                ? sub(positions[static_cast<size_t>(current)], positions[static_cast<size_t>(prev)])
                : sub(positions[static_cast<size_t>(valid[0])], positions[static_cast<size_t>(current)]);
            const Vec3 in_norm = normalize(incoming);

            double best_dot = -2.0;
            next = valid[0];
            for (int cand : valid) {
                const Vec3 outgoing = normalize(
                    sub(positions[static_cast<size_t>(cand)], positions[static_cast<size_t>(current)]));
                const double d = dot(in_norm, outgoing);
                if (d > best_dot) {
                    best_dot = d;
                    next = cand;
                }
            }
        }

        if (next == start)
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

/// Merge adjacent coplanar n-gon faces (Simple subdiv → 4 quads per cube face, etc.).
/// triangle_indices / groups from each source face are unioned into the survivor.
void merge_coplanar_faces(BMesh& mesh, double merge_angle_deg) {
    const int face_count = static_cast<int>(mesh.faces.size());
    if (face_count < 2)
        return;

    const double cos_merge = std::cos(merge_angle_deg * kPi / 180.0);

    std::unordered_map<int64_t, std::vector<int>> edge_faces;
    for (int fi = 0; fi < face_count; ++fi) {
        const auto& verts = mesh.faces[static_cast<size_t>(fi)].verts;
        const int n = static_cast<int>(verts.size());
        for (int i = 0; i < n; ++i) {
            const int a = verts[static_cast<size_t>(i)];
            const int b = verts[static_cast<size_t>((i + 1) % n)];
            edge_faces[edge_key(a, b)].push_back(fi);
        }
    }

    std::vector<Vec3> normals(static_cast<size_t>(face_count));
    for (int fi = 0; fi < face_count; ++fi)
        normals[static_cast<size_t>(fi)] =
            face_normal_from_loop(mesh, mesh.faces[static_cast<size_t>(fi)].verts);

    UnionFind uf(face_count);
    for (const auto& entry : edge_faces) {
        const std::vector<int>& faces = entry.second;
        if (faces.size() != 2)
            continue;
        const int f0 = faces[0];
        const int f1 = faces[1];
        if (length_squared(normals[static_cast<size_t>(f0)]) < 1e-20 ||
            length_squared(normals[static_cast<size_t>(f1)]) < 1e-20)
            continue;
        if (dot(normals[static_cast<size_t>(f0)], normals[static_cast<size_t>(f1)]) >= cos_merge)
            uf.unite(f0, f1);
    }

    std::unordered_map<int, std::vector<int>> groups;
    for (int fi = 0; fi < face_count; ++fi)
        groups[uf.find(fi)].push_back(fi);

    if (static_cast<int>(groups.size()) == face_count)
        return;

    std::vector<BMeshFace> merged;
    merged.reserve(groups.size());

    for (auto& entry : groups) {
        std::vector<int>& members = entry.second;
        if (members.size() == 1) {
            merged.push_back(std::move(mesh.faces[static_cast<size_t>(members[0])]));
            continue;
        }

        std::unordered_map<int64_t, int> edge_use;
        for (int fi : members) {
            const auto& verts = mesh.faces[static_cast<size_t>(fi)].verts;
            const int n = static_cast<int>(verts.size());
            for (int i = 0; i < n; ++i) {
                const int a = verts[static_cast<size_t>(i)];
                const int b = verts[static_cast<size_t>((i + 1) % n)];
                edge_use[edge_key(a, b)]++;
            }
        }

        std::unordered_map<int, std::vector<int>> boundary_adj;
        for (int fi : members) {
            const auto& verts = mesh.faces[static_cast<size_t>(fi)].verts;
            const int n = static_cast<int>(verts.size());
            for (int i = 0; i < n; ++i) {
                const int a = verts[static_cast<size_t>(i)];
                const int b = verts[static_cast<size_t>((i + 1) % n)];
                if (edge_use[edge_key(a, b)] == 1) {
                    boundary_adj[a].push_back(b);
                    boundary_adj[b].push_back(a);
                }
            }
        }

        std::vector<int> loop = trace_boundary_loop(boundary_adj, mesh.verts);
        if (loop.size() < 3) {
            for (int fi : members)
                merged.push_back(std::move(mesh.faces[static_cast<size_t>(fi)]));
            continue;
        }

        const Vec3 ref_n = normals[static_cast<size_t>(members.front())];
        const Vec3 loop_n = face_normal_from_loop(mesh, loop);
        if (dot(ref_n, loop_n) < 0.0)
            std::reverse(loop.begin(), loop.end());

        BMeshFace face;
        face.verts = std::move(loop);
        for (int fi : members) {
            auto& src = mesh.faces[static_cast<size_t>(fi)];
            face.triangle_indices.insert(face.triangle_indices.end(),
                                         src.triangle_indices.begin(),
                                         src.triangle_indices.end());
            face.groups.insert(src.groups.begin(), src.groups.end());
        }
        merged.push_back(std::move(face));
    }

    mesh.faces = std::move(merged);
}

/// Remove valence-2 vertices that sit collinear on face boundaries (edge mids
/// left by Simple subdivision after coplanar merge). Restores clean cube edges.
void dissolve_collinear_valence2_verts(BMesh& mesh) {
    constexpr double kCosColinear = 0.999999; // ~0.08°

    auto rebuild_neighbors = [&](std::vector<std::unordered_set<int>>& nbrs) {
        nbrs.assign(mesh.verts.size(), {});
        for (const auto& face : mesh.faces) {
            const int n = static_cast<int>(face.verts.size());
            for (int i = 0; i < n; ++i) {
                const int a = face.verts[static_cast<size_t>(i)];
                const int b = face.verts[static_cast<size_t>((i + 1) % n)];
                if (a < 0 || b < 0 || a >= static_cast<int>(nbrs.size()) ||
                    b >= static_cast<int>(nbrs.size()))
                    continue;
                nbrs[static_cast<size_t>(a)].insert(b);
                nbrs[static_cast<size_t>(b)].insert(a);
            }
        }
    };

    std::vector<std::unordered_set<int>> nbrs;
    rebuild_neighbors(nbrs);

    for (int pass = 0; pass < 64; ++pass) {
        std::unordered_set<int> remove;
        for (int vi = 0; vi < static_cast<int>(nbrs.size()); ++vi) {
            if (nbrs[static_cast<size_t>(vi)].size() != 2)
                continue;
            auto it = nbrs[static_cast<size_t>(vi)].begin();
            const int a = *it++;
            const int b = *it;
            const Vec3 u = normalize(sub(mesh.verts[static_cast<size_t>(vi)],
                                         mesh.verts[static_cast<size_t>(a)]));
            const Vec3 v = normalize(sub(mesh.verts[static_cast<size_t>(b)],
                                         mesh.verts[static_cast<size_t>(vi)]));
            if (length_squared(u) < 1e-20 || length_squared(v) < 1e-20)
                continue;
            if (dot(u, v) >= kCosColinear)
                remove.insert(vi);
        }
        if (remove.empty())
            break;

        bool any = false;
        for (auto& face : mesh.faces) {
            std::vector<int> cleaned;
            cleaned.reserve(face.verts.size());
            for (int v : face.verts) {
                if (remove.count(v) == 0)
                    cleaned.push_back(v);
            }
            if (cleaned.size() >= 3 && cleaned.size() < face.verts.size()) {
                face.verts = std::move(cleaned);
                any = true;
            }
        }
        if (!any)
            break;
        rebuild_neighbors(nbrs);
    }
}

} // namespace

void build_disk_cycles(BMesh& mesh) {
    mesh.disk_cycles.clear();

    // Collect face-loop entries: for each vertex V in each face loop [..., A, V, B, ...],
    // the disk cycle at V goes: edge-to-A → face → edge-to-B (CCW from outside).
    struct LoopEntry {
        int prev_v;   // edge (prev_v → V) precedes V in the face loop
        int next_v;   // edge (V → next_v) follows V in the face loop
        int face;     // face index
    };

    std::unordered_map<int, std::vector<LoopEntry>> per_vertex;

    for (int fi = 0; fi < static_cast<int>(mesh.faces.size()); ++fi) {
        const auto& verts = mesh.faces[static_cast<size_t>(fi)].verts;
        const int n = static_cast<int>(verts.size());
        for (int i = 0; i < n; ++i) {
            const int v = verts[static_cast<size_t>(i)];
            const int prev_v = verts[static_cast<size_t>((i + n - 1) % n)];
            const int next_v = verts[static_cast<size_t>((i + 1) % n)];
            per_vertex[v].push_back({prev_v, next_v, fi});
        }
    }

    // Chain entries into ordered disk cycles.
    // Entry (prev_v=A, next_v=B, face=F) means: at V, edge-to-A is followed by
    // edge-to-B, with face F between them.  Chain: find next entry whose prev_v == current next_v.
    for (auto& [v_idx, entries] : per_vertex) {
        if (entries.empty())
            continue;

        std::vector<bool> used(entries.size(), false);
        std::vector<LoopEntry> ordered;
        ordered.reserve(entries.size());

        ordered.push_back(entries[0]);
        used[0] = true;
        int current_next = entries[0].next_v;

        for (size_t i = 1; i < entries.size(); ++i) {
            bool found = false;
            for (size_t j = 0; j < entries.size(); ++j) {
                if (used[j])
                    continue;
                if (entries[j].prev_v == current_next) {
                    ordered.push_back(entries[j]);
                    used[j] = true;
                    current_next = entries[j].next_v;
                    found = true;
                    break;
                }
            }
            if (!found) {
                // Non-manifold or broken topology: append remaining unordered.
                for (size_t j = 0; j < entries.size(); ++j)
                    if (!used[j])
                        ordered.push_back(entries[j]);
                break;
            }
        }

        // Convert ordered loop entries to disk entries.
        // ordered[i] = (prev_v, next_v, face)
        //   → edge at position i goes to prev_v
        //   → face between this edge and next = ordered[i].face
        //   → face between previous edge and this = ordered[(i-1+n)%n].face
        const int n = static_cast<int>(ordered.size());
        std::vector<BMeshDiskEntry> disk;
        disk.reserve(ordered.size());
        for (int i = 0; i < n; ++i) {
            const int prev_i = (i + n - 1) % n;
            disk.push_back({
                ordered[static_cast<size_t>(i)].prev_v,          // other_v
                ordered[static_cast<size_t>(prev_i)].face,        // fprev
                ordered[static_cast<size_t>(i)].face              // fnext
            });
        }

        mesh.disk_cycles[v_idx] = std::move(disk);
    }
}

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

        std::vector<int> loop = trace_boundary_loop(boundary_adj, welded.positions);
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
    build_disk_cycles(result);
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

    // Take n-gon faces directly (no fan re-triangulation — collinear verts on
    // loops yield zero-area tris that break angle-based merging).
    // Then merge adjacent coplanar faces (Simple subdiv leaves 4 quads / face)
    // and dissolve valence-2 collinear edge midpoints so bevel sees clean edges.
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

    // Attach face groups before merge so survivors union group names.
    for (const std::string& group_name : geometry.groups().group_names(GroupDomain::Face)) {
        const auto& members = geometry.groups().members(GroupDomain::Face, group_name);
        for (int fi : members) {
            if (fi < 0 || fi >= static_cast<int>(result.faces.size()))
                continue;
            result.faces[static_cast<size_t>(fi)].groups.insert(group_name);
        }
    }

    if (options.merge_coplanar_angle_deg > 0.0) {
        merge_coplanar_faces(result, options.merge_coplanar_angle_deg);
        dissolve_collinear_valence2_verts(result);
    }

    build_edges(result);

    for (const std::string& group_name : geometry.groups().group_names(GroupDomain::Edge)) {
        const auto& members = geometry.groups().members(GroupDomain::Edge, group_name);
        for (int64_t key : members) {
            const auto it = result.edges.find(key);
            if (it != result.edges.end())
                it->second.groups.insert(group_name);
        }
    }

    mark_sharp_edges(result, options.sharp_angle_deg);
    build_disk_cycles(result);
    return result;
}

} // namespace pcg::internal::geometry
