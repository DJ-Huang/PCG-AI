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

void apply_position_attribute_deltas(const data::PcgGeometry& source,
                                     data::PcgGeometry& destination,
                                     const std::vector<int>& destination_to_source)
{
    if (destination_to_source.size() != destination.points().size())
        return;
    for (const auto& name : destination.attributes().names(data::AttributeOwner::Point)) {
        auto* attribute = destination.attributes().find(data::AttributeOwner::Point, name);
        if (!attribute || attribute->schema().type != data::AttributeType::Float ||
            attribute->schema().tuple_size < 3 ||
            attribute->schema().transform_role != data::AttributeTransformRole::Position)
            continue;
        auto& values = attribute->float_values_mut();
        const size_t width = static_cast<size_t>(attribute->schema().tuple_size);
        for (size_t point = 0; point < destination.points().size(); ++point) {
            const int source_point = destination_to_source[point];
            if (source_point < 0 || static_cast<size_t>(source_point) >= source.points().size())
                continue;
            const auto delta = sub(destination.points()[point],
                                   source.points()[static_cast<size_t>(source_point)]);
            values[point * width] += delta.x;
            values[point * width + 1] += delta.y;
            values[point * width + 2] += delta.z;
        }
    }
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
    data::GeometryElementRemap topology_remap;
    std::vector<int> remap(input.points().size(), -1);
    for (size_t i = 0; i < input.points().size(); ++i) {
        auto p = input.points()[i];
        double* coordinate = &p.x;
        if (options.axis == "y" || options.axis == "Y") coordinate = &p.y;
        else if (options.axis == "z" || options.axis == "Z") coordinate = &p.z;
        *coordinate = options.offset * 2.0 - *coordinate;
        remap[i] = static_cast<int>(mirrored.points().size());
        mirrored.points_mut().push_back(p);
        topology_remap.points.push_back(static_cast<int>(i));
    }
    int source_corner_offset = 0;
    for (size_t face_index = 0; face_index < input.faces().size(); ++face_index) {
        const auto& face = input.faces()[face_index];
        std::vector<int> next;
        for (auto it = face.rbegin(); it != face.rend(); ++it) {
            next.push_back(remap[static_cast<size_t>(*it)]);
            const int local = static_cast<int>(std::distance(it, face.rend())) - 1;
            topology_remap.vertices.push_back(source_corner_offset + local);
        }
        mirrored.faces_mut().push_back(std::move(next));
        topology_remap.primitives.push_back(static_cast<int>(face_index));
        source_corner_offset += static_cast<int>(face.size());
    }
    data::propagate_geometry_data(input, mirrored, topology_remap);
    data::GeometryAffineTransform mirror_transform;
    if (options.axis == "y" || options.axis == "Y") {
        mirror_transform.linear[4] = -1.0;
        mirror_transform.translation.y = options.offset * 2.0;
    } else if (options.axis == "z" || options.axis == "Z") {
        mirror_transform.linear[8] = -1.0;
        mirror_transform.translation.z = options.offset * 2.0;
    } else {
        mirror_transform.linear[0] = -1.0;
        mirror_transform.translation.x = options.offset * 2.0;
    }
    data::transform_geometry_attributes(mirrored, mirror_transform);
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
    data::GeometryElementRemap topology_remap;
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
            topology_remap.points.push_back(static_cast<int>(i));
        }
    }

    std::unordered_set<std::string> unique_faces;
    int source_corner_offset = 0;
    for (size_t face_index = 0; face_index < input.faces().size(); ++face_index) {
        std::vector<int> face;
        std::vector<int> vertex_sources;
        for (size_t corner = 0; corner < input.faces()[face_index].size(); ++corner) {
            const int old_index = input.faces()[face_index][corner];
            const int index = point_remap[static_cast<size_t>(old_index)];
            if (face.empty() || face.back() != index) {
                face.push_back(index);
                vertex_sources.push_back(source_corner_offset + static_cast<int>(corner));
            }
        }
        if (face.size() > 1 && face.front() == face.back()) {
            face.pop_back();
            vertex_sources.pop_back();
        }
        std::unordered_set<int> distinct(face.begin(), face.end());
        if (options.remove_degenerate && distinct.size() < 3) {
            source_corner_offset += static_cast<int>(input.faces()[face_index].size());
            continue;
        }
        std::vector<int> canonical(face.begin(), face.end());
        std::sort(canonical.begin(), canonical.end());
        std::string key;
        for (int index : canonical) key += std::to_string(index) + ',';
        if (!unique_faces.insert(key).second) {
            source_corner_offset += static_cast<int>(input.faces()[face_index].size());
            continue;
        }
        output.faces_mut().push_back(std::move(face));
        topology_remap.primitives.push_back(static_cast<int>(face_index));
        topology_remap.vertices.insert(topology_remap.vertices.end(),
                                       vertex_sources.begin(), vertex_sources.end());
        source_corner_offset += static_cast<int>(input.faces()[face_index].size());
    }
    data::propagate_geometry_data(input, output, topology_remap);
    data::maintain_unshared_edge_group(output);
    return output;
}

data::PcgGeometry poly_extrude_geometry(const data::PcgGeometry& input,
                                        const PolyExtrudeOptions& options)
{
    data::PcgGeometry output;
    output.points_mut() = input.points();
    data::GeometryElementRemap topology_remap;
    topology_remap.points.reserve(input.points().size());
    for (size_t i = 0; i < input.points().size(); ++i)
        topology_remap.points.push_back(static_cast<int>(i));

    std::unordered_set<geometry::GroupId> selected;
    if (options.face_group.empty()) {
        for (size_t i = 0; i < input.faces().size(); ++i)
            selected.insert(static_cast<geometry::GroupId>(i));
    } else {
        selected = input.groups().eval(geometry::GroupDomain::Face, options.face_group);
    }
    if (selected.empty()) return input;

    // When a face group is set and keepOriginal is false, emit only the extruded
    // shell (selected tops → sides + new top). Unselected faces are dropped so
    // footprint buildings do not carry leftover pad sides/bottoms into Merge.
    const bool keep_unselected =
        options.face_group.empty() || options.keep_original;

    std::vector<int> source_corner_offsets(input.faces().size(), 0);
    int corner_cursor = 0;
    for (size_t i = 0; i < input.faces().size(); ++i) {
        source_corner_offsets[i] = corner_cursor;
        corner_cursor += static_cast<int>(input.faces()[i].size());
        const bool is_selected =
            selected.count(static_cast<geometry::GroupId>(i)) > 0;
        const bool keep_face =
            (is_selected && options.keep_original) || (!is_selected && keep_unselected);
        if (!keep_face)
            continue;
        output.faces_mut().push_back(input.faces()[i]);
        topology_remap.primitives.push_back(static_cast<int>(i));
        for (size_t corner = 0; corner < input.faces()[i].size(); ++corner)
            topology_remap.vertices.push_back(source_corner_offsets[i] +
                                              static_cast<int>(corner));
    }

    std::vector<std::pair<size_t, size_t>> generated_face_ranges;
    for (geometry::GroupId selected_id : selected) {
        const int selected_index = static_cast<int>(selected_id);
        if (selected_index < 0 || static_cast<size_t>(selected_index) >= input.faces().size()) continue;
        const auto& base = input.faces()[static_cast<size_t>(selected_index)];
        if (base.size() < 3) continue;
        const auto center = face_center(input, base);
        const auto normal = face_normal(input, base);
        double distance = options.distance;
        if (!options.distance_attribute.empty()) {
            if (const data::AttributeArray* attr = input.attributes().find(
                    data::AttributeOwner::Primitive, options.distance_attribute)) {
                if (attr->schema().type == data::AttributeType::Float &&
                    attr->schema().tuple_size >= 1 &&
                    static_cast<size_t>(selected_index) < attr->size()) {
                    distance = attr->float_values()[static_cast<size_t>(selected_index) *
                                                    static_cast<size_t>(attr->schema().tuple_size)];
                } else if (attr->schema().type == data::AttributeType::Int &&
                           static_cast<size_t>(selected_index) < attr->size()) {
                    distance = static_cast<double>(
                        attr->int_values()[static_cast<size_t>(selected_index)]);
                }
            }
        }
        double average_radius = 0.0;
        for (int index : base) average_radius += length(sub(input.points()[static_cast<size_t>(index)], center));
        average_radius /= static_cast<double>(base.size());
        const double inset_scale = average_radius <= 1e-12
            ? 1.0 : std::max(0.001, 1.0 - options.inset / average_radius);

        std::vector<int> top;
        for (int index : base) {
            auto p = add(center, scale(sub(input.points()[static_cast<size_t>(index)], center), inset_scale));
            p = add(p, scale(normal, distance));
            top.push_back(static_cast<int>(output.points().size()));
            output.points_mut().push_back(p);
            topology_remap.points.push_back(index);
        }
        generated_face_ranges.push_back({output.faces().size(), base.size()});
        for (size_t edge = 0; edge < base.size(); ++edge) {
            const size_t next = (edge + 1) % base.size();
            output.faces_mut().push_back({base[edge], base[next], top[next], top[edge]});
            topology_remap.primitives.push_back(selected_index);
            const int corner_base = source_corner_offsets[static_cast<size_t>(selected_index)];
            topology_remap.vertices.push_back(corner_base + static_cast<int>(edge));
            topology_remap.vertices.push_back(corner_base + static_cast<int>(next));
            topology_remap.vertices.push_back(corner_base + static_cast<int>(next));
            topology_remap.vertices.push_back(corner_base + static_cast<int>(edge));
        }
        output.faces_mut().push_back(std::move(top));
        topology_remap.primitives.push_back(selected_index);
        for (size_t corner = 0; corner < base.size(); ++corner)
            topology_remap.vertices.push_back(
                source_corner_offsets[static_cast<size_t>(selected_index)] +
                static_cast<int>(corner));
    }

    data::propagate_geometry_data(input, output, topology_remap);

    // Generated top points inherit source point attributes, then position-role
    // attributes follow the same displacement as the authoritative point.
    apply_position_attribute_deltas(input, output, topology_remap.points);

    for (const auto& [first_face, side_count] : generated_face_ranges) {
        size_t generated_face = first_face;
        for (size_t side = 0; side < side_count; ++side)
            output.groups().add(geometry::GroupDomain::Face, options.side_group,
                                static_cast<geometry::GroupId>(generated_face++));
        output.groups().add(geometry::GroupDomain::Face, options.top_group,
                            static_cast<geometry::GroupId>(generated_face++));
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
        data::GeometryAffineTransform transform;
        if (options.mode == "linear") {
            transform.translation = {options.translate_x * copy,
                                     options.translate_y * copy,
                                     options.translate_z * copy};
        } else {
            const double angle = options.angle * t * kPi / 180.0;
            const auto origin = rotate_about_axis({0.0, 0.0, 0.0}, center, options.axis, angle);
            const auto x = rotate_about_axis({1.0, 0.0, 0.0}, center, options.axis, angle);
            const auto y = rotate_about_axis({0.0, 1.0, 0.0}, center, options.axis, angle);
            const auto z = rotate_about_axis({0.0, 0.0, 1.0}, center, options.axis, angle);
            transform.linear = {
                x.x - origin.x, y.x - origin.x, z.x - origin.x,
                x.y - origin.y, y.y - origin.y, z.y - origin.y,
                x.z - origin.z, y.z - origin.z, z.z - origin.z,
            };
            transform.translation = origin;
        }
        for (auto& point : instance.points_mut())
            point = data::transform_position(transform, point);
        data::transform_geometry_attributes(instance, transform);
        // Copy SOP semantics union same-named groups across instances. Per-copy
        // prefixes would silently break downstream group selectors.
        output = data::merge_geometries(output, instance);
    }
    output.detail() = input.detail();
    data::maintain_unshared_edge_group(output);
    return output;
}

data::PcgGeometry shell_geometry(const data::PcgGeometry& input,
                                 const ShellMeshOptions& options)
{
    data::PcgGeometry output;
    data::GeometryElementRemap topology_remap;
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
    for (size_t i = 0; i < input.points().size(); ++i) {
        output.points_mut().push_back(add(input.points()[i], scale(normals[i], outer_distance)));
        topology_remap.points.push_back(static_cast<int>(i));
    }
    for (size_t i = 0; i < input.points().size(); ++i) {
        output.points_mut().push_back(add(input.points()[i], scale(normals[i], inner_distance)));
        topology_remap.points.push_back(static_cast<int>(i));
    }

    std::vector<int> source_corner_offsets(input.faces().size(), 0);
    int source_corner_cursor = 0;
    for (size_t face_index = 0; face_index < input.faces().size(); ++face_index) {
        const auto& face = input.faces()[face_index];
        source_corner_offsets[face_index] = source_corner_cursor;
        const int outer_face = static_cast<int>(output.faces().size());
        output.faces_mut().push_back(face);
        output.groups().add(geometry::GroupDomain::Face, options.outer_group, outer_face);
        topology_remap.primitives.push_back(static_cast<int>(face_index));
        for (size_t corner = 0; corner < face.size(); ++corner)
            topology_remap.vertices.push_back(source_corner_cursor + static_cast<int>(corner));

        std::vector<int> inner;
        inner.reserve(face.size());
        for (auto it = face.rbegin(); it != face.rend(); ++it) {
            inner.push_back(*it + point_count);
            const int local = static_cast<int>(std::distance(it, face.rend())) - 1;
            topology_remap.vertices.push_back(source_corner_cursor + local);
        }
        const int inner_face = static_cast<int>(output.faces().size());
        output.faces_mut().push_back(std::move(inner));
        output.groups().add(geometry::GroupDomain::Face, options.inner_group, inner_face);
        topology_remap.primitives.push_back(static_cast<int>(face_index));
        source_corner_cursor += static_cast<int>(face.size());
    }

    if (options.close_boundaries) {
        std::unordered_set<int64_t> emitted;
        for (size_t face_index = 0; face_index < input.faces().size(); ++face_index) {
            const auto& face = input.faces()[face_index];
            for (size_t edge = 0; edge < face.size(); ++edge) {
                const int a = face[edge];
                const int b = face[(edge + 1) % face.size()];
                const int64_t key = geometry::edge_key(a, b);
                if (edge_use_count[key] != 1 || !emitted.insert(key).second) continue;
                const int rim_face = static_cast<int>(output.faces().size());
                output.faces_mut().push_back({a, b, b + point_count, a + point_count});
                output.groups().add(geometry::GroupDomain::Face, options.rim_group, rim_face);
                topology_remap.primitives.push_back(static_cast<int>(face_index));
                const int source_corner = source_corner_offsets[face_index];
                const int next = static_cast<int>((edge + 1) % face.size());
                topology_remap.vertices.push_back(source_corner + static_cast<int>(edge));
                topology_remap.vertices.push_back(source_corner + next);
                topology_remap.vertices.push_back(source_corner + next);
                topology_remap.vertices.push_back(source_corner + static_cast<int>(edge));
            }
        }
    }
    data::propagate_geometry_data(input, output, topology_remap);
    apply_position_attribute_deltas(input, output, topology_remap.points);
    data::maintain_unshared_edge_group(output);
    return output;
}

namespace {

data::PcgVec3 axis_unit(const std::string& axis)
{
    if (axis == "x") return {1.0, 0.0, 0.0};
    if (axis == "y") return {0.0, 1.0, 0.0};
    return {0.0, 0.0, 1.0};
}

double graded_t(int r, int rows)
{
    if (rows <= 0) return 0.0;
    return 0.5 - 0.5 * std::cos(kPi * (static_cast<double>(r) / static_cast<double>(rows)));
}

/// Half-thickness at outline parameter t∈[0,1] and station xf∈[0,1] (img2threejs zAt).
double profile_half_z(double t, double xf, double half_z, const OutlineSolidOptions& options)
{
    const double h = std::max(0.0, half_z);
    if (h <= 0.0) return 0.0;
    const std::string& mode = options.profile_mode;
    if (mode == "slab") {
        const double roll = std::max(1e-6, options.handle_roll_frac);
        const double d = std::min(t, 1.0 - t) / roll;
        if (d >= 1.0) return h;
        const double k = 1.0 - d;
        const double edge = std::clamp(options.edge_frac, 0.0, 1.0);
        return h * (edge + (1.0 - edge) * std::sqrt(std::max(0.0, 1.0 - k * k)));
    }
    if (mode == "blade") {
        const double sr = std::max(1e-6, options.spine_roll_frac);
        const double gs = std::clamp(options.grind_start_frac, sr, 1.0);
        const double eb = std::clamp(options.edge_bevel_frac, gs, 1.0);
        const double edge = std::clamp(options.edge_frac, 0.0, 1.0);
        if (t < sr) {
            const double k = 1.0 - t / sr;
            return h * (edge + (1.0 - edge) * std::sqrt(std::max(0.0, 1.0 - k * k)));
        }
        if (t < gs)
            return h * (1.0 - 0.12 * (t - sr) / std::max(gs - sr, 1e-4));
        if (t < eb)
            return std::max(0.0, h * (0.88 - 0.74 * (t - gs) / std::max(eb - gs, 1e-4)));
        return std::max(0.0, h * (0.14 - 0.115 * (t - eb) / std::max(1.0 - eb, 1e-4)));
    }
    (void)xf;
    return h;
}

data::PcgGeometry outline_solid_from_columns(const std::vector<OutlineColumn>& columns,
                                             const OutlineSolidOptions& options)
{
    data::PcgGeometry output;
    if (columns.size() < 2) return output;

    const int cols = static_cast<int>(columns.size());
    // At least one interior band so spine/edge rows produce face quads.
    const int rows = std::max(1, options.rows);
    const int row_count = rows + 1; // r = 0..rows
    const data::PcgVec3 axis = axis_unit(options.thickness_axis);
    const double x0 = columns.front().x;
    const double x1 = columns.back().x;
    const double span = std::max(std::fabs(x1 - x0), 1e-6);
    const int face_verts = cols * row_count;

    auto at = [&](int side, int c, int r) { return side * face_verts + c * row_count + r; };

    // side0 = +axis (front), side1 = -axis (back)
    for (int side = 0; side < 2; ++side) {
        const double sign = side == 0 ? 1.0 : -1.0;
        for (int c = 0; c < cols; ++c) {
            const auto& col = columns[static_cast<size_t>(c)];
            const double xf = (col.x - x0) / span;
            for (int r = 0; r < row_count; ++r) {
                const double t = graded_t(r, rows);
                const double y = col.y_top + (col.y_bot - col.y_top) * t;
                const double hz = profile_half_z(t, xf, col.half_z, options);
                data::PcgVec3 p{col.x, y, 0.0};
                // Place XY in the plane perpendicular to thicknessAxis.
                if (options.thickness_axis == "x")
                    p = {0.0, col.x, y};
                else if (options.thickness_axis == "y")
                    p = {col.x, 0.0, y};
                output.points_mut().push_back(add(p, scale(axis, sign * hz)));
            }
        }
    }

    // Broad faces as quads (triangulate later). Front: +axis; back: reverse winding.
    for (int side = 0; side < 2; ++side) {
        for (int c = 0; c < cols - 1; ++c) {
            for (int r = 0; r < rows; ++r) {
                const int a = at(side, c, r);
                const int b = at(side, c + 1, r);
                const int a1 = at(side, c, r + 1);
                const int b1 = at(side, c + 1, r + 1);
                const int face = static_cast<int>(output.faces().size());
                if (side == 0)
                    output.faces_mut().push_back({a, a1, b1, b});
                else
                    output.faces_mut().push_back({a, b, b1, a1});
                output.groups().add(geometry::GroupDomain::Face,
                                    side == 0 ? options.front_group : options.back_group, face);
            }
        }
    }

    // Rim: spine (r=0), edge (r=rows), plus butt/tip caps at c=0 and c=cols-1.
    auto add_rim = [&](int a, int b, int c, int d) {
        const int face = static_cast<int>(output.faces().size());
        output.faces_mut().push_back({a, b, c, d});
        output.groups().add(geometry::GroupDomain::Face, options.rim_group, face);
    };
    for (int c = 0; c < cols - 1; ++c) {
        // Spine (+Y / r=0): outward
        add_rim(at(0, c, 0), at(0, c + 1, 0), at(1, c + 1, 0), at(1, c, 0));
        // Cutting edge (r=rows)
        add_rim(at(0, c, rows), at(1, c, rows), at(1, c + 1, rows), at(0, c + 1, rows));
    }
    // Butt / tip strips along rows
    for (int r = 0; r < rows; ++r) {
        add_rim(at(0, 0, r), at(1, 0, r), at(1, 0, r + 1), at(0, 0, r + 1));
        add_rim(at(0, cols - 1, r), at(0, cols - 1, r + 1), at(1, cols - 1, r + 1),
                at(1, cols - 1, r));
    }

    data::maintain_unshared_edge_group(output);
    return output;
}

} // namespace

std::vector<OutlineColumn> columns_from_spine_edge(const data::PcgSpline& spine,
                                                   const data::PcgSpline& edge,
                                                   double fallback_half_z)
{
    std::vector<OutlineColumn> columns;
    if (spine.points.size() < 2 || spine.points.size() != edge.points.size())
        return columns;
    columns.reserve(spine.points.size());
    const double fb = fallback_half_z > 0.0 ? fallback_half_z : 0.0;
    for (size_t i = 0; i < spine.points.size(); ++i) {
        const auto& s = spine.points[i];
        const auto& e = edge.points[i];
        OutlineColumn col;
        col.x = s.x;
        col.y_top = s.y;
        col.y_bot = e.y;
        // Encode half-thickness in Z of the station polylines (XY = silhouette).
        if (s.z > 0.0)
            col.half_z = s.z;
        else if (e.z > 0.0)
            col.half_z = e.z;
        else
            col.half_z = fb;
        columns.push_back(col);
    }
    return columns;
}

data::PcgGeometry outline_solid_from_spline(const data::PcgSpline& outline,
                                            const OutlineSolidOptions& options)
{
    if (!options.columns.empty())
        return outline_solid_from_columns(options.columns, options);

    data::PcgGeometry output;
    if (outline.points.size() < 3)
        return output;

    std::vector<data::PcgVec3> ring;
    ring.reserve(outline.points.size());
    for (const auto& pt : outline.points)
        ring.push_back({pt.x, pt.y, pt.z});

    // Drop duplicate closing vertex on closed polylines.
    if (ring.size() >= 2) {
        const auto& a = ring.front();
        const auto& b = ring.back();
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        const double dz = a.z - b.z;
        if (dx * dx + dy * dy + dz * dz <= 1e-16)
            ring.pop_back();
    }
    if (ring.size() < 3)
        return output;

    const int n = static_cast<int>(ring.size());
    std::vector<double> halves(static_cast<size_t>(n), options.thickness * 0.5);
    if (!options.thickness_samples.empty()) {
        if (static_cast<int>(options.thickness_samples.size()) != n)
            return output;
        for (int i = 0; i < n; ++i)
            halves[static_cast<size_t>(i)] =
                options.thickness_samples[static_cast<size_t>(i)] * 0.5;
    }
    bool any_positive = false;
    for (double h : halves) {
        if (h > 0.0) {
            any_positive = true;
            break;
        }
    }
    if (!any_positive)
        return output;

    const data::PcgVec3 axis = axis_unit(options.thickness_axis);

    // Front (+half) then back (-half); rim shares these welded point indices.
    for (int i = 0; i < n; ++i)
        output.points_mut().push_back(add(ring[static_cast<size_t>(i)],
                                          scale(axis, halves[static_cast<size_t>(i)])));
    for (int i = 0; i < n; ++i)
        output.points_mut().push_back(add(ring[static_cast<size_t>(i)],
                                          scale(axis, -halves[static_cast<size_t>(i)])));

    std::vector<int> front(static_cast<size_t>(n));
    std::vector<int> back(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        front[static_cast<size_t>(i)] = i;
        // Reverse winding on back so normals face outward.
        back[static_cast<size_t>(i)] = n + (n - 1 - i);
    }

    const int front_face = static_cast<int>(output.faces().size());
    output.faces_mut().push_back(front);
    output.groups().add(geometry::GroupDomain::Face, options.front_group, front_face);

    const int back_face = static_cast<int>(output.faces().size());
    output.faces_mut().push_back(back);
    output.groups().add(geometry::GroupDomain::Face, options.back_group, back_face);

    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const int rim_face = static_cast<int>(output.faces().size());
        // Outward rim for CCW outline (viewed along +thicknessAxis):
        // front-i → back-i → back-j → front-j.
        output.faces_mut().push_back({i, n + i, n + j, j});
        output.groups().add(geometry::GroupDomain::Face, options.rim_group, rim_face);
    }

    data::maintain_unshared_edge_group(output);
    return output;
}

} // namespace pcg::internal::elements
