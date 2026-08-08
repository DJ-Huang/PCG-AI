#include "elements/mesh_scatter_algorithms.hpp"

#include "elements/element_utils.hpp"
#include "geometry/bmesh.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pcg::internal::elements {
namespace {

struct TriangleRef {
    int i0 = 0;
    int i1 = 0;
    int i2 = 0;
    double area = 0.0;
};

struct BoundaryEdge {
    data::PcgVec3 a;
    data::PcgVec3 b;
};

double triangle_area(const data::PcgVertex& a, const data::PcgVertex& b, const data::PcgVertex& c)
{
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double abz = b.z - a.z;
    const double acx = c.x - a.x;
    const double acy = c.y - a.y;
    const double acz = c.z - a.z;
    const double cx = aby * acz - abz * acy;
    const double cy = abz * acx - abx * acz;
    const double cz = abx * acy - aby * acx;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

double triangle_area_vec(const data::PcgVec3& a, const data::PcgVec3& b, const data::PcgVec3& c)
{
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double abz = b.z - a.z;
    const double acx = c.x - a.x;
    const double acy = c.y - a.y;
    const double acz = c.z - a.z;
    const double cx = aby * acz - abz * acy;
    const double cy = abz * acx - abx * acz;
    const double cz = abx * acy - aby * acx;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

void face_normal(const data::PcgVertex& a,
                 const data::PcgVertex& b,
                 const data::PcgVertex& c,
                 double& nx,
                 double& ny,
                 double& nz)
{
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double abz = b.z - a.z;
    const double acx = c.x - a.x;
    const double acy = c.y - a.y;
    const double acz = c.z - a.z;
    nx = aby * acz - abz * acy;
    ny = abz * acx - abx * acz;
    nz = abx * acy - aby * acx;
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-12) {
        nx /= len;
        ny /= len;
        nz /= len;
    }
}

double rand01(uint32_t& state)
{
    return static_cast<double>(next_rand(state)) / 4294967296.0;
}

int pick_triangle(const std::vector<double>& cumulative, double sample, int fallback)
{
    const auto it = std::lower_bound(cumulative.begin(), cumulative.end(), sample);
    if (it == cumulative.end())
        return fallback;
    return static_cast<int>(std::distance(cumulative.begin(), it));
}

double point_segment_distance(double px,
                              double py,
                              double pz,
                              const data::PcgVec3& a,
                              const data::PcgVec3& b)
{
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double abz = b.z - a.z;
    const double apx = px - a.x;
    const double apy = py - a.y;
    const double apz = pz - a.z;
    const double ab_len_sq = abx * abx + aby * aby + abz * abz;
    double t = 0.0;
    if (ab_len_sq > 1e-24)
        t = std::clamp((apx * abx + apy * aby + apz * abz) / ab_len_sq, 0.0, 1.0);
    const double dx = px - (a.x + abx * t);
    const double dy = py - (a.y + aby * t);
    const double dz = pz - (a.z + abz * t);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

double min_boundary_distance(double px,
                             double py,
                             double pz,
                             const std::vector<BoundaryEdge>& edges)
{
    double best = 1e300;
    for (const auto& edge : edges) {
        const double d = point_segment_distance(px, py, pz, edge.a, edge.b);
        if (d < best)
            best = d;
    }
    return best;
}

std::unordered_set<geometry::GroupId> build_allowed_faces(const data::PcgGeometry& geometry,
                                                          const SampleMeshSurfaceOptions& options)
{
    std::unordered_set<geometry::GroupId> allowed;
    if (options.face_group.empty()) {
        for (size_t i = 0; i < geometry.faces().size(); ++i)
            allowed.insert(static_cast<geometry::GroupId>(i));
    } else {
        allowed = geometry.groups().eval(geometry::GroupDomain::Face, options.face_group);
    }

    for (const std::string& exclude : options.exclude_groups) {
        if (exclude.empty())
            continue;
        const auto rejected = geometry.groups().eval(geometry::GroupDomain::Face, exclude);
        for (geometry::GroupId id : rejected)
            allowed.erase(id);
    }
    return allowed;
}

/** Edges that appear exactly once among the allowed faces — Houdini Blast(group)
 *  then Distance-From-Border unshared-edge semantics. */
std::vector<BoundaryEdge> build_group_boundary_edges(
    const data::PcgGeometry& geometry,
    const std::unordered_set<geometry::GroupId>& allowed)
{
    std::unordered_map<int64_t, int> use_count;
    for (geometry::GroupId face_id : allowed) {
        if (face_id < 0 || static_cast<size_t>(face_id) >= geometry.faces().size())
            continue;
        const auto& face = geometry.faces()[static_cast<size_t>(face_id)];
        const int n = static_cast<int>(face.size());
        for (int i = 0; i < n; ++i) {
            const int a = face[static_cast<size_t>(i)];
            const int b = face[static_cast<size_t>((i + 1) % n)];
            use_count[geometry::edge_key(a, b)]++;
        }
    }

    std::vector<BoundaryEdge> edges;
    edges.reserve(use_count.size());
    for (const auto& entry : use_count) {
        if (entry.second != 1)
            continue;
        const auto ends = geometry::edge_group_points(entry.first);
        if (ends[0] < 0 || ends[1] < 0 ||
            static_cast<size_t>(ends[0]) >= geometry.points().size() ||
            static_cast<size_t>(ends[1]) >= geometry.points().size())
            continue;
        edges.push_back({geometry.points()[static_cast<size_t>(ends[0])],
                         geometry.points()[static_cast<size_t>(ends[1])]});
    }
    return edges;
}

std::vector<BoundaryEdge> build_mesh_boundary_edges(const data::PcgMeshData& mesh)
{
    const auto& vertices = mesh.vertices();
    const auto& triangles = mesh.triangles();
    std::unordered_map<int64_t, int> use_count;
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const int i0 = triangles[t];
        const int i1 = triangles[t + 1];
        const int i2 = triangles[t + 2];
        use_count[geometry::edge_key(i0, i1)]++;
        use_count[geometry::edge_key(i1, i2)]++;
        use_count[geometry::edge_key(i2, i0)]++;
    }

    std::vector<BoundaryEdge> edges;
    for (const auto& entry : use_count) {
        if (entry.second != 1)
            continue;
        const auto ends = geometry::edge_group_points(entry.first);
        if (ends[0] < 0 || ends[1] < 0 ||
            static_cast<size_t>(ends[0]) >= vertices.size() ||
            static_cast<size_t>(ends[1]) >= vertices.size())
            continue;
        const auto& va = vertices[static_cast<size_t>(ends[0])];
        const auto& vb = vertices[static_cast<size_t>(ends[1])];
        edges.push_back({{va.x, va.y, va.z}, {vb.x, vb.y, vb.z}});
    }
    return edges;
}

data::PcgPointData sample_triangles(const std::vector<data::PcgVertex>& vertices,
                                    const std::vector<TriangleRef>& tris,
                                    const std::vector<BoundaryEdge>& boundary,
                                    const SampleMeshSurfaceOptions& options)
{
    data::PcgPointData points;
    if (options.count <= 0 || tris.empty())
        return points;

    std::vector<double> cumulative;
    cumulative.reserve(tris.size());
    double total_area = 0.0;
    for (const auto& tri : tris) {
        total_area += tri.area;
        cumulative.push_back(total_area);
    }
    if (total_area <= 1e-12)
        return points;

    uint32_t rng = mix_seed(options.seed, static_cast<int>(tris.size() * 97 + options.count));
    const bool use_margin = options.edge_margin > 0.0 && !boundary.empty();
    const int max_attempts = std::max(options.count * 50, options.count + 16);

    int accepted = 0;
    for (int attempt = 0; attempt < max_attempts && accepted < options.count; ++attempt) {
        if (options.is_cancel_requested && options.is_cancel_requested())
            return points;

        const double pick = rand01(rng) * total_area;
        const int tri_index = pick_triangle(cumulative, pick, static_cast<int>(tris.size()) - 1);
        const TriangleRef& tri = tris[static_cast<size_t>(tri_index)];

        const data::PcgVertex& a = vertices[static_cast<size_t>(tri.i0)];
        const data::PcgVertex& b = vertices[static_cast<size_t>(tri.i1)];
        const data::PcgVertex& c = vertices[static_cast<size_t>(tri.i2)];

        const double r1 = std::sqrt(rand01(rng));
        const double r2 = rand01(rng);
        const double u = 1.0 - r1;
        const double v = r1 * (1.0 - r2);
        const double w = r1 * r2;

        double nx = 0.0;
        double ny = 0.0;
        double nz = 0.0;
        face_normal(a, b, c, nx, ny, nz);

        double px = u * a.x + v * b.x + w * c.x;
        double py = u * a.y + v * b.y + w * c.y;
        double pz = u * a.z + v * b.z + w * c.z;

        if (use_margin &&
            min_boundary_distance(px, py, pz, boundary) < options.edge_margin)
            continue;

        if (options.looseness > 0.0) {
            const double jitter = (rand01(rng) - 0.5) * options.looseness;
            px += nx * jitter;
            py += ny * jitter;
            pz += nz * jitter;
        }

        if (options.normal_offset != 0.0) {
            px += nx * options.normal_offset;
            py += ny * options.normal_offset;
            pz += nz * options.normal_offset;
        }

        data::PcgPoint point;
        point.x = px;
        point.y = py;
        point.z = pz;
        point.attributes["nx"] = nx;
        point.attributes["ny"] = ny;
        point.attributes["nz"] = nz;
        point.attributes["triIndex"] = tri_index;
        points.add_point(point);
        ++accepted;
    }

    return points;
}

} // namespace

data::PcgPointData sample_mesh_surface(const data::PcgMeshData& mesh,
                                       const SampleMeshSurfaceOptions& options)
{
    data::PcgPointData empty;
    if (options.count <= 0)
        return empty;

    const auto& vertices = mesh.vertices();
    const auto& triangles = mesh.triangles();
    if (vertices.empty() || triangles.size() < 3)
        return empty;

    std::vector<TriangleRef> tris;
    tris.reserve(triangles.size() / 3);
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        if (options.is_cancel_requested && options.is_cancel_requested())
            return empty;

        const int i0 = triangles[t];
        const int i1 = triangles[t + 1];
        const int i2 = triangles[t + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || static_cast<size_t>(i0) >= vertices.size() ||
            static_cast<size_t>(i1) >= vertices.size() || static_cast<size_t>(i2) >= vertices.size())
            continue;

        const double area =
            triangle_area(vertices[static_cast<size_t>(i0)], vertices[static_cast<size_t>(i1)],
                          vertices[static_cast<size_t>(i2)]);
        if (area <= 1e-12)
            continue;

        tris.push_back({i0, i1, i2, area});
    }

    const std::vector<BoundaryEdge> boundary =
        options.edge_margin > 0.0 ? build_mesh_boundary_edges(mesh) : std::vector<BoundaryEdge>{};
    return sample_triangles(vertices, tris, boundary, options);
}

data::PcgPointData sample_mesh_surface(const data::PcgGeometry& geometry,
                                       const SampleMeshSurfaceOptions& options)
{
    data::PcgPointData empty;
    if (options.count <= 0)
        return empty;
    if (geometry.points().empty() || geometry.faces().empty())
        return empty;

    const auto allowed = build_allowed_faces(geometry, options);
    if (allowed.empty())
        return empty;

    // Fan-triangulate like triangulate_geometry_shared, keeping face index mapping.
    std::vector<data::PcgVertex> vertices;
    vertices.reserve(geometry.points().size());
    for (const auto& p : geometry.points())
        vertices.push_back({p.x, p.y, p.z});

    std::vector<TriangleRef> tris;
    for (size_t face_index = 0; face_index < geometry.faces().size(); ++face_index) {
        if (options.is_cancel_requested && options.is_cancel_requested())
            return empty;
        if (allowed.count(static_cast<geometry::GroupId>(face_index)) == 0)
            continue;

        const auto& face = geometry.faces()[face_index];
        if (face.size() < 3)
            continue;

        const auto triangles = data::triangulate_face_corners(geometry.points(), face);
        for (const auto& triangle : triangles) {
            const int i0 = face[static_cast<size_t>(triangle[0])];
            const int i1 = face[static_cast<size_t>(triangle[1])];
            const int i2 = face[static_cast<size_t>(triangle[2])];
            if (i0 < 0 || i1 < 0 || i2 < 0 || static_cast<size_t>(i0) >= vertices.size() ||
                static_cast<size_t>(i1) >= vertices.size() ||
                static_cast<size_t>(i2) >= vertices.size())
                continue;

            const double area = triangle_area_vec(geometry.points()[static_cast<size_t>(i0)],
                                                  geometry.points()[static_cast<size_t>(i1)],
                                                  geometry.points()[static_cast<size_t>(i2)]);
            if (area <= 1e-12)
                continue;
            tris.push_back({i0, i1, i2, area});
        }
    }

    const std::vector<BoundaryEdge> boundary =
        options.edge_margin > 0.0 ? build_group_boundary_edges(geometry, allowed)
                                  : std::vector<BoundaryEdge>{};
    return sample_triangles(vertices, tris, boundary, options);
}

// ── Point Relax ──────────────────────────────────────────────────────────

namespace {

double read_pscale(const nlohmann::json& attributes, double fallback)
{
    if (attributes.contains("pscale") && attributes["pscale"].is_number())
        return std::max(0.0, attributes["pscale"].get<double>());
    return fallback;
}

bool read_normal(const nlohmann::json& attributes, double& nx, double& ny, double& nz)
{
    nx = ny = nz = 0.0;
    if (attributes.contains("nx") && attributes["nx"].is_number())
        nx = attributes["nx"].get<double>();
    else
        return false;
    if (attributes.contains("ny") && attributes["ny"].is_number())
        ny = attributes["ny"].get<double>();
    if (attributes.contains("nz") && attributes["nz"].is_number())
        nz = attributes["nz"].get<double>();
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12)
        return false;
    nx /= len; ny /= len; nz /= len;
    return true;
}

} // namespace

data::PcgPointData relax_points(const data::PcgPointData& input,
                                const PointRelaxOptions& options)
{
    data::PcgPointData output = input;
    std::vector<data::PcgPoint>& pts = output.points_mut();
    const int n = static_cast<int>(pts.size());
    if (n < 2)
        return output;

    // Gather radii and normals.
    std::vector<double> radii(n);
    std::vector<double> normals(n * 3, 0.0);
    std::vector<bool> has_normal(n, false);
    for (int i = 0; i < n; ++i) {
        radii[i] = options.use_pscale
            ? read_pscale(pts[i].attributes, options.radius)
            : options.radius;
        double nx, ny, nz;
        if (read_normal(pts[i].attributes, nx, ny, nz)) {
            normals[i * 3]     = nx;
            normals[i * 3 + 1] = ny;
            normals[i * 3 + 2] = nz;
            has_normal[i] = true;
        }
    }

    for (int iter = 0; iter < options.max_iterations; ++iter) {
        if (options.is_cancel_requested && options.is_cancel_requested())
            break;

        std::vector<double> disp(n * 3, 0.0);
        bool any_overlap = false;

        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                double dx = pts[j].x - pts[i].x;
                double dy = pts[j].y - pts[i].y;
                double dz = pts[j].z - pts[i].z;
                double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                const double min_dist = radii[i] + radii[j];
                if (dist >= min_dist || dist < 1e-12)
                    continue;

                any_overlap = true;
                double overlap = (min_dist - dist) * 0.5;
                dx /= dist; dy /= dist; dz /= dist;

                // Project push direction onto the surface plane (perpendicular
                // to each point's normal) when a normal is available.
                auto project = [&](double dirx, double diry, double dirz,
                                   double nx, double ny, double nz) {
                    const double dot = dirx * nx + diry * ny + dirz * nz;
                    dirx -= nx * dot;
                    diry -= ny * dot;
                    dirz -= nz * dot;
                    const double len = std::sqrt(dirx * dirx +
                                                  diry * diry +
                                                  dirz * dirz);
                    if (len < 1e-12)
                        return std::make_tuple(0.0, 0.0, 0.0);
                    return std::make_tuple(dirx / len * overlap,
                                            diry / len * overlap,
                                            dirz / len * overlap);
                };

                // Push i away from j (negative direction), j away from i.
                if (has_normal[i]) {
                    auto [px, py, pz] = project(-dx, -dy, -dz,
                                                 normals[i * 3],
                                                 normals[i * 3 + 1],
                                                 normals[i * 3 + 2]);
                    disp[i * 3]     += px;
                    disp[i * 3 + 1] += py;
                    disp[i * 3 + 2] += pz;
                } else {
                    disp[i * 3]     += -dx * overlap;
                    disp[i * 3 + 1] += -dy * overlap;
                    disp[i * 3 + 2] += -dz * overlap;
                }

                if (has_normal[j]) {
                    auto [px, py, pz] = project(dx, dy, dz,
                                                 normals[j * 3],
                                                 normals[j * 3 + 1],
                                                 normals[j * 3 + 2]);
                    disp[j * 3]     += px;
                    disp[j * 3 + 1] += py;
                    disp[j * 3 + 2] += pz;
                } else {
                    disp[j * 3]     += dx * overlap;
                    disp[j * 3 + 1] += dy * overlap;
                    disp[j * 3 + 2] += dz * overlap;
                }
            }
        }

        if (!any_overlap)
            break;

        for (int i = 0; i < n; ++i) {
            pts[i].x += disp[i * 3];
            pts[i].y += disp[i * 3 + 1];
            pts[i].z += disp[i * 3 + 2];
        }
    }

    return output;
}

// ── Points From Volume ────────────────────────────────────────────────────

namespace {

struct VolumeTriangle {
    double v0[3];
    double v1[3];
    double v2[3];
};

// Moller-Trumbore ray-triangle intersection. Returns true if the ray
// (origin + t*dir, dir = +X with tiny Y/Z skew to avoid shared-edge double-count)
// hits the triangle. Uses double-precision eps.
bool ray_hits_triangle_x(const double origin[3], const VolumeTriangle& tri)
{
    // Skew the ray direction slightly off-axis to avoid passing through
    // shared triangle edges, which would cause double-counting.
    // Asymmetric Y/Z skew so diagonal y==z rays don't stay on the diagonal.
    const double dir[3] = {1.0, 1.7e-4, 3.1e-4};
    const double e1x = tri.v1[0] - tri.v0[0];
    const double e1y = tri.v1[1] - tri.v0[1];
    const double e1z = tri.v1[2] - tri.v0[2];
    const double e2x = tri.v2[0] - tri.v0[0];
    const double e2y = tri.v2[1] - tri.v0[1];
    const double e2z = tri.v2[2] - tri.v0[2];

    // h = cross(dir, e2)
    const double hx = dir[1] * e2z - dir[2] * e2y;
    const double hy = dir[2] * e2x - dir[0] * e2z;
    const double hz = dir[0] * e2y - dir[1] * e2x;

    const double a = e1x * hx + e1y * hy + e1z * hz;
    if (std::abs(a) < 1e-14)
        return false;

    const double inv_a = 1.0 / a;
    const double sx = origin[0] - tri.v0[0];
    const double sy = origin[1] - tri.v0[1];
    const double sz = origin[2] - tri.v0[2];

    // u = dot(s, h) * inv_a
    const double u = (sx * hx + sy * hy + sz * hz) * inv_a;
    if (u < -1e-10 || u > 1.0 + 1e-10)
        return false;

    // q = cross(s, e1)
    const double qx = sy * e1z - sz * e1y;
    const double qy = sz * e1x - sx * e1z;
    const double qz = sx * e1y - sy * e1x;

    // v = dot(dir, q) * inv_a
    const double v = (dir[0] * qx + dir[1] * qy + dir[2] * qz) * inv_a;
    if (v < -1e-10 || u + v > 1.0 + 1e-10)
        return false;

    const double t = (e2x * qx + e2y * qy + e2z * qz) * inv_a;
    return t > 1e-10;
}

bool point_inside_mesh(const double origin[3], const std::vector<VolumeTriangle>& tris)
{
    int crossings = 0;
    for (const auto& tri : tris) {
        if (ray_hits_triangle_x(origin, tri))
            ++crossings;
    }
    return (crossings & 1) != 0;
}

std::vector<VolumeTriangle> build_volume_triangles(const data::PcgMeshData& mesh)
{
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    std::vector<VolumeTriangle> out;
    out.reserve(tris.size() / 3);
    for (size_t t = 0; t + 2 < tris.size(); t += 3) {
        const int i0 = tris[t];
        const int i1 = tris[t + 1];
        const int i2 = tris[t + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 ||
            static_cast<size_t>(i0) >= verts.size() ||
            static_cast<size_t>(i1) >= verts.size() ||
            static_cast<size_t>(i2) >= verts.size())
            continue;
        const auto& a = verts[static_cast<size_t>(i0)];
        const auto& b = verts[static_cast<size_t>(i1)];
        const auto& c = verts[static_cast<size_t>(i2)];
        out.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}});
    }
    return out;
}

std::vector<VolumeTriangle> build_volume_triangles(const data::PcgGeometry& geometry)
{
    std::vector<VolumeTriangle> out;
    const auto& pts = geometry.points();
    for (const auto& face : geometry.faces()) {
        if (face.size() < 3)
            continue;
        const auto tri_indices = data::triangulate_face_corners(pts, face);
        for (const auto& tri : tri_indices) {
            const int i0 = face[static_cast<size_t>(tri[0])];
            const int i1 = face[static_cast<size_t>(tri[1])];
            const int i2 = face[static_cast<size_t>(tri[2])];
            if (i0 < 0 || i1 < 0 || i2 < 0 ||
                static_cast<size_t>(i0) >= pts.size() ||
                static_cast<size_t>(i1) >= pts.size() ||
                static_cast<size_t>(i2) >= pts.size())
                continue;
            const auto& a = pts[static_cast<size_t>(i0)];
            const auto& b = pts[static_cast<size_t>(i1)];
            const auto& c = pts[static_cast<size_t>(i2)];
            out.push_back({{a.x, a.y, a.z}, {b.x, b.y, b.z}, {c.x, c.y, c.z}});
        }
    }
    return out;
}

data::PcgPointData sample_volume_from_triangles(
    const std::vector<VolumeTriangle>& tris,
    const PointsFromVolumeOptions& options)
{
    data::PcgPointData points;
    if (tris.empty() || options.point_separation <= 0.0)
        return points;

    // Compute AABB.
    double minx = 1e300, miny = 1e300, minz = 1e300;
    double maxx = -1e300, maxy = -1e300, maxz = -1e300;
    for (const auto& tri : tris) {
        for (int v = 0; v < 3; ++v) {
            const double* p = v == 0 ? tri.v0 : (v == 1 ? tri.v1 : tri.v2);
            minx = std::min(minx, p[0]); maxx = std::max(maxx, p[0]);
            miny = std::min(miny, p[1]); maxy = std::max(maxy, p[1]);
            minz = std::min(minz, p[2]); maxz = std::max(maxz, p[2]);
        }
    }

    const double cell = options.point_separation;
    const int nx = std::max(1, static_cast<int>(std::ceil((maxx - minx) / cell)));
    const int ny = std::max(1, static_cast<int>(std::ceil((maxy - miny) / cell)));
    const int nz = std::max(1, static_cast<int>(std::ceil((maxz - minz) / cell)));

    // Cap voxel count to prevent runaway memory.
    if (static_cast<int64_t>(nx) * ny * nz > 50'000'000)
        return points;

    // Mark inside voxels.
    std::vector<uint8_t> inside(static_cast<size_t>(nx) * ny * nz, 0);
    for (int iz = 0; iz < nz; ++iz) {
        if (options.is_cancel_requested && options.is_cancel_requested())
            return points;
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix) {
                const double origin[3] = {
                    minx + (ix + 0.5) * cell,
                    miny + (iy + 0.5) * cell,
                    minz + (iz + 0.5) * cell,
                };
                const size_t idx = static_cast<size_t>(ix) + static_cast<size_t>(ny) * (iy + static_cast<size_t>(nz) * iz);
                if (point_inside_mesh(origin, tris))
                    inside[idx] = 1;
            }
        }
    }

    // For shell_only: keep only voxels that have at least one 6-neighbor outside.
    auto is_inside = [&](int ix, int iy, int iz) -> bool {
        if (ix < 0 || ix >= nx || iy < 0 || iy >= ny || iz < 0 || iz >= nz)
            return false;
        return inside[static_cast<size_t>(ix) + static_cast<size_t>(ny) * (iy + static_cast<size_t>(nz) * iz)] != 0;
    };

    uint32_t rng = mix_seed(options.seed, nx * 31 + ny * 17 + nz);

    for (int iz = 0; iz < nz; ++iz) {
        if (options.is_cancel_requested && options.is_cancel_requested())
            return points;
        for (int iy = 0; iy < ny; ++iy) {
            for (int ix = 0; ix < nx; ++ix) {
                const size_t idx = static_cast<size_t>(ix) + static_cast<size_t>(ny) * (iy + static_cast<size_t>(nz) * iz);
                if (!inside[idx])
                    continue;

                if (options.shell_only) {
                    const bool has_outside_neighbor =
                        !is_inside(ix - 1, iy, iz) || !is_inside(ix + 1, iy, iz) ||
                        !is_inside(ix, iy - 1, iz) || !is_inside(ix, iy + 1, iz) ||
                        !is_inside(ix, iy, iz - 1) || !is_inside(ix, iy, iz + 1);
                    if (!has_outside_neighbor)
                        continue;
                }

                double px = minx + (ix + 0.5) * cell;
                double py = miny + (iy + 0.5) * cell;
                double pz = minz + (iz + 0.5) * cell;

                if (options.jitter > 0.0) {
                    px += (rand01(rng) - 0.5) * options.jitter;
                    py += (rand01(rng) - 0.5) * options.jitter;
                    pz += (rand01(rng) - 0.5) * options.jitter;
                }

                data::PcgPoint point;
                point.x = px;
                point.y = py;
                point.z = pz;
                points.add_point(point);
            }
        }
    }

    return points;
}

} // namespace

data::PcgPointData sample_mesh_volume(const data::PcgMeshData& mesh,
                                       const PointsFromVolumeOptions& options)
{
    return sample_volume_from_triangles(build_volume_triangles(mesh), options);
}

data::PcgPointData sample_mesh_volume(const data::PcgGeometry& geometry,
                                       const PointsFromVolumeOptions& options)
{
    return sample_volume_from_triangles(build_volume_triangles(geometry), options);
}

} // namespace pcg::internal::elements
