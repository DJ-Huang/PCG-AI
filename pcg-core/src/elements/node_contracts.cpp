#include "elements/node_contracts.hpp"

#include "node_manifest_embedded.hpp"

#include <nlohmann/json.hpp>

namespace pcg::internal::elements {
namespace {

std::unordered_map<std::string, NodeContract> parse_contracts()
{
    std::unordered_map<std::string, NodeContract> result;
    const auto manifest = nlohmann::json::parse(kNodeManifestJson);
    for (const auto& node : manifest.at("nodes")) {
        NodeContract contract;
        for (const auto& pin : node.at("inputs")) {
            contract.inputs.emplace(
                pin.at("id").get<std::string>(),
                PinContract{pin.at("pinType").get<std::string>(),
                            pin.value("variadic", false)});
        }
        for (const auto& pin : node.at("outputs")) {
            contract.outputs.emplace(
                pin.at("id").get<std::string>(),
                PinContract{pin.at("pinType").get<std::string>(), false});
        }
        result.emplace(node.at("type").get<std::string>(), std::move(contract));
    }
    return result;
}

const std::unordered_map<std::string, NodeContract>& contracts()
{
    static const auto value = parse_contracts();
    return value;
}

} // namespace

const NodeContract* find_node_contract(const std::string& node_type)
{
    const auto found = contracts().find(node_type);
    return found == contracts().end() ? nullptr : &found->second;
}

bool pin_types_compatible(const std::string& source, const std::string& target)
{
    return source == target || source == "Any" || target == "Any";
}

} // namespace pcg::internal::elements
