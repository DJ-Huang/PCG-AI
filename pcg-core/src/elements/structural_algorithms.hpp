#pragma once

#include "data/pcg_point_data.hpp"
#include "data/pcg_spline_data.hpp"

#include <utility>
#include <vector>

namespace pcg::internal::elements {

struct Edge2D {
    int a = 0;
    int b = 0;
    double weight = 0.0;
};

struct Vec2D {
    double x = 0.0;
    double z = 0.0;
};

std::vector<Vec2D> points_to_xz(const data::PcgPointData& points);
data::PcgSplineData convex_hull_spline(const data::PcgPointData& points, double tolerance = 0.0);
data::PcgSplineData connect_nearest_splines(const data::PcgPointData& points, int k, double max_distance);
data::PcgSplineData delaunay_edge_splines(const data::PcgPointData& points);
data::PcgSplineData mst_splines(const data::PcgSplineData& edges, const data::PcgPointData& points);
data::PcgSplineData voronoi_edge_splines(const data::PcgPointData& points);
data::PcgSplineData astar_path_spline(const data::PcgSplineData& edges,
                                      const data::PcgPointData& points,
                                      int start_index,
                                      int end_index);

std::vector<Edge2D> extract_edges_from_splines(const data::PcgSplineData& splines,
                                                 const data::PcgPointData& points);

} // namespace pcg::internal::elements
