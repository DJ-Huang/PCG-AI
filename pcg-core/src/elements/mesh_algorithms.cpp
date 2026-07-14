#include "elements/mesh_algorithms.hpp"
#include "elements/bevel_blender.hpp"
#include "elements/element_utils.hpp"
#include "geometry/bmesh.hpp"

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

data::PcgGeometry create_box_geometry(double width, double height, double depth)
{
    const double hx = std::max(width, 0.0) * 0.5;
    const double hy = std::max(height, 0.0) * 0.5;
    const double hz = std::max(depth, 0.0) * 0.5;

    data::PcgGeometry geo;
    // Shared corners. Face loops are CCW when viewed from outside so fan
    // triangulation (i0,i,i+1) yields outward normals — same as add_quad after
    // its index swap. Do NOT copy add_quad's pre-swap CW vertex order here.
    geo.points_mut() = {
        {-hx, -hy, -hz}, // 0
        { hx, -hy, -hz}, // 1
        { hx,  hy, -hz}, // 2
        {-hx,  hy, -hz}, // 3
        {-hx, -hy,  hz}, // 4
        { hx, -hy,  hz}, // 5
        { hx,  hy,  hz}, // 6
        {-hx,  hy,  hz}, // 7
    };
    // +X, -X, +Y, -Y, +Z, -Z
    geo.faces_mut() = {
        {1, 2, 6, 5},
        {4, 7, 3, 0},
        {3, 7, 6, 2},
        {4, 0, 1, 5},
        {4, 5, 6, 7},
        {1, 0, 3, 2},
    };
    return geo;
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

// ── Welded mesh structure shared by all subdivision methods ─────────────────
struct WeldedMesh {
    std::vector<Vec3> positions;
    std::vector<int> triangles;       // flat (a,b,c) * nt
    std::vector<int> weld_remap;      // original vertex → welded index
    int nv = 0;
    int nt = 0;
};

WeldedMesh weld_mesh_for_subd(const data::PcgMeshData& mesh) {
    WeldedMesh w;
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();

    std::unordered_map<std::string, int> pos_to_idx;
    w.weld_remap.resize(verts.size(), -1);

    for (size_t i = 0; i < verts.size(); ++i) {
        const std::string key = position_key(to_vec3(verts[i]));
        auto it = pos_to_idx.find(key);
        if (it == pos_to_idx.end()) {
            const int idx = static_cast<int>(w.positions.size());
            w.positions.push_back(to_vec3(verts[i]));
            pos_to_idx[key] = idx;
            w.weld_remap[i] = idx;
        } else {
            w.weld_remap[i] = it->second;
        }
    }
    w.nv = static_cast<int>(w.positions.size());

    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const int a = w.weld_remap[tris[i]];
        const int b = w.weld_remap[tris[i + 1]];
        const int c = w.weld_remap[tris[i + 2]];
        if (a == b || b == c || a == c) continue;
        w.triangles.push_back(a);
        w.triangles.push_back(b);
        w.triangles.push_back(c);
    }
    w.nt = static_cast<int>(w.triangles.size() / 3);
    return w;
}

auto ek64 = [](int a, int b) -> int64_t {
    return static_cast<int64_t>(std::min(a, b)) * 1000000LL + std::max(a, b);
};

void propagate_attributes(const data::PcgMeshData& src, data::PcgMeshData& dst,
                           const WeldedMesh& w,
                           const std::vector<int>& extra_src_a,
                           const std::vector<int>& extra_src_b,
                           int pad_count = 0) {
    const int nv = w.nv;
    const int nextra = static_cast<int>(extra_src_a.size());
    const bool has_colors = src.has_colors();
    const bool has_uvs = src.has_uvs();
    const bool has_normals = src.has_normals();
    if (!has_colors && !has_uvs && !has_normals) return;

    std::vector<int> w2o(nv, -1);
    for (size_t i = 0; i < src.vertices().size(); ++i) {
        const int wi = w.weld_remap[i];
        if (w2o[wi] < 0) w2o[wi] = static_cast<int>(i);
    }

    const int total = nv + nextra + pad_count;

    if (has_colors) {
        std::vector<data::PcgColor> oc(static_cast<size_t>(total));
        for (int vi = 0; vi < nv; ++vi) {
            const int oi = w2o[vi];
            oc[static_cast<size_t>(vi)] = oi >= 0 ? src.colors()[static_cast<size_t>(oi)] : data::PcgColor{};
        }
        for (int i = 0; i < nextra; ++i) {
            const int oa = w2o[extra_src_a[static_cast<size_t>(i)]];
            const int ob = w2o[extra_src_b[static_cast<size_t>(i)]];
            const auto& ca = oa >= 0 ? src.colors()[static_cast<size_t>(oa)] : data::PcgColor{};
            const auto& cb = ob >= 0 ? src.colors()[static_cast<size_t>(ob)] : data::PcgColor{};
            oc[static_cast<size_t>(nv + i)] = {(ca.r + cb.r) * 0.5, (ca.g + cb.g) * 0.5,
                                                (ca.b + cb.b) * 0.5, (ca.a + cb.a) * 0.5};
        }
        dst.set_colors(std::move(oc));
    }

    if (has_uvs) {
        std::vector<data::PcgVec2> ou(static_cast<size_t>(total));
        for (int vi = 0; vi < nv; ++vi) {
            const int oi = w2o[vi];
            ou[static_cast<size_t>(vi)] = oi >= 0 ? src.uvs()[static_cast<size_t>(oi)] : data::PcgVec2{};
        }
        for (int i = 0; i < nextra; ++i) {
            const int oa = w2o[extra_src_a[static_cast<size_t>(i)]];
            const int ob = w2o[extra_src_b[static_cast<size_t>(i)]];
            const auto& ua = oa >= 0 ? src.uvs()[static_cast<size_t>(oa)] : data::PcgVec2{};
            const auto& ub = ob >= 0 ? src.uvs()[static_cast<size_t>(ob)] : data::PcgVec2{};
            ou[static_cast<size_t>(nv + i)] = {(ua.u + ub.u) * 0.5, (ua.v + ub.v) * 0.5};
        }
        dst.set_uvs(std::move(ou));
    }

    if (has_normals) {
        std::vector<data::PcgVertex> on(static_cast<size_t>(total));
        for (int vi = 0; vi < nv; ++vi) {
            const int oi = w2o[vi];
            on[static_cast<size_t>(vi)] = oi >= 0 ? src.normals()[static_cast<size_t>(oi)] : data::PcgVertex{};
        }
        for (int i = 0; i < nextra; ++i) {
            const int oa = w2o[extra_src_a[static_cast<size_t>(i)]];
            const int ob = w2o[extra_src_b[static_cast<size_t>(i)]];
            const auto& na = oa >= 0 ? src.normals()[static_cast<size_t>(oa)] : data::PcgVertex{};
            const auto& nb = ob >= 0 ? src.normals()[static_cast<size_t>(ob)] : data::PcgVertex{};
            on[static_cast<size_t>(nv + i)] = {(na.x + nb.x) * 0.5, (na.y + nb.y) * 0.5, (na.z + nb.z) * 0.5};
        }
        dst.set_normals(std::move(on));
    }
}

// ── Simple (linear) subdivision: flat 1-to-4, no vertex movement ──────────────
// Operates directly on original vertex indices — no welding, no attribute
// propagation. Matches the original pre-multi-method subdivide_mesh behavior.
data::PcgMeshData subdivide_simple(const data::PcgMeshData& mesh, int levels) {
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

// ── Loop subdivision (triangle meshes) ───────────────────────────────────────
data::PcgMeshData subdivide_loop(const data::PcgMeshData& mesh, int levels) {
    data::PcgMeshData current = mesh;
    levels = std::clamp(levels, 0, 4);
    constexpr double kPi = 3.14159265358979323846;

    for (int level = 0; level < levels; ++level) {
        const WeldedMesh w = weld_mesh_for_subd(current);
        if (w.nt == 0) break;

        // Build edge → opposite vertices and vertex → neighbor set
        std::unordered_map<int64_t, std::vector<int>> edge_opp;
        std::vector<std::unordered_set<int>> vneigh(static_cast<size_t>(w.nv));

        for (int ti = 0; ti < w.nt; ++ti) {
            const int a = w.triangles[ti * 3], b = w.triangles[ti * 3 + 1], c = w.triangles[ti * 3 + 2];
            edge_opp[ek64(a, b)].push_back(c);
            edge_opp[ek64(b, c)].push_back(a);
            edge_opp[ek64(c, a)].push_back(b);
            vneigh[a].insert(b); vneigh[b].insert(a);
            vneigh[b].insert(c); vneigh[c].insert(b);
            vneigh[c].insert(a); vneigh[a].insert(c);
        }

        auto is_bnd = [&](int a, int b) {
            const auto it = edge_opp.find(ek64(a, b));
            return it == edge_opp.end() || it->second.size() < 2;
        };

        // Odd vertices
        std::unordered_map<int64_t, int> odd_map;
        std::vector<Vec3> odd_pos;
        std::vector<int> odd_a, odd_b;

        auto get_odd = [&](int a, int b) -> int {
            const int64_t key = ek64(a, b);
            auto it = odd_map.find(key);
            if (it != odd_map.end()) return it->second;
            const auto& opps = edge_opp[key];
            Vec3 p;
            if (opps.size() >= 2)
                p = add(scale(add(w.positions[a], w.positions[b]), 3.0 / 8.0),
                        scale(add(w.positions[opps[0]], w.positions[opps[1]]), 1.0 / 8.0));
            else
                p = scale(add(w.positions[a], w.positions[b]), 0.5);
            const int idx = static_cast<int>(odd_pos.size());
            odd_pos.push_back(p);
            odd_a.push_back(a);
            odd_b.push_back(b);
            odd_map[key] = idx;
            return idx;
        };

        for (int ti = 0; ti < w.nt; ++ti) {
            const int a = w.triangles[ti * 3], b = w.triangles[ti * 3 + 1], c = w.triangles[ti * 3 + 2];
            get_odd(a, b); get_odd(b, c); get_odd(c, a);
        }

        // Even vertices
        std::vector<Vec3> even_pos(static_cast<size_t>(w.nv));
        for (int vi = 0; vi < w.nv; ++vi) {
            const Vec3& p = w.positions[vi];
            const int n = static_cast<int>(vneigh[static_cast<size_t>(vi)].size());
            if (n == 0) { even_pos[static_cast<size_t>(vi)] = p; continue; }

            std::vector<int> bn;
            for (int nb : vneigh[static_cast<size_t>(vi)])
                if (is_bnd(vi, nb)) bn.push_back(nb);

            if (bn.size() >= 2) {
                even_pos[static_cast<size_t>(vi)] = add(scale(p, 0.75),
                    scale(add(w.positions[bn[0]], w.positions[bn[1]]), 0.125));
            } else {
                double beta;
                if (n == 3) beta = 3.0 / 16.0;
                else {
                    const double c = std::cos(2.0 * kPi / n);
                    beta = (1.0 / n) * (5.0 / 8.0 - std::pow(3.0 / 8.0 + 0.25 * c, 2));
                }
                Vec3 sum = {0, 0, 0};
                for (int nb : vneigh[static_cast<size_t>(vi)])
                    sum = add(sum, w.positions[nb]);
                even_pos[static_cast<size_t>(vi)] = add(scale(p, 1.0 - n * beta), scale(sum, beta));
            }
        }

        // Build output
        data::PcgMeshData next;
        for (int vi = 0; vi < w.nv; ++vi)
            next.add_vertex(to_vertex(even_pos[static_cast<size_t>(vi)]));
        for (const auto& p : odd_pos)
            next.add_vertex(to_vertex(p));

        const int odd_base = w.nv;
        for (int ti = 0; ti < w.nt; ++ti) {
            const int a = w.triangles[ti * 3], b = w.triangles[ti * 3 + 1], c = w.triangles[ti * 3 + 2];
            const int ab = odd_base + odd_map[ek64(a, b)];
            const int bc = odd_base + odd_map[ek64(b, c)];
            const int ca = odd_base + odd_map[ek64(c, a)];
            next.add_triangle(a, ab, ca);
            next.add_triangle(ab, b, bc);
            next.add_triangle(ca, bc, c);
            next.add_triangle(ab, bc, ca);
        }
        current = std::move(next);
    }
    return current;
}

// ── Catmull-Clark subdivision (polygon faces, not triangle soup) ─────────────
// Triangle-CC leaves ~41° fake creases inside former quads after L1, which
// angle-based bevel then treats as hard edges (CC_L1 bad_edges). Run CC on
// coplanar-merged n-gons so a box stays 6 quads → 24 quads with only silhouette
// creases above sharpAngle.
data::PcgMeshData subdivide_catmull_clark(const data::PcgMeshData& mesh, int levels) {
    data::PcgMeshData current = mesh;
    levels = std::clamp(levels, 0, 4);

    for (int level = 0; level < levels; ++level) {
        geometry::BMeshBuildOptions opts;
        opts.merge_coplanar_angle_deg = 2.0;
        opts.sharp_angle_deg = 180.0; // not used for topology
        const geometry::BMesh bm = geometry::bmesh_from_mesh(current, opts);
        if (bm.faces.empty() || bm.verts.empty())
            break;

        const int nv = static_cast<int>(bm.verts.size());
        const int nf = static_cast<int>(bm.faces.size());

        auto to_local = [](const geometry::Vec3& v) -> Vec3 { return {v.x, v.y, v.z}; };

        std::vector<Vec3> positions(static_cast<size_t>(nv));
        for (int i = 0; i < nv; ++i)
            positions[static_cast<size_t>(i)] = to_local(bm.verts[static_cast<size_t>(i)]);

        // Incident faces / neighbors from polygon loops
        std::vector<std::vector<int>> vert_faces(static_cast<size_t>(nv));
        std::vector<std::unordered_set<int>> vneigh(static_cast<size_t>(nv));
        for (int fi = 0; fi < nf; ++fi) {
            const auto& loop = bm.faces[static_cast<size_t>(fi)].verts;
            const int n = static_cast<int>(loop.size());
            for (int i = 0; i < n; ++i) {
                const int a = loop[static_cast<size_t>(i)];
                const int b = loop[static_cast<size_t>((i + 1) % n)];
                vert_faces[static_cast<size_t>(a)].push_back(fi);
                vneigh[static_cast<size_t>(a)].insert(b);
                vneigh[static_cast<size_t>(b)].insert(a);
            }
        }

        auto is_bnd_edge = [&](int a, int b) {
            const auto it = bm.edges.find(geometry::edge_key(a, b));
            return it == bm.edges.end() || it->second.face1 < 0;
        };

        // 1. Face points
        std::vector<Vec3> face_pts(static_cast<size_t>(nf));
        for (int fi = 0; fi < nf; ++fi) {
            const auto& loop = bm.faces[static_cast<size_t>(fi)].verts;
            Vec3 sum{0, 0, 0};
            for (int vi : loop)
                sum = add(sum, positions[static_cast<size_t>(vi)]);
            face_pts[static_cast<size_t>(fi)] =
                scale(sum, 1.0 / static_cast<double>(loop.size()));
        }

        // 2. Edge points
        std::unordered_map<int64_t, int> edge_pt_idx;
        std::vector<Vec3> edge_pt_pos;
        edge_pt_pos.reserve(bm.edges.size());

        auto get_edge_pt = [&](int a, int b) -> int {
            const int64_t key = geometry::edge_key(a, b);
            const auto it = edge_pt_idx.find(key);
            if (it != edge_pt_idx.end())
                return it->second;

            const Vec3 mid = scale(add(positions[static_cast<size_t>(a)],
                                       positions[static_cast<size_t>(b)]), 0.5);
            Vec3 ep = mid;
            const auto eit = bm.edges.find(key);
            if (eit != bm.edges.end() && eit->second.face0 >= 0 && eit->second.face1 >= 0) {
                const Vec3 favg = scale(
                    add(face_pts[static_cast<size_t>(eit->second.face0)],
                        face_pts[static_cast<size_t>(eit->second.face1)]),
                    0.5);
                ep = scale(add(mid, favg), 0.5);
            }

            const int idx = static_cast<int>(edge_pt_pos.size());
            edge_pt_pos.push_back(ep);
            edge_pt_idx[key] = idx;
            return idx;
        };

        for (const auto& entry : bm.edges)
            get_edge_pt(entry.second.v0, entry.second.v1);

        // 3. Vertex points
        std::vector<Vec3> vert_pt(static_cast<size_t>(nv));
        for (int vi = 0; vi < nv; ++vi) {
            const Vec3& p = positions[static_cast<size_t>(vi)];
            const auto& vf = vert_faces[static_cast<size_t>(vi)];
            if (vf.empty()) {
                vert_pt[static_cast<size_t>(vi)] = p;
                continue;
            }

            std::vector<int> bn;
            for (int nb : vneigh[static_cast<size_t>(vi)])
                if (is_bnd_edge(vi, nb))
                    bn.push_back(nb);

            if (bn.size() >= 2) {
                vert_pt[static_cast<size_t>(vi)] = add(
                    scale(p, 0.75),
                    scale(add(positions[static_cast<size_t>(bn[0])],
                              positions[static_cast<size_t>(bn[1])]),
                          0.125));
            } else {
                const int nval = static_cast<int>(vneigh[static_cast<size_t>(vi)].size());
                if (nval == 0) {
                    vert_pt[static_cast<size_t>(vi)] = p;
                    continue;
                }
                Vec3 fsum{0, 0, 0};
                for (int fi : vf)
                    fsum = add(fsum, face_pts[static_cast<size_t>(fi)]);
                // Average unique incident face points (vf may list a face once per loop visit)
                const Vec3 Q = scale(fsum, 1.0 / static_cast<double>(vf.size()));

                Vec3 rsum{0, 0, 0};
                for (int nb : vneigh[static_cast<size_t>(vi)])
                    rsum = add(rsum, scale(add(p, positions[static_cast<size_t>(nb)]), 0.5));
                const Vec3 R = scale(rsum, 1.0 / nval);

                const double m1 = static_cast<double>(nval - 3) / nval;
                const double m2 = 1.0 / nval;
                const double m3 = 2.0 / nval;
                vert_pt[static_cast<size_t>(vi)] =
                    add(add(scale(p, m1), scale(Q, m2)), scale(R, m3));
            }
        }

        // 4. Emit: [0,nv) verts, [nv,nv+ne) edges, [nv+ne, ...) face points
        data::PcgMeshData next;
        for (int vi = 0; vi < nv; ++vi)
            next.add_vertex(to_vertex(vert_pt[static_cast<size_t>(vi)]));
        for (const auto& p : edge_pt_pos)
            next.add_vertex(to_vertex(p));
        const int ep_base = nv;
        const int fp_base = nv + static_cast<int>(edge_pt_pos.size());
        for (const auto& p : face_pts)
            next.add_vertex(to_vertex(p));

        // 5. Each n-gon → n quads (2 tris), CCW: (v, e_next, fp, e_prev)
        for (int fi = 0; fi < nf; ++fi) {
            const auto& loop = bm.faces[static_cast<size_t>(fi)].verts;
            const int n = static_cast<int>(loop.size());
            if (n < 3)
                continue;
            const int fp = fp_base + fi;
            for (int i = 0; i < n; ++i) {
                const int v = loop[static_cast<size_t>(i)];
                const int v_next = loop[static_cast<size_t>((i + 1) % n)];
                const int v_prev = loop[static_cast<size_t>((i + n - 1) % n)];
                const int e_next = ep_base + get_edge_pt(v, v_next);
                const int e_prev = ep_base + get_edge_pt(v_prev, v);
                next.add_triangle(v, e_next, fp);
                next.add_triangle(v, fp, e_prev);
            }
        }

        current = std::move(next);
    }
    return current;
}

// ── Dispatch ────────────────────────────────────────────────────────────────
data::PcgMeshData subdivide_mesh(const data::PcgMeshData& mesh, int levels, SubdivideMethod method)
{
    switch (method) {
    case SubdivideMethod::Simple:       return subdivide_simple(mesh, levels);
    case SubdivideMethod::Loop:         return subdivide_loop(mesh, levels);
    case SubdivideMethod::CatmullClark:
    default:                             return subdivide_catmull_clark(mesh, levels);
    }
}

data::PcgGeometry subdivide_geometry(const data::PcgGeometry& geometry, int levels, SubdivideMethod method)
{
    if (geometry.points().empty() || geometry.faces().empty())
        return geometry;

    data::PcgMeshData mesh;
    for (const auto& p : geometry.points())
        mesh.add_vertex({p.x, p.y, p.z});
    for (const auto& face : geometry.faces()) {
        if (face.size() < 3) continue;
        const int i0 = face[0];
        for (size_t i = 1; i + 1 < face.size(); ++i)
            mesh.add_triangle(i0, face[i], face[i + 1]);
    }

    const data::PcgMeshData result = subdivide_mesh(mesh, levels, method);
    data::PcgGeometry out = data::geometry_from_mesh(result);
    out.detail() = geometry.detail();
    return out;
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
    // share the same vertex indices.
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
