#include "elements/add_algorithms.hpp"
#include "pcg_api.h"

#include <cstdio>
#include <vector>

using pcg::internal::elements::add_geometry;
using pcg::internal::elements::AddOptions;
using pcg::internal::elements::parse_add_points;
using pcg::internal::elements::parse_add_polygon_line;

namespace {

int g_failures = 0;

void expect(bool ok, const char* message)
{
    if (!ok) {
        std::printf("FAIL: %s\n", message);
        ++g_failures;
    }
}

void expect_indices(const std::vector<int>& actual,
                    const std::vector<int>& expected,
                    const char* message)
{
    if (actual != expected) {
        std::printf("FAIL: %s\n", message);
        ++g_failures;
    }
}

} // namespace

int main()
{
    expect_indices(parse_add_polygon_line("0-3", 8), {0, 1, 2, 3}, "range 0-3");
    expect_indices(parse_add_polygon_line("1 3 5", 8), {1, 3, 5}, "single indices");
    expect_indices(parse_add_polygon_line("0-5:2,3", 10), {2, 5}, "stepped range 0-5:2,3");

    const nlohmann::json data = {
        {"points",
         nlohmann::json::array({
             {{"enabled", true}, {"x", 0.0}, {"y", 0.0}, {"z", 0.0}},
             {{"enabled", false}, {"x", 9.0}, {"y", 0.0}, {"z", 0.0}},
             {{"enabled", true}, {"x", 1.0}, {"y", 0.0}, {"z", 0.0}},
             {{"enabled", true}, {"x", 1.0}, {"y", 0.0}, {"z", 1.0}},
             {{"enabled", true}, {"x", 0.0}, {"y", 0.0}, {"z", 1.0}},
         })}};
    const auto parsed = parse_add_points(data);
    expect(parsed.size() == 4, "parse_add_points skips disabled entries");

    AddOptions options;
    options.points = parsed;
    options.polygons_spec = "0-3";
    const auto geometry = add_geometry({}, options);
    expect(geometry.points().size() == 4, "add_geometry appends four points");
    expect(geometry.faces().size() == 1, "add_geometry creates one polygon");
    if (!geometry.faces().empty())
        expect_indices(geometry.faces()[0], {0, 1, 2, 3}, "quad polygon indices");

    AddOptions keep_points;
    keep_points.delete_primitives_keep_points = true;
    keep_points.points = {{{true, 5.0, 0.0, 0.0, 1.0}}};
    pcg::internal::data::PcgGeometry box;
    box.points_mut() = {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}};
    box.faces_mut() = {{0, 1, 2, 3}};
    const auto kept = add_geometry(box, keep_points);
    expect(kept.faces().empty(), "deletePrimitivesKeepPoints clears faces");
    expect(kept.points().size() == 5, "deletePrimitivesKeepPoints keeps input points");

    const char* graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "add", "type": "Add", "data": {
          "points": [
            {"enabled": true, "x": 0, "y": 0, "z": 0},
            {"enabled": true, "x": 2, "y": 0, "z": 0},
            {"enabled": true, "x": 2, "y": 0, "z": 2},
            {"enabled": true, "x": 0, "y": 0, "z": 2}
          ],
          "polygons": "0-3"
        }},
        {"id": "out", "type": "Output", "data": {}}
      ],
      "edges": [
        {"source": "add", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    char err[512] = {};
    expect(pcg_validate_graph(graph, err, sizeof(err)) == PCG_OK, "Add graph validates");

    if (g_failures == 0)
        std::printf("PASS: test_add\n");
    return g_failures == 0 ? 0 : 1;
}
