#pragma once

// Named point / edge / face / vertex groups.

#include <string>
#include <cstdint>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pcg::internal::geometry {

using GroupId = int64_t;

enum class GroupDomain {
    Point,
    Edge,
    Face,
    Vertex,
};

GroupId edge_group_id(int point_a, int point_b);
std::array<int, 2> edge_group_points(GroupId id);

/** Named membership sets per domain. Groups live on geometry, not in JSON metadata. */
class GroupTable {
public:
    bool contains(GroupDomain domain, const std::string& name, GroupId id) const;
    bool has_group(GroupDomain domain, const std::string& name) const;
    void add(GroupDomain domain, const std::string& name, GroupId id);
    void remove(GroupDomain domain, const std::string& name, GroupId id);
    void clear_group(GroupDomain domain, const std::string& name);
    /// Ensure a named group exists even when empty (Houdini create-even-if-empty).
    void ensure_group(GroupDomain domain, const std::string& name);

    const std::unordered_set<GroupId>& members(GroupDomain domain, const std::string& name) const;
    std::vector<std::string> group_names(GroupDomain domain) const;

    void union_into(GroupDomain domain, const std::string& dst, const std::string& src);
    void intersect_into(GroupDomain domain,
                        const std::string& dst,
                        const std::string& a,
                        const std::string& b);
    void subtract_into(GroupDomain domain, const std::string& dst, const std::string& src);

    /// Houdini subset subset: "a", "a - b", "a & b" (whitespace trimmed).
    std::unordered_set<GroupId> eval(GroupDomain domain, const std::string& expr) const;

    /// Merge groups from another table; optional prefix avoids name clashes on MergeGeometry.
    void merge_from(const GroupTable& other, const std::string& prefix = "");

    bool operator==(const GroupTable& other) const;

private:
    using GroupMap = std::unordered_map<std::string, std::unordered_set<GroupId>>;

    GroupMap& map_for(GroupDomain domain);
    const GroupMap& map_for(GroupDomain domain) const;

    static const std::unordered_set<GroupId> kEmpty;

    GroupMap point_groups_;
    GroupMap edge_groups_;
    GroupMap face_groups_;
    GroupMap vertex_groups_;
};

} // namespace pcg::internal::geometry
