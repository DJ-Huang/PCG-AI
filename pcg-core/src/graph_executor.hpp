#pragma once

#include "internal/graph_types.hpp"
#include "pcg_api.h"

namespace pcg::internal {

PcgResultCode execute_graph(const Graph& graph,
                            int seed,
                            nlohmann::json& out_result,
                            char* err_buf,
                            int err_buf_size);

} // namespace pcg::internal
