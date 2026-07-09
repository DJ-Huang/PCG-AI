#pragma once

#include "data/pcg_mesh_data.hpp"
#include "data/pcg_texture_data.hpp"
#include "elements/bevel_blender.hpp"

namespace pcg::internal::data {
class PcgGeometry;
}

namespace pcg::internal::elements {

using BevelEdgeSelection = bevel::BevelEdgeSelection;

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
    Texture,
};

enum class TextureCoordsMode {
    Local,
};

struct NoiseDeformOptions {
    double intensity = 0.02;
    double noise_scale = 2.0;
    double mid_level = 0.5;
    NoiseDeformType noise_type = NoiseDeformType::Perlin;
    TextureCoordsMode texture_coords = TextureCoordsMode::Local;
    int seed = 0;
    const data::PcgTextureData* texture = nullptr;
    double repeat_x = 1.0;
    double repeat_y = 1.0;
};

data::PcgMeshData create_box_mesh(double width, double height, double depth);
data::PcgMeshData subdivide_mesh(const data::PcgMeshData& mesh, int levels);
data::PcgMeshData noise_deform_mesh(const data::PcgMeshData& mesh, const NoiseDeformOptions& options);
data::PcgMeshData noise_deform_mesh(const data::PcgMeshData& mesh, double intensity, double noise_scale,
                                    NoiseDeformType noise_type, int seed);
data::PcgMeshData bevel_mesh(const data::PcgMeshData& mesh, double amount, int segments,
                             BevelMethod method = BevelMethod::Edge,
                             BevelOffsetType offset_type = BevelOffsetType::Offset,
                             bool clamp_overlap = true, double angle_limit_deg = 30.0,
                             float profile = 0.5f,
                             BevelMiter miter_outer = BevelMiter::Sharp,
                             BevelMiter miter_inner = BevelMiter::Sharp,
                             BevelVMeshMethod vmesh_method = BevelVMeshMethod::Adj,
                             bool (*is_cancel_requested)() = nullptr,
                             const BevelEdgeSelection& edge_selection = {},
                             const data::PcgGeometry* geometry = nullptr);
data::PcgMeshData bevel_geometry(const data::PcgGeometry& geometry, double amount, int segments,
                                 BevelMethod method = BevelMethod::Edge,
                                 BevelOffsetType offset_type = BevelOffsetType::Offset,
                                 bool clamp_overlap = true, double angle_limit_deg = 30.0,
                                 float profile = 0.5f,
                                 BevelMiter miter_outer = BevelMiter::Sharp,
                                 BevelMiter miter_inner = BevelMiter::Sharp,
                                 BevelVMeshMethod vmesh_method = BevelVMeshMethod::Adj,
                                 bool (*is_cancel_requested)() = nullptr,
                                 const BevelEdgeSelection& edge_selection = {});

} // namespace pcg::internal::elements
