#pragma once

#include "data/pcg_spline_data.hpp"

#include <string>
#include <unordered_map>

namespace pcg::internal {

/** Runtime spline slots keyed by GetSplineData node id (filled by pcg_execute_graph_v7). */
class SplineRuntime {
public:
    void clear();
    void add_slot(std::string slot_id, data::PcgSplineData splines);
    const data::PcgSplineData* find(const std::string& slot_id) const;

private:
    std::unordered_map<std::string, data::PcgSplineData> slots_;
};

} // namespace pcg::internal
