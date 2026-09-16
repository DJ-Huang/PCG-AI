#include "pcg_api.h"

#include "data/pcg_geometry.hpp"
#include "data/pcg_point_data.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/mesh_scatter_algorithms.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

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

bool point_in_bbox(const pcg::internal::data::PcgPoint& p,
                   double min_x, double min_y, double min_z,
                   double max_x, double max_y, double max_z,
                   double eps)
{
    return p.x >= min_x - eps && p.x <= max_x + eps &&
           p.y >= min_y - eps && p.y <= max_y + eps &&
           p.z >= min_z - eps && p.z <= max_z + eps;
}

} // namespace

int main()
{
    using namespace pcg::internal::data;
    using namespace pcg::internal::elements;

    // Box 2x2x2 centered at origin, triangle soup.
    {
        const PcgMeshData box = create_box_mesh(2.0, 2.0, 2.0);

        PointsFromVolumeOptions opts;
        opts.point_separation = 0.5;
        opts.seed = 42;

        const PcgPointData points = sample_mesh_volume(box, opts);
        const auto& pts = points.points();

        // 4x4x4 = 64 voxels, all inside the box.
        expect(pts.size() == 64, "box mesh volume point count == 64");

        bool all_inside = true;
        for (const auto& p : pts) {
            if (!point_in_bbox(p, -1.0, -1.0, -1.0, 1.0, 1.0, 1.0, 1e-4)) {
                all_inside = false;
                break;
            }
        }
        expect(all_inside, "all volume points inside box AABB");
    }

    // Halving separation produces ~8x points.
    {
        const PcgMeshData box = create_box_mesh(2.0, 2.0, 2.0);

        PointsFromVolumeOptions opts1;
        opts1.point_separation = 0.5;
        opts1.seed = 1;
        const size_t count1 = sample_mesh_volume(box, opts1).points().size();

        PointsFromVolumeOptions opts2;
        opts2.point_separation = 0.25;
        opts2.seed = 1;
        const size_t count2 = sample_mesh_volume(box, opts2).points().size();

        expect(count1 == 64, "sep=0.5 -> 64 points");
        expect(count2 == 512, "sep=0.25 -> 512 points");
        expect(count2 >= count1 * 7, "halving separation produces ~8x points");
    }

    // shell_only: only boundary-layer voxels.
    {
        const PcgMeshData box = create_box_mesh(2.0, 2.0, 2.0);

        PointsFromVolumeOptions opts;
        opts.point_separation = 0.5;
        opts.seed = 1;
        opts.shell_only = true;

        const PcgPointData points = sample_mesh_volume(box, opts);
        const auto& pts = points.points();

        // 4x4x4 grid, interior 2x2x2 = 8, shell = 64 - 8 = 56.
        expect(pts.size() == 56, "shell_only produces 56 points (64 - 8 interior)");

        bool all_inside = true;
        for (const auto& p : pts) {
            if (!point_in_bbox(p, -1.0, -1.0, -1.0, 1.0, 1.0, 1.0, 1e-4)) {
                all_inside = false;
                break;
            }
        }
        expect(all_inside, "shell points inside box AABB");
    }

    // Geometry input (PcgGeometry with quads).
    {
        const PcgGeometry box = create_box_geometry(2.0, 2.0, 2.0);

        PointsFromVolumeOptions opts;
        opts.point_separation = 0.5;
        opts.seed = 42;

        const PcgPointData points = sample_mesh_volume(box, opts);
        const auto& pts = points.points();

        expect(pts.size() == 64, "geometry volume point count == 64");

        bool all_inside = true;
        for (const auto& p : pts) {
            if (!point_in_bbox(p, -1.0, -1.0, -1.0, 1.0, 1.0, 1.0, 1e-4)) {
                all_inside = false;
                break;
            }
        }
        expect(all_inside, "geometry volume points inside box AABB");
    }

    // Jitter does not move points outside the AABB (with slack).
    {
        const PcgMeshData box = create_box_mesh(2.0, 2.0, 2.0);

        PointsFromVolumeOptions opts;
        opts.point_separation = 0.5;
        opts.seed = 7;
        opts.jitter = 0.1;

        const PcgPointData points = sample_mesh_volume(box, opts);
        const auto& pts = points.points();

        expect(pts.size() == 64, "jitter preserves point count");

        bool all_inside = true;
        for (const auto& p : pts) {
            if (!point_in_bbox(p, -1.2, -1.2, -1.2, 1.2, 1.2, 1.2, 1e-4)) {
                all_inside = false;
                break;
            }
        }
        expect(all_inside, "jittered points near box AABB");
    }

    if (g_fail > 0) {
        std::printf("\n%d test(s) failed.\n", g_fail);
        return 1;
    }
    std::printf("\nAll PointsFromVolume tests passed.\n");
    return 0;
}
