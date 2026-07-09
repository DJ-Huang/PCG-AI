#pragma once

// Named point / edge / face groups (Houdini-style subset; vertex domain is phase 2).

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pcg::internal::geometry {

enum class GroupDomain {
    Point,
    Edge,
    Face,
};

/** Named membership sets per domain. Groups live on geometry, not in JSON metadata. */
class GroupTable {
public:
    bool contains(GroupDomain domain, const std::string& name, int id) const;
    void add(GroupDomain domain, const std::string& name, int id);
    void remove(GroupDomain domain, const std::string& name, int id);
    void clear_group(GroupDomain domain, const std::string& name);

    const std::unordered_set<int>& members(GroupDomain domain, const std::string& name) const;
    std::vector<std::string> group_names(GroupDomain domain) const;

    void union_into(GroupDomain domain, const std::string& dst, const std::string& src);
    void intersect_into(GroupDomain domain,
                        const std::string& dst,
                        const std::string& a,
                        const std::string& b);
    void subtract_into(GroupDomain domain, const std::string& dst, const std::string& src);

    /// Houdini subset subset: "a", "a - b", "a & b" (whitespace trimmed).
    std::unordered_set<int> eval(GroupDomain domain, const std::string& expr) const;

    /// Merge groups from another table; optional prefix avoids name clashes on MergeGeometry.
    void merge_from(const GroupTable& other, const std::string& prefix = "");

    bool operator==(const GroupTable& other) const;

private:
    using GroupMap = std::unordered_map<std::string, std::unordered_set<int>>;

    GroupMap& map_for(GroupDomain domain);
    const GroupMap& map_for(GroupDomain domain) const;

    static const std::unordered_set<int> kEmpty;

    GroupMap point_groups_;
    GroupMap edge_groups_;
    GroupMap face_groups_;
};

} // namespace pcg::internal::geometry
