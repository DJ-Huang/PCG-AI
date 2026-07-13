#include "elements/mesh_algorithms.hpp"
#include "elements/bevel_blender.hpp"
#include "elements/element_utils.hpp"

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

} // namespace

data::PcgMeshData noise_deform_mesh(const data::PcgMeshData& mesh, const NoiseDeformOptions& options)
{
    if (mesh.vertices().empty() || mesh.triangles().size() < 3)
        return mesh;

    NoiseDeformOptions opts = options;
    opts.intensity = std::max(opts.intensity, 0.0);
    opts.noise_scale = std::max(opts.noise_scale, 1e-6);
    opts.repeat_x = std::max(opts.repeat_x, 1e-6);
    opts.repeat_y = std::max(opts.repeat_y, 1e-6);
    if (opts.intensity <= 1e-9)
        return mesh;

    if (opts.noise_type == NoiseDeformType::Texture &&
        (opts.texture == nullptr || opts.texture->empty()))
        return mesh;

    data::PcgMeshData out = mesh;
    std::vector<Vec3> accum;
    accumulate_vertex_normals(out, accum);

    const double seed_offset = static_cast<double>(opts.seed) * 0.137;
    auto& verts_mut = out.vertices_mut();
    for (size_t i = 0; i < verts_mut.size(); ++i) {
        Vec3 n = normalize(accum[i]);
        if (length(n) <= 1e-9)
            continue;

        const Vec3 p = to_vec3(verts_mut[i]);
        double displacement = 0.0;
        if (opts.noise_type == NoiseDeformType::Texture && opts.texture) {
            const double tin = opts.texture->sample_grayscale_local(
                p.x, p.y, p.z, opts.noise_scale, opts.repeat_x, opts.repeat_y);
            displacement = (tin - opts.mid_level) * opts.intensity;
        }
        else {
            const double nx = (p.x + seed_offset) * opts.noise_scale;
            const double ny = (p.y + seed_offset * 1.3) * opts.noise_scale;
            const double nz = (p.z + seed_offset * 1.7) * opts.noise_scale;
            displacement = perlin_noise_3d(nx, ny, nz, opts.seed) * opts.intensity;
        }

        verts_mut[i] = to_vertex(add(p, scale(n, displacement)));
    }

    return out;
}

data::PcgMeshData noise_deform_mesh(const data::PcgMeshData& mesh, double intensity, double noise_scale,
                                    NoiseDeformType noise_type, int seed)
{
    NoiseDeformOptions opts;
    opts.intensity = intensity;
    opts.noise_scale = noise_scale;
    opts.noise_type = noise_type;
    opts.seed = seed;
    return noise_deform_mesh(mesh, opts);
}

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

data::PcgMeshData create_cylinder_mesh(double radius, double height,
                                       int radial_segments, int height_segments,
                                       bool cap_top, bool cap_bottom)
{
    if (radius < 0.001 || height < 0.001 || radial_segments < 3 || height_segments < 1)
        return {};

    radial_segments = std::min(radial_segments, 128);
    height_segments = std::min(height_segments, 64);

    data::PcgMeshData mesh;
    const double half_h = height * 0.5;
    const double pi = 3.14159265358979323846;

    // Side vertices: (heightSegments + 1) rings * radialSegments
    for (int h = 0; h <= height_segments; ++h) {
        const double y = -half_h + height * static_cast<double>(h) / height_segments;
        for (int r = 0; r < radial_segments; ++r) {
            const double angle = 2.0 * pi * r / radial_segments;
            mesh.add_vertex({radius * std::cos(angle), y, radius * std::sin(angle)});
        }
    }

    // Side faces — outward winding matching add_quad convention
    for (int h = 0; h < height_segments; ++h) {
        for (int r = 0; r < radial_segments; ++r) {
            const int r_next = (r + 1) % radial_segments;
            const int i0 = h * radial_segments + r;
            const int i1 = h * radial_segments + r_next;
            const int i2 = (h + 1) * radial_segments + r_next;
            const int i3 = (h + 1) * radial_segments + r;
            mesh.add_triangle(i0, i2, i1);
            mesh.add_triangle(i0, i3, i2);
        }
    }

    const int bottom_ring = 0;
    const int top_ring = height_segments * radial_segments;

    if (cap_bottom) {
        const int center_idx = static_cast<int>(mesh.vertices().size());
        mesh.add_vertex({0.0, -half_h, 0.0});
        for (int r = 0; r < radial_segments; ++r) {
            const int r_next = (r + 1) % radial_segments;
            mesh.add_triangle(center_idx, bottom_ring + r, bottom_ring + r_next);
        }
    }

    if (cap_top) {
        const int center_idx = static_cast<int>(mesh.vertices().size());
        mesh.add_vertex({0.0, half_h, 0.0});
        for (int r = 0; r < radial_segments; ++r) {
            const int r_next = (r + 1) % radial_segments;
            mesh.add_triangle(center_idx, top_ring + r_next, top_ring + r);
        }
    }

    return mesh;
}

data::PcgGeometry revolve_geometry(const data::PcgSplineData& profile,
                                  const RevolveGeometryOptions& options)
{
    if (profile.splines().empty())
        return {};

    const auto& spline = profile.splines()[0];
    if (spline.points.empty())
        return {};

    int axis = -1;
    if (options.axis == "x" || options.axis == "X") axis = 0;
    else if (options.axis == "y" || options.axis == "Y") axis = 1;
    else if (options.axis == "z" || options.axis == "Z") axis = 2;
    else return {};

    if (options.segments < 3)
        return {};

    const int seg = options.segments;
    const double pi = 3.14159265358979323846;
    const double eps = 1e-8;

    // Extract profile points as Vec3
    std::vector<Vec3> prof;
    prof.reserve(spline.points.size());
    for (const auto& p : spline.points)
        prof.push_back({p.x, p.y, p.z});

    // Helper: distance to rotation axis
    auto axis_dist = [axis](const Vec3& p) -> double {
        if (axis == 0) return std::sqrt(p.y * p.y + p.z * p.z);
        if (axis == 1) return std::sqrt(p.x * p.x + p.z * p.z);
        return std::sqrt(p.x * p.x + p.y * p.y);
    };

    // Helper: create ring point at angle theta
    auto ring_point = [axis](const Vec3& p, double theta) -> data::PcgVec3 {
        const double c = std::cos(theta);
        const double s = std::sin(theta);
        if (axis == 0) return {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
        if (axis == 1) return {p.x * c - p.z * s, p.y, p.x * s + p.z * c};
        return {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
    };

    // Helper: axis projection of a point
    auto axis_point = [axis](const Vec3& p) -> data::PcgVec3 {
        if (axis == 0) return {p.x, 0.0, 0.0};
        if (axis == 1) return {0.0, p.y, 0.0};
        return {0.0, 0.0, p.z};
    };

    data::PcgGeometry geo;

    // Generate vertices
    // For each profile point: if on axis, create 1 point; otherwise create seg points
    std::vector<std::vector<int>> vert_idx(prof.size());
    for (size_t i = 0; i < prof.size(); ++i) {
        if (axis_dist(prof[i]) <= eps) {
            vert_idx[i].resize(1);
            vert_idx[i][0] = static_cast<int>(geo.points().size());
            geo.points_mut().push_back(axis_point(prof[i]));
        } else {
            vert_idx[i].resize(seg);
            for (int j = 0; j < seg; ++j) {
                const double theta = 2.0 * pi * j / seg;
                vert_idx[i][j] = static_cast<int>(geo.points().size());
                geo.points_mut().push_back(ring_point(prof[i], theta));
            }
        }
    }

    // Generate faces
    auto add_quad_face = [&geo](int a, int b, int c, int d) {
        geo.faces_mut().push_back({a, b, c, d});
    };
    auto add_tri_face = [&geo](int a, int b, int c) {
        geo.faces_mut().push_back({a, b, c});
    };

    int n = static_cast<int>(prof.size());
    if (options.close_profile) {
        // Connect last to first
        for (int i = 0; i < n; ++i) {
            int i2 = (i + 1) % n;
            const auto& vi = vert_idx[i];
            const auto& vj = vert_idx[i2];
            bool ai = (vi.size() == 1);
            bool aj = (vj.size() == 1);
            if (ai && aj) continue;
            if (ai) {
                // axis-ring → triangle
                for (int j = 0; j < seg; ++j) {
                    int jn = (j + 1) % seg;
                    add_tri_face(vi[0], vj[j], vj[jn]);
                }
            } else if (aj) {
                for (int j = 0; j < seg; ++j) {
                    int jn = (j + 1) % seg;
                    add_tri_face(vi[j], vj[0], vi[jn]);
                }
            } else {
                for (int j = 0; j < seg; ++j) {
                    int jn = (j + 1) % seg;
                    add_quad_face(vi[j], vj[j], vj[jn], vi[jn]);
                }
            }
        }
    } else {
        for (int i = 0; i < n - 1; ++i) {
            const auto& vi = vert_idx[i];
            const auto& vj = vert_idx[i + 1];
            bool ai = (vi.size() == 1);
            bool aj = (vj.size() == 1);
            if (ai && aj) continue;
            if (ai) {
                for (int j = 0; j < seg; ++j) {
                    int jn = (j + 1) % seg;
                    add_tri_face(vi[0], vj[j], vj[jn]);
                }
            } else if (aj) {
                for (int j = 0; j < seg; ++j) {
                    int jn = (j + 1) % seg;
                    add_tri_face(vi[j], vj[0], vi[jn]);
                }
            } else {
                for (int j = 0; j < seg; ++j) {
                    int jn = (j + 1) % seg;
                    add_quad_face(vi[j], vj[j], vj[jn], vi[jn]);
                }
            }
        }

        // Caps for open profile
        if (options.cap_start && vert_idx[0].size() > 1) {
            // Project first profile point to axis
            int cap_center = static_cast<int>(geo.points().size());
            geo.points_mut().push_back(axis_point(prof[0]));
            for (int j = 0; j < seg; ++j) {
                int jn = (j + 1) % seg;
                if (axis == 1)
                    add_tri_face(cap_center, vert_idx[0][j], vert_idx[0][jn]);
                else
                    add_tri_face(cap_center, vert_idx[0][jn], vert_idx[0][j]);
            }
        }
        if (options.cap_end && vert_idx[n - 1].size() > 1) {
            int cap_center = static_cast<int>(geo.points().size());
            geo.points_mut().push_back(axis_point(prof[n - 1]));
            for (int j = 0; j < seg; ++j) {
                int jn = (j + 1) % seg;
                if (axis == 1)
                    add_tri_face(cap_center, vert_idx[n - 1][jn], vert_idx[n - 1][j]);
                else
                    add_tri_face(cap_center, vert_idx[n - 1][j], vert_idx[n - 1][jn]);
            }
        }
    }

    return geo;
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
                             double angle_limit_deg, float profile,
                             BevelMiter miter_outer, BevelMiter miter_inner,
                             BevelVMeshMethod vmesh_method,
                             bool (*is_cancel_requested)(),
                             const BevelEdgeSelection& edge_selection,
                             const data::PcgGeometry* geometry)
{
    switch (method) {
    case BevelMethod::VertexPush:
        return bevel_mesh_vertex_push(mesh, amount, segments);
    case BevelMethod::Edge:
    default:
        return bevel::bevel_mesh_blender(
            mesh, amount, segments,
            bevel::BevelOffsetType(offset_type),
            clamp_overlap, angle_limit_deg, profile,
            bevel::BevelMiter(miter_outer),
            bevel::BevelMiter(miter_inner),
            bevel::BevelVMeshMethod(vmesh_method),
            is_cancel_requested,
            edge_selection,
            geometry);
    }
}

data::PcgGeometry bevel_geometry(const data::PcgGeometry& geometry, double amount, int segments,
                                 BevelMethod method, BevelOffsetType offset_type,
                                 bool clamp_overlap, double angle_limit_deg, float profile,
                                 BevelMiter miter_outer, BevelMiter miter_inner,
                                 BevelVMeshMethod vmesh_method,
                                 bool (*is_cancel_requested)(),
                                 const BevelEdgeSelection& edge_selection)
{
    // Shared-vertex triangulation: geometry.points() are already welded, so use
    // them directly to ensure BMesh (from geometry) and WeldedMesh (from mesh)
    // share the same vertex indices. Per-face duplication (triangulate_geometry)
    // reorders vertices on re-weld, breaking the index alignment.
    data::PcgMeshData mesh;
    for (const auto& p : geometry.points())
        mesh.add_vertex({p.x, p.y, p.z});
    for (const auto& face : geometry.faces()) {
        if (face.size() < 3)
            continue;
        const int i0 = face[0];
        for (size_t i = 1; i + 1 < face.size(); ++i)
            mesh.add_triangle(i0, face[i], face[i + 1]);
    }

    if (method == BevelMethod::VertexPush) {
        // VertexPush only moves vertices; preserve geometry topology + groups.
        data::PcgMeshData result_mesh = bevel_mesh_vertex_push(mesh, amount, segments);
        data::PcgGeometry result = geometry;
        auto& pts = result.points_mut();
        for (size_t i = 0; i < pts.size() && i < result_mesh.vertices().size(); ++i) {
            pts[i] = {result_mesh.vertices()[i].x,
                      result_mesh.vertices()[i].y,
                      result_mesh.vertices()[i].z};
        }
        return result;
    }

    data::PcgGeometry out_geom;
    const data::PcgMeshData result_mesh = bevel::bevel_mesh_blender(
        mesh, amount, segments,
        bevel::BevelOffsetType(offset_type),
        clamp_overlap, angle_limit_deg, profile,
        bevel::BevelMiter(miter_outer),
        bevel::BevelMiter(miter_inner),
        bevel::BevelVMeshMethod(vmesh_method),
        is_cancel_requested,
        edge_selection,
        &geometry,
        &out_geom);

    if (!out_geom.points().empty()) {
        out_geom.detail() = geometry.detail();
        return out_geom;
    }

    // Fallback: fast paths (e.g. axis-aligned box) return PcgMeshData without
    // populating out_geometry. Convert the mesh result back to geometry.
    if (!result_mesh.vertices().empty()) {
        data::PcgGeometry fallback = data::geometry_from_mesh(result_mesh);
        fallback.detail() = geometry.detail();
        return fallback;
    }

    return geometry;
}

data::PcgGeometry transform_geometry(const data::PcgGeometry& geometry,
                                    double translate_x, double translate_y, double translate_z,
                                    double rotation_x_deg, double rotation_y_deg, double rotation_z_deg,
                                    double scale_x, double scale_y, double scale_z)
{
    data::PcgGeometry out = geometry;
    const double rx = rotation_x_deg * 3.14159265358979323846 / 180.0;
    const double ry = rotation_y_deg * 3.14159265358979323846 / 180.0;
    const double rz = rotation_z_deg * 3.14159265358979323846 / 180.0;

    const auto rot_x = [&](double x, double y, double z) {
        const double c = std::cos(rx), s = std::sin(rx);
        return data::PcgVec3{x, y * c - z * s, y * s + z * c};
    };
    const auto rot_y = [&](double x, double y, double z) {
        const double c = std::cos(ry), s = std::sin(ry);
        return data::PcgVec3{x * c + z * s, y, -x * s + z * c};
    };
    const auto rot_z = [&](double x, double y, double z) {
        const double c = std::cos(rz), s = std::sin(rz);
        return data::PcgVec3{x * c - y * s, x * s + y * c, z};
    };

    for (auto& p : out.points_mut()) {
        data::PcgVec3 v = rot_x(p.x, p.y, p.z);
        v = rot_y(v.x, v.y, v.z);
        v = rot_z(v.x, v.y, v.z);
        v.x *= scale_x;
        v.y *= scale_y;
        v.z *= scale_z;
        v.x += translate_x;
        v.y += translate_y;
        v.z += translate_z;
        p = v;
    }

    return out;
}

} // namespace pcg::internal::elements
