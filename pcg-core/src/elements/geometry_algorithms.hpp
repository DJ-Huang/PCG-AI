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
    std::vector<std::string> from_face_groups;
    std::vector<std::string> from_edge_groups;
};

struct GroupCombineOptions {
    std::string output_group = "combined";
    std::string domain = "edge";
    std::string operation = "union";
    std::vector<std::string> source_groups;
};

struct FaceGroupByNormalOptions {
    std::string output_group = "material_faces";
    double direction_x = 0.0;
    double direction_y = 1.0;
    double direction_z = 0.0;
    double spread_angle_deg = 30.0;
};

data::PcgGeometry group_create(const data::PcgGeometry& input, const GroupCreateOptions& options);
data::PcgGeometry group_combine(const data::PcgGeometry& input, const GroupCombineOptions& options);
data::PcgGeometry face_group_by_normal(const data::PcgGeometry& input,
                                       const FaceGroupByNormalOptions& options);

} // namespace pcg::internal::elements
