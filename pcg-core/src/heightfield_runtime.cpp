#include "heightfield_runtime.hpp"

namespace pcg::internal {

void HeightFieldRuntime::clear()
{
    slots_.clear();
}

void HeightFieldRuntime::add_slot(std::string slot_id, data::PcgHeightField heightfield)
{
    if (slot_id.empty() || !heightfield.valid())
        return;
    slots_[std::move(slot_id)] = std::move(heightfield);
}

const data::PcgHeightField* HeightFieldRuntime::find(const std::string& slot_id) const
{
    const auto it = slots_.find(slot_id);
    return it == slots_.end() ? nullptr : &it->second;
}

} // namespace pcg::internal
