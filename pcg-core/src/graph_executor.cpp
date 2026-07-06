#include "graph_executor.hpp"

#include "data/pcg_context.hpp"
#include "elements/pcg_element.hpp"
#include "internal/error_util.hpp"
#include "texture_runtime.hpp"

#include <queue>
#include <unordered_map>
#include <vector>

namespace pcg::internal {
namespace {

PcgResultCode fail(char* err_buf, int err_buf_size, PcgResultCode code, const char* message)
{
    write_error(err_buf, err_buf_size, message);
    return code;
}

std::vector<std::string> topological_order(const Graph& graph,
                                           char* err_buf,
                                           int err_buf_size,
                                           PcgResultCode& code)
{
    std::unordered_map<std::string, int> indegree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    for (const auto& node : graph.nodes)
        indegree[node.id] = 0;

    for (const auto& edge : graph.edges) {
        adjacency[edge.source].push_back(edge.target);
        ++indegree[edge.target];
    }

    std::queue<std::string> ready;
    for (const auto& [node_id, degree] : indegree) {
        if (degree == 0)
            ready.push(node_id);
    }

    std::vector<std::string> order;
    order.reserve(graph.nodes.size());
    while (!ready.empty()) {
        const std::string current = ready.front();
        ready.pop();
        order.push_back(current);

        for (const auto& next : adjacency[current]) {
            if (--indegree[next] == 0)
                ready.push(next);
        }
    }

    if (order.size() != graph.nodes.size()) {
        code = fail(err_buf, err_buf_size, PCG_ERR_CYCLE_DETECTED, "Cycle detected during execution");
        return {};
    }

    code = PCG_OK;
    return order;
}

void gather_inputs(const Graph& graph,
                   const std::string& node_id,
                   const NodeOutputMap& outputs,
                   data::PcgDataCollection& inputs,
                   char* err_buf,
                   int err_buf_size,
                   PcgResultCode& code)
{
    for (const auto& edge : graph.edges) {
        if (edge.target != node_id)
            continue;

        const auto it = outputs.find(edge.source);
        if (it == outputs.end()) {
            code = fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Missing upstream output");
            return;
        }

        const std::string pin = edge.target_handle.empty() ? "in" : edge.target_handle;
        const data::PcgDataCollection& upstream = it->second;

        if (const data::PcgMeshData* mesh = upstream.find_mesh("out")) {
            inputs.add_mesh(pin, *mesh);
            continue;
        }
        if (const data::PcgMeshData* mesh = upstream.primary_mesh()) {
            inputs.add_mesh(pin, *mesh);
            continue;
        }

        const nlohmann::json* payload = upstream.find_json("out");
        if (!payload) {
            const nlohmann::json primary = upstream.primary_json();
            if (!primary.is_object() || primary.empty()) {
                code = fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Missing upstream output");
                return;
            }
            inputs.add(pin, data::PcgDataType::Unknown, primary);
            continue;
        }

        inputs.add(pin, data::PcgDataType::Unknown, *payload);
    }

    code = PCG_OK;
}

} // namespace

PcgResultCode execute_graph(const Graph& graph,
                            int seed,
                            GraphExecutionResult& out_result,
                            char* err_buf,
                            int err_buf_size,
                            const TextureRuntime* textures)
{
    elements::register_builtin_elements();

    PcgResultCode topo_code = PCG_OK;
    const auto order = topological_order(graph, err_buf, err_buf_size, topo_code);
    if (topo_code != PCG_OK)
        return topo_code;

    std::unordered_map<std::string, const GraphNode*> node_by_id;
    for (const auto& node : graph.nodes)
        node_by_id[node.id] = &node;

    NodeOutputMap outputs;
    for (const auto& node_id : order) {
        const GraphNode* node = node_by_id[node_id];
        const elements::IPcgElement* element = elements::find_element(node->type);
        if (!element)
            return fail(err_buf, err_buf_size, PCG_ERR_UNKNOWN_NODE, "Unknown node type");

        PcgContext ctx;
        ctx.graph_seed = seed;
        ctx.graph = &graph;
        ctx.node = node;
        ctx.textures = textures;
        ctx.err_buf = err_buf;
        ctx.err_buf_size = err_buf_size;

        PcgResultCode input_code = PCG_OK;
        gather_inputs(graph, node_id, outputs, ctx.inputs, err_buf, err_buf_size, input_code);
        if (input_code != PCG_OK)
            return input_code;

        const PcgResultCode rc = element->execute(ctx);
        if (rc != PCG_OK)
            return rc;

        outputs[node_id] = std::move(ctx.outputs);
    }

    const GraphNode* sink = nullptr;
    const GraphNode* fallback_sink = nullptr;
    for (const auto& node : graph.nodes) {
        bool has_outgoing = false;
        for (const auto& edge : graph.edges) {
            if (edge.source == node.id) {
                has_outgoing = true;
                break;
            }
        }
        if (!has_outgoing) {
            if (node.type == "Output" && !sink)
                sink = &node;
            if (!fallback_sink)
                fallback_sink = &node;
        }
    }

    if (!sink)
        sink = fallback_sink;

    if (!sink)
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Graph has no sink node");

    const auto sink_it = outputs.find(sink->id);
    if (sink_it == outputs.end())
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Sink node produced no output");

    const data::PcgDataCollection& sink_output = sink_it->second;
    if (const data::PcgMeshData* mesh = sink_output.primary_mesh()) {
        out_result.kind = GraphResultKind::Mesh;
        out_result.mesh = *mesh;
        out_result.json = nlohmann::json::object();
        return PCG_OK;
    }

    out_result.kind = GraphResultKind::Json;
    out_result.json = sink_output.primary_json();
    out_result.mesh = data::PcgMeshData{};
    return PCG_OK;
}

} // namespace pcg::internal
