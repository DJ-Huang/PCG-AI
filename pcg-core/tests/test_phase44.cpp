#include "pcg_api.h"

#include "data/pcg_point_data.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/mesh_scatter_algorithms.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void expect_code(PcgResultCode actual, PcgResultCode expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL: %s expected %d got %d\n", label, static_cast<int>(expected),
                    static_cast<int>(actual));
        std::exit(1);
    }
}

void expect_true(bool condition, const char* label)
{
    if (!condition) {
        std::printf("FAIL: %s\n", label);
        std::exit(1);
    }
}

bool point_in_bbox(const pcg::internal::data::PcgPoint& p,
                   double min_x,
                   double min_y,
                   double min_z,
                   double max_x,
                   double max_y,
                   double max_z,
                   double eps)
{
    return p.x >= min_x - eps && p.x <= max_x + eps && p.y >= min_y - eps && p.y <= max_y + eps &&
           p.z >= min_z - eps && p.z <= max_z + eps;
}

} // namespace

int main()
{
    using namespace pcg::internal::data;
    using namespace pcg::internal::elements;

    char err[512] = {};
    char out[262144] = {};

    // ── Unit: area-weighted sampling on 2×2×2 box ──
    {
        const PcgMeshData box = create_box_mesh(2.0, 2.0, 2.0);
        SampleMeshSurfaceOptions opts;
        opts.count = 2000;
        opts.seed = 42;

        const PcgPointData points = sample_mesh_surface(box, opts);
        expect_true(points.points().size() == 2000, "sample count");

        int top_face = 0;
        for (const auto& p : points.points()) {
            expect_true(point_in_bbox(p, -1.0, -1.0, -1.0, 1.0, 1.0, 1.0, 1e-4),
                        "point inside mesh bbox");
            expect_true(p.attributes.contains("nx"), "normal attribute nx");
            expect_true(p.attributes.contains("ny"), "normal attribute ny");
            expect_true(p.attributes.contains("nz"), "normal attribute nz");
            expect_true(p.attributes.contains("triIndex"), "triIndex attribute");

            const double ny = p.attributes.value("ny", 0.0);
            if (ny > 0.9 && p.y > 0.5)
                ++top_face;
        }
        expect_true(top_face > 50, "samples on +Y box face");

        const PcgPointData again = sample_mesh_surface(box, opts);
        expect_true(!points.points().empty() && !again.points().empty(), "deterministic non-empty");
        const auto& a0 = points.points().front();
        const auto& b0 = again.points().front();
        expect_true(std::abs(a0.x - b0.x) < 1e-9 && std::abs(a0.y - b0.y) < 1e-9 &&
                        std::abs(a0.z - b0.z) < 1e-9,
                    "deterministic seed");
    }
    std::printf("PASS: sample_mesh_surface unit tests\n");

    // ── Graph: CreateBoxMesh → SampleMeshSurface → Output ──
    const char* scatter_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
         "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
        {"id": "scatter", "type": "SampleMeshSurface", "position": {"x":200,"y":0},
         "data": {"count": 128, "seed": 7, "normalOffset": 0.0, "looseness": 0.0}},
        {"id": "out", "type": "Output", "position": {"x":400,"y":0}, "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "box", "target": "scatter", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e2", "source": "scatter", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    expect_code(pcg_validate_graph(scatter_graph, err, sizeof(err)), PCG_OK, "phase44 validate");
    expect_code(pcg_execute_graph(scatter_graph, 99, out, sizeof(out)), PCG_OK, "phase44 execute");
    expect_true(std::strstr(out, "\"points\"") != nullptr, "json points output");
    expect_true(std::strstr(out, "\"nx\"") != nullptr, "json normal metadata");
    std::printf("PASS: CreateBoxMesh → SampleMeshSurface → Output pipeline\n");

    // ── Graph: subdivide then scatter ──
    const char* subdiv_scatter = R"({
      "version": "1.0",
      "nodes": [
        {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
         "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
        {"id": "sub", "type": "SubdivideMesh", "position": {"x":150,"y":0}, "data": {"levels": 1}},
        {"id": "scatter", "type": "SampleMeshSurface", "position": {"x":300,"y":0},
         "data": {"count": 64, "seed": 3}},
        {"id": "out", "type": "Output", "position": {"x":450,"y":0}, "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "box", "target": "sub", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e2", "source": "sub", "target": "scatter", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e3", "source": "scatter", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    expect_code(pcg_execute_graph(subdiv_scatter, 1, out, sizeof(out)), PCG_OK, "subdivide scatter");
    std::printf("PASS: subdivide + scatter pipeline\n");

    // ── Missing mesh input must error, not crash (4.4e T1) ──
    const char* noise_no_input = R"({
      "version": "1.0",
      "nodes": [
        {"id": "noise", "type": "MeshNoiseDeform", "position": {"x":0,"y":0},
         "data": {"intensity": 0.1, "scale": 2.0}},
        {"id": "out", "type": "Output", "position": {"x":150,"y":0}, "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "noise", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    expect_code(pcg_execute_graph_v2(noise_no_input, 1, nullptr, out, sizeof(out), nullptr, 0,
                                     nullptr, nullptr, err, sizeof(err)),
                PCG_ERR_EXECUTION, "noise missing mesh input");
    expect_true(std::strstr(err, "MeshNoiseDeform missing mesh input") != nullptr,
                "noise missing mesh error message");
    std::printf("PASS: MeshNoiseDeform missing input returns error\n");

    // ── GetMeshData mesh slot (4.4b) ──
    const float box_positions[] = {
        -1, -1, -1, 1, -1, -1, 1, 1, -1, -1, 1, -1,
        -1, -1, 1,  1, -1, 1,  1, 1, 1,  -1, 1, 1,
    };
    const uint32_t box_indices[] = {
        0, 1, 2, 0, 2, 3, 4, 6, 5, 4, 7, 6, 0, 4, 5, 0, 5, 1,
        2, 6, 7, 2, 7, 3, 0, 3, 7, 0, 7, 4, 1, 5, 6, 1, 6, 2,
    };
    const PcgMeshSlot mesh_slot{
        "src",
        8,
        36,
        box_positions,
        box_indices,
    };
    const char* get_mesh_scatter = R"({
      "version": "1.0",
      "nodes": [
        {"id": "src", "type": "GetMeshData", "position": {"x":0,"y":0},
         "data": {"source": "Binding", "bindingKey": "targetMesh", "meshAsset": ""}},
        {"id": "scatter", "type": "SampleMeshSurface", "position": {"x":200,"y":0},
         "data": {"count": 16, "seed": 1}},
        {"id": "out", "type": "Output", "position": {"x":400,"y":0}, "data": {}}
      ],
      "edges": [
        {"id": "e1", "source": "src", "target": "scatter", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e2", "source": "scatter", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";
    int kind = 0;
    expect_code(pcg_execute_graph_v4(get_mesh_scatter, 3, nullptr, 0, &mesh_slot, 1, &kind, out,
                                     sizeof(out), nullptr, 0, nullptr, nullptr, err, sizeof(err)),
                PCG_OK, "get mesh data scatter");
    expect_true(kind == PCG_RESULT_KIND_JSON, "get mesh scatter json kind");
    std::printf("PASS: GetMeshData mesh slot scatter\n");

    std::printf("ALL phase44 tests passed\n");
    return 0;
}
