#include "spline_runtime.hpp"

namespace pcg::internal {

void SplineRuntime::clear()
{
    slots_.clear();
}

void SplineRuntime::add_slot(std::string slot_id, data::PcgSplineData splines)
{
    if (slot_id.empty())
        return;
    slots_[std::move(slot_id)] = std::move(splines);
}

const data::PcgSplineData* SplineRuntime::find(const std::string& slot_id) const
{
    const auto it = slots_.find(slot_id);
    return it == slots_.end() ? nullptr : &it->second;
}

} // namespace pcg::internal
