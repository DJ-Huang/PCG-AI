#include "pcg_api.h"
#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_mesh_data.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using pcg::internal::data::PcgMeshData;

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

std::string spawn_graph_json()
{
    return R"({
      "version": "1.0",
      "nodes": [
        { "id": "box", "type": "CreateBoxMesh", "data": { "width": 1, "height": 1, "depth": 1 } },
        { "id": "grid", "type": "CreatePointGrid", "data": { "pointCountX": 2, "pointCountY": 2, "spacing": 1 } },
        { "id": "spawner", "type": "StaticMeshSpawner", "data": { "scale": 1 } },
        { "id": "output", "type": "Output", "data": {} }
      ],
      "edges": [
        { "id": "e1", "source": "grid", "target": "spawner", "sourceHandle": "out", "targetHandle": "in" },
        { "id": "e2", "source": "box", "target": "spawner", "sourceHandle": "out", "targetHandle": "mesh" },
        { "id": "e3", "source": "spawner", "target": "output", "sourceHandle": "out", "targetHandle": "in" }
      ]
    })";
}

PcgMeshData make_prototype(int index)
{
    const double width = 1.0 + index;
    const double height = 2.0 + index * 0.5;
    const double depth = 0.75 + index * 0.25;
    PcgMeshData mesh;
    mesh.vertices_mut() = {{0.0, 0.0, 0.0},
                           {width, 0.0, 0.0},
                           {width, height, depth},
                           {0.0, height, depth}};
    mesh.triangles_mut() = {0, 1, 2, 0, 2, 3};
    mesh.set_materials({"building_variant_" + std::to_string(index)}, {0, 0});
    return mesh;
}

void test_multi_prototype_pcms()
{
    std::vector<PcgMeshData> input_meshes;
    for (int i = 0; i < 6; ++i)
        input_meshes.push_back(make_prototype(i));
    const std::vector<int> input_counts = {3, 0, 5, 1, 4, 2};

    const int required = pcg::internal::data::multi_spawn_binary_size(input_meshes, input_counts);
    expect(required > 0, "six-prototype PCMS reports a positive size");
    std::vector<uint8_t> buffer(static_cast<size_t>(required));
    expect(pcg::internal::data::write_multi_spawn_binary(
               input_meshes, input_counts, buffer.data(), static_cast<int>(buffer.size())),
           "six-prototype PCMS write succeeds");

    std::vector<PcgMeshData> output_meshes;
    std::vector<int> output_counts;
    expect(pcg::internal::data::read_multi_spawn_binary(
               buffer.data(), static_cast<int>(buffer.size()), output_meshes, output_counts),
           "six-prototype PCMS read succeeds");
    expect(output_meshes.size() == 6, "PCMS preserves six prototypes");
    expect(output_counts == input_counts, "PCMS preserves point counts including empty batch");
    for (size_t i = 0; i < output_meshes.size(); ++i) {
        expect(output_meshes[i].vertices().size() == input_meshes[i].vertices().size(),
               "PCMS preserves prototype vertex count at index " + std::to_string(i));
        expect(output_meshes[i].triangles().size() == input_meshes[i].triangles().size(),
               "PCMS preserves prototype index count at index " + std::to_string(i));
        expect(output_meshes[i].material_slots() == input_meshes[i].material_slots(),
               "PCMS preserves prototype material order at index " + std::to_string(i));
        expect(output_meshes[i].vertices()[1].x == input_meshes[i].vertices()[1].x &&
                   output_meshes[i].vertices()[2].y == input_meshes[i].vertices()[2].y &&
                   output_meshes[i].vertices()[2].z == input_meshes[i].vertices()[2].z,
               "PCMS preserves prototype dimensions at index " + std::to_string(i));
    }
    std::printf("multi-prototype PCMS round-trip passed: prototypes=%zu emptyIndex=1\n",
                output_meshes.size());
}

} // namespace

int main()
{
    test_multi_prototype_pcms();
    const std::string json = spawn_graph_json();
    char err[1024] = {};
    int kind = 0;
    int point_count = 0;
    uint32_t flags = 0;
    int vertex_count = 0;
    int index_count = 0;
    std::vector<uint8_t> points_buf(1024 * 1024);
    std::vector<uint8_t> mesh_buf(1024 * 1024);
    char out_json[64] = {};

    const PcgResultCode rc = pcg_execute_graph_v6(
        json.c_str(),
        42,
        nullptr,
        0,
        nullptr,
        0,
        &kind,
        out_json,
        sizeof(out_json),
        mesh_buf.data(),
        static_cast<int>(mesh_buf.size()),
        points_buf.data(),
        static_cast<int>(points_buf.size()),
        &point_count,
        &flags,
        &vertex_count,
        &index_count,
        nullptr,
        nullptr,
        0,
        err,
        sizeof(err));

    if (rc != PCG_OK) {
        std::fprintf(stderr, "FAIL: pcg_execute_graph_v6: %s\n", err);
        return 1;
    }
    if (kind != PCG_RESULT_KIND_POINTS) {
        std::fprintf(stderr, "FAIL: expected Points kind, got %d\n", kind);
        return 1;
    }
    if (point_count != 4) {
        std::fprintf(stderr, "FAIL: expected 4 points, got %d\n", point_count);
        return 1;
    }
    if (vertex_count <= 0 || index_count < 3) {
        std::fprintf(stderr, "FAIL: spawn mesh empty verts=%d indices=%d\n",
                     vertex_count, index_count);
        return 1;
    }
    if (out_json[0] != '\0') {
        std::fprintf(stderr, "FAIL: unexpected out_json\n");
        return 1;
    }

    std::printf("spawn mesh binary test passed: points=%d verts=%d indices=%d\n",
                point_count,
                vertex_count,
                index_count);
    return 0;
}
