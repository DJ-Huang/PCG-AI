#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pcg::internal {

struct NodePerfEntry {
    std::string id;
    std::string type;
    double ms = 0.0;
    bool cached = false;
};

/** Collects per-node timings during graph execution. */
class GraphPerfReport {
public:
    void clear() { entries_.clear(); }

    void add(const std::string& id, const std::string& type, double ms, bool cached)
    {
        entries_.push_back(NodePerfEntry{id, type, ms, cached});
    }

    const std::vector<NodePerfEntry>& entries() const { return entries_; }

private:
    std::vector<NodePerfEntry> entries_;
};

} // namespace pcg::internal
