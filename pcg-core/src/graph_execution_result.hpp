#pragma once

#include "data/pcg_mesh_data.hpp"
#include "data/pcg_point_data.hpp"

#include <nlohmann/json.hpp>

#include <memory>

namespace pcg::internal {

enum class GraphResultKind {
    Json,
    Mesh,
    Points,
};

struct GraphExecutionResult {
    GraphResultKind kind = GraphResultKind::Json;
    nlohmann::json json;
    data::PcgMeshData mesh;
    std::shared_ptr<const data::PcgPointData> points;
    nlohmann::json point_sidecar;
    data::PcgMeshData spawn_mesh;
};

} // namespace pcg::internal
