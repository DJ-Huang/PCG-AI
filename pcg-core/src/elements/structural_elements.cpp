#include "elements/element_utils.hpp"
#include "elements/pcg_element.hpp"
#include "elements/structural_algorithms.hpp"
#include "elements/structural_elements.hpp"

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

class ConvexHullElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ConvexHull"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        const nlohmann::json* input = require_input_json(ctx, "in", "ConvexHull missing points input");
        const data::PcgPointData points = parse_point_input(*input);
        if (points.points().size() < 3)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ConvexHull requires at least 3 points");

        emit_splines(ctx, convex_hull_spline(points));
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

        const nlohmann::json* input = require_input_json(ctx, "in", "ConnectNearest missing points input");
        const data::PcgPointData points = parse_point_input(*input);
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
        const nlohmann::json* input = require_input_json(ctx, "in", "Delaunay missing points input");
        const data::PcgPointData points = parse_point_input(*input);
        if (points.points().size() < 3)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Delaunay requires at least 3 points");

        emit_splines(ctx, delaunay_edge_splines(points));
        return PCG_OK;
    }
};

class MSTElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MST"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        const nlohmann::json* edge_input = require_input_json(ctx, "in", "MST missing edges input");
        const nlohmann::json* point_input = require_input_json(ctx, "points", "MST missing points input");
        const data::PcgSplineData edges = parse_spline_input(*edge_input);
        const data::PcgPointData points = parse_point_input(*point_input);

        emit_splines(ctx, mst_splines(edges, points));
        return PCG_OK;
    }
};

class VoronoiElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Voronoi"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        const nlohmann::json* input = require_input_json(ctx, "in", "Voronoi missing points input");
        const data::PcgPointData points = parse_point_input(*input);
        if (points.points().size() < 3)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Voronoi requires at least 3 points");

        emit_splines(ctx, voronoi_edge_splines(points));
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

        const nlohmann::json* edge_input = require_input_json(ctx, "in", "AStarPathfinding missing edges input");
        const nlohmann::json* point_input = require_input_json(ctx, "points", "AStarPathfinding missing points input");
        const data::PcgSplineData edges = parse_spline_input(*edge_input);
        const data::PcgPointData points = parse_point_input(*point_input);

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
