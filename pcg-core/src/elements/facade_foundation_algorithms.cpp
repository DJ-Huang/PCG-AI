#include "elements/facade_foundation_algorithms.hpp"

#include "geometry/bmesh.hpp"
#include "geometry/group_table.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
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
        const auto& face = input.faces()[static_cast<size_t>(fi)];
        const auto center = face_centroid(input, face);
        data::PcgPoint point{center.x, center.y, center.z};
        point.attributes["primnum"] = fi;

        // Horizontal AABB of the face — used to scale building footprints to lots.
        if (!face.empty()) {
            double min_x = 0.0, max_x = 0.0, min_z = 0.0, max_z = 0.0;
            bool first = true;
            for (int index : face) {
                if (index < 0 || static_cast<size_t>(index) >= input.points().size())
                    continue;
                const auto& p = input.points()[static_cast<size_t>(index)];
                if (first) {
                    min_x = max_x = p.x;
                    min_z = max_z = p.z;
                    first = false;
                } else {
                    min_x = std::min(min_x, p.x);
                    max_x = std::max(max_x, p.x);
                    min_z = std::min(min_z, p.z);
                    max_z = std::max(max_z, p.z);
                }
            }
            if (!first) {
                point.attributes["lotSizeX"] = std::max(0.0, max_x - min_x);
                point.attributes["lotSizeZ"] = std::max(0.0, max_z - min_z);
            }
        }

        output.add_point(std::move(point));
    }
    return output;
}

data::PcgGeometry group_transfer_geometry(const data::PcgGeometry& target,
                                          const data::PcgGeometry& source,
                                          const GroupTransferOptions& options)
{
    data::PcgGeometry output = target;

    const auto trim = [](std::string s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.erase(s.begin());
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.pop_back();
        return s;
    };

    const auto split_tokens = [&](const std::string& pattern) {
        std::vector<std::string> tokens;
        std::string current;
        for (char ch : pattern) {
            if (ch == ',' || ch == ';' || std::isspace(static_cast<unsigned char>(ch))) {
                if (!current.empty()) {
                    tokens.push_back(current);
                    current.clear();
                }
            } else {
                current.push_back(ch);
            }
        }
        if (!current.empty())
            tokens.push_back(current);
        return tokens;
    };

    const auto glob_match = [](const std::string& name, const std::string& pattern) {
        if (pattern == "*")
            return true;
        const auto star = pattern.find('*');
        if (star == std::string::npos)
            return name == pattern;
        if (pattern.size() == 1)
            return true;
        if (star == 0) {
            const std::string suffix = pattern.substr(1);
            return name.size() >= suffix.size() &&
                   name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
        }
        if (star + 1 == pattern.size()) {
            const std::string prefix = pattern.substr(0, star);
            return name.size() >= prefix.size() && name.compare(0, prefix.size(), prefix) == 0;
        }
        const std::string prefix = pattern.substr(0, star);
        const std::string suffix = pattern.substr(star + 1);
        return name.size() >= prefix.size() + suffix.size() &&
               name.compare(0, prefix.size(), prefix) == 0 &&
               name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    };

    const auto select_group_names = [&](geometry::GroupDomain domain,
                                        const std::string& pattern) {
        const auto all = source.groups().group_names(domain);
        const std::string trimmed = trim(pattern);
        if (trimmed.empty() || trimmed == "*")
            return all;
        const auto tokens = split_tokens(trimmed);
        std::vector<std::string> selected;
        for (const auto& name : all) {
            for (const auto& token : tokens) {
                if (glob_match(name, token)) {
                    selected.push_back(name);
                    break;
                }
            }
        }
        return selected;
    };

    const auto resolve_dest_name = [&](geometry::GroupDomain domain,
                                       const std::string& source_name,
                                       const std::string& prefix) -> std::string {
        const std::string proposed = prefix + source_name;
        const bool exists = output.groups().has_group(domain, proposed);
        if (!exists)
            return proposed;
        if (options.group_name_conflict == "overwrite")
            return proposed;
        if (options.group_name_conflict == "addSuffix") {
            for (int suffix = 2; suffix < 100000; ++suffix) {
                const std::string candidate = proposed + std::to_string(suffix);
                if (!output.groups().has_group(domain, candidate))
                    return candidate;
            }
            return proposed + "_dup";
        }
        // skip
        return {};
    };

    const auto dist2 = [](const data::PcgVec3& a, const data::PcgVec3& b) {
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        const double dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    };

    const auto find_closest = [&](const std::vector<data::PcgVec3>& source_centers,
                                  const data::PcgVec3& query, int& out_index,
                                  double& out_dist2) {
        out_index = -1;
        out_dist2 = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i < source_centers.size(); ++i) {
            const double d2 = dist2(query, source_centers[i]);
            if (d2 < out_dist2) {
                out_dist2 = d2;
                out_index = static_cast<int>(i);
            }
        }
    };

    const auto within_threshold = [&](double d2) {
        if (!options.enable_distance_threshold)
            return true;
        const double max_d = std::max(0.0, options.distance_threshold);
        return d2 <= max_d * max_d;
    };

    const auto transfer_domain =
        [&](geometry::GroupDomain domain, bool enabled, const std::string& pattern,
            const std::string& prefix,
            const std::function<std::vector<data::PcgVec3>(const data::PcgGeometry&)>&
                collect_centers,
            const std::function<std::vector<geometry::GroupId>(const data::PcgGeometry&)>&
                collect_ids) {
            if (!enabled)
                return;

            const auto source_names = select_group_names(domain, pattern);
            if (source_names.empty())
                return;

            const auto source_centers = collect_centers(source);
            const auto source_ids = collect_ids(source);
            if (source_centers.empty() || source_centers.size() != source_ids.size())
                return;

            const auto dest_centers = collect_centers(output);
            const auto dest_ids = collect_ids(output);
            if (dest_centers.size() != dest_ids.size())
                return;

            // Closest-source map per destination element (Houdini proximity).
            std::vector<int> closest(dest_centers.size(), -1);
            for (size_t di = 0; di < dest_centers.size(); ++di) {
                int idx = -1;
                double d2 = 0.0;
                find_closest(source_centers, dest_centers[di], idx, d2);
                if (idx >= 0 && within_threshold(d2))
                    closest[di] = idx;
            }

            for (const auto& source_name : source_names) {
                const std::string dest_name = resolve_dest_name(domain, source_name, prefix);
                if (dest_name.empty())
                    continue;

                if (options.group_name_conflict == "overwrite" ||
                    !output.groups().has_group(domain, dest_name)) {
                    output.groups().clear_group(domain, dest_name);
                }

                const auto& members = source.groups().members(domain, source_name);
                size_t added = 0;
                for (size_t di = 0; di < closest.size(); ++di) {
                    const int si = closest[di];
                    if (si < 0)
                        continue;
                    if (members.count(source_ids[static_cast<size_t>(si)]) == 0)
                        continue;
                    output.groups().add(domain, dest_name, dest_ids[di]);
                    ++added;
                }

                if (added == 0) {
                    if (options.create_empty_groups)
                        output.groups().ensure_group(domain, dest_name);
                    else
                        output.groups().clear_group(domain, dest_name);
                }
            }
        };

    const auto face_centers = [](const data::PcgGeometry& geo) {
        std::vector<data::PcgVec3> centers;
        centers.reserve(geo.faces().size());
        for (const auto& face : geo.faces())
            centers.push_back(face_centroid(geo, face));
        return centers;
    };
    const auto face_ids = [](const data::PcgGeometry& geo) {
        std::vector<geometry::GroupId> ids;
        ids.reserve(geo.faces().size());
        for (size_t i = 0; i < geo.faces().size(); ++i)
            ids.push_back(static_cast<geometry::GroupId>(i));
        return ids;
    };

    const auto point_centers = [](const data::PcgGeometry& geo) {
        return geo.points();
    };
    const auto point_ids = [](const data::PcgGeometry& geo) {
        std::vector<geometry::GroupId> ids;
        ids.reserve(geo.points().size());
        for (size_t i = 0; i < geo.points().size(); ++i)
            ids.push_back(static_cast<geometry::GroupId>(i));
        return ids;
    };

    const auto collect_edge_keys = [](const data::PcgGeometry& geo) {
        std::vector<geometry::GroupId> keys;
        std::unordered_set<geometry::GroupId> seen;
        for (const auto& face : geo.faces()) {
            if (face.size() < 2)
                continue;
            for (size_t i = 0; i < face.size(); ++i) {
                const int a = face[i];
                const int b = face[(i + 1) % face.size()];
                if (a < 0 || b < 0)
                    continue;
                const geometry::GroupId key = geometry::edge_group_id(a, b);
                if (seen.insert(key).second)
                    keys.push_back(key);
            }
        }
        // Also include edge-group members that may not appear in faces (rare).
        for (const auto& name : geo.groups().group_names(geometry::GroupDomain::Edge)) {
            for (geometry::GroupId id : geo.groups().members(geometry::GroupDomain::Edge, name)) {
                if (seen.insert(id).second)
                    keys.push_back(id);
            }
        }
        return keys;
    };

    const auto edge_centers = [&](const data::PcgGeometry& geo) {
        const auto keys = collect_edge_keys(geo);
        std::vector<data::PcgVec3> centers;
        centers.reserve(keys.size());
        for (geometry::GroupId key : keys) {
            const auto ends = geometry::edge_group_points(key);
            data::PcgVec3 mid{};
            if (ends[0] >= 0 && ends[1] >= 0 &&
                static_cast<size_t>(ends[0]) < geo.points().size() &&
                static_cast<size_t>(ends[1]) < geo.points().size()) {
                const auto& a = geo.points()[static_cast<size_t>(ends[0])];
                const auto& b = geo.points()[static_cast<size_t>(ends[1])];
                mid.x = 0.5 * (a.x + b.x);
                mid.y = 0.5 * (a.y + b.y);
                mid.z = 0.5 * (a.z + b.z);
            }
            centers.push_back(mid);
        }
        return centers;
    };
    const auto edge_ids = [&](const data::PcgGeometry& geo) {
        return collect_edge_keys(geo);
    };

    transfer_domain(geometry::GroupDomain::Face, options.transfer_primitives,
                    options.primitive_groups, options.primitive_group_prefix, face_centers,
                    face_ids);
    transfer_domain(geometry::GroupDomain::Point, options.transfer_points, options.point_groups,
                    options.point_group_prefix, point_centers, point_ids);
    transfer_domain(geometry::GroupDomain::Edge, options.transfer_edges, options.edge_groups,
                    options.edge_group_prefix, edge_centers, edge_ids);
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
