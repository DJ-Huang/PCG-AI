#include "graph_cook_cache.hpp"

namespace pcg::internal {

void GraphCookCache::clear()
{
    entries_.clear();
    structure_hash_ = 0;
    executed_ = 0;
    skipped_ = 0;
}

bool GraphCookCache::try_get(const std::string& node_id,
                             uint64_t input_hash,
                             data::PcgDataCollection& out_outputs,
                             uint64_t& out_output_hash)
{
    const auto it = entries_.find(node_id);
    if (it == entries_.end() || it->second.input_hash != input_hash) {
        ++executed_;
        return false;
    }

    ++skipped_;
    out_outputs = it->second.outputs;
    out_output_hash = it->second.output_hash;
    return true;
}

void GraphCookCache::put(const std::string& node_id,
                         uint64_t input_hash,
                         uint64_t output_hash,
                         data::PcgDataCollection outputs)
{
    NodeCookEntry entry;
    entry.input_hash = input_hash;
    entry.output_hash = output_hash;
    entry.outputs = std::move(outputs);
    entries_[node_id] = std::move(entry);
}

} // namespace pcg::internal
