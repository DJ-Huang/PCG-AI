#pragma once

// Canonical polygon geometry transport (Houdini GU_Detail analogue).
// Triangulation is deferred to Sink / explicit Triangulate nodes.

#include "data/pcg_mesh_data.hpp"
#include "geometry/group_table.hpp"

#include <vector>

namespace pcg::internal::data {

struct PcgVec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

enum class ShadeMode {
    Auto,
    Smooth,
    Flat,
};

struct GeometryDetailMeta {
    ShadeMode shade_mode = ShadeMode::Auto;
    double cusp_angle_deg = 30.0;
};

struct NormalComputeOptions {
    ShadeMode shade_mode = ShadeMode::Auto;
    double cusp_angle_deg = 30.0;
    bool hard_group_boundaries = true;
};

class PcgGeometry {
public:
    std::vector<PcgVec3>& points_mut() { return points_; }
    const std::vector<PcgVec3>& points() const { return points_; }
    std::vector<std::vector<int>>& faces_mut() { return faces_; }
    const std::vector<std::vector<int>>& faces() const { return faces_; }
    geometry::GroupTable& groups() { return groups_; }
    const geometry::GroupTable& groups() const { return groups_; }
    GeometryDetailMeta& detail() { return detail_; }
    const GeometryDetailMeta& detail() const { return detail_; }

private:
    std::vector<PcgVec3> points_;
    std::vector<std::vector<int>> faces_;
    geometry::GroupTable groups_;
    GeometryDetailMeta detail_;
};

/// Fan-triangulate n-gon faces for display / legacy mesh nodes.
PcgMeshData triangulate_geometry(const PcgGeometry& geometry);

/// Triangulate with shared vertices (preserves topology for manifold checks).
PcgMeshData triangulate_geometry_shared(const PcgGeometry& geometry);

/// Triangulate with vertex split and per-vertex normals based on shade policy.
/// Triangle order matches triangulate_geometry_shared (build_group_stats compatible).
PcgMeshData compute_split_normals(const PcgGeometry& geometry, const NormalComputeOptions& options);

/// Rebuild polygon topology from triangle soup (weld + coplanar merge via BMesh).
PcgGeometry geometry_from_mesh(const PcgMeshData& mesh);

/// Enumerate manifold edges; boundary edges have face1 < 0.
std::vector<int64_t> geometry_edge_keys(const PcgGeometry& geometry);

/// Maintain or create the named edge group for boundary (unshared) edges.
void maintain_unshared_edge_group(PcgGeometry& geometry, const std::string& name = "unshared");

/// Concatenate geometries; group names are unioned with optional prefix on the right operand.
PcgGeometry merge_geometries(const PcgGeometry& a, const PcgGeometry& b, const std::string& b_prefix = "");

} // namespace pcg::internal::data
