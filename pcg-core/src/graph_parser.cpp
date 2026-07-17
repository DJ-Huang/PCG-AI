#include "graph_parser.hpp"

#include "elements/pcg_element.hpp"
#include "internal/error_util.hpp"

#include <algorithm>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

bool parse_nodes(const nlohmann::json& array,
                 std::vector<GraphNode>& nodes,
                 std::string& error)
{
    std::unordered_set<std::string> node_ids;
    for (const auto& node_json : array) {
        if (!node_json.is_object()) { error = "Node must be an object"; return false; }
        if (!node_json.contains("id") || !node_json["id"].is_string()) { error = "Node missing id"; return false; }
        if (!node_json.contains("type") || !node_json["type"].is_string()) { error = "Node missing type"; return false; }
        if (!node_json.contains("data")) { error = "Node missing data"; return false; }

        GraphNode node;
        node.id = node_json["id"].get<std::string>();
        node.type = node_json["type"].get<std::string>();
        node.data = node_json["data"];
        if (!node_ids.insert(node.id).second) { error = "Duplicate node id"; return false; }
        nodes.push_back(std::move(node));
    }
    return true;
}

bool parse_edges(const nlohmann::json& array,
                 std::vector<GraphEdge>& edges,
                 std::string& error)
{
    for (const auto& edge_json : array) {
        if (!edge_json.is_object()) { error = "Edge must be an object"; return false; }
        if (!edge_json.contains("source") || !edge_json["source"].is_string()) { error = "Edge missing source"; return false; }
        if (!edge_json.contains("target") || !edge_json["target"].is_string()) { error = "Edge missing target"; return false; }

        GraphEdge edge;
        edge.id = edge_json.value("id", "");
        edge.source = edge_json["source"].get<std::string>();
        edge.target = edge_json["target"].get<std::string>();
        edge.source_handle = edge_json.value("sourceHandle", "out");
        edge.target_handle = edge_json.value("targetHandle", "in");
        edges.push_back(std::move(edge));
    }
    return true;
}

bool parse_ports(const nlohmann::json& array,
                 std::vector<GraphPort>& ports,
                 std::string& error)
{
    std::unordered_set<std::string> ids;
    for (const auto& port_json : array) {
        if (!port_json.is_object() || !port_json.contains("id") || !port_json["id"].is_string()) {
            error = "Subgraph port missing id";
            return false;
        }
        GraphPort port;
        port.id = port_json["id"].get<std::string>();
        port.name = port_json.value("name", port.id);
        port.pin_type = port_json.value("pinType", "Any");
        if (!ids.insert(port.id).second) { error = "Duplicate subgraph port id"; return false; }
        ports.push_back(std::move(port));
    }
    return true;
}

struct Endpoint {
    std::string node;
    std::string handle;
};

struct ExpandedScope {
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::unordered_map<std::string, std::vector<Endpoint>> input_targets;
    std::unordered_map<std::string, std::vector<Endpoint>> output_sources;
};

bool expand_scope(const std::vector<GraphNode>& nodes,
                  const std::vector<GraphEdge>& edges,
                  const std::string& prefix,
                  const std::unordered_map<std::string, const GraphSubgraph*>& definitions,
                  std::vector<std::string>& stack,
                  ExpandedScope& out,
                  std::string& error)
{
    std::unordered_map<std::string, const GraphNode*> by_id;
    std::unordered_map<std::string, ExpandedScope> instances;
    for (const auto& node : nodes) {
        by_id[node.id] = &node;
        if (node.type == "SubgraphInput" || node.type == "SubgraphOutput")
            continue;
        if (node.type != "Subgraph") {
            GraphNode flat = node;
            flat.id = prefix + node.id;
            out.nodes.push_back(std::move(flat));
            continue;
        }

        const std::string subgraph_id = node.data.value("subgraphId", std::string{});
        const auto definition_it = definitions.find(subgraph_id);
        if (subgraph_id.empty() || definition_it == definitions.end()) {
            error = "Subgraph instance references missing definition: " + subgraph_id;
            return false;
        }
        if (std::find(stack.begin(), stack.end(), subgraph_id) != stack.end()) {
            error = "Recursive subgraph reference detected: " + subgraph_id;
            return false;
        }
        stack.push_back(subgraph_id);
        ExpandedScope child;
        const GraphSubgraph& definition = *definition_it->second;
        if (!expand_scope(definition.nodes, definition.edges, prefix + node.id + "/",
                          definitions, stack, child, error))
            return false;
        stack.pop_back();
        out.nodes.insert(out.nodes.end(), child.nodes.begin(), child.nodes.end());
        out.edges.insert(out.edges.end(), child.edges.begin(), child.edges.end());
        instances.emplace(node.id, std::move(child));
    }

    auto source_endpoints = [&](const GraphEdge& edge) -> std::vector<Endpoint> {
        const auto node_it = by_id.find(edge.source);
        if (node_it == by_id.end()) return {};
        if (node_it->second->type == "SubgraphInput") return {};
        if (node_it->second->type == "SubgraphOutput") return {};
        if (node_it->second->type == "Subgraph") {
            const auto instance_it = instances.find(edge.source);
            if (instance_it == instances.end()) return {};
            const auto port_it = instance_it->second.output_sources.find(edge.source_handle);
            return port_it == instance_it->second.output_sources.end() ? std::vector<Endpoint>{} : port_it->second;
        }
        return {{prefix + edge.source, edge.source_handle}};
    };

    auto target_endpoints = [&](const GraphEdge& edge) -> std::vector<Endpoint> {
        const auto node_it = by_id.find(edge.target);
        if (node_it == by_id.end()) return {};
        if (node_it->second->type == "SubgraphInput") return {};
        if (node_it->second->type == "SubgraphOutput") return {};
        if (node_it->second->type == "Subgraph") {
            const auto instance_it = instances.find(edge.target);
            if (instance_it == instances.end()) return {};
            const auto port_it = instance_it->second.input_targets.find(edge.target_handle);
            return port_it == instance_it->second.input_targets.end() ? std::vector<Endpoint>{} : port_it->second;
        }
        return {{prefix + edge.target, edge.target_handle}};
    };

    int edge_counter = 0;
    for (const auto& edge : edges) {
        const auto source_it = by_id.find(edge.source);
        const auto target_it = by_id.find(edge.target);
        if (source_it == by_id.end() || target_it == by_id.end()) {
            error = "Subgraph edge endpoint not found";
            return false;
        }

        const auto sources = source_endpoints(edge);
        const auto targets = target_endpoints(edge);
        if (source_it->second->type == "SubgraphInput") {
            if (targets.empty()) { error = "Subgraph input is not connected to an executable node"; return false; }
            auto& list = out.input_targets[edge.source_handle];
            list.insert(list.end(), targets.begin(), targets.end());
            continue;
        }
        if (target_it->second->type == "SubgraphOutput") {
            if (sources.empty()) { error = "Subgraph output is not connected from an executable node"; return false; }
            auto& list = out.output_sources[edge.target_handle];
            list.insert(list.end(), sources.begin(), sources.end());
            continue;
        }
        if (sources.empty() || targets.empty()) {
            error = "Subgraph edge resolves to an empty interface";
            return false;
        }
        for (const auto& source : sources) {
            for (const auto& target : targets) {
                GraphEdge flat;
                flat.id = prefix + (edge.id.empty() ? "edge" + std::to_string(++edge_counter) : edge.id);
                flat.source = source.node;
                flat.target = target.node;
                flat.source_handle = source.handle;
                flat.target_handle = target.handle;
                out.edges.push_back(std::move(flat));
            }
        }
    }
    return true;
}

bool flatten_subgraphs(Graph& graph, std::string& error)
{
    if (graph.subgraphs.empty())
        return true;
    std::unordered_map<std::string, const GraphSubgraph*> definitions;
    for (const auto& definition : graph.subgraphs) {
        if (!definitions.emplace(definition.id, &definition).second) {
            error = "Duplicate subgraph id";
            return false;
        }
    }
    ExpandedScope flat;
    std::vector<std::string> stack;
    if (!expand_scope(graph.nodes, graph.edges, "", definitions, stack, flat, error))
        return false;
    graph.nodes = std::move(flat.nodes);
    graph.edges = std::move(flat.edges);
    return true;
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

    std::string parse_error;
    if (!parse_nodes(doc["nodes"], graph.nodes, parse_error) ||
        !parse_edges(doc["edges"], graph.edges, parse_error))
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, parse_error.c_str());

    if (doc.contains("subgraphs")) {
        if (!doc["subgraphs"].is_array())
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Invalid subgraphs array");
        for (const auto& subgraph_json : doc["subgraphs"]) {
            if (!subgraph_json.is_object() || !subgraph_json.contains("id") ||
                !subgraph_json["id"].is_string() || !subgraph_json.contains("nodes") ||
                !subgraph_json["nodes"].is_array() || !subgraph_json.contains("edges") ||
                !subgraph_json["edges"].is_array())
                return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Invalid subgraph definition");
            GraphSubgraph subgraph;
            subgraph.id = subgraph_json["id"].get<std::string>();
            subgraph.name = subgraph_json.value("name", subgraph.id);
            const auto inputs = subgraph_json.value("inputs", nlohmann::json::array());
            const auto outputs = subgraph_json.value("outputs", nlohmann::json::array());
            if (!inputs.is_array() || !outputs.is_array() ||
                !parse_ports(inputs, subgraph.inputs, parse_error) ||
                !parse_ports(outputs, subgraph.outputs, parse_error) ||
                !parse_nodes(subgraph_json["nodes"], subgraph.nodes, parse_error) ||
                !parse_edges(subgraph_json["edges"], subgraph.edges, parse_error))
                return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, parse_error.c_str());
            graph.subgraphs.push_back(std::move(subgraph));
        }
    }

    if (!flatten_subgraphs(graph, parse_error))
        return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, parse_error.c_str());

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
        {
            char msg[512];
            std::snprintf(msg, sizeof(msg), "Unknown node type: \"%s\"", node.type.c_str());
            return fail(err_buf, err_buf_size, PCG_ERR_UNKNOWN_NODE, msg);
        }

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
