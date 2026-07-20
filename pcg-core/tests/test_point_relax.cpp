#include "pcg_api.h"

#include "data/pcg_point_data.hpp"
#include "elements/mesh_scatter_algorithms.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using json = nlohmann::json;

namespace {

int g_fail = 0;

void expect(bool cond, const char* msg)
{
    if (cond) {
        std::printf("PASS: %s\n", msg);
    } else {
        std::printf("FAIL: %s\n", msg);
        ++g_fail;
    }
}

// ── Direct algorithm test ──────────────────────────────────────────────────

void test_relax_overlapping_points()
{
    using namespace pcg::internal::data;
    using namespace pcg::internal::elements;

    PcgPointData input;
    // Four points crammed into a 1x1 square — all within 2*radius of each other.
    // Surface normal = +Y so relaxation is constrained to the XZ plane.
    auto add = [&](double x, double z) {
        PcgPoint p;
        p.x = x; p.y = 0.0; p.z = z;
        p.attributes = {{"pscale", 1.0}, {"nx", 0.0}, {"ny", 1.0}, {"nz", 0.0}};
        input.add_point(p);
    };
    add(0.0, 0.0);
    add(0.5, 0.0);
    add(0.0, 0.5);
    add(0.5, 0.5);

    PointRelaxOptions opts;
    opts.max_iterations = 200;
    opts.radius = 1.0;
    opts.use_pscale = true;

    PcgPointData result = relax_points(input, opts);
    const auto& pts = result.points();

    expect(pts.size() == 4, "relax preserves point count");

    // After relaxation, every pair should be at least (r_i + r_j) = 2.0 apart.
    bool all_separated = true;
    for (size_t i = 0; i < pts.size(); ++i) {
        for (size_t j = i + 1; j < pts.size(); ++j) {
            double dx = pts[i].x - pts[j].x;
            double dy = pts[i].y - pts[j].y;
            double dz = pts[i].z - pts[j].z;
            double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 2.0 - 1e-6) {
                all_separated = false;
                std::printf("  pair (%zu,%zu) dist=%.4f < 2.0\n", i, j, dist);
            }
        }
    }
    expect(all_separated, "relax: all point pairs separated by >= 2*radius");

    // Points should stay on the Y=0 plane (normal = +Y).
    bool on_plane = true;
    for (const auto& p : pts)
        if (std::abs(p.y) > 1e-6) on_plane = false;
    expect(on_plane, "relax: points stay on surface plane (Y=0)");
}

void test_relax_no_overlap()
{
    using namespace pcg::internal::data;
    using namespace pcg::internal::elements;

    PcgPointData input;
    auto add = [&](double x, double z) {
        PcgPoint p;
        p.x = x; p.y = 0.0; p.z = z;
        p.attributes = {{"pscale", 0.5}, {"nx", 0.0}, {"ny", 1.0}, {"nz", 0.0}};
        input.add_point(p);
    };
    // Points are 5 units apart — no overlap with radius 0.5.
    add(0.0, 0.0);
    add(5.0, 0.0);
    add(0.0, 5.0);
    add(5.0, 5.0);

    PointRelaxOptions opts;
    opts.max_iterations = 50;
    opts.radius = 0.5;
    opts.use_pscale = true;

    PcgPointData result = relax_points(input, opts);
    const auto& pts = result.points();

    // Positions should be unchanged.
    bool unchanged = true;
    for (size_t i = 0; i < pts.size(); ++i) {
        if (std::abs(pts[i].x - input.points()[i].x) > 1e-6 ||
            std::abs(pts[i].z - input.points()[i].z) > 1e-6) {
            unchanged = false;
        }
    }
    expect(unchanged, "relax: non-overlapping points unchanged");
}

void test_relax_uniform_radius()
{
    using namespace pcg::internal::data;
    using namespace pcg::internal::elements;

    PcgPointData input;
    // No pscale attribute; rely on uniform radius parameter.
    auto add = [&](double x, double z) {
        PcgPoint p;
        p.x = x; p.y = 0.0; p.z = z;
        p.attributes = {{"nx", 0.0}, {"ny", 1.0}, {"nz", 0.0}};
        input.add_point(p);
    };
    add(0.0, 0.0);
    add(0.3, 0.0);

    PointRelaxOptions opts;
    opts.max_iterations = 100;
    opts.radius = 1.0;
    opts.use_pscale = false;  // ignore pscale, use uniform radius

    PcgPointData result = relax_points(input, opts);
    const auto& pts = result.points();

    double dx = pts[0].x - pts[1].x;
    double dz = pts[0].z - pts[1].z;
    double dist = std::sqrt(dx * dx + dz * dz);
    expect(dist >= 2.0 - 1e-6, "relax: uniform radius separates points");
}

void test_relax_single_point()
{
    using namespace pcg::internal::data;
    using namespace pcg::internal::elements;

    PcgPointData input;
    PcgPoint p;
    p.x = 5.0; p.y = 0.0; p.z = 3.0;
    input.add_point(p);

    PointRelaxOptions opts;
    opts.max_iterations = 50;
    opts.radius = 1.0;

    PcgPointData result = relax_points(input, opts);
    expect(result.points().size() == 1, "relax: single point preserved");
    expect(std::abs(result.points()[0].x - 5.0) < 1e-9, "relax: single point unmoved");
}

// ── Graph executor integration test ────────────────────────────────────────

struct ExecResult {
    PcgResultCode code = PCG_OK;
    int kind = 0;
    std::vector<char> json_buf;
    std::string error;
};

ExecResult execute_graph(const char* graph_json, int seed)
{
    ExecResult r;
    r.json_buf.resize(8 * 1024 * 1024);
    std::vector<uint8_t> mesh_buf(8 * 1024 * 1024);
    char err_buf[1024] = {};

    r.code = pcg_execute_graph_v7(
        graph_json, seed,
        nullptr, 0,
        nullptr, 0,
        nullptr, 0,
        &r.kind, r.json_buf.data(), static_cast<int>(r.json_buf.size()),
        mesh_buf.data(), static_cast<int>(mesh_buf.size()),
        nullptr, 0,
        nullptr, nullptr, nullptr, nullptr,
        nullptr,
        nullptr, 0,
        err_buf, sizeof(err_buf));

    r.error = err_buf;
    return r;
}

void test_graph_point_relax()
{
    const char* graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "spawn", "type": "SpawnPoints", "position": {"x":0,"y":0},
         "data": {"count": 8, "radius": 2.0}},
        {"id": "relax", "type": "PointRelax", "position": {"x":200,"y":0},
         "data": {"maxIterations": 200, "radius": 1.0, "usePscale": false}},
        {"id": "out", "type": "Output", "position": {"x":400,"y":0}, "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "spawn", "target": "relax", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e2", "source": "relax", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    auto r = execute_graph(graph, 42);
    expect(r.code == PCG_OK, "graph: SpawnPoints → PointRelax → Output succeeds");
    expect(r.kind == PCG_RESULT_KIND_JSON, "graph: result is JSON (points)");

    json out = json::parse(r.json_buf.data());
    expect(out.contains("points"), "graph: JSON contains points array");

    const auto& pts = out["points"];
    expect(pts.size() == 8, "graph: 8 points in output");

    // Verify all pairs are at least 2.0 apart (radius=1.0, no pscale).
    bool all_separated = true;
    for (size_t i = 0; i < pts.size(); ++i) {
        for (size_t j = i + 1; j < pts.size(); ++j) {
            double dx = pts[i]["x"].get<double>() - pts[j]["x"].get<double>();
            double dy = pts[i]["y"].get<double>() - pts[j]["y"].get<double>();
            double dz = pts[i]["z"].get<double>() - pts[j]["z"].get<double>();
            double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < 2.0 - 1e-4) {
                all_separated = false;
                std::printf("  pair (%zu,%zu) dist=%.4f < 2.0\n", i, j, dist);
            }
        }
    }
    expect(all_separated, "graph: all points separated by >= 2*radius after relax");
}

} // namespace

int main()
{
    test_relax_overlapping_points();
    test_relax_no_overlap();
    test_relax_uniform_radius();
    test_relax_single_point();
    test_graph_point_relax();

    if (g_fail > 0) {
        std::printf("\n%d FAILURES\n", g_fail);
        return 1;
    }
    std::printf("\nAll PointRelax tests passed.\n");
    return 0;
}
