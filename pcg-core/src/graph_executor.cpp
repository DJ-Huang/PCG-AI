#include "graph_executor.hpp"

#include "blocks/blocks.hpp"
#include "internal/error_util.hpp"

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

const nlohmann::json* find_single_input(const Graph& graph,
                                        const std::string& node_id,
                                        const NodeOutputMap& outputs)
{
    const GraphNode* node = nullptr;
    for (const auto& candidate : graph.nodes) {
        if (candidate.id == node_id) {
            node = &candidate;
            break;
        }
    }
    if (!node)
        return nullptr;

    const nlohmann::json* input = nullptr;
    for (const auto& edge : graph.edges) {
        if (edge.target != node_id)
            continue;

        const auto it = outputs.find(edge.source);
        if (it == outputs.end())
            return nullptr;

        if (input != nullptr)
            return nullptr;

        input = &it->second;
    }

    return input;
}

} // namespace

PcgResultCode execute_graph(const Graph& graph,
                            int seed,
                            nlohmann::json& out_result,
                            char* err_buf,
                            int err_buf_size)
{
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
        nlohmann::json block_output;
        PcgResultCode rc = PCG_OK;

        if (node->type == "ParseConfig") {
            rc = blocks::execute_parse_config(*node, seed, block_output, err_buf, err_buf_size);
        } else if (node->type == "SpawnPoints") {
            const nlohmann::json* config = find_single_input(graph, node_id, outputs);
            if (!config)
                return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "SpawnPoints missing config input");

            rc = blocks::execute_spawn_points(*node, *config, seed, block_output, err_buf, err_buf_size);
        } else if (node->type == "PlaceInScene") {
            const nlohmann::json* points_payload = find_single_input(graph, node_id, outputs);
            if (!points_payload)
                return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "PlaceInScene missing points input");

            rc = blocks::execute_place_in_scene(*node, *points_payload, block_output, err_buf, err_buf_size);
        } else {
            return fail(err_buf, err_buf_size, PCG_ERR_UNKNOWN_NODE, "Unknown node type");
        }

        if (rc != PCG_OK)
            return rc;

        outputs[node_id] = std::move(block_output);
    }

    const GraphNode* sink = nullptr;
    for (const auto& node : graph.nodes) {
        bool has_outgoing = false;
        for (const auto& edge : graph.edges) {
            if (edge.source == node.id) {
                has_outgoing = true;
                break;
            }
        }
        if (!has_outgoing)
            sink = &node;
    }

    if (!sink)
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Graph has no sink node");

    const auto sink_it = outputs.find(sink->id);
    if (sink_it == outputs.end())
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "Sink node produced no output");

    out_result = sink_it->second;
    return PCG_OK;
}

} // namespace pcg::internal
