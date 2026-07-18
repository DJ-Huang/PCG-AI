#pragma once

#include "data/pcg_heightfield.hpp"

#include <string>
#include <unordered_map>

namespace pcg::internal {

/** Runtime height-field slots keyed by GetTerrainData node id. */
class HeightFieldRuntime {
public:
    void clear();
    void add_slot(std::string slot_id, data::PcgHeightField heightfield);
    const data::PcgHeightField* find(const std::string& slot_id) const;

private:
    std::unordered_map<std::string, data::PcgHeightField> slots_;
};

} // namespace pcg::internal
