#include "pcg_api.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition) ++failures;
}

struct Result {
    PcgResultCode code = PCG_OK;
    int kind = 0;
    int vertices = 0;
    int indices = 0;
    PcgCookStats stats{};
    nlohmann::json perf = nlohmann::json::array();
    std::string error;
};

Result execute(const char* graph)
{
    std::vector<char> json(4 * 1024 * 1024);
    std::vector<unsigned char> mesh(16 * 1024 * 1024);
    std::vector<char> perf(256 * 1024);
    char error[1024] = {};
    Result result;
    result.code = pcg_execute_graph_v7(
        graph, 42, nullptr, 0, nullptr, 0, nullptr, 0,
        &result.kind, json.data(), static_cast<int>(json.size()),
        mesh.data(), static_cast<int>(mesh.size()), nullptr, 0,
        nullptr, nullptr, &result.vertices, &result.indices,
        &result.stats, perf.data(), static_cast<int>(perf.size()), error, sizeof(error));
    if (perf[0] != '\0')
        result.perf = nlohmann::json::parse(perf.data());
    result.error = error;
    return result;
}

} // namespace

int main()
{
    const char* loft_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"p0","type":"CreateBezierSpline","data":{"closed":false,"subdivisions":6,"controlPoints":"[{\"x\":-2.0,\"y\":0.3,\"z\":0},{\"x\":-2.0,\"y\":0.2,\"z\":0.35},{\"x\":-2.0,\"y\":0.7,\"z\":0.65},{\"x\":-2.0,\"y\":0.9,\"z\":0.65},{\"x\":-2.0,\"y\":1.1,\"z\":0.65},{\"x\":-2.0,\"y\":1.2,\"z\":0.25},{\"x\":-2.0,\"y\":1.2,\"z\":0}]"}},
        {"id":"p1","type":"CreateBezierSpline","data":{"closed":false,"subdivisions":6,"controlPoints":"[{\"x\":-0.7,\"y\":0.2,\"z\":0},{\"x\":-0.7,\"y\":0.15,\"z\":0.5},{\"x\":-0.7,\"y\":0.8,\"z\":0.9},{\"x\":-0.7,\"y\":1.0,\"z\":0.9},{\"x\":-0.7,\"y\":1.35,\"z\":0.85},{\"x\":-0.7,\"y\":1.45,\"z\":0.3},{\"x\":-0.7,\"y\":1.45,\"z\":0}]"}},
        {"id":"p2","type":"CreateBezierSpline","data":{"closed":false,"subdivisions":6,"controlPoints":"[{\"x\":0.8,\"y\":0.2,\"z\":0},{\"x\":0.8,\"y\":0.15,\"z\":0.5},{\"x\":0.8,\"y\":0.75,\"z\":0.9},{\"x\":0.8,\"y\":0.95,\"z\":0.9},{\"x\":0.8,\"y\":1.3,\"z\":0.85},{\"x\":0.8,\"y\":1.4,\"z\":0.3},{\"x\":0.8,\"y\":1.4,\"z\":0}]"}},
        {"id":"p3","type":"CreateBezierSpline","data":{"closed":false,"subdivisions":6,"controlPoints":"[{\"x\":2.0,\"y\":0.3,\"z\":0},{\"x\":2.0,\"y\":0.2,\"z\":0.35},{\"x\":2.0,\"y\":0.65,\"z\":0.65},{\"x\":2.0,\"y\":0.85,\"z\":0.65},{\"x\":2.0,\"y\":1.05,\"z\":0.6},{\"x\":2.0,\"y\":1.1,\"z\":0.2},{\"x\":2.0,\"y\":1.1,\"z\":0}]"}},
        {"id":"loft","type":"LoftMesh","data":{"columns":20,"sortAxis":"x","closedProfile":false,"capStart":false,"capEnd":false,"autoAlign":true}},
        {"id":"mirror","type":"MirrorMesh","data":{"axis":"z","offset":0,"mergeOriginal":true,"weldSeam":true,"weldTolerance":0.0001}},
        {"id":"fuse","type":"FuseMesh","data":{"tolerance":0.0001,"removeDegenerate":true}},
        {"id":"shell","type":"ShellMesh","data":{"thickness":0.02,"direction":"inward","closeBoundaries":true}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e0","source":"p0","target":"loft","sourceHandle":"out","targetHandle":"profiles"},
        {"id":"e1","source":"p1","target":"loft","sourceHandle":"out","targetHandle":"profiles"},
        {"id":"e2","source":"p2","target":"loft","sourceHandle":"out","targetHandle":"profiles"},
        {"id":"e3","source":"p3","target":"loft","sourceHandle":"out","targetHandle":"profiles"},
        {"id":"e4","source":"loft","target":"mirror","sourceHandle":"out","targetHandle":"in"},
        {"id":"e5","source":"mirror","target":"fuse","sourceHandle":"out","targetHandle":"in"},
        {"id":"e6","source":"fuse","target":"shell","sourceHandle":"out","targetHandle":"in"},
        {"id":"e7","source":"shell","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    const auto loft = execute(loft_graph);
    expect(loft.code == PCG_OK, loft.error.empty() ? "Bezier -> Loft -> Mirror -> Fuse -> Shell executes" : loft.error.c_str());
    expect(loft.kind == PCG_RESULT_KIND_MESH, "loft graph returns mesh");
    expect(loft.vertices > 100 && loft.indices > 300, "loft graph produces a smooth multi-section body");

    const char* detail_graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"box","type":"CreateBoxMesh","data":{"sizeX":0.1,"sizeY":0.5,"sizeZ":0.08,"centerX":0,"centerY":0.65,"centerZ":0}},
        {"id":"extrude","type":"PolyExtrude","data":{"faceGroup":"","distance":0.01,"inset":0.01,"keepOriginal":false}},
        {"id":"copy","type":"CopyMesh","data":{"mode":"circular","count":8,"axis":"x","angle":360,"centerX":0,"centerY":0,"centerZ":0}},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e0","source":"box","target":"extrude","sourceHandle":"out","targetHandle":"in"},
        {"id":"e1","source":"extrude","target":"copy","sourceHandle":"out","targetHandle":"in"},
        {"id":"e2","source":"copy","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";
    const auto detail = execute(detail_graph);
    expect(detail.code == PCG_OK, detail.error.empty() ? "PolyExtrude -> CopyMesh executes" : detail.error.c_str());
    expect(detail.vertices > 0 && detail.indices > 0, "detail graph produces geometry");

    std::ifstream sedan_file("../../examples/realistic-sedan.pcg");
    std::stringstream sedan_json;
    sedan_json << sedan_file.rdbuf();
    expect(sedan_file.good() || sedan_file.eof(), "realistic sedan fixture is readable");
    pcg_cook_cache_clear();
    const auto cold_start = std::chrono::steady_clock::now();
    const auto sedan = execute(sedan_json.str().c_str());
    const auto cold_end = std::chrono::steady_clock::now();
    expect(sedan.code == PCG_OK,
           sedan.error.empty() ? "realistic sedan graph executes end to end" : sedan.error.c_str());
    expect(sedan.kind == PCG_RESULT_KIND_MESH, "realistic sedan returns a Unity mesh");
    expect(sedan.vertices > 1000 && sedan.indices > 3000,
           "realistic sedan contains production-scale body and detail geometry");

    auto preview_doc = nlohmann::json::parse(sedan_json.str());
    std::string output_id;
    for (const auto& node : preview_doc["nodes"]) {
        if (node.value("type", "") == "Output") {
            output_id = node.value("id", "");
            break;
        }
    }
    std::string preview_source_id;
    for (const auto& edge : preview_doc["edges"]) {
        if (edge.value("target", "") == output_id) {
            preview_source_id = edge.value("source", "");
            break;
        }
    }
    expect(!output_id.empty() && !preview_source_id.empty(),
           "realistic sedan output edge is available for node preview");

    auto& preview_nodes = preview_doc["nodes"];
    preview_nodes.erase(
        std::remove_if(preview_nodes.begin(), preview_nodes.end(),
                       [&output_id](const auto& node) { return node.value("id", "") == output_id; }),
        preview_nodes.end());
    auto& preview_edges = preview_doc["edges"];
    preview_edges.erase(
        std::remove_if(preview_edges.begin(), preview_edges.end(),
                       [&output_id](const auto& edge) { return edge.value("target", "") == output_id; }),
        preview_edges.end());
    preview_nodes.push_back({
        {"id", "__pcg_preview_sink__"},
        {"type", "Output"},
        {"data", nlohmann::json::object()},
    });
    preview_edges.push_back({
        {"id", "__pcg_preview_sink___edge"},
        {"source", preview_source_id},
        {"target", "__pcg_preview_sink__"},
        {"sourceHandle", "out"},
        {"targetHandle", "in"},
    });

    const std::string preview_json = preview_doc.dump();
    const auto preview_start = std::chrono::steady_clock::now();
    const auto preview = execute(preview_json.c_str());
    const auto preview_end = std::chrono::steady_clock::now();
    expect(preview.code == PCG_OK,
           preview.error.empty() ? "realistic sedan final MergeMesh preview executes" : preview.error.c_str());
    expect(preview.stats.nodes_executed == 1,
           "final MergeMesh preview executes only the synthetic output node");
    expect(preview.stats.nodes_skipped + preview.stats.nodes_executed == sedan.stats.nodes_executed,
           "final MergeMesh preview reuses all unchanged upstream nodes");

    const double cold_ms =
        std::chrono::duration<double, std::milli>(cold_end - cold_start).count();
    const double preview_ms =
        std::chrono::duration<double, std::milli>(preview_end - preview_start).count();
    std::printf("realistic sedan cold cook: %.2f ms; cached final MergeMesh preview: %.2f ms\n",
                cold_ms, preview_ms);
    auto slow_nodes = sedan.perf;
    std::sort(slow_nodes.begin(), slow_nodes.end(), [](const auto& a, const auto& b) {
        return a.value("ms", 0.0) > b.value("ms", 0.0);
    });
    std::printf("slowest realistic sedan nodes:\n");
    for (size_t i = 0; i < std::min<size_t>(10, slow_nodes.size()); ++i) {
        const auto& node = slow_nodes[i];
        std::printf("  %s (%s): %.2f ms\n",
                    node.value("id", "").c_str(), node.value("type", "").c_str(),
                    node.value("ms", 0.0));
    }

    return failures == 0 ? 0 : 1;
}
