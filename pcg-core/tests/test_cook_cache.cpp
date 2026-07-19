#include "pcg_api.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

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

PcgResultCode run_v5(const char* json, PcgCookStats* stats, char* out, int out_size)
{
    char err[512] = {};
    static unsigned char mesh[1024 * 1024] = {};
    int kind = PCG_RESULT_KIND_NONE;
    int vertex_count = 0;
    int index_count = 0;
    return pcg_execute_graph_v5(json, 42, nullptr, 0, nullptr, 0, &kind, out, out_size,
                                mesh, sizeof(mesh), &vertex_count, &index_count, stats, err, sizeof(err));
}

} // namespace

int main()
{
    char out[262144] = {};
    PcgCookStats stats{};

    const char* pipeline = R"({
        "version": "1.0",
        "nodes": [
            {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
             "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
            {"id": "sub", "type": "SubdivideMesh", "position": {"x":200,"y":0},
             "data": {"levels": 1}},
            {"id": "sample", "type": "SampleMeshSurface", "position": {"x":400,"y":0},
             "data": {"count": 32, "seed": 7}},
            {"id": "out", "type": "Output", "position": {"x":600,"y":0}, "data": {}}
        ],
        "edges": [
            {"id": "e1", "source": "box", "target": "sub"},
            {"id": "e2", "source": "sub", "target": "sample"},
            {"id": "e3", "source": "sample", "target": "out"}
        ]
    })";

    pcg_cook_cache_clear();
    expect_code(run_v5(pipeline, &stats, out, sizeof(out)), PCG_OK, "first cook");
    expect_true(stats.nodes_executed == 4, "first cook executes all nodes");
    expect_true(stats.nodes_skipped == 0, "first cook skips none");

    expect_code(run_v5(pipeline, &stats, out, sizeof(out)), PCG_OK, "second cook same graph");
    expect_true(stats.nodes_skipped == 4, "second cook skips all nodes");
    expect_true(stats.nodes_executed == 0, "second cook executes none");

    const char* changed_downstream = R"({
        "version": "1.0",
        "nodes": [
            {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
             "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
            {"id": "sub", "type": "SubdivideMesh", "position": {"x":200,"y":0},
             "data": {"levels": 2}},
            {"id": "sample", "type": "SampleMeshSurface", "position": {"x":400,"y":0},
             "data": {"count": 32, "seed": 7}},
            {"id": "out", "type": "Output", "position": {"x":600,"y":0}, "data": {}}
        ],
        "edges": [
            {"id": "e1", "source": "box", "target": "sub"},
            {"id": "e2", "source": "sub", "target": "sample"},
            {"id": "e3", "source": "sample", "target": "out"}
        ]
    })";

    expect_code(run_v5(changed_downstream, &stats, out, sizeof(out)), PCG_OK, "changed subdiv levels");
    expect_true(stats.nodes_skipped == 1, "box cache hit after downstream param change");
    expect_true(stats.nodes_executed == 3, "sub/sample/out recomputed");

    const char* full_preview_source = R"({
        "version": "1.0",
        "nodes": [
            {"id": "box", "type": "CreateBoxMesh", "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
            {"id": "sub", "type": "SubdivideMesh", "data": {"levels": 1}},
            {"id": "out", "type": "Output", "data": {}}
        ],
        "edges": [
            {"id": "e1", "source": "box", "target": "sub", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e2", "source": "sub", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
        ]
    })";

    const char* changed_preview_sink = R"({
        "version": "1.0",
        "nodes": [
            {"id": "box", "type": "CreateBoxMesh", "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
            {"id": "sub", "type": "SubdivideMesh", "data": {"levels": 1}},
            {"id": "__pcg_preview_sink__", "type": "Output", "data": {}}
        ],
        "edges": [
            {"id": "e1", "source": "box", "target": "sub", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "__pcg_preview_sink___edge", "source": "sub", "target": "__pcg_preview_sink__", "sourceHandle": "out", "targetHandle": "in"}
        ]
    })";

    pcg_cook_cache_clear();
    expect_code(run_v5(full_preview_source, &stats, out, sizeof(out)), PCG_OK,
                "full graph before preview sink change");
    expect_true(stats.nodes_executed == 3, "full graph cold cook executes all nodes");

    expect_code(run_v5(changed_preview_sink, &stats, out, sizeof(out)), PCG_OK,
                "preview sink topology change");
    expect_true(stats.nodes_skipped == 2,
                "preview sink change reuses unchanged expensive upstream nodes");
    expect_true(stats.nodes_executed == 1,
                "preview sink change executes only the synthetic output node");

    std::printf("PASS: per-node cook cache\n");
    return 0;
}
