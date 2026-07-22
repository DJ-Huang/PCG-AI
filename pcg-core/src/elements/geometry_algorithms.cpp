#include "elements/geometry_algorithms.hpp"

#include "elements/element_utils.hpp"
#include "geometry/bmesh.hpp"
#include "geometry/bvh.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

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

geometry::AABB geometry_aabb(const data::PcgGeometry& geometry)
{
    geometry::AABB box;
    for (const auto& point : geometry.points())
        box.expand({point.x, point.y, point.z});
    return box;
}

bool face_centroid(const data::PcgGeometry& input, size_t face_index, geometry::Vec3& out)
{
    const auto& face = input.faces()[face_index];
    if (face.empty())
        return false;
    double sx = 0.0, sy = 0.0, sz = 0.0;
    int count = 0;
    for (int pi : face) {
        if (pi < 0 || static_cast<size_t>(pi) >= input.points().size())
            continue;
        const auto& p = input.points()[static_cast<size_t>(pi)];
        sx += p.x;
        sy += p.y;
        sz += p.z;
        ++count;
    }
    if (count == 0)
        return false;
    out = {sx / count, sy / count, sz / count};
    return true;
}

bool edge_midpoint(const data::PcgGeometry& input, geometry::GroupId edge_id, geometry::Vec3& out)
{
    const auto ends = geometry::edge_group_points(edge_id);
    if (ends[0] < 0 || ends[1] < 0
        || static_cast<size_t>(ends[0]) >= input.points().size()
        || static_cast<size_t>(ends[1]) >= input.points().size())
        return false;
    const auto& a = input.points()[static_cast<size_t>(ends[0])];
    const auto& b = input.points()[static_cast<size_t>(ends[1])];
    out = {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5, (a.z + b.z) * 0.5};
    return true;
}

bool element_position(const data::PcgGeometry& input,
                      geometry::GroupDomain domain,
                      geometry::GroupId id,
                      geometry::Vec3& out)
{
    if (domain == geometry::GroupDomain::Point) {
        if (id < 0 || static_cast<size_t>(id) >= input.points().size())
            return false;
        const auto& p = input.points()[static_cast<size_t>(id)];
        out = {p.x, p.y, p.z};
        return true;
    }
    if (domain == geometry::GroupDomain::Face)
        return face_centroid(input, static_cast<size_t>(id), out);
    return edge_midpoint(input, id, out);
}

bool face_normal_vec(const data::PcgGeometry& input, size_t face_index, geometry::Vec3& out)
{
    const auto& face = input.faces()[face_index];
    if (face.size() < 3)
        return false;

    double nx = 0.0, ny = 0.0, nz = 0.0;
    for (size_t i = 0; i < face.size(); ++i) {
        const int current_index = face[i];
        const int next_index = face[(i + 1) % face.size()];
        if (current_index < 0 || next_index < 0
            || static_cast<size_t>(current_index) >= input.points().size()
            || static_cast<size_t>(next_index) >= input.points().size())
            return false;
        const auto& current = input.points()[static_cast<size_t>(current_index)];
        const auto& next = input.points()[static_cast<size_t>(next_index)];
        nx += (current.y - next.y) * (current.z + next.z);
        ny += (current.z - next.z) * (current.x + next.x);
        nz += (current.x - next.x) * (current.y + next.y);
    }
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len <= 1e-12)
        return false;
    out = {nx / len, ny / len, nz / len};
    return true;
}

bool element_normal(const data::PcgGeometry& input,
                    geometry::GroupDomain domain,
                    geometry::GroupId id,
                    geometry::Vec3& out)
{
    if (domain == geometry::GroupDomain::Face)
        return face_normal_vec(input, static_cast<size_t>(id), out);

    if (domain == geometry::GroupDomain::Point) {
        geometry::Vec3 sum{0.0, 0.0, 0.0};
        int count = 0;
        for (size_t fi = 0; fi < input.faces().size(); ++fi) {
            const auto& face = input.faces()[fi];
            bool touches = false;
            for (int pi : face) {
                if (pi == static_cast<int>(id)) {
                    touches = true;
                    break;
                }
            }
            if (!touches)
                continue;
            geometry::Vec3 n;
            if (!face_normal_vec(input, fi, n))
                continue;
            sum.x += n.x;
            sum.y += n.y;
            sum.z += n.z;
            ++count;
        }
        if (count == 0)
            return false;
        const double len = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
        if (len <= 1e-12)
            return false;
        out = {sum.x / len, sum.y / len, sum.z / len};
        return true;
    }

    // Edge: average of adjacent face normals via BMesh.
    const geometry::BMesh bmesh = geometry::bmesh_from_geometry(input);
    const auto it = bmesh.edges.find(id);
    if (it == bmesh.edges.end())
        return false;
    geometry::Vec3 sum{0.0, 0.0, 0.0};
    int count = 0;
    if (it->second.face0 >= 0) {
        const auto n = geometry::face_normal(bmesh, it->second.face0);
        sum.x += n.x;
        sum.y += n.y;
        sum.z += n.z;
        ++count;
    }
    if (it->second.face1 >= 0) {
        const auto n = geometry::face_normal(bmesh, it->second.face1);
        sum.x += n.x;
        sum.y += n.y;
        sum.z += n.z;
        ++count;
    }
    if (count == 0)
        return false;
    const double len = std::sqrt(sum.x * sum.x + sum.y * sum.y + sum.z * sum.z);
    if (len <= 1e-12)
        return false;
    out = {sum.x / len, sum.y / len, sum.z / len};
    return true;
}

std::unordered_set<geometry::GroupId> all_elements(const data::PcgGeometry& input,
                                                   geometry::GroupDomain domain)
{
    std::unordered_set<geometry::GroupId> out;
    if (domain == geometry::GroupDomain::Point) {
        for (size_t i = 0; i < input.points().size(); ++i)
            out.insert(static_cast<geometry::GroupId>(i));
    } else if (domain == geometry::GroupDomain::Face) {
        for (size_t i = 0; i < input.faces().size(); ++i)
            out.insert(static_cast<geometry::GroupId>(i));
    } else {
        const geometry::BMesh bmesh = geometry::bmesh_from_geometry(input);
        for (const auto& entry : bmesh.edges)
            out.insert(entry.first);
    }
    return out;
}

std::unordered_set<geometry::GroupId> select_edges(const data::PcgGeometry& input,
                                                   const GroupCreateOptions& options)
{
    std::unordered_set<geometry::GroupId> selected;

    if (options.mode == "unshared") {
        data::PcgGeometry tmp = input;
        data::maintain_unshared_edge_group(tmp, "__tmp_unshared__");
        selected = tmp.groups().members(geometry::GroupDomain::Edge, "__tmp_unshared__");
        return selected;
    }

    if (options.mode == "all")
        return all_elements(input, geometry::GroupDomain::Edge);

    const geometry::BMesh bmesh = geometry::bmesh_from_geometry(input);

    std::unordered_set<int64_t> candidate_edges;
    if (!options.from_edge_groups.empty()) {
        for (const auto& grp : options.from_edge_groups) {
            const auto members = input.groups().members(geometry::GroupDomain::Edge, grp);
            for (geometry::GroupId id : members)
                candidate_edges.insert(id);
        }
    }

    for (const auto& entry : bmesh.edges) {
        const geometry::BMeshEdge& edge = entry.second;

        if (!options.from_edge_groups.empty() && candidate_edges.count(entry.first) == 0)
            continue;

        if (!options.include_unshared && edge.face1 < 0)
            continue;

        if (!options.from_face_groups.empty()) {
            bool touches = false;
            if (edge.face0 >= 0) {
                for (const auto& grp : options.from_face_groups)
                    if (input.groups().contains(geometry::GroupDomain::Face, grp, edge.face0)) {
                        touches = true;
                        break;
                    }
            }
            if (!touches && edge.face1 >= 0) {
                for (const auto& grp : options.from_face_groups)
                    if (input.groups().contains(geometry::GroupDomain::Face, grp, edge.face1)) {
                        touches = true;
                        break;
                    }
            }
            if (!touches)
                continue;
        }

        if (options.mode == "angle") {
            if (edge_angle_deg(input, entry.first) < options.min_edge_angle_deg)
                continue;
        }

        selected.insert(entry.first);
    }

    return selected;
}

} // namespace

data::PcgGeometry group_create(const data::PcgGeometry& input,
                               const GroupCreateOptions& options,
                               const data::PcgGeometry* bounding)
{
    data::PcgGeometry result = input;
    const geometry::GroupDomain domain = parse_domain(options.domain);

    const bool any_filter = options.enable_base_group || options.enable_bounding
        || options.enable_normals || options.enable_edges || options.enable_random;

    std::unordered_set<geometry::GroupId> previous;
    if (options.initial_merge != "replace")
        previous = input.groups().members(domain, options.output_group);

    result.groups().clear_group(domain, options.output_group);

    if (!any_filter)
        return result;

    // Fast path: edges-only unshared with replace merge.
    if (options.enable_edges && !options.enable_base_group && !options.enable_bounding
        && !options.enable_normals && !options.enable_random
        && domain == geometry::GroupDomain::Edge && options.mode == "unshared"
        && options.initial_merge == "replace") {
        data::maintain_unshared_edge_group(result, options.output_group);
        return result;
    }

    std::unordered_set<geometry::GroupId> candidates = all_elements(input, domain);

    if (options.enable_base_group && !options.base_groups.empty()) {
        std::unordered_set<geometry::GroupId> base;
        for (const auto& name : options.base_groups) {
            for (geometry::GroupId id : input.groups().members(domain, name))
                base.insert(id);
        }
        for (auto it = candidates.begin(); it != candidates.end();) {
            if (base.count(*it) == 0)
                it = candidates.erase(it);
            else
                ++it;
        }
    }

    if (options.enable_bounding) {
        if (bounding == nullptr || bounding->points().empty()) {
            candidates.clear();
        } else {
            const geometry::AABB box = geometry_aabb(*bounding);
            for (auto it = candidates.begin(); it != candidates.end();) {
                geometry::Vec3 pos;
                if (!element_position(input, domain, *it, pos) || !box.contains(pos))
                    it = candidates.erase(it);
                else
                    ++it;
            }
        }
    }

    if (options.enable_normals) {
        const double direction_length = std::sqrt(
            options.direction_x * options.direction_x
            + options.direction_y * options.direction_y
            + options.direction_z * options.direction_z);
        if (direction_length <= 1e-12) {
            candidates.clear();
        } else {
            const double dx = options.direction_x / direction_length;
            const double dy = options.direction_y / direction_length;
            const double dz = options.direction_z / direction_length;
            const double spread = std::clamp(options.spread_angle_deg, 0.0, 180.0);
            const double threshold = std::cos(spread * kPi / 180.0);
            for (auto it = candidates.begin(); it != candidates.end();) {
                geometry::Vec3 n;
                if (!element_normal(input, domain, *it, n)
                    || (n.x * dx + n.y * dy + n.z * dz) < threshold)
                    it = candidates.erase(it);
                else
                    ++it;
            }
        }
    }

    if (options.enable_edges) {
        if (domain != geometry::GroupDomain::Edge) {
            // Include-by-edges only applies to edge groups in this parity pass.
        } else {
            const auto edge_hits = select_edges(input, options);
            for (auto it = candidates.begin(); it != candidates.end();) {
                if (edge_hits.count(*it) == 0)
                    it = candidates.erase(it);
                else
                    ++it;
            }
        }
    }

    if (options.enable_random) {
        const double chance = std::clamp(options.random_chance, 0.0, 1.0);
        uint32_t state = mix_seed(options.random_seed, static_cast<int>(candidates.size()));
        for (auto it = candidates.begin(); it != candidates.end();) {
            const double roll = next_rand(state) / static_cast<double>(UINT32_MAX);
            if (roll > chance)
                it = candidates.erase(it);
            else
                ++it;
        }
    }

    std::unordered_set<geometry::GroupId> selected = std::move(candidates);

    if (options.initial_merge == "union") {
        for (geometry::GroupId id : previous)
            selected.insert(id);
    } else if (options.initial_merge == "intersect") {
        for (auto it = selected.begin(); it != selected.end();) {
            if (previous.count(*it) == 0)
                it = selected.erase(it);
            else
                ++it;
        }
    } else if (options.initial_merge == "subtract") {
        std::unordered_set<geometry::GroupId> kept = previous;
        for (geometry::GroupId id : selected)
            kept.erase(id);
        selected = std::move(kept);
    }

    for (geometry::GroupId id : selected)
        result.groups().add(domain, options.output_group, id);

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
            for (geometry::GroupId id : current) {
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

data::PcgGeometry face_group_by_normal(const data::PcgGeometry& input,
                                       const FaceGroupByNormalOptions& options)
{
    data::PcgGeometry result = input;
    result.groups().clear_group(geometry::GroupDomain::Face, options.output_group);

    const double direction_length = std::sqrt(options.direction_x * options.direction_x
        + options.direction_y * options.direction_y + options.direction_z * options.direction_z);
    if (direction_length <= 1e-12 || options.output_group.empty())
        return result;

    const double dx = options.direction_x / direction_length;
    const double dy = options.direction_y / direction_length;
    const double dz = options.direction_z / direction_length;
    const double spread = std::clamp(options.spread_angle_deg, 0.0, 180.0);
    const double threshold = std::cos(spread * kPi / 180.0);

    for (size_t face_index = 0; face_index < input.faces().size(); ++face_index) {
        geometry::Vec3 n;
        if (!face_normal_vec(input, face_index, n))
            continue;
        if ((n.x * dx + n.y * dy + n.z * dz) >= threshold)
            result.groups().add(geometry::GroupDomain::Face, options.output_group,
                                static_cast<int>(face_index));
    }

    return result;
}

} // namespace pcg::internal::elements
