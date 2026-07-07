#pragma once

#include "data/pcg_mesh_data.hpp"

#include <string>
#include <unordered_map>

namespace pcg::internal {

/** Runtime mesh slots keyed by GetMeshData node id (filled by pcg_execute_graph_v4). */
class MeshRuntime {
public:
    void clear();
    void add_slot(std::string slot_id, data::PcgMeshData mesh);
    const data::PcgMeshData* find(const std::string& slot_id) const;

private:
    std::unordered_map<std::string, data::PcgMeshData> slots_;
};

} // namespace pcg::internal
