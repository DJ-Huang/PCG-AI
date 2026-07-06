#pragma once

#include "graph_execution_result.hpp"
#include "internal/graph_types.hpp"
#include "pcg_api.h"
#include "texture_runtime.hpp"

namespace pcg::internal {

PcgResultCode execute_graph(const Graph& graph,
                            int seed,
                            GraphExecutionResult& out_result,
                            char* err_buf,
                            int err_buf_size,
                            const TextureRuntime* textures = nullptr);

} // namespace pcg::internal
