#include "graph_parser.hpp"

#include "elements/pcg_element.hpp"
#include "elements/node_contracts.hpp"
#include "internal/error_util.hpp"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <tuple>
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
        edge.source_pin_type = edge_json.value("sourcePinType", "");
        edge.target_pin_type = edge_json.value("targetPinType", "");
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

bool parse_parameters(const nlohmann::json& array,
                      std::vector<GraphParameter>& parameters,
                      std::string& error)
{
    if (!array.is_array()) {
        error = "Subgraph parameters must be an array";
        return false;
    }
    std::unordered_set<std::string> ids;
    for (const auto& parameter_json : array) {
        if (!parameter_json.is_object() ||
            !parameter_json.contains("id") || !parameter_json["id"].is_string() ||
            !parameter_json.contains("targetNode") || !parameter_json["targetNode"].is_string() ||
            !parameter_json.contains("targetProperty") || !parameter_json["targetProperty"].is_string()) {
            error = "Invalid subgraph parameter";
            return false;
        }
        GraphParameter parameter;
        parameter.id = parameter_json["id"].get<std::string>();
        parameter.type = parameter_json.value("type", "number");
        parameter.default_value = parameter_json.value("default", nlohmann::json{});
        parameter.target_node = parameter_json["targetNode"].get<std::string>();
        parameter.target_property = parameter_json["targetProperty"].get<std::string>();
        if (parameter.id.empty() || parameter.target_node.empty() || parameter.target_property.empty() ||
            !ids.insert(parameter.id).second) {
            error = "Invalid or duplicate subgraph parameter id";
            return false;
        }
        parameters.push_back(std::move(parameter));
    }
    return true;
}

bool validate_subgraph_output_contract(const GraphSubgraph& subgraph,
                                       std::string& error)
{
    if (subgraph.outputs.size() != 1) {
        error = "Subgraph must declare exactly one output port";
        return false;
    }

    const auto output_count = std::count_if(
        subgraph.nodes.begin(),
        subgraph.nodes.end(),
        [](const GraphNode& node) {
            return node.type == "Output" || node.type == "SubgraphOutput";
        });
    if (output_count != 1) {
        error = "Subgraph must contain exactly one Output node";
        return false;
    }
    for (const auto& parameter : subgraph.parameters) {
        const bool target_exists = std::any_of(
            subgraph.nodes.begin(), subgraph.nodes.end(),
            [&](const GraphNode& node) { return node.id == parameter.target_node; });
        if (!target_exists) {
            error = "Subgraph parameter target node not found: " + parameter.target_node;
            return false;
        }
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
    std::unordered_map<std::string, std::vector<std::string>> passthrough_inputs_by_output;
    std::unordered_set<std::string> declared_inputs;
};

struct ParentScopeContext {
    const std::vector<GraphNode>* nodes = nullptr;
    const std::vector<GraphEdge>* edges = nullptr;
    std::string prefix;
    const std::unordered_map<std::string, ExpandedScope>* instances = nullptr;
};

bool resolve_parent_ref_endpoints(const GraphNode& ref_node,
                                  const ParentScopeContext& parent_context,
                                  std::vector<Endpoint>& endpoints,
                                  std::string& error)
{
    if (!parent_context.nodes) {
        error = "SubgraphParentRef is only valid inside a subgraph definition.";
        return false;
    }
    const std::string parent_node_id = ref_node.data.value("parentNodeId", std::string{});
    if (parent_node_id.empty()) {
        error = "SubgraphParentRef missing parentNodeId.";
        return false;
    }
    std::string parent_handle = ref_node.data.value("parentHandle", std::string{"out"});
    if (parent_handle.empty())
        parent_handle = "out";

    const GraphNode* parent_node = nullptr;
    for (const auto& node : *parent_context.nodes) {
        if (node.id == parent_node_id) {
            parent_node = &node;
            break;
        }
    }
    if (!parent_node) {
        error = "SubgraphParentRef parent node not found: " + parent_node_id;
        return false;
    }
    if (parent_node->type == "SubgraphInput" || parent_node->type == "SubgraphOutput" ||
        parent_node->type == "SubgraphParentRef") {
        error = "SubgraphParentRef cannot reference structural parent node: " + parent_node_id;
        return false;
    }
    if (parent_node->type == "Subgraph") {
        if (!parent_context.instances) {
            error = "SubgraphParentRef parent subgraph instance is not expanded: " + parent_node_id;
            return false;
        }
        const auto instance_it = parent_context.instances->find(parent_node_id);
        if (instance_it == parent_context.instances->end()) {
            error = "SubgraphParentRef parent subgraph instance is not expanded: " + parent_node_id;
            return false;
        }
        const auto port_it = instance_it->second.output_sources.find(parent_handle);
        if (port_it == instance_it->second.output_sources.end() || port_it->second.empty()) {
            error = "SubgraphParentRef parent subgraph output is not connected: " + parent_node_id + "/" + parent_handle;
            return false;
        }
        endpoints.insert(endpoints.end(), port_it->second.begin(), port_it->second.end());
        return true;
    }
    endpoints.push_back({parent_context.prefix + parent_node_id, parent_handle});
    return true;
}

nlohmann::json parse_instance_overrides(const GraphNode& instance)
{
    if (!instance.data.is_object() || !instance.data.contains("subgraphParameterOverrides"))
        return nlohmann::json::array();
    nlohmann::json overrides = instance.data["subgraphParameterOverrides"];
    if (overrides.is_string())
        overrides = nlohmann::json::parse(overrides.get<std::string>(), nullptr, false);
    return overrides;
}

nlohmann::json resolve_parameter_value(const GraphParameter& parameter,
                                       const nlohmann::json& overrides)
{
    if (overrides.is_object()) {
        const auto found = overrides.find(parameter.id);
        if (found != overrides.end())
            return *found;
    }
    if (!overrides.is_array())
        return parameter.default_value;
    for (const auto& entry : overrides) {
        if (!entry.is_object() || entry.value("parameterId", "") != parameter.id)
            continue;
        if (entry.contains("value"))
            return entry["value"];
        if (parameter.type == "integer" && entry.contains("intValue"))
            return entry["intValue"];
        if (parameter.type == "number" && entry.contains("floatValue"))
            return entry["floatValue"];
        if (parameter.type == "boolean" && entry.contains("boolValue"))
            return entry["boolValue"];
        if (entry.contains("stringValue")) {
            const auto& stored = entry["stringValue"];
            if (parameter.type == "vector3" && stored.is_string()) {
                auto vector = nlohmann::json::parse(stored.get<std::string>(), nullptr, false);
                if (vector.is_array() && vector.size() == 3)
                    return vector;
            }
            return stored;
        }
    }
    return parameter.default_value;
}

GraphSubgraph resolve_subgraph_instance(const GraphSubgraph& definition,
                                        const GraphNode& instance)
{
    GraphSubgraph resolved = definition;
    const nlohmann::json overrides = parse_instance_overrides(instance);
    for (const auto& parameter : resolved.parameters) {
        const auto target = std::find_if(
            resolved.nodes.begin(), resolved.nodes.end(),
            [&](const GraphNode& node) { return node.id == parameter.target_node; });
        if (target == resolved.nodes.end() || !target->data.is_object())
            continue;
        target->data[parameter.target_property] = resolve_parameter_value(parameter, overrides);
    }
    return resolved;
}

bool expand_scope(const std::vector<GraphNode>& nodes,
                  const std::vector<GraphEdge>& edges,
                  const std::string& prefix,
                  const std::unordered_map<std::string, const GraphSubgraph*>& definitions,
                  std::vector<std::string>& stack,
                  const ParentScopeContext& parent_context,
                  const GraphSubgraph* scope_definition,
                  ExpandedScope& out,
                  std::string& error)
{
    const auto is_scope_output = [scope_definition](const GraphNode* node) {
        return scope_definition && node &&
               (node->type == "Output" || node->type == "SubgraphOutput");
    };
    const auto scope_output_handle = [scope_definition](const std::string& legacy_handle) {
        return scope_definition && !scope_definition->outputs.empty()
            ? scope_definition->outputs.front().id
            : legacy_handle;
    };

    std::unordered_map<std::string, const GraphNode*> by_id;
    std::unordered_map<std::string, ExpandedScope> instances;
    for (const auto& node : nodes) {
        by_id[node.id] = &node;
        if (node.type == "SubgraphInput" || node.type == "SubgraphOutput" ||
            node.type == "SubgraphParentRef" ||
            (scope_definition && node.type == "Output"))
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
        const GraphSubgraph definition = resolve_subgraph_instance(*definition_it->second, node);
        ParentScopeContext child_parent;
        child_parent.nodes = &nodes;
        child_parent.edges = &edges;
        child_parent.prefix = prefix;
        child_parent.instances = &instances;
        if (!expand_scope(definition.nodes, definition.edges, prefix + node.id + "/",
                          definitions, stack, child_parent, &definition, child, error))
            return false;
        stack.pop_back();
        out.nodes.insert(out.nodes.end(), child.nodes.begin(), child.nodes.end());
        out.edges.insert(out.edges.end(), child.edges.begin(), child.edges.end());
        for (const auto& input : definition.inputs)
            child.declared_inputs.insert(input.id);
        instances.emplace(node.id, std::move(child));
    }

    std::unordered_set<std::string> resolving_passthroughs;
    std::function<std::vector<Endpoint>(const GraphEdge&)> source_endpoints;
    source_endpoints = [&](const GraphEdge& edge) -> std::vector<Endpoint> {
        const auto node_it = by_id.find(edge.source);
        if (node_it == by_id.end()) return {};
        if (node_it->second->type == "SubgraphInput") return {};
        if (is_scope_output(node_it->second)) return {};
        if (node_it->second->type == "SubgraphParentRef") {
            std::vector<Endpoint> endpoints;
            if (!resolve_parent_ref_endpoints(*node_it->second, parent_context, endpoints, error))
                return {};
            return endpoints;
        }
        if (node_it->second->type == "Subgraph") {
            const auto instance_it = instances.find(edge.source);
            if (instance_it == instances.end()) return {};
            const auto port_it = instance_it->second.output_sources.find(edge.source_handle);
            std::vector<Endpoint> endpoints =
                port_it == instance_it->second.output_sources.end()
                    ? std::vector<Endpoint>{}
                    : port_it->second;
            const auto passthrough_it =
                instance_it->second.passthrough_inputs_by_output.find(edge.source_handle);
            if (passthrough_it == instance_it->second.passthrough_inputs_by_output.end())
                return endpoints;

            const std::string resolving_key = edge.source + "\x1f" + edge.source_handle;
            if (!resolving_passthroughs.insert(resolving_key).second) {
                error = "Subgraph passthrough cycle detected: " + edge.source + "/" + edge.source_handle;
                return {};
            }
            for (const auto& input_handle : passthrough_it->second) {
                for (const auto& incoming : edges) {
                    if (incoming.target != edge.source || incoming.target_handle != input_handle)
                        continue;
                    auto resolved = source_endpoints(incoming);
                    if (!error.empty())
                        return {};
                    endpoints.insert(endpoints.end(), resolved.begin(), resolved.end());
                }
            }
            resolving_passthroughs.erase(resolving_key);
            return endpoints;
        }
        return {{prefix + edge.source, edge.source_handle}};
    };

    auto target_endpoints = [&](const GraphEdge& edge) -> std::vector<Endpoint> {
        const auto node_it = by_id.find(edge.target);
        if (node_it == by_id.end()) return {};
        if (node_it->second->type == "SubgraphInput") return {};
        if (is_scope_output(node_it->second)) return {};
        if (node_it->second->type == "SubgraphParentRef") return {};
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

        if (source_it->second->type == "SubgraphInput" &&
            is_scope_output(target_it->second)) {
            auto& inputs = out.passthrough_inputs_by_output[
                scope_output_handle(edge.target_handle)];
            if (std::find(inputs.begin(), inputs.end(), edge.source_handle) == inputs.end())
                inputs.push_back(edge.source_handle);
            continue;
        }

        const auto sources = source_endpoints(edge);
        if (!error.empty())
            return false;
        const auto targets = target_endpoints(edge);
        if (source_it->second->type == "SubgraphInput") {
            if (targets.empty()) {
                if (target_it->second->type == "Subgraph") {
                    const auto instance_it = instances.find(edge.target);
                    if (instance_it != instances.end() &&
                        instance_it->second.declared_inputs.count(edge.target_handle) != 0)
                        continue;
                }
                error = "Subgraph input is not connected to an executable node";
                return false;
            }
            auto& list = out.input_targets[edge.source_handle];
            list.insert(list.end(), targets.begin(), targets.end());
            continue;
        }
        if (is_scope_output(target_it->second)) {
            if (sources.empty()) { error = "Subgraph output is not connected from an executable node"; return false; }
            auto& list = out.output_sources[scope_output_handle(edge.target_handle)];
            list.insert(list.end(), sources.begin(), sources.end());
            continue;
        }
        if (sources.empty() || targets.empty()) {
            if (targets.empty() && target_it->second->type == "Subgraph") {
                const auto instance_it = instances.find(edge.target);
                if (instance_it != instances.end() &&
                    instance_it->second.declared_inputs.count(edge.target_handle) != 0)
                    continue;
            }
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
                flat.source_pin_type = edge.source_pin_type;
                flat.target_pin_type = edge.target_pin_type;
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
    ParentScopeContext root_parent;
    if (!expand_scope(graph.nodes, graph.edges, "", definitions, stack, root_parent,
                      nullptr, flat, error))
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
            const auto parameters = subgraph_json.value("parameters", nlohmann::json::array());
            if (!inputs.is_array() || !outputs.is_array() || !parameters.is_array() ||
                !parse_ports(inputs, subgraph.inputs, parse_error) ||
                !parse_ports(outputs, subgraph.outputs, parse_error) ||
                !parse_parameters(parameters, subgraph.parameters, parse_error) ||
                !parse_nodes(subgraph_json["nodes"], subgraph.nodes, parse_error) ||
                !parse_edges(subgraph_json["edges"], subgraph.edges, parse_error) ||
                !validate_subgraph_output_contract(subgraph, parse_error))
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
    if (graph.version != "1.0" && graph.version != "2.0" && graph.version != "3.0")
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
        if (!elements::find_node_contract(node.type))
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON,
                        "Executable node is missing from node manifest");

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

    std::set<std::tuple<std::string, std::string, std::string, std::string>> edge_signatures;
    std::unordered_map<std::string, int> target_pin_counts;
    for (const auto& edge : graph.edges) {
        const auto* source_contract = elements::find_node_contract(node_by_id.at(edge.source)->type);
        const auto* target_contract = elements::find_node_contract(node_by_id.at(edge.target)->type);
        if (!source_contract || !target_contract)
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON,
                        "Executable node is missing from node manifest");
        const auto source_pin = source_contract->outputs.find(edge.source_handle);
        if (source_pin == source_contract->outputs.end())
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON,
                        "Edge source handle is not declared by node manifest");
        const auto target_pin = target_contract->inputs.find(edge.target_handle);
        if (target_pin == target_contract->inputs.end())
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON,
                        "Edge target handle is not declared by node manifest");
        if (!elements::pin_types_compatible(source_pin->second.type, target_pin->second.type))
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON,
                        "Edge pin types are incompatible");
        if ((!edge.source_pin_type.empty() &&
             elements::normalize_pin_type(edge.source_pin_type) !=
                 elements::normalize_pin_type(source_pin->second.type)) ||
            (!edge.target_pin_type.empty() &&
             elements::normalize_pin_type(edge.target_pin_type) !=
                 elements::normalize_pin_type(target_pin->second.type)))
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON,
                        "Edge pin type metadata does not match node manifest");
        if (!edge_signatures.emplace(edge.source, edge.source_handle,
                                     edge.target, edge.target_handle).second)
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON, "Duplicate edge");
        const std::string key = edge.target + "\x1f" + edge.target_handle;
        if (++target_pin_counts[key] > 1 && !target_pin->second.variadic)
            return fail(err_buf, err_buf_size, PCG_ERR_INVALID_JSON,
                        "Multiple edges connected to a non-variadic input");
    }

    return PCG_OK;
}

} // namespace pcg::internal
