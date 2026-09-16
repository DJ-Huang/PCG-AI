// Boolean outcomes must survive node execution, graph assembly, and cache hits.
#include "pcg_api.h"
#include "graph_executor.hpp"
#include "graph_parser.hpp"
#include "cook_diagnostics.hpp"
#include "data/pcg_context.hpp"
#include "elements/boolean_elements.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/pcg_element.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace pcg::internal;
using nlohmann::json;

namespace {
void expect(bool value, const char* label)
{
    if (!value) { std::printf("FAIL: %s\n", label); std::exit(1); }
}

json fixture(const char* operation = "subtract", const char* policy = "error", int budget = 500000)
{
    auto graph = json::parse(R"({"version":"1.0","nodes":[
        {"id":"a","type":"CreateBoxMesh","data":{"width":4,"height":4,"depth":4}},
        {"id":"b","type":"CreateBoxMesh","data":{"width":2,"height":2,"depth":2}},
        {"id":"bool","type":"BooleanMesh","data":{}},
        {"id":"out","type":"Output","data":{}}],
        "edges":[
        {"id":"ea","source":"a","target":"bool","targetHandle":"a"},
        {"id":"eb","source":"b","target":"bool","targetHandle":"b"},
        {"id":"eo","source":"bool","target":"out"}]})");
    graph["nodes"][2]["data"] = {{"operation", operation}, {"onFailure", policy}, {"triangleBudget", budget}};
    for (auto& node : graph["nodes"]) node["position"] = {{"x", 0}, {"y", 0}};
    return graph;
}

PcgResultCode run(const json& input, GraphExecutionResult& result, GraphCookCache* cache = nullptr)
{
    Graph graph;
    char error[1024] = {};
    expect(parse_graph(input.dump().c_str(), graph, error, sizeof(error)) == PCG_OK, "parse fixture");
    const auto code = execute_graph(graph, 42, result, error, sizeof(error),
                                   nullptr, nullptr, nullptr, nullptr, cache);
    if (code != PCG_OK) expect(std::string(error).find("bool") != std::string::npos, "error identifies node");
    return code;
}

void test_graph_outcomes_and_cache()
{
    GraphExecutionResult result;
    GraphCookCache cache;
    auto graph = fixture();
    expect(run(graph, result, &cache) == PCG_OK, "subtract succeeds");
    expect(result.json["cook_outcome"] == "success", "successful cook outcome");
    expect(result.json["node_diagnostics"].size() == 1, "one Boolean receipt");
    expect(result.json["evaluation_seed"] == 42, "native seed receipt");
    expect(result.json["evaluation_graph"]["nodes"].size() == 4, "native input snapshot");
    expect(run(graph, result, &cache) == PCG_OK, "cached subtract succeeds");
    expect(result.json["node_diagnostics"][0]["cache_hit"] == true, "cache retains diagnostic");
    graph = fixture("subtract", "passthroughA", 1);
    for (int i = 0; i < 2; ++i) {
        expect(run(graph, result, &cache) == PCG_OK, "explicit fallback keeps preview alive");
        expect(result.json["cook_outcome"] == "degraded", "fallback is not success");
        expect(result.json["execution_acceptable"] == false, "fallback blocks acceptance");
        expect(result.json["node_diagnostics"][0]["cache_hit"] == false, "fallback never cached");
        expect(result.json["node_diagnostics"][0]["effective_settings"]["triangleBudget"] == 1, "effective limits recorded");
    }
    expect(run(fixture("subtract", "error", 1), result, &cache) != PCG_OK, "strict policy fails");
    expect(result.json["cook_outcome"] == "failure", "hard failure has structured outcome");
    expect(result.mesh.vertices().empty(), "failure clears previous result mesh");
    expect(run(fixture(), result, &cache) == PCG_OK, "clean cook after failed/degraded cook");
    expect(result.json["fallback_used"] == false, "degradation does not leak into later cooks");
    expect(run(fixture("union"), result) == PCG_OK, "normal union still succeeds");
}

void test_native_abi_json()
{
    for (const char* operation : {"subtract", "union"}) {
        auto graph = fixture(operation, "passthroughA", 1);
        std::vector<char> json_out(8 * 1024 * 1024);
        std::vector<uint8_t> mesh(8 * 1024 * 1024);
        char error[1024] = {};
        int kind = 0, vertices = 0, indices = 0;
        const auto rc = pcg_execute_graph_v7(graph.dump().c_str(), 42,
            nullptr, 0, nullptr, 0, nullptr, 0, &kind,
            json_out.data(), static_cast<int>(json_out.size()),
            mesh.data(), static_cast<int>(mesh.size()), nullptr, 0,
            nullptr, nullptr, &vertices, &indices, nullptr, nullptr, 0, error, sizeof(error));
        expect(rc == PCG_OK && kind == PCG_RESULT_KIND_MESH, "C ABI exports fallback mesh");
        const auto receipt = json::parse(json_out.data());
        expect(receipt["cook_outcome"] == "degraded", "C ABI mesh JSON retains diagnostic");
    }
}

bool cancel_now() { return true; }

void test_empty_noop_and_cancellation()
{
    elements::register_builtin_elements();
    const auto* element = elements::find_element("BooleanMesh");
    expect(element != nullptr, "BooleanMesh registered");
    auto a = data::geometry_from_mesh(elements::create_box_mesh(2, 2, 2));
    auto b = a;
    for (auto& point : b.points_mut()) point.x += 10;
    GraphNode node{"bool", "BooleanMesh", {{"operation", "intersect"}, {"onFailure", "passthroughA"}}};
    auto evaluate = [&](const data::PcgGeometry& lhs, const data::PcgGeometry& rhs, bool cancel) {
        PcgContext ctx;
        char error[1024] = {};
        ctx.node = &node; ctx.err_buf = error; ctx.err_buf_size = sizeof(error);
        ctx.is_cancel_requested = cancel ? cancel_now : nullptr;
        ctx.inputs.add_geometry("a", lhs); ctx.inputs.add_geometry("b", rhs);
        const auto rc = element->execute(ctx);
        expect(cancel ? rc != PCG_OK : rc == PCG_OK, "node result code");
        return ctx.outputs;
    };
    const auto empty = evaluate(a, b, false);
    expect(empty.find_geometry("out") && empty.find_geometry("out")->faces().empty(), "valid empty not replaced with A");
    expect((*empty.find_json(kNodeDiagnosticTag))["outcome"] == "empty", "empty distinguished from degraded");
    node.data["operation"] = "subtract";
    auto no_op = evaluate(a, b, false);
    expect((*no_op.find_json(kNodeDiagnosticTag))["outcome"] == "noop", "disjoint subtract is conservative no-op");
    auto chained = evaluate(*empty.find_geometry("out"), b, false);
    expect(chained.find_geometry("out")->faces().empty(), "typed empty remains composable");
    const auto cancelled = evaluate(a, b, true);
    expect(cancelled.find_geometry("out") == nullptr, "cancellation never returns A");
    expect((*cancelled.find_json(kNodeDiagnosticTag))["outcome"] == "failure", "cancel has failure outcome");
}

void test_foreach_preserves_every_failure()
{
    auto graph = fixture("subtract", "passthroughA", 1);
    graph["nodes"].push_back({{"id", "begin"}, {"type", "ForEachBegin"},
        {"position", {{"x", 0}, {"y", 0}}}, {"data", {{"method", "count"}, {"iterations", 2}}}});
    graph["nodes"].push_back({{"id", "end"}, {"type", "ForEachEnd"},
        {"position", {{"x", 0}, {"y", 0}}}, {"data", {{"gatherMethod", "merge"}}}});
    graph["edges"][0]["target"] = "begin";
    graph["edges"][0]["targetHandle"] = "in";
    graph["edges"][2]["target"] = "end";
    graph["edges"].push_back({{"id", "ebegin"}, {"source", "begin"}, {"target", "bool"}, {"targetHandle", "a"}});
    graph["edges"].push_back({{"id", "eend"}, {"source", "end"}, {"target", "out"}});
    GraphExecutionResult result;
    expect(run(graph, result) == PCG_OK, "loop preview completes with explicit fallback");
    expect(result.json["node_diagnostics"].size() == 2, "each loop invocation has a receipt");
    expect(result.json["cook_outcome"] == "degraded", "loop fallback reaches final cook");
}
} // namespace

int main()
{
    test_graph_outcomes_and_cache();
    test_native_abi_json();
    test_empty_noop_and_cancellation();
    test_foreach_preserves_every_failure();
    std::puts("test_boolean_graph: all tests passed");
    return 0;
}
