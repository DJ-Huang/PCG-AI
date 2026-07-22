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

std::string normalize_pin_type(const std::string& type)
{
    if (type == "Spline")
        return "SpatialSpline";
    return type;
}

bool pin_types_compatible(const std::string& source, const std::string& target)
{
    const std::string normalized_source = normalize_pin_type(source);
    const std::string normalized_target = normalize_pin_type(target);
    return normalized_source == normalized_target || normalized_source == "Any" ||
           normalized_target == "Any";
}

} // namespace pcg::internal::elements
