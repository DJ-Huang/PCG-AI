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

    if (result.face_origins.size() != result.geometry.faces().size())
        return result.geometry;

    // Houdini Boolean SOP semantics: only neighboring triangles originating
    // from the same input polygon may be reconstructed. "Unchanged" further
    // excludes every source polygon touched by an intersection split.
    BMeshBuildOptions opts;
    opts.merge_coplanar_angle_deg = 0.0;
    opts.preserve_grouped_edges = true;
    opts.face_merge_keys.reserve(result.face_origins.size());
    for (const BooleanFaceOrigin& origin : result.face_origins) {
        if (origin.source < 0 || origin.original_face < 0 ||
            (mode == DetriangulateMode::Unchanged && origin.changed)) {
            opts.face_merge_keys.push_back(-1);
            continue;
        }
        opts.face_merge_keys.push_back(
            (static_cast<int64_t>(origin.source) << 32) |
            static_cast<uint32_t>(origin.original_face));
    }

    BMesh bm = bmesh_from_geometry(result.geometry, opts);
    data::PcgGeometry out = geometry_from_bmesh(bm);
    out.detail() = result.geometry.detail();
    if (result.geometry.has_colors())
        out.set_colors(result.geometry.colors());
    if (result.geometry.has_uvs())
        out.set_uvs(result.geometry.uvs());

    return out;
}

} // namespace pcg::internal::geometry
