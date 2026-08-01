#include "elements/geometry_algorithms.hpp"

#include "elements/delete_algorithms.hpp"
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
    if (domain == "vertex")
        return geometry::GroupDomain::Vertex;
    if (domain == "face" || domain == "prim" || domain == "primitive")
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

namespace {

bool face_all_points_in(const data::PcgGeometry& input,
                        size_t face_index,
                        const std::unordered_set<geometry::GroupId>& points)
{
    const auto& face = input.faces()[face_index];
    if (face.empty())
        return false;
    for (int pi : face) {
        if (points.count(static_cast<geometry::GroupId>(pi)) == 0)
            return false;
    }
    return true;
}

bool face_any_point_in(const data::PcgGeometry& input,
                       size_t face_index,
                       const std::unordered_set<geometry::GroupId>& points)
{
    const auto& face = input.faces()[face_index];
    for (int pi : face) {
        if (points.count(static_cast<geometry::GroupId>(pi)) != 0)
            return true;
    }
    return false;
}

bool face_shares_edge_in_points(const data::PcgGeometry& input,
                                size_t face_index,
                                const std::unordered_set<geometry::GroupId>& points)
{
    const auto& face = input.faces()[face_index];
    const size_t n = face.size();
    if (n < 2)
        return false;
    for (size_t i = 0; i < n; ++i) {
        const int a = face[i];
        const int b = face[(i + 1) % n];
        if (points.count(static_cast<geometry::GroupId>(a)) != 0
            && points.count(static_cast<geometry::GroupId>(b)) != 0)
            return true;
    }
    return false;
}

bool face_all_edges_in(const data::PcgGeometry& input,
                       size_t face_index,
                       const std::unordered_set<geometry::GroupId>& edges)
{
    const auto& face = input.faces()[face_index];
    const size_t n = face.size();
    if (n < 2)
        return false;
    for (size_t i = 0; i < n; ++i) {
        const geometry::GroupId key =
            geometry::edge_group_id(face[i], face[(i + 1) % n]);
        if (edges.count(key) == 0)
            return false;
    }
    return true;
}

bool face_any_edge_in(const data::PcgGeometry& input,
                      size_t face_index,
                      const std::unordered_set<geometry::GroupId>& edges)
{
    const auto& face = input.faces()[face_index];
    const size_t n = face.size();
    if (n < 2)
        return false;
    for (size_t i = 0; i < n; ++i) {
        const geometry::GroupId key =
            geometry::edge_group_id(face[i], face[(i + 1) % n]);
        if (edges.count(key) != 0)
            return true;
    }
    return false;
}

std::unordered_set<geometry::GroupId> points_of_faces(
    const data::PcgGeometry& input, const std::unordered_set<geometry::GroupId>& faces)
{
    std::unordered_set<geometry::GroupId> points;
    for (geometry::GroupId fid : faces) {
        if (fid < 0 || static_cast<size_t>(fid) >= input.faces().size())
            continue;
        for (int pi : input.faces()[static_cast<size_t>(fid)])
            points.insert(static_cast<geometry::GroupId>(pi));
    }
    return points;
}

std::unordered_set<geometry::GroupId> points_of_edges(
    const std::unordered_set<geometry::GroupId>& edges)
{
    std::unordered_set<geometry::GroupId> points;
    for (geometry::GroupId eid : edges) {
        const auto ends = geometry::edge_group_points(eid);
        points.insert(static_cast<geometry::GroupId>(ends[0]));
        points.insert(static_cast<geometry::GroupId>(ends[1]));
    }
    return points;
}

struct CornerIndex {
    int face = -1;
    int local = -1;
    int global = -1;
    int point = -1;
};

std::vector<CornerIndex> build_corners(const data::PcgGeometry& input)
{
    std::vector<CornerIndex> corners;
    corners.reserve(static_cast<size_t>(std::max(0, input.corner_count())));
    int global = 0;
    for (size_t fi = 0; fi < input.faces().size(); ++fi) {
        const auto& face = input.faces()[fi];
        for (size_t li = 0; li < face.size(); ++li) {
            CornerIndex c;
            c.face = static_cast<int>(fi);
            c.local = static_cast<int>(li);
            c.global = global++;
            c.point = face[li];
            corners.push_back(c);
        }
    }
    return corners;
}

std::unordered_set<geometry::GroupId> corners_of_points(
    const std::vector<CornerIndex>& corners,
    const std::unordered_set<geometry::GroupId>& points)
{
    std::unordered_set<geometry::GroupId> out;
    for (const auto& c : corners) {
        if (points.count(static_cast<geometry::GroupId>(c.point)) != 0)
            out.insert(static_cast<geometry::GroupId>(c.global));
    }
    return out;
}

std::unordered_set<geometry::GroupId> points_of_corners(
    const std::vector<CornerIndex>& corners,
    const std::unordered_set<geometry::GroupId>& verts)
{
    std::unordered_set<geometry::GroupId> out;
    for (geometry::GroupId vid : verts) {
        if (vid < 0 || static_cast<size_t>(vid) >= corners.size())
            continue;
        out.insert(static_cast<geometry::GroupId>(corners[static_cast<size_t>(vid)].point));
    }
    return out;
}

bool edge_zero_length(const data::PcgGeometry& input, geometry::GroupId edge_id)
{
    const auto ends = geometry::edge_group_points(edge_id);
    if (ends[0] == ends[1])
        return true;
    if (ends[0] < 0 || ends[1] < 0
        || static_cast<size_t>(ends[0]) >= input.points().size()
        || static_cast<size_t>(ends[1]) >= input.points().size())
        return true;
    const auto& a = input.points()[static_cast<size_t>(ends[0])];
    const auto& b = input.points()[static_cast<size_t>(ends[1])];
    const double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz <= 1e-24;
}

bool attribute_values_differ(const data::AttributeArray& attr,
                             size_t index_a,
                             size_t index_b,
                             double tolerance)
{
    const int tuple = std::max(1, attr.schema().tuple_size);
    if (attr.schema().type == data::AttributeType::Int) {
        const auto& values = attr.int_values();
        const size_t base_a = index_a * static_cast<size_t>(tuple);
        const size_t base_b = index_b * static_cast<size_t>(tuple);
        if (base_a + static_cast<size_t>(tuple) > values.size()
            || base_b + static_cast<size_t>(tuple) > values.size())
            return false;
        for (int i = 0; i < tuple; ++i) {
            if (values[base_a + static_cast<size_t>(i)] != values[base_b + static_cast<size_t>(i)])
                return true;
        }
        return false;
    }
    if (attr.schema().type == data::AttributeType::Float) {
        const auto& values = attr.float_values();
        const size_t base_a = index_a * static_cast<size_t>(tuple);
        const size_t base_b = index_b * static_cast<size_t>(tuple);
        if (base_a + static_cast<size_t>(tuple) > values.size()
            || base_b + static_cast<size_t>(tuple) > values.size())
            return false;
        for (int i = 0; i < tuple; ++i) {
            if (std::abs(values[base_a + static_cast<size_t>(i)]
                         - values[base_b + static_cast<size_t>(i)])
                > tolerance)
                return true;
        }
        return false;
    }
    if (attr.schema().type == data::AttributeType::String) {
        const auto& values = attr.string_values();
        const size_t base_a = index_a * static_cast<size_t>(tuple);
        const size_t base_b = index_b * static_cast<size_t>(tuple);
        if (base_a + static_cast<size_t>(tuple) > values.size()
            || base_b + static_cast<size_t>(tuple) > values.size())
            return false;
        for (int i = 0; i < tuple; ++i) {
            if (values[base_a + static_cast<size_t>(i)] != values[base_b + static_cast<size_t>(i)])
                return true;
        }
    }
    return false;
}

std::unordered_set<geometry::GroupId> connectivity_boundary_edges(
    const data::PcgGeometry& input,
    const GroupPromoteOptions& options,
    const geometry::BMesh& bmesh)
{
    std::unordered_set<geometry::GroupId> out;
    if (!options.use_connectivity_attribute || options.connectivity_attribute.empty())
        return out;

    const std::string& name = options.connectivity_attribute;
    const double tol = std::max(0.0, options.connectivity_attribute_tolerance);

    // Built-in corner UVs when requesting "uv".
    if ((name == "uv" || name == "UV") && input.has_corner_uvs()) {
        const auto corners = build_corners(input);
        std::unordered_map<geometry::GroupId, std::vector<int>> edge_corners;
        for (const auto& c : corners) {
            const auto& face = input.faces()[static_cast<size_t>(c.face)];
            const int next_point = face[(static_cast<size_t>(c.local) + 1) % face.size()];
            const geometry::GroupId eid = geometry::edge_group_id(c.point, next_point);
            edge_corners[eid].push_back(c.global);
        }
        for (const auto& entry : edge_corners) {
            if (entry.second.size() < 2)
                continue;
            for (size_t i = 0; i + 1 < entry.second.size(); ++i) {
                const size_t a = static_cast<size_t>(entry.second[i]);
                const size_t b = static_cast<size_t>(entry.second[i + 1]);
                if (a >= input.corner_uvs().size() || b >= input.corner_uvs().size())
                    continue;
                const auto& ua = input.corner_uvs()[a];
                const auto& ub = input.corner_uvs()[b];
                if (std::abs(ua.u - ub.u) > tol || std::abs(ua.v - ub.v) > tol) {
                    out.insert(entry.first);
                    break;
                }
            }
        }
        return out;
    }

    const data::AttributeArray* point_attr =
        input.attributes().find(data::AttributeOwner::Point, name);
    const data::AttributeArray* vertex_attr =
        input.attributes().find(data::AttributeOwner::Vertex, name);
    const data::AttributeArray* prim_attr =
        input.attributes().find(data::AttributeOwner::Primitive, name);

    if (prim_attr != nullptr) {
        for (const auto& entry : bmesh.edges) {
            const auto& edge = entry.second;
            if (edge.face0 < 0 || edge.face1 < 0)
                continue;
            if (attribute_values_differ(*prim_attr,
                                        static_cast<size_t>(edge.face0),
                                        static_cast<size_t>(edge.face1),
                                        tol))
                out.insert(entry.first);
        }
        return out;
    }

    if (point_attr != nullptr) {
        for (const auto& entry : bmesh.edges) {
            const auto ends = geometry::edge_group_points(entry.first);
            // Point-attribute connectivity: boundary separates differing connected regions.
            // An edge is a seam when endpoints differ (simple discontinuity proxy).
            if (attribute_values_differ(*point_attr,
                                        static_cast<size_t>(ends[0]),
                                        static_cast<size_t>(ends[1]),
                                        tol))
                out.insert(entry.first);
        }
        return out;
    }

    if (vertex_attr != nullptr) {
        const auto corners = build_corners(input);
        std::unordered_map<geometry::GroupId, std::vector<int>> edge_corners;
        for (const auto& c : corners) {
            const auto& face = input.faces()[static_cast<size_t>(c.face)];
            const int next_point = face[(static_cast<size_t>(c.local) + 1) % face.size()];
            const geometry::GroupId eid = geometry::edge_group_id(c.point, next_point);
            edge_corners[eid].push_back(c.global);
        }
        for (const auto& entry : edge_corners) {
            if (entry.second.size() < 2)
                continue;
            for (size_t i = 0; i + 1 < entry.second.size(); ++i) {
                if (attribute_values_differ(*vertex_attr,
                                            static_cast<size_t>(entry.second[i]),
                                            static_cast<size_t>(entry.second[i + 1]),
                                            tol)) {
                    out.insert(entry.first);
                    break;
                }
            }
        }
    }

    return out;
}

std::unordered_set<geometry::GroupId> promote_members(const data::PcgGeometry& input,
                                                     const GroupPromoteOptions& options,
                                                     geometry::GroupDomain from,
                                                     geometry::GroupDomain to,
                                                     const std::unordered_set<geometry::GroupId>& source)
{
    std::unordered_set<geometry::GroupId> out;
    if (source.empty())
        return out;

    if (from == to) {
        out = source;
        return out;
    }

    const geometry::BMesh bmesh = geometry::bmesh_from_geometry(input);
    const auto corners = build_corners(input);

    if (from == geometry::GroupDomain::Point && to == geometry::GroupDomain::Edge) {
        for (const auto& entry : bmesh.edges) {
            const auto ends = geometry::edge_group_points(entry.first);
            const bool a = source.count(static_cast<geometry::GroupId>(ends[0])) != 0;
            const bool b = source.count(static_cast<geometry::GroupId>(ends[1])) != 0;
            if (options.include_only_entirely_contained ? (a && b) : (a || b))
                out.insert(entry.first);
        }
        return out;
    }

    if (from == geometry::GroupDomain::Point && to == geometry::GroupDomain::Face) {
        for (size_t fi = 0; fi < input.faces().size(); ++fi) {
            bool keep = false;
            if (options.include_only_primitives_sharing_edge)
                keep = face_shares_edge_in_points(input, fi, source);
            else if (options.include_only_entirely_contained)
                keep = face_all_points_in(input, fi, source);
            else
                keep = face_any_point_in(input, fi, source);
            if (keep)
                out.insert(static_cast<geometry::GroupId>(fi));
        }
        return out;
    }

    if (from == geometry::GroupDomain::Point && to == geometry::GroupDomain::Vertex)
        return corners_of_points(corners, source);

    if (from == geometry::GroupDomain::Edge && to == geometry::GroupDomain::Point)
        return points_of_edges(source);

    if (from == geometry::GroupDomain::Edge && to == geometry::GroupDomain::Face) {
        for (size_t fi = 0; fi < input.faces().size(); ++fi) {
            bool keep = false;
            if (options.include_only_primitives_sharing_edge
                || !options.include_only_entirely_contained)
                keep = face_any_edge_in(input, fi, source);
            else
                keep = face_all_edges_in(input, fi, source);
            if (keep)
                out.insert(static_cast<geometry::GroupId>(fi));
        }
        return out;
    }

    if (from == geometry::GroupDomain::Edge && to == geometry::GroupDomain::Vertex) {
        // Entirely contained: only the beginning corner of each half-edge in the group.
        for (const auto& c : corners) {
            const auto& face = input.faces()[static_cast<size_t>(c.face)];
            const int next_point = face[(static_cast<size_t>(c.local) + 1) % face.size()];
            const geometry::GroupId eid = geometry::edge_group_id(c.point, next_point);
            if (source.count(eid) == 0)
                continue;
            if (options.include_only_entirely_contained) {
                out.insert(static_cast<geometry::GroupId>(c.global));
            } else {
                out.insert(static_cast<geometry::GroupId>(c.global));
                const int next_local = (c.local + 1) % static_cast<int>(face.size());
                // Approximate opposite endpoint corner on same face.
                int cursor = 0;
                for (size_t fi = 0; fi < static_cast<size_t>(c.face); ++fi)
                    cursor += static_cast<int>(input.faces()[fi].size());
                out.insert(static_cast<geometry::GroupId>(cursor + next_local));
            }
        }
        return out;
    }

    if (from == geometry::GroupDomain::Face && to == geometry::GroupDomain::Point)
        return points_of_faces(input, source);

    if (from == geometry::GroupDomain::Face && to == geometry::GroupDomain::Edge) {
        if (options.include_only_entirely_contained) {
            for (const auto& entry : bmesh.edges) {
                const geometry::BMeshEdge& edge = entry.second;
                const bool f0_ok =
                    edge.face0 < 0
                    || source.count(static_cast<geometry::GroupId>(edge.face0)) != 0;
                const bool f1_ok =
                    edge.face1 < 0
                    || source.count(static_cast<geometry::GroupId>(edge.face1)) != 0;
                const bool touches =
                    (edge.face0 >= 0
                     && source.count(static_cast<geometry::GroupId>(edge.face0)) != 0)
                    || (edge.face1 >= 0
                        && source.count(static_cast<geometry::GroupId>(edge.face1)) != 0);
                if (touches && f0_ok && f1_ok)
                    out.insert(entry.first);
            }
        } else {
            for (const auto& entry : bmesh.edges) {
                const geometry::BMeshEdge& edge = entry.second;
                if ((edge.face0 >= 0
                     && source.count(static_cast<geometry::GroupId>(edge.face0)) != 0)
                    || (edge.face1 >= 0
                        && source.count(static_cast<geometry::GroupId>(edge.face1)) != 0))
                    out.insert(entry.first);
            }
        }
        return out;
    }

    if (from == geometry::GroupDomain::Face && to == geometry::GroupDomain::Vertex) {
        for (const auto& c : corners) {
            if (source.count(static_cast<geometry::GroupId>(c.face)) != 0)
                out.insert(static_cast<geometry::GroupId>(c.global));
        }
        return out;
    }

    if (from == geometry::GroupDomain::Vertex && to == geometry::GroupDomain::Point)
        return points_of_corners(corners, source);

    if (from == geometry::GroupDomain::Vertex && to == geometry::GroupDomain::Edge) {
        const auto points = points_of_corners(corners, source);
        for (const auto& entry : bmesh.edges) {
            const auto ends = geometry::edge_group_points(entry.first);
            const bool a = points.count(static_cast<geometry::GroupId>(ends[0])) != 0;
            const bool b = points.count(static_cast<geometry::GroupId>(ends[1])) != 0;
            if (options.include_only_entirely_contained ? (a && b) : (a || b))
                out.insert(entry.first);
        }
        return out;
    }

    if (from == geometry::GroupDomain::Vertex && to == geometry::GroupDomain::Face) {
        const auto points = points_of_corners(corners, source);
        for (size_t fi = 0; fi < input.faces().size(); ++fi) {
            bool keep = false;
            if (options.include_only_primitives_sharing_edge)
                keep = face_shares_edge_in_points(input, fi, points);
            else if (options.include_only_entirely_contained)
                keep = face_all_points_in(input, fi, points);
            else
                keep = face_any_point_in(input, fi, points);
            if (keep)
                out.insert(static_cast<geometry::GroupId>(fi));
        }
        return out;
    }

    return out;
}

std::unordered_set<geometry::GroupId> filter_boundary(const data::PcgGeometry& input,
                                                      const GroupPromoteOptions& options,
                                                      geometry::GroupDomain to,
                                                      const std::unordered_set<geometry::GroupId>& members)
{
    if (members.empty())
        return {};

    const geometry::BMesh bmesh = geometry::bmesh_from_geometry(input);
    const bool include_unshared_edges = options.include_unshared_edges;
    // Curve-vs-polygon unshared distinction is not modeled yet; keep the Houdini toggle for UI parity.
    (void)options.include_all_unshared_curve_edges;
    std::unordered_set<geometry::GroupId> out;

    if (to == geometry::GroupDomain::Edge) {
        std::unordered_set<geometry::GroupId> interior_faces;
        for (size_t fi = 0; fi < input.faces().size(); ++fi) {
            if (face_all_edges_in(input, fi, members)
                || face_any_edge_in(input, fi, members))
                interior_faces.insert(static_cast<geometry::GroupId>(fi));
        }

        for (geometry::GroupId eid : members) {
            const auto it = bmesh.edges.find(eid);
            if (it == bmesh.edges.end())
                continue;
            const geometry::BMeshEdge& edge = it->second;
            const bool in0 =
                edge.face0 >= 0
                && interior_faces.count(static_cast<geometry::GroupId>(edge.face0)) != 0;
            const bool in1 =
                edge.face1 >= 0
                && interior_faces.count(static_cast<geometry::GroupId>(edge.face1)) != 0;
            if (edge.face1 < 0) {
                if (include_unshared_edges && in0)
                    out.insert(eid);
                continue;
            }
            if (in0 != in1)
                out.insert(eid);
        }

        for (geometry::GroupId eid : connectivity_boundary_edges(input, options, bmesh)) {
            if (members.count(eid) != 0)
                out.insert(eid);
        }
        return out;
    }

    if (to == geometry::GroupDomain::Face) {
        std::unordered_set<geometry::GroupId> attr_boundary_edges =
            connectivity_boundary_edges(input, options, bmesh);
        for (geometry::GroupId fid : members) {
            if (fid < 0 || static_cast<size_t>(fid) >= input.faces().size())
                continue;
            const auto& face = input.faces()[static_cast<size_t>(fid)];
            const size_t n = face.size();
            bool on_boundary = false;
            bool shares_attr_boundary_point = false;
            for (size_t i = 0; i < n; ++i) {
                const geometry::GroupId key =
                    geometry::edge_group_id(face[i], face[(i + 1) % n]);
                if (attr_boundary_edges.count(key) != 0) {
                    on_boundary = true;
                    break;
                }
                if (options.include_all_primitives_sharing_attribute_boundary_points) {
                    for (geometry::GroupId ae : attr_boundary_edges) {
                        const auto ends = geometry::edge_group_points(ae);
                        if (face[i] == ends[0] || face[i] == ends[1]) {
                            shares_attr_boundary_point = true;
                            break;
                        }
                    }
                }
                const auto it = bmesh.edges.find(key);
                if (it == bmesh.edges.end())
                    continue;
                const geometry::BMeshEdge& edge = it->second;
                if (edge.face1 < 0) {
                    if (include_unshared_edges) {
                        on_boundary = true;
                        break;
                    }
                    continue;
                }
                const int other = edge.face0 == static_cast<int>(fid) ? edge.face1 : edge.face0;
                if (members.count(static_cast<geometry::GroupId>(other)) == 0) {
                    on_boundary = true;
                    break;
                }
            }
            if (on_boundary
                || (options.include_all_primitives_sharing_attribute_boundary_points
                    && shares_attr_boundary_point))
                out.insert(fid);
        }
        return out;
    }

    if (to == geometry::GroupDomain::Point || to == geometry::GroupDomain::Vertex) {
        std::unordered_set<geometry::GroupId> point_members = members;
        if (to == geometry::GroupDomain::Vertex)
            point_members = points_of_corners(build_corners(input), members);

        std::unordered_set<geometry::GroupId> boundary_points;
        for (const auto& entry : bmesh.edges) {
            const auto ends = geometry::edge_group_points(entry.first);
            const bool a = point_members.count(static_cast<geometry::GroupId>(ends[0])) != 0;
            const bool b = point_members.count(static_cast<geometry::GroupId>(ends[1])) != 0;
            if (a == b)
                continue;
            if (entry.second.face1 < 0 && !include_unshared_edges)
                continue;
            if (a)
                boundary_points.insert(static_cast<geometry::GroupId>(ends[0]));
            if (b)
                boundary_points.insert(static_cast<geometry::GroupId>(ends[1]));
        }
        for (geometry::GroupId eid : connectivity_boundary_edges(input, options, bmesh)) {
            const auto ends = geometry::edge_group_points(eid);
            if (point_members.count(static_cast<geometry::GroupId>(ends[0])) != 0)
                boundary_points.insert(static_cast<geometry::GroupId>(ends[0]));
            if (point_members.count(static_cast<geometry::GroupId>(ends[1])) != 0)
                boundary_points.insert(static_cast<geometry::GroupId>(ends[1]));
        }

        if (to == geometry::GroupDomain::Point)
            return boundary_points;
        return corners_of_points(build_corners(input), boundary_points);
    }

    return members;
}

void remove_degenerate_bridges(const data::PcgGeometry& input,
                               geometry::GroupDomain to,
                               std::unordered_set<geometry::GroupId>& members)
{
    if (to == geometry::GroupDomain::Edge) {
        for (auto it = members.begin(); it != members.end();) {
            if (edge_zero_length(input, *it))
                it = members.erase(it);
            else
                ++it;
        }
        return;
    }
    if (to == geometry::GroupDomain::Point) {
        // Drop points that only appear as coincident endpoints of zero-length edges.
        const geometry::BMesh bmesh = geometry::bmesh_from_geometry(input);
        for (auto it = members.begin(); it != members.end();) {
            bool keep = false;
            for (const auto& entry : bmesh.edges) {
                const auto ends = geometry::edge_group_points(entry.first);
                if (ends[0] != static_cast<int>(*it) && ends[1] != static_cast<int>(*it))
                    continue;
                if (!edge_zero_length(input, entry.first)) {
                    keep = true;
                    break;
                }
            }
            if (!keep)
                it = members.erase(it);
            else
                ++it;
        }
    }
}

void write_integer_attribute(data::PcgGeometry& geometry,
                             geometry::GroupDomain domain,
                             const std::string& name,
                             const std::unordered_set<geometry::GroupId>& members)
{
    data::AttributeOwner owner = data::AttributeOwner::Point;
    size_t count = geometry.points().size();
    if (domain == geometry::GroupDomain::Face) {
        owner = data::AttributeOwner::Primitive;
        count = geometry.faces().size();
    } else if (domain == geometry::GroupDomain::Vertex) {
        owner = data::AttributeOwner::Vertex;
        count = static_cast<size_t>(std::max(0, geometry.corner_count()));
    } else if (domain != geometry::GroupDomain::Point) {
        return;
    }

    auto& attr = geometry.attributes().create_int(owner, name, 1, {0});
    attr.resize(count);
    auto& values = attr.int_values_mut();
    for (size_t i = 0; i < values.size(); ++i)
        values[i] = 0;
    for (geometry::GroupId id : members) {
        if (id < 0 || static_cast<size_t>(id) >= values.size())
            continue;
        values[static_cast<size_t>(id)] = 1;
    }
}

bool group_name_glob_match(const std::string& name, const std::string& pattern)
{
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
}

std::vector<std::string> split_group_name_tokens(const std::string& text)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : text) {
        if (ch == ' ' || ch == '\t' || ch == ',') {
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
}

geometry::GroupDomain parse_group_delete_domain(const std::string& type)
{
    if (type == "points" || type == "point")
        return geometry::GroupDomain::Point;
    if (type == "edges" || type == "edge")
        return geometry::GroupDomain::Edge;
    if (type == "vertices" || type == "vertex")
        return geometry::GroupDomain::Vertex;
    if (type == "primitives" || type == "primitive" || type == "face")
        return geometry::GroupDomain::Face;
    return geometry::GroupDomain::Face;
}

std::vector<geometry::GroupDomain> domains_for_group_delete_type(const std::string& type)
{
    if (type == "any")
        return {geometry::GroupDomain::Point, geometry::GroupDomain::Edge,
                geometry::GroupDomain::Face, geometry::GroupDomain::Vertex};
    return {parse_group_delete_domain(type)};
}

bool group_name_matches_patterns(const std::string& name,
                                 const std::vector<std::string>& patterns)
{
    for (const auto& pattern : patterns) {
        if (group_name_glob_match(name, pattern))
            return true;
    }
    return false;
}

} // namespace

data::PcgGeometry group_promote(const data::PcgGeometry& input, const GroupPromoteOptions& options)
{
    data::PcgGeometry result = input;
    if (options.group_name.empty())
        return result;

    const geometry::GroupDomain from = parse_domain(options.from_domain);
    const geometry::GroupDomain to = parse_domain(options.to_domain);
    const std::string dest_name =
        options.new_name.empty() ? options.group_name : options.new_name;

    const auto source = input.groups().members(from, options.group_name);
    std::unordered_set<geometry::GroupId> promoted =
        promote_members(input, options, from, to, source);

    if (options.include_only_on_boundary)
        promoted = filter_boundary(input, options, to, promoted);

    if (options.remove_degenerate_bridges)
        remove_degenerate_bridges(input, to, promoted);

    const bool same_slot = (from == to && dest_name == options.group_name);
    result.groups().clear_group(to, dest_name);

    if (options.output_as_integer_attribute
        && (to == geometry::GroupDomain::Point || to == geometry::GroupDomain::Face
            || to == geometry::GroupDomain::Vertex)) {
        write_integer_attribute(result, to, dest_name, promoted);
        // Houdini deletes the group after writing the attribute.
    } else {
        for (geometry::GroupId id : promoted)
            result.groups().add(to, dest_name, id);
    }

    if (!options.keep_original_group && !same_slot)
        result.groups().clear_group(from, options.group_name);

    return result;
}

std::vector<GroupDeleteRule> parse_group_delete_rules(const nlohmann::json& data)
{
    std::vector<GroupDeleteRule> rules;
    nlohmann::json array = nlohmann::json::array();

    if (data.contains("deletions")) {
        if (data["deletions"].is_array())
            array = data["deletions"];
        else if (data["deletions"].is_string()) {
            try {
                array = nlohmann::json::parse(data["deletions"].get<std::string>());
            } catch (...) {
                array = nlohmann::json::array();
            }
        }
    }

    if (array.is_array()) {
        for (const auto& item : array) {
            if (!item.is_object())
                continue;
            GroupDeleteRule rule;
            if (item.contains("enabled")) {
                if (item["enabled"].is_boolean())
                    rule.enabled = item["enabled"].get<bool>();
                else if (item["enabled"].is_string())
                    rule.enabled = item["enabled"].get<std::string>() == "true";
            }
            rule.group_type = item.value("groupType", std::string("any"));
            rule.group_names = item.value("groupNames", std::string(""));
            rules.push_back(rule);
        }
    }

    if (rules.empty()) {
        GroupDeleteRule rule;
        rule.enabled = true;
        rule.group_type = "any";
        rule.group_names = data.value("groupNames", std::string(""));
        rules.push_back(rule);
    }
    return rules;
}

data::PcgGeometry group_delete(const data::PcgGeometry& input, const GroupDeleteOptions& options)
{
    data::PcgGeometry result = input;

    for (const GroupDeleteRule& rule : options.rules) {
        if (!rule.enabled)
            continue;

        const auto patterns = split_group_name_tokens(rule.group_names);
        if (patterns.empty())
            continue;

        for (const geometry::GroupDomain domain : domains_for_group_delete_type(rule.group_type)) {
            for (const std::string& name : result.groups().group_names(domain)) {
                if (group_name_matches_patterns(name, patterns))
                    result.groups().clear_group(domain, name);
            }
        }
    }

    if (options.delete_unused_groups)
        remove_empty_groups(result);

    return result;
}

} // namespace pcg::internal::elements
