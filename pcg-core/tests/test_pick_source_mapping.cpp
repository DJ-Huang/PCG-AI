#include "pcg_api.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

void expect_true(bool condition, const char* label)
{
    if (!condition) {
        std::printf("FAIL: %s\n", label);
        std::exit(1);
    }
}

struct CookOutput {
    std::string json;
    int kind = PCG_RESULT_KIND_NONE;
    int vertex_count = 0;
    int index_count = 0;
};

PcgResultCode run_preview_cook(const char* graph_json, CookOutput& output)
{
    char err[512] = {};
    static char json_buf[8 * 1024 * 1024] = {};
    static unsigned char mesh[16 * 1024 * 1024] = {};
    PcgCookStats stats{};
    const PcgResultCode code = pcg_execute_graph_v5(
        graph_json, 42, nullptr, 0, nullptr, 0, &output.kind, json_buf, sizeof(json_buf),
        mesh, sizeof(mesh), &output.vertex_count, &output.index_count, &stats, err, sizeof(err));
    if (code != PCG_OK)
        std::printf("cook error: %s\n", err);
    output.json = json_buf;
    return code;
}

int source_index_of(const nlohmann::json& source_nodes, const std::string& id)
{
    for (size_t i = 0; i < source_nodes.size(); ++i) {
        if (source_nodes[i].get<std::string>() == id)
            return static_cast<int>(i);
    }
    return -1;
}

} // namespace

int main()
{
    pcg_cook_cache_clear();

    // Two boxes merged into a preview sink: triangles must attribute back to
    // the box node that created each face, in PCGM triangle order.
    const char* merged = R"({
        "version": "1.0",
        "nodes": [
            {"id": "boxA", "type": "CreateBoxMesh", "data": {"width": 1.0, "height": 1.0, "depth": 1.0}},
            {"id": "boxB", "type": "CreateBoxMesh", "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
            {"id": "merge", "type": "MergeMesh", "data": {}},
            {"id": "__pcg_preview_sink__", "type": "Output", "data": {}}
        ],
        "edges": [
            {"id": "e1", "source": "boxA", "target": "merge", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e2", "source": "boxB", "target": "merge", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e3", "source": "merge", "target": "__pcg_preview_sink__", "sourceHandle": "out", "targetHandle": "in"}
        ]
    })";

    CookOutput out;
    expect_true(run_preview_cook(merged, out) == PCG_OK, "merged preview cook ok");
    expect_true(out.kind == PCG_RESULT_KIND_MESH, "merged cook produces mesh");

    const auto json = nlohmann::json::parse(out.json);
    expect_true(json.contains("source_nodes"), "json has source_nodes");
    expect_true(json.contains("triangle_sources"), "json has triangle_sources");

    const auto& source_nodes = json["source_nodes"];
    const auto& triangle_sources = json["triangle_sources"];
    expect_true(source_nodes.size() == 2, "two attributed source nodes");
    const int idx_a = source_index_of(source_nodes, "boxA");
    const int idx_b = source_index_of(source_nodes, "boxB");
    expect_true(idx_a >= 0, "boxA in source_nodes");
    expect_true(idx_b >= 0, "boxB in source_nodes");

    const int triangle_count = out.index_count / 3;
    expect_true(static_cast<int>(triangle_sources.size()) == triangle_count,
                "triangle_sources aligned with PCGM triangle count");

    // Box = 6 quad faces = 12 triangles; merge concatenates in edge order.
    expect_true(triangle_count == 24, "two boxes -> 24 triangles");
    for (int t = 0; t < 12; ++t)
        expect_true(triangle_sources[t].get<int>() == idx_a, "first 12 triangles -> boxA");
    for (int t = 12; t < 24; ++t)
        expect_true(triangle_sources[t].get<int>() == idx_b, "last 12 triangles -> boxB");

    // Second cook hits the node cache: attribution must stay identical.
    CookOutput cached;
    expect_true(run_preview_cook(merged, cached) == PCG_OK, "cached preview cook ok");
    const auto cached_json = nlohmann::json::parse(cached.json);
    expect_true(cached_json["triangle_sources"] == triangle_sources,
                "cache-hit cook keeps identical triangle_sources");

    // Transform preserves upstream attribution: every face still maps to box.
    const char* transformed = R"({
        "version": "1.0",
        "nodes": [
            {"id": "box", "type": "CreateBoxMesh", "data": {"width": 1.0, "height": 1.0, "depth": 1.0}},
            {"id": "move", "type": "TransformMesh", "data": {"translateY": 5.0}},
            {"id": "__pcg_preview_sink__", "type": "Output", "data": {}}
        ],
        "edges": [
            {"id": "e1", "source": "box", "target": "move", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e2", "source": "move", "target": "__pcg_preview_sink__", "sourceHandle": "out", "targetHandle": "in"}
        ]
    })";

    expect_true(run_preview_cook(transformed, out) == PCG_OK, "transform preview cook ok");
    const auto tjson = nlohmann::json::parse(out.json);
    const auto& t_sources = tjson["triangle_sources"];
    const int box_idx = source_index_of(tjson["source_nodes"], "box");
    expect_true(box_idx >= 0, "box attributed through transform");
    expect_true(!t_sources.empty(), "transform cook has triangle_sources");
    for (const auto& entry : t_sources)
        expect_true(entry.get<int>() == box_idx, "all triangles attribute to box");

    // No preview sink -> no attribution payload (production cooks stay clean).
    const char* production = R"({
        "version": "1.0",
        "nodes": [
            {"id": "box", "type": "CreateBoxMesh", "data": {"width": 1.0, "height": 1.0, "depth": 1.0}},
            {"id": "out", "type": "Output", "data": {}}
        ],
        "edges": [
            {"id": "e1", "source": "box", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
        ]
    })";

    expect_true(run_preview_cook(production, out) == PCG_OK, "production cook ok");
    const auto pjson = nlohmann::json::parse(out.json);
    expect_true(!pjson.contains("source_nodes"), "production cook omits source_nodes");
    expect_true(!pjson.contains("triangle_sources"), "production cook omits triangle_sources");

    std::printf("PASS\n");
    return 0;
}
