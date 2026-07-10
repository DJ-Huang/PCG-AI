#pragma once

// Blender-aligned boolean CSG: patch/cell graph, BFS winding propagation, extraction.
// Reference: Blender mesh_boolean.cc (Zhou et al. Mesh Arrangements).

#include "geometry/imesh.hpp"
#include "geometry/mesh_topology.hpp"
#include "geometry/arrangement.hpp"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pcg::internal::geometry {

constexpr int kNoIndex = -1;

struct Patch {
    std::vector<int> tris;
    int cell_above = kNoIndex;
    int cell_below = kNoIndex;

    void add_tri(int t) { tris.push_back(t); }
    int rep_tri() const { return tris.empty() ? kNoIndex : tris[0]; }
};

struct Cell {
    std::vector<int> patches;
    std::vector<int> winding; ///< per-operand winding numbers
    bool winding_assigned = false;
    bool in_output_volume = false;
    bool zero_volume = false;
    int merged_to = kNoIndex;

    void add_patch(int p) {
        for (int x : patches) if (x == p) return;
        patches.push_back(p);
    }

    int patch_other(int p) const {
        if (patches.size() != 2) return kNoIndex;
        return patches[0] == p ? patches[1] : patches[0];
    }

    int merged_target() const {
        return merged_to == kNoIndex ? kNoIndex : merged_to;
    }
};

struct PatchesInfo {
    explicit PatchesInfo(int ntri) : tri_to_patch(static_cast<size_t>(ntri), kNoIndex) {}

    std::vector<Patch> patches;
    std::vector<int> tri_to_patch;
    std::unordered_map<int64_t, TopoEdge> patch_patch_edges;

    int add_patch() {
        patches.emplace_back();
        return static_cast<int>(patches.size()) - 1;
    }

    void grow_patch(int patch_index, int t) {
        tri_to_patch[static_cast<size_t>(t)] = patch_index;
        patches[patch_index].add_tri(t);
    }

    bool tri_assigned(int t) const { return tri_to_patch[static_cast<size_t>(t)] != kNoIndex; }

    int patch_of_tri(int t) const { return tri_to_patch[static_cast<size_t>(t)]; }

    Patch& patch(int p) { return patches[p]; }
    const Patch& patch(int p) const { return patches[p]; }

    int tot_patch() const { return static_cast<int>(patches.size()); }

    void add_patch_patch_edge(int p1, int p2, const TopoEdge& e) {
        const int64_t k = (static_cast<int64_t>(std::min(p1, p2)) << 32) |
                          static_cast<uint32_t>(std::max(p1, p2));
        patch_patch_edges[k] = e;
    }

    TopoEdge patch_patch_edge(int p1, int p2) const {
        const int64_t k = (static_cast<int64_t>(std::min(p1, p2)) << 32) |
                          static_cast<uint32_t>(std::max(p1, p2));
        auto it = patch_patch_edges.find(k);
        if (it == patch_patch_edges.end()) return {};
        return it->second;
    }
};

struct CellsInfo {
    std::vector<Cell> cells;

    int add_cell() {
        cells.emplace_back();
        return static_cast<int>(cells.size()) - 1;
    }

    Cell& cell(int c) { return cells[c]; }
    const Cell& cell(int c) const { return cells[c]; }

    int tot_cell() const { return static_cast<int>(cells.size()); }

    void init_windings(int nshapes) {
        for (auto& c : cells) {
            c.winding.assign(static_cast<size_t>(nshapes), 0);
        }
    }
};

/// Build patch graph from subdivided IMesh.
PatchesInfo find_patches(const IMesh& mesh, const MeshTriTopology& topo);

/// Partition space into cells; sets patch.cell_above / cell_below.
CellsInfo find_cells(const IMesh& mesh, const MeshTriTopology& topo, PatchesInfo& pinfo);

/// Find exterior (ambient) cell index.
int find_ambient_cell(const IMesh& mesh, const PatchesInfo& pinfo);

/// BFS propagate winding numbers from ambient cell; sets in_output_volume.
void propagate_windings(const IMesh& mesh, CellsInfo& cinfo, PatchesInfo& pinfo, int ambient_cell,
                        BooleanOp op, int nshapes,
                        const std::function<int(int)>& shape_fn,
                        const std::vector<int>& shape_ambient);

/// Connect disconnected/nested patch components (Blender finish_patch_cell_graph).
void finish_patch_cell_graph(const IMesh& mesh, CellsInfo& cinfo, PatchesInfo& pinfo,
                             const MeshTriTopology& topo);

/// Extract output geometry from cells marked in_output_volume.
data::PcgGeometry extract_boolean_geometry(const IMesh& mesh, const PatchesInfo& pinfo,
                                           const CellsInfo& cinfo, BooleanOp op);

/// Full CSG pipeline on already-subdivided IMesh.
data::PcgGeometry classify_and_extract(const IMesh& combined, BooleanOp op);

} // namespace pcg::internal::geometry
