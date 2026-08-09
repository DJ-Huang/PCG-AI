#include "elements/spline_algorithms.hpp"

#include "data/pcg_geometry.hpp"
#include "elements/element_utils.hpp"
#include "geometry/spline_geometry.hpp"
#include "geometry/sweep_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace pcg::internal::elements {
namespace {

geometry::Vec3 to_vec3(const data::PcgSplinePoint& p) { return {p.x, p.y, p.z}; }
geometry::Vec3 to_vec3(const data::PcgVertex& v) { return {v.x, v.y, v.z}; }
data::PcgVertex to_vertex(const geometry::Vec3& v) { return {v.x, v.y, v.z}; }

std::vector<geometry::Vec3> collect_control_points(const CreateSplineOptions& options)
{
    std::vector<geometry::Vec3> points = options.control_points;
    if (!points.empty())
        return points;

    points.push_back({options.start_x, options.start_y, options.start_z});
    points.push_back({options.end_x, options.end_y, options.end_z});
    return points;
}

data::PcgMeshData make_box_profile(double width, double height)
{
    data::PcgMeshData profile;
    const double hw = width * 0.5;
    const double hh = height * 0.5;
    profile.add_vertex({-hw, -hh, 0.0});
    profile.add_vertex({hw, -hh, 0.0});
    profile.add_vertex({hw, hh, 0.0});
    profile.add_vertex({-hw, hh, 0.0});
    profile.add_triangle(0, 1, 2);
    profile.add_triangle(0, 2, 3);
    return profile;
}

geometry::CurveProfile make_builtin_curve_profile(const SweepAlongSplineOptions& options)
{
    geometry::CurveProfile profile;
    const int columns = std::max(3, options.columns);

    if (options.surface_shape == "circle") {
        profile.closed = true;
        profile.points.reserve(static_cast<size_t>(columns));
        for (int i = 0; i < columns; ++i) {
            const double t = static_cast<double>(i) / static_cast<double>(columns) * 2.0 * 3.14159265358979323846;
            profile.points.push_back(
                {std::cos(t) * options.radius, std::sin(t) * options.radius, 0.0});
        }
        return profile;
    }

    if (options.surface_shape == "ribbon") {
        profile.closed = false;
        const double hw = options.profile_width * 0.5;
        profile.points.push_back({-hw, 0.0, 0.0});
        profile.points.push_back({hw, 0.0, 0.0});
        return profile;
    }

    profile.closed = true;
    const double hw = options.profile_width * 0.5;
    const double hh = options.profile_height * 0.5;
    profile.points.push_back({-hw, -hh, 0.0});
    profile.points.push_back({hw, -hh, 0.0});
    profile.points.push_back({hw, hh, 0.0});
    profile.points.push_back({-hw, hh, 0.0});
    return profile;
}

geometry::CurveProfile profile_from_spline_data(const data::PcgSplineData& profile_spline,
                                              const SweepAlongSplineOptions& options)
{
    if (profile_spline.splines().empty())
        return {};

    const auto& spline = profile_spline.splines().front();
    std::vector<geometry::Vec3> points;
    points.reserve(spline.points.size());
    for (const auto& p : spline.points)
        points.push_back(to_vec3(p));

    return geometry::prepare_curve_profile(points, spline.closed, false, options.profile_plane);
}

bool spline_is_closed(const std::vector<geometry::Vec3>& polyline, bool closed_flag)
{
    if (closed_flag)
        return true;
    if (polyline.size() < 2)
        return false;
    return geometry::length(geometry::sub(polyline.front(), polyline.back())) <= 1e-4;
}

} // namespace

std::vector<geometry::Vec3> spline_data_to_polyline(const data::PcgSplineData& splines, size_t spline_index)
{
    if (splines.splines().empty())
        return {};
    const size_t idx = std::min(spline_index, splines.splines().size() - 1);
    const auto& spline = splines.splines()[idx];
    std::vector<geometry::Vec3> polyline;
    polyline.reserve(spline.points.size());
    for (const auto& p : spline.points)
        polyline.push_back(to_vec3(p));
    return polyline;
}

data::PcgSplineData polyline_to_spline_data(const std::vector<geometry::Vec3>& polyline, bool closed)
{
    data::PcgSplineData out;
    data::PcgSpline spline;
    spline.closed = closed;
    for (const auto& p : polyline)
        spline.points.push_back(data::PcgSplinePoint{p.x, p.y, p.z});
    out.add_spline(std::move(spline));
    return out;
}

data::PcgSplineData create_spline_data(const CreateSplineOptions& options)
{
    const std::vector<geometry::Vec3> controls = collect_control_points(options);
    const std::vector<geometry::Vec3> polyline =
        geometry::build_spline_polyline(controls, options.mode.c_str(), options.closed, options.subdivisions);
    return polyline_to_spline_data(polyline, options.closed);
}

data::PcgSplineData create_spiral_spline_data(const CreateSpiralSplineOptions& options)
{
    data::PcgSplineData out;

    if (options.radius <= 0.0 || options.pitch == 0.0 ||
        options.turns <= 0.0 || options.points_per_turn < 4)
        return out;

    int axis = -1;
    if (options.axis == "x" || options.axis == "X") axis = 0;
    else if (options.axis == "y" || options.axis == "Y") axis = 1;
    else if (options.axis == "z" || options.axis == "Z") axis = 2;
    else return out;

    const double pi = 3.14159265358979323846;
    const int sample_count = static_cast<int>(std::ceil(options.turns * options.points_per_turn)) + 1;

    data::PcgSpline spline;
    spline.closed = false;
    spline.points.reserve(static_cast<size_t>(sample_count));

    for (int i = 0; i < sample_count; ++i) {
        const double t = std::min(static_cast<double>(i) / options.points_per_turn, options.turns);
        const double height = t * options.pitch;
        const double angle = t * 2.0 * pi;
        const double c = options.radius * std::cos(angle);
        const double s = options.radius * std::sin(angle);

        data::PcgSplinePoint p{};
        if (axis == 0)      { p = {height, c, s}; }
        else if (axis == 1) { p = {c, height, s}; }
        else                { p = {c, s, height}; }

        spline.points.push_back(p);
    }

    out.add_spline(std::move(spline));
    return out;
}

data::PcgVec3 map_circle_uv_to_xyz(const std::string& orientation, double u, double v)
{
    if (orientation == "xy")
        return {u, v, 0.0};
    if (orientation == "yz")
        return {0.0, u, v};
    if (orientation == "zx")
        return {v, 0.0, u};
    return {};
}

data::PcgVec3 rotate_xyz_deg(const data::PcgVec3& point, const data::PcgVec3& degrees)
{
    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double rx = degrees.x * kDegToRad;
    const double ry = degrees.y * kDegToRad;
    const double rz = degrees.z * kDegToRad;

    const double cx = std::cos(rx);
    const double sx = std::sin(rx);
    const double cy = std::cos(ry);
    const double sy = std::sin(ry);
    const double cz = std::cos(rz);
    const double sz = std::sin(rz);

    double x = point.x;
    double y = point.y;
    double z = point.z;

    const double y1 = y * cx - z * sx;
    const double z1 = y * sx + z * cx;
    y = y1;
    z = z1;

    const double x2 = x * cy + z * sy;
    const double z2 = -x * sy + z * cy;
    x = x2;
    z = z2;

    const double x3 = x * cz - y * sz;
    const double y3 = x * sz + y * cz;
    x = x3;
    y = y3;

    return {x, y, z};
}

data::PcgSplineData create_arc_spline_data(const CreateArcSplineOptions& options)
{
    data::PcgSplineData out;

    const double scale = options.uniform_scale;
    const double radius_x = options.radius_x * scale;
    const double radius_y = options.radius_y * scale;
    if (radius_x <= 0.0 || radius_y <= 0.0 || options.divisions < 1)
        return out;

    const std::string& orientation = options.orientation;
    if (orientation != "xy" && orientation != "yz" && orientation != "zx")
        return out;

    const std::string& arc_type = options.arc_type;
    if (arc_type != "closed" && arc_type != "openArc" && arc_type != "closedArc" &&
        arc_type != "slicedArc")
        return out;

    constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
    const double start_deg = arc_type == "closed" ? 0.0 : options.start_angle_deg;
    const double end_deg = arc_type == "closed" ? 360.0 : options.end_angle_deg;
    const double start_rad = start_deg * kDegToRad;
    const double end_rad = end_deg * kDegToRad;
    const double span = end_rad - start_rad;

    const int divisions = options.divisions;
    int arc_samples = divisions + 1;
    if (arc_type == "closed")
        arc_samples = std::max(divisions, 3);
    else if (arc_type == "slicedArc")
        arc_samples = divisions + 1;

    data::PcgSpline spline;
    if (arc_type == "closed" || arc_type == "closedArc" || arc_type == "slicedArc")
        spline.closed = true;
    else
        spline.closed = false;

    const auto emit_local = [&](double u, double v) {
        if (options.reverse) {
            u = -u;
            v = -v;
        }
        data::PcgVec3 local = map_circle_uv_to_xyz(orientation, u, v);
        local = rotate_xyz_deg(local, options.rotate_deg);
        return data::PcgSplinePoint{
            local.x + options.center.x,
            local.y + options.center.y,
            local.z + options.center.z,
        };
    };

    const auto sample_arc = [&](int index, int count) {
        const double t = count <= 1 ? 0.0
                                    : static_cast<double>(index) / static_cast<double>(count - 1);
        const double angle = start_rad + span * t;
        return emit_local(radius_x * std::cos(angle), radius_y * std::sin(angle));
    };

    if (arc_type == "slicedArc") {
        spline.points.push_back(emit_local(0.0, 0.0));
        for (int i = 0; i < arc_samples; ++i)
            spline.points.push_back(sample_arc(i, arc_samples));
    } else if (arc_type == "closedArc") {
        for (int i = 0; i < arc_samples; ++i)
            spline.points.push_back(sample_arc(i, arc_samples));
        if (!spline.points.empty())
            spline.points.push_back(spline.points.front());
    } else if (arc_type == "closed") {
        for (int i = 0; i < arc_samples; ++i) {
            const double angle = 2.0 * 3.14159265358979323846 *
                                 static_cast<double>(i) / static_cast<double>(arc_samples);
            spline.points.push_back(
                emit_local(radius_x * std::cos(angle), radius_y * std::sin(angle)));
        }
    } else {
        for (int i = 0; i < arc_samples; ++i)
            spline.points.push_back(sample_arc(i, arc_samples));
    }

    if (spline.points.size() < 2)
        return out;

    out.add_spline(std::move(spline));
    return out;
}

namespace {

void set_error(std::string* error, const char* message)
{
    if (error)
        *error = message;
}

bool is_finite_number(double value)
{
    return std::isfinite(value);
}

double point_to_segment_distance(const geometry::Vec3& p,
                                 const geometry::Vec3& a,
                                 const geometry::Vec3& b)
{
    const geometry::Vec3 ab = geometry::sub(b, a);
    const geometry::Vec3 ap = geometry::sub(p, a);
    const double ab2 = geometry::dot(ab, ab);
    if (ab2 <= 1e-30)
        return geometry::length(ap);
    double t = geometry::dot(ap, ab) / ab2;
    if (t < 0.0)
        t = 0.0;
    else if (t > 1.0)
        t = 1.0;
    const geometry::Vec3 q = geometry::add(a, geometry::scale(ab, t));
    return geometry::length(geometry::sub(p, q));
}

bool mark_protect_mask(const data::PcgSpline& spline,
                       const std::vector<ProtectSpan>& spans,
                       std::vector<char>& protected_mask,
                       std::string* error)
{
    const int n = static_cast<int>(spline.points.size());
    protected_mask.assign(static_cast<size_t>(n), 0);
    for (const ProtectSpan& span : spans) {
        if (span.start < 0 || span.end < 0 || span.start >= n || span.end >= n) {
            set_error(error, "ConditionOutline protectSpans index out of range");
            return false;
        }
        if (span.start <= span.end) {
            for (int i = span.start; i <= span.end; ++i)
                protected_mask[static_cast<size_t>(i)] = 1;
        } else {
            if (!spline.closed) {
                set_error(error, "ConditionOutline cross-seam protectSpans requires closed spline");
                return false;
            }
            for (int i = span.start; i < n; ++i)
                protected_mask[static_cast<size_t>(i)] = 1;
            for (int i = 0; i <= span.end; ++i)
                protected_mask[static_cast<size_t>(i)] = 1;
        }
    }
    return true;
}

bool has_explicit_close_duplicate(const data::PcgSpline& spline)
{
    if (!spline.closed || spline.points.size() < 2)
        return false;

    const auto& first = spline.points.front();
    const auto& last = spline.points.back();
    return geometry::length(
               geometry::sub(geometry::Vec3{first.x, first.y, first.z},
                              geometry::Vec3{last.x, last.y, last.z})) <= 1e-9;
}

std::vector<geometry::Vec3> smooth_polyline(const std::vector<geometry::Vec3>& src,
                                            bool closed,
                                            int win,
                                            const std::vector<char>& protected_mask)
{
    std::vector<geometry::Vec3> out = src;
    if (win <= 1 || src.empty())
        return out;

    const int n = static_cast<int>(src.size());
    const int half = win / 2;
    for (int i = 0; i < n; ++i) {
        if (protected_mask[static_cast<size_t>(i)])
            continue;
        geometry::Vec3 acc{0.0, 0.0, 0.0};
        for (int k = -half; k <= half; ++k) {
            int j = i + k;
            if (closed) {
                j %= n;
                if (j < 0)
                    j += n;
            } else {
                j = std::max(0, std::min(n - 1, j));
            }
            acc = geometry::add(acc, src[static_cast<size_t>(j)]);
        }
        out[static_cast<size_t>(i)] = geometry::scale(acc, 1.0 / static_cast<double>(win));
    }
    return out;
}

void rdp_mark_segment(const std::vector<geometry::Vec3>& pts,
                      const std::vector<int>& path,
                      double eps,
                      std::vector<char>& keep)
{
    if (path.size() <= 2)
        return;

    const geometry::Vec3& a = pts[static_cast<size_t>(path.front())];
    const geometry::Vec3& b = pts[static_cast<size_t>(path.back())];
    double best_d = -1.0;
    size_t best_pos = 0;
    for (size_t i = 1; i + 1 < path.size(); ++i) {
        const double d = point_to_segment_distance(pts[static_cast<size_t>(path[i])], a, b);
        if (d > best_d) {
            best_d = d;
            best_pos = i;
        }
    }
    // Fixed <= rule: distance <= eps may be deleted; only > eps becomes a keep anchor.
    if (best_d <= eps)
        return;

    const int pivot = path[best_pos];
    keep[static_cast<size_t>(pivot)] = 1;
    std::vector<int> left(path.begin(), path.begin() + static_cast<std::ptrdiff_t>(best_pos) + 1);
    std::vector<int> right(path.begin() + static_cast<std::ptrdiff_t>(best_pos), path.end());
    rdp_mark_segment(pts, left, eps, keep);
    rdp_mark_segment(pts, right, eps, keep);
}

std::vector<int> closed_path_indices(int start, int end, int n)
{
    std::vector<int> path;
    path.reserve(static_cast<size_t>(n + 1));
    int j = start;
    path.push_back(j);
    while (j != end) {
        j = (j + 1) % n;
        path.push_back(j);
    }
    return path;
}

std::vector<geometry::Vec3> simplify_rdp(const std::vector<geometry::Vec3>& pts,
                                         bool closed,
                                         double eps,
                                         const std::vector<char>& protected_mask)
{
    const int n = static_cast<int>(pts.size());
    if (n == 0 || eps <= 0.0)
        return pts;

    std::vector<char> keep(static_cast<size_t>(n), 0);
    for (int i = 0; i < n; ++i) {
        if (protected_mask[static_cast<size_t>(i)])
            keep[static_cast<size_t>(i)] = 1;
    }
    if (!closed) {
        keep[0] = 1;
        keep[static_cast<size_t>(n - 1)] = 1;
    } else {
        bool any = false;
        for (char flag : keep) {
            if (flag) {
                any = true;
                break;
            }
        }
        if (!any) {
            // Deterministic closed seed: index 0 and farthest point from it.
            keep[0] = 1;
            int farthest = 0;
            double best = -1.0;
            for (int i = 1; i < n; ++i) {
                const double d = geometry::length(geometry::sub(pts[static_cast<size_t>(i)], pts[0]));
                if (d > best) {
                    best = d;
                    farthest = i;
                }
            }
            keep[static_cast<size_t>(farthest)] = 1;
        }
    }

    std::vector<int> anchors;
    anchors.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (keep[static_cast<size_t>(i)])
            anchors.push_back(i);
    }

    if (!closed) {
        for (size_t a = 0; a + 1 < anchors.size(); ++a) {
            std::vector<int> path;
            for (int i = anchors[a]; i <= anchors[a + 1]; ++i)
                path.push_back(i);
            rdp_mark_segment(pts, path, eps, keep);
        }
    } else {
        const size_t count = anchors.size();
        for (size_t a = 0; a < count; ++a) {
            const int start = anchors[a];
            const int end = anchors[(a + 1) % count];
            const std::vector<int> path = closed_path_indices(start, end, n);
            rdp_mark_segment(pts, path, eps, keep);
        }
    }

    std::vector<geometry::Vec3> out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        if (keep[static_cast<size_t>(i)])
            out.push_back(pts[static_cast<size_t>(i)]);
    }
    return out;
}

data::PcgSpline condition_one_spline(const data::PcgSpline& spline,
                                     const ConditionOutlineOptions& options,
                                     std::string* error)
{
    data::PcgSpline out = spline;

    std::vector<char> input_protected_mask;
    if (!mark_protect_mask(spline, options.protect_spans, input_protected_mask, error))
        return {};

    if (spline.points.size() < 3)
        return out;

    const bool noop = options.win <= 1 && options.eps <= 0.0;
    if (noop)
        return out;

    const bool explicit_close_duplicate = has_explicit_close_duplicate(spline);
    const size_t logical_count =
        spline.points.size() - (explicit_close_duplicate ? static_cast<size_t>(1) : 0u);
    if (logical_count < 3)
        return out;

    std::vector<char> protected_mask(logical_count, 0);
    for (size_t i = 0; i < logical_count; ++i)
        protected_mask[i] = input_protected_mask[i];
    if (explicit_close_duplicate && input_protected_mask.back())
        protected_mask.front() = 1;

    std::vector<geometry::Vec3> work;
    work.reserve(logical_count);
    for (size_t i = 0; i < logical_count; ++i)
        work.push_back(to_vec3(spline.points[i]));

    work = smooth_polyline(work, spline.closed, options.win, protected_mask);
    work = simplify_rdp(work, spline.closed, options.eps, protected_mask);

    out.points.clear();
    out.points.reserve(work.size());
    for (const auto& p : work)
        out.points.push_back(data::PcgSplinePoint{p.x, p.y, p.z});
    return out;
}

} // namespace

data::PcgSplineData condition_outline_data(const data::PcgSplineData& input,
                                           const ConditionOutlineOptions& options,
                                           std::string* error)
{
    if (error)
        error->clear();

    if (options.win < 1 || (options.win % 2) == 0) {
        set_error(error, "ConditionOutline win must be an odd integer >= 1");
        return {};
    }
    if (!is_finite_number(options.eps) || options.eps < 0.0) {
        set_error(error, "ConditionOutline eps must be a finite number >= 0");
        return {};
    }

    data::PcgSplineData out;
    out.metadata() = input.metadata();
    for (const auto& spline : input.splines()) {
        data::PcgSpline next = condition_one_spline(spline, options, error);
        if (error && !error->empty())
            return {};
        out.add_spline(std::move(next));
    }
    return out;
}

data::PcgSplineData resample_spline_data(const data::PcgSplineData& input, const ResampleSplineOptions& options)
{
    geometry::PolylineResampleOptions polyline_opts;
    if (options.use_max_segments || options.use_max_segment_length) {
        polyline_opts.use_max_segments = options.use_max_segments;
        polyline_opts.max_segments = options.max_segments;
        polyline_opts.use_max_segment_length = options.use_max_segment_length;
        polyline_opts.max_segment_length = options.max_segment_length;
        polyline_opts.measure = options.measure;
        polyline_opts.even_last_segment_same_length = options.even_last_segment_same_length;
        polyline_opts.maintain_last_vertex = options.maintain_last_vertex;
    } else if (options.mode == "count") {
        polyline_opts.use_max_segments = true;
        polyline_opts.max_segments = std::max(1, options.point_count - 1);
    } else {
        polyline_opts.use_max_segment_length = true;
        polyline_opts.max_segment_length = std::max(0.01, options.spacing);
        polyline_opts.even_last_segment_same_length = false;
    }

    data::PcgSplineData out;
    for (size_t curve_index = 0; curve_index < input.splines().size(); ++curve_index) {
        const auto& spline = input.splines()[curve_index];
        std::vector<geometry::Vec3> polyline;
        polyline.reserve(spline.points.size());
        for (const auto& p : spline.points)
            polyline.push_back(to_vec3(p));

        const geometry::PolylineResampleResult resampled =
            geometry::resample_polyline_houdini(polyline, polyline_opts);

        data::PcgSpline next;
        next.closed = spline.closed;
        for (const auto& p : resampled.points)
            next.points.push_back(data::PcgSplinePoint{p.x, p.y, p.z});

        if (options.write_curve_u_attr && !resampled.curve_u.empty())
            next.attributes[options.curve_u_attribute] = resampled.curve_u;
        if (options.write_distance_attr && !resampled.half_edge_lengths.empty())
            next.attributes[options.distance_attribute] = resampled.half_edge_lengths;
        if (options.write_curve_num_attr)
            next.attributes[options.curve_num_attribute] = static_cast<int>(curve_index);
        out.add_spline(std::move(next));
    }
    return out;
}

data::PcgPointData sample_along_spline(const data::PcgSplineData& splines, const SampleAlongSplineOptions& options)
{
    data::PcgPointData points;
    if (splines.splines().empty())
        return points;

    for (const auto& spline : splines.splines()) {
        std::vector<geometry::Vec3> polyline;
        for (const auto& p : spline.points)
            polyline.push_back(to_vec3(p));

        const double total = geometry::polyline_length(polyline);
        if (total <= 1e-9)
            continue;

        const auto frames = geometry::build_frames(polyline);
        double traveled = options.offset;
        while (traveled <= total + 1e-9) {
            size_t seg = 0;
            double seg_start = 0.0;
            while (seg + 1 < polyline.size()) {
                const double seg_len = geometry::length(geometry::sub(polyline[seg + 1], polyline[seg]));
                if (seg_len > 1e-9 && traveled <= seg_start + seg_len)
                    break;
                seg_start += seg_len;
                ++seg;
            }

            if (seg + 1 >= polyline.size())
                break;

            const double seg_len = geometry::length(geometry::sub(polyline[seg + 1], polyline[seg]));
            const double local_t = seg_len <= 1e-9 ? 0.0 : (traveled - seg_start) / seg_len;
            const geometry::Vec3 pos =
                geometry::add(polyline[seg], geometry::scale(geometry::sub(polyline[seg + 1], polyline[seg]), local_t));
            const geometry::Frame3& frame = frames[std::min(seg, frames.size() - 1)];

            data::PcgPoint point{pos.x, pos.y, pos.z};
            if (options.align_to_tangent) {
                point.attributes["nx"] = frame.normal.x;
                point.attributes["ny"] = frame.normal.y;
                point.attributes["nz"] = frame.normal.z;
                point.attributes["tx"] = frame.tangent.x;
                point.attributes["ty"] = frame.tangent.y;
                point.attributes["tz"] = frame.tangent.z;
            }
            points.add_point(std::move(point));

            if (!options.include_end && traveled + options.spacing > total)
                break;
            traveled += std::max(0.01, options.spacing);
        }

        if (options.include_end && !polyline.empty()) {
            const geometry::Vec3& end = polyline.back();
            const geometry::Frame3& frame = frames.back();
            data::PcgPoint point{end.x, end.y, end.z};
            if (options.align_to_tangent) {
                point.attributes["nx"] = frame.normal.x;
                point.attributes["ny"] = frame.normal.y;
                point.attributes["nz"] = frame.normal.z;
                point.attributes["tx"] = frame.tangent.x;
                point.attributes["ty"] = frame.tangent.y;
                point.attributes["tz"] = frame.tangent.z;
            }
            points.add_point(std::move(point));
        }
    }

    (void)options.seed;
    return points;
}

data::PcgMeshData extract_cross_section_profile(const data::PcgMeshData& mesh,
                                                const CrossSectionProfileOptions& options)
{
    geometry::CrossSectionOptions prep;
    prep.plane = options.plane;
    prep.weld_epsilon = options.weld_epsilon;
    prep.center = options.center;

    const geometry::CrossSectionMesh section = geometry::prepare_cross_section(mesh, prep);
    data::PcgMeshData out;
    for (const auto& v : section.vertices)
        out.add_vertex({v.x, v.y, v.z});
    for (int idx : section.triangles)
        out.triangles_mut().push_back(idx);
    return out;
}

data::PcgMeshData sweep_along_spline(const data::PcgSplineData& backbone,
                                     const data::PcgSplineData* profile_spline,
                                     const SweepAlongSplineOptions& options)
{
    data::PcgMeshData result;
    if (backbone.splines().empty())
        return result;

    geometry::CurveProfile profile;
    if (options.use_profile_spline && profile_spline && !profile_spline->splines().empty())
        profile = profile_from_spline_data(*profile_spline, options);
    else
        profile = make_builtin_curve_profile(options);

    if (profile.points.size() < 2)
        return result;

    geometry::SweepAlongFramesOptions sweep_opts;
    sweep_opts.cap_start = options.cap_start;
    sweep_opts.cap_end = options.cap_end;
    sweep_opts.profile_roll_radians =
        options.profile_roll_degrees * 3.14159265358979323846 / 180.0;
    sweep_opts.twist_radians = options.twist_degrees * 3.14159265358979323846 / 180.0;
    sweep_opts.scale_start = options.scale_start;
    sweep_opts.scale_end = options.scale_end;
    sweep_opts.profile_closed = profile.closed;

    const geometry::Vec3 up_hint{options.up_x, options.up_y, options.up_z};

    for (const auto& spline : backbone.splines()) {
        std::vector<geometry::Vec3> polyline;
        for (const auto& p : spline.points)
            polyline.push_back(to_vec3(p));

        sweep_opts.backbone_closed = spline_is_closed(polyline, spline.closed);

        polyline = geometry::resample_polyline_by_spacing(polyline, std::max(0.05, options.sample_spacing));
        if (polyline.size() < 2)
            continue;

        const auto frames = geometry::build_frames(polyline, up_hint);
        const data::PcgMeshData swept = geometry::sweep_curve_profile(profile, frames, sweep_opts);
        result = merge_meshes(result, swept);
    }

    return result;
}

data::PcgGeometry sweep_along_spline_geometry(const data::PcgSplineData& backbone,
                                              const data::PcgSplineData* profile_spline,
                                              const SweepAlongSplineOptions& options)
{
    data::PcgGeometry result;
    if (backbone.splines().empty())
        return result;

    geometry::CurveProfile profile;
    if (options.use_profile_spline && profile_spline && !profile_spline->splines().empty())
        profile = profile_from_spline_data(*profile_spline, options);
    else
        profile = make_builtin_curve_profile(options);

    if (profile.points.size() < 2)
        return result;

    geometry::SweepAlongFramesOptions sweep_opts;
    sweep_opts.cap_start = options.cap_start;
    sweep_opts.cap_end = options.cap_end;
    sweep_opts.profile_roll_radians =
        options.profile_roll_degrees * 3.14159265358979323846 / 180.0;
    sweep_opts.twist_radians = options.twist_degrees * 3.14159265358979323846 / 180.0;
    sweep_opts.scale_start = options.scale_start;
    sweep_opts.scale_end = options.scale_end;
    sweep_opts.profile_closed = profile.closed;

    const geometry::Vec3 up_hint{options.up_x, options.up_y, options.up_z};

    for (const auto& spline : backbone.splines()) {
        std::vector<geometry::Vec3> polyline;
        for (const auto& p : spline.points)
            polyline.push_back(to_vec3(p));

        sweep_opts.backbone_closed = spline_is_closed(polyline, spline.closed);

        polyline = geometry::resample_polyline_by_spacing(polyline, std::max(0.05, options.sample_spacing));
        if (polyline.size() < 2)
            continue;

        const auto frames = geometry::build_frames(polyline, up_hint);
        const data::PcgGeometry swept =
            geometry::sweep_curve_profile_geometry(profile, frames, sweep_opts);
        result = data::merge_geometries(result, swept);
    }

    return result;
}

data::PcgMeshData extrude_along_spline(const data::PcgSplineData& splines,
                                       const data::PcgMeshData* profile_mesh,
                                       const ExtrudeAlongSplineOptions& options)
{
    data::PcgMeshData result;
    if (splines.splines().empty())
        return result;

    const data::PcgMeshData box_profile = make_box_profile(options.profile_width, options.profile_height);
    const data::PcgMeshData& source_profile =
        (options.use_profile_mesh && profile_mesh && !profile_mesh->vertices().empty()) ? *profile_mesh
                                                                                        : box_profile;

    geometry::CrossSectionOptions prep;
    prep.plane = options.profile_plane;
    prep.center = true;
    const geometry::CrossSectionMesh section = geometry::prepare_cross_section(source_profile, prep);

    geometry::SweepAlongFramesOptions sweep_opts;
    sweep_opts.cap_start = options.cap_start;
    sweep_opts.cap_end = options.cap_end;
    sweep_opts.profile_roll_radians =
        options.profile_roll_degrees * 3.14159265358979323846 / 180.0;
    sweep_opts.twist_radians = options.twist_degrees * 3.14159265358979323846 / 180.0;
    sweep_opts.scale_start = options.scale_start;
    sweep_opts.scale_end = options.scale_end;

    const geometry::Vec3 up_hint{options.up_x, options.up_y, options.up_z};

    for (const auto& spline : splines.splines()) {
        std::vector<geometry::Vec3> polyline;
        for (const auto& p : spline.points)
            polyline.push_back(to_vec3(p));

        polyline = geometry::resample_polyline_by_spacing(polyline, std::max(0.05, options.sample_spacing));
        if (polyline.size() < 2)
            continue;

        const auto frames = geometry::build_frames(polyline, up_hint);
        const data::PcgMeshData swept = geometry::sweep_cross_section(section, frames, sweep_opts);
        result = merge_meshes(result, swept);
    }

    return result;
}

data::PcgMeshData merge_meshes(const data::PcgMeshData& a, const data::PcgMeshData& b)
{
    if (a.vertices().empty())
        return b;
    if (b.vertices().empty())
        return a;

    data::PcgMeshData out;
    const int a_verts = static_cast<int>(a.vertices().size());

    for (const auto& v : a.vertices())
        out.vertices_mut().push_back(v);
    for (const auto& v : b.vertices())
        out.vertices_mut().push_back(v);

    for (int idx : a.triangles())
        out.triangles_mut().push_back(idx);
    for (int idx : b.triangles())
        out.triangles_mut().push_back(idx + a_verts);

    if (a.has_normals() && b.has_normals()) {
        std::vector<data::PcgVertex> normals;
        normals.reserve(out.vertices().size());
        for (const auto& n : a.normals())
            normals.push_back(n);
        for (const auto& n : b.normals())
            normals.push_back(n);
        out.set_normals(std::move(normals));
    }

    if (a.has_colors() && b.has_colors()) {
        std::vector<data::PcgColor> colors;
        colors.reserve(out.vertices().size());
        for (const auto& c : a.colors())
            colors.push_back(c);
        for (const auto& c : b.colors())
            colors.push_back(c);
        out.set_colors(std::move(colors));
    }

    if (a.has_uvs() && b.has_uvs()) {
        std::vector<data::PcgVec2> uvs;
        uvs.reserve(out.vertices().size());
        for (const auto& uv : a.uvs())
            uvs.push_back(uv);
        for (const auto& uv : b.uvs())
            uvs.push_back(uv);
        out.set_uvs(std::move(uvs));
    }

    out.metadata() = a.metadata();
    return out;
}

data::PcgMeshData merge_meshes(const std::vector<data::PcgMeshData>& meshes)
{
    data::PcgMeshData out;
    for (const auto& mesh : meshes)
        out = merge_meshes(out, mesh);
    return out;
}

data::PcgMeshData instance_along_spline(const data::PcgSplineData& splines,
                                        const data::PcgMeshData& prototype,
                                        const InstanceAlongSplineOptions& options)
{
    SampleAlongSplineOptions sample_opts;
    sample_opts.spacing = options.spacing;
    sample_opts.offset = options.offset;
    sample_opts.include_end = options.include_end;
    sample_opts.align_to_tangent = options.align_to_tangent;

    const data::PcgPointData placements = sample_along_spline(splines, sample_opts);
    data::PcgMeshData merged;

    geometry::Vec3 prototype_center{0.0, 0.0, 0.0};
    if (!prototype.vertices().empty()) {
        for (const auto& v : prototype.vertices())
            prototype_center = geometry::add(prototype_center, to_vec3(v));
        prototype_center =
            geometry::scale(prototype_center, 1.0 / static_cast<double>(prototype.vertices().size()));
    }

    for (const auto& placement : placements.points()) {
        geometry::Vec3 normal{0.0, 1.0, 0.0};
        geometry::Vec3 tangent{0.0, 0.0, 1.0};
        if (placement.attributes.contains("nx") && placement.attributes.contains("ny") &&
            placement.attributes.contains("nz")) {
            normal = geometry::Vec3{
                placement.attributes["nx"].get<double>(),
                placement.attributes["ny"].get<double>(),
                placement.attributes["nz"].get<double>(),
            };
        }
        if (placement.attributes.contains("tx") && placement.attributes.contains("ty") &&
            placement.attributes.contains("tz")) {
            tangent = geometry::Vec3{
                placement.attributes["tx"].get<double>(),
                placement.attributes["ty"].get<double>(),
                placement.attributes["tz"].get<double>(),
            };
        }

        geometry::Vec3 binormal = geometry::normalize(geometry::cross(normal, tangent));

        const geometry::Frame3 frame{{placement.x, placement.y, placement.z}, tangent, normal, binormal};
        const int offset = static_cast<int>(merged.vertices().size());

        for (const auto& v : prototype.vertices()) {
            geometry::Vec3 local = geometry::sub(to_vec3(v), prototype_center);
            local = geometry::scale(local, options.scale);
            merged.add_vertex(to_vertex(geometry::transform_local_to_world(frame, local)));
        }

        for (int idx : prototype.triangles())
            merged.triangles_mut().push_back(idx + offset);
    }

    return merged;
}

} // namespace pcg::internal::elements
