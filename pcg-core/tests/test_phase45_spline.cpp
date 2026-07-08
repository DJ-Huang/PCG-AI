#include "pcg_api.h"

#include "data/pcg_mesh_binary.hpp"
#include "elements/spline_algorithms.hpp"
#include "elements/mesh_algorithms.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void expect_code(PcgResultCode actual, PcgResultCode expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL: %s expected %d got %d\n", label, static_cast<int>(expected),
                    static_cast<int>(actual));
        std::exit(1);
    }
}

bool read_mesh_binary(const void* buffer,
                      int buffer_size,
                      int* vertex_count,
                      int* index_count,
                      std::vector<float>& positions,
                      std::vector<uint32_t>& indices)
{
    if (!buffer || buffer_size < 16 || !vertex_count || !index_count)
        return false;

    const auto* bytes = static_cast<const uint8_t*>(buffer);
    const uint32_t magic = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
    if (magic != PCG_MESH_BINARY_MAGIC)
        return false;

    *vertex_count = static_cast<int>(bytes[8] | (bytes[9] << 8) | (bytes[10] << 16) | (bytes[11] << 24));
    *index_count = static_cast<int>(bytes[12] | (bytes[13] << 8) | (bytes[14] << 16) | (bytes[15] << 24));

    const int pos_bytes = *vertex_count * 3 * static_cast<int>(sizeof(float));
    const int idx_bytes = *index_count * static_cast<int>(sizeof(uint32_t));
    if (buffer_size < 16 + pos_bytes + idx_bytes)
        return false;

    positions.resize(static_cast<size_t>(*vertex_count) * 3);
    std::memcpy(positions.data(), bytes + 16, static_cast<size_t>(pos_bytes));
    indices.resize(static_cast<size_t>(*index_count));
    std::memcpy(indices.data(), bytes + 16 + pos_bytes, static_cast<size_t>(idx_bytes));
    return true;
}

PcgResultCode execute_mesh_graph(const char* json, int seed, int& vertex_count, int& index_count)
{
    std::vector<uint8_t> mesh_buf(8 * 1024 * 1024);
    int kind = 0;
  char json_out[4096];
    char err[512];
    const PcgResultCode rc = pcg_execute_graph_v7(
        json, seed, nullptr, 0, nullptr, 0, nullptr, 0, &kind, json_out, sizeof(json_out),
        mesh_buf.data(), static_cast<int>(mesh_buf.size()), nullptr, 0, nullptr, nullptr,
        &vertex_count, &index_count, nullptr, nullptr, 0, err, sizeof(err));
    if (rc != PCG_OK)
        std::printf("execute error: %s\n", err);
    return rc;
}

} // namespace

int main()
{
    using namespace pcg::internal::elements;

    CreateSplineOptions create_opts;
    create_opts.mode = "line";
    create_opts.start_x = 0.0;
    create_opts.end_x = 20.0;
    const auto spline = create_spline_data(create_opts);
    if (spline.splines().empty() || spline.splines().front().points.size() < 2)
    {
        std::printf("FAIL: create_spline_data line\n");
        return 1;
    }
    std::printf("PASS: create_spline_data line (%zu points)\n",
                spline.splines().front().points.size());

    ExtrudeAlongSplineOptions extrude_opts;
    extrude_opts.profile_width = 4.0;
    extrude_opts.profile_height = 0.5;
    extrude_opts.sample_spacing = 1.0;
    const auto deck = extrude_along_spline(spline, nullptr, extrude_opts);
    if (deck.vertices().empty() || deck.triangles().empty())
    {
        std::printf("FAIL: extrude_along_spline unit\n");
        return 1;
    }
    std::printf("PASS: extrude_along_spline unit (%zu verts, %zu tris)\n", deck.vertices().size(),
                deck.triangles().size() / 3);

    SweepAlongSplineOptions sweep_opts;
    sweep_opts.surface_shape = "rectangle";
    sweep_opts.profile_width = 4.0;
    sweep_opts.profile_height = 0.5;
    sweep_opts.sample_spacing = 1.0;
    const auto swept_rect = sweep_along_spline(spline, nullptr, sweep_opts);
    if (swept_rect.vertices().empty() || swept_rect.triangles().empty())
    {
        std::printf("FAIL: sweep_along_spline rectangle\n");
        return 1;
    }
    std::printf("PASS: sweep_along_spline rectangle (%zu verts, %zu tris)\n",
                swept_rect.vertices().size(), swept_rect.triangles().size() / 3);

    CreateSplineOptions profile_opts_create;
    profile_opts_create.mode = "polyline";
    profile_opts_create.closed = true;
    profile_opts_create.control_points = {
        {-2.0, -0.25, 0.0},
        {2.0, -0.25, 0.0},
        {2.0, 0.25, 0.0},
        {-2.0, 0.25, 0.0},
    };
    const auto profile_spline = create_spline_data(profile_opts_create);

    SweepAlongSplineOptions curve_sweep_opts;
    curve_sweep_opts.surface_shape = "crossSection";
    curve_sweep_opts.use_profile_spline = true;
    curve_sweep_opts.sample_spacing = 1.0;
    const auto swept_curve = sweep_along_spline(spline, &profile_spline, curve_sweep_opts);
    if (swept_curve.vertices().empty() || swept_curve.triangles().empty())
    {
        std::printf("FAIL: sweep_along_spline profile curve\n");
        return 1;
    }
    std::printf("PASS: sweep_along_spline profile curve (%zu verts, %zu tris)\n",
                swept_curve.vertices().size(), swept_curve.triangles().size() / 3);

    const auto box_profile = create_box_mesh(4.0, 1.0, 0.05);
    ExtrudeAlongSplineOptions profile_opts;
    profile_opts.sample_spacing = 1.0;
    profile_opts.use_profile_mesh = true;
    const auto swept_profile = extrude_along_spline(spline, &box_profile, profile_opts);
    if (swept_profile.vertices().empty() || swept_profile.triangles().empty())
    {
        std::printf("FAIL: extrude_along_spline profile mesh sweep\n");
        return 1;
    }
    std::printf("PASS: extrude_along_spline profile mesh sweep (%zu verts, %zu tris)\n",
                swept_profile.vertices().size(), swept_profile.triangles().size() / 3);

    const auto extracted = extract_cross_section_profile(box_profile, CrossSectionProfileOptions{});
    if (extracted.vertices().size() < 4 || extracted.triangles().size() < 6)
    {
        std::printf("FAIL: extract_cross_section_profile\n");
        return 1;
    }
    std::printf("PASS: extract_cross_section_profile (%zu verts, %zu tris)\n",
                extracted.vertices().size(), extracted.triangles().size() / 3);

    const char* bridge_graph = R"({
      "version": "1.0",
      "nodes": [
        {
          "id": "path",
          "type": "CreateSpline",
          "position": { "x": 0, "y": 0 },
          "data": {
            "mode": "catmullRom",
            "controlPoints": "[{\"x\":0,\"y\":0,\"z\":0},{\"x\":12,\"y\":3,\"z\":8},{\"x\":28,\"y\":0,\"z\":12},{\"x\":40,\"y\":0,\"z\":0}]",
            "subdivisions": 6
          }
        },
        {
          "id": "deck_profile",
          "type": "CreateSpline",
          "position": { "x": 0, "y": -180 },
          "data": {
            "mode": "polyline",
            "closed": true,
            "controlPoints": "[{\"x\":-3,\"y\":-0.2,\"z\":0},{\"x\":3,\"y\":-0.2,\"z\":0},{\"x\":3,\"y\":0.2,\"z\":0},{\"x\":-3,\"y\":0.2,\"z\":0}]"
          }
        },
        {
          "id": "deck",
          "type": "SweepAlongSpline",
          "position": { "x": 300, "y": 0 },
          "data": { "surfaceShape": "crossSection", "sampleSpacing": 1.0 }
        },
        {
          "id": "pier_proto",
          "type": "CreateBoxMesh",
          "position": { "x": 0, "y": 200 },
          "data": { "width": 1.0, "height": 6.0, "depth": 1.0 }
        },
        {
          "id": "piers",
          "type": "InstanceAlongSpline",
          "position": { "x": 300, "y": 200 },
          "data": { "spacing": 10.0, "includeEnd": false, "scale": 1.0 }
        },
        {
          "id": "merge",
          "type": "MergeMesh",
          "position": { "x": 600, "y": 100 },
          "data": {}
        },
        {
          "id": "out",
          "type": "Output",
          "position": { "x": 900, "y": 100 },
          "data": { "label": "Bridge" }
        }
      ],
      "edges": [
        { "id": "e1", "source": "path", "target": "deck", "sourceHandle": "out", "targetHandle": "backbone" },
        { "id": "e1b", "source": "deck_profile", "target": "deck", "sourceHandle": "out", "targetHandle": "profile" },
        { "id": "e2", "source": "path", "target": "piers", "sourceHandle": "out", "targetHandle": "spline" },
        { "id": "e3", "source": "pier_proto", "target": "piers", "sourceHandle": "out", "targetHandle": "mesh" },
        { "id": "e4", "source": "deck", "target": "merge", "sourceHandle": "out", "targetHandle": "a" },
        { "id": "e5", "source": "piers", "target": "merge", "sourceHandle": "out", "targetHandle": "b" },
        { "id": "e6", "source": "merge", "target": "out", "sourceHandle": "out", "targetHandle": "in" }
      ]
    })";

    int vertex_count = 0;
    int index_count = 0;
    expect_code(execute_mesh_graph(bridge_graph, 42, vertex_count, index_count), PCG_OK,
                "bridge graph execute");
    if (vertex_count < 100 || index_count < 100)
    {
        std::printf("FAIL: bridge mesh too small (%d verts, %d indices)\n", vertex_count,
                    index_count);
        return 1;
    }
    std::printf("PASS: bridge graph mesh (%d verts, %d indices)\n", vertex_count, index_count);

    std::printf("All phase45 spline tests passed.\n");
    return 0;
}
