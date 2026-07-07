#pragma once

#include "data/pcg_mesh_data.hpp"
#include "data/pcg_point_data.hpp"

namespace pcg::internal::elements {

struct SampleMeshSurfaceOptions {
    int count = 100;
    int seed = 0;
    double normal_offset = 0.0;
    double looseness = 0.0;
};

/** Area-weighted random points on mesh triangles with face normal metadata (nx, ny, nz, triIndex). */
data::PcgPointData sample_mesh_surface(const data::PcgMeshData& mesh,
                                       const SampleMeshSurfaceOptions& options);

} // namespace pcg::internal::elements
