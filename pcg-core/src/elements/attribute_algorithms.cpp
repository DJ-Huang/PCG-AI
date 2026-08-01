#include "elements/attribute_algorithms.hpp"

#include "data/pcg_attribute_table.hpp"
#include "geometry/group_table.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <vector>

namespace pcg::internal::elements {
namespace {

constexpr double kEpsilon = 1.0e-12;

std::string trim(std::string s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.pop_back();
    return s;
}

std::vector<std::string> split_tokens(const std::string& pattern)
{
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
}

bool glob_match(const std::string& name, const std::string& pattern)
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

geometry::GroupDomain parse_group_domain(const std::string& type)
{
    if (type == "points" || type == "point")
        return geometry::GroupDomain::Point;
    if (type == "edges" || type == "edge")
        return geometry::GroupDomain::Edge;
    if (type == "vertices" || type == "vertex")
        return geometry::GroupDomain::Vertex;
    return geometry::GroupDomain::Face;
}

data::AttributeOwner owner_from_group_domain(geometry::GroupDomain domain)
{
    switch (domain) {
    case geometry::GroupDomain::Point:
        return data::AttributeOwner::Point;
    case geometry::GroupDomain::Vertex:
        return data::AttributeOwner::Vertex;
    case geometry::GroupDomain::Face:
    case geometry::GroupDomain::Edge:
        return data::AttributeOwner::Primitive;
    }
    return data::AttributeOwner::Primitive;
}

data::PcgVec3 face_centroid(const data::PcgGeometry& geometry, const std::vector<int>& face)
{
    data::PcgVec3 center{};
    if (face.empty())
        return center;
    for (int index : face) {
        if (index < 0 || static_cast<size_t>(index) >= geometry.points().size())
            continue;
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

std::vector<data::PcgVec3> owner_locations(const data::PcgGeometry& geometry,
                                           data::AttributeOwner owner)
{
    if (owner == data::AttributeOwner::Point)
        return geometry.points();
    std::vector<data::PcgVec3> result;
    if (owner == data::AttributeOwner::Vertex) {
        result.reserve(static_cast<size_t>(geometry.corner_count()));
        for (const auto& face : geometry.faces()) {
            for (int point : face) {
                if (point < 0 || static_cast<size_t>(point) >= geometry.points().size())
                    result.push_back({});
                else
                    result.push_back(geometry.points()[static_cast<size_t>(point)]);
            }
        }
    } else if (owner == data::AttributeOwner::Primitive) {
        result.reserve(geometry.faces().size());
        for (const auto& face : geometry.faces())
            result.push_back(face_centroid(geometry, face));
    }
    return result;
}

size_t owner_element_count(const data::PcgGeometry& geometry, data::AttributeOwner owner)
{
    switch (owner) {
    case data::AttributeOwner::Point:
        return geometry.points().size();
    case data::AttributeOwner::Primitive:
        return geometry.faces().size();
    case data::AttributeOwner::Vertex:
        return static_cast<size_t>(geometry.corner_count());
    case data::AttributeOwner::Detail:
        return 1;
    }
    return 0;
}

std::unordered_set<geometry::GroupId> resolve_group_members(const data::PcgGeometry& geometry,
                                                            const std::string& group_name,
                                                            geometry::GroupDomain domain)
{
    std::unordered_set<geometry::GroupId> members;
    const std::string name = trim(group_name);
    if (name.empty() || name == "*")
        return members;
    if (!geometry.groups().has_group(domain, name))
        return members;
    return geometry.groups().members(domain, name);
}

std::vector<char> selection_mask(const data::PcgGeometry& geometry,
                                 data::AttributeOwner owner,
                                 const std::string& group_name,
                                 const std::string& group_type)
{
    const size_t count = owner_element_count(geometry, owner);
    std::vector<char> selected(count, 1);
    const std::string name = trim(group_name);
    if (name.empty() || name == "*")
        return selected;

    const geometry::GroupDomain domain = parse_group_domain(group_type);
    const auto members = resolve_group_members(geometry, name, domain);
    if (members.empty() && !geometry.groups().has_group(domain, name)) {
        std::fill(selected.begin(), selected.end(), 0);
        return selected;
    }

    const data::AttributeOwner group_owner = owner_from_group_domain(domain);
    if (group_owner == owner) {
        for (size_t i = 0; i < count; ++i)
            selected[i] = members.count(static_cast<geometry::GroupId>(i)) ? 1 : 0;
        return selected;
    }

    if (domain == geometry::GroupDomain::Face &&
        (owner == data::AttributeOwner::Point || owner == data::AttributeOwner::Vertex)) {
        std::fill(selected.begin(), selected.end(), 0);
        if (owner == data::AttributeOwner::Point) {
            for (size_t fi = 0; fi < geometry.faces().size(); ++fi) {
                if (members.count(static_cast<geometry::GroupId>(fi)) == 0)
                    continue;
                for (int pt : geometry.faces()[fi]) {
                    if (pt >= 0 && static_cast<size_t>(pt) < count)
                        selected[static_cast<size_t>(pt)] = 1;
                }
            }
        } else {
            size_t corner = 0;
            for (size_t fi = 0; fi < geometry.faces().size(); ++fi) {
                const bool face_selected =
                    members.count(static_cast<geometry::GroupId>(fi)) != 0;
                for (size_t ci = 0; ci < geometry.faces()[fi].size(); ++ci, ++corner) {
                    if (corner < count)
                        selected[corner] = face_selected ? 1 : 0;
                }
            }
        }
        return selected;
    }

    if (domain == geometry::GroupDomain::Point && owner == data::AttributeOwner::Primitive) {
        for (size_t fi = 0; fi < geometry.faces().size(); ++fi) {
            bool hit = false;
            for (int pt : geometry.faces()[fi]) {
                if (pt >= 0 && members.count(static_cast<geometry::GroupId>(pt)) != 0) {
                    hit = true;
                    break;
                }
            }
            selected[fi] = hit ? 1 : 0;
        }
        return selected;
    }

    return selected;
}

std::vector<std::string> select_attribute_names(const data::AttributeTable& table,
                                                data::AttributeOwner owner,
                                                const std::string& pattern,
                                                bool allow_p)
{
    const auto all = table.names(owner);
    const std::string trimmed = trim(pattern);
    std::vector<std::string> selected;
    const auto tokens =
        (trimmed.empty() || trimmed == "*") ? std::vector<std::string>{"*"} : split_tokens(trimmed);
    for (const auto& name : all) {
        if (!allow_p && name == "P")
            continue;
        for (const auto& token : tokens) {
            if (glob_match(name, token)) {
                selected.push_back(name);
                break;
            }
        }
    }
    return selected;
}

data::AttributeArray& ensure_attribute(data::AttributeTable& table,
                                       const data::AttributeArray& source_attr,
                                       size_t element_count)
{
    const auto& schema = source_attr.schema();
    auto* existing = table.find(schema.owner, schema.name);
    if (existing && existing->schema_compatible(source_attr)) {
        existing->resize(element_count);
        return *existing;
    }
    if (existing)
        table.erase(schema.owner, schema.name);

    data::AttributeArray* created = nullptr;
    switch (schema.type) {
    case data::AttributeType::Int:
        created = &table.create_int(schema.owner, schema.name, schema.tuple_size,
                                    source_attr.default_int(), schema.transform_role);
        break;
    case data::AttributeType::Float:
        created = &table.create_float(schema.owner, schema.name, schema.tuple_size,
                                      source_attr.default_float(), schema.transform_role);
        break;
    case data::AttributeType::String:
        created = &table.create_string(schema.owner, schema.name, schema.tuple_size,
                                       source_attr.default_string());
        break;
    }
    created->resize(element_count);
    return *created;
}

/// Metaball kernel weight for normalized distance t in [0,1]. Outside → 0.
double kernel_weight(const std::string& kernel, double t)
{
    if (t <= 0.0)
        return 1.0;
    if (t >= 1.0)
        return 0.0;

    if (kernel == "uniform")
        return 1.0;
    if (kernel == "wyvill") {
        const double u = 1.0 - t * t;
        return u * u * u;
    }
    if (kernel == "hart") {
        const double t2 = t * t;
        const double t3 = t2 * t;
        const double t4 = t2 * t2;
        const double t5 = t4 * t;
        return 1.0 - 6.0 * t5 + 15.0 * t4 - 10.0 * t3;
    }
    if (kernel == "blinn") {
        const double u = 1.0 - t * t;
        return u * u;
    }
    if (kernel == "links") {
        const double u = 1.0 - t;
        return u * u * (1.0 + 2.0 * t);
    }
    if (kernel == "heron") {
        const double u = 1.0 - t * t;
        return u * u * (3.0 - 2.0 * u);
    }
    // elendt (default / Blend model)
    const double u = 1.0 - t;
    return u * u * u * u * (4.0 * t + 1.0);
}

double sample_kernel_weight(const AttributeTransferOptions& options, double distance)
{
    const double radius = std::max(0.0, options.kernel_radius);
    if (radius <= kEpsilon)
        return distance <= kEpsilon ? 1.0 : 0.0;
    if (options.kernel_function == "uniform")
        return distance <= radius + kEpsilon ? 1.0 : 0.0;
    return kernel_weight(options.kernel_function, distance / radius);
}

/// Source influence in the blend-width feather outside Distance Threshold.
double blend_source_factor(const AttributeTransferOptions& options, double nearest_dist)
{
    if (!options.enable_distance_threshold)
        return 1.0;

    const double threshold = std::max(0.0, options.distance_threshold);
    const double blend = std::max(0.0, options.blend_width);
    if (nearest_dist <= threshold + kEpsilon)
        return 1.0;
    if (blend <= kEpsilon || nearest_dist > threshold + blend + kEpsilon)
        return 0.0;

    if (options.kernel_function == "uniform")
        return std::clamp(options.uniform_bias, 0.0, 1.0);

    const double t = (nearest_dist - threshold) / blend;
    return kernel_weight(options.kernel_function, t);
}

struct WeightedSample {
    int index = -1;
    double distance = 0.0;
    double weight = 0.0;
};

void blend_element_values(data::AttributeArray& dest,
                          size_t dest_index,
                          const data::AttributeArray& source,
                          const std::vector<WeightedSample>& samples,
                          double source_factor)
{
    if (samples.empty() || dest_index >= dest.size() || source_factor <= kEpsilon)
        return;

    double weight_sum = 0.0;
    for (const auto& sample : samples)
        weight_sum += sample.weight;
    if (weight_sum <= kEpsilon) {
        // Degenerate weights → nearest sample.
        dest.set_element_from(source, dest_index, static_cast<size_t>(samples.front().index));
        if (source_factor < 1.0 - kEpsilon) {
            // Soft-blend toward original is handled below for float/int only; strings keep
            // nearest when factor is partial.
        }
        return;
    }

    const size_t width = static_cast<size_t>(dest.schema().tuple_size);
    switch (dest.schema().type) {
    case data::AttributeType::Float: {
        auto& values = dest.float_values_mut();
        const auto& src_values = source.float_values();
        for (size_t c = 0; c < width; ++c) {
            double blended = 0.0;
            for (const auto& sample : samples) {
                const size_t src_i =
                    static_cast<size_t>(sample.index) * width + c;
                if (src_i < src_values.size())
                    blended += sample.weight * src_values[src_i];
            }
            blended /= weight_sum;
            const size_t dest_i = dest_index * width + c;
            if (source_factor >= 1.0 - kEpsilon)
                values[dest_i] = blended;
            else
                values[dest_i] = values[dest_i] * (1.0 - source_factor) + blended * source_factor;
        }
        break;
    }
    case data::AttributeType::Int: {
        auto& values = dest.int_values_mut();
        const auto& src_values = source.int_values();
        for (size_t c = 0; c < width; ++c) {
            double blended = 0.0;
            for (const auto& sample : samples) {
                const size_t src_i =
                    static_cast<size_t>(sample.index) * width + c;
                if (src_i < src_values.size())
                    blended += sample.weight * static_cast<double>(src_values[src_i]);
            }
            blended /= weight_sum;
            const size_t dest_i = dest_index * width + c;
            const double final_value =
                source_factor >= 1.0 - kEpsilon
                    ? blended
                    : static_cast<double>(values[dest_i]) * (1.0 - source_factor) +
                          blended * source_factor;
            values[dest_i] = static_cast<int64_t>(std::llround(final_value));
        }
        break;
    }
    case data::AttributeType::String: {
        // Strings: pick highest-weight sample (nearest when equal).
        size_t best = 0;
        for (size_t i = 1; i < samples.size(); ++i) {
            if (samples[i].weight > samples[best].weight)
                best = i;
        }
        if (source_factor >= 0.5)
            dest.set_element_from(source, dest_index, static_cast<size_t>(samples[best].index));
        break;
    }
    }
}

void transfer_detail(data::PcgGeometry& output,
                     const data::PcgGeometry& source,
                     const AttributeTransferOptions& options)
{
    if (!options.transfer_detail)
        return;
    const auto names = select_attribute_names(source.attributes(), data::AttributeOwner::Detail,
                                              options.detail_attributes, options.allow_p_attribute);
    for (const auto& name : names) {
        const auto* src = source.attributes().find(data::AttributeOwner::Detail, name);
        if (!src || src->size() == 0)
            continue;
        auto& dest = ensure_attribute(output.attributes(), *src, 1);
        dest.set_element_from(*src, 0, 0);
    }
}

void transfer_owner(data::PcgGeometry& output,
                    const data::PcgGeometry& source,
                    data::AttributeOwner owner,
                    bool enabled,
                    const std::string& pattern,
                    const AttributeTransferOptions& options)
{
    if (!enabled)
        return;

    const auto names =
        select_attribute_names(source.attributes(), owner, pattern, options.allow_p_attribute);
    if (names.empty())
        return;

    const auto source_locations = owner_locations(source, owner);
    const auto dest_locations = owner_locations(output, owner);
    if (source_locations.empty() || dest_locations.empty())
        return;

    const auto source_mask =
        selection_mask(source, owner, options.source_group, options.source_group_type);
    const auto dest_mask =
        selection_mask(output, owner, options.destination_group, options.destination_group_type);

    const auto dist2 = [](const data::PcgVec3& a, const data::PcgVec3& b) {
        const double dx = a.x - b.x;
        const double dy = a.y - b.y;
        const double dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    };

    const int max_samples = std::max(1, options.max_sample_count);
    const size_t dest_count = owner_element_count(output, owner);

    std::vector<std::vector<WeightedSample>> per_dest(dest_locations.size());
    std::vector<double> source_factor(dest_locations.size(), 0.0);

    for (size_t di = 0; di < dest_locations.size(); ++di) {
        if (di >= dest_mask.size() || !dest_mask[di])
            continue;

        std::vector<WeightedSample> candidates;
        candidates.reserve(source_locations.size());
        for (size_t si = 0; si < source_locations.size(); ++si) {
            if (si >= source_mask.size() || !source_mask[si])
                continue;
            const double d = std::sqrt(dist2(dest_locations[di], source_locations[si]));
            candidates.push_back({static_cast<int>(si), d, 0.0});
        }
        if (candidates.empty())
            continue;

        std::partial_sort(
            candidates.begin(),
            candidates.begin() + std::min(static_cast<size_t>(max_samples), candidates.size()),
            candidates.end(),
            [](const WeightedSample& a, const WeightedSample& b) {
                return a.distance < b.distance;
            });
        candidates.resize(std::min(static_cast<size_t>(max_samples), candidates.size()));

        const double nearest = candidates.front().distance;
        const double factor = blend_source_factor(options, nearest);
        if (factor <= kEpsilon)
            continue;

        for (auto& sample : candidates)
            sample.weight = sample_kernel_weight(options, sample.distance);

        // Max sample count 1 or zero kernel radius → hard nearest neighbor.
        if (max_samples == 1 || options.kernel_radius <= kEpsilon) {
            candidates.resize(1);
            candidates.front().weight = 1.0;
        } else {
            double sum = 0.0;
            for (const auto& sample : candidates)
                sum += sample.weight;
            if (sum <= kEpsilon) {
                candidates.resize(1);
                candidates.front().weight = 1.0;
            }
        }

        per_dest[di] = std::move(candidates);
        source_factor[di] = factor;
    }

    for (const auto& name : names) {
        const auto* src = source.attributes().find(owner, name);
        if (!src)
            continue;
        auto& dest = ensure_attribute(output.attributes(), *src, dest_count);
        for (size_t di = 0; di < per_dest.size() && di < dest_count; ++di) {
            if (per_dest[di].empty())
                continue;
            blend_element_values(dest, di, *src, per_dest[di], source_factor[di]);
        }
    }
}

} // namespace

data::PcgGeometry attribute_transfer_geometry(const data::PcgGeometry& target,
                                              const data::PcgGeometry& source,
                                              const AttributeTransferOptions& options)
{
    data::PcgGeometry output = target;
    transfer_detail(output, source, options);
    transfer_owner(output, source, data::AttributeOwner::Primitive, options.transfer_primitives,
                   options.primitive_attributes, options);
    transfer_owner(output, source, data::AttributeOwner::Point, options.transfer_points,
                   options.point_attributes, options);
    transfer_owner(output, source, data::AttributeOwner::Vertex, options.transfer_vertices,
                   options.vertex_attributes, options);
    return output;
}

} // namespace pcg::internal::elements
