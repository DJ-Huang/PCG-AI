#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_point_data.hpp"

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
};

data::PcgPointData randomize_point_attributes(const data::PcgPointData& input,
                                              int graph_seed,
                                              const AttributeRandomizeOptions& options);

data::PcgGeometry copy_geometry_to_points(const data::PcgGeometry& prototype,
                                          const data::PcgPointData& points);

} // namespace pcg::internal::elements
