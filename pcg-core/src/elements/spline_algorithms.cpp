#include "elements/spline_algorithms.hpp"

#include "elements/element_utils.hpp"

#include <cmath>
#include <cstring>

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

void append_cap_fan(data::PcgMeshData& mesh, const std::vector<int>& ring, bool flip)
{
    if (ring.size() < 3)
        return;

    geometry::Vec3 center{0.0, 0.0, 0.0};
    for (int idx : ring) {
        const auto& v = mesh.vertices()[static_cast<size_t>(idx)];
        center = geometry::add(center, to_vec3(v));
    }
    center = geometry::scale(center, 1.0 / static_cast<double>(ring.size()));

    const int center_idx = static_cast<int>(mesh.vertices().size());
    mesh.add_vertex(to_vertex(center));

    for (size_t i = 0; i + 1 < ring.size(); ++i) {
        const int a = ring[i];
        const int b = ring[i + 1];
        if (flip)
            mesh.add_triangle(center_idx, b, a);
        else
            mesh.add_triangle(center_idx, a, b);
    }
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

std::vector<geometry::Vec3> profile_ring_local(const data::PcgMeshData& profile)
{
    std::vector<geometry::Vec3> ring;
    ring.reserve(profile.vertices().size());
    for (const auto& v : profile.vertices())
        ring.push_back(to_vec3(v));
    return ring;
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

data::PcgSplineData resample_spline_data(const data::PcgSplineData& input, const ResampleSplineOptions& options)
{
    data::PcgSplineData out;
    for (const auto& spline : input.splines()) {
        std::vector<geometry::Vec3> polyline;
        polyline.reserve(spline.points.size());
        for (const auto& p : spline.points)
            polyline.push_back(to_vec3(p));

        std::vector<geometry::Vec3> resampled;
        if (options.mode == "count")
            resampled = geometry::resample_polyline_by_count(polyline, std::max(2, options.point_count));
        else
            resampled = geometry::resample_polyline_by_spacing(polyline, std::max(0.01, options.spacing));

        data::PcgSpline next;
        next.closed = spline.closed;
        for (const auto& p : resampled)
            next.points.push_back(data::PcgSplinePoint{p.x, p.y, p.z});
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

data::PcgMeshData extrude_along_spline(const data::PcgSplineData& splines,
                                       const data::PcgMeshData* profile_mesh,
                                       const ExtrudeAlongSplineOptions& options)
{
    data::PcgMeshData result;
    if (splines.splines().empty())
        return result;

    const data::PcgMeshData box_profile = make_box_profile(options.profile_width, options.profile_height);
    const data::PcgMeshData& profile = (options.use_profile_mesh && profile_mesh && !profile_mesh->vertices().empty())
                                           ? *profile_mesh
                                           : box_profile;
    const std::vector<geometry::Vec3> profile_ring = profile_ring_local(profile);

    for (const auto& spline : splines.splines()) {
        std::vector<geometry::Vec3> polyline;
        for (const auto& p : spline.points)
            polyline.push_back(to_vec3(p));

        polyline = geometry::resample_polyline_by_spacing(polyline, std::max(0.05, options.sample_spacing));
        if (polyline.size() < 2)
            continue;

        const auto frames = geometry::build_frames(polyline);
        std::vector<std::vector<int>> rings;
        rings.reserve(frames.size());

        for (size_t i = 0; i < frames.size(); ++i) {
            std::vector<int> ring;
            ring.reserve(profile_ring.size());
            for (const auto& local : profile_ring) {
                const geometry::Vec3 world = geometry::transform_local_to_world(frames[i], local);
                ring.push_back(static_cast<int>(result.vertices().size()));
                result.add_vertex(to_vertex(world));
            }
            rings.push_back(std::move(ring));
        }

        for (size_t i = 0; i + 1 < rings.size(); ++i) {
            const auto& a = rings[i];
            const auto& b = rings[i + 1];
            const size_t count = std::min(a.size(), b.size());
            for (size_t j = 0; j < count; ++j) {
                const size_t jn = (j + 1) % count;
                result.add_triangle(a[j], b[j], b[jn]);
                result.add_triangle(a[j], b[jn], a[jn]);
            }
        }

        if (options.cap_start && !rings.empty())
            append_cap_fan(result, rings.front(), true);
        if (options.cap_end && !rings.empty())
            append_cap_fan(result, rings.back(), false);
    }

    return result;
}

data::PcgMeshData transform_mesh(const data::PcgMeshData& mesh, const TransformMeshOptions& options)
{
    data::PcgMeshData out = mesh;
    const double rx = options.rotation_x_deg * 3.14159265358979323846 / 180.0;
    const double ry = options.rotation_y_deg * 3.14159265358979323846 / 180.0;
    const double rz = options.rotation_z_deg * 3.14159265358979323846 / 180.0;

    const auto rot_x = [&](const geometry::Vec3& v) {
        const double c = std::cos(rx);
        const double s = std::sin(rx);
        return geometry::Vec3{v.x, v.y * c - v.z * s, v.y * s + v.z * c};
    };
    const auto rot_y = [&](const geometry::Vec3& v) {
        const double c = std::cos(ry);
        const double s = std::sin(ry);
        return geometry::Vec3{v.x * c + v.z * s, v.y, -v.x * s + v.z * c};
    };
    const auto rot_z = [&](const geometry::Vec3& v) {
        const double c = std::cos(rz);
        const double s = std::sin(rz);
        return geometry::Vec3{v.x * c - v.y * s, v.x * s + v.y * c, v.z};
    };

    for (auto& vertex : out.vertices_mut()) {
        geometry::Vec3 v = to_vec3(vertex);
        v = rot_x(v);
        v = rot_y(v);
        v = rot_z(v);
        v.x *= options.scale_x;
        v.y *= options.scale_y;
        v.z *= options.scale_z;
        v = geometry::add(v, geometry::Vec3{options.translate_x, options.translate_y, options.translate_z});
        vertex = to_vertex(v);
    }

    return out;
}

data::PcgMeshData merge_meshes(const data::PcgMeshData& a, const data::PcgMeshData& b)
{
    data::PcgMeshData out = a;
    const int offset = static_cast<int>(out.vertices().size());
    for (const auto& v : b.vertices())
        out.vertices_mut().push_back(v);
    for (int idx : b.triangles())
        out.triangles_mut().push_back(idx + offset);
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

        geometry::Vec3 binormal = geometry::normalize(geometry::cross(tangent, normal));
        normal = geometry::normalize(geometry::cross(binormal, tangent));

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
