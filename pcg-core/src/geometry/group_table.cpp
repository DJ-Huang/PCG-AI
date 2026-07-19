#include "geometry/group_table.hpp"

#include <algorithm>
#include <cctype>

namespace pcg::internal::geometry {
namespace {

std::string trim(const std::string& s)
{
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
        ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
        --end;
    return s.substr(start, end - start);
}

} // namespace

GroupId edge_group_id(int point_a, int point_b)
{
    const uint32_t a = static_cast<uint32_t>(std::min(point_a, point_b));
    const uint32_t b = static_cast<uint32_t>(std::max(point_a, point_b));
    return static_cast<GroupId>((static_cast<uint64_t>(a) << 32u) | b);
}

std::array<int, 2> edge_group_points(GroupId id)
{
    const uint64_t bits = static_cast<uint64_t>(id);
    return {static_cast<int>(bits >> 32u), static_cast<int>(bits & 0xffffffffu)};
}

const std::unordered_set<GroupId> GroupTable::kEmpty{};

GroupTable::GroupMap& GroupTable::map_for(GroupDomain domain)
{
    switch (domain) {
    case GroupDomain::Point:
        return point_groups_;
    case GroupDomain::Edge:
        return edge_groups_;
    case GroupDomain::Face:
        return face_groups_;
    case GroupDomain::Vertex:
        return vertex_groups_;
    }
    return edge_groups_;
}

const GroupTable::GroupMap& GroupTable::map_for(GroupDomain domain) const
{
    return const_cast<GroupTable*>(this)->map_for(domain);
}

bool GroupTable::contains(GroupDomain domain, const std::string& name, GroupId id) const
{
    const auto& groups = map_for(domain);
    const auto it = groups.find(name);
    if (it == groups.end())
        return false;
    return it->second.count(id) > 0;
}

void GroupTable::add(GroupDomain domain, const std::string& name, GroupId id)
{
    map_for(domain)[name].insert(id);
}

void GroupTable::remove(GroupDomain domain, const std::string& name, GroupId id)
{
    auto& groups = map_for(domain);
    const auto it = groups.find(name);
    if (it == groups.end())
        return;
    it->second.erase(id);
    if (it->second.empty())
        groups.erase(it);
}

void GroupTable::clear_group(GroupDomain domain, const std::string& name)
{
    map_for(domain).erase(name);
}

const std::unordered_set<GroupId>& GroupTable::members(GroupDomain domain,
                                                       const std::string& name) const
{
    const auto& groups = map_for(domain);
    const auto it = groups.find(name);
    if (it == groups.end())
        return kEmpty;
    return it->second;
}

std::vector<std::string> GroupTable::group_names(GroupDomain domain) const
{
    std::vector<std::string> names;
    for (const auto& entry : map_for(domain))
        names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    return names;
}

void GroupTable::union_into(GroupDomain domain, const std::string& dst, const std::string& src)
{
    const auto& src_members = members(domain, src);
    auto& dst_members = map_for(domain)[dst];
    dst_members.insert(src_members.begin(), src_members.end());
}

void GroupTable::intersect_into(GroupDomain domain,
                                const std::string& dst,
                                const std::string& a,
                                const std::string& b)
{
    const auto& a_members = members(domain, a);
    const auto& b_members = members(domain, b);
    std::unordered_set<GroupId> result;
    for (GroupId id : a_members) {
        if (b_members.count(id) > 0)
            result.insert(id);
    }
    map_for(domain)[dst] = std::move(result);
}

void GroupTable::subtract_into(GroupDomain domain, const std::string& dst, const std::string& src)
{
    auto& groups = map_for(domain);
    const auto dst_it = groups.find(dst);
    if (dst_it == groups.end())
        return;
    if (dst == src) {
        groups.erase(dst_it);
        return;
    }

    const auto src_it = groups.find(src);
    if (src_it == groups.end())
        return;

    auto& dst_members = dst_it->second;
    for (GroupId id : src_it->second)
        dst_members.erase(id);
    if (dst_members.empty())
        groups.erase(dst_it);
}

std::unordered_set<GroupId> GroupTable::eval(GroupDomain domain, const std::string& expr) const
{
    const std::string trimmed = trim(expr);
    if (trimmed.empty())
        return {};

    const auto amp = trimmed.find('&');
    if (amp != std::string::npos) {
        const std::string left = trim(trimmed.substr(0, amp));
        const std::string right = trim(trimmed.substr(amp + 1));
        const auto left_set = eval(domain, left);
        const auto right_set = eval(domain, right);
        std::unordered_set<GroupId> result;
        for (GroupId id : left_set) {
            if (right_set.count(id) > 0)
                result.insert(id);
        }
        return result;
    }

    const auto minus = trimmed.find('-');
    if (minus != std::string::npos) {
        const std::string left = trim(trimmed.substr(0, minus));
        const std::string right = trim(trimmed.substr(minus + 1));
        auto result = eval(domain, left);
        const auto sub = eval(domain, right);
        for (GroupId id : sub)
            result.erase(id);
        return result;
    }

    return members(domain, trimmed);
}

void GroupTable::merge_from(const GroupTable& other, const std::string& prefix)
{
    const auto merge_domain = [&](GroupDomain domain) {
        for (const auto& entry : other.map_for(domain)) {
            const std::string name = prefix + entry.first;
            auto& dst = map_for(domain)[name];
            dst.insert(entry.second.begin(), entry.second.end());
        }
    };
    merge_domain(GroupDomain::Point);
    merge_domain(GroupDomain::Edge);
    merge_domain(GroupDomain::Face);
    merge_domain(GroupDomain::Vertex);
}

bool GroupTable::operator==(const GroupTable& other) const
{
    return point_groups_ == other.point_groups_ && edge_groups_ == other.edge_groups_ &&
           face_groups_ == other.face_groups_ && vertex_groups_ == other.vertex_groups_;
}

} // namespace pcg::internal::geometry
