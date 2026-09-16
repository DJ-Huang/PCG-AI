#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"

#include <string>
#include <vector>

namespace pcg::internal::elements {

struct MeasureMeshOptions {
    std::string group;
    std::string element_type = "primitives"; // points | primitives
    /// perimeter | area | volume | size | volume_approx (legacy aliases kept)
    std::string measure = "perimeter";
    /// perElement | perPiece | throughout
    std::string accumulate = "perElement";
    std::string piece_attribute;
    bool refine_to_connected = true;

    bool use_position_attribute = false;
    std::string position_attribute = "P";

    bool use_minimum = false;
    double minimum = -1.0;
    bool use_maximum = false;
    double maximum = 1.0;
    bool use_width = true;
    double width = 6.0;
    /// unit | sd | mad  (Houdini: x 1.0 / x SD / x MAD)
    std::string width_scale = "mad";
    /// fixed | mean | median
    std::string center_type = "median";
    double center_fixed = 0.0;

    std::string attribute_name = "length";
    bool use_total_attribute = false;
    std::string total_attribute_name = "totalperimeter";
    bool use_range_group = false;
    std::string range_group = "inrange";
    bool bake_visualized_range = false;
    bool use_remap_range = false;
    double remap_min = 0.0;
    double remap_max = 1.0;
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
    std::string group;
    double u_start = 0.0;
    double u_end = 1.0;
    bool use_first_u = true;
    bool use_second_u = true;
    /** Houdini: primitive attribute scaling First/Second U per spline. */
    std::string u_start_attrib;
    std::string u_end_attrib;
    /** Houdini: Carve Curves by Relative Arc Length */
    bool arc_length_u = true;
    /** V parameters carve surfaces in Houdini; parsed for parity, no-op on 1D splines. */
    double v_start = 0.25;
    double v_end = 0.75;
    bool use_first_v = true;
    bool use_second_v = true;
    std::string v_start_attrib;
    std::string v_end_attrib;
    /** "divisions" | "breakpoints" */
    std::string location = "divisions";
    int u_divisions = 2;
    int v_divisions = 2;
    bool cut_at_all_internal_u_breakpoints = true;
    bool cut_at_all_internal_v_breakpoints = true;
    /** "cut" | "extract" */
    std::string operation = "cut";
    /** "curves3d" | "points"; identical on 1D splines (iso-curve of a curve is a point). */
    std::string extract_type = "curves3d";
    bool keep_original = false;
    bool only_at_breakpoints = false;
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

struct ResampleOptions {
    std::string group;
    bool maintain_primitive_order = false;
    int level_of_detail = 1;
    bool resample_by_polygon_edge = false;
    std::string method = "evenLength"; // evenLength | evenX | evenY | evenZ
    std::string measure = "arc";       // arc | chord
    bool use_max_segment_length = false;
    double max_segment_length = 0.1;
    bool use_max_segments = true;
    int max_segments = 2;
    bool allow_attribute_override = true;
    bool even_last_segment_same_length = true;
    bool maintain_last_vertex = false;
    bool randomize_first_segment_length = false;
    bool create_only_points = false;
    std::string treat_polygons_as = "straight"; // straight | subdivision | interpolating
    bool output_as_subdivision_curves = false;
    bool write_distance_attr = false;
    std::string distance_attribute = "ptdist";
    bool write_tangent_attr = false;
    std::string tangent_attribute = "tangentu";
    bool write_curve_u_attr = false;
    std::string curve_u_attribute = "curveu";
    bool write_curve_num_attr = false;
    std::string curve_num_attribute = "curvenum";
    int graph_seed = 0;
};

void set_detail_int(data::PcgGeometry& geometry, const std::string& name, int64_t value);
void set_detail_float(data::PcgGeometry& geometry, const std::string& name, double value);
int read_iterations_attribute(const data::PcgGeometry& geometry, const std::string& name, int fallback);

data::PcgGeometry measure_mesh_geometry(const data::PcgGeometry& input,
                                        const MeasureMeshOptions& options);

/// Measure polyline / curve length (and related scalars) on ConvertLine-style spline payloads.
data::PcgSplineData measure_spline_data(const data::PcgSplineData& input,
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
                                const SortGeometryOptions& options,
                                std::string* error_out = nullptr);
data::PcgSplineData sort_spline_data(const data::PcgSplineData& input,
                                     const SortGeometryOptions& options,
                                     std::string* error_out = nullptr);
data::PcgPointData sort_point_data(const data::PcgPointData& input,
                                   const SortDomainOptions& options,
                                   std::string* error_out = nullptr);
data::PcgSplineData carve_spline_data(const data::PcgSplineData& input,
                                      const CarveSplineOptions& options);
/// Houdini Carve Extract: points at each U location (and original vertices when keep_original).
data::PcgPointData carve_spline_extract_points(const data::PcgSplineData& input,
                                               const CarveSplineOptions& options);
data::PcgSplineData find_shortest_path_on_mesh(const data::PcgGeometry& input,
                                               const FindShortestPathOptions& options);
data::PcgGeometry resample_geometry(const data::PcgGeometry& input,
                                    const ResampleOptions& options);
data::PcgGeometry tree_simple_leaf_geometry(const data::PcgPointData& points,
                                            const TreeSimpleLeafOptions& options);

} // namespace pcg::internal::elements
