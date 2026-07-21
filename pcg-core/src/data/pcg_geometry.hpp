#pragma once

// Canonical polygon geometry transport (Houdini GU_Detail analogue).
// Triangulation is deferred to Sink / explicit Triangulate nodes.

#include "data/pcg_mesh_data.hpp"
#include "data/pcg_attribute_table.hpp"
#include "geometry/group_table.hpp"

#include <array>
#include <unordered_set>
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

struct GeometryElementRemap {
    // Destination element index -> source element index. -1 means generated.
    std::vector<int> points;
    std::vector<int> vertices;
    std::vector<int> primitives;
};

struct GeometryAffineTransform {
    // Row-major 3x3 linear transform followed by translation.
    std::array<double, 9> linear{1.0, 0.0, 0.0,
                                 0.0, 1.0, 0.0,
                                 0.0, 0.0, 1.0};
    PcgVec3 translation{};
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
    AttributeTable& attributes() { return attributes_; }
    const AttributeTable& attributes() const { return attributes_; }

    AttributeCounts attribute_counts() const
    {
        return {points_.size(), static_cast<size_t>(corner_count()), faces_.size(), 1};
    }
    bool validate_attributes(std::string* error = nullptr) const
    {
        return attributes_.validate(attribute_counts(), error);
    }

    bool has_colors() const { return has_colors_; }
    const std::vector<PcgColor>& colors() const { return colors_; }
    void set_colors(std::vector<PcgColor> c) { colors_ = std::move(c); has_colors_ = true; }

    bool has_uvs() const { return has_uvs_; }
    const std::vector<PcgVec2>& uvs() const { return uvs_; }
    void set_uvs(std::vector<PcgVec2> u) { uvs_ = std::move(u); has_uvs_ = true; }

    /// Corner (vertex/face-corner) UV — render source of truth (Houdini vertex UV).
    /// Size must equal total corner count (sum of face sizes). Point UV remains a
    /// continuous intermediate field; Sink prefers corner UV when present.
    bool has_corner_uvs() const { return has_corner_uvs_; }
    const std::vector<PcgVec2>& corner_uvs() const { return corner_uvs_; }
    void set_corner_uvs(std::vector<PcgVec2> u);
    int corner_count() const;
    /// Expand per-point UV onto every face corner that references that point.
    void expand_point_uvs_to_corners();

    bool has_material() const { return has_material_; }
    const std::string& material_name() const { return material_name_; }
    void set_material_name(std::string m);
    bool has_face_materials() const { return face_materials_.size() == faces_.size(); }
    const std::vector<std::string>& face_materials() const { return face_materials_; }
    std::vector<std::string>& face_materials_mut();
    void set_face_materials(std::vector<std::string> materials);

private:
    std::vector<PcgVec3> points_;
    std::vector<std::vector<int>> faces_;
    geometry::GroupTable groups_;
    GeometryDetailMeta detail_;
    AttributeTable attributes_;
    std::vector<PcgColor> colors_;
    bool has_colors_ = false;
    std::vector<PcgVec2> uvs_;
    bool has_uvs_ = false;
    std::vector<PcgVec2> corner_uvs_;
    bool has_corner_uvs_ = false;
    std::string material_name_;
    bool has_material_ = false;
    std::vector<std::string> face_materials_;
};

/// Triangulate one simple polygon and return local corner indices. Uses a
/// constrained Delaunay triangulation so concave n-gons are handled correctly.
/// Invalid/self-intersecting input falls back to the stable legacy fan.
std::vector<std::array<int, 3>> triangulate_face_corners(
    const std::vector<PcgVec3>& points, const std::vector<int>& face);

/// Triangulate polygon faces for display / legacy mesh nodes.
PcgMeshData triangulate_geometry(const PcgGeometry& geometry);

/// Triangulate with shared vertices (preserves topology for manifold checks).
PcgMeshData triangulate_geometry_shared(const PcgGeometry& geometry);

/// Triangulate with vertex split and per-vertex normals based on shade policy.
/// Triangle order matches triangulate_geometry_shared (build_group_stats compatible).
PcgMeshData compute_split_normals(const PcgGeometry& geometry, const NormalComputeOptions& options);

/// Rebuild polygon topology from triangle soup (weld + coplanar merge via BMesh).
PcgGeometry geometry_from_mesh(const PcgMeshData& mesh);

/// Propagate attributes, fixed channels, materials, and groups through a topology edit.
/// Destination topology must already be populated and remap sizes must match its domains.
void propagate_geometry_data(const PcgGeometry& source,
                             PcgGeometry& destination,
                             const GeometryElementRemap& remap);
PcgVec3 transform_position(const GeometryAffineTransform& transform, const PcgVec3& value);
PcgVec3 transform_vector(const GeometryAffineTransform& transform, const PcgVec3& value);
PcgVec3 transform_normal(const GeometryAffineTransform& transform, const PcgVec3& value);
void transform_geometry_attributes(PcgGeometry& geometry,
                                   const GeometryAffineTransform& transform);

/// Enumerate manifold edges; boundary edges have face1 < 0.
std::vector<int64_t> geometry_edge_keys(const PcgGeometry& geometry);

/// Maintain or create the named edge group for boundary (unshared) edges.
void maintain_unshared_edge_group(PcgGeometry& geometry, const std::string& name = "unshared");

/// Concatenate geometries; group names are unioned with optional prefix on the right operand.
PcgGeometry merge_geometries(const PcgGeometry& a, const PcgGeometry& b, const std::string& b_prefix = "");

/// Extract a subset of faces into a new geometry (points remapped; attrs/groups
/// propagated). Face indices outside [0, faces) are ignored.
PcgGeometry extract_faces(const PcgGeometry& geometry,
                          const std::unordered_set<int>& face_indices);

/// Split a geometry into independent bevel shells. Uses primitive int attributes
/// (e.g. lotid) when present; otherwise edge-connected face components. Boundary
/// vertices/edges between clusters are duplicated so bevel cannot couple shells.
std::vector<PcgGeometry> partition_geometry_bevel_shells(const PcgGeometry& geometry);

} // namespace pcg::internal::data
