#include "geometry/spline_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pcg::internal::geometry {
namespace {

constexpr double kEpsilon = 1e-9;

int wrap_index(int index, int count, bool closed)
{
    if (count <= 0)
        return 0;
    if (!closed)
        return std::clamp(index, 0, count - 1);
    int wrapped = index % count;
    if (wrapped < 0)
        wrapped += count;
    return wrapped;
}

Vec3 project_onto_plane_perpendicular(const Vec3& v, const Vec3& axis)
{
    return sub(v, scale(axis, dot(v, axis)));
}

Vec3 compute_frame_normal(const Vec3& tangent, const Vec3& up_hint)
{
    Vec3 normal = project_onto_plane_perpendicular(up_hint, tangent);
    if (length(normal) <= kEpsilon) {
        const Vec3 fallback = std::abs(tangent.y) < 0.9 ? Vec3{0.0, 1.0, 0.0} : Vec3{1.0, 0.0, 0.0};
        normal = project_onto_plane_perpendicular(fallback, tangent);
    }
    return normalize(normal);
}

} // namespace

Vec3 add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 scale(const Vec3& v, double s) { return {v.x * s, v.y * s, v.z * s}; }
double dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

double length(const Vec3& v) { return std::sqrt(dot(v, v)); }

Vec3 normalize(const Vec3& v)
{
    const double len = length(v);
    if (len <= kEpsilon)
        return {0.0, 0.0, 0.0};
    return scale(v, 1.0 / len);
}

Vec3 catmull_rom(const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, double t)
{
    const double t2 = t * t;
    const double t3 = t2 * t;
    return {
        0.5 * ((2.0 * p1.x) + (-p0.x + p2.x) * t +
               (2.0 * p0.x - 5.0 * p1.x + 4.0 * p2.x - p3.x) * t2 +
               (-p0.x + 3.0 * p1.x - 3.0 * p2.x + p3.x) * t3),
        0.5 * ((2.0 * p1.y) + (-p0.y + p2.y) * t +
               (2.0 * p0.y - 5.0 * p1.y + 4.0 * p2.y - p3.y) * t2 +
               (-p0.y + 3.0 * p1.y - 3.0 * p2.y + p3.y) * t3),
        0.5 * ((2.0 * p1.z) + (-p0.z + p2.z) * t +
               (2.0 * p0.z - 5.0 * p1.z + 4.0 * p2.z - p3.z) * t2 +
               (-p0.z + 3.0 * p1.z - 3.0 * p2.z + p3.z) * t3),
    };
}

std::vector<Vec3> build_spline_polyline(const std::vector<Vec3>& control_points,
                                        const char* mode,
                                        bool closed,
                                        int subdivisions_per_segment)
{
    std::vector<Vec3> out;
    if (control_points.empty())
        return out;

    const int subdivisions = std::max(1, subdivisions_per_segment);

    if (std::strcmp(mode, "line") == 0 && control_points.size() >= 2) {
        const Vec3& a = control_points.front();
        const Vec3& b = control_points.back();
        for (int i = 0; i <= subdivisions; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(subdivisions);
            out.push_back(add(a, scale(sub(b, a), t)));
        }
        return out;
    }

    if (std::strcmp(mode, "polyline") == 0) {
        out = control_points;
        if (closed && !out.empty() && length(sub(out.front(), out.back())) > kEpsilon)
            out.push_back(out.front());
        return out;
    }

    const int count = static_cast<int>(control_points.size());
    if (count < 2)
        return control_points;

    const int segment_count = closed ? count : count - 1;
    for (int seg = 0; seg < segment_count; ++seg) {
        const Vec3 p0 = control_points[static_cast<size_t>(wrap_index(seg - 1, count, closed))];
        const Vec3 p1 = control_points[static_cast<size_t>(wrap_index(seg, count, closed))];
        const Vec3 p2 = control_points[static_cast<size_t>(wrap_index(seg + 1, count, closed))];
        const Vec3 p3 = control_points[static_cast<size_t>(wrap_index(seg + 2, count, closed))];

        const int start_i = (seg == 0) ? 0 : 1;
        for (int i = start_i; i <= subdivisions; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(subdivisions);
            out.push_back(catmull_rom(p0, p1, p2, p3, t));
        }
    }

    return out;
}

double polyline_length(const std::vector<Vec3>& polyline)
{
    if (polyline.size() < 2)
        return 0.0;

    double total = 0.0;
    for (size_t i = 1; i < polyline.size(); ++i)
        total += length(sub(polyline[i], polyline[i - 1]));
    return total;
}

std::vector<Vec3> resample_polyline_by_spacing(const std::vector<Vec3>& polyline, double spacing)
{
    std::vector<Vec3> out;
    if (polyline.empty())
        return out;
    if (polyline.size() == 1 || spacing <= kEpsilon) {
        out.push_back(polyline.front());
        return out;
    }

    const double total = polyline_length(polyline);
    if (total <= kEpsilon) {
        out.push_back(polyline.front());
        return out;
    }

    out.push_back(polyline.front());
    double traveled = 0.0;
    double next_sample = spacing;
    size_t seg = 0;
    double seg_start = 0.0;

    while (next_sample < total && seg + 1 < polyline.size()) {
        const double seg_len = length(sub(polyline[seg + 1], polyline[seg]));
        if (seg_len <= kEpsilon) {
            ++seg;
            continue;
        }

        while (next_sample <= seg_start + seg_len && next_sample < total) {
            const double local_t = (next_sample - seg_start) / seg_len;
            out.push_back(add(polyline[seg], scale(sub(polyline[seg + 1], polyline[seg]), local_t)));
            next_sample += spacing;
        }

        seg_start += seg_len;
        ++seg;
    }

    if (length(sub(out.back(), polyline.back())) > kEpsilon)
        out.push_back(polyline.back());

    traveled = polyline_length(out);
    (void)traveled;
    return out;
}

std::vector<Vec3> resample_polyline_by_count(const std::vector<Vec3>& polyline, int point_count)
{
    std::vector<Vec3> out;
    if (polyline.empty() || point_count <= 0)
        return out;
    if (point_count == 1) {
        out.push_back(polyline.front());
        return out;
    }

    const double total = polyline_length(polyline);
    if (total <= kEpsilon) {
        out.assign(static_cast<size_t>(point_count), polyline.front());
        return out;
    }

    std::vector<double> cumulative;
    cumulative.push_back(0.0);
    for (size_t i = 1; i < polyline.size(); ++i)
        cumulative.push_back(cumulative.back() + length(sub(polyline[i], polyline[i - 1])));

    for (int i = 0; i < point_count; ++i) {
        const double target = total * static_cast<double>(i) / static_cast<double>(point_count - 1);
        const auto upper = std::lower_bound(cumulative.begin(), cumulative.end(), target);
        if (upper == cumulative.begin()) {
            out.push_back(polyline.front());
            continue;
        }
        if (upper == cumulative.end()) {
            out.push_back(polyline.back());
            continue;
        }

        const size_t idx = static_cast<size_t>(upper - cumulative.begin());
        const double seg_start = cumulative[idx - 1];
        const double seg_len = cumulative[idx] - seg_start;
        const double local_t = seg_len <= kEpsilon ? 0.0 : (target - seg_start) / seg_len;
        out.push_back(add(polyline[idx - 1], scale(sub(polyline[idx], polyline[idx - 1]), local_t)));
    }

    return out;
}

std::vector<Frame3> build_frames(const std::vector<Vec3>& polyline, const Vec3& up_hint)
{
    std::vector<Frame3> frames;
    if (polyline.empty())
        return frames;

    frames.reserve(polyline.size());
    Vec3 prev_normal{0.0, 0.0, 0.0};

    for (size_t i = 0; i < polyline.size(); ++i) {
        Vec3 tangent;
        if (polyline.size() == 1)
            tangent = {0.0, 0.0, 1.0};
        else if (i == 0)
            tangent = normalize(sub(polyline[1], polyline[0]));
        else if (i + 1 == polyline.size())
            tangent = normalize(sub(polyline[i], polyline[i - 1]));
        else
            tangent = normalize(add(sub(polyline[i + 1], polyline[i]), sub(polyline[i], polyline[i - 1])));

        Vec3 normal = compute_frame_normal(tangent, up_hint);

        if (i > 0 && length(prev_normal) > kEpsilon) {
            Vec3 axis = cross(prev_normal, normal);
            if (length(axis) > kEpsilon) {
                axis = normalize(axis);
                const double angle = std::asin(std::clamp(length(cross(prev_normal, normal)), -1.0, 1.0));
                const Vec3 rotated = add(
                    scale(prev_normal, std::cos(angle)),
                    scale(cross(axis, prev_normal), std::sin(angle)));
                if (length(rotated) > kEpsilon)
                    normal = normalize(rotated);
            }
        }

        // Profile local +X maps to binormal; cross(up, tangent) matches path-right width.
        Vec3 binormal = normalize(cross(normal, tangent));
        if (length(binormal) <= kEpsilon)
            binormal = normalize(cross(Vec3{1.0, 0.0, 0.0}, tangent));

        frames.push_back(Frame3{polyline[i], tangent, normal, binormal});
        prev_normal = normal;
    }

    return frames;
}

Vec3 transform_local_to_world(const Frame3& frame, const Vec3& local)
{
    return add(
        add(add(frame.origin, scale(frame.binormal, local.x)), scale(frame.normal, local.y)),
        scale(frame.tangent, local.z));
}

} // namespace pcg::internal::geometry
