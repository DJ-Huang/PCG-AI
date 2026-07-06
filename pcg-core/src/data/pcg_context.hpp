#pragma once

#include "data/pcg_data_collection.hpp"
#include "internal/graph_types.hpp"

namespace pcg::internal {

class TextureRuntime;

/** Per-node execution context (UE PCGContext analogue). */
struct PcgContext {
    int graph_seed = 0;
    const Graph* graph = nullptr;
    const GraphNode* node = nullptr;
    const TextureRuntime* textures = nullptr;
    data::PcgDataCollection inputs;
    data::PcgDataCollection outputs;
    char* err_buf = nullptr;
    int err_buf_size = 0;
};

} // namespace pcg::internal
