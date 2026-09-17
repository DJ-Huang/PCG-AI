#include "elements/pcg_element.hpp"
#include "elements/attribute_elements.hpp"
#include "elements/primitive_elements.hpp"
#include "elements/mesh_elements.hpp"
#include "elements/geometry_elements.hpp"
#include "elements/boolean_elements.hpp"
#include "elements/mesh_scatter_elements.hpp"
#include "elements/spline_elements.hpp"
#include "elements/spline_mesh_elements.hpp"
#include "elements/structural_elements.hpp"
#include "elements/material_elements.hpp"
#include "elements/uv_elements.hpp"
#include "elements/vehicle_modeling_elements.hpp"
#include "elements/building_elements.hpp"
#include "elements/heightfield_elements.hpp"
#include "elements/houdini_sop_elements.hpp"
#include "elements/assembly_elements.hpp"
#include "elements/facade_foundation_elements.hpp"
#include "elements/topology_parity_elements.hpp"
#include "elements/oriented_sdf_surface.hpp"
#include "elements/add_elements.hpp"
#include "elements/node_contracts.hpp"
#include "scripting/generated_operation_policy.hpp"
#include "scripting/operation_bridge.hpp"
#include "node_manifest_embedded.hpp"

#include "internal/error_util.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

PcgResultCode fail(PcgContext& ctx, PcgResultCode code, const char* message)
{
    write_error(ctx.err_buf, ctx.err_buf_size, message);
    return code;
}

class SpawnPointsElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SpawnPoints"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail(ctx, PCG_ERR_EXECUTION, "SpawnPoints missing node");

        int count = ctx.node->data.value("count", 100);
        if (count < 0)
            count = 0;
        if (count > 10000)
            count = 10000;
        const double radius = ctx.node->data.value("radius", 10.0);

        if (radius < 0.0)
            return fail(ctx, PCG_ERR_EXECUTION, "SpawnPoints radius must be >= 0");

        uint32_t rng = static_cast<uint32_t>(ctx.graph_seed) ^
                       static_cast<uint32_t>(ctx.graph_seed * 2654435761u);

        data::PcgPointData points;
        constexpr double kPi = 3.14159265358979323846;
        for (int i = 0; i < count; ++i) {
            rng = rng * 1664525u + 1013904223u;
            const double t = (count <= 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(count);
            const double angle = t * 2.0 * kPi + (rng % 1000) / 1000.0 * 0.25;
            rng = rng * 1664525u + 1013904223u;
            const double radial = radius * (0.25 + (rng % 1000) / 1000.0 * 0.75);

            points.add_point(data::PcgPoint{
                radial * std::cos(angle),
                0.0,
                radial * std::sin(angle),
            });
        }

        ctx.outputs.add_points("out", std::move(points));
        return PCG_OK;
    }
};

class PlaceInSceneElement final : public IPcgElement {
public:
    const char* type_name() const override { return "PlaceInScene"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail(ctx, PCG_ERR_EXECUTION, "PlaceInScene missing node");

        auto points_payload = ctx.inputs.find_points_shared("in");
        if (!points_payload) {
            const nlohmann::json* json_payload = ctx.inputs.find_json("in");
            if (!json_payload)
                return fail(ctx, PCG_ERR_EXECUTION, "PlaceInScene missing points input");

            if (!json_payload->contains("points") || !(*json_payload)["points"].is_array())
                return fail(ctx, PCG_ERR_EXECUTION, "PlaceInScene missing points input");

            const std::string prefab = ctx.node->data.value("prefab", "");
            const double scale = ctx.node->data.value("scale", 1.0);

            nlohmann::json out{
                {"status", "ok"},
                {"prefab", prefab},
                {"scale", scale},
                {"pointCount", (*json_payload)["points"].size()},
                {"points", (*json_payload)["points"]},
            };
            ctx.outputs.add("out", data::PcgDataType::Point, std::move(out));
            return PCG_OK;
        }

        const std::string prefab = ctx.node->data.value("prefab", "");
        const double scale = ctx.node->data.value("scale", 1.0);

        nlohmann::json sidecar{
            {"status", "ok"},
            {"prefab", prefab},
            {"scale", scale},
            {"pointCount", points_payload->points().size()},
        };
        ctx.outputs.add_points_shared_with_meta("out", std::move(points_payload), std::move(sidecar));
        return PCG_OK;
    }
};

/// Houdini-style terminal node. Passes input through to output unchanged.
/// The execution engine prefers Output nodes as the graph's sink.
class OutputElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Output"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (auto heightfield = ctx.inputs.find_heightfield_shared("in")) {
            ctx.outputs.add_heightfield_shared("out", heightfield);
            return PCG_OK;
        }

        if (auto geometry = ctx.inputs.find_geometry_shared("in")) {
            ctx.outputs.add_geometry_shared("out", geometry);
            return PCG_OK;
        }

        if (auto mesh = ctx.inputs.find_mesh_shared("in")) {
            ctx.outputs.add_mesh_shared("out", mesh);
            if (auto spawn_mesh = ctx.inputs.find_mesh_shared("spawnMesh"))
                ctx.outputs.add_mesh_shared("spawnMesh", spawn_mesh);
            return PCG_OK;
        }

        if (const data::PcgTaggedData* in_item = ctx.inputs.find("in");
            in_item && in_item->points) {
            ctx.outputs.add_points_shared_with_meta("out", in_item->points, in_item->payload);
            for (const auto& item : ctx.inputs.items()) {
                if (item.tag == "spawnMesh" && item.mesh)
                    ctx.outputs.add_mesh_shared("spawnMesh", item.mesh);
            }
            return PCG_OK;
        }

        if (const data::PcgSplineData* splines = ctx.inputs.find_splines("in")) {
            ctx.outputs.add_splines("out", *splines);
            return PCG_OK;
        }

        const nlohmann::json* input = ctx.inputs.find_json("in");
        if (!input)
            return fail(ctx, PCG_ERR_EXECUTION, "Output missing input");

        ctx.outputs.add("out", data::PcgDataType::Unknown, *input);
        if (auto spawn_mesh = ctx.inputs.find_mesh_shared("spawnMesh"))
            ctx.outputs.add_mesh_shared("spawnMesh", spawn_mesh);
        return PCG_OK;
    }
};

std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& registry()
{
    static std::unordered_map<std::string, std::unique_ptr<IPcgElement>> instance;
    return instance;
}

} // namespace

void register_builtin_elements()
{
    // Registration is immutable after first construction. std::call_once keeps
    // concurrent workers from observing a partially populated registry.
    static std::once_flag once;
    std::call_once(once, [] {
        auto& map = registry();
        map.emplace("SpawnPoints", std::make_unique<SpawnPointsElement>());
        map.emplace("PlaceInScene", std::make_unique<PlaceInSceneElement>());
        map.emplace("Output", std::make_unique<OutputElement>());
        register_attribute_elements(map);
        register_phase41_elements(map);
        register_phase42_elements(map);
        register_mesh_elements(map);
        register_geometry_elements(map);
        register_add_elements(map);
        register_boolean_elements(map);
        register_mesh_scatter_elements(map);
        register_spline_elements(map);
        register_spline_mesh_elements(map);
        register_material_elements(map);
        register_uv_elements(map);
        register_vehicle_modeling_elements(map);
        register_building_elements(map);
        register_heightfield_elements(map);
        register_assembly_elements(map);
        register_facade_foundation_elements(map);
        register_topology_parity_elements(map);
        register_oriented_sdf_surface_elements(map);
        register_houdini_sop_elements(map);
    });
}

const IPcgElement* find_element(const std::string& type)
{
    register_builtin_elements();
    const auto it = registry().find(type);
    return it == registry().end() ? nullptr : it->second.get();
}

bool is_known_element_type(const std::string& type)
{
    return find_element(type) != nullptr;
}

} // namespace pcg::internal::elements

namespace pcg::internal::scripting {
namespace {

using generated::OperationPolicyEntry;

OperationCapability capability_from_string(const std::string& value)
{
    if (value == "pure")
        return OperationCapability::Pure;
    if (value == "resource-read")
        return OperationCapability::ResourceRead;
    if (value == "graph-control")
        return OperationCapability::GraphControl;
    if (value == "side-effecting")
        return OperationCapability::SideEffecting;
    return OperationCapability::HostBound;
}

const char* capability_name(OperationCapability value)
{
    switch (value) {
    case OperationCapability::Pure: return "pure";
    case OperationCapability::ResourceRead: return "resource-read";
    case OperationCapability::GraphControl: return "graph-control";
    case OperationCapability::SideEffecting: return "side-effecting";
    case OperationCapability::HostBound: return "host-bound";
    }
    return "host-bound";
}

const OperationPolicyEntry* find_policy(const std::string& operation)
{
    const auto* begin = generated::kOperationPolicies;
    const auto* end = begin + generated::kOperationPolicyCount;
    const auto it = std::lower_bound(begin, end, operation,
        [](const OperationPolicyEntry& entry, const std::string& key) {
            return std::string(entry.operation) < key;
        });
    if (it == end || operation != it->operation)
        return nullptr;
    return it;
}

const nlohmann::json& manifest()
{
    static const auto value = nlohmann::json::parse(elements::kNodeManifestJson);
    return value;
}

const nlohmann::json* find_manifest_node(const std::string& operation)
{
    for (const auto& node : manifest().at("nodes")) {
        if (node.value("type", std::string()) == operation)
            return &node;
    }
    return nullptr;
}

nlohmann::json make_diagnostic(const std::string& code,
                               const std::string& operation,
                               const std::string& message,
                               const std::string& path = std::string())
{
    nlohmann::json diagnostic = {
        {"code", code},
        {"operation", operation},
        {"message", message},
    };
    if (!path.empty())
        diagnostic["path"] = path;
    return diagnostic;
}

PcgResultCode fail_invocation(OperationInvocationResult& result,
                              PcgResultCode native_code,
                              const std::string& code,
                              const std::string& operation,
                              const std::string& message,
                              const std::string& path = std::string())
{
    result.code = native_code;
    result.outputs = data::PcgDataCollection{};
    result.diagnostic = make_diagnostic(code, operation, message, path);
    result.diagnostic["nativeCode"] = static_cast<int>(native_code);
    result.statistics["outcome"] = "failure";
    return native_code;
}

bool validate_property_value(const std::string& name,
                             const nlohmann::json& spec,
                             const nlohmann::json& value,
                             std::string& reason)
{
    const std::string type = spec.value("type", std::string());
    bool valid = true;
    if (type == "integer")
        valid = value.is_number_integer() || value.is_number_unsigned();
    else if (type == "number")
        valid = value.is_number();
    else if (type == "boolean")
        valid = value.is_boolean();
    else if (type == "string" || type == "groupSelect")
        valid = value.is_string();
    else if (type == "enum") {
        valid = value.is_string() || value.is_number() || value.is_boolean();
        if (valid && spec.contains("options") && spec["options"].is_array()) {
            bool matched = false;
            for (const auto& option : spec["options"]) {
                if (option.contains("value") && option["value"] == value) {
                    matched = true;
                    break;
                }
            }
            valid = matched;
        }
    }

    if (!valid) {
        reason = "parameter '" + name + "' has incompatible type or enum value";
        return false;
    }

    if (value.is_number()) {
        const double number = value.get<double>();
        if (spec.contains("minimum") && number < spec["minimum"].get<double>()) {
            reason = "parameter '" + name + "' is below its minimum";
            return false;
        }
        if (spec.contains("maximum") && number > spec["maximum"].get<double>()) {
            reason = "parameter '" + name + "' is above its maximum";
            return false;
        }
    }
    return true;
}

bool normalize_parameters(const OperationMetadata& metadata,
                          const nlohmann::json& provided,
                          nlohmann::json& normalized,
                          std::string& invalid_name,
                          std::string& reason)
{
    if (!provided.is_object()) {
        reason = "parameters must be a JSON object";
        return false;
    }

    for (auto it = provided.begin(); it != provided.end(); ++it) {
        if (!metadata.parameters.contains(it.key())) {
            invalid_name = it.key();
            reason = "unknown parameter '" + it.key() + "'";
            return false;
        }
    }

    normalized = nlohmann::json::object();
    for (auto it = metadata.parameters.begin(); it != metadata.parameters.end(); ++it) {
        const std::string& name = it.key();
        const auto& spec = it.value();
        if (provided.contains(name)) {
            std::string validation_reason;
            if (!validate_property_value(name, spec, provided.at(name), validation_reason)) {
                invalid_name = name;
                reason = std::move(validation_reason);
                return false;
            }
            normalized[name] = provided.at(name);
        } else if (spec.contains("default")) {
            normalized[name] = spec.at("default");
        }
    }
    return true;
}

std::string tagged_pin_type(const data::PcgTaggedData& item)
{
    if (item.heightfield)
        return "HeightField";
    if (item.geometry)
        return "SpatialGeometry";
    if (item.mesh)
        return "SpatialMesh";
    if (item.points)
        return "SpatialPoint";
    if (item.splines)
        return "SpatialSpline";
    if (item.type == data::PcgDataType::Param)
        return "Param";
    if (item.type == data::PcgDataType::Point)
        return "SpatialPoint";
    if (item.type == data::PcgDataType::Spline)
        return "SpatialSpline";
    if (item.type == data::PcgDataType::Mesh)
        return "SpatialMesh";
    if (item.type == data::PcgDataType::Geometry)
        return "SpatialGeometry";
    if (item.type == data::PcgDataType::HeightField)
        return "HeightField";
    return "Any";
}

const nlohmann::json* find_pin(const nlohmann::json& pins, const std::string& id)
{
    for (const auto& pin : pins) {
        if (pin.value("id", std::string()) == id)
            return &pin;
    }
    return nullptr;
}

bool validate_collection(const data::PcgDataCollection& collection,
                         const nlohmann::json& pins,
                         std::string& bad_tag,
                         std::string& reason)
{
    for (const auto& item : collection.items()) {
        const nlohmann::json* pin = find_pin(pins, item.tag);
        if (!pin) {
            bad_tag = item.tag;
            reason = "undeclared port '" + item.tag + "'";
            return false;
        }
        const std::string expected = pin->value("pinType", "Any");
        const std::string actual = tagged_pin_type(item);
        if (!elements::pin_types_compatible(actual, expected)) {
            bad_tag = item.tag;
            reason = "port '" + item.tag + "' expects " + expected + " but received " + actual;
            return false;
        }
    }
    return true;
}

bool has_required_resources(const OperationMetadata& metadata,
                            const OperationInvocationContext& context,
                            std::string& missing)
{
    for (const auto& dependency : metadata.resource_dependencies) {
        if (dependency == "mesh-runtime" && !context.meshes) {
            missing = dependency;
            return false;
        }
        if (dependency == "texture-runtime" && !context.textures) {
            missing = dependency;
            return false;
        }
        if (dependency == "spline-runtime" && !context.splines) {
            missing = dependency;
            return false;
        }
        if (dependency == "heightfield-runtime" && !context.heightfields) {
            missing = dependency;
            return false;
        }
    }
    return true;
}

} // namespace

OperationMetadata operation_metadata(const std::string& operation)
{
    OperationMetadata metadata;
    metadata.operation = operation;
    metadata.display_name = operation;
    metadata.capability = capability_from_string(generated::kDefaultCapability);
    metadata.script_callable = generated::kDefaultScriptCallable;
    metadata.cancellation_supported = generated::kDefaultCancellationSupported;

    if (const OperationPolicyEntry* policy = find_policy(operation)) {
        metadata.capability = capability_from_string(policy->capability);
        metadata.script_callable = policy->script_callable;
        metadata.cancellation_supported = policy->cancellation_supported;
        for (std::size_t i = 0; i < policy->resource_dependency_count; ++i)
            metadata.resource_dependencies.emplace_back(policy->resource_dependencies[i]);
    }

    if (const auto* node = find_manifest_node(operation)) {
        metadata.display_name = node->value("displayName", operation);
        metadata.parameters = node->value("properties", nlohmann::json::object());
        metadata.inputs = node->value("inputs", nlohmann::json::array());
        metadata.outputs = node->value("outputs", nlohmann::json::array());
    }
    return metadata;
}

nlohmann::json operation_discovery_json()
{
    elements::register_builtin_elements();
    nlohmann::json operations = nlohmann::json::array();
    for (const auto& node : manifest().at("nodes")) {
        const std::string operation = node.value("type", std::string());
        if (operation.empty() || !elements::is_known_element_type(operation))
            continue;
        const OperationMetadata metadata = operation_metadata(operation);
        operations.push_back({
            {"operation", metadata.operation},
            {"displayName", metadata.display_name},
            {"capability", capability_name(metadata.capability)},
            {"scriptCallable", metadata.script_callable},
            {"cancellationSupported", metadata.cancellation_supported},
            {"resourceDependencies", metadata.resource_dependencies},
            {"parameters", metadata.parameters},
            {"inputs", metadata.inputs},
            {"outputs", metadata.outputs},
        });
    }
    return {
        {"schemaVersion", 1},
        {"nodeManifestVersion", manifest().value("version", std::string())},
        {"operations", std::move(operations)},
    };
}

std::string operation_declarations()
{
    std::vector<std::string> allowed;
    const auto discovery = operation_discovery_json();
    for (const auto& operation : discovery.at("operations")) {
        if (operation.value("scriptCallable", false))
            allowed.push_back(operation.at("operation").get<std::string>());
    }
    std::sort(allowed.begin(), allowed.end());

    std::ostringstream out;
    out << "// Generated from node-manifest.json + script-operation-policy-v1.json.\n";
    out << "export type PcgScriptOperationName =\n";
    for (std::size_t i = 0; i < allowed.size(); ++i)
        out << "  | \"" << allowed[i] << "\"" << (i + 1 == allowed.size() ? ";\n" : "\n");
    if (allowed.empty())
        out << "  never;\n";
    out << "\nexport type PcgOperationParameters = Readonly<Record<string, unknown>>;\n";
    out << "export type PcgOperationInputs = Readonly<Record<string, unknown>>;\n";
    out << "export type PcgOperationOutputs = Readonly<Record<string, unknown>>;\n\n";
    out << "export interface PcgOperationInvoker {\n";
    out << "  invoke(operation: PcgScriptOperationName, parameters?: PcgOperationParameters, inputs?: PcgOperationInputs): PcgOperationOutputs;\n";
    out << "}\n";
    return out.str();
}

PcgResultCode invoke_operation(const OperationInvocationRequest& request,
                               OperationInvocationResult& result)
{
    result = OperationInvocationResult{};
    result.statistics = {
        {"operation", request.operation},
        {"outcome", "running"},
    };

    if (request.operation.empty()) {
        return fail_invocation(result, PCG_ERR_INVALID_ARGUMENT, "invalid_operation",
                               request.operation, "operation name must not be empty", "operation");
    }

    // Explicitly denied names win over registry lookup so reserved graph-control
    // operations such as recursive CustomFunction stay fail-closed even before
    // an executable element exists for them.
    if (const OperationPolicyEntry* policy = find_policy(request.operation);
        policy && !policy->script_callable) {
        return fail_invocation(result, PCG_ERR_INVALID_ARGUMENT, "operation_disallowed",
                               request.operation,
                               "operation is not on the script-callable allowlist", "operation");
    }

    const elements::IPcgElement* element = elements::find_element(request.operation);
    if (!element) {
        return fail_invocation(result, PCG_ERR_UNKNOWN_NODE, "unknown_operation",
                               request.operation, "unknown registered Core operation", "operation");
    }

    const OperationMetadata metadata = operation_metadata(request.operation);
    if (!metadata.script_callable) {
        return fail_invocation(result, PCG_ERR_INVALID_ARGUMENT, "operation_disallowed",
                               request.operation,
                               "operation is not on the script-callable allowlist", "operation");
    }

    if (request.context.is_cancel_requested && request.context.is_cancel_requested()) {
        return fail_invocation(result, PCG_ERR_EXECUTION, "cancelled",
                               request.operation, "operation invocation cancelled before execution");
    }

    std::string missing_resource;
    if (!has_required_resources(metadata, request.context, missing_resource)) {
        return fail_invocation(result, PCG_ERR_INVALID_ARGUMENT, "missing_resource_context",
                               request.operation,
                               "required resource context is unavailable: " + missing_resource,
                               "context.resources");
    }

    nlohmann::json normalized_parameters;
    std::string invalid_parameter;
    std::string parameter_reason;
    if (!normalize_parameters(metadata, request.parameters, normalized_parameters,
                              invalid_parameter, parameter_reason)) {
        const std::string path = invalid_parameter.empty()
            ? "parameters" : "parameters." + invalid_parameter;
        return fail_invocation(result, PCG_ERR_INVALID_ARGUMENT, "invalid_parameters",
                               request.operation, parameter_reason, path);
    }

    std::string bad_input;
    std::string input_reason;
    if (!validate_collection(request.inputs, metadata.inputs, bad_input, input_reason)) {
        return fail_invocation(result, PCG_ERR_INVALID_ARGUMENT, "incompatible_input",
                               request.operation, input_reason, "inputs." + bad_input);
    }

    GraphNode node;
    node.id = "__script_operation__";
    node.type = request.operation;
    node.data = std::move(normalized_parameters);

    char error_buffer[1024] = {};
    PcgContext ctx;
    ctx.graph_seed = request.context.graph_seed;
    ctx.graph = request.context.graph;
    ctx.node = &node;
    ctx.textures = request.context.textures;
    ctx.meshes = request.context.meshes;
    ctx.splines = request.context.splines;
    ctx.heightfields = request.context.heightfields;
    ctx.inputs = request.inputs;
    ctx.err_buf = error_buffer;
    ctx.err_buf_size = static_cast<int>(sizeof(error_buffer));
    ctx.is_cancel_requested = request.context.is_cancel_requested;
    ctx.dependencies = request.context.dependencies;
    ctx.statistics = &result.statistics;

    const auto start = std::chrono::steady_clock::now();
    PcgResultCode code = PCG_OK;
    try {
        code = element->execute(ctx);
    } catch (const std::exception& exception) {
        code = PCG_ERR_EXECUTION;
        write_error(error_buffer, static_cast<int>(sizeof(error_buffer)), exception.what());
    } catch (...) {
        code = PCG_ERR_EXECUTION;
        write_error(error_buffer, static_cast<int>(sizeof(error_buffer)), "unknown native exception");
    }
    const auto end = std::chrono::steady_clock::now();
    result.statistics["durationMs"] =
        std::chrono::duration<double, std::milli>(end - start).count();

    if (code != PCG_OK) {
        const std::string message = error_buffer[0] != '\0'
            ? std::string(error_buffer) : "native operation failed";
        return fail_invocation(result, code, "operation_failed", request.operation, message);
    }

    if (request.context.is_cancel_requested && request.context.is_cancel_requested()) {
        return fail_invocation(result, PCG_ERR_EXECUTION, "cancelled",
                               request.operation, "operation invocation cancelled during execution");
    }

    std::string bad_output;
    std::string output_reason;
    if (!validate_collection(ctx.outputs, metadata.outputs, bad_output, output_reason)) {
        return fail_invocation(result, PCG_ERR_EXECUTION, "incompatible_output",
                               request.operation, output_reason, "outputs." + bad_output);
    }

    result.outputs = std::move(ctx.outputs);
    result.code = PCG_OK;
    result.diagnostic = nullptr;
    result.statistics["outcome"] = "success";
    result.statistics["outputCount"] = result.outputs.items().size();
    return PCG_OK;
}

} // namespace pcg::internal::scripting
