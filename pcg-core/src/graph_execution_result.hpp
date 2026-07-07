#pragma once

#include "data/pcg_mesh_data.hpp"

#include <nlohmann/json.hpp>

namespace pcg::internal {

enum class GraphResultKind {
    Json,
    Mesh,
};

struct GraphExecutionResult {
    GraphResultKind kind = GraphResultKind::Json;
    nlohmann::json json;
    data::PcgMeshData mesh;
    data::PcgMeshData spawn_mesh;
};

} // namespace pcg::internal
