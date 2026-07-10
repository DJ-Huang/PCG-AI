// IMesh implementation: intermediate mesh for boolean operations.

#include "geometry/imesh.hpp"
#include "geometry/bmesh.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace pcg::internal::geometry {

// ── Mat4 ───────────────────────────────────────────────────

Mat4 Mat4::identity()
{
    Mat4 r{};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0;
    return r;
}

Vec3 Mat4::transform_point(const Vec3& v) const
{
    const double x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3];
    const double y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3];
    const double z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3];
    const double w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3];
    if (w != 0.0 && w != 1.0) {
        return {x / w, y / w, z / w};
    }
    return {x, y, z};
}

// ── Quantization ───────────────────────────────────────────

double quantize_scale_for_extent(double max_extent)
{
    // Choose scale so that quantized coordinates fit comfortably in int32 range,
    // leaving headroom for int64 determinant computation.
    // Target: max quantized value ≈ 2^30 ≈ 1e9
    if (max_extent < 1e-12) return 1e6;
    return 1e9 / max_extent;
}

int64_t quantize_coord(double val, double scale)
{
    return static_cast<int64_t>(std::llround(val * scale));
}

// ── IMesh ──────────────────────────────────────────────────

IMesh IMesh::from_geometry(const data::PcgGeometry& geo,
                           int operand_index,
                           const Mat4& xform,
                           double quantize_scale)
{
    IMesh mesh;

    mesh.quantize_scale = quantize_scale;

    // Weld vertices by quantized position
    struct QuantVert {
        int64_t qx, qy, qz;
        bool operator==(const QuantVert& o) const {
            return qx == o.qx && qy == o.qy && qz == o.qz;
        }
    };
    struct QuantVertHash {
        size_t operator()(const QuantVert& v) const {
            size_t h = std::hash<int64_t>{}(v.qx);
            h ^= std::hash<int64_t>{}(v.qy) + 0x9e3779b9 + (h << 6) + (h >> 2);
            h ^= std::hash<int64_t>{}(v.qz) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }
    };
    std::unordered_map<QuantVert, int, QuantVertHash> weld_map;

    auto add_vert = [&](const Vec3& co, int orig_point) -> int {
        IMeshVert v;
        v.co = xform.transform_point(co);
        v.qx = quantize_coord(v.co.x, quantize_scale);
        v.qy = quantize_coord(v.co.y, quantize_scale);
        v.qz = quantize_coord(v.co.z, quantize_scale);
        v.orig_point = orig_point;

        QuantVert qv{v.qx, v.qy, v.qz};
        auto it = weld_map.find(qv);
        if (it != weld_map.end()) {
            return it->second;
        }
        int idx = static_cast<int>(mesh.verts.size());
        mesh.verts.push_back(v);
        weld_map[qv] = idx;
        return idx;
    };

    // Fan-triangulate each n-gon face
    const auto& points = geo.points();
    const auto& faces = geo.faces();
    for (int fi = 0; fi < static_cast<int>(faces.size()); ++fi) {
        const auto& face = faces[fi];
        if (face.size() < 3) continue;

        // Add vertices for this face
        std::vector<int> face_verts;
        face_verts.reserve(face.size());
        for (int pi : face) {
            if (pi < 0 || pi >= static_cast<int>(points.size())) continue;
            const auto& p = points[pi];
            Vec3 co{p.x, p.y, p.z};
            face_verts.push_back(add_vert(co, pi));
        }

        if (face_verts.size() < 3) continue;

        // Fan triangulate from vertex 0
        for (size_t i = 1; i + 1 < face_verts.size(); ++i) {
            IMeshTri tri;
            tri.v0 = face_verts[0];
            tri.v1 = face_verts[i];
            tri.v2 = face_verts[i + 1];
            tri.source = operand_index;
            tri.orig_face = fi;
            tri.parent_tri = -1;
            tri.split_by_seam = false;
            mesh.tris.push_back(tri);
        }
    }

    return mesh;
}

void IMesh::compute_aabb(Vec3& out_min, Vec3& out_max) const
{
    out_min = {1e30, 1e30, 1e30};
    out_max = {-1e30, -1e30, -1e30};
    for (const auto& v : verts) {
        out_min.x = std::min(out_min.x, v.co.x);
        out_min.y = std::min(out_min.y, v.co.y);
        out_min.z = std::min(out_min.z, v.co.z);
        out_max.x = std::max(out_max.x, v.co.x);
        out_max.y = std::max(out_max.y, v.co.y);
        out_max.z = std::max(out_max.z, v.co.z);
    }
}

int IMesh::find_or_insert_vert(const Vec3& pos, double weld_eps)
{
    // Use the same quantize_scale as from_geometry for consistent welding
    const int64_t qx = quantize_coord(pos.x, quantize_scale);
    const int64_t qy = quantize_coord(pos.y, quantize_scale);
    const int64_t qz = quantize_coord(pos.z, quantize_scale);

    for (int i = 0; i < static_cast<int>(verts.size()); ++i) {
        if (verts[i].qx == qx && verts[i].qy == qy && verts[i].qz == qz) {
            return i;
        }
    }

    // Also try fuzzy match within weld_eps for floating-point intersection points
    // that may not quantize exactly the same as existing vertices
    for (int i = 0; i < static_cast<int>(verts.size()); ++i) {
        double dx = verts[i].co.x - pos.x;
        double dy = verts[i].co.y - pos.y;
        double dz = verts[i].co.z - pos.z;
        if (dx*dx + dy*dy + dz*dz < weld_eps * weld_eps) {
            return i;
        }
    }

    IMeshVert v;
    v.co = pos;
    v.qx = qx;
    v.qy = qy;
    v.qz = qz;
    v.orig_point = -1;
    int idx = static_cast<int>(verts.size());
    verts.push_back(v);
    return idx;
}

} // namespace pcg::internal::geometry
