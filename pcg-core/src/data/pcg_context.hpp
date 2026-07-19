#pragma once

#include "data/pcg_data_collection.hpp"
#include "internal/graph_types.hpp"

namespace pcg::internal {

class TextureRuntime;
class MeshRuntime;
class SplineRuntime;
class HeightFieldRuntime;

/** Per-node execution context (UE PCGContext analogue). */
struct PcgContext {
    int graph_seed = 0;
    const Graph* graph = nullptr;
    const GraphNode* node = nullptr;
    const TextureRuntime* textures = nullptr;
    const MeshRuntime* meshes = nullptr;
    const SplineRuntime* splines = nullptr;
    const HeightFieldRuntime* heightfields = nullptr;
    data::PcgDataCollection inputs;
    data::PcgDataCollection outputs;
    char* err_buf = nullptr;
    int err_buf_size = 0;
    bool (*is_cancel_requested)() = nullptr;
};

} // namespace pcg::internal
