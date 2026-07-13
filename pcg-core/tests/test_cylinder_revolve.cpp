#include "elements/mesh_algorithms.hpp"
#include "elements/spline_algorithms.hpp"
#include "data/pcg_mesh_data.hpp"
#include "data/pcg_geometry.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace pcg::internal::data;
using namespace pcg::internal::elements;

namespace {

int g_fail = 0;

void expect(bool cond, const char* msg)
{
    if (cond) {
        std::printf("PASS: %s\n", msg);
    } else {
        std::printf("FAIL: %s\n", msg);
        ++g_fail;
    }
}

struct Vec3 { double x, y, z; };

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

double dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// Geometric normal of a triangle (not normalized, just direction)
Vec3 tri_normal(const PcgMeshData& mesh, int tri_idx)
{
    const auto& tris = mesh.triangles();
    const auto& verts = mesh.vertices();
    int i0 = tris[tri_idx * 3];
    int i1 = tris[tri_idx * 3 + 1];
    int i2 = tris[tri_idx * 3 + 2];
    Vec3 v0{verts[i0].x, verts[i0].y, verts[i0].z};
    Vec3 v1{verts[i1].x, verts[i1].y, verts[i1].z};
    Vec3 v2{verts[i2].x, verts[i2].y, verts[i2].z};
    return cross(sub(v1, v0), sub(v2, v0));
}

// Check all triangle normals point outward from cylinder center axis (Y)
bool all_outward_y(const PcgMeshData& mesh)
{
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    for (size_t i = 0; i < tris.size(); i += 3) {
        int i0 = tris[i], i1 = tris[i + 1], i2 = tris[i + 2];
        double cx = (verts[i0].x + verts[i1].x + verts[i2].x) / 3.0;
        double cy = (verts[i0].y + verts[i1].y + verts[i2].y) / 3.0;
        double cz = (verts[i0].z + verts[i1].z + verts[i2].z) / 3.0;
        Vec3 n = tri_normal(mesh, static_cast<int>(i / 3));

        // Detect cap faces: all 3 vertices share the same Y (within epsilon)
        bool is_cap = std::abs(verts[i0].y - verts[i1].y) < 1e-10 &&
                      std::abs(verts[i1].y - verts[i2].y) < 1e-10;
        Vec3 outward;
        if (is_cap) {
            outward = {0.0, cy > 0 ? 1.0 : -1.0, 0.0};
        } else {
            outward = {cx, 0.0, cz};
        }
        if (dot(n, outward) <= 0)
            return false;
    }
    return true;
}

// Check revolve geometry: no duplicate indices in any face, no zero-area faces
bool no_degenerate_faces(const PcgGeometry& geo)
{
    for (const auto& face : geo.faces()) {
        for (size_t i = 0; i < face.size(); ++i) {
            for (size_t j = i + 1; j < face.size(); ++j) {
                if (face[i] == face[j])
                    return false;
            }
        }
        if (face.size() >= 3) {
            const auto& p0 = geo.points()[face[0]];
            const auto& p1 = geo.points()[face[1]];
            const auto& p2 = geo.points()[face[2]];
            Vec3 v0{p0.x, p0.y, p0.z};
            Vec3 v1{p1.x, p1.y, p1.z};
            Vec3 v2{p2.x, p2.y, p2.z};
            Vec3 n = cross(sub(v1, v0), sub(v2, v0));
            if (n.x == 0 && n.y == 0 && n.z == 0)
                return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    // --- Cylinder AC2.1: r=1,h=2,radial=8,heightSeg=1,both caps ---
    {
        auto mesh = create_cylinder_mesh(1.0, 2.0, 8, 1, true, true);
        expect(mesh.vertices().size() == 18, "cylinder: 18 vertices (16 side + 2 cap centers)");
        expect(mesh.triangles().size() == 96, "cylinder: 96 indices (48 side + 24 cap*2)");
    }

    // --- Cylinder AC2.2: all triangle normals outward ---
    {
        auto mesh = create_cylinder_mesh(1.0, 2.0, 8, 1, true, true);
        expect(all_outward_y(mesh), "cylinder: all normals outward");
    }

    // --- Cylinder: no caps ---
    {
        auto mesh = create_cylinder_mesh(1.0, 2.0, 8, 1, false, false);
        expect(mesh.vertices().size() == 16, "cylinder no cap: 16 vertices");
        expect(mesh.triangles().size() == 48, "cylinder no cap: 48 indices");
    }

    // --- Cylinder: single cap (top only) ---
    {
        auto mesh = create_cylinder_mesh(1.0, 2.0, 8, 1, true, false);
        expect(mesh.vertices().size() == 17, "cylinder top cap: 17 vertices");
        expect(mesh.triangles().size() == 72, "cylinder top cap: 72 indices");
    }

    // --- Cylinder: multi height segment ---
    {
        auto mesh = create_cylinder_mesh(1.0, 2.0, 6, 3, false, false);
        expect(mesh.vertices().size() == 24, "cylinder multi seg: 24 vertices (4*6)");
        expect(mesh.triangles().size() == 108, "cylinder multi seg: 108 indices (3*6*6)");
    }

    // --- Cylinder: invalid params ---
    {
        auto m1 = create_cylinder_mesh(0.0001, 2.0, 8, 1, true, true);
        expect(m1.vertices().empty(), "cylinder: radius too small");

        auto m2 = create_cylinder_mesh(1.0, 0.0001, 8, 1, true, true);
        expect(m2.vertices().empty(), "cylinder: height too small");

        auto m3 = create_cylinder_mesh(1.0, 2.0, 2, 1, true, true);
        expect(m3.vertices().empty(), "cylinder: radialSegments < 3");
    }

    // --- Revolve: ring-ring (no axis points) ---
    {
        // Straight vertical profile, offset from Y axis → tube
        PcgSplineData profile;
        PcgSpline spline;
        spline.closed = false;
        spline.points.push_back({1.0, -1.0, 0.0});
        spline.points.push_back({1.0, 1.0, 0.0});
        profile.add_spline(std::move(spline));

        RevolveGeometryOptions opts;
        opts.axis = "y";
        opts.segments = 8;
        auto geo = revolve_geometry(profile, opts);
        expect(geo.points().size() == 16, "revolve ring-ring: 16 points (2*8)");
        expect(geo.faces().size() == 8, "revolve ring-ring: 8 quad faces");
        expect(no_degenerate_faces(geo), "revolve ring-ring: no degenerate faces");
    }

    // --- Revolve: axis-ring (profile touches axis) ---
    {
        // L-shaped profile: starts on axis, goes out then up
        PcgSplineData profile;
        PcgSpline spline;
        spline.closed = false;
        spline.points.push_back({0.0, 0.0, 0.0});   // on axis
        spline.points.push_back({1.0, 0.0, 0.0});   // off axis
        spline.points.push_back({1.0, 1.0, 0.0});   // off axis
        profile.add_spline(std::move(spline));

        RevolveGeometryOptions opts;
        opts.axis = "y";
        opts.segments = 8;
        auto geo = revolve_geometry(profile, opts);
        // Point 0: 1 axis point; Point 1: 8 ring; Point 2: 8 ring
        expect(geo.points().size() == 17, "revolve axis-ring: 17 points (1+8+8)");
        expect(no_degenerate_faces(geo), "revolve axis-ring: no degenerate faces");
    }

    // --- Revolve: both caps (open profile, endpoints off axis) ---
    {
        PcgSplineData profile;
        PcgSpline spline;
        spline.closed = false;
        spline.points.push_back({1.0, -1.0, 0.0});
        spline.points.push_back({1.0, 1.0, 0.0});
        profile.add_spline(std::move(spline));

        RevolveGeometryOptions opts;
        opts.axis = "y";
        opts.segments = 8;
        opts.cap_start = true;
        opts.cap_end = true;
        auto geo = revolve_geometry(profile, opts);
        // 16 ring points + 2 cap centers = 18
        expect(geo.points().size() == 18, "revolve caps: 18 points (16+2)");
        // 8 side quads + 8 start tris + 8 end tris = 24 faces
        expect(geo.faces().size() == 24, "revolve caps: 24 faces (8+8+8)");
        expect(no_degenerate_faces(geo), "revolve caps: no degenerate faces");
    }

    // --- Revolve: closeProfile ---
    {
        PcgSplineData profile;
        PcgSpline spline;
        spline.closed = false;
        spline.points.push_back({1.0, -1.0, 0.0});
        spline.points.push_back({1.0, 1.0, 0.0});
        profile.add_spline(std::move(spline));

        RevolveGeometryOptions opts;
        opts.axis = "y";
        opts.segments = 8;
        opts.close_profile = true;
        auto geo = revolve_geometry(profile, opts);
        // closeProfile connects last to first → 16 ring points, 16 faces
        expect(geo.points().size() == 16, "revolve close: 16 points");
        expect(geo.faces().size() == 16, "revolve close: 16 faces");
    }

    // --- Revolve: invalid axis ---
    {
        PcgSplineData profile;
        PcgSpline spline;
        spline.points.push_back({1.0, 0.0, 0.0});
        spline.points.push_back({1.0, 1.0, 0.0});
        profile.add_spline(std::move(spline));

        RevolveGeometryOptions opts;
        opts.axis = "w";
        auto geo = revolve_geometry(profile, opts);
        expect(geo.points().empty(), "revolve: invalid axis returns empty");
    }

    // --- Revolve AC2.3: no duplicate index, no zero-area ---
    {
        PcgSplineData profile;
        PcgSpline spline;
        spline.closed = false;
        spline.points.push_back({0.0, 0.0, 0.0});
        spline.points.push_back({1.0, 0.0, 0.0});
        spline.points.push_back({1.0, 1.0, 0.0});
        spline.points.push_back({0.0, 1.0, 0.0});
        profile.add_spline(std::move(spline));

        RevolveGeometryOptions opts;
        opts.axis = "y";
        opts.segments = 16;
        opts.cap_start = true;
        opts.cap_end = true;
        auto geo = revolve_geometry(profile, opts);
        expect(no_degenerate_faces(geo), "revolve AC2.3: no degenerate/duplicate faces");
    }

    if (g_fail > 0) {
        std::printf("\n%d tests FAILED\n", g_fail);
        return 1;
    }
    std::printf("\nAll cylinder/revolve tests passed\n");
    return 0;
}
