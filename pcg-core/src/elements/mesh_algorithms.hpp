#pragma once

#include "data/pcg_mesh_data.hpp"

namespace pcg::internal::elements {

enum class BevelMethod {
    Edge,
    VertexPush,
};

enum class BevelOffsetType {
    Offset,
    Width,
};

data::PcgMeshData create_box_mesh(double width, double height, double depth);
data::PcgMeshData subdivide_mesh(const data::PcgMeshData& mesh, int levels);
data::PcgMeshData bevel_mesh(const data::PcgMeshData& mesh, double amount, int segments,
                             BevelMethod method = BevelMethod::Edge,
                             BevelOffsetType offset_type = BevelOffsetType::Offset,
                             bool clamp_overlap = true, double angle_limit_deg = 30.0);

} // namespace pcg::internal::elements
