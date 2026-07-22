#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"

#include <string>
#include <vector>

namespace pcg::internal::elements {

struct MeasureMeshOptions {
    std::string measure = "perimeter"; // perimeter | area | size | volume_approx
    std::string attribute_name = "length";
    std::string piece_attribute; // optional: per-piece via Connectivity class
};

struct BoundMeshOptions {
    bool oriented = false; // reserved; currently AABB
    double padding = 0.0;
};

struct ComputeNormalsOptions {
    std::string shade_mode = "auto"; // auto | smooth | flat
    double cusp_angle_deg = 30.0;
    bool write_point_n = true;
};

struct SmoothMeshOptions {
    int iterations = 5;
    double strength = 0.5; // 0..1
    bool fix_boundary = true;
};

struct ThickenMeshOptions {
    double depth = 0.2;
    std::string direction = "both"; // both | outward | inward
    bool dissolve_middle_edge = true;
};

struct PolySliceOptions {
    double origin_x = 0.0;
    double origin_y = 0.0;
    double origin_z = 0.0;
    double normal_x = 0.0;
    double normal_y = 1.0;
    double normal_z = 0.0;
};

struct PolyWireOptions {
    double radius = 0.05;
    int columns = 8;
    bool cap_start = true;
    bool cap_end = true;
};

struct ConnectivityOptions {
    std::string attribute_name = "class";
    std::string connectivity = "face"; // face (shared edge) | point (shared vertex)
};

struct AssembleOptions {
    std::string piece_attribute = "piece";
    std::string class_attribute = "class";
    bool create_if_missing = true;
};

struct SortDomainOptions {
    // nochange | vertexorder | x | y | z | reverse | random | shift |
    // proximity | vector | spatial | attribute | primindex | reorder | axis
    std::string method = "nochange";
    std::string axis = "x"; // legacy when method == "axis"
    std::string group;
    int seed = 0;
    int offset = 1;
    data::PcgVec3 proximity_point{};
    data::PcgVec3 vector{0.0, 1.0, 0.0};
    std::string attribute_name;
    int component = 0;
    std::string ordering_attribute = "sort_index";
    bool reverse = false;
    bool sort_indices = false;
    bool combine_sort_indices = true;
};

struct SortGeometryOptions {
    // Houdini Sort SOP: independent Point Sort then Primitive Sort.
    SortDomainOptions points;
    SortDomainOptions primitives;
    bool optimize_vertex_order = true;
};

struct CarveSplineOptions {
    double u_start = 0.0;
    double u_end = 1.0;
    bool use_first_u = true;
    bool use_second_u = true;
    /** Houdini: Carve Curves by Relative Arc Length */
    bool arc_length_u = true;
    /** "breakpoints" | "divisions" */
    std::string location = "breakpoints";
    bool cut_at_all_internal_u_breakpoints = true;
    int u_divisions = 1;
    bool keep_inside = true;
    bool keep_outside = false;
};

struct FindShortestPathOptions {
    int start_point = 0;
    int end_point = 1;
    std::string start_group;
    std::string end_group;
};

struct TreeSimpleLeafOptions {
    double leaf_width = 0.08;
    double leaf_height = 0.12;
    double leaf_thickness = 0.005;
    int seed = 0;
    double scale_min = 0.7;
    double scale_max = 1.3;
};

void set_detail_int(data::PcgGeometry& geometry, const std::string& name, int64_t value);
void set_detail_float(data::PcgGeometry& geometry, const std::string& name, double value);
int read_iterations_attribute(const data::PcgGeometry& geometry, const std::string& name, int fallback);

data::PcgGeometry measure_mesh_geometry(const data::PcgGeometry& input,
                                        const MeasureMeshOptions& options);
data::PcgGeometry bound_mesh_geometry(const data::PcgGeometry& input,
                                      const BoundMeshOptions& options);
data::PcgGeometry compute_normals_geometry(const data::PcgGeometry& input,
                                           const ComputeNormalsOptions& options);
data::PcgGeometry smooth_mesh_geometry(const data::PcgGeometry& input,
                                       const SmoothMeshOptions& options);
data::PcgGeometry reverse_mesh_geometry(const data::PcgGeometry& input);
data::PcgGeometry thicken_mesh_geometry(const data::PcgGeometry& input,
                                        const ThickenMeshOptions& options);
data::PcgGeometry poly_slice_geometry(const data::PcgGeometry& input,
                                      const PolySliceOptions& options);
data::PcgGeometry poly_wire_geometry(const data::PcgSplineData& splines,
                                     const PolyWireOptions& options);
data::PcgGeometry connectivity_geometry(const data::PcgGeometry& input,
                                        const ConnectivityOptions& options);
data::PcgGeometry assemble_geometry(const data::PcgGeometry& input,
                                    const AssembleOptions& options);
data::PcgGeometry sort_geometry(const data::PcgGeometry& input,
                                const SortGeometryOptions& options);
data::PcgSplineData carve_spline_data(const data::PcgSplineData& input,
                                      const CarveSplineOptions& options);
data::PcgSplineData find_shortest_path_on_mesh(const data::PcgGeometry& input,
                                               const FindShortestPathOptions& options);
data::PcgGeometry tree_simple_leaf_geometry(const data::PcgPointData& points,
                                            const TreeSimpleLeafOptions& options);

} // namespace pcg::internal::elements
