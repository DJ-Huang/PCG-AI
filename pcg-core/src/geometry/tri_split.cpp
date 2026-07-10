// Triangle splitting implementation.
// Handles all cases: edge-to-edge crossing, vertex-to-edge, vertex-to-vertex.

#include "geometry/tri_split.hpp"

#include <CDT.h>

#include <algorithm>
#include <cmath>

namespace pcg::internal::geometry {

bool point_in_triangle_3d(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c)
{
    double u, v, w;
    return barycentric(p, a, b, c, u, v, w);
}

bool barycentric(const Vec3& p, const Vec3& a, const Vec3& b, const Vec3& c,
                 double& u, double& v, double& w)
{
    const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
    const Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
    const Vec3 ap{p.x - a.x, p.y - a.y, p.z - a.z};

    const double d00 = ab.x * ab.x + ab.y * ab.y + ab.z * ab.z;
    const double d01 = ab.x * ac.x + ab.y * ac.y + ab.z * ac.z;
    const double d11 = ac.x * ac.x + ac.y * ac.y + ac.z * ac.z;
    const double d20 = ap.x * ab.x + ap.y * ab.y + ap.z * ab.z;
    const double d21 = ap.x * ac.x + ap.y * ac.y + ap.z * ac.z;

    const double denom = d00 * d11 - d01 * d01;
    if (std::fabs(denom) < 1e-20) return false;

    v = (d11 * d20 - d01 * d21) / denom;
    w = (d00 * d21 - d01 * d20) / denom;
    u = 1.0 - v - w;

    return u >= -1e-10 && v >= -1e-10 && w >= -1e-10;
}

namespace {

/// Determine which edge of the triangle a point is on.
/// Barycentric: u=weight of v0(A), v=weight of v1(B), w=weight of v2(C)
/// Edge AB: w ≈ 0   Edge BC: u ≈ 0   Edge CA: v ≈ 0
/// Returns: 0=AB, 1=BC, 2=CA, -1=interior
int edge_of_bary(double u, double v, double w, double tol)
{
    bool on_ab = std::fabs(w) < tol;
    bool on_bc = std::fabs(u) < tol;
    bool on_ca = std::fabs(v) < tol;

    if (on_ab && on_bc) return 0; // vertex B
    if (on_bc && on_ca) return 1; // vertex C
    if (on_ab && on_ca) return 2; // vertex A
    if (on_ab) return 0;
    if (on_bc) return 1;
    if (on_ca) return 2;
    return -1;
}

/// Find which vertex index a point coincides with.
/// Returns 0, 1, 2, or -1 (not at a vertex).
int vertex_of_bary(double u, double v, double w, double tol)
{
    if (u > 1.0 - tol) return 0; // vertex A
    if (v > 1.0 - tol) return 1; // vertex B
    if (w > 1.0 - tol) return 2; // vertex C
    return -1;
}

/// Ensure a triangle's winding matches the reference normal.
std::array<int, 3> ensure_ccw(const IMesh& mesh, int v0, int v1, int v2,
                               const Vec3& ref_normal)
{
    const Vec3& p0 = mesh.verts[v0].co;
    const Vec3& p1 = mesh.verts[v1].co;
    const Vec3& p2 = mesh.verts[v2].co;
    Vec3 e1{p1.x - p0.x, p1.y - p0.y, p1.z - p0.z};
    Vec3 e2{p2.x - p0.x, p2.y - p0.y, p2.z - p0.z};
    Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x};
    double dot = n.x * ref_normal.x + n.y * ref_normal.y + n.z * ref_normal.z;
    if (dot < 0) return {v0, v2, v1};
    return {v0, v1, v2};
}

} // anonymous namespace

std::vector<std::array<int, 3>> split_triangle_by_segment(
    IMesh& mesh, int tri_index,
    const Vec3& seg_p0, const Vec3& seg_p1,
    double weld_eps)
{
    if (tri_index < 0 || tri_index >= static_cast<int>(mesh.tris.size()))
        return {};

    const IMeshTri& tri = mesh.tris[tri_index];
    const Vec3& a = mesh.verts[tri.v0].co;
    const Vec3& b = mesh.verts[tri.v1].co;
    const Vec3& c = mesh.verts[tri.v2].co;

    // Compute original triangle normal for winding reference
    Vec3 oe1{b.x - a.x, b.y - a.y, b.z - a.z};
    Vec3 oe2{c.x - a.x, c.y - a.y, c.z - a.z};
    Vec3 ref_normal{oe1.y * oe2.z - oe1.z * oe2.y,
                     oe1.z * oe2.x - oe1.x * oe2.z,
                     oe1.x * oe2.y - oe1.y * oe2.x};

    // Insert the two segment endpoints (welded)
    int ip0 = mesh.find_or_insert_vert(seg_p0, weld_eps);
    int ip1 = mesh.find_or_insert_vert(seg_p1, weld_eps);

    // Degenerate segment
    if (ip0 == ip1) return {};

    int tri_verts[3] = {tri.v0, tri.v1, tri.v2};

    // Check if endpoints coincide with triangle vertices
    int vert0 = -1, vert1 = -1;
    for (int vi = 0; vi < 3; ++vi) {
        if (ip0 == tri_verts[vi]) vert0 = vi;
        if (ip1 == tri_verts[vi]) vert1 = vi;
    }

    // ── Case: Both endpoints at vertices ─────────────────
    // Segment goes vertex-to-vertex: either along an edge (no split needed)
    // or vertex-to-opposite-vertex (impossible for a single segment crossing)
    if (vert0 >= 0 && vert1 >= 0) {
        // Adjacent vertices = segment along edge → no split needed
        // Opposite vertices in a triangle don't exist (all vertices are adjacent)
        return {};
    }

    // ── Case: One endpoint at vertex, other on edge ───────
    // Split into 2 sub-triangles
    if (vert0 >= 0 || vert1 >= 0) {
        int vert_idx = (vert0 >= 0) ? vert0 : vert1;
        int ip_edge = (vert0 >= 0) ? ip1 : ip0;
        int shared = tri_verts[vert_idx];
        int other1 = tri_verts[(vert_idx + 1) % 3];
        int other2 = tri_verts[(vert_idx + 2) % 3];

        // The segment goes from 'shared' vertex to 'ip_edge' on the opposite edge.
        // Determine which of the two edges (shared→other1 or shared→other2)
        // the ip_edge point is on.
        // The opposite edge is other1→other2.
        // ip_edge should be on edge other1→other2.

        // Split: (shared, other1, ip_edge) + (shared, ip_edge, other2)
        std::vector<std::array<int, 3>> result;
        result.push_back(ensure_ccw(mesh, shared, other1, ip_edge, ref_normal));
        result.push_back(ensure_ccw(mesh, shared, ip_edge, other2, ref_normal));
        return result;
    }

    // ── Case: Neither endpoint at vertex ──────────────────
    // Standard edge-to-edge crossing

    // Compute barycentric coordinates
    double u0, v0, w0, u1, v1, w1;
    barycentric(seg_p0, a, b, c, u0, v0, w0);
    barycentric(seg_p1, a, b, c, u1, v1, w1);

    double tri_size = std::sqrt(oe1.x*oe1.x + oe1.y*oe1.y + oe1.z*oe1.z);
    double tol = 1e-6 * std::max(1.0, tri_size);

    int edge0 = edge_of_bary(u0, v0, w0, tol);
    int edge1 = edge_of_bary(u1, v1, w1, tol);

    // If an endpoint is in the interior, treat as being on the nearest edge
    if (edge0 < 0) {
        double min_bary = std::min({std::fabs(w0), std::fabs(u0), std::fabs(v0)});
        if (min_bary == std::fabs(w0)) edge0 = 0;
        else if (min_bary == std::fabs(u0)) edge0 = 1;
        else edge0 = 2;
    }
    if (edge1 < 0) {
        double min_bary = std::min({std::fabs(w1), std::fabs(u1), std::fabs(v1)});
        if (min_bary == std::fabs(w1)) edge1 = 0;
        else if (min_bary == std::fabs(u1)) edge1 = 1;
        else edge1 = 2;
    }

    // Same edge — segment lies along an edge, no split needed
    if (edge0 == edge1) return {};

    // Determine shared vertex (the vertex between the two crossed edges)
    int shared_idx = -1;
    if ((edge0 == 0 && edge1 == 1) || (edge0 == 1 && edge1 == 0)) shared_idx = 1;
    else if ((edge0 == 1 && edge1 == 2) || (edge0 == 2 && edge1 == 1)) shared_idx = 2;
    else if ((edge0 == 0 && edge1 == 2) || (edge0 == 2 && edge1 == 0)) shared_idx = 0;

    if (shared_idx < 0) return {};

    int shared = tri_verts[shared_idx];
    int other1 = tri_verts[(shared_idx + 1) % 3];
    int other2 = tri_verts[(shared_idx + 2) % 3];

    // The segment divides the triangle into:
    // 1. Triangle near shared vertex: (shared, ip0, ip1)
    // 2. Quadrilateral (other1, other2, ip1, ip0) → split into 2 triangles:
    //    (other1, other2, ip1) + (other1, ip1, ip0)
    std::vector<std::array<int, 3>> result;
    result.push_back(ensure_ccw(mesh, shared, ip0, ip1, ref_normal));
    result.push_back(ensure_ccw(mesh, other1, other2, ip1, ref_normal));
    result.push_back(ensure_ccw(mesh, other1, ip1, ip0, ref_normal));

    return result;
}

std::vector<std::array<int, 3>> split_triangle_by_constraints(
    IMesh& mesh, int tri_index,
    const std::vector<std::pair<Vec3, Vec3>>& segments,
    double weld_eps)
{
    if (tri_index < 0 || tri_index >= static_cast<int>(mesh.tris.size()) ||
        segments.empty()) {
        return {};
    }

    const IMeshTri tri = mesh.tris[static_cast<size_t>(tri_index)];
    const Vec3 tri3[3] = {
        mesh.verts[tri.v0].co,
        mesh.verts[tri.v1].co,
        mesh.verts[tri.v2].co,
    };
    const Vec3 ab{tri3[1].x - tri3[0].x, tri3[1].y - tri3[0].y, tri3[1].z - tri3[0].z};
    const Vec3 ac{tri3[2].x - tri3[0].x, tri3[2].y - tri3[0].y, tri3[2].z - tri3[0].z};
    const Vec3 normal{
        ab.y * ac.z - ab.z * ac.y,
        ab.z * ac.x - ab.x * ac.z,
        ab.x * ac.y - ab.y * ac.x,
    };

    int axis = 2;
    const double anx = std::fabs(normal.x);
    const double any = std::fabs(normal.y);
    const double anz = std::fabs(normal.z);
    if (anx >= any && anx >= anz) axis = 0;
    else if (any >= anz) axis = 1;

    auto project = [axis](const Vec3& p) -> CDT::V2d<double> {
        if (axis == 0) return {p.y, p.z};
        if (axis == 1) return {p.x, p.z};
        return {p.x, p.y};
    };
    auto unproject = [&](const CDT::V2d<double>& p) -> Vec3 {
        Vec3 out = tri3[0];
        if (axis == 0) {
            out.y = p.x;
            out.z = p.y;
            out.x = tri3[0].x -
                    (normal.y * (out.y - tri3[0].y) +
                     normal.z * (out.z - tri3[0].z)) / normal.x;
        } else if (axis == 1) {
            out.x = p.x;
            out.z = p.y;
            out.y = tri3[0].y -
                    (normal.x * (out.x - tri3[0].x) +
                     normal.z * (out.z - tri3[0].z)) / normal.y;
        } else {
            out.x = p.x;
            out.y = p.y;
            out.z = tri3[0].z -
                    (normal.x * (out.x - tri3[0].x) +
                     normal.y * (out.y - tri3[0].y)) / normal.z;
        }
        return out;
    };

    const CDT::V2d<double> tri2[3] = {
        project(tri3[0]), project(tri3[1]), project(tri3[2]),
    };
    auto orient2 = [](const CDT::V2d<double>& a,
                      const CDT::V2d<double>& b,
                      const CDT::V2d<double>& c) {
        return (b.x - a.x) * (c.y - a.y) -
               (b.y - a.y) * (c.x - a.x);
    };
    const double winding = orient2(tri2[0], tri2[1], tri2[2]) >= 0.0 ? 1.0 : -1.0;

    auto clip_to_triangle = [&](CDT::V2d<double> p0, CDT::V2d<double> p1,
                                CDT::V2d<double>& out0, CDT::V2d<double>& out1) {
        double t0 = 0.0;
        double t1 = 1.0;
        for (int edge = 0; edge < 3; ++edge) {
            const auto& a = tri2[edge];
            const auto& b = tri2[(edge + 1) % 3];
            const double f0 = winding * orient2(a, b, p0);
            const double f1 = winding * orient2(a, b, p1);
            const double eps = 1e-12;
            if (f0 < -eps && f1 < -eps) return false;
            if ((f0 < -eps) != (f1 < -eps)) {
                const double t = f0 / (f0 - f1);
                if (f0 < -eps) t0 = std::max(t0, t);
                else t1 = std::min(t1, t);
            }
        }
        if (t1 - t0 <= 1e-12) return false;
        out0 = {p0.x + (p1.x - p0.x) * t0, p0.y + (p1.y - p0.y) * t0};
        out1 = {p0.x + (p1.x - p0.x) * t1, p0.y + (p1.y - p0.y) * t1};
        return true;
    };

    std::vector<CDT::V2d<double>> vertices;
    std::vector<CDT::Edge> edges;
    auto add_vertex = [&](const CDT::V2d<double>& p) -> CDT::VertInd {
        const double eps2 = std::max(1e-24, weld_eps * weld_eps);
        for (CDT::VertInd i = 0; i < vertices.size(); ++i) {
            const double dx = vertices[i].x - p.x;
            const double dy = vertices[i].y - p.y;
            if (dx * dx + dy * dy <= eps2) return i;
        }
        vertices.push_back(p);
        return static_cast<CDT::VertInd>(vertices.size() - 1);
    };

    const CDT::VertInd boundary[3] = {
        add_vertex(tri2[0]),
        add_vertex(tri2[1]),
        add_vertex(tri2[2]),
    };
    edges.emplace_back(boundary[0], boundary[1]);
    edges.emplace_back(boundary[1], boundary[2]);
    edges.emplace_back(boundary[2], boundary[0]);

    for (const auto& segment : segments) {
        CDT::V2d<double> clipped0;
        CDT::V2d<double> clipped1;
        if (!clip_to_triangle(project(segment.first), project(segment.second),
                              clipped0, clipped1)) {
            continue;
        }
        const CDT::VertInd v0 = add_vertex(clipped0);
        const CDT::VertInd v1 = add_vertex(clipped1);
        if (v0 != v1) edges.emplace_back(v0, v1);
    }
    if (vertices.size() == 3) return {};

    CDT::Triangulation<double> cdt(
        CDT::VertexInsertionOrder::AsProvided,
        CDT::IntersectingConstraintEdges::TryResolve,
        std::max(1e-12, weld_eps));
    cdt.insertVertices(vertices);
    cdt.insertEdges(edges);
    cdt.eraseSuperTriangle();

    std::vector<int> mesh_vertices(cdt.vertices.size(), -1);
    for (size_t i = 0; i < cdt.vertices.size(); ++i) {
        mesh_vertices[i] = mesh.find_or_insert_vert(unproject(cdt.vertices[i]), weld_eps);
    }

    std::vector<std::array<int, 3>> result;
    result.reserve(cdt.triangles.size());
    for (const CDT::Triangle& cdt_tri : cdt.triangles) {
        const int v0 = mesh_vertices[cdt_tri.vertices[0]];
        const int v1 = mesh_vertices[cdt_tri.vertices[1]];
        const int v2 = mesh_vertices[cdt_tri.vertices[2]];
        if (v0 == v1 || v1 == v2 || v2 == v0) continue;
        const auto out_tri = ensure_ccw(mesh, v0, v1, v2, normal);
        const Vec3& a = mesh.verts[out_tri[0]].co;
        const Vec3& b = mesh.verts[out_tri[1]].co;
        const Vec3& c = mesh.verts[out_tri[2]].co;
        const Vec3 e1{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 e2{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 cross{
            e1.y * e2.z - e1.z * e2.y,
            e1.z * e2.x - e1.x * e2.z,
            e1.x * e2.y - e1.y * e2.x,
        };
        if (cross.x * cross.x + cross.y * cross.y + cross.z * cross.z <= 1e-24) continue;
        result.push_back(out_tri);
    }
    return result;
}

} // namespace pcg::internal::geometry
