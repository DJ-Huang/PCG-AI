#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_point_data.hpp"

#include <string>
#include <vector>

namespace pcg::internal::elements {

struct AttributeRandomizeOptions {
    int seed = 0;
    double translate_x = 0.0;
    double translate_y = 0.0;
    double translate_z = 0.0;
    double rotate_x_deg = 0.0;
    double rotate_y_deg = 0.0;
    double rotate_z_deg = 0.0;
    double scale_min = 1.0;
    double scale_max = 1.0;
    double color_min_r = 1.0;
    double color_min_g = 1.0;
    double color_min_b = 1.0;
    double color_max_r = 1.0;
    double color_max_g = 1.0;
    double color_max_b = 1.0;
    std::vector<std::string> material_names;
};

data::PcgPointData randomize_point_attributes(const data::PcgPointData& input,
                                              int graph_seed,
                                              const AttributeRandomizeOptions& options);

data::PcgGeometry copy_geometry_to_points(const data::PcgGeometry& prototype,
                                          const data::PcgPointData& points);

} // namespace pcg::internal::elements
