#pragma once

#include "data/pcg_geometry.hpp"

#include <string>
#include <vector>

namespace pcg::internal::elements {

struct GroupCreateOptions {
    std::string output_group = "bevel_edges";
    std::string domain = "edge";
    std::string mode = "angle";
    double min_edge_angle_deg = 30.0;
    bool include_unshared = false;
    std::string from_face_group;
    std::string from_edge_group;
};

struct GroupCombineOptions {
    std::string output_group = "combined";
    std::string domain = "edge";
    std::string operation = "union";
    std::vector<std::string> source_groups;
};

data::PcgGeometry group_create(const data::PcgGeometry& input, const GroupCreateOptions& options);
data::PcgGeometry group_combine(const data::PcgGeometry& input, const GroupCombineOptions& options);

} // namespace pcg::internal::elements
