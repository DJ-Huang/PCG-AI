#pragma once

#include "data/pcg_geometry.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace pcg::internal::elements {

struct AddPointSpec {
    bool enabled = true;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

struct AddOptions {
    bool delete_primitives_keep_points = false;
    std::vector<AddPointSpec> points;
    std::string polygons_spec;
};

/// Parse Houdini Add-style polygon line tokens into ordered point indices.
/// Supports single indices, ranges (`0-3`), and stepped ranges (`0-15:2,3`).
std::vector<int> parse_add_polygon_line(const std::string& line, int point_count);

std::vector<AddPointSpec> parse_add_points(const nlohmann::json& data);

data::PcgGeometry add_geometry(const data::PcgGeometry& input, const AddOptions& options);

} // namespace pcg::internal::elements
