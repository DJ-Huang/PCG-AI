#pragma once

// IMesh: Intermediate mesh for boolean operations.
// Triangulated mesh with quantized coordinates and source tracking.

#include "geometry/bmesh.hpp"
#include "data/pcg_geometry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pcg::internal::geometry {

struct Mat4 {
    double m[4][4];
    static Mat4 identity();
    Vec3 transform_point(const Vec3& v) const;
};

struct IMeshVert {
    Vec3 co;           // double (display / output)
    int64_t qx = 0, qy = 0, qz = 0; // quantized integers (exact calc)
    int orig_point = -1; // original PcgGeometry point index
};

struct IMeshTri {
    int v0 = 0, v1 = 0, v2 = 0;
    int source = 0;       // 0=A, 1=B
    int orig_face = -1;   // original face index
    int parent_tri = -1;  // -1 = original triangle
    bool split_by_seam = false; // detriangulate=unchanged uses this
    bool is_coplanar = false;    // true if produced by coplanar partition
    bool coplanar_inside = false; // if is_coplanar: true=inside other operand
};

struct IntersectEdgeRecord {
    int64_t edge_key = 0;
    int tri_a = -1;
    int tri_b = -1;
};

struct IMesh {
    std::vector<IMeshVert> verts;
    std::vector<IMeshTri> tris;
    std::vector<IntersectEdgeRecord> seam_edges;
    double quantize_scale = 1e6; ///< Scale used for quantization (set by from_geometry)

    /// Build IMesh from canonical geometry.
    /// Triangulates polygons, welds vertices, applies transform, and quantizes.
    static IMesh from_geometry(const data::PcgGeometry& geo,
                               int operand_index,
                               const Mat4& xform,
                               double quantize_scale);

    /// Compute AABB of all triangles.
    void compute_aabb(Vec3& out_min, Vec3& out_max) const;

    /// Find or insert a vertex by 3D position (weld using quantize_scale).
    int find_or_insert_vert(const Vec3& pos, double weld_eps);
};

/// Compute an adaptive quantize scale based on geometry extent.
/// Ensures int64 arithmetic won't overflow for building-scale geometry.
double quantize_scale_for_extent(double max_extent);

/// Quantize a double coordinate to int64.
int64_t quantize_coord(double val, double scale);

} // namespace pcg::internal::geometry
