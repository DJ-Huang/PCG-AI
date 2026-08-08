#pragma once

#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_point_data.hpp"

#include <string>
#include <vector>

namespace pcg::internal::elements {

struct SampleMeshSurfaceOptions {
    int count = 100;
    int seed = 0;
    double normal_offset = 0.0;
    double looseness = 0.0;
    /// Houdini Scatter SOP Group: primitives to sample. Empty = all faces.
    std::string face_group;
    /// Faces to exclude from the sampling pool (BevelMesh-style name list).
    std::vector<std::string> exclude_groups;
    /// Reject samples closer than this to the sampling-face-group boundary
    /// (Houdini: Labs Distance From Border + density hard cutoff).
    double edge_margin = 0.0;
    bool (*is_cancel_requested)() = nullptr;
};

/** Area-weighted random points on mesh triangles with face normal metadata (nx, ny, nz, triIndex).
 *  face_group / exclude_groups are ignored (no face groups on triangle soup).
 *  edge_margin uses unshared edges of the triangle mesh. */
data::PcgPointData sample_mesh_surface(const data::PcgMeshData& mesh,
                                       const SampleMeshSurfaceOptions& options);

/** Geometry-aware sampling: faceGroup filter + group-boundary edgeMargin. */
data::PcgPointData sample_mesh_surface(const data::PcgGeometry& geometry,
                                       const SampleMeshSurfaceOptions& options);

struct PointRelaxOptions {
    int max_iterations = 50;
    double radius = 1.0;
    bool use_pscale = true;
    bool (*is_cancel_requested)() = nullptr;
};

/// Generalized Lloyd's relaxation: iteratively push apart points whose
/// spherical radii overlap, constrained to the surface plane defined by
/// per-point normal attributes (nx, ny, nz) when present.
/// Radii are read from the "pscale" attribute when use_pscale is true,
/// falling back to the uniform radius parameter.
data::PcgPointData relax_points(const data::PcgPointData& input,
                                const PointRelaxOptions& options);

struct PointsFromVolumeOptions {
    double point_separation = 0.5;
    double jitter = 0.0;
    int seed = 0;
    bool shell_only = false;
    bool (*is_cancel_requested)() = nullptr;
};

/// Generate points inside a mesh volume by voxelizing the AABB and testing
/// voxel-center containment via ray casting. shell_only restricts output
/// to voxels adjacent to the mesh surface.
data::PcgPointData sample_mesh_volume(const data::PcgMeshData& mesh,
                                       const PointsFromVolumeOptions& options);

data::PcgPointData sample_mesh_volume(const data::PcgGeometry& geometry,
                                       const PointsFromVolumeOptions& options);

} // namespace pcg::internal::elements
