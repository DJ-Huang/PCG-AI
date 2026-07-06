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

enum class BevelMiter {
    Sharp,
    Patch,
    Arc,
};

enum class BevelVMeshMethod {
    Adj,
    Cutoff,
};

enum class NoiseDeformType {
    Perlin,
};

data::PcgMeshData create_box_mesh(double width, double height, double depth);
data::PcgMeshData subdivide_mesh(const data::PcgMeshData& mesh, int levels);
data::PcgMeshData noise_deform_mesh(const data::PcgMeshData& mesh, double intensity, double noise_scale,
                                    NoiseDeformType noise_type, int seed);
data::PcgMeshData bevel_mesh(const data::PcgMeshData& mesh, double amount, int segments,
                             BevelMethod method = BevelMethod::Edge,
                             BevelOffsetType offset_type = BevelOffsetType::Offset,
                             bool clamp_overlap = true, double angle_limit_deg = 30.0,
                             float profile = 0.5f,
                             BevelMiter miter_outer = BevelMiter::Sharp,
                             BevelMiter miter_inner = BevelMiter::Sharp,
                             BevelVMeshMethod vmesh_method = BevelVMeshMethod::Adj);

} // namespace pcg::internal::elements
