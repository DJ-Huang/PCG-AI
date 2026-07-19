#pragma once

#include "data/pcg_data_collection.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace pcg::internal {

struct GraphNode {
    std::string id;
    std::string type;
    nlohmann::json data;
};

struct GraphEdge {
    std::string id;
    std::string source;
    std::string target;
    std::string source_handle = "out";
    std::string target_handle = "in";
    std::string source_pin_type;
    std::string target_pin_type;
};

struct GraphPort {
    std::string id;
    std::string name;
    std::string pin_type = "Any";
};

struct GraphSubgraph {
    std::string id;
    std::string name;
    std::vector<GraphPort> inputs;
    std::vector<GraphPort> outputs;
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
};

struct Graph {
    std::string version;
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
    std::vector<GraphSubgraph> subgraphs;
};

using NodeOutputMap = std::unordered_map<std::string, data::PcgDataCollection>;

} // namespace pcg::internal
