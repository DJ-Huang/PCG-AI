#pragma once

#include "internal/graph_types.hpp"
#include "pcg_api.h"

namespace pcg::internal {

PcgResultCode parse_graph(const char* json,
                          Graph& out_graph,
                          char* err_buf,
                          int err_buf_size);

PcgResultCode validate_graph_structure(const Graph& graph,
                                       char* err_buf,
                                       int err_buf_size);

} // namespace pcg::internal
