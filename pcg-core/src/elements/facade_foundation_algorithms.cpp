#include "elements/facade_foundation_algorithms.hpp"

#include "data/pcg_attribute_table.hpp"
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

namespace {

struct ConvertEdge {
    int a = -1;
    int b = -1;
    int source_face = -1;
};

nlohmann::json attribute_element_to_json(const data::AttributeArray& attr, size_t index)
{
    if (index >= attr.size())
        return nullptr;

    const int tuple = std::max(1, attr.schema().tuple_size);
    const size_t offset = index * static_cast<size_t>(tuple);

    if (attr.schema().type == data::AttributeType::String) {
        if (tuple == 1) {
            if (offset >= attr.string_values().size())
                return nullptr;
            return attr.string_values()[offset];
        }
        nlohmann::json values = nlohmann::json::array();
        for (int comp = 0; comp < tuple; ++comp) {
            const size_t slot = offset + static_cast<size_t>(comp);
            if (slot >= attr.string_values().size())
                return nullptr;
            values.push_back(attr.string_values()[slot]);
        }
        return values;
    }

    if (attr.schema().type == data::AttributeType::Int) {
        if (tuple == 1) {
            if (offset >= attr.int_values().size())
                return nullptr;
            return attr.int_values()[offset];
        }
        nlohmann::json values = nlohmann::json::array();
        for (int comp = 0; comp < tuple; ++comp) {
            const size_t slot = offset + static_cast<size_t>(comp);
            if (slot >= attr.int_values().size())
                return nullptr;
            values.push_back(attr.int_values()[slot]);
        }
        return values;
    }

    if (tuple == 1) {
        if (offset >= attr.float_values().size())
            return nullptr;
        return attr.float_values()[offset];
    }
    nlohmann::json values = nlohmann::json::array();
    for (int comp = 0; comp < tuple; ++comp) {
        const size_t slot = offset + static_cast<size_t>(comp);
        if (slot >= attr.float_values().size())
            return nullptr;
        values.push_back(attr.float_values()[slot]);
    }
    return values;
}

void copy_detail_attributes_to_metadata(const data::PcgGeometry& input,
                                        data::PcgSplineData& output)
{
    for (const auto& name : input.attributes().names(data::AttributeOwner::Detail)) {
        const data::AttributeArray* attr =
            input.attributes().find(data::AttributeOwner::Detail, name);
        if (!attr || attr->size() == 0)
            continue;
        const nlohmann::json value = attribute_element_to_json(*attr, 0);
        if (!value.is_null())
            output.metadata().set(name, value);
    }
}

void copy_primitive_attributes_to_json(const data::PcgGeometry& input,
                                       int face_index,
                                       nlohmann::json& destination)
{
    if (face_index < 0 || static_cast<size_t>(face_index) >= input.faces().size())
        return;

    const size_t prim_index = static_cast<size_t>(face_index);
    for (const auto& name : input.attributes().names(data::AttributeOwner::Primitive)) {
        const data::AttributeArray* attr =
            input.attributes().find(data::AttributeOwner::Primitive, name);
        if (!attr || prim_index >= attr->size())
            continue;
        const nlohmann::json value = attribute_element_to_json(*attr, prim_index);
        if (!value.is_null())
            destination[name] = value;
    }
    destination["primnum"] = face_index;
}

double point_distance_sq(const data::PcgVec3& a, const data::PcgVec3& b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
}

double segment_length(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return std::sqrt(point_distance_sq(a, b));
}

class PointUnionFind {
public:
    int find(int point)
    {
        auto& parent = parents_[point];
        if (parent == point)
            return point;
        parent = find(parent);
        return parent;
    }

    void unite(int a, int b)
    {
        const int root_a = find(a);
        const int root_b = find(b);
        if (root_a == root_b)
            return;
        if (root_a < root_b)
            parents_[root_b] = root_a;
        else
            parents_[root_a] = root_b;
    }

    void ensure(int point) { parents_.emplace(point, point); }

private:
    std::unordered_map<int, int> parents_;
};

data::PcgSplinePoint to_spline_point(const data::PcgVec3& point)
{
    return {point.x, point.y, point.z};
}

void append_spline_point(data::PcgSpline& spline, const data::PcgVec3& point)
{
    const auto next = to_spline_point(point);
    if (!spline.points.empty()) {
        const auto& last = spline.points.back();
        if (std::abs(last.x - next.x) < 1e-12 && std::abs(last.y - next.y) < 1e-12 &&
            std::abs(last.z - next.z) < 1e-12)
            return;
    }
    spline.points.push_back(next);
}

void set_spline_length_attr(data::PcgSpline& spline, const std::string& attr_name, double length)
{
    if (attr_name.empty())
        return;
    spline.attributes[attr_name] = length;
}

std::vector<ConvertEdge> collect_convert_edges(const data::PcgGeometry& input,
                                               const ConvertLineOptions& options)
{
    std::unordered_map<int64_t, int> edge_face_count;
    std::unordered_map<int64_t, std::pair<int, int>> edge_endpoints;
    std::unordered_map<int64_t, int> edge_source_face;
    for (size_t fi = 0; fi < input.faces().size(); ++fi) {
        const auto& face = input.faces()[fi];
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
            edge_source_face.emplace(key, static_cast<int>(fi));
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

    std::vector<ConvertEdge> edges;
    edges.reserve(edge_endpoints.size());
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
        const auto face_it = edge_source_face.find(key);
        const int source_face = face_it == edge_source_face.end() ? -1 : face_it->second;
        edges.push_back({a_idx, b_idx, source_face});
    }
    return edges;
}

void merge_nearby_endpoints(std::vector<ConvertEdge>& edges,
                            const data::PcgGeometry& input,
                            const ConvertLineOptions& options)
{
    std::unordered_map<int, int> degree;
    for (const auto& edge : edges) {
        ++degree[edge.a];
        ++degree[edge.b];
    }

    PointUnionFind uf;
    for (const auto& edge : edges) {
        uf.ensure(edge.a);
        uf.ensure(edge.b);
    }

    const double max_dist2 = options.max_distance * options.max_distance;
    std::vector<int> endpoints;
    endpoints.reserve(degree.size());
    for (const auto& entry : degree) {
        if (entry.second == 1)
            endpoints.push_back(entry.first);
    }

    for (size_t i = 0; i < endpoints.size(); ++i) {
        for (size_t j = i + 1; j < endpoints.size(); ++j) {
            const int pi = endpoints[i];
            const int pj = endpoints[j];
            if (static_cast<size_t>(pi) >= input.points().size() ||
                static_cast<size_t>(pj) >= input.points().size())
                continue;
            const auto& a = input.points()[static_cast<size_t>(pi)];
            const auto& b = input.points()[static_cast<size_t>(pj)];
            if (point_distance_sq(a, b) <= max_dist2)
                uf.unite(pi, pj);
        }
    }

    if (!options.connect_only_to_other_end_points) {
        for (const int endpoint : endpoints) {
            if (static_cast<size_t>(endpoint) >= input.points().size())
                continue;
            const auto& ep = input.points()[static_cast<size_t>(endpoint)];
            for (size_t pi = 0; pi < input.points().size(); ++pi) {
                if (static_cast<int>(pi) == endpoint)
                    continue;
                if (degree[static_cast<int>(pi)] == 1)
                    continue;
                if (point_distance_sq(ep, input.points()[pi]) <= max_dist2)
                    uf.unite(endpoint, static_cast<int>(pi));
            }
        }
    }

    for (auto& edge : edges) {
        edge.a = uf.find(edge.a);
        edge.b = uf.find(edge.b);
        if (edge.a > edge.b)
            std::swap(edge.a, edge.b);
    }
}

void emit_segment_spline(data::PcgSplineData& output,
                         const data::PcgGeometry& input,
                         const ConvertEdge& edge,
                         const ConvertLineOptions& options)
{
    if (static_cast<size_t>(edge.a) >= input.points().size() ||
        static_cast<size_t>(edge.b) >= input.points().size())
        return;

    data::PcgSpline spline;
    spline.closed = false;
    const auto& a = input.points()[static_cast<size_t>(edge.a)];
    const auto& b = input.points()[static_cast<size_t>(edge.b)];
    spline.points.push_back(to_spline_point(a));
    spline.points.push_back(to_spline_point(b));
    if (options.compute_length)
        set_spline_length_attr(spline, options.length_attribute, segment_length(a, b));
    copy_primitive_attributes_to_json(input, edge.source_face, spline.attributes);
    output.splines_mut().push_back(std::move(spline));
}

void emit_connected_splines(data::PcgSplineData& output,
                            const data::PcgGeometry& input,
                            std::vector<ConvertEdge> edges,
                            const ConvertLineOptions& options)
{
    if (edges.empty())
        return;

    merge_nearby_endpoints(edges, input, options);

    std::unordered_map<int, std::vector<std::pair<int, size_t>>> adjacency;
    for (size_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
        const auto& edge = edges[edge_index];
        adjacency[edge.a].emplace_back(edge.b, edge_index);
        adjacency[edge.b].emplace_back(edge.a, edge_index);
    }

    std::vector<bool> used_edge(edges.size(), false);

    const auto extend_path = [&](std::vector<int>& path, int from, int current, bool prepend) {
        int prev = from;
        while (true) {
            bool advanced = false;
            for (const auto& link : adjacency[current]) {
                const int next = link.first;
                const size_t edge_index = link.second;
                if (used_edge[edge_index] || next == prev)
                    continue;
                used_edge[edge_index] = true;
                if (prepend)
                    path.insert(path.begin(), next);
                else
                    path.push_back(next);
                prev = current;
                current = next;
                advanced = true;
                break;
            }
            if (!advanced)
                break;
        }
    };

    const auto emit_path = [&](const std::vector<int>& path, int source_face) {
        if (path.size() < 2)
            return;

        data::PcgSpline spline;
        spline.closed = false;
        double total_length = 0.0;
        for (size_t i = 0; i < path.size(); ++i) {
            const auto& point = input.points()[static_cast<size_t>(path[i])];
            append_spline_point(spline, point);
            if (i > 0) {
                const auto& prev = input.points()[static_cast<size_t>(path[i - 1])];
                total_length += segment_length(prev, point);
            }
        }

        const bool isolated_loop = path.size() >= 3 && path.front() == path.back();
        if (options.make_isolated_loops_closed && isolated_loop) {
            spline.closed = true;
            if (spline.points.size() > 1)
                spline.points.pop_back();
        }

        if (options.compute_length)
            set_spline_length_attr(spline, options.length_attribute, total_length);
        copy_primitive_attributes_to_json(input, source_face, spline.attributes);
        output.splines_mut().push_back(std::move(spline));
    };

    for (size_t edge_index = 0; edge_index < edges.size(); ++edge_index) {
        if (used_edge[edge_index])
            continue;
        const auto& edge = edges[edge_index];
        used_edge[edge_index] = true;
        std::vector<int> path{edge.a, edge.b};
        extend_path(path, edge.a, edge.b, false);
        extend_path(path, edge.b, edge.a, true);
        emit_path(path, edge.source_face);
    }
}

} // namespace

data::PcgSplineData convert_line_geometry(const data::PcgGeometry& input,
                                          const ConvertLineOptions& options)
{
    data::PcgSplineData output;
    if (input.points().empty() || input.faces().empty())
        return output;

    auto edges = collect_convert_edges(input, options);
    if (edges.empty())
        return output;

    if (options.connect_path)
        emit_connected_splines(output, input, std::move(edges), options);
    else
        for (const auto& edge : edges)
            emit_segment_spline(output, input, edge, options);

    copy_detail_attributes_to_metadata(input, output);

    return output;
}

data::PcgSplineData convert_geometry_primitives_to_splines(
    const data::PcgGeometry& input)
{
    data::PcgSplineData output;
    for (size_t face_index = 0; face_index < input.faces().size(); ++face_index) {
        const auto& face = input.faces()[face_index];
        if (face.size() < 2)
            continue;

        const bool repeats_first = face.size() > 2 && face.front() == face.back();
        const size_t point_count = face.size() - (repeats_first ? 1u : 0u);
        if (point_count < 2)
            continue;

        data::PcgSpline spline;
        spline.closed = repeats_first || point_count > 2;
        spline.points.reserve(point_count);
        bool valid = true;
        for (size_t corner = 0; corner < point_count; ++corner) {
            const int point_index = face[corner];
            if (point_index < 0 ||
                static_cast<size_t>(point_index) >= input.points().size()) {
                valid = false;
                break;
            }
            spline.points.push_back(
                to_spline_point(input.points()[static_cast<size_t>(point_index)]));
        }
        if (!valid)
            continue;

        copy_primitive_attributes_to_json(
            input, static_cast<int>(face_index), spline.attributes);
        for (const auto& group_name :
             input.groups().group_names(geometry::GroupDomain::Face)) {
            if (input.groups().contains(geometry::GroupDomain::Face,
                                        group_name,
                                        static_cast<geometry::GroupId>(face_index))) {
                spline.attributes[group_name] = true;
            }
        }
        output.add_spline(std::move(spline));
    }
    copy_detail_attributes_to_metadata(input, output);
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
