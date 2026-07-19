#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_heightfield.hpp"
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
    /// Pre-triangulation Sink geometry for optional geometry_binary export (best-effort).
    std::shared_ptr<const data::PcgGeometry> source_geometry;
    /// Nearest typed HeightField upstream of the Sink for host terrain adapters.
    std::shared_ptr<const data::PcgHeightField> source_heightfield;
};

} // namespace pcg::internal
