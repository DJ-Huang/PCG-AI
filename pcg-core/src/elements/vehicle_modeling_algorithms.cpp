#include "elements/vehicle_modeling_algorithms.hpp"

#include "geometry/group_table.hpp"
#include "geometry/mesh_topology.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace pcg::internal::elements {
namespace {

constexpr double kPi = 3.14159265358979323846;

data::PcgVec3 add(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

data::PcgVec3 sub(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

data::PcgVec3 scale(const data::PcgVec3& v, double s)
{
    return {v.x * s, v.y * s, v.z * s};
}

double length(const data::PcgVec3& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

data::PcgVec3 normalize(const data::PcgVec3& v)
{
    const double len = length(v);
    return len <= 1e-12 ? data::PcgVec3{} : scale(v, 1.0 / len);
}

double axis_value(const data::PcgVec3& p, const std::string& axis)
{
    if (axis == "y" || axis == "Y") return p.y;
    if (axis == "z" || axis == "Z") return p.z;
    return p.x;
}

data::PcgVec3 to_point(const data::PcgSplinePoint& p)
{
    return {p.x, p.y, p.z};
}

data::PcgVec3 sample_profile(const std::vector<data::PcgVec3>& points,
                                  bool closed,
                                  double u)
{
    if (points.empty()) return {};
    if (points.size() == 1) return points.front();
    const int segments = closed ? static_cast<int>(points.size())
                                : static_cast<int>(points.size()) - 1;
    const double f = std::clamp(u, 0.0, 1.0) * segments;
    int seg = std::min(static_cast<int>(std::floor(f)), segments - 1);
    const double t = std::min(1.0, f - seg);
    const int next = closed ? (seg + 1) % static_cast<int>(points.size()) : seg + 1;
    return add(points[static_cast<size_t>(seg)],
               scale(sub(points[static_cast<size_t>(next)], points[static_cast<size_t>(seg)]), t));
}

std::vector<data::PcgVec3> resample_profile(const data::PcgSpline& spline,
                                             int columns,
                                             bool closed)
{
    std::vector<data::PcgVec3> source;
    source.reserve(spline.points.size());
    for (const auto& p : spline.points) source.push_back(to_point(p));
    if (source.size() > 1 && length(sub(source.front(), source.back())) <= 1e-8)
        source.pop_back();

    std::vector<data::PcgVec3> result;
    result.reserve(static_cast<size_t>(columns));
    for (int i = 0; i < columns; ++i) {
        const double denominator = closed ? static_cast<double>(columns)
                                          : static_cast<double>(std::max(1, columns - 1));
        result.push_back(sample_profile(source, closed, static_cast<double>(i) / denominator));
    }
    return result;
}

double squared_distance(const data::PcgVec3& a, const data::PcgVec3& b)
{
    const auto d = sub(a, b);
    return d.x * d.x + d.y * d.y + d.z * d.z;
}

void align_closed_profile(std::vector<data::PcgVec3>& profile,
                          const std::vector<data::PcgVec3>& previous)
{
    if (profile.size() != previous.size() || profile.empty()) return;
    const int n = static_cast<int>(profile.size());
    double best_cost = std::numeric_limits<double>::max();
    int best_shift = 0;
    bool best_reverse = false;
    for (int reverse = 0; reverse < 2; ++reverse) {
        for (int shift = 0; shift < n; ++shift) {
            double cost = 0.0;
            for (int i = 0; i < n; ++i) {
                const int index = reverse ? (shift - i + n * 2) % n : (shift + i) % n;
                cost += squared_distance(previous[static_cast<size_t>(i)],
                                         profile[static_cast<size_t>(index)]);
            }
            if (cost < best_cost) {
                best_cost = cost;
                best_shift = shift;
                best_reverse = reverse != 0;
            }
        }
    }
    std::vector<data::PcgVec3> aligned;
    aligned.reserve(profile.size());
    for (int i = 0; i < n; ++i) {
        const int index = best_reverse ? (best_shift - i + n * 2) % n : (best_shift + i) % n;
        aligned.push_back(profile[static_cast<size_t>(index)]);
    }
    profile = std::move(aligned);
}

data::PcgVec3 face_center(const data::PcgGeometry& geometry, const std::vector<int>& face)
{
    data::PcgVec3 center{};
    for (int index : face) center = add(center, geometry.points()[static_cast<size_t>(index)]);
    return scale(center, 1.0 / static_cast<double>(face.size()));
}

data::PcgVec3 face_normal(const data::PcgGeometry& geometry, const std::vector<int>& face)
{
    data::PcgVec3 normal{};
    for (size_t i = 0; i < face.size(); ++i) {
        const auto& current = geometry.points()[static_cast<size_t>(face[i])];
        const auto& next = geometry.points()[static_cast<size_t>(face[(i + 1) % face.size()])];
        normal.x += (current.y - next.y) * (current.z + next.z);
        normal.y += (current.z - next.z) * (current.x + next.x);
        normal.z += (current.x - next.x) * (current.y + next.y);
    }
    return normalize(normal);
}

std::string position_key(const data::PcgVec3& p, double tolerance)
{
    const auto q = [tolerance](double value) {
        return static_cast<int64_t>(std::llround(value / tolerance));
    };
    return std::to_string(q(p.x)) + ',' + std::to_string(q(p.y)) + ',' + std::to_string(q(p.z));
}

data::PcgVec3 rotate_about_axis(const data::PcgVec3& point,
                                const data::PcgVec3& center,
                                const std::string& axis,
                                double radians)
{
    auto p = sub(point, center);
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    data::PcgVec3 rotated{};
    if (axis == "y" || axis == "Y")
        rotated = {p.x * c + p.z * s, p.y, -p.x * s + p.z * c};
    else if (axis == "z" || axis == "Z")
        rotated = {p.x * c - p.y * s, p.x * s + p.y * c, p.z};
    else
        rotated = {p.x, p.y * c - p.z * s, p.y * s + p.z * c};
    return add(rotated, center);
}

} // namespace

data::PcgGeometry loft_splines(const std::vector<data::PcgSpline>& input_profiles,
                               const LoftMeshOptions& options)
{
    data::PcgGeometry output;
    if (input_profiles.size() < 2 || options.columns < 2) return output;

    struct Section { double order = 0.0; std::vector<data::PcgVec3> points; };
    std::vector<Section> sections;
    for (const auto& spline : input_profiles) {
        if (spline.points.size() < 2) continue;
        auto points = resample_profile(spline, options.columns, options.closed_profile);
        data::PcgVec3 center{};
        for (const auto& p : points) center = add(center, p);
        center = scale(center, 1.0 / static_cast<double>(points.size()));
        sections.push_back({axis_value(center, options.sort_axis), std::move(points)});
    }
    if (sections.size() < 2) return output;
    std::stable_sort(sections.begin(), sections.end(), [](const Section& a, const Section& b) {
        return a.order < b.order;
    });
    if (options.auto_align) {
        for (size_t i = 1; i < sections.size(); ++i) {
            if (options.closed_profile)
                align_closed_profile(sections[i].points, sections[i - 1].points);
            else if (squared_distance(sections[i - 1].points.front(), sections[i].points.back()) <
                     squared_distance(sections[i - 1].points.front(), sections[i].points.front()))
                std::reverse(sections[i].points.begin(), sections[i].points.end());
        }
    }

    for (const auto& section : sections)
        output.points_mut().insert(output.points_mut().end(), section.points.begin(), section.points.end());

    const int columns = options.columns;
    const int edges = options.closed_profile ? columns : columns - 1;
    for (int row = 0; row + 1 < static_cast<int>(sections.size()); ++row) {
        for (int col = 0; col < edges; ++col) {
            const int next = (col + 1) % columns;
            const int a = row * columns + col;
            const int b = row * columns + next;
            const int c = (row + 1) * columns + next;
            const int d = (row + 1) * columns + col;
            const int face_index = static_cast<int>(output.faces().size());
            output.faces_mut().push_back({a, b, c, d});
            output.groups().add(geometry::GroupDomain::Face, "side", face_index);
        }
    }
    if (options.closed_profile && options.cap_start) {
        std::vector<int> face;
        for (int i = columns - 1; i >= 0; --i) face.push_back(i);
        const int index = static_cast<int>(output.faces().size());
        output.faces_mut().push_back(std::move(face));
        output.groups().add(geometry::GroupDomain::Face, "cap_start", index);
    }
    if (options.closed_profile && options.cap_end) {
        std::vector<int> face;
        const int base = (static_cast<int>(sections.size()) - 1) * columns;
        for (int i = 0; i < columns; ++i) face.push_back(base + i);
        const int index = static_cast<int>(output.faces().size());
        output.faces_mut().push_back(std::move(face));
        output.groups().add(geometry::GroupDomain::Face, "cap_end", index);
    }
    if (options.shade_mode == "smooth")
        output.detail().shade_mode = data::ShadeMode::Smooth;
    else if (options.shade_mode == "flat")
        output.detail().shade_mode = data::ShadeMode::Flat;
    else
        output.detail().shade_mode = data::ShadeMode::Auto;
    output.detail().cusp_angle_deg = options.cusp_angle_deg;
    data::maintain_unshared_edge_group(output);
    return output;
}

data::PcgGeometry mirror_geometry(const data::PcgGeometry& input,
                                  const MirrorMeshOptions& options)
{
    data::PcgGeometry mirrored;
    std::vector<int> remap(input.points().size(), -1);
    for (size_t i = 0; i < input.points().size(); ++i) {
        auto p = input.points()[i];
        double* coordinate = &p.x;
        if (options.axis == "y" || options.axis == "Y") coordinate = &p.y;
        else if (options.axis == "z" || options.axis == "Z") coordinate = &p.z;
        *coordinate = options.offset * 2.0 - *coordinate;
        remap[i] = static_cast<int>(mirrored.points().size());
        mirrored.points_mut().push_back(p);
    }
    for (const auto& face : input.faces()) {
        std::vector<int> next;
        for (auto it = face.rbegin(); it != face.rend(); ++it)
            next.push_back(remap[static_cast<size_t>(*it)]);
        mirrored.faces_mut().push_back(std::move(next));
    }
    mirrored.detail() = input.detail();
    data::PcgGeometry result = options.merge_original ? data::merge_geometries(input, mirrored, "mirror_")
                                                      : std::move(mirrored);
    if (options.merge_original && options.weld_seam)
        result = fuse_geometry(result, FuseMeshOptions{options.weld_tolerance, true});
    data::maintain_unshared_edge_group(result);
    return result;
}

data::PcgGeometry fuse_geometry(const data::PcgGeometry& input,
                                const FuseMeshOptions& options)
{
    data::PcgGeometry output;
    output.detail() = input.detail();
    const double tolerance = std::max(0.00000001, options.tolerance);
    std::unordered_map<std::string, int> welded;
    std::vector<int> point_remap(input.points().size(), -1);
    for (size_t i = 0; i < input.points().size(); ++i) {
        const std::string key = position_key(input.points()[i], tolerance);
        const auto found = welded.find(key);
        if (found != welded.end()) {
            point_remap[i] = found->second;
        } else {
            const int index = static_cast<int>(output.points().size());
            welded.emplace(key, index);
            point_remap[i] = index;
            output.points_mut().push_back(input.points()[i]);
        }
    }

    std::vector<int> face_remap(input.faces().size(), -1);
    std::unordered_set<std::string> unique_faces;
    for (size_t face_index = 0; face_index < input.faces().size(); ++face_index) {
        std::vector<int> face;
        for (int old_index : input.faces()[face_index]) {
            const int index = point_remap[static_cast<size_t>(old_index)];
            if (face.empty() || face.back() != index) face.push_back(index);
        }
        if (face.size() > 1 && face.front() == face.back()) face.pop_back();
        std::unordered_set<int> distinct(face.begin(), face.end());
        if (options.remove_degenerate && distinct.size() < 3) continue;
        std::vector<int> canonical(face.begin(), face.end());
        std::sort(canonical.begin(), canonical.end());
        std::string key;
        for (int index : canonical) key += std::to_string(index) + ',';
        if (!unique_faces.insert(key).second) continue;
        face_remap[face_index] = static_cast<int>(output.faces().size());
        output.faces_mut().push_back(std::move(face));
    }

    for (const auto& name : input.groups().group_names(geometry::GroupDomain::Face)) {
        for (int old_face : input.groups().members(geometry::GroupDomain::Face, name)) {
            if (old_face >= 0 && static_cast<size_t>(old_face) < face_remap.size() && face_remap[old_face] >= 0)
                output.groups().add(geometry::GroupDomain::Face, name, face_remap[old_face]);
        }
    }
    data::maintain_unshared_edge_group(output);
    return output;
}

data::PcgGeometry poly_extrude_geometry(const data::PcgGeometry& input,
                                        const PolyExtrudeOptions& options)
{
    data::PcgGeometry output = input;
    std::unordered_set<int> selected;
    if (options.face_group.empty()) {
        for (size_t i = 0; i < input.faces().size(); ++i) selected.insert(static_cast<int>(i));
    } else {
        selected = input.groups().eval(geometry::GroupDomain::Face, options.face_group);
    }
    if (selected.empty()) return output;

    std::vector<std::vector<int>> faces;
    for (size_t i = 0; i < input.faces().size(); ++i) {
        if (selected.count(static_cast<int>(i)) == 0 || options.keep_original)
            faces.push_back(input.faces()[i]);
    }
    output.faces_mut() = std::move(faces);
    output.groups() = {};

    for (int selected_index : selected) {
        if (selected_index < 0 || static_cast<size_t>(selected_index) >= input.faces().size()) continue;
        const auto& base = input.faces()[static_cast<size_t>(selected_index)];
        if (base.size() < 3) continue;
        const auto center = face_center(input, base);
        const auto normal = face_normal(input, base);
        double average_radius = 0.0;
        for (int index : base) average_radius += length(sub(input.points()[static_cast<size_t>(index)], center));
        average_radius /= static_cast<double>(base.size());
        const double inset_scale = average_radius <= 1e-12
            ? 1.0 : std::max(0.001, 1.0 - options.inset / average_radius);

        std::vector<int> top;
        for (int index : base) {
            auto p = add(center, scale(sub(input.points()[static_cast<size_t>(index)], center), inset_scale));
            p = add(p, scale(normal, options.distance));
            top.push_back(static_cast<int>(output.points().size()));
            output.points_mut().push_back(p);
        }
        for (size_t edge = 0; edge < base.size(); ++edge) {
            const size_t next = (edge + 1) % base.size();
            const int face_index = static_cast<int>(output.faces().size());
            output.faces_mut().push_back({base[edge], base[next], top[next], top[edge]});
            output.groups().add(geometry::GroupDomain::Face, options.side_group, face_index);
        }
        const int top_index = static_cast<int>(output.faces().size());
        output.faces_mut().push_back(std::move(top));
        output.groups().add(geometry::GroupDomain::Face, options.top_group, top_index);
    }
    data::maintain_unshared_edge_group(output);
    return output;
}

data::PcgGeometry copy_geometry(const data::PcgGeometry& input,
                                const CopyMeshOptions& options)
{
    data::PcgGeometry output;
    const int count = std::max(1, options.count);
    const data::PcgVec3 center{options.center_x, options.center_y, options.center_z};
    for (int copy = 0; copy < count; ++copy) {
        data::PcgGeometry instance = input;
        const double t = count <= 1 ? 0.0 : static_cast<double>(copy) / static_cast<double>(count);
        for (auto& p : instance.points_mut()) {
            if (options.mode == "linear") {
                p.x += options.translate_x * copy;
                p.y += options.translate_y * copy;
                p.z += options.translate_z * copy;
            } else {
                p = rotate_about_axis(p, center, options.axis, options.angle * t * kPi / 180.0);
            }
        }
        output = data::merge_geometries(output, instance, "copy" + std::to_string(copy) + "_");
    }
    output.detail() = input.detail();
    data::maintain_unshared_edge_group(output);
    return output;
}

data::PcgGeometry shell_geometry(const data::PcgGeometry& input,
                                 const ShellMeshOptions& options)
{
    data::PcgGeometry output;
    if (input.points().empty() || input.faces().empty() || options.thickness <= 0.0)
        return output;

    std::vector<data::PcgVec3> normals(input.points().size());
    std::unordered_map<int64_t, int> edge_use_count;
    for (const auto& face : input.faces()) {
        if (face.size() < 3) continue;
        const auto normal = face_normal(input, face);
        for (int index : face)
            normals[static_cast<size_t>(index)] = add(normals[static_cast<size_t>(index)], normal);
        for (size_t edge = 0; edge < face.size(); ++edge) {
            const int a = face[edge];
            const int b = face[(edge + 1) % face.size()];
            edge_use_count[geometry::edge_key(a, b)]++;
        }
    }
    for (auto& normal : normals) normal = normalize(normal);

    double outer_distance = options.thickness * 0.5;
    double inner_distance = -options.thickness * 0.5;
    if (options.direction == "outward") {
        outer_distance = options.thickness;
        inner_distance = 0.0;
    } else if (options.direction == "inward") {
        outer_distance = 0.0;
        inner_distance = -options.thickness;
    }

    const int point_count = static_cast<int>(input.points().size());
    for (size_t i = 0; i < input.points().size(); ++i)
        output.points_mut().push_back(add(input.points()[i], scale(normals[i], outer_distance)));
    for (size_t i = 0; i < input.points().size(); ++i)
        output.points_mut().push_back(add(input.points()[i], scale(normals[i], inner_distance)));

    for (const auto& face : input.faces()) {
        const int outer_face = static_cast<int>(output.faces().size());
        output.faces_mut().push_back(face);
        output.groups().add(geometry::GroupDomain::Face, options.outer_group, outer_face);

        std::vector<int> inner;
        inner.reserve(face.size());
        for (auto it = face.rbegin(); it != face.rend(); ++it)
            inner.push_back(*it + point_count);
        const int inner_face = static_cast<int>(output.faces().size());
        output.faces_mut().push_back(std::move(inner));
        output.groups().add(geometry::GroupDomain::Face, options.inner_group, inner_face);
    }

    if (options.close_boundaries) {
        std::unordered_set<int64_t> emitted;
        for (const auto& face : input.faces()) {
            for (size_t edge = 0; edge < face.size(); ++edge) {
                const int a = face[edge];
                const int b = face[(edge + 1) % face.size()];
                const int64_t key = geometry::edge_key(a, b);
                if (edge_use_count[key] != 1 || !emitted.insert(key).second) continue;
                const int rim_face = static_cast<int>(output.faces().size());
                output.faces_mut().push_back({a, b, b + point_count, a + point_count});
                output.groups().add(geometry::GroupDomain::Face, options.rim_group, rim_face);
            }
        }
    }
    output.detail() = input.detail();
    data::maintain_unshared_edge_group(output);
    return output;
}

} // namespace pcg::internal::elements
