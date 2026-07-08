#include "pcg_api.h"

#include "data/pcg_mesh_binary.hpp"
#include "elements/spline_algorithms.hpp"

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
          "id": "deck",
          "type": "ExtrudeAlongSpline",
          "position": { "x": 300, "y": 0 },
          "data": { "profileWidth": 6.0, "profileHeight": 0.4, "sampleSpacing": 1.0 }
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
        { "id": "e1", "source": "path", "target": "deck", "sourceHandle": "out", "targetHandle": "spline" },
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
