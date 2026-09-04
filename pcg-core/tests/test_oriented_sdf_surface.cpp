#include "elements/oriented_sdf_surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

using namespace pcg::internal;
using namespace pcg::internal::data;
using namespace pcg::internal::elements;

namespace {

[[noreturn]] void fail(const std::string& message)
{
    std::fprintf(stderr, "FAIL: %s\n", message.c_str());
    std::exit(1);
}

void expect(bool condition, const std::string& message)
{
    if (!condition)
        fail(message);
}

std::vector<OrientedSurfacePoint> fibonacci_sphere(size_t count)
{
    constexpr double pi = 3.14159265358979323846;
    const double golden_angle = pi * (3.0 - std::sqrt(5.0));
    std::vector<OrientedSurfacePoint> points;
    points.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        const double y = 1.0 - (2.0 * index + 1.0) / count;
        const double radius = std::sqrt(std::max(0.0, 1.0 - y * y));
        const double angle = golden_angle * index;
        const PcgVec3 position{std::cos(angle) * radius, y, std::sin(angle) * radius};
        points.push_back({position, position,
                          {position.x * 0.25 + 0.5, position.y * 0.25 + 0.5,
                           position.z * 0.25 + 0.5, 1.0},
                          {static_cast<double>(index) / count, (y + 1.0) * 0.5}});
    }
    return points;
}

void expect_near(double actual, double expected, double tolerance, const std::string& message)
{
    if (std::abs(actual - expected) > tolerance)
        fail(message + ": expected " + std::to_string(expected) +
             ", got " + std::to_string(actual));
}

void test_codec_round_trip()
{
    const auto source = fibonacci_sphere(512);
    std::string payload;
    std::string error;
    OrientedPointCloudStats encoded;
    expect(encode_oriented_point_cloud(source, true, true, payload, encoded, error),
           "OPC1 encode succeeds: " + error);
    expect(!payload.empty(), "OPC1 payload is non-empty");
    expect(encoded.point_count == source.size(), "OPC1 preserves point count");
    expect(encoded.payload_bytes == 40 + source.size() * 20,
           "OPC1 v2 uses the documented 20-byte point records");

    std::vector<OrientedSurfacePoint> decoded;
    OrientedPointCloudStats decoded_stats;
    expect(decode_oriented_point_cloud(payload, decoded, decoded_stats, error),
           "OPC1 decode succeeds: " + error);
    expect(decoded.size() == source.size(), "OPC1 decode restores every point");
    expect(decoded_stats.has_source_colors, "OPC1 colour provenance survives round trip");
    expect(decoded_stats.has_source_uvs, "OPC1 UV provenance survives round trip");
    for (size_t index : {size_t{0}, size_t{127}, size_t{511}}) {
        expect_near(decoded[index].position.x, source[index].position.x, 4.0e-5,
                    "quantised X position remains accurate");
        expect_near(decoded[index].position.y, source[index].position.y, 4.0e-5,
                    "quantised Y position remains accurate");
        expect_near(decoded[index].position.z, source[index].position.z, 4.0e-5,
                    "quantised Z position remains accurate");
        const double alignment = decoded[index].normal.x * source[index].normal.x +
                                 decoded[index].normal.y * source[index].normal.y +
                                 decoded[index].normal.z * source[index].normal.z;
        expect(alignment > 0.99999, "quantised oriented normal remains aligned");
        expect_near(decoded[index].uv.u, source[index].uv.u, 2.0e-5,
                    "quantised U remains accurate");
        expect_near(decoded[index].uv.v, source[index].uv.v, 2.0e-5,
                    "quantised V remains accurate");
    }
}

void test_surface_nets_reconstruction()
{
    const auto samples = fibonacci_sphere(1024);
    OrientedSdfOptions options;
    options.cell_size = 0.1;
    options.support_radius_cells = 2.5;
    options.max_active_cells = 200000;
    options.transfer_colors = true;
    options.transfer_uvs = true;

    PcgGeometry first;
    PcgGeometry second;
    std::string error;
    expect(reconstruct_oriented_sdf_surface(samples, options, first, error),
           "oriented SDF reconstruction succeeds: " + error);
    expect(reconstruct_oriented_sdf_surface(samples, options, second, error),
           "repeated oriented SDF reconstruction succeeds: " + error);
    expect(first.points().size() > 1000, "Surface Nets emits a detailed sphere surface");
    expect(first.faces().size() > 1000, "Surface Nets connects the reconstructed surface");
    expect(first.points().size() == second.points().size() &&
           first.faces().size() == second.faces().size(),
           "reconstruction is deterministic");
    expect(first.has_colors() && first.colors().size() == first.points().size(),
           "surface colour transfer matches reconstructed vertices");
    expect(first.has_uvs() && first.uvs().size() == first.points().size(),
           "surface UV transfer matches reconstructed vertices");
    const auto* transferred_normals = first.attributes().find(AttributeOwner::Point, "N");
    expect(transferred_normals && transferred_normals->schema().tuple_size == 3 &&
           transferred_normals->size() == first.points().size(),
           "surface normal transfer matches reconstructed vertices");
    const auto& normal_values = transferred_normals->float_values();
    for (size_t point_index = 0; point_index < first.points().size(); ++point_index) {
        const PcgVec3 transferred{
            normal_values[point_index * 3],
            normal_values[point_index * 3 + 1],
            normal_values[point_index * 3 + 2],
        };
        const bool copied_from_measurement = std::any_of(
            samples.begin(), samples.end(), [&](const OrientedSurfacePoint& sample) {
                return std::abs(transferred.x - sample.normal.x) <= 1.0e-12 &&
                       std::abs(transferred.y - sample.normal.y) <= 1.0e-12 &&
                       std::abs(transferred.z - sample.normal.z) <= 1.0e-12;
            });
        expect(copied_from_measurement,
               "surface normal transfer selects one measured source normal");
    }
    for (const auto& uv : first.uvs()) {
        const bool copied_from_measurement = std::any_of(
            samples.begin(), samples.end(), [&](const OrientedSurfacePoint& sample) {
                return std::abs(uv.u - sample.uv.u) <= 1.0e-12 &&
                       std::abs(uv.v - sample.uv.v) <= 1.0e-12;
            });
        expect(copied_from_measurement,
               "surface UV transfer selects one measured UV instead of averaging across seams");
    }

    PcgVec3 low{1e9, 1e9, 1e9};
    PcgVec3 high{-1e9, -1e9, -1e9};
    for (const auto& point : first.points()) {
        low.x = std::min(low.x, point.x); low.y = std::min(low.y, point.y); low.z = std::min(low.z, point.z);
        high.x = std::max(high.x, point.x); high.y = std::max(high.y, point.y); high.z = std::max(high.z, point.z);
    }
    expect_near(low.x, -1.0, 0.16, "reconstructed sphere minimum X");
    expect_near(low.y, -1.0, 0.16, "reconstructed sphere minimum Y");
    expect_near(low.z, -1.0, 0.16, "reconstructed sphere minimum Z");
    expect_near(high.x, 1.0, 0.16, "reconstructed sphere maximum X");
    expect_near(high.y, 1.0, 0.16, "reconstructed sphere maximum Y");
    expect_near(high.z, 1.0, 0.16, "reconstructed sphere maximum Z");

    for (const auto& face : first.faces()) {
        expect(face.size() == 4, "Surface Nets emits quads before sink triangulation");
        for (int vertex : face)
            expect(vertex >= 0 && static_cast<size_t>(vertex) < first.points().size(),
                   "Surface Nets face index is valid");
    }
}

void test_triangle_interior_sampling()
{
    PcgGeometry triangle;
    triangle.points_mut() = {{0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}};
    triangle.faces_mut().push_back({0, 1, 2});
    triangle.set_uvs({{0.0, 0.0}, {1.0, 0.0}, {0.0, 1.0}});

    std::vector<OrientedSurfacePoint> samples;
    OrientedPointCloudStats stats;
    std::string error;
    expect(sample_oriented_geometry(triangle, 0.25, samples, stats, error),
           "triangle-interior sampling succeeds: " + error);
    expect(samples.size() > triangle.points().size(),
           "triangle-interior sampling adds topology-free measurements");
    expect(stats.source_point_count == 3, "sampling records the source vertex count");
    expect(stats.point_count == samples.size(), "sampling records the dense point count");
    expect(stats.has_source_uvs, "sampling preserves UV provenance");
    for (const auto& sample : samples) {
        expect_near(sample.position.z, 0.0, 1.0e-9, "sample stays on source triangle");
        expect(sample.position.x >= -1.0e-9 && sample.position.y >= -1.0e-9 &&
               sample.position.x + sample.position.y <= 1.0 + 1.0e-9,
               "sample remains inside the source triangle");
        expect_near(sample.uv.u, sample.position.x, 1.0e-9,
                    "barycentric sampling preserves U");
        expect_near(sample.uv.v, sample.position.y, 1.0e-9,
                    "barycentric sampling preserves V");
    }
}

} // namespace

int main()
{
    test_codec_round_trip();
    std::printf("PASS: OPC1 topology-free codec round trip\n");
    test_surface_nets_reconstruction();
    std::printf("PASS: sparse oriented MLS-SDF + Surface Nets reconstruction\n");
    test_triangle_interior_sampling();
    std::printf("PASS: dense topology-free triangle-interior sampling\n");
    return 0;
}
