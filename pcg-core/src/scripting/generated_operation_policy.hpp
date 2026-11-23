#pragma once

#include <cstddef>

namespace pcg::internal::scripting::generated {

struct OperationPolicyEntry {
    const char* operation;
    const char* capability;
    bool script_callable;
    bool cancellation_supported;
    const char* const* resource_dependencies;
    std::size_t resource_dependency_count;
};

inline constexpr const char* kDefaultCapability = "host-bound";
inline constexpr bool kDefaultScriptCallable = false;
inline constexpr bool kDefaultCancellationSupported = false;

inline constexpr const char* kImportMeshResources[] = {"mesh-runtime"};

inline constexpr OperationPolicyEntry kOperationPolicies[] = {
    {"AssignMaterial", "pure", true, false, nullptr, 0},
    {"BevelMesh", "pure", true, true, nullptr, 0},
    {"BooleanMesh", "pure", true, true, nullptr, 0},
    {"CreateBoxMesh", "pure", true, false, nullptr, 0},
    {"CreateCylinderMesh", "pure", true, false, nullptr, 0},
    {"CreateGridMesh", "pure", true, false, nullptr, 0},
    {"CustomFunction", "graph-control", false, false, nullptr, 0},
    {"ForEachBegin", "graph-control", false, false, nullptr, 0},
    {"ForEachEnd", "graph-control", false, false, nullptr, 0},
    {"ImportMesh", "resource-read", false, false, kImportMeshResources, 1},
    {"MergeMesh", "pure", true, false, nullptr, 0},
    {"Output", "graph-control", false, false, nullptr, 0},
    {"PlaceInScene", "side-effecting", false, false, nullptr, 0},
    {"PolyExtrude", "pure", true, false, nullptr, 0},
    {"SubdivideMesh", "pure", true, false, nullptr, 0},
    {"TransformMesh", "pure", true, false, nullptr, 0},
};

inline constexpr std::size_t kOperationPolicyCount =
    sizeof(kOperationPolicies) / sizeof(kOperationPolicies[0]);

} // namespace pcg::internal::scripting::generated
