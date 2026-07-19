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
    std::string mode = "spacing";
    double spacing = 1.0;
    int point_count = 32;
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

struct TransformMeshOptions {
    double translate_x = 0.0;
    double translate_y = 0.0;
    double translate_z = 0.0;
    double rotation_x_deg = 0.0;
    double rotation_y_deg = 0.0;
    double rotation_z_deg = 0.0;
    double scale_x = 1.0;
    double scale_y = 1.0;
    double scale_z = 1.0;
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

data::PcgSplineData resample_spline_data(const data::PcgSplineData& input, const ResampleSplineOptions& options);
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
data::PcgMeshData transform_mesh(const data::PcgMeshData& mesh, const TransformMeshOptions& options);
data::PcgMeshData merge_meshes(const data::PcgMeshData& a, const data::PcgMeshData& b);
data::PcgMeshData merge_meshes(const std::vector<data::PcgMeshData>& meshes);
data::PcgMeshData instance_along_spline(const data::PcgSplineData& splines,
                                        const data::PcgMeshData& prototype,
                                        const InstanceAlongSplineOptions& options);

std::vector<geometry::Vec3> spline_data_to_polyline(const data::PcgSplineData& splines, size_t spline_index = 0);
data::PcgSplineData polyline_to_spline_data(const std::vector<geometry::Vec3>& polyline, bool closed = false);

} // namespace pcg::internal::elements
