#pragma once

#include "data/pcg_texture_data.hpp"

#include <string>
#include <unordered_map>

namespace pcg::internal {

/** Runtime texture slots keyed by ImageTexture node id (filled by pcg_execute_graph_v3). */
class TextureRuntime {
public:
    void clear();
    void add_slot(std::string slot_id, data::PcgTextureData texture);
    const data::PcgTextureData* find(const std::string& slot_id) const;

private:
    std::unordered_map<std::string, data::PcgTextureData> slots_;
};

} // namespace pcg::internal
