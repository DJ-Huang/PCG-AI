#include "elements/element_utils.hpp"
#include "elements/pcg_element.hpp"
#include "elements/structural_algorithms.hpp"
#include "elements/structural_elements.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

int clamp_int(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

void filter_splines_by_max_length(data::PcgSplineData& data, double max_length)
{
    if (max_length < 0.0)
        return;
    auto& splines = data.splines_mut();
    splines.erase(
        std::remove_if(splines.begin(), splines.end(),
            [&](const data::PcgSpline& s) {
                if (s.points.size() < 2)
                    return true;
                const auto& p0 = s.points.front();
                const auto& p1 = s.points.back();
                const double dx = p1.x - p0.x;
                const double dy = p1.y - p0.y;
                const double dz = p1.z - p0.z;
                return std::sqrt(dx * dx + dy * dy + dz * dz) > max_length;
            }),
        splines.end());
}

void apply_offset_y(data::PcgSplineData& data, double offset_y)
{
    if (offset_y == 0.0)
        return;
    for (auto& spline : data.splines_mut())
        for (auto& pt : spline.points)
            pt.y += offset_y;
}

class ConvexHullElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ConvexHull"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ConvexHull missing node");

        data::PcgPointData points = get_points_input(ctx, "in", "ConvexHull missing points input");
        if (points.points().size() < 3)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ConvexHull requires at least 3 points");

        const double tolerance = ctx.node->data.value("tolerance", 0.0);
        emit_splines(ctx, convex_hull_spline(points, tolerance));
        return PCG_OK;
    }
};

class ConnectNearestElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ConnectNearest"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ConnectNearest missing node");

        data::PcgPointData points = get_points_input(ctx, "in", "ConnectNearest missing points input");
        if (points.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ConnectNearest missing points input");
        const int k = clamp_int(ctx.node->data.value("k", 1), 1, 16);
        const double max_distance = ctx.node->data.value("maxDistance", -1.0);

        emit_splines(ctx, connect_nearest_splines(points, k, max_distance));
        return PCG_OK;
    }
};

class DelaunayElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Delaunay"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Delaunay missing node");

        data::PcgPointData points = get_points_input(ctx, "in", "Delaunay missing points input");
        if (points.points().size() < 3)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Delaunay requires at least 3 points");

        const double max_edge_length = ctx.node->data.value("maxEdgeLength", -1.0);
        data::PcgSplineData splines = delaunay_edge_splines(points);
        filter_splines_by_max_length(splines, max_edge_length);
        emit_splines(ctx, std::move(splines));
        return PCG_OK;
    }
};

class MSTElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MST"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MST missing node");

        data::PcgSplineData edges = get_splines_input(ctx, "in", "MST missing edges input");
        if (edges.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MST missing edges input");

        const nlohmann::json* point_input = ctx.inputs.find_json("points");
        data::PcgPointData points;
        if (const data::PcgPointData* typed_points = ctx.inputs.find_points("points"))
            points = *typed_points;
        else if (point_input)
            points = parse_point_input(*point_input);

        const double max_edge_length = ctx.node->data.value("maxEdgeLength", -1.0);
        data::PcgSplineData splines = mst_splines(edges, points);
        filter_splines_by_max_length(splines, max_edge_length);
        emit_splines(ctx, std::move(splines));
        return PCG_OK;
    }
};

class VoronoiElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Voronoi"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Voronoi missing node");

        data::PcgPointData points = get_points_input(ctx, "in", "Voronoi missing points input");
        if (points.points().size() < 3)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Voronoi requires at least 3 points");

        const double offset_y = ctx.node->data.value("offsetY", 0.0);
        const double max_edge_length = ctx.node->data.value("maxEdgeLength", -1.0);
        data::PcgSplineData splines = voronoi_edge_splines(points);
        apply_offset_y(splines, offset_y);
        filter_splines_by_max_length(splines, max_edge_length);
        emit_splines(ctx, std::move(splines));
        return PCG_OK;
    }
};

class AStarPathfindingElement final : public IPcgElement {
public:
    const char* type_name() const override { return "AStarPathfinding"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AStarPathfinding missing node");

        data::PcgSplineData edges = get_splines_input(ctx, "in", "AStarPathfinding missing edges input");
        if (edges.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AStarPathfinding missing edges input");

        data::PcgPointData points;
        if (const data::PcgPointData* typed_points = ctx.inputs.find_points("points"))
            points = *typed_points;
        else if (const nlohmann::json* point_input = ctx.inputs.find_json("points"))
            points = parse_point_input(*point_input);
        else
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AStarPathfinding missing points input");

        const int n = static_cast<int>(points.points().size());
        const int start_index = clamp_int(ctx.node->data.value("startIndex", 0), 0, std::max(0, n - 1));
        const int end_index = clamp_int(ctx.node->data.value("endIndex", std::max(0, n - 1)), 0, std::max(0, n - 1));

        emit_splines(ctx, astar_path_spline(edges, points, start_index, end_index));
        return PCG_OK;
    }
};

} // namespace

void register_phase42_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("ConvexHull", std::make_unique<ConvexHullElement>());
    map.emplace("ConnectNearest", std::make_unique<ConnectNearestElement>());
    map.emplace("Delaunay", std::make_unique<DelaunayElement>());
    map.emplace("MST", std::make_unique<MSTElement>());
    map.emplace("Voronoi", std::make_unique<VoronoiElement>());
    map.emplace("AStarPathfinding", std::make_unique<AStarPathfindingElement>());
}

} // namespace pcg::internal::elements
