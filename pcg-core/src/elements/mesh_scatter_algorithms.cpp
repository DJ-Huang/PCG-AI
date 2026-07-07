#include "elements/mesh_scatter_algorithms.hpp"

#include "elements/element_utils.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pcg::internal::elements {
namespace {

struct TriangleRef {
    int i0 = 0;
    int i1 = 0;
    int i2 = 0;
    double area = 0.0;
};

double triangle_area(const data::PcgVertex& a, const data::PcgVertex& b, const data::PcgVertex& c)
{
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double abz = b.z - a.z;
    const double acx = c.x - a.x;
    const double acy = c.y - a.y;
    const double acz = c.z - a.z;
    const double cx = aby * acz - abz * acy;
    const double cy = abz * acx - abx * acz;
    const double cz = abx * acy - aby * acx;
    return 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
}

void face_normal(const data::PcgVertex& a,
                 const data::PcgVertex& b,
                 const data::PcgVertex& c,
                 double& nx,
                 double& ny,
                 double& nz)
{
    const double abx = b.x - a.x;
    const double aby = b.y - a.y;
    const double abz = b.z - a.z;
    const double acx = c.x - a.x;
    const double acy = c.y - a.y;
    const double acz = c.z - a.z;
    nx = aby * acz - abz * acy;
    ny = abz * acx - abx * acz;
    nz = abx * acy - aby * acx;
    const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len > 1e-12) {
        nx /= len;
        ny /= len;
        nz /= len;
    }
}

double rand01(uint32_t& state)
{
    return static_cast<double>(next_rand(state)) / 4294967296.0;
}

int pick_triangle(const std::vector<double>& cumulative, double sample, int fallback)
{
    const auto it = std::lower_bound(cumulative.begin(), cumulative.end(), sample);
    if (it == cumulative.end())
        return fallback;
    return static_cast<int>(std::distance(cumulative.begin(), it));
}

} // namespace

data::PcgPointData sample_mesh_surface(const data::PcgMeshData& mesh,
                                       const SampleMeshSurfaceOptions& options)
{
    data::PcgPointData points;
    if (options.count <= 0)
        return points;

    const auto& vertices = mesh.vertices();
    const auto& triangles = mesh.triangles();
    if (vertices.empty() || triangles.size() < 3)
        return points;

    std::vector<TriangleRef> tris;
    tris.reserve(triangles.size() / 3);
    for (size_t t = 0; t + 2 < triangles.size(); t += 3) {
        const int i0 = triangles[t];
        const int i1 = triangles[t + 1];
        const int i2 = triangles[t + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0 || static_cast<size_t>(i0) >= vertices.size() ||
            static_cast<size_t>(i1) >= vertices.size() || static_cast<size_t>(i2) >= vertices.size())
            continue;

        const double area =
            triangle_area(vertices[static_cast<size_t>(i0)], vertices[static_cast<size_t>(i1)],
                          vertices[static_cast<size_t>(i2)]);
        if (area <= 1e-12)
            continue;

        tris.push_back({i0, i1, i2, area});
    }

    if (tris.empty())
        return points;

    std::vector<double> cumulative;
    cumulative.reserve(tris.size());
    double total_area = 0.0;
    for (const auto& tri : tris) {
        total_area += tri.area;
        cumulative.push_back(total_area);
    }

    uint32_t rng = mix_seed(options.seed, static_cast<int>(tris.size() * 97 + options.count));

    for (int sample = 0; sample < options.count; ++sample) {
        const double pick = rand01(rng) * total_area;
        const int tri_index = pick_triangle(cumulative, pick, static_cast<int>(tris.size()) - 1);
        const TriangleRef& tri = tris[static_cast<size_t>(tri_index)];

        const data::PcgVertex& a = vertices[static_cast<size_t>(tri.i0)];
        const data::PcgVertex& b = vertices[static_cast<size_t>(tri.i1)];
        const data::PcgVertex& c = vertices[static_cast<size_t>(tri.i2)];

        const double r1 = std::sqrt(rand01(rng));
        const double r2 = rand01(rng);
        const double u = 1.0 - r1;
        const double v = r1 * (1.0 - r2);
        const double w = r1 * r2;

        double nx = 0.0;
        double ny = 0.0;
        double nz = 0.0;
        face_normal(a, b, c, nx, ny, nz);

        data::PcgPoint point;
        point.x = u * a.x + v * b.x + w * c.x;
        point.y = u * a.y + v * b.y + w * c.y;
        point.z = u * a.z + v * b.z + w * c.z;

        if (options.looseness > 0.0) {
            const double jitter = (rand01(rng) - 0.5) * options.looseness;
            point.x += nx * jitter;
            point.y += ny * jitter;
            point.z += nz * jitter;
        }

        if (options.normal_offset != 0.0) {
            point.x += nx * options.normal_offset;
            point.y += ny * options.normal_offset;
            point.z += nz * options.normal_offset;
        }

        point.attributes["nx"] = nx;
        point.attributes["ny"] = ny;
        point.attributes["nz"] = nz;
        point.attributes["triIndex"] = tri_index;
        points.add_point(point);
    }

    return points;
}

} // namespace pcg::internal::elements
