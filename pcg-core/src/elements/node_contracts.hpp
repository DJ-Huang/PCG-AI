#pragma once

#include <string>
#include <unordered_map>

namespace pcg::internal::elements {

struct PinContract {
    std::string type;
    bool variadic = false;
};

struct NodeContract {
    std::unordered_map<std::string, PinContract> inputs;
    std::unordered_map<std::string, PinContract> outputs;
};

const NodeContract* find_node_contract(const std::string& node_type);
bool pin_types_compatible(const std::string& source, const std::string& target);

} // namespace pcg::internal::elements
