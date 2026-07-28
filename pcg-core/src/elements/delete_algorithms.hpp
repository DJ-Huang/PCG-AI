#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"

#include <nlohmann/json.hpp>
#include <string>

namespace pcg::internal::elements {

struct DeleteOptions {
    std::string group;
    std::string entity = "points";
    bool delete_non_selected = false;
    std::string geometry_type = "all";

    bool number_enable = false;
    std::string number_mode = "pattern";
    std::string number_pattern;
    int number_range_start = 0;
    int number_range_end = 0;
    int number_select_of = 1;
    int number_select_offset = 0;
    std::string number_expression;

    bool bounding_enable = false;
    std::string bounding_type = "box";
    double bounding_center_x = 0.0;
    double bounding_center_y = 0.0;
    double bounding_center_z = 0.0;
    double bounding_size_x = 1.0;
    double bounding_size_y = 1.0;
    double bounding_size_z = 1.0;
    double bounding_radius = 1.0;

    bool normal_enable = false;
    double normal_dir_x = 0.0;
    double normal_dir_y = 1.0;
    double normal_dir_z = 0.0;
    double normal_spread = 180.0;

    bool degenerate_duplicate_points = false;
    bool degenerate_zero_area = false;
    bool degenerate_open_face_perimeter = false;
    double degenerate_tolerance = 0.0001;

    bool random_enable = false;
    int random_seed = 0;
    std::string random_seed_attribute;
    double random_percent = 100.0;

    bool keep_points = false;
    bool delete_unused_groups = false;
};

DeleteOptions parse_delete_options(const nlohmann::json& data);

/// Remove group names that have no remaining members (Houdini Delete Unused Groups).
void remove_empty_groups(data::PcgGeometry& geometry);

data::PcgGeometry delete_geometry(const data::PcgGeometry& source,
                                  const DeleteOptions& options,
                                  int graph_seed,
                                  std::string& error);

data::PcgSplineData delete_splines(const data::PcgSplineData& source,
                                   const DeleteOptions& options,
                                   int graph_seed,
                                   std::string& error);

data::PcgPointData delete_points(const data::PcgPointData& source,
                                 const DeleteOptions& options,
                                 int graph_seed,
                                 std::string& error);

} // namespace pcg::internal::elements
