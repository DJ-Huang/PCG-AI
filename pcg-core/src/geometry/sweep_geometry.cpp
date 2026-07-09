#include "geometry/sweep_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace pcg::internal::geometry {
namespace {

constexpr double kEpsilon = 1e-9;

Vec3 mesh_centroid(const data::PcgMeshData& mesh)
{
    if (mesh.vertices().empty())
        return {};

    Vec3 sum{};
    for (const auto& v : mesh.vertices())
        sum = add(sum, {v.x, v.y, v.z});
    return scale(sum, 1.0 / static_cast<double>(mesh.vertices().size()));
}

int dominant_thickness_axis(const std::vector<Vec3>& verts)
{
    if (verts.empty())
        return 2;

    Vec3 min_v = verts.front();
    Vec3 max_v = verts.front();
    for (const auto& v : verts) {
        min_v.x = std::min(min_v.x, v.x);
        min_v.y = std::min(min_v.y, v.y);
        min_v.z = std::min(min_v.z, v.z);
        max_v.x = std::max(max_v.x, v.x);
        max_v.y = std::max(max_v.y, v.y);
        max_v.z = std::max(max_v.z, v.z);
    }

    const double extents[3] = {max_v.x - min_v.x, max_v.y - min_v.y, max_v.z - min_v.z};
    int axis = 0;
    for (int i = 1; i < 3; ++i) {
        if (extents[i] < extents[axis])
            axis = i;
    }
    return axis;
}

Vec3 axis_vector(int axis)
{
    if (axis == 0)
        return {1.0, 0.0, 0.0};
    if (axis == 1)
        return {0.0, 1.0, 0.0};
    return {0.0, 0.0, 1.0};
}

bool triangle_normal_matches_thickness_axis(const Vec3& a,
                                            const Vec3& b,
                                            const Vec3& c,
                                            int thickness_axis,
                                            double min_alignment)
{
    const Vec3 n = normalize(cross(sub(b, a), sub(c, a)));
    if (length(n) <= kEpsilon)
        return false;
    const Vec3 axis = axis_vector(thickness_axis);
    return std::abs(dot(n, axis)) >= min_alignment;
}

Vec3 project_to_plane(const Vec3& v, const std::string& plane, int thickness_axis)
{
    Vec3 out = v;
    if (plane == "xy" || (plane == "auto" && thickness_axis == 2))
        out.z = 0.0;
    else if (plane == "xz" || (plane == "auto" && thickness_axis == 1))
        out.y = 0.0;
    else if (plane == "yz" || (plane == "auto" && thickness_axis == 0))
        out.x = 0.0;
    return out;
}

int64_t weld_key(const Vec3& v, double epsilon)
{
    const double inv = 1.0 / std::max(epsilon, 1e-12);
    const int64_t qx = static_cast<int64_t>(std::llround(v.x * inv));
    const int64_t qy = static_cast<int64_t>(std::llround(v.y * inv));
    const int64_t qz = static_cast<int64_t>(std::llround(v.z * inv));
    return (qx << 42) ^ (qy << 21) ^ qz;
}

std::vector<std::pair<int, int>> extract_boundary_edges(const std::vector<int>& triangles, int vertex_count)
{
    std::unordered_map<uint64_t, int> directed_use;
    directed_use.reserve(triangles.size());

    const auto edge_key = [](int a, int b) -> uint64_t {
        return (static_cast<uint64_t>(static_cast<uint32_t>(a)) << 32) |
               static_cast<uint32_t>(b);
    };

    for (size_t i = 0; i + 2 < triangles.size(); i += 3) {
        const int a = triangles[i];
        const int b = triangles[i + 1];
        const int c = triangles[i + 2];
        if (a < 0 || b < 0 || c < 0 || a >= vertex_count || b >= vertex_count || c >= vertex_count)
            continue;
        ++directed_use[edge_key(a, b)];
        ++directed_use[edge_key(b, c)];
        ++directed_use[edge_key(c, a)];
    }

    std::vector<std::pair<int, int>> boundary;
    boundary.reserve(directed_use.size());
    for (const auto& [key, count] : directed_use) {
        if (count != 1)
            continue;
        const int a = static_cast<int>(key >> 32);
        const int b = static_cast<int>(key & 0xffffffffu);
        if (directed_use[edge_key(b, a)] == 0)
            boundary.push_back({a, b});
    }
    return boundary;
}

Frame3 apply_twist_and_scale(const Frame3& frame, double twist, double scale_xy)
{
    const double c = std::cos(twist);
    const double s = std::sin(twist);
    const Vec3 binormal = normalize(add(scale(frame.binormal, c), scale(frame.normal, s)));
    const Vec3 normal = normalize(add(scale(frame.binormal, -s), scale(frame.normal, c)));
    Frame3 out = frame;
    out.binormal = binormal;
    out.normal = normal;
    out.tangent = normalize(frame.tangent);
    (void)scale_xy;
    return out;
}

Vec3 transform_profile_vertex(const Frame3& frame, const Vec3& local, double scale_xy)
{
    const Vec3 scaled{local.x * scale_xy, local.y * scale_xy, local.z};
    return transform_local_to_world(frame, scaled);
}

void append_side_quad(data::PcgMeshData& mesh, int a0, int b0, int b1, int a1)
{
    mesh.add_triangle(a0, b0, b1);
    mesh.add_triangle(a0, b1, a1);
}

} // namespace

CrossSectionMesh prepare_cross_section(const data::PcgMeshData& mesh, const CrossSectionOptions& options)
{
    CrossSectionMesh section;
    if (mesh.vertices().empty())
        return section;

    const Vec3 center = options.center ? mesh_centroid(mesh) : Vec3{};
    std::vector<Vec3> local;
    local.reserve(mesh.vertices().size());
    for (const auto& v : mesh.vertices())
        local.push_back(sub({v.x, v.y, v.z}, center));

    const int thickness_axis = dominant_thickness_axis(local);

    std::unordered_map<int64_t, int> welded;
    welded.reserve(local.size());
    std::vector<int> remap(local.size(), -1);
    for (size_t i = 0; i < local.size(); ++i) {
        const int64_t key = weld_key(local[i], options.weld_epsilon);
        const auto it = welded.find(key);
        if (it == welded.end()) {
            const int index = static_cast<int>(section.vertices.size());
            welded.emplace(key, index);
            remap[i] = index;
            section.vertices.push_back(project_to_plane(local[i], options.plane, thickness_axis));
        } else {
            remap[i] = it->second;
        }
    }

    std::vector<uint64_t> triangle_keys;
    triangle_keys.reserve(mesh.triangles().size() / 3);

    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const size_t ia = static_cast<size_t>(mesh.triangles()[i]);
        const size_t ib = static_cast<size_t>(mesh.triangles()[i + 1]);
        const size_t ic = static_cast<size_t>(mesh.triangles()[i + 2]);
        if (ia >= local.size() || ib >= local.size() || ic >= local.size())
            continue;

        if (!triangle_normal_matches_thickness_axis(local[ia], local[ib], local[ic], thickness_axis, 0.85))
            continue;

        const int a = remap[ia];
        const int b = remap[ib];
        const int c = remap[ic];
        if (a < 0 || b < 0 || c < 0 || a == b || b == c || c == a)
            continue;

        int sorted[3] = {a, b, c};
        if (sorted[0] > sorted[1])
            std::swap(sorted[0], sorted[1]);
        if (sorted[1] > sorted[2])
            std::swap(sorted[1], sorted[2]);
        if (sorted[0] > sorted[1])
            std::swap(sorted[0], sorted[1]);
        const uint64_t tri_key = (static_cast<uint64_t>(sorted[0]) << 42) |
                                 (static_cast<uint64_t>(sorted[1]) << 21) |
                                 static_cast<uint64_t>(sorted[2]);
        if (std::find(triangle_keys.begin(), triangle_keys.end(), tri_key) != triangle_keys.end())
            continue;
        triangle_keys.push_back(tri_key);

        section.triangles.push_back(a);
        section.triangles.push_back(b);
        section.triangles.push_back(c);
    }

    return section;
}

data::PcgMeshData sweep_cross_section(const CrossSectionMesh& section,
                                      const std::vector<Frame3>& frames,
                                      const SweepAlongFramesOptions& options)
{
    data::PcgMeshData result;
    if (section.vertices.empty() || frames.empty())
        return result;

    const size_t profile_count = section.vertices.size();
    const size_t frame_count = frames.size();
    const bool has_topology = section.triangles.size() >= 3;

    std::vector<std::vector<int>> rings(frame_count, std::vector<int>(profile_count, -1));

    for (size_t fi = 0; fi < frame_count; ++fi) {
        const double t = frame_count <= 1 ? 0.0 : static_cast<double>(fi) / static_cast<double>(frame_count - 1);
        const double roll = options.profile_roll_radians + options.twist_radians * t;
        const double scale_xy = options.scale_start + (options.scale_end - options.scale_start) * t;
        const Frame3 frame = apply_twist_and_scale(frames[fi], roll, scale_xy);

        for (size_t vi = 0; vi < profile_count; ++vi) {
            rings[fi][vi] = static_cast<int>(result.vertices().size());
            const Vec3 world = transform_profile_vertex(frame, section.vertices[vi], scale_xy);
            result.add_vertex({world.x, world.y, world.z});
        }
    }

    if (has_topology) {
        const auto boundary = extract_boundary_edges(section.triangles, static_cast<int>(profile_count));
        for (size_t fi = 0; fi + 1 < frame_count; ++fi) {
            for (const auto& edge : boundary) {
                const int a0 = rings[fi][static_cast<size_t>(edge.first)];
                const int b0 = rings[fi][static_cast<size_t>(edge.second)];
                const int a1 = rings[fi + 1][static_cast<size_t>(edge.first)];
                const int b1 = rings[fi + 1][static_cast<size_t>(edge.second)];
                append_side_quad(result, a0, b0, b1, a1);
            }
        }

        if (options.cap_start) {
            for (size_t i = 0; i + 2 < section.triangles.size(); i += 3) {
                const int a = rings[0][static_cast<size_t>(section.triangles[i])];
                const int b = rings[0][static_cast<size_t>(section.triangles[i + 1])];
                const int c = rings[0][static_cast<size_t>(section.triangles[i + 2])];
                result.add_triangle(c, b, a);
            }
        }

        if (options.cap_end && frame_count > 0) {
            const size_t last = frame_count - 1;
            for (size_t i = 0; i + 2 < section.triangles.size(); i += 3) {
                const int a = rings[last][static_cast<size_t>(section.triangles[i])];
                const int b = rings[last][static_cast<size_t>(section.triangles[i + 1])];
                const int c = rings[last][static_cast<size_t>(section.triangles[i + 2])];
                result.add_triangle(a, b, c);
            }
        }
        return result;
    }

    // Ribbon fallback: connect vertices as a closed polygon ring.
    for (size_t fi = 0; fi + 1 < frame_count; ++fi) {
        for (size_t vi = 0; vi < profile_count; ++vi) {
            const size_t vin = (vi + 1) % profile_count;
            const int a0 = rings[fi][vi];
            const int b0 = rings[fi][vin];
            const int a1 = rings[fi + 1][vi];
            const int b1 = rings[fi + 1][vin];
            append_side_quad(result, a0, b0, b1, a1);
        }
    }

    if (options.cap_start && profile_count >= 3) {
        Vec3 center{};
        for (size_t vi = 0; vi < profile_count; ++vi) {
            const auto& v = result.vertices()[static_cast<size_t>(rings[0][vi])];
            center = add(center, {v.x, v.y, v.z});
        }
        center = scale(center, 1.0 / static_cast<double>(profile_count));
        const int center_idx = static_cast<int>(result.vertices().size());
        result.add_vertex({center.x, center.y, center.z});
        for (size_t vi = 0; vi + 1 < profile_count; ++vi)
            result.add_triangle(center_idx, rings[0][vi + 1], rings[0][vi]);
    }

    if (options.cap_end && profile_count >= 3 && frame_count > 0) {
        const size_t last = frame_count - 1;
        Vec3 center{};
        for (size_t vi = 0; vi < profile_count; ++vi) {
            const auto& v = result.vertices()[static_cast<size_t>(rings[last][vi])];
            center = add(center, {v.x, v.y, v.z});
        }
        center = scale(center, 1.0 / static_cast<double>(profile_count));
        const int center_idx = static_cast<int>(result.vertices().size());
        result.add_vertex({center.x, center.y, center.z});
        for (size_t vi = 0; vi + 1 < profile_count; ++vi)
            result.add_triangle(center_idx, rings[last][vi], rings[last][vi + 1]);
    }

    return result;
}

CurveProfile prepare_curve_profile(const std::vector<Vec3>& points,
                                   bool closed_hint,
                                   bool center,
                                   const std::string& plane)
{
    CurveProfile profile;
    if (points.empty())
        return profile;

    profile.closed = closed_hint;
    profile.points = points;

    if (center) {
        Vec3 centroid{};
        for (const auto& p : profile.points)
            centroid = add(centroid, p);
        centroid = scale(centroid, 1.0 / static_cast<double>(profile.points.size()));
        for (auto& p : profile.points)
            p = sub(p, centroid);
    }

    const int thickness_axis = dominant_thickness_axis(profile.points);
    for (auto& p : profile.points)
        p = project_to_plane(p, plane, thickness_axis);

    if (profile.points.size() >= 2 && profile.closed) {
        const Vec3& first = profile.points.front();
        const Vec3& last = profile.points.back();
        if (length(sub(first, last)) <= 1e-4)
            profile.points.pop_back();
    }

    return profile;
}

data::PcgMeshData sweep_curve_profile(const CurveProfile& profile,
                                      const std::vector<Frame3>& frames,
                                      const SweepAlongFramesOptions& options)
{
    data::PcgMeshData result;
    if (profile.points.size() < 2 || frames.empty())
        return result;

    const size_t profile_count = profile.points.size();
    const size_t frame_count = frames.size();

    std::vector<std::vector<int>> rings(frame_count, std::vector<int>(profile_count, -1));

    for (size_t fi = 0; fi < frame_count; ++fi) {
        const double t = frame_count <= 1 ? 0.0 : static_cast<double>(fi) / static_cast<double>(frame_count - 1);
        const double roll = options.profile_roll_radians + options.twist_radians * t;
        const double scale_xy = options.scale_start + (options.scale_end - options.scale_start) * t;
        const Frame3 frame = apply_twist_and_scale(frames[fi], roll, scale_xy);

        for (size_t vi = 0; vi < profile_count; ++vi) {
            rings[fi][vi] = static_cast<int>(result.vertices().size());
            const Vec3 world = transform_profile_vertex(frame, profile.points[vi], scale_xy);
            result.add_vertex({world.x, world.y, world.z});
        }
    }

    const size_t edge_count = profile.closed ? profile_count : profile_count - 1;
    for (size_t fi = 0; fi + 1 < frame_count; ++fi) {
        for (size_t ei = 0; ei < edge_count; ++ei) {
            const size_t nj = (ei + 1) % profile_count;
            const int a0 = rings[fi][ei];
            const int b0 = rings[fi][nj];
            const int a1 = rings[fi + 1][ei];
            const int b1 = rings[fi + 1][nj];
            append_side_quad(result, a0, b0, b1, a1);
        }
    }

    const bool forms_tube = profile.closed || options.backbone_closed;
    if (!forms_tube || options.backbone_closed)
        return result;

    if (!profile.closed)
        return result;

    const auto append_profile_cap = [&](const std::vector<int>& ring, bool flip) {
        if (ring.size() < 3)
            return;

        Vec3 center{};
        for (int idx : ring) {
            const auto& v = result.vertices()[static_cast<size_t>(idx)];
            center = add(center, {v.x, v.y, v.z});
        }
        center = scale(center, 1.0 / static_cast<double>(ring.size()));
        const int center_idx = static_cast<int>(result.vertices().size());
        result.add_vertex({center.x, center.y, center.z});

        for (size_t vi = 0; vi < ring.size(); ++vi) {
            const size_t vj = (vi + 1) % ring.size();
            if (flip)
                result.add_triangle(center_idx, ring[vj], ring[vi]);
            else
                result.add_triangle(center_idx, ring[vi], ring[vj]);
        }
    };

    if (options.cap_start)
        append_profile_cap(rings.front(), true);
    if (options.cap_end && frame_count > 0)
        append_profile_cap(rings.back(), false);

    return result;
}

} // namespace pcg::internal::geometry
