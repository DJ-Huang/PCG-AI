#pragma once

#include "data/pcg_data_collection.hpp"
#include "pcg_api.h"
#include "internal/graph_types.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace pcg::internal {

// Sidecar belongs to the producing node, not its geometry. Node cache copies
// retain it, while topology-changing downstream nodes cannot erase the receipt.
inline constexpr const char* kNodeDiagnosticTag = "__pcg_node_diagnostic__";

class CookDiagnostics {
public:
    bool degraded() const { return degraded_; }

    void append(const data::PcgDataCollection& outputs, bool cache_hit)
    {
        const auto* value = outputs.find_json(kNodeDiagnosticTag);
        if (!value || !value->is_object())
            return;
        auto entry = *value;
        entry["cache_hit"] = cache_hit;
        // Do not key by node ID: ForEach may execute one node many times.
        entry["execution_index"] = entries_.size();
        degraded_ = degraded_ || entry.value("fallback_used", false);
        entries_.push_back(std::move(entry));
    }

    void attach(nlohmann::json& result, PcgResultCode code, const Graph& graph, int seed) const
    {
        if (!result.is_object()) {
            auto payload = std::move(result);
            result = nlohmann::json::object();
            if (!payload.is_null()) result["result"] = std::move(payload);
        }
        // Record actual native inputs, not exposed defaults or an editor path.
        // This snapshot is evidence for receipt comparison, not a visual score.
        auto nodes = nlohmann::json::array();
        auto edges = nlohmann::json::array();
        for (const auto& node : graph.nodes)
            nodes.push_back({{"id", node.id}, {"type", node.type}, {"data", node.data}});
        for (const auto& edge : graph.edges)
            edges.push_back({{"source", edge.source}, {"target", edge.target},
                {"sourceHandle", edge.source_handle.empty() ? "out" : edge.source_handle},
                {"targetHandle", edge.target_handle.empty() ? "in" : edge.target_handle}});
        result["evaluation_graph"] = {{"nodes", std::move(nodes)}, {"edges", std::move(edges)}};
        result["evaluation_seed"] = seed;
        result["diagnostics_version"] = 1;
        result["node_diagnostics"] = entries_;
        result["cook_outcome"] = code != PCG_OK ? "failure"
            : (degraded_ ? "degraded" : "success");
        result["fallback_used"] = degraded_;
        // Execution eligibility is NOT a visual-fidelity judgement.
        result["execution_acceptable"] = code == PCG_OK && !degraded_;
    }

private:
    nlohmann::json entries_ = nlohmann::json::array();
    bool degraded_ = false;
};

} // namespace pcg::internal
