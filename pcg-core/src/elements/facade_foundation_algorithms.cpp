#include "elements/facade_foundation_algorithms.hpp"

#include "geometry/bmesh.hpp"
#include "geometry/group_table.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace pcg::internal::elements {
namespace {

data::PcgVec3 face_centroid(const data::PcgGeometry& geometry, const std::vector<int>& face)
{
    data::PcgVec3 center{};
    if (face.empty())
        return center;
    for (int index : face) {
        const auto& p = geometry.points()[static_cast<size_t>(index)];
        center.x += p.x;
        center.y += p.y;
        center.z += p.z;
    }
    const double inv = 1.0 / static_cast<double>(face.size());
    center.x *= inv;
    center.y *= inv;
    center.z *= inv;
    return center;
}

double plane_side(const data::PcgVec3& point, const ClipOptions& options)
{
    const double dx = point.x - options.origin_x;
    const double dy = point.y - options.origin_y;
    const double dz = point.z - options.origin_z;
    return dx * options.normal_x + dy * options.normal_y + dz * options.normal_z;
}

} // namespace

data::PcgGeometry primitive_transform_geometry(const data::PcgGeometry& input,
                                               const PrimitiveTransformOptions& options)
{
    data::PcgGeometry output;
    if (input.faces().empty())
        return input;

    std::unordered_set<geometry::GroupId> selected;
    if (options.face_group.empty()) {
        for (size_t i = 0; i < input.faces().size(); ++i)
            selected.insert(static_cast<geometry::GroupId>(i));
    } else {
        selected = input.groups().eval(geometry::GroupDomain::Face, options.face_group);
    }
    if (selected.empty())
        return input;

    // Independent per-face scale about centroid: duplicate verts so shared edges
    // shrink independently (Houdini Primitive SOP lot-inset pattern).
    data::GeometryElementRemap remap;
    for (geometry::GroupId face_id : selected) {
        const int fi = static_cast<int>(face_id);
        if (fi < 0 || static_cast<size_t>(fi) >= input.faces().size())
            continue;
        const auto& face = input.faces()[static_cast<size_t>(fi)];
        if (face.size() < 3)
            continue;
        const data::PcgVec3 center = face_centroid(input, face);
        std::vector<int> remapped;
        remapped.reserve(face.size());
        for (int pi : face) {
            const auto& src = input.points()[static_cast<size_t>(pi)];
            data::PcgVec3 scaled{
                center.x + (src.x - center.x) * options.scale,
                center.y + (src.y - center.y) * options.scale,
                center.z + (src.z - center.z) * options.scale,
            };
            remapped.push_back(static_cast<int>(output.points().size()));
            output.points_mut().push_back(scaled);
            remap.points.push_back(pi);
            remap.vertices.push_back(-1);
        }
        remap.primitives.push_back(fi);
        output.faces_mut().push_back(std::move(remapped));
    }

    // Keep unselected faces with shared topology when a face group is used.
    if (!options.face_group.empty()) {
        std::unordered_map<int, int> point_map;
        for (size_t fi = 0; fi < input.faces().size(); ++fi) {
            if (selected.count(static_cast<geometry::GroupId>(fi)) > 0)
                continue;
            const auto& face = input.faces()[fi];
            std::vector<int> remapped;
            remapped.reserve(face.size());
            for (int pi : face) {
                auto it = point_map.find(pi);
                int mapped = -1;
                if (it == point_map.end()) {
                    mapped = static_cast<int>(output.points().size());
                    point_map.emplace(pi, mapped);
                    output.points_mut().push_back(input.points()[static_cast<size_t>(pi)]);
                    remap.points.push_back(pi);
                } else {
                    mapped = it->second;
                }
                remapped.push_back(mapped);
                remap.vertices.push_back(-1);
            }
            remap.primitives.push_back(static_cast<int>(fi));
            output.faces_mut().push_back(std::move(remapped));
        }
    }

    data::propagate_geometry_data(input, output, remap);
    data::maintain_unshared_edge_group(output);
    return output;
}

data::PcgSplineData convert_line_geometry(const data::PcgGeometry& input,
                                          const ConvertLineOptions& options)
{
    data::PcgSplineData output;
    if (input.points().empty() || input.faces().empty())
        return output;

    std::unordered_map<int64_t, int> edge_face_count;
    std::unordered_map<int64_t, std::pair<int, int>> edge_endpoints;
    for (const auto& face : input.faces()) {
        if (face.size() < 2)
            continue;
        for (size_t i = 0; i < face.size(); ++i) {
            const int a = face[i];
            const int b = face[(i + 1) % face.size()];
            if (a < 0 || b < 0)
                continue;
            const int64_t key = geometry::edge_group_id(a, b);
            ++edge_face_count[key];
            edge_endpoints.emplace(key, std::make_pair(std::min(a, b), std::max(a, b)));
        }
    }

    std::unordered_set<int64_t> allowed;
    const bool filter_group = options.mode == "group" || !options.edge_group.empty();
    if (filter_group && !options.edge_group.empty()) {
        const auto members =
            input.groups().members(geometry::GroupDomain::Edge, options.edge_group);
        for (geometry::GroupId id : members)
            allowed.insert(id);
    }

    for (const auto& entry : edge_endpoints) {
        const int64_t key = entry.first;
        if (options.mode == "unshared" && edge_face_count[key] > 1)
            continue;
        if (filter_group && allowed.count(key) == 0)
            continue;

        const int a_idx = entry.second.first;
        const int b_idx = entry.second.second;
        if (static_cast<size_t>(a_idx) >= input.points().size() ||
            static_cast<size_t>(b_idx) >= input.points().size())
            continue;

        data::PcgSpline spline;
        spline.closed = false;
        const auto& a = input.points()[static_cast<size_t>(a_idx)];
        const auto& b = input.points()[static_cast<size_t>(b_idx)];
        spline.points.push_back({a.x, a.y, a.z});
        spline.points.push_back({b.x, b.y, b.z});
        output.splines_mut().push_back(std::move(spline));
    }
    return output;
}

data::PcgPointData extract_centroid_geometry(const data::PcgGeometry& input,
                                             const ExtractCentroidOptions& options)
{
    data::PcgPointData output;
    if (options.method == "points") {
        data::PcgVec3 center{};
        if (input.points().empty())
            return output;
        for (const auto& p : input.points()) {
            center.x += p.x;
            center.y += p.y;
            center.z += p.z;
        }
        const double inv = 1.0 / static_cast<double>(input.points().size());
        output.add_point({center.x * inv, center.y * inv, center.z * inv});
        return output;
    }

    std::unordered_set<geometry::GroupId> selected;
    if (options.face_group.empty()) {
        for (size_t i = 0; i < input.faces().size(); ++i)
            selected.insert(static_cast<geometry::GroupId>(i));
    } else {
        selected = input.groups().eval(geometry::GroupDomain::Face, options.face_group);
    }

    for (geometry::GroupId face_id : selected) {
        const int fi = static_cast<int>(face_id);
        if (fi < 0 || static_cast<size_t>(fi) >= input.faces().size())
            continue;
        const auto center = face_centroid(input, input.faces()[static_cast<size_t>(fi)]);
        data::PcgPoint point{center.x, center.y, center.z};
        point.attributes["primnum"] = fi;
        output.add_point(std::move(point));
    }
    return output;
}

data::PcgGeometry group_transfer_geometry(const data::PcgGeometry& target,
                                          const data::PcgGeometry& source,
                                          const GroupTransferOptions& options)
{
    data::PcgGeometry output = target;
    if (options.group_name.empty())
        return output;

    const geometry::GroupDomain domain = options.domain == "point"
        ? geometry::GroupDomain::Point
        : geometry::GroupDomain::Face;
    output.groups().clear_group(domain, options.group_name);

    const auto source_members = source.groups().members(domain, options.group_name);
    if (source_members.empty())
        return output;

    const double max_dist2 = options.distance * options.distance;

    if (domain == geometry::GroupDomain::Face) {
        std::vector<data::PcgVec3> source_centers;
        source_centers.reserve(source_members.size());
        for (geometry::GroupId id : source_members) {
            if (id < 0 || static_cast<size_t>(id) >= source.faces().size())
                continue;
            source_centers.push_back(face_centroid(source, source.faces()[static_cast<size_t>(id)]));
        }
        for (size_t fi = 0; fi < output.faces().size(); ++fi) {
            const auto center = face_centroid(output, output.faces()[fi]);
            for (const auto& sc : source_centers) {
                const double dx = center.x - sc.x;
                const double dy = center.y - sc.y;
                const double dz = center.z - sc.z;
                if (dx * dx + dy * dy + dz * dz <= max_dist2) {
                    output.groups().add(domain, options.group_name,
                                        static_cast<geometry::GroupId>(fi));
                    break;
                }
            }
        }
    } else {
        for (size_t pi = 0; pi < output.points().size(); ++pi) {
            const auto& tp = output.points()[pi];
            for (geometry::GroupId id : source_members) {
                if (id < 0 || static_cast<size_t>(id) >= source.points().size())
                    continue;
                const auto& sp = source.points()[static_cast<size_t>(id)];
                const double dx = tp.x - sp.x;
                const double dy = tp.y - sp.y;
                const double dz = tp.z - sp.z;
                if (dx * dx + dy * dy + dz * dz <= max_dist2) {
                    output.groups().add(domain, options.group_name,
                                        static_cast<geometry::GroupId>(pi));
                    break;
                }
            }
        }
    }
    return output;
}

data::PcgGeometry clip_geometry(const data::PcgGeometry& input, const ClipOptions& options)
{
    std::vector<bool> keep_points(input.points().size(), false);
    for (size_t i = 0; i < input.points().size(); ++i) {
        const double side = plane_side(input.points()[i], options);
        keep_points[i] = options.keep_positive ? side >= -1e-12 : side <= 1e-12;
    }

    std::unordered_set<int> keep_faces;
    for (size_t fi = 0; fi < input.faces().size(); ++fi) {
        bool all_keep = true;
        for (int pi : input.faces()[fi]) {
            if (pi < 0 || static_cast<size_t>(pi) >= keep_points.size() || !keep_points[static_cast<size_t>(pi)]) {
                all_keep = false;
                break;
            }
        }
        if (all_keep)
            keep_faces.insert(static_cast<int>(fi));
    }
    return data::extract_faces(input, keep_faces);
}

std::vector<data::PcgGeometry> foreach_pieces(const data::PcgGeometry& input,
                                              const std::string& method,
                                              const std::string& piece_attribute)
{
    std::vector<data::PcgGeometry> pieces;
    if (input.faces().empty())
        return pieces;

    if (method == "piece") {
        const data::AttributeArray* attr =
            input.attributes().find(data::AttributeOwner::Primitive, piece_attribute);
        std::unordered_map<int64_t, std::unordered_set<int>> clusters;
        if (attr && attr->schema().type == data::AttributeType::Int) {
            const auto& values = attr->int_values();
            for (size_t fi = 0; fi < input.faces().size(); ++fi) {
                const int64_t key = fi < values.size() ? values[fi] : 0;
                clusters[key].insert(static_cast<int>(fi));
            }
        } else {
            for (size_t fi = 0; fi < input.faces().size(); ++fi)
                clusters[0].insert(static_cast<int>(fi));
        }
        pieces.reserve(clusters.size());
        for (const auto& entry : clusters)
            pieces.push_back(data::extract_faces(input, entry.second));
        return pieces;
    }

    // primitive (default): one piece per face
    pieces.reserve(input.faces().size());
    for (size_t fi = 0; fi < input.faces().size(); ++fi)
        pieces.push_back(data::extract_faces(input, {static_cast<int>(fi)}));
    return pieces;
}

} // namespace pcg::internal::elements
