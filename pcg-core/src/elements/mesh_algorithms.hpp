#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_spline_data.hpp"
#include "data/pcg_texture_data.hpp"
#include "elements/bevel_blender.hpp"

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

/// Box as 8 shared corners + 6 quads (canonical Geometry; triangulate only at Sink).
data::PcgGeometry create_box_geometry(double width, double height, double depth);

/// Houdini Grid–style plane: rows×cols quads on XY / XZ / YZ, centered at origin.
data::PcgGeometry create_grid_geometry(double size_x, double size_y,
                                       int rows, int cols,
                                       const std::string& plane = "xz");

data::PcgMeshData create_cylinder_mesh(double radius, double height,
                                       int radial_segments, int height_segments,
                                       bool cap_top, bool cap_bottom);

/// Cylinder as quad side faces + optional n-gon caps (canonical Geometry;
/// triangulate only at Sink).
data::PcgGeometry create_cylinder_geometry(double radius, double height,
                                           int radial_segments, int height_segments,
                                           bool cap_top, bool cap_bottom);

struct RevolveGeometryOptions {
    std::string axis = "y";
    int segments = 16;
    bool close_profile = false;
    bool cap_start = false;
    bool cap_end = false;
};

data::PcgGeometry revolve_geometry(const data::PcgSplineData& profile,
                                  const RevolveGeometryOptions& options);

enum class SubdivideMethod {
    CatmullClark,
    Loop,
    Simple,
};

data::PcgMeshData subdivide_mesh(const data::PcgMeshData& mesh, int levels,
                                 SubdivideMethod method = SubdivideMethod::CatmullClark);
data::PcgGeometry subdivide_geometry(const data::PcgGeometry& geometry, int levels,
                                     SubdivideMethod method = SubdivideMethod::CatmullClark);
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
data::PcgGeometry bevel_geometry(const data::PcgGeometry& geometry, double amount, int segments,
                                 BevelMethod method = BevelMethod::Edge,
                                 BevelOffsetType offset_type = BevelOffsetType::Offset,
                                 bool clamp_overlap = true, double angle_limit_deg = 30.0,
                                 float profile = 0.5f,
                                 BevelMiter miter_outer = BevelMiter::Sharp,
                                 BevelMiter miter_inner = BevelMiter::Sharp,
                                 BevelVMeshMethod vmesh_method = BevelVMeshMethod::Adj,
                                 bool (*is_cancel_requested)() = nullptr,
                                 const BevelEdgeSelection& edge_selection = {});

data::PcgGeometry transform_geometry(const data::PcgGeometry& geometry,
                                    double translate_x, double translate_y, double translate_z,
                                    double rotation_x_deg, double rotation_y_deg, double rotation_z_deg,
                                    double scale_x, double scale_y, double scale_z);

} // namespace pcg::internal::elements
