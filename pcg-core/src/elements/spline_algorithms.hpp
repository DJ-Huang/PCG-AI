#pragma once

#include "geometry/spline_geometry.hpp"

#include "data/pcg_mesh_data.hpp"
#include "data/pcg_geometry.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"

#include <string>
#include <vector>

namespace pcg::internal::elements {

struct CreateSplineOptions {
    std::string mode = "catmullRom";
    bool closed = false;
    int subdivisions = 8;
    double start_x = 0.0;
    double start_y = 0.0;
    double start_z = 0.0;
    double end_x = 10.0;
    double end_y = 0.0;
    double end_z = 0.0;
    std::vector<geometry::Vec3> control_points;
};

struct ResampleSplineOptions {
    // Legacy fields (still supported).
    std::string mode = "spacing";
    double spacing = 1.0;
    int point_count = 32;

    // Houdini Resample parity.
    bool use_max_segment_length = false;
    double max_segment_length = 0.1;
    bool use_max_segments = false;
    int max_segments = 2;
    std::string measure = "arc";
    bool even_last_segment_same_length = true;
    bool maintain_last_vertex = false;
    bool write_distance_attr = false;
    std::string distance_attribute = "ptdist";
    bool write_tangent_attr = false;
    std::string tangent_attribute = "tangentu";
    bool write_curve_u_attr = false;
    std::string curve_u_attribute = "curveu";
    bool write_curve_num_attr = false;
    std::string curve_num_attribute = "curvenum";
};

struct SampleAlongSplineOptions {
    double spacing = 5.0;
    double offset = 0.0;
    bool include_end = true;
    bool align_to_tangent = true;
    int seed = 0;
};

struct ExtrudeAlongSplineOptions {
    double profile_width = 2.0;
    double profile_height = 0.3;
    double sample_spacing = 1.0;
    bool cap_start = true;
    bool cap_end = true;
    bool use_profile_mesh = false;
    double up_x = 0.0;
    double up_y = 1.0;
    double up_z = 0.0;
    double twist_degrees = 0.0;
    double profile_roll_degrees = 0.0;
    double scale_start = 1.0;
    double scale_end = 1.0;
    std::string profile_plane = "auto";
};

struct CrossSectionProfileOptions {
    std::string plane = "auto";
    double weld_epsilon = 1e-4;
    bool center = true;
};

struct SweepAlongSplineOptions {
    std::string surface_shape = "crossSection";
    double profile_width = 6.0;
    double profile_height = 0.4;
    double radius = 1.0;
    int columns = 16;
    double sample_spacing = 1.0;
    bool cap_start = false;
    bool cap_end = false;
    bool use_profile_spline = false;
    double up_x = 0.0;
    double up_y = 1.0;
    double up_z = 0.0;
    double twist_degrees = 0.0;
    double profile_roll_degrees = 0.0;
    double scale_start = 1.0;
    double scale_end = 1.0;
    std::string profile_plane = "xy";
    std::string shade_mode = "auto";
    double cusp_angle_deg = 30.0;
};

struct InstanceAlongSplineOptions {
    double spacing = 5.0;
    double offset = 0.0;
    bool include_end = false;
    bool align_to_tangent = true;
    double scale = 1.0;
};

data::PcgSplineData create_spline_data(const CreateSplineOptions& options);

struct CreateSpiralSplineOptions {
    double radius = 1.0;
    double pitch = 0.5;
    double turns = 3.0;
    int points_per_turn = 24;
    std::string axis = "y";
};

data::PcgSplineData create_spiral_spline_data(const CreateSpiralSplineOptions& options);

struct CreateArcSplineOptions {
    double radius_x = 1.0;
    double radius_y = 1.0;
    double start_angle_deg = 0.0;
    double end_angle_deg = 180.0;
    int divisions = 16;
    std::string orientation = "xy";
    std::string arc_type = "openArc";
    data::PcgVec3 center{0.0, 0.0, 0.0};
    data::PcgVec3 rotate_deg{0.0, 0.0, 0.0};
    double uniform_scale = 1.0;
    bool reverse = false;
};

data::PcgSplineData create_arc_spline_data(const CreateArcSplineOptions& options);

data::PcgSplineData resample_spline_data(const data::PcgSplineData& input, const ResampleSplineOptions& options);

struct ProtectSpan {
    int start = 0;
    int end = 0;
};

struct ConditionOutlineOptions {
    int win = 1;           // odd >= 1; 1 = no smooth
    double eps = 0.0;      // finite >= 0; 0 disables RDP deletion
    std::vector<ProtectSpan> protect_spans;
};

/** Smooth then RDP-simplify each spline. Writes a concrete error and returns {} on failure. */
data::PcgSplineData condition_outline_data(const data::PcgSplineData& input,
                                           const ConditionOutlineOptions& options,
                                           std::string* error = nullptr);

data::PcgPointData sample_along_spline(const data::PcgSplineData& splines, const SampleAlongSplineOptions& options);
data::PcgMeshData extrude_along_spline(const data::PcgSplineData& splines,
                                         const data::PcgMeshData* profile_mesh,
                                         const ExtrudeAlongSplineOptions& options);
data::PcgMeshData extract_cross_section_profile(const data::PcgMeshData& mesh,
                                                const CrossSectionProfileOptions& options);
data::PcgMeshData sweep_along_spline(const data::PcgSplineData& backbone,
                                     const data::PcgSplineData* profile_spline,
                                     const SweepAlongSplineOptions& options);
data::PcgGeometry sweep_along_spline_geometry(const data::PcgSplineData& backbone,
                                              const data::PcgSplineData* profile_spline,
                                              const SweepAlongSplineOptions& options);
data::PcgMeshData merge_meshes(const data::PcgMeshData& a, const data::PcgMeshData& b);
data::PcgMeshData merge_meshes(const std::vector<data::PcgMeshData>& meshes);
data::PcgMeshData instance_along_spline(const data::PcgSplineData& splines,
                                        const data::PcgMeshData& prototype,
                                        const InstanceAlongSplineOptions& options);

std::vector<geometry::Vec3> spline_data_to_polyline(const data::PcgSplineData& splines, size_t spline_index = 0);
data::PcgSplineData polyline_to_spline_data(const std::vector<geometry::Vec3>& polyline, bool closed = false);

} // namespace pcg::internal::elements
