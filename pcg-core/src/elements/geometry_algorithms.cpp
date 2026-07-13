#include "elements/geometry_algorithms.hpp"

#include "geometry/bmesh.hpp"

#include <cmath>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

constexpr double kPi = 3.14159265358979323846;

geometry::GroupDomain parse_domain(const std::string& domain)
{
    if (domain == "point")
        return geometry::GroupDomain::Point;
    if (domain == "face" || domain == "prim")
        return geometry::GroupDomain::Face;
    return geometry::GroupDomain::Edge;
}

double edge_angle_deg(const data::PcgGeometry& geometry, int64_t edge_key)
{
    const geometry::BMesh bmesh = geometry::bmesh_from_geometry(geometry);
    const auto it = bmesh.edges.find(edge_key);
    if (it == bmesh.edges.end() || it->second.face0 < 0 || it->second.face1 < 0)
        return 180.0;

    const geometry::Vec3 n0 = geometry::face_normal(bmesh, it->second.face0);
    const geometry::Vec3 n1 = geometry::face_normal(bmesh, it->second.face1);
    const double cos_a = n0.x * n1.x + n0.y * n1.y + n0.z * n1.z;
    return std::acos(std::clamp(cos_a, -1.0, 1.0)) * 180.0 / kPi;
}

} // namespace

data::PcgGeometry group_create(const data::PcgGeometry& input, const GroupCreateOptions& options)
{
    data::PcgGeometry result = input;
    const geometry::GroupDomain domain = parse_domain(options.domain);
    result.groups().clear_group(domain, options.output_group);

    if (options.mode == "unshared") {
        data::maintain_unshared_edge_group(result, options.output_group);
        return result;
    }

    if (domain != geometry::GroupDomain::Edge)
        return result;

    const geometry::BMesh bmesh = geometry::bmesh_from_geometry(input);

    // If from_edge_groups is set, only consider edges already in those groups
    std::unordered_set<int64_t> candidate_edges;
    if (!options.from_edge_groups.empty()) {
        for (const auto& grp : options.from_edge_groups) {
            const auto members = input.groups().members(geometry::GroupDomain::Edge, grp);
            for (int id : members)
                candidate_edges.insert(static_cast<int64_t>(id));
        }
    }

    for (const auto& entry : bmesh.edges) {
        const geometry::BMeshEdge& edge = entry.second;

        // Filter by from_edge_groups if specified
        if (!options.from_edge_groups.empty() && candidate_edges.count(entry.first) == 0)
            continue;

        if (!options.include_unshared && edge.face1 < 0)
            continue;

        if (!options.from_face_groups.empty()) {
            bool touches = false;
            if (edge.face0 >= 0) {
                for (const auto& grp : options.from_face_groups)
                    if (input.groups().contains(geometry::GroupDomain::Face, grp, edge.face0)) {
                        touches = true; break; }
            }
            if (!touches && edge.face1 >= 0) {
                for (const auto& grp : options.from_face_groups)
                    if (input.groups().contains(geometry::GroupDomain::Face, grp, edge.face1)) {
                        touches = true; break; }
            }
            if (!touches)
                continue;
        }

        if (options.mode == "angle") {
            if (edge_angle_deg(input, entry.first) < options.min_edge_angle_deg)
                continue;
        }

        result.groups().add(domain, options.output_group, static_cast<int>(entry.first));
    }

    return result;
}

data::PcgGeometry group_combine(const data::PcgGeometry& input, const GroupCombineOptions& options)
{
    data::PcgGeometry result = input;
    const geometry::GroupDomain domain = parse_domain(options.domain);
    result.groups().clear_group(domain, options.output_group);

    if (options.source_groups.empty())
        return result;

    if (options.operation == "intersect" && options.source_groups.size() >= 2) {
        result.groups().intersect_into(domain, options.output_group, options.source_groups[0],
                                       options.source_groups[1]);
        for (size_t i = 2; i < options.source_groups.size(); ++i) {
            const auto current = result.groups().members(domain, options.output_group);
            result.groups().clear_group(domain, options.output_group);
            for (int id : current) {
                if (input.groups().contains(domain, options.source_groups[i], id))
                    result.groups().add(domain, options.output_group, id);
            }
        }
        return result;
    }

    if (options.operation == "subtract" && options.source_groups.size() >= 2) {
        result.groups().union_into(domain, options.output_group, options.source_groups[0]);
        for (size_t i = 1; i < options.source_groups.size(); ++i)
            result.groups().subtract_into(domain, options.output_group, options.source_groups[i]);
        return result;
    }

    for (const std::string& src : options.source_groups)
        result.groups().union_into(domain, options.output_group, src);

    return result;
}

} // namespace pcg::internal::elements
