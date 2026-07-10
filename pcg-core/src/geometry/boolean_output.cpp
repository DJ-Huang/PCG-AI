// Boolean output implementation: detriangulation and group finalization.

#include "geometry/boolean_output.hpp"
#include "geometry/bmesh.hpp"

namespace pcg::internal::geometry {

data::PcgGeometry finalize_boolean_output(const BooleanResult& result,
                                          DetriangulateMode mode)
{
    if (result.error != BooleanErrorType::Ok)
        return result.geometry;

    if (mode == DetriangulateMode::None)
        return result.geometry;

    // Use BMesh to merge coplanar triangles
    BMeshBuildOptions opts;
    if (mode == DetriangulateMode::All) {
        opts.merge_coplanar_angle_deg = 2.0;
    } else {
        // Unchanged: only merge non-seam triangles
        opts.merge_coplanar_angle_deg = 2.0;
    }

    BMesh bm = bmesh_from_geometry(result.geometry, opts);
    data::PcgGeometry out = geometry_from_bmesh(bm);

    // Preserve groups from the input
    out.groups() = result.geometry.groups();

    return out;
}

} // namespace pcg::internal::geometry
