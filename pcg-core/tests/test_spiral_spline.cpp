#include "elements/spline_algorithms.hpp"
#include "data/pcg_spline_data.hpp"

#include <cmath>
#include <cstdio>

using namespace pcg::internal::data;
using namespace pcg::internal::elements;

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

} // namespace

int main()
{
    // --- Spiral: 3 turns, default axis Y ---
    {
        CreateSpiralSplineOptions opts;
        opts.radius = 1.0;
        opts.pitch = 0.5;
        opts.turns = 3.0;
        opts.points_per_turn = 24;
        opts.axis = "y";

        auto spline_data = create_spiral_spline_data(opts);
        expect(spline_data.splines().size() == 1, "spiral 3t: 1 spline");
        const auto& pts = spline_data.splines()[0].points;
        // sample_count = ceil(3*24) + 1 = 73
        expect(pts.size() == 73, "spiral 3t: 73 samples");
        expect(!spline_data.splines()[0].closed, "spiral 3t: not closed");

        // Final point should be at t=3, height = 3*0.5 = 1.5
        const auto& last = pts.back();
        expect(std::abs(last.y - 1.5) < 1e-10, "spiral 3t: endpoint height = 1.5");

        // First point should be at t=0, height = 0, angle = 0
        const auto& first = pts.front();
        expect(std::abs(first.y - 0.0) < 1e-10, "spiral 3t: start height = 0");
        expect(std::abs(first.x - 1.0) < 1e-10, "spiral 3t: start x = radius");
        expect(std::abs(first.z - 0.0) < 1e-10, "spiral 3t: start z = 0");
    }

    // --- Spiral AC2.4: 2.5 turns, fractional ---
    {
        CreateSpiralSplineOptions opts;
        opts.radius = 2.0;
        opts.pitch = 1.0;
        opts.turns = 2.5;
        opts.points_per_turn = 10;
        opts.axis = "y";

        auto spline_data = create_spiral_spline_data(opts);
        const auto& pts = spline_data.splines()[0].points;
        // sample_count = ceil(2.5*10) + 1 = 26
        expect(pts.size() == 26, "spiral 2.5t: 26 samples");

        // Endpoint height = 2.5 * 1.0 = 2.5
        expect(std::abs(pts.back().y - 2.5) < 1e-10, "spiral AC2.4: endpoint height = 2.5*pitch");

        // Fractional turn preserved: t for last sample = min(25/10, 2.5) = 2.5
        // angle = 2.5 * 2π = 5π → cos = -1, sin = 0 (but for 2.5 turns, angle = 5π)
        // Actually 2.5 * 2π = 5π, cos(5π) = -1, sin(5π) = 0
        expect(std::abs(pts.back().x - (-2.0)) < 1e-10, "spiral AC2.4: endpoint x = -radius");
    }

    // --- Spiral: X axis ---
    {
        CreateSpiralSplineOptions opts;
        opts.radius = 1.0;
        opts.pitch = 0.5;
        opts.turns = 1.0;
        opts.points_per_turn = 8;
        opts.axis = "x";

        auto spline_data = create_spiral_spline_data(opts);
        const auto& pts = spline_data.splines()[0].points;
        // sample_count = ceil(8) + 1 = 9
        expect(pts.size() == 9, "spiral X: 9 samples");
        // height along X: endpoint x = 1.0 * 0.5 = 0.5
        expect(std::abs(pts.back().x - 0.5) < 1e-10, "spiral X: endpoint x = 0.5");
    }

    // --- Spiral: Z axis ---
    {
        CreateSpiralSplineOptions opts;
        opts.radius = 1.0;
        opts.pitch = 0.3;
        opts.turns = 2.0;
        opts.points_per_turn = 12;
        opts.axis = "z";

        auto spline_data = create_spiral_spline_data(opts);
        const auto& pts = spline_data.splines()[0].points;
        // sample_count = ceil(24) + 1 = 25
        expect(pts.size() == 25, "spiral Z: 25 samples");
        // height along Z: endpoint z = 2.0 * 0.3 = 0.6
        expect(std::abs(pts.back().z - 0.6) < 1e-10, "spiral Z: endpoint z = 0.6");
    }

    // --- Spiral: invalid params ---
    {
        CreateSpiralSplineOptions opts;

        opts.radius = 0.0;
        expect(create_spiral_spline_data(opts).splines().empty(), "spiral: radius=0 rejected");

        opts.radius = 1.0;
        opts.pitch = 0.0;
        expect(create_spiral_spline_data(opts).splines().empty(), "spiral: pitch=0 rejected");

        opts.pitch = 0.5;
        opts.turns = 0.0;
        expect(create_spiral_spline_data(opts).splines().empty(), "spiral: turns=0 rejected");

        opts.turns = 1.0;
        opts.points_per_turn = 3;
        expect(create_spiral_spline_data(opts).splines().empty(), "spiral: pointsPerTurn<4 rejected");

        opts.points_per_turn = 8;
        opts.axis = "w";
        expect(create_spiral_spline_data(opts).splines().empty(), "spiral: invalid axis rejected");
    }

    // --- Spiral: negative pitch (descending) ---
    {
        CreateSpiralSplineOptions opts;
        opts.radius = 1.0;
        opts.pitch = -0.5;
        opts.turns = 2.0;
        opts.points_per_turn = 10;
        opts.axis = "y";

        auto spline_data = create_spiral_spline_data(opts);
        const auto& pts = spline_data.splines()[0].points;
        expect(pts.size() == 21, "spiral neg pitch: 21 samples");
        expect(std::abs(pts.back().y - (-1.0)) < 1e-10, "spiral neg pitch: endpoint y = -1.0");
    }

    // --- Arc: semicircle in XY (axis Z) ---
    {
        CreateArcSplineOptions opts;
        opts.radius = 2.0;
        opts.start_angle_deg = 0.0;
        opts.end_angle_deg = 180.0;
        opts.segments = 8;
        opts.axis = "z";

        auto spline_data = create_arc_spline_data(opts);
        expect(spline_data.splines().size() == 1, "arc: 1 spline");
        const auto& pts = spline_data.splines()[0].points;
        expect(pts.size() == 9, "arc: segments+1 samples");
        expect(!spline_data.splines()[0].closed, "arc: open polyline");
        expect(std::abs(pts.front().x - 2.0) < 1e-10 && std::abs(pts.front().y) < 1e-10,
               "arc: start at (radius,0,0)");
        expect(std::abs(pts.back().x - (-2.0)) < 1e-10 && std::abs(pts.back().y) < 1e-10,
               "arc: end at (-radius,0,0)");
        expect(std::abs(pts[4].x) < 1e-10 && std::abs(pts[4].y - 2.0) < 1e-10,
               "arc: midpoint at (0,radius,0)");
    }

    // --- Arc: reject invalid ---
    {
        CreateArcSplineOptions opts;
        opts.radius = 0.0;
        expect(create_arc_spline_data(opts).splines().empty(), "arc: radius=0 rejected");
        opts.radius = 1.0;
        opts.segments = 0;
        expect(create_arc_spline_data(opts).splines().empty(), "arc: segments<1 rejected");
        opts.segments = 4;
        opts.axis = "w";
        expect(create_arc_spline_data(opts).splines().empty(), "arc: invalid axis rejected");
    }

    if (g_fail > 0) {
        std::printf("\n%d tests FAILED\n", g_fail);
        return 1;
    }
    std::printf("\nAll spiral spline tests passed\n");
    return 0;
}
