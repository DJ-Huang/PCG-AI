#include "elements/uv_algorithms.hpp"

#include <algorithm>
#include <cmath>

namespace pcg::internal::elements {
namespace {

const double kEps = 1e-8;

double get_coord(const data::PcgVertex& v, int axis)
{
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}

void aabb_range(const std::vector<data::PcgVertex>& verts, int axis_a, int axis_b,
                double& min_a, double& min_b, double& ext_a, double& ext_b)
{
    double max_a = get_coord(verts[0], axis_a);
    double max_b = get_coord(verts[0], axis_b);
    min_a = max_a;
    min_b = max_b;
    for (const auto& v : verts) {
        min_a = std::min(min_a, get_coord(v, axis_a));
        max_a = std::max(max_a, get_coord(v, axis_a));
        min_b = std::min(min_b, get_coord(v, axis_b));
        max_b = std::max(max_b, get_coord(v, axis_b));
    }
    ext_a = max_a - min_a;
    ext_b = max_b - min_b;
}

} // namespace

void generate_uv(data::PcgMeshData& mesh,
                 const std::string& projection,
                 const std::string& axis,
                 double scale_u, double scale_v,
                 double offset_u, double offset_v)
{
    const int vc = static_cast<int>(mesh.vertices().size());
    if (vc == 0)
        return;

    std::vector<data::PcgVec2> uvs(vc);

    if (projection == "planar") {
        int ua = 0, ub = 1;
        if (axis == "x")      { ua = 1; ub = 2; }
        else if (axis == "y") { ua = 0; ub = 2; }
        else                  { ua = 0; ub = 1; }

        const auto& verts = mesh.vertices();
        double min_a, min_b, ext_a, ext_b;
        aabb_range(verts, ua, ub, min_a, min_b, ext_a, ext_b);
        for (int i = 0; i < vc; ++i) {
            double u = ext_a > kEps ? (get_coord(verts[i], ua) - min_a) / ext_a : 0.0;
            double v = ext_b > kEps ? (get_coord(verts[i], ub) - min_b) / ext_b : 0.0;
            uvs[i] = {u * scale_u + offset_u, v * scale_v + offset_v};
        }
    } else if (projection == "cylindrical") {
        int axis_idx = 1;
        if (axis == "x")      axis_idx = 0;
        else if (axis == "y") axis_idx = 1;
        else                  axis_idx = 2;

        int ua = (axis_idx + 1) % 3;
        int ub = (axis_idx + 2) % 3;

        const auto& verts = mesh.vertices();
        double min_h, min_b, ext_h, ext_b;
        aabb_range(verts, axis_idx, 0, min_h, min_b, ext_h, ext_b);
        for (int i = 0; i < vc; ++i) {
            double dx = get_coord(verts[i], ua);
            double dz = get_coord(verts[i], ub);
            double u = std::atan2(dz, dx) / (2.0 * M_PI) + 0.5;
            double v = ext_h > kEps ? (get_coord(verts[i], axis_idx) - min_h) / ext_h : 0.0;
            uvs[i] = {u * scale_u + offset_u, v * scale_v + offset_v};
        }
    } else if (projection == "spherical") {
        int axis_idx = 1;
        if (axis == "x")      axis_idx = 0;
        else if (axis == "y") axis_idx = 1;
        else                  axis_idx = 2;

        int ua = (axis_idx + 1) % 3;
        int ub = (axis_idx + 2) % 3;

        const auto& verts = mesh.vertices();
        double cx = 0, cy = 0, cz = 0;
        for (const auto& v : verts) { cx += v.x; cy += v.y; cz += v.z; }
        cx /= vc; cy /= vc; cz /= vc;

        for (int i = 0; i < vc; ++i) {
            double dx = verts[i].x - cx;
            double dy = verts[i].y - cy;
            double dz = verts[i].z - cz;
            double len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len < kEps) {
                uvs[i] = {0.0, 0.0};
                continue;
            }
            double a2 = get_coord(verts[i], ua) - get_coord({cx, cy, cz}, ua);
            double a3 = get_coord(verts[i], ub) - get_coord({cx, cy, cz}, ub);
            double ax = get_coord(verts[i], axis_idx) - get_coord({cx, cy, cz}, axis_idx);
            double u = std::atan2(a3, a2) / (2.0 * M_PI) + 0.5;
            double v = (ax / len) * 0.5 + 0.5;
            if (std::isnan(u) || std::isinf(u)) u = 0.0;
            if (std::isnan(v) || std::isinf(v)) v = 0.0;
            uvs[i] = {u * scale_u + offset_u, v * scale_v + offset_v};
        }
    } else {
        // Unsupported projections (e.g. legacy "box") must not silently write constant UVs.
        // Callers should reject unknown projections before invoking this helper.
        return;
    }

    mesh.set_uvs(uvs);
}

void project_texture_uv(data::PcgMeshData& mesh,
                        const std::string& direction,
                        double scale_u, double scale_v,
                        double offset_u, double offset_v,
                        double repeat_x, double repeat_y)
{
    const int vc = static_cast<int>(mesh.vertices().size());
    if (vc == 0)
        return;

    int ua = 0, ub = 1;
    if (direction == "x")      { ua = 1; ub = 2; }
    else if (direction == "y") { ua = 0; ub = 2; }
    else                       { ua = 0; ub = 1; }

    const auto& verts = mesh.vertices();

    double min_a = get_coord(verts[0], ua);
    double max_a = min_a;
    double min_b = get_coord(verts[0], ub);
    double max_b = min_b;
    for (const auto& v : verts) {
        min_a = std::min(min_a, get_coord(v, ua));
        max_a = std::max(max_a, get_coord(v, ua));
        min_b = std::min(min_b, get_coord(v, ub));
        max_b = std::max(max_b, get_coord(v, ub));
    }
    const double ext_a = max_a - min_a;
    const double ext_b = max_b - min_b;

    std::vector<data::PcgVec2> uvs(vc);
    const double eff_u = scale_u * repeat_x;
    const double eff_v = scale_v * repeat_y;
    for (int i = 0; i < vc; ++i) {
        double u = ext_a > kEps ? (get_coord(verts[i], ua) - min_a) / ext_a : 0.0;
        double v = ext_b > kEps ? (get_coord(verts[i], ub) - min_b) / ext_b : 0.0;
        uvs[i] = {u * eff_u + offset_u, v * eff_v + offset_v};
    }

    mesh.set_uvs(uvs);
}

// ── Geometry-native UV generation ──────────────────────────
// These operate on PcgGeometry points (double precision PcgVec3) directly,
// avoiding the geometry→mesh→geometry round-trip that triangulates n-gon faces.

namespace {

double get_coord_v3(const data::PcgVec3& v, int axis)
{
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}

void aabb_range_v3(const std::vector<data::PcgVec3>& pts, int axis_a, int axis_b,
                   double& min_a, double& min_b, double& ext_a, double& ext_b)
{
    double max_a = get_coord_v3(pts[0], axis_a);
    double max_b = get_coord_v3(pts[0], axis_b);
    min_a = max_a;
    min_b = max_b;
    for (const auto& p : pts) {
        min_a = std::min(min_a, get_coord_v3(p, axis_a));
        max_a = std::max(max_a, get_coord_v3(p, axis_a));
        min_b = std::min(min_b, get_coord_v3(p, axis_b));
        max_b = std::max(max_b, get_coord_v3(p, axis_b));
    }
    ext_a = max_a - min_a;
    ext_b = max_b - min_b;
}

} // namespace

std::vector<data::PcgVec2> generate_uv_geometry(const data::PcgGeometry& geometry,
                                                 const std::string& projection,
                                                 const std::string& axis,
                                                 double scale_u, double scale_v,
                                                 double offset_u, double offset_v)
{
    const auto& pts = geometry.points();
    const int vc = static_cast<int>(pts.size());
    if (vc == 0)
        return {};

    std::vector<data::PcgVec2> uvs(vc);

    if (projection == "planar") {
        int ua = 0, ub = 1;
        if (axis == "x")      { ua = 1; ub = 2; }
        else if (axis == "y") { ua = 0; ub = 2; }
        else                  { ua = 0; ub = 1; }

        double min_a, min_b, ext_a, ext_b;
        aabb_range_v3(pts, ua, ub, min_a, min_b, ext_a, ext_b);
        for (int i = 0; i < vc; ++i) {
            double u = ext_a > kEps ? (get_coord_v3(pts[static_cast<size_t>(i)], ua) - min_a) / ext_a : 0.0;
            double v = ext_b > kEps ? (get_coord_v3(pts[static_cast<size_t>(i)], ub) - min_b) / ext_b : 0.0;
            uvs[static_cast<size_t>(i)] = {u * scale_u + offset_u, v * scale_v + offset_v};
        }
    } else if (projection == "cylindrical") {
        int axis_idx = 1;
        if (axis == "x")      axis_idx = 0;
        else if (axis == "y") axis_idx = 1;
        else                  axis_idx = 2;

        int ua = (axis_idx + 1) % 3;
        int ub = (axis_idx + 2) % 3;

        double min_h, min_b, ext_h, ext_b;
        aabb_range_v3(pts, axis_idx, 0, min_h, min_b, ext_h, ext_b);
        for (int i = 0; i < vc; ++i) {
            double dx = get_coord_v3(pts[static_cast<size_t>(i)], ua);
            double dz = get_coord_v3(pts[static_cast<size_t>(i)], ub);
            double u = std::atan2(dz, dx) / (2.0 * M_PI) + 0.5;
            double v = ext_h > kEps ? (get_coord_v3(pts[static_cast<size_t>(i)], axis_idx) - min_h) / ext_h : 0.0;
            uvs[static_cast<size_t>(i)] = {u * scale_u + offset_u, v * scale_v + offset_v};
        }
    } else if (projection == "spherical") {
        int axis_idx = 1;
        if (axis == "x")      axis_idx = 0;
        else if (axis == "y") axis_idx = 1;
        else                  axis_idx = 2;

        int ua = (axis_idx + 1) % 3;
        int ub = (axis_idx + 2) % 3;

        double cx = 0, cy = 0, cz = 0;
        for (const auto& p : pts) { cx += p.x; cy += p.y; cz += p.z; }
        cx /= vc; cy /= vc; cz /= vc;

        for (int i = 0; i < vc; ++i) {
            double dx = pts[static_cast<size_t>(i)].x - cx;
            double dy = pts[static_cast<size_t>(i)].y - cy;
            double dz = pts[static_cast<size_t>(i)].z - cz;
            double len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len < kEps) {
                uvs[static_cast<size_t>(i)] = {0.0, 0.0};
                continue;
            }
            double a2 = get_coord_v3(pts[static_cast<size_t>(i)], ua) - get_coord_v3({cx, cy, cz}, ua);
            double a3 = get_coord_v3(pts[static_cast<size_t>(i)], ub) - get_coord_v3({cx, cy, cz}, ub);
            double ax = get_coord_v3(pts[static_cast<size_t>(i)], axis_idx) - get_coord_v3({cx, cy, cz}, axis_idx);
            double u = std::atan2(a3, a2) / (2.0 * M_PI) + 0.5;
            double v = (ax / len) * 0.5 + 0.5;
            if (std::isnan(u) || std::isinf(u)) u = 0.0;
            if (std::isnan(v) || std::isinf(v)) v = 0.0;
            uvs[static_cast<size_t>(i)] = {u * scale_u + offset_u, v * scale_v + offset_v};
        }
    } else {
        // Unsupported — return empty so callers do not treat constant offset as a valid UV map.
        return {};
    }

    return uvs;
}

std::vector<data::PcgVec2> project_texture_uv_geometry(const data::PcgGeometry& geometry,
                                                        const std::string& direction,
                                                        double scale_u, double scale_v,
                                                        double offset_u, double offset_v,
                                                        double repeat_x, double repeat_y)
{
    const auto& pts = geometry.points();
    const int vc = static_cast<int>(pts.size());
    if (vc == 0)
        return {};

    int ua = 0, ub = 1;
    if (direction == "x")      { ua = 1; ub = 2; }
    else if (direction == "y") { ua = 0; ub = 2; }
    else                       { ua = 0; ub = 1; }

    double min_a = get_coord_v3(pts[0], ua);
    double max_a = min_a;
    double min_b = get_coord_v3(pts[0], ub);
    double max_b = min_b;
    for (const auto& p : pts) {
        min_a = std::min(min_a, get_coord_v3(p, ua));
        max_a = std::max(max_a, get_coord_v3(p, ua));
        min_b = std::min(min_b, get_coord_v3(p, ub));
        max_b = std::max(max_b, get_coord_v3(p, ub));
    }
    const double ext_a = max_a - min_a;
    const double ext_b = max_b - min_b;

    std::vector<data::PcgVec2> uvs(vc);
    const double eff_u = scale_u * repeat_x;
    const double eff_v = scale_v * repeat_y;
    for (int i = 0; i < vc; ++i) {
        double u = ext_a > kEps ? (get_coord_v3(pts[static_cast<size_t>(i)], ua) - min_a) / ext_a : 0.0;
        double v = ext_b > kEps ? (get_coord_v3(pts[static_cast<size_t>(i)], ub) - min_b) / ext_b : 0.0;
        uvs[static_cast<size_t>(i)] = {u * eff_u + offset_u, v * eff_v + offset_v};
    }

    return uvs;
}

} // namespace pcg::internal::elements
