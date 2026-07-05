#pragma once

#include "internal/graph_types.hpp"
#include "pcg_api.h"

namespace pcg::internal::blocks {

PcgResultCode execute_spawn_points(const GraphNode& node,
                                   int graph_seed,
                                   nlohmann::json& out,
                                   char* err_buf,
                                   int err_buf_size);

PcgResultCode execute_place_in_scene(const GraphNode& node,
                                     const nlohmann::json& points_payload,
                                     nlohmann::json& out,
                                     char* err_buf,
                                     int err_buf_size);

} // namespace pcg::internal::blocks
