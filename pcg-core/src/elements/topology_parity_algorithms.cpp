#include "elements/topology_parity_algorithms.hpp"

#include "elements/spline_algorithms.hpp"
#include "elements/vehicle_modeling_algorithms.hpp"
#include "data/pcg_geometry.hpp"
#include "geometry/group_table.hpp"
#include "geometry/spline_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pcg::internal::elements {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kEps = 1e-12;

data::PcgVec3 sub(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

data::PcgVec3 add(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

data::PcgVec3 mul(const data::PcgVec3& a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}

double dot(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

data::PcgVec3 cross(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

double length(const data::PcgVec3& v)
{
    return std::sqrt(std::max(0.0, dot(v, v)));
}

data::PcgVec3 normalize(const data::PcgVec3& v)
{
    const double len = length(v);
    if (len <= kEps)
        return {0.0, 1.0, 0.0};
    return mul(v, 1.0 / len);
}

void compute_bbox(const data::PcgGeometry& input,
                  data::PcgVec3& bmin,
                  data::PcgVec3& bmax)
{
    bmin = {std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max()};
    bmax = {-std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::max(),
            -std::numeric_limits<double>::max()};
    for (const auto& p : input.points()) {
        bmin.x = std::min(bmin.x, p.x);
        bmin.y = std::min(bmin.y, p.y);
        bmin.z = std::min(bmin.z, p.z);
        bmax.x = std::max(bmax.x, p.x);
        bmax.y = std::max(bmax.y, p.y);
        bmax.z = std::max(bmax.z, p.z);
    }
    if (input.points().empty()) {
        bmin = {};
        bmax = {};
    }
}

data::PcgVec3 face_normal(const data::PcgGeometry& geometry, size_t face_index)
{
    const auto& face = geometry.faces()[face_index];
    if (face.size() < 3)
        return {0.0, 1.0, 0.0};
    data::PcgVec3 n{};
    for (size_t i = 0; i < face.size(); ++i) {
        const auto& p0 = geometry.points()[static_cast<size_t>(face[i])];
        const auto& p1 = geometry.points()[static_cast<size_t>(face[(i + 1) % face.size()])];
        n.x += (p0.y - p1.y) * (p0.z + p1.z);
        n.y += (p0.z - p1.z) * (p0.x + p1.x);
        n.z += (p0.x - p1.x) * (p0.y + p1.y);
    }
    return normalize(n);
}

double face_area(const data::PcgGeometry& geometry, size_t face_index)
{
    const auto& face = geometry.faces()[face_index];
    if (face.size() < 3)
        return 0.0;
    const auto& p0 = geometry.points()[static_cast<size_t>(face[0])];
    double area = 0.0;
    for (size_t i = 1; i + 1 < face.size(); ++i) {
        const auto& p1 = geometry.points()[static_cast<size_t>(face[i])];
        const auto& p2 = geometry.points()[static_cast<size_t>(face[i + 1])];
        area += 0.5 * length(cross(sub(p1, p0), sub(p2, p0)));
    }
    return area;
}

double face_perimeter(const data::PcgGeometry& geometry, size_t face_index)
{
    const auto& face = geometry.faces()[face_index];
    if (face.size() < 2)
        return 0.0;
    double peri = 0.0;
    for (size_t i = 0; i < face.size(); ++i) {
        const auto& a = geometry.points()[static_cast<size_t>(face[i])];
        const auto& b = geometry.points()[static_cast<size_t>(face[(i + 1) % face.size()])];
        peri += length(sub(a, b));
    }
    return peri;
}

uint64_t edge_key(int a, int b)
{
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    return (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
           static_cast<uint32_t>(hi);
}

uint32_t expand_bits10(uint32_t v)
{
    v &= 0x3FFu;
    v = (v | (v << 16)) & 0x30000FFu;
    v = (v | (v << 8)) & 0x300F00Fu;
    v = (v | (v << 4)) & 0x30C30C3u;
    v = (v | (v << 2)) & 0x9249249u;
    return v;
}

uint64_t morton3(uint32_t x, uint32_t y, uint32_t z)
{
    return static_cast<uint64_t>(expand_bits10(x)) |
           (static_cast<uint64_t>(expand_bits10(y)) << 1) |
           (static_cast<uint64_t>(expand_bits10(z)) << 2);
}

uint32_t sort_lcg(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

std::string resolve_sort_method(const SortDomainOptions& options)
{
    if (options.method == "axis") {
        if (options.axis == "y" || options.axis == "z")
            return options.axis;
        return "x";
    }
    return options.method.empty() ? "nochange" : options.method;
}

data::PcgVec3 element_position(const data::PcgGeometry& geometry,
                               bool primitives,
                               size_t index)
{
    if (!primitives) {
        if (index >= geometry.points().size())
            return {};
        return geometry.points()[index];
    }
    if (index >= geometry.faces().size())
        return {};
    const auto& face = geometry.faces()[index];
    if (face.empty())
        return {};
    data::PcgVec3 c{};
    for (int pi : face)
        c = add(c, geometry.points()[static_cast<size_t>(pi)]);
    return mul(c, 1.0 / static_cast<double>(face.size()));
}

std::vector<int> corner_offsets(const data::PcgGeometry& geometry)
{
    std::vector<int> offsets(geometry.faces().size() + 1, 0);
    for (size_t i = 0; i < geometry.faces().size(); ++i)
        offsets[i + 1] = offsets[i] + static_cast<int>(geometry.faces()[i].size());
    return offsets;
}

bool read_numeric_attr_component(const data::AttributeArray& attr,
                                 size_t index,
                                 int component,
                                 double& out)
{
    const int tuple = std::max(1, attr.schema().tuple_size);
    const int comp = std::clamp(component, 0, tuple - 1);
    if (index >= attr.size())
        return false;
    if (attr.schema().type == data::AttributeType::Float) {
        out = attr.float_values()[index * static_cast<size_t>(tuple) +
                                  static_cast<size_t>(comp)];
        return true;
    }
    if (attr.schema().type == data::AttributeType::Int) {
        out = static_cast<double>(
            attr.int_values()[index * static_cast<size_t>(tuple) +
                              static_cast<size_t>(comp)]);
        return true;
    }
    return false;
}

bool read_string_attr_component(const data::AttributeArray& attr,
                                size_t index,
                                int component,
                                std::string& out)
{
    if (attr.schema().type != data::AttributeType::String)
        return false;
    const int tuple = std::max(1, attr.schema().tuple_size);
    const int comp = std::clamp(component, 0, tuple - 1);
    if (index >= attr.size())
        return false;
    out = attr.string_values()[index * static_cast<size_t>(tuple) +
                               static_cast<size_t>(comp)];
    return true;
}

std::vector<size_t> select_sort_slots(const data::PcgGeometry& geometry,
                                      bool primitives,
                                      const std::string& group)
{
    const size_t count = primitives ? geometry.faces().size() : geometry.points().size();
    std::vector<size_t> slots;
    slots.reserve(count);
    if (group.empty()) {
        for (size_t i = 0; i < count; ++i)
            slots.push_back(i);
        return slots;
    }
    const auto domain =
        primitives ? geometry::GroupDomain::Face : geometry::GroupDomain::Point;
    const auto members = geometry.groups().eval(domain, group);
    for (size_t i = 0; i < count; ++i) {
        if (members.count(static_cast<geometry::GroupId>(i)) > 0)
            slots.push_back(i);
    }
    return slots;
}

std::vector<size_t> compute_sort_order(const data::PcgGeometry& geometry,
                                       const SortDomainOptions& options,
                                       bool primitives)
{
    const size_t count = primitives ? geometry.faces().size() : geometry.points().size();
    std::vector<size_t> identity(count);
    std::iota(identity.begin(), identity.end(), 0);
    if (count == 0)
        return identity;

    const std::string method = resolve_sort_method(options);
    auto slots = select_sort_slots(geometry, primitives, options.group);
    if (slots.empty())
        return identity;

    std::vector<size_t> members = slots;
    const data::AttributeOwner owner =
        primitives ? data::AttributeOwner::Primitive : data::AttributeOwner::Point;

    const data::AttributeArray* sort_attr =
        (!options.attribute_name.empty())
            ? geometry.attributes().find(owner, options.attribute_name)
            : nullptr;
    const data::AttributeArray* ordering_attr =
        (!options.ordering_attribute.empty())
            ? geometry.attributes().find(owner, options.ordering_attribute)
            : nullptr;

    auto stable_tie = [&](size_t a, size_t b) -> int {
        if (options.sort_indices && options.combine_sort_indices && ordering_attr &&
            ordering_attr->schema().type == data::AttributeType::Int) {
            double ka = 0.0;
            double kb = 0.0;
            const bool ok_a = read_numeric_attr_component(*ordering_attr, a, 0, ka);
            const bool ok_b = read_numeric_attr_component(*ordering_attr, b, 0, kb);
            if (ok_a && ok_b) {
                if (ka < kb)
                    return -1;
                if (ka > kb)
                    return 1;
            }
        }
        if (a < b)
            return -1;
        if (a > b)
            return 1;
        return 0;
    };

    auto finish_members = [&](std::vector<size_t>& ordered) {
        if (options.reverse)
            std::reverse(ordered.begin(), ordered.end());
    };

    if (method == "nochange") {
        finish_members(members);
    } else if (method == "reverse") {
        std::reverse(members.begin(), members.end());
        finish_members(members);
    } else if (method == "shift") {
        const int n = static_cast<int>(members.size());
        if (n > 0) {
            int off = options.offset % n;
            if (off < 0)
                off += n;
            std::vector<size_t> shifted(members.size());
            for (int i = 0; i < n; ++i)
                shifted[static_cast<size_t>((i + off) % n)] = members[static_cast<size_t>(i)];
            members = std::move(shifted);
        }
        finish_members(members);
    } else if (method == "random") {
        uint32_t state = static_cast<uint32_t>(options.seed) ^ 0xA5A5A5A5u ^
                         static_cast<uint32_t>(count * 2654435761u);
        std::vector<std::pair<uint32_t, size_t>> keyed;
        keyed.reserve(members.size());
        for (size_t idx : members)
            keyed.push_back({sort_lcg(state), idx});
        std::stable_sort(keyed.begin(), keyed.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (size_t i = 0; i < members.size(); ++i)
            members[i] = keyed[i].second;
        finish_members(members);
    } else if (method == "vertexorder") {
        if (!primitives) {
            std::vector<char> seen(count, 0);
            std::vector<size_t> ordered;
            ordered.reserve(count);
            auto consider = [&](size_t pi) {
                if (pi >= count || seen[pi])
                    return;
                seen[pi] = 1;
                if (options.group.empty() ||
                    std::binary_search(slots.begin(), slots.end(), pi))
                    ordered.push_back(pi);
            };
            for (const auto& face : geometry.faces()) {
                for (int pi : face)
                    consider(static_cast<size_t>(pi));
            }
            for (size_t pi : slots)
                consider(pi);
            members = std::move(ordered);
        }
        finish_members(members);
    } else if (method == "primindex") {
        if (!primitives) {
            std::vector<int> lowest(count, std::numeric_limits<int>::max());
            for (size_t fi = 0; fi < geometry.faces().size(); ++fi) {
                for (int pi : geometry.faces()[fi]) {
                    if (pi < 0 || static_cast<size_t>(pi) >= count)
                        continue;
                    lowest[static_cast<size_t>(pi)] =
                        std::min(lowest[static_cast<size_t>(pi)], static_cast<int>(fi));
                }
            }
            std::stable_sort(members.begin(), members.end(), [&](size_t a, size_t b) {
                const int ka = lowest[a] == std::numeric_limits<int>::max() ? -1 : lowest[a];
                const int kb = lowest[b] == std::numeric_limits<int>::max() ? -1 : lowest[b];
                if (ka != kb)
                    return ka < kb;
                return stable_tie(a, b) < 0;
            });
        }
        finish_members(members);
    } else if (method == "reorder") {
        const data::AttributeArray* idx_attr =
            ordering_attr != nullptr
                ? ordering_attr
                : (sort_attr != nullptr ? sort_attr : nullptr);
        if (idx_attr && idx_attr->schema().type == data::AttributeType::Int) {
            std::vector<std::pair<int64_t, size_t>> keyed;
            keyed.reserve(members.size());
            for (size_t idx : members) {
                double key = 0.0;
                if (!read_numeric_attr_component(*idx_attr, idx, 0, key))
                    key = static_cast<double>(idx);
                keyed.push_back({static_cast<int64_t>(key), idx});
            }
            std::stable_sort(keyed.begin(), keyed.end(),
                             [](const auto& a, const auto& b) { return a.first < b.first; });
            // Values are destination indices: build members such that
            // destination slots get sources sorted by their index value.
            // Reorder: attr[source] = destination => sort sources by attr value.
            for (size_t i = 0; i < members.size(); ++i)
                members[i] = keyed[i].second;
        }
        finish_members(members);
    } else {
        enum class KeyType { Number, String };
        struct Key {
            KeyType type = KeyType::Number;
            double number = 0.0;
            std::string text;
        };
        auto key_of = [&](size_t index) -> Key {
            Key key;
            if (method == "attribute" && sort_attr) {
                if (sort_attr->schema().type == data::AttributeType::String) {
                    key.type = KeyType::String;
                    if (!read_string_attr_component(*sort_attr, index, options.component,
                                                    key.text))
                        key.text.clear();
                    return key;
                }
                if (read_numeric_attr_component(*sort_attr, index, options.component,
                                                key.number))
                    return key;
            }
            const data::PcgVec3 p = element_position(geometry, primitives, index);
            if (method == "x") {
                key.number = p.x;
            } else if (method == "y") {
                key.number = p.y;
            } else if (method == "z") {
                key.number = p.z;
            } else if (method == "proximity") {
                key.number = length(sub(p, options.proximity_point));
            } else if (method == "vector") {
                key.number = dot(p, options.vector);
            } else if (method == "spatial") {
                data::PcgVec3 bmin;
                data::PcgVec3 bmax;
                compute_bbox(geometry, bmin, bmax);
                const double dx = std::max(bmax.x - bmin.x, kEps);
                const double dy = std::max(bmax.y - bmin.y, kEps);
                const double dz = std::max(bmax.z - bmin.z, kEps);
                const auto q = [&](double v, double lo, double span) {
                    const double t = std::clamp((v - lo) / span, 0.0, 1.0);
                    return static_cast<uint32_t>(t * 1023.0 + 0.5);
                };
                key.number = static_cast<double>(
                    morton3(q(p.x, bmin.x, dx), q(p.y, bmin.y, dy), q(p.z, bmin.z, dz)));
            } else {
                key.number = p.x;
            }
            return key;
        };

        std::stable_sort(members.begin(), members.end(), [&](size_t a, size_t b) {
            const Key ka = key_of(a);
            const Key kb = key_of(b);
            if (ka.type == KeyType::String || kb.type == KeyType::String) {
                if (ka.text != kb.text)
                    return ka.text < kb.text;
                return stable_tie(a, b) < 0;
            }
            if (ka.number != kb.number)
                return ka.number < kb.number;
            return stable_tie(a, b) < 0;
        });
        finish_members(members);
    }

    std::vector<size_t> order = identity;
    for (size_t i = 0; i < slots.size() && i < members.size(); ++i)
        order[slots[i]] = members[i];
    return order;
}

data::PcgGeometry apply_point_permutation(const data::PcgGeometry& input,
                                          const std::vector<size_t>& order)
{
    data::PcgGeometry output;
    const size_t n = order.size();
    std::vector<data::PcgVec3> points(n);
    std::vector<int> old_to_new(input.points().size(), -1);
    data::GeometryElementRemap remap;
    remap.points.resize(n, -1);
    for (size_t dest = 0; dest < n; ++dest) {
        const size_t src = order[dest];
        points[dest] = input.points()[src];
        remap.points[dest] = static_cast<int>(src);
        if (src < old_to_new.size())
            old_to_new[src] = static_cast<int>(dest);
    }
    output.points_mut() = std::move(points);
    output.faces_mut() = input.faces();
    for (auto& face : output.faces_mut()) {
        for (int& pi : face) {
            if (pi >= 0 && static_cast<size_t>(pi) < old_to_new.size())
                pi = old_to_new[static_cast<size_t>(pi)];
        }
    }
    remap.primitives.resize(output.faces().size());
    std::iota(remap.primitives.begin(), remap.primitives.end(), 0);
    remap.vertices.resize(static_cast<size_t>(output.corner_count()));
    std::iota(remap.vertices.begin(), remap.vertices.end(), 0);
    data::propagate_geometry_data(input, output, remap);
    return output;
}

data::PcgGeometry apply_primitive_permutation(const data::PcgGeometry& input,
                                              const std::vector<size_t>& order)
{
    data::PcgGeometry output;
    output.points_mut() = input.points();
    std::vector<std::vector<int>> faces;
    faces.reserve(order.size());
    for (size_t src : order)
        faces.push_back(input.faces()[src]);
    output.faces_mut() = std::move(faces);

    data::GeometryElementRemap remap;
    remap.points.resize(output.points().size());
    std::iota(remap.points.begin(), remap.points.end(), 0);
    remap.primitives.resize(order.size(), -1);
    for (size_t dest = 0; dest < order.size(); ++dest)
        remap.primitives[dest] = static_cast<int>(order[dest]);

    const auto src_offsets = corner_offsets(input);
    remap.vertices.resize(static_cast<size_t>(output.corner_count()), -1);
    size_t dest_corner = 0;
    for (size_t dest = 0; dest < order.size(); ++dest) {
        const size_t src = order[dest];
        const int src_begin = src_offsets[src];
        const size_t n = input.faces()[src].size();
        for (size_t c = 0; c < n; ++c)
            remap.vertices[dest_corner++] = src_begin + static_cast<int>(c);
    }
    data::propagate_geometry_data(input, output, remap);
    return output;
}

void write_sort_indices(data::PcgGeometry& geometry,
                        bool primitives,
                        const std::vector<size_t>& order,
                        const std::string& attribute_name)
{
    if (attribute_name.empty())
        return;
    const size_t count = order.size();
    // order[dest] = source => attribute[source] = dest
    std::vector<int64_t> values(count, 0);
    for (size_t dest = 0; dest < count; ++dest) {
        const size_t src = order[dest];
        if (src < count)
            values[src] = static_cast<int64_t>(dest);
    }
    const auto owner =
        primitives ? data::AttributeOwner::Primitive : data::AttributeOwner::Point;
    auto& attr = geometry.attributes().create_int(owner, attribute_name, 1);
    attr.resize(count);
    attr.int_values_mut() = std::move(values);
}

} // namespace

void set_detail_int(data::PcgGeometry& geometry, const std::string& name, int64_t value)
{
    data::AttributeArray* existing =
        geometry.attributes().find(data::AttributeOwner::Detail, name);
    if (!existing) {
        auto& attr = geometry.attributes().create_int(data::AttributeOwner::Detail, name, 1);
        attr.resize(1);
        attr.int_values_mut()[0] = value;
        return;
    }
    existing->resize(1);
    if (existing->schema().type == data::AttributeType::Int)
        existing->int_values_mut()[0] = value;
}

void set_detail_float(data::PcgGeometry& geometry, const std::string& name, double value)
{
    data::AttributeArray* existing =
        geometry.attributes().find(data::AttributeOwner::Detail, name);
    if (!existing) {
        auto& attr = geometry.attributes().create_float(data::AttributeOwner::Detail, name, 1);
        attr.resize(1);
        attr.float_values_mut()[0] = value;
        return;
    }
    existing->resize(1);
    if (existing->schema().type == data::AttributeType::Float)
        existing->float_values_mut()[0] = value;
}

int read_iterations_attribute(const data::PcgGeometry& geometry,
                              const std::string& name,
                              int fallback)
{
    if (name.empty())
        return fallback;
    if (const data::AttributeArray* detail =
            geometry.attributes().find(data::AttributeOwner::Detail, name)) {
        if (detail->schema().type == data::AttributeType::Int && !detail->int_values().empty())
            return static_cast<int>(detail->int_values()[0]);
        if (detail->schema().type == data::AttributeType::Float &&
            !detail->float_values().empty())
            return static_cast<int>(std::lround(detail->float_values()[0]));
    }
    if (const data::AttributeArray* prim =
            geometry.attributes().find(data::AttributeOwner::Primitive, name)) {
        if (prim->schema().type == data::AttributeType::Int && !prim->int_values().empty())
            return static_cast<int>(prim->int_values()[0]);
        if (prim->schema().type == data::AttributeType::Float && !prim->float_values().empty())
            return static_cast<int>(std::lround(prim->float_values()[0]));
    }
    return fallback;
}

data::PcgGeometry measure_mesh_geometry(const data::PcgGeometry& input,
                                        const MeasureMeshOptions& options)
{
    data::PcgGeometry output = input;
    const size_t face_count = output.faces().size();
    auto& attr = output.attributes().create_float(
        data::AttributeOwner::Primitive, options.attribute_name, 1);
    attr.resize(face_count);

    data::PcgVec3 bmin{}, bmax{};
    compute_bbox(input, bmin, bmax);
    const data::PcgVec3 size_v = sub(bmax, bmin);

    for (size_t fi = 0; fi < face_count; ++fi) {
        double value = 0.0;
        if (options.measure == "area")
            value = face_area(input, fi);
        else if (options.measure == "size")
            value = std::max({size_v.x, size_v.y, size_v.z});
        else if (options.measure == "volume_approx")
            value = std::abs(size_v.x * size_v.y * size_v.z);
        else
            value = face_perimeter(input, fi);
        attr.float_values_mut()[fi] = value;
    }

    double total = 0.0;
    for (double v : attr.float_values())
        total += v;
    set_detail_float(output, options.attribute_name, total);
    set_detail_float(output, "bbox_size_x", size_v.x);
    set_detail_float(output, "bbox_size_y", size_v.y);
    set_detail_float(output, "bbox_size_z", size_v.z);
    return output;
}

data::PcgGeometry bound_mesh_geometry(const data::PcgGeometry& input,
                                      const BoundMeshOptions& options)
{
    data::PcgVec3 bmin{}, bmax{};
    compute_bbox(input, bmin, bmax);
    bmin = sub(bmin, {options.padding, options.padding, options.padding});
    bmax = add(bmax, {options.padding, options.padding, options.padding});

    data::PcgGeometry output;
    auto& pts = output.points_mut();
    pts = {
        {bmin.x, bmin.y, bmin.z}, {bmax.x, bmin.y, bmin.z},
        {bmax.x, bmin.y, bmax.z}, {bmin.x, bmin.y, bmax.z},
        {bmin.x, bmax.y, bmin.z}, {bmax.x, bmax.y, bmin.z},
        {bmax.x, bmax.y, bmax.z}, {bmin.x, bmax.y, bmax.z},
    };
    output.faces_mut() = {
        {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
        {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0},
    };
    output.detail() = input.detail();
    set_detail_float(output, "bbox_min_x", bmin.x);
    set_detail_float(output, "bbox_min_y", bmin.y);
    set_detail_float(output, "bbox_min_z", bmin.z);
    set_detail_float(output, "bbox_max_x", bmax.x);
    set_detail_float(output, "bbox_max_y", bmax.y);
    set_detail_float(output, "bbox_max_z", bmax.z);
    return output;
}

data::PcgGeometry compute_normals_geometry(const data::PcgGeometry& input,
                                           const ComputeNormalsOptions& options)
{
    data::PcgGeometry output = input;
    if (options.shade_mode == "smooth")
        output.detail().shade_mode = data::ShadeMode::Smooth;
    else if (options.shade_mode == "flat")
        output.detail().shade_mode = data::ShadeMode::Flat;
    else
        output.detail().shade_mode = data::ShadeMode::Auto;
    output.detail().cusp_angle_deg = options.cusp_angle_deg;

    if (!options.write_point_n || output.points().empty())
        return output;

    std::vector<data::PcgVec3> normals(output.points().size(), data::PcgVec3{});
    for (size_t fi = 0; fi < output.faces().size(); ++fi) {
        const data::PcgVec3 n = face_normal(output, fi);
        const double area = std::max(face_area(output, fi), kEps);
        for (int pi : output.faces()[fi]) {
            if (pi < 0 || static_cast<size_t>(pi) >= normals.size())
                continue;
            normals[static_cast<size_t>(pi)] =
                add(normals[static_cast<size_t>(pi)], mul(n, area));
        }
    }

    auto& n_attr =
        output.attributes().create_float(data::AttributeOwner::Point, "N", 3, {},
                                         data::AttributeTransformRole::Normal);
    n_attr.resize(output.points().size());
    auto& values = n_attr.float_values_mut();
    for (size_t i = 0; i < normals.size(); ++i) {
        const data::PcgVec3 n = normalize(normals[i]);
        values[i * 3 + 0] = n.x;
        values[i * 3 + 1] = n.y;
        values[i * 3 + 2] = n.z;
    }
    return output;
}

data::PcgGeometry smooth_mesh_geometry(const data::PcgGeometry& input,
                                       const SmoothMeshOptions& options)
{
    data::PcgGeometry output = input;
    if (output.points().empty() || options.iterations <= 0)
        return output;

    std::unordered_map<int, std::unordered_set<int>> adjacency;
    std::unordered_set<int> boundary;
    std::unordered_map<uint64_t, int> edge_use;
    for (const auto& face : output.faces()) {
        for (size_t i = 0; i < face.size(); ++i) {
            const int a = face[i];
            const int b = face[(i + 1) % face.size()];
            adjacency[a].insert(b);
            adjacency[b].insert(a);
            const uint64_t key = edge_key(a, b);
            edge_use[key] += 1;
        }
    }
    for (const auto& entry : edge_use) {
        if (entry.second == 1) {
            const int a = static_cast<int>(entry.first >> 32);
            const int b = static_cast<int>(entry.first & 0xffffffffu);
            boundary.insert(a);
            boundary.insert(b);
        }
    }

    const double strength = std::clamp(options.strength, 0.0, 1.0);
    for (int iter = 0; iter < options.iterations; ++iter) {
        std::vector<data::PcgVec3> next = output.points();
        for (size_t i = 0; i < output.points().size(); ++i) {
            if (options.fix_boundary && boundary.count(static_cast<int>(i)) > 0)
                continue;
            const auto it = adjacency.find(static_cast<int>(i));
            if (it == adjacency.end() || it->second.empty())
                continue;
            data::PcgVec3 avg{};
            for (int nb : it->second)
                avg = add(avg, output.points()[static_cast<size_t>(nb)]);
            avg = mul(avg, 1.0 / static_cast<double>(it->second.size()));
            const auto& cur = output.points()[i];
            next[i] = add(mul(cur, 1.0 - strength), mul(avg, strength));
        }
        output.points_mut() = std::move(next);
    }
    return output;
}

data::PcgGeometry reverse_mesh_geometry(const data::PcgGeometry& input)
{
    data::PcgGeometry output = input;
    for (auto& face : output.faces_mut())
        std::reverse(face.begin(), face.end());
    return output;
}

data::PcgGeometry thicken_mesh_geometry(const data::PcgGeometry& input,
                                        const ThickenMeshOptions& options)
{
    ShellMeshOptions shell;
    shell.thickness = std::max(1e-6, options.depth);
    if (options.direction == "outward")
        shell.direction = "outward";
    else if (options.direction == "inward")
        shell.direction = "inward";
    else
        shell.direction = "centered";
    shell.close_boundaries = true;
    return shell_geometry(input, shell);
}

data::PcgGeometry poly_slice_geometry(const data::PcgGeometry& input,
                                      const PolySliceOptions& options)
{
    // Keep both half-spaces as separate face sets by clipping twice and merging.
    auto clip_keep = [&](bool keep_pos) {
        data::PcgGeometry out;
        std::vector<int> remap(input.points().size(), -1);
        auto side = [&](const data::PcgVec3& p) {
            const data::PcgVec3 n = normalize(
                {options.normal_x, options.normal_y, options.normal_z});
            const data::PcgVec3 o{options.origin_x, options.origin_y, options.origin_z};
            return dot(sub(p, o), n);
        };
        for (size_t fi = 0; fi < input.faces().size(); ++fi) {
            bool all = true;
            for (int pi : input.faces()[fi]) {
                const double s = side(input.points()[static_cast<size_t>(pi)]);
                if (keep_pos ? s < -kEps : s > kEps) {
                    all = false;
                    break;
                }
            }
            if (!all)
                continue;
            std::vector<int> face;
            for (int pi : input.faces()[fi]) {
                if (remap[static_cast<size_t>(pi)] < 0) {
                    remap[static_cast<size_t>(pi)] = static_cast<int>(out.points().size());
                    out.points_mut().push_back(input.points()[static_cast<size_t>(pi)]);
                }
                face.push_back(remap[static_cast<size_t>(pi)]);
            }
            if (face.size() >= 3)
                out.faces_mut().push_back(std::move(face));
        }
        out.detail() = input.detail();
        return out;
    };

    data::PcgGeometry a = clip_keep(true);
    data::PcgGeometry b = clip_keep(false);
    // Tag halves with slice_side detail/prim attr.
    auto& side_a = a.attributes().create_int(data::AttributeOwner::Primitive, "slice_side", 1);
    side_a.resize(a.faces().size());
    std::fill(side_a.int_values_mut().begin(), side_a.int_values_mut().end(), 1);
    auto& side_b = b.attributes().create_int(data::AttributeOwner::Primitive, "slice_side", 1);
    side_b.resize(b.faces().size());
    std::fill(side_b.int_values_mut().begin(), side_b.int_values_mut().end(), 0);

    return data::merge_geometries(a, b);
}

data::PcgGeometry poly_wire_geometry(const data::PcgSplineData& splines,
                                     const PolyWireOptions& options)
{
    SweepAlongSplineOptions sweep;
    sweep.surface_shape = "circle";
    sweep.radius = std::max(1e-6, options.radius);
    sweep.columns = std::max(3, options.columns);
    sweep.sample_spacing = 0.05;
    sweep.cap_start = options.cap_start;
    sweep.cap_end = options.cap_end;
    return sweep_along_spline_geometry(splines, nullptr, sweep);
}

data::PcgGeometry connectivity_geometry(const data::PcgGeometry& input,
                                        const ConnectivityOptions& options)
{
    data::PcgGeometry output = input;
    const size_t face_count = output.faces().size();
    std::vector<int64_t> class_ids(face_count, -1);
    if (face_count == 0)
        return output;

    std::unordered_map<uint64_t, std::vector<int>> edge_to_faces;
    std::unordered_map<int, std::vector<int>> point_to_faces;
    for (size_t fi = 0; fi < face_count; ++fi) {
        const auto& face = output.faces()[fi];
        for (size_t i = 0; i < face.size(); ++i) {
            const int a = face[i];
            const int b = face[(i + 1) % face.size()];
            edge_to_faces[edge_key(a, b)].push_back(static_cast<int>(fi));
            point_to_faces[a].push_back(static_cast<int>(fi));
        }
    }

    int next_class = 0;
    for (size_t seed = 0; seed < face_count; ++seed) {
        if (class_ids[seed] >= 0)
            continue;
        std::queue<int> q;
        q.push(static_cast<int>(seed));
        class_ids[seed] = next_class;
        while (!q.empty()) {
            const int fi = q.front();
            q.pop();
            std::vector<int> neighbors;
            if (options.connectivity == "point") {
                for (int pi : output.faces()[static_cast<size_t>(fi)]) {
                    for (int nj : point_to_faces[pi])
                        neighbors.push_back(nj);
                }
            } else {
                const auto& face = output.faces()[static_cast<size_t>(fi)];
                for (size_t i = 0; i < face.size(); ++i) {
                    const uint64_t key = edge_key(face[i], face[(i + 1) % face.size()]);
                    for (int nj : edge_to_faces[key])
                        neighbors.push_back(nj);
                }
            }
            for (int nj : neighbors) {
                if (nj < 0 || static_cast<size_t>(nj) >= class_ids.size())
                    continue;
                if (class_ids[static_cast<size_t>(nj)] >= 0)
                    continue;
                class_ids[static_cast<size_t>(nj)] = next_class;
                q.push(nj);
            }
        }
        ++next_class;
    }

    auto& attr = output.attributes().create_int(
        data::AttributeOwner::Primitive, options.attribute_name, 1);
    attr.resize(face_count);
    attr.int_values_mut() = class_ids;
    set_detail_int(output, "num_classes", next_class);
    return output;
}

data::PcgGeometry assemble_geometry(const data::PcgGeometry& input,
                                    const AssembleOptions& options)
{
    data::PcgGeometry output = input;
    const data::AttributeArray* existing =
        output.attributes().find(data::AttributeOwner::Primitive, options.piece_attribute);
    if (existing && existing->schema().type == data::AttributeType::Int &&
        existing->size() == output.faces().size())
        return output;

    const data::AttributeArray* class_attr =
        output.attributes().find(data::AttributeOwner::Primitive, options.class_attribute);
    if (class_attr && class_attr->schema().type == data::AttributeType::Int &&
        class_attr->size() == output.faces().size()) {
        auto& piece = output.attributes().create_int(
            data::AttributeOwner::Primitive, options.piece_attribute, 1);
        piece.resize(output.faces().size());
        piece.int_values_mut() = class_attr->int_values();
        return output;
    }

    if (!options.create_if_missing)
        return output;

    ConnectivityOptions conn;
    conn.attribute_name = options.class_attribute;
    output = connectivity_geometry(output, conn);
    class_attr =
        output.attributes().find(data::AttributeOwner::Primitive, options.class_attribute);
    auto& piece = output.attributes().create_int(
        data::AttributeOwner::Primitive, options.piece_attribute, 1);
    piece.resize(output.faces().size());
    if (class_attr)
        piece.int_values_mut() = class_attr->int_values();
    return output;
}

data::PcgGeometry sort_geometry(const data::PcgGeometry& input,
                                const SortGeometryOptions& options)
{
    auto apply_domain = [](const data::PcgGeometry& geometry,
                           const SortDomainOptions& domain,
                           bool primitives) {
        const size_t count =
            primitives ? geometry.faces().size() : geometry.points().size();
        if (count == 0)
            return geometry;

        const std::string method = resolve_sort_method(domain);
        const bool needs_work =
            method != "nochange" || domain.reverse || domain.sort_indices;
        if (!needs_work)
            return geometry;

        const std::vector<size_t> order = compute_sort_order(geometry, domain, primitives);
        if (domain.sort_indices) {
            data::PcgGeometry output = geometry;
            write_sort_indices(output, primitives, order, domain.ordering_attribute);
            return output;
        }

        bool identity = true;
        for (size_t i = 0; i < order.size(); ++i) {
            if (order[i] != i) {
                identity = false;
                break;
            }
        }
        if (identity)
            return geometry;

        return primitives ? apply_primitive_permutation(geometry, order)
                          : apply_point_permutation(geometry, order);
    };

    data::PcgGeometry output = apply_domain(input, options.points, false);
    output = apply_domain(output, options.primitives, true);
    (void)options.optimize_vertex_order;
    return output;
}

std::vector<double> spline_vertex_u(const data::PcgSpline& spline, bool arc_length_u)
{
    const size_t n = spline.points.size();
    std::vector<double> vertex_u(n, 0.0);
    if (n < 2)
        return vertex_u;

    if (arc_length_u) {
        double total = 0.0;
        for (size_t i = 1; i < n; ++i) {
            const auto& a = spline.points[i];
            const auto& b = spline.points[i - 1];
            const double dx = a.x - b.x;
            const double dy = a.y - b.y;
            const double dz = a.z - b.z;
            total += std::sqrt(dx * dx + dy * dy + dz * dz);
            vertex_u[i] = total;
        }
        if (total > kEps) {
            for (double& u : vertex_u)
                u /= total;
        } else {
            const double denom = static_cast<double>(n - 1);
            for (size_t i = 0; i < n; ++i)
                vertex_u[i] = static_cast<double>(i) / denom;
        }
    } else {
        const double denom = static_cast<double>(n - 1);
        for (size_t i = 0; i < n; ++i)
            vertex_u[i] = static_cast<double>(i) / denom;
    }
    return vertex_u;
}

data::PcgSplinePoint evaluate_spline_at_u(const data::PcgSpline& spline,
                                          const std::vector<double>& vertex_u,
                                          double u)
{
    u = std::clamp(u, 0.0, 1.0);
    if (spline.points.empty())
        return {};
    if (spline.points.size() == 1 || u <= vertex_u.front() + kEps)
        return spline.points.front();
    if (u >= vertex_u.back() - kEps)
        return spline.points.back();

    for (size_t i = 0; i + 1 < vertex_u.size(); ++i) {
        if (u < vertex_u[i + 1] - kEps || i + 2 == vertex_u.size()) {
            const double span = vertex_u[i + 1] - vertex_u[i];
            const double t = span <= kEps ? 0.0 : (u - vertex_u[i]) / span;
            const auto& a = spline.points[i];
            const auto& b = spline.points[i + 1];
            return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
        }
    }
    return spline.points.back();
}

bool same_spline_point(const data::PcgSplinePoint& a, const data::PcgSplinePoint& b)
{
    return std::abs(a.x - b.x) <= kEps && std::abs(a.y - b.y) <= kEps &&
           std::abs(a.z - b.z) <= kEps;
}

std::vector<data::PcgSplinePoint> build_carved_segment(const data::PcgSpline& spline,
                                                       const std::vector<double>& vertex_u,
                                                       double u_from,
                                                       double u_to)
{
    if (u_to < u_from)
        std::swap(u_from, u_to);
    if (u_to - u_from <= kEps)
        return {};

    std::vector<data::PcgSplinePoint> segment;
    segment.push_back(evaluate_spline_at_u(spline, vertex_u, u_from));
    for (size_t i = 0; i < spline.points.size(); ++i) {
        const double u = vertex_u[i];
        if (u > u_from + kEps && u < u_to - kEps)
            segment.push_back(spline.points[i]);
    }
    const auto end = evaluate_spline_at_u(spline, vertex_u, u_to);
    if (!same_spline_point(end, segment.back()))
        segment.push_back(end);
    return segment.size() >= 2 ? segment : std::vector<data::PcgSplinePoint>{};
}

void append_carved_spline(data::PcgSplineData& output,
                          const data::PcgSpline& source,
                          std::vector<data::PcgSplinePoint> points)
{
    if (points.size() < 2)
        return;
    data::PcgSpline carved;
    carved.closed = false;
    carved.attributes = source.attributes;
    carved.points = std::move(points);
    output.add_spline(std::move(carved));
}

data::PcgSplineData carve_spline_data(const data::PcgSplineData& input,
                                      const CarveSplineOptions& options)
{
    data::PcgSplineData output;
    const double u_min_raw = options.use_first_u ? std::clamp(options.u_start, 0.0, 1.0) : 0.0;
    const double u_max_raw =
        options.use_second_u ? std::clamp(options.u_end, 0.0, 1.0) : 1.0;
    const double u0 = std::min(u_min_raw, u_max_raw);
    const double u1 = std::max(u_min_raw, u_max_raw);
    const bool keep_inside = options.keep_inside;
    const bool keep_outside = options.keep_outside;
    if (!keep_inside && !keep_outside)
        return output;

    const bool breakpoints = options.location != "divisions";

    for (const auto& spline : input.splines()) {
        if (spline.points.size() < 2)
            continue;

        const std::vector<double> vertex_u =
            spline_vertex_u(spline, options.arc_length_u);
        const size_t n = spline.points.size();

        std::vector<double> cuts;
        if (breakpoints) {
            if (options.use_first_u)
                cuts.push_back(u_min_raw);
            if (options.use_second_u)
                cuts.push_back(u_max_raw);
            if (options.cut_at_all_internal_u_breakpoints) {
                for (size_t i = 1; i + 1 < n; ++i) {
                    const double u = vertex_u[i];
                    if (u > u0 + kEps && u < u1 - kEps)
                        cuts.push_back(u);
                }
            }
        } else {
            if (options.use_first_u)
                cuts.push_back(u_min_raw);
            if (options.use_second_u)
                cuts.push_back(u_max_raw);
            const int divisions = std::max(1, options.u_divisions);
            for (int d = 1; d < divisions; ++d)
                cuts.push_back(u0 + (u1 - u0) * static_cast<double>(d) /
                                          static_cast<double>(divisions));
        }

        cuts.push_back(0.0);
        cuts.push_back(1.0);
        std::sort(cuts.begin(), cuts.end());
        cuts.erase(std::unique(cuts.begin(), cuts.end(),
                               [](double a, double b) { return std::abs(a - b) <= kEps; }),
                   cuts.end());

        auto segment_kept = [&](double a, double b) {
            const double lo = std::min(a, b);
            const double hi = std::max(a, b);
            const bool fully_inside = lo >= u0 - kEps && hi <= u1 + kEps;
            const bool fully_outside = hi <= u0 + kEps || lo >= u1 - kEps;
            return (keep_inside && fully_inside) || (keep_outside && fully_outside);
        };

        for (size_t i = 0; i + 1 < cuts.size(); ++i) {
            const double seg_u0 = cuts[i];
            const double seg_u1 = cuts[i + 1];
            if (!segment_kept(seg_u0, seg_u1))
                continue;
            append_carved_spline(output, spline,
                                 build_carved_segment(spline, vertex_u, seg_u0, seg_u1));
        }
    }
    return output;
}

data::PcgSplineData find_shortest_path_on_mesh(const data::PcgGeometry& input,
                                               const FindShortestPathOptions& options)
{
    data::PcgSplineData output;
    if (input.points().size() < 2)
        return output;

    auto to_spline_point = [](const data::PcgVec3& p) {
        return data::PcgSplinePoint{p.x, p.y, p.z};
    };
    auto resolve_endpoint = [&](int fallback, const std::string& group) {
        if (!group.empty()) {
            const auto& members =
                input.groups().members(geometry::GroupDomain::Point, group);
            if (!members.empty())
                return static_cast<int>(*members.begin());
        }
        return std::clamp(fallback, 0, static_cast<int>(input.points().size()) - 1);
    };
    const int start = resolve_endpoint(options.start_point, options.start_group);
    const int goal = resolve_endpoint(options.end_point, options.end_group);
    if (start == goal) {
        data::PcgSpline spline;
        spline.points = {to_spline_point(input.points()[static_cast<size_t>(start)]),
                         to_spline_point(input.points()[static_cast<size_t>(goal)])};
        output.add_spline(std::move(spline));
        return output;
    }

    std::unordered_map<int, std::vector<std::pair<int, double>>> graph;
    for (const auto& face : input.faces()) {
        for (size_t i = 0; i < face.size(); ++i) {
            const int a = face[i];
            const int b = face[(i + 1) % face.size()];
            const double w =
                length(sub(input.points()[static_cast<size_t>(a)],
                           input.points()[static_cast<size_t>(b)]));
            graph[a].push_back({b, w});
            graph[b].push_back({a, w});
        }
    }

    const int n = static_cast<int>(input.points().size());
    std::vector<double> dist(static_cast<size_t>(n), std::numeric_limits<double>::infinity());
    std::vector<int> prev(static_cast<size_t>(n), -1);
    using Node = std::pair<double, int>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> pq;
    dist[static_cast<size_t>(start)] = 0.0;
    pq.push({0.0, start});
    while (!pq.empty()) {
        const auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[static_cast<size_t>(u)])
            continue;
        if (u == goal)
            break;
        for (const auto& [v, w] : graph[u]) {
            const double nd = d + w;
            if (nd < dist[static_cast<size_t>(v)]) {
                dist[static_cast<size_t>(v)] = nd;
                prev[static_cast<size_t>(v)] = u;
                pq.push({nd, v});
            }
        }
    }

    if (prev[static_cast<size_t>(goal)] < 0 && start != goal)
        return output;

    std::vector<int> path;
    for (int cur = goal; cur >= 0; cur = prev[static_cast<size_t>(cur)]) {
        path.push_back(cur);
        if (cur == start)
            break;
    }
    std::reverse(path.begin(), path.end());
    if (path.empty() || path.front() != start)
        return output;

    data::PcgSpline spline;
    for (int pi : path)
        spline.points.push_back(to_spline_point(input.points()[static_cast<size_t>(pi)]));
    output.add_spline(std::move(spline));
    return output;
}

data::PcgGeometry tree_simple_leaf_geometry(const data::PcgPointData& points,
                                            const TreeSimpleLeafOptions& options)
{
    data::PcgGeometry output;
    uint32_t rng = static_cast<uint32_t>(options.seed) * 747796405u + 2891336453u;
    auto next01 = [&]() {
        rng = rng * 1664525u + 1013904223u;
        return (rng & 0xffffffu) / static_cast<double>(0xffffffu);
    };

    for (size_t i = 0; i < points.points().size(); ++i) {
        const auto& p = points.points()[i];
        const double t = options.scale_min +
                         (options.scale_max - options.scale_min) * next01();
        const double w = options.leaf_width * t * 0.5;
        const double h = options.leaf_height * t;
        const double th = options.leaf_thickness * t * 0.5;
        const double yaw = next01() * 2.0 * kPi;
        const double cy = std::cos(yaw);
        const double sy = std::sin(yaw);

        auto xform = [&](double lx, double ly, double lz) {
            const double x = lx * cy - lz * sy;
            const double z = lx * sy + lz * cy;
            return data::PcgVec3{p.x + x, p.y + ly, p.z + z};
        };

        const int base = static_cast<int>(output.points().size());
        output.points_mut().push_back(xform(-w, 0.0, -th));
        output.points_mut().push_back(xform(w, 0.0, -th));
        output.points_mut().push_back(xform(w, h, -th));
        output.points_mut().push_back(xform(-w, h, -th));
        output.points_mut().push_back(xform(-w, 0.0, th));
        output.points_mut().push_back(xform(w, 0.0, th));
        output.points_mut().push_back(xform(w, h, th));
        output.points_mut().push_back(xform(-w, h, th));
        output.faces_mut().push_back({base + 0, base + 1, base + 2, base + 3});
        output.faces_mut().push_back({base + 4, base + 7, base + 6, base + 5});
        output.faces_mut().push_back({base + 0, base + 4, base + 5, base + 1});
        output.faces_mut().push_back({base + 1, base + 5, base + 6, base + 2});
        output.faces_mut().push_back({base + 2, base + 6, base + 7, base + 3});
        output.faces_mut().push_back({base + 3, base + 7, base + 4, base + 0});
    }
    return output;
}

geometry::PolylineResampleOptions polyline_opts_for_face(const data::PcgGeometry& input,
                                                         size_t face_index,
                                                         const ResampleOptions& options)
{
    geometry::PolylineResampleOptions polyline_opts;
    polyline_opts.use_max_segments = options.use_max_segments;
    polyline_opts.max_segments = options.max_segments;
    polyline_opts.use_max_segment_length = options.use_max_segment_length;
    polyline_opts.max_segment_length = options.max_segment_length;
    polyline_opts.measure = options.measure;
    polyline_opts.even_last_segment_same_length = options.even_last_segment_same_length;
    polyline_opts.maintain_last_vertex = options.maintain_last_vertex;

    if (!options.allow_attribute_override)
        return polyline_opts;

    auto read_prim_scalar = [&](const std::string& name, double& out) -> bool {
        const data::AttributeArray* attr =
            input.attributes().find(data::AttributeOwner::Primitive, name);
        if (!attr || face_index >= attr->size())
            return false;
        if (attr->schema().type == data::AttributeType::Float) {
            out = attr->float_values()[face_index];
            return true;
        }
        if (attr->schema().type == data::AttributeType::Int) {
            out = static_cast<double>(attr->int_values()[face_index]);
            return true;
        }
        return false;
    };

    double segment_length = 0.0;
    if (read_prim_scalar("segment_length", segment_length) && segment_length > 0.0) {
        polyline_opts.use_max_segment_length = true;
        polyline_opts.use_max_segments = false;
        polyline_opts.max_segment_length = segment_length;
    }
    double num_segments = 0.0;
    if (read_prim_scalar("num_segments", num_segments) && num_segments > 0.0) {
        polyline_opts.use_max_segments = true;
        polyline_opts.use_max_segment_length = false;
        polyline_opts.max_segments = static_cast<int>(num_segments);
    }
    return polyline_opts;
}

int append_point(data::PcgGeometry& output, const data::PcgVec3& point)
{
    const int index = static_cast<int>(output.points().size());
    output.points_mut().push_back(point);
    return index;
}

void write_resample_point_attrs(data::PcgGeometry& output,
                                int point_index,
                                const geometry::PolylineResampleResult& resampled,
                                size_t sample_index,
                                int curve_number,
                                const ResampleOptions& options)
{
    if (point_index < 0 || static_cast<size_t>(point_index) >= output.points().size())
        return;

    auto write_scalar = [&](const std::string& name, double value) {
        if (name.empty())
            return;
        data::AttributeArray* attr = output.attributes().find(data::AttributeOwner::Point, name);
        if (!attr) {
            attr = &output.attributes().create_float(data::AttributeOwner::Point, name, 1);
            attr->resize(output.points().size());
        } else if (attr->size() < output.points().size()) {
            attr->resize(output.points().size());
        }
        attr->float_values_mut()[static_cast<size_t>(point_index)] = value;
    };

    if (options.write_curve_u_attr && sample_index < resampled.curve_u.size())
        write_scalar(options.curve_u_attribute, resampled.curve_u[sample_index]);
    if (options.write_distance_attr && sample_index < resampled.half_edge_lengths.size())
        write_scalar(options.distance_attribute, resampled.half_edge_lengths[sample_index]);
    if (options.write_curve_num_attr)
        write_scalar(options.curve_num_attribute, static_cast<double>(curve_number));

    if (options.write_tangent_attr && sample_index < resampled.tangents.size()) {
        data::AttributeArray* attr =
            output.attributes().find(data::AttributeOwner::Point, options.tangent_attribute);
        if (!attr) {
            attr = &output.attributes().create_float(data::AttributeOwner::Point,
                                                     options.tangent_attribute, 3);
            attr->resize(output.points().size());
        } else if (attr->size() < output.points().size()) {
            attr->resize(output.points().size());
        }
        auto& values = attr->float_values_mut();
        const auto& t = resampled.tangents[sample_index];
        values[static_cast<size_t>(point_index) * 3 + 0] = t.x;
        values[static_cast<size_t>(point_index) * 3 + 1] = t.y;
        values[static_cast<size_t>(point_index) * 3 + 2] = t.z;
    }
}

data::PcgGeometry resample_geometry(const data::PcgGeometry& input, const ResampleOptions& options)
{
    data::PcgGeometry output;
    if (input.faces().empty())
        return input;

    std::unordered_set<geometry::GroupId> selected;
    if (options.group.empty()) {
        for (size_t i = 0; i < input.faces().size(); ++i)
            selected.insert(static_cast<geometry::GroupId>(i));
    } else {
        selected = input.groups().eval(geometry::GroupDomain::Face, options.group);
    }

    std::unordered_map<int, int> point_remap;
    auto remap_point = [&](int source_index) {
        if (source_index < 0 || static_cast<size_t>(source_index) >= input.points().size())
            return -1;
        const auto found = point_remap.find(source_index);
        if (found != point_remap.end())
            return found->second;
        const int mapped = append_point(output, input.points()[static_cast<size_t>(source_index)]);
        point_remap.emplace(source_index, mapped);
        return mapped;
    };

    int curve_counter = 0;
    for (size_t fi = 0; fi < input.faces().size(); ++fi) {
        const auto& face = input.faces()[fi];
        const bool resample_face =
            selected.count(static_cast<geometry::GroupId>(fi)) > 0 && face.size() >= 2;

        if (!resample_face) {
            std::vector<int> remapped;
            remapped.reserve(face.size());
            for (int pi : face) {
                const int mapped = remap_point(pi);
                if (mapped >= 0)
                    remapped.push_back(mapped);
            }
            if (remapped.size() >= 2)
                output.faces_mut().push_back(std::move(remapped));
            continue;
        }

        std::vector<geometry::Vec3> polyline;
        polyline.reserve(face.size());
        for (int pi : face) {
            if (pi < 0 || static_cast<size_t>(pi) >= input.points().size())
                continue;
            const auto& p = input.points()[static_cast<size_t>(pi)];
            polyline.push_back({p.x, p.y, p.z});
        }
        if (polyline.size() < 2)
            continue;

        const bool closed =
            face.size() >= 3 && face.front() == face.back() &&
            geometry::length(geometry::sub(polyline.front(), polyline.back())) <= kEps;
        if (closed && polyline.size() >= 2)
            polyline.pop_back();

        const geometry::PolylineResampleOptions polyline_opts =
            polyline_opts_for_face(input, fi, options);
        const geometry::PolylineResampleResult resampled =
            geometry::resample_polyline_houdini(polyline, polyline_opts);

        if (resampled.points.empty())
            continue;

        if (options.create_only_points) {
            for (size_t si = 0; si < resampled.points.size(); ++si) {
                const auto& p = resampled.points[si];
                const int pi = append_point(output, {p.x, p.y, p.z});
                write_resample_point_attrs(output, pi, resampled, si, curve_counter, options);
            }
        } else {
            std::vector<int> remapped;
            remapped.reserve(resampled.points.size() + 1);
            for (size_t si = 0; si < resampled.points.size(); ++si) {
                const auto& p = resampled.points[si];
                const int pi = append_point(output, {p.x, p.y, p.z});
                write_resample_point_attrs(output, pi, resampled, si, curve_counter, options);
                remapped.push_back(pi);
            }
            if (closed && !remapped.empty())
                remapped.push_back(remapped.front());
            if (remapped.size() >= 2)
                output.faces_mut().push_back(std::move(remapped));
        }
        ++curve_counter;
    }

    output.groups() = input.groups();
    output.detail() = input.detail();
    return output;
}

} // namespace pcg::internal::elements
