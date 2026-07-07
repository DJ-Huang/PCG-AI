#include "mesh_runtime.hpp"

namespace pcg::internal {

void MeshRuntime::clear()
{
    slots_.clear();
}

void MeshRuntime::add_slot(std::string slot_id, data::PcgMeshData mesh)
{
    slots_[std::move(slot_id)] = std::move(mesh);
}

const data::PcgMeshData* MeshRuntime::find(const std::string& slot_id) const
{
    const auto it = slots_.find(slot_id);
    return it == slots_.end() ? nullptr : &it->second;
}

} // namespace pcg::internal
