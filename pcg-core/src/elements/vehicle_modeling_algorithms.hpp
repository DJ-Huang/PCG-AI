#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_spline_data.hpp"

#include <string>
#include <vector>

namespace pcg::internal::elements {

struct LoftMeshOptions {
    int columns = 32;
    std::string sort_axis = "x";
    bool closed_profile = true;
    bool cap_start = true;
    bool cap_end = true;
    bool auto_align = true;
    std::string shade_mode = "auto";
    double cusp_angle_deg = 30.0;
};

struct MirrorMeshOptions {
    std::string axis = "z";
    double offset = 0.0;
    bool merge_original = true;
    bool weld_seam = true;
    double weld_tolerance = 0.0001;
};

struct FuseMeshOptions {
    double tolerance = 0.0001;
    bool remove_degenerate = true;
};

struct PolyExtrudeOptions {
    std::string face_group;
    double distance = 0.02;
    std::string distance_attribute;
    double inset = 0.0;
    bool keep_original = false;
    std::string top_group = "extrude_top";
    std::string side_group = "extrude_side";
};

struct CopyMeshOptions {
    std::string mode = "circular";
    int count = 6;
    std::string axis = "x";
    double angle = 360.0;
    double translate_x = 0.0;
    double translate_y = 0.0;
    double translate_z = 0.0;
    double center_x = 0.0;
    double center_y = 0.0;
    double center_z = 0.0;
};

struct ShellMeshOptions {
    double thickness = 0.02;
    std::string direction = "centered";
    bool close_boundaries = true;
    std::string outer_group = "shell_outer";
    std::string inner_group = "shell_inner";
    std::string rim_group = "shell_rim";
};

data::PcgGeometry loft_splines(const std::vector<data::PcgSpline>& profiles,
                               const LoftMeshOptions& options);
data::PcgGeometry mirror_geometry(const data::PcgGeometry& input,
                                  const MirrorMeshOptions& options);
data::PcgGeometry fuse_geometry(const data::PcgGeometry& input,
                                const FuseMeshOptions& options);
data::PcgGeometry poly_extrude_geometry(const data::PcgGeometry& input,
                                        const PolyExtrudeOptions& options);
data::PcgGeometry copy_geometry(const data::PcgGeometry& input,
                                const CopyMeshOptions& options);
data::PcgGeometry shell_geometry(const data::PcgGeometry& input,
                                 const ShellMeshOptions& options);

} // namespace pcg::internal::elements
