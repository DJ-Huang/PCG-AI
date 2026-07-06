#include "texture_runtime.hpp"

namespace pcg::internal {

void TextureRuntime::clear()
{
    slots_.clear();
}

void TextureRuntime::add_slot(std::string slot_id, data::PcgTextureData texture)
{
    if (slot_id.empty() || texture.empty())
        return;
    slots_[std::move(slot_id)] = std::move(texture);
}

const data::PcgTextureData* TextureRuntime::find(const std::string& slot_id) const
{
    const auto it = slots_.find(slot_id);
    if (it == slots_.end() || it->second.empty())
        return nullptr;
    return &it->second;
}

} // namespace pcg::internal
