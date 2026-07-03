#include "graph_parser.hpp"

#include "elements/pcg_element.hpp"
#include "internal/error_util.hpp"

#include <set>
#include <unordered_map>
#include <unordered_set>

namespace pcg::internal {
namespace {

bool is_known_node_type(const std::string& type)
{
    return elements::is_known_element_type(type);
}

PcgResultCode fail(char* err_buf, int err_buf_size, PcgResultCode code, const char* message)
{
    write_error(err_buf, err_buf_size, message);
    return code;
}

} // namespace

PcgResultCode parse_graph(const char* json,
                          Graph& out_graph,
                          char* err_buf,
                          int err_buf_size)
{
    if (!json)
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Graph JSON is null");

    nlohmann::json doc;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const nlohmann::json::exception& ex) {
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, ex.what());
    }

    if (!doc.is_object())
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Graph root must be an object");

    if (!doc.contains("version") || !doc["version"].is_string())
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Missing or invalid version");

    if (!doc.contains("nodes") || !doc["nodes"].is_array())
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Missing or invalid nodes array");

    if (!doc.contains("edges") || !doc["edges"].is_array())
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Missing or invalid edges array");

    Graph graph;
    graph.version = doc["version"].get<std::string>();

    std::unordered_set<std::string> node_ids;
    for (const auto& node_json : doc["nodes"]) {
        if (!node_json.is_object())
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Node must be an object");

        if (!node_json.contains("id") || !node_json["id"].is_string())
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Node missing id");

        if (!node_json.contains("type") || !node_json["type"].is_string())
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Node missing type");

        if (!node_json.contains("data"))
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Node missing data");

        GraphNode node;
        node.id = node_json["id"].get<std::string>();
        node.type = node_json["type"].get<std::string>();
        node.data = node_json["data"];

        if (!node_ids.insert(node.id).second)
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Duplicate node id");

        graph.nodes.push_back(std::move(node));
    }

    for (const auto& edge_json : doc["edges"]) {
        if (!edge_json.is_object())
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Edge must be an object");

        if (!edge_json.contains("source") || !edge_json["source"].is_string())
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Edge missing source");

        if (!edge_json.contains("target") || !edge_json["target"].is_string())
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Edge missing target");

        GraphEdge edge;
        edge.id = edge_json.value("id", "");
        edge.source = edge_json["source"].get<std::string>();
        edge.target = edge_json["target"].get<std::string>();
        edge.source_handle = edge_json.value("sourceHandle", "out");
        edge.target_handle = edge_json.value("targetHandle", "in");
        graph.edges.push_back(std::move(edge));
    }

    out_graph = std::move(graph);
    return PCG_OK;
}

PcgResultCode validate_graph_structure(const Graph& graph,
                                       char* err_buf,
                                       int err_buf_size)
{
    if (graph.version != "1.0")
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Unsupported graph version");

    if (graph.nodes.empty())
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Graph has no nodes");

    std::unordered_map<std::string, const GraphNode*> node_by_id;
    for (const auto& node : graph.nodes) {
        if (!is_known_node_type(node.type))
            return fail(err_buf, err_buf_size, PCG_ERR_UNKNOWN_NODE, "Unknown node type");

        node_by_id[node.id] = &node;
    }

    std::unordered_map<std::string, int> indegree;
    std::unordered_map<std::string, std::vector<std::string>> adjacency;
    for (const auto& node : graph.nodes)
        indegree[node.id] = 0;

    for (const auto& edge : graph.edges) {
        if (!node_by_id.count(edge.source))
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Edge source not found");

        if (!node_by_id.count(edge.target))
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Edge target not found");

        if (edge.source == edge.target)
            return fail(err_buf, err_buf_size, PCG_ERR_CYCLE_DETECTED, "Self-loop detected");

        adjacency[edge.source].push_back(edge.target);
        ++indegree[edge.target];
    }

    std::vector<std::string> queue;
    for (const auto& [node_id, degree] : indegree) {
        if (degree == 0)
            queue.push_back(node_id);
    }

    size_t visited = 0;
    while (!queue.empty()) {
        const std::string current = queue.back();
        queue.pop_back();
        ++visited;

        for (const auto& next : adjacency[current]) {
            if (--indegree[next] == 0)
                queue.push_back(next);
        }
    }

    if (visited != graph.nodes.size())
        return fail(err_buf, err_buf_size, PCG_ERR_CYCLE_DETECTED, "Cycle detected in graph");

    return PCG_OK;
}

} // namespace pcg::internal
