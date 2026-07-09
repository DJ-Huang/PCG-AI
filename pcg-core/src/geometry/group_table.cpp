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

const std::unordered_set<int> GroupTable::kEmpty{};

GroupTable::GroupMap& GroupTable::map_for(GroupDomain domain)
{
    switch (domain) {
    case GroupDomain::Point:
        return point_groups_;
    case GroupDomain::Edge:
        return edge_groups_;
    case GroupDomain::Face:
        return face_groups_;
    }
    return edge_groups_;
}

const GroupTable::GroupMap& GroupTable::map_for(GroupDomain domain) const
{
    return const_cast<GroupTable*>(this)->map_for(domain);
}

bool GroupTable::contains(GroupDomain domain, const std::string& name, int id) const
{
    const auto& groups = map_for(domain);
    const auto it = groups.find(name);
    if (it == groups.end())
        return false;
    return it->second.count(id) > 0;
}

void GroupTable::add(GroupDomain domain, const std::string& name, int id)
{
    map_for(domain)[name].insert(id);
}

void GroupTable::remove(GroupDomain domain, const std::string& name, int id)
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

const std::unordered_set<int>& GroupTable::members(GroupDomain domain,
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
    std::unordered_set<int> result;
    for (int id : a_members) {
        if (b_members.count(id) > 0)
            result.insert(id);
    }
    map_for(domain)[dst] = std::move(result);
}

void GroupTable::subtract_into(GroupDomain domain, const std::string& dst, const std::string& src)
{
    auto& dst_members = map_for(domain)[dst];
    const auto& src_members = members(domain, src);
    for (int id : src_members)
        dst_members.erase(id);
}

std::unordered_set<int> GroupTable::eval(GroupDomain domain, const std::string& expr) const
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
        std::unordered_set<int> result;
        for (int id : left_set) {
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
        for (int id : sub)
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
}

bool GroupTable::operator==(const GroupTable& other) const
{
    return point_groups_ == other.point_groups_ && edge_groups_ == other.edge_groups_ &&
           face_groups_ == other.face_groups_;
}

} // namespace pcg::internal::geometry
