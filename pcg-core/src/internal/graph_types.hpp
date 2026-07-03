#pragma once

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
};

struct Graph {
    std::string version;
    std::vector<GraphNode> nodes;
    std::vector<GraphEdge> edges;
};

using NodeOutputMap = std::unordered_map<std::string, nlohmann::json>;

} // namespace pcg::internal
