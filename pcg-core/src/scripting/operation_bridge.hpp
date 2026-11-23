#pragma once

#include "data/pcg_context.hpp"
#include "pcg_api.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace pcg::internal::scripting {

/**
 * A fail-closed capability classification for invoking registered Core elements
 * outside the visual graph executor. Only explicitly script-callable operations
 * may cross this bridge.
 */
enum class OperationCapability {
    Pure,
    ResourceRead,
    GraphControl,
    HostBound,
    SideEffecting,
};

struct OperationMetadata {
    std::string operation;
    std::string display_name;
    OperationCapability capability = OperationCapability::HostBound;
    bool script_callable = false;
    bool cancellation_supported = false;
    std::vector<std::string> resource_dependencies;
    nlohmann::json parameters = nlohmann::json::object();
    nlohmann::json inputs = nlohmann::json::array();
    nlohmann::json outputs = nlohmann::json::array();
};

/** Context inherited from the owning Cook/script evaluation. */
struct OperationInvocationContext {
    int graph_seed = 0;
    const Graph* graph = nullptr;
    const TextureRuntime* textures = nullptr;
    const MeshRuntime* meshes = nullptr;
    const SplineRuntime* splines = nullptr;
    const HeightFieldRuntime* heightfields = nullptr;
    const nlohmann::json* dependencies = nullptr;
    bool (*is_cancel_requested)() = nullptr;
};

struct OperationInvocationRequest {
    std::string operation;
    nlohmann::json parameters = nlohmann::json::object();
    data::PcgDataCollection inputs;
    OperationInvocationContext context;
};

struct OperationInvocationResult {
    PcgResultCode code = PCG_OK;
    data::PcgDataCollection outputs;
    /** Stable machine-readable diagnostic object, null on success. */
    nlohmann::json diagnostic = nullptr;
    /** Per-invocation statistics; native operations may append their own fields. */
    nlohmann::json statistics = nlohmann::json::object();
};

/** Metadata for one registered operation. Unclassified operations are deny-by-default. */
OperationMetadata operation_metadata(const std::string& operation);

/** Discovery contract consumed by script hosts/tooling. */
nlohmann::json operation_discovery_json();

/** Generated JavaScript/TypeScript declaration surface for the generic bridge. */
std::string operation_declarations();

/**
 * Invoke one registered native operation through the script-safe contract.
 * Defaults are normalized from node-manifest metadata and execution still goes
 * through the same IPcgElement implementation used by visual graph nodes.
 */
PcgResultCode invoke_operation(const OperationInvocationRequest& request,
                               OperationInvocationResult& result);

} // namespace pcg::internal::scripting
