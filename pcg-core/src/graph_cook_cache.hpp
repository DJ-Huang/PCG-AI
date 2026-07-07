#pragma once

#include "data/pcg_data_collection.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace pcg::internal {

/** Per-node cook cache entry (Blender modifier-stack memoization). */
struct NodeCookEntry {
    uint64_t input_hash = 0;
    uint64_t output_hash = 0;
    data::PcgDataCollection outputs;
};

/** Session-scoped graph cook cache with topology fingerprint invalidation. */
class GraphCookCache {
public:
    void clear();

    uint64_t structure_hash() const { return structure_hash_; }
    void set_structure_hash(uint64_t hash) { structure_hash_ = hash; }

    bool try_get(const std::string& node_id,
                 uint64_t input_hash,
                 data::PcgDataCollection& out_outputs,
                 uint64_t& out_output_hash);

    void put(const std::string& node_id,
             uint64_t input_hash,
             uint64_t output_hash,
             data::PcgDataCollection outputs);

    int nodes_executed() const { return executed_; }
    int nodes_skipped() const { return skipped_; }
    void reset_stats() { executed_ = 0; skipped_ = 0; }

private:
    std::unordered_map<std::string, NodeCookEntry> entries_;
    uint64_t structure_hash_ = 0;
    int executed_ = 0;
    int skipped_ = 0;
};

} // namespace pcg::internal
