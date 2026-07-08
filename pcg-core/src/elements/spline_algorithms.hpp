#pragma once

#include "geometry/spline_geometry.hpp"

#include "data/pcg_mesh_data.hpp"
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
data::PcgSplineData resample_spline_data(const data::PcgSplineData& input, const ResampleSplineOptions& options);
data::PcgPointData sample_along_spline(const data::PcgSplineData& splines, const SampleAlongSplineOptions& options);
data::PcgMeshData extrude_along_spline(const data::PcgSplineData& splines,
                                         const data::PcgMeshData* profile_mesh,
                                         const ExtrudeAlongSplineOptions& options);
data::PcgMeshData transform_mesh(const data::PcgMeshData& mesh, const TransformMeshOptions& options);
data::PcgMeshData merge_meshes(const data::PcgMeshData& a, const data::PcgMeshData& b);
data::PcgMeshData instance_along_spline(const data::PcgSplineData& splines,
                                        const data::PcgMeshData& prototype,
                                        const InstanceAlongSplineOptions& options);

std::vector<geometry::Vec3> spline_data_to_polyline(const data::PcgSplineData& splines, size_t spline_index = 0);
data::PcgSplineData polyline_to_spline_data(const std::vector<geometry::Vec3>& polyline, bool closed = false);

} // namespace pcg::internal::elements
