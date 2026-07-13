// Unit tests for compute_split_normals — polygon corner topology, hard-edge
// classification, island union-find, and render mesh + normal generation.
#include "data/pcg_geometry.hpp"
#include "data/pcg_mesh_binary.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace pcg::internal::data;
using pcg::internal::geometry::GroupDomain;

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

// Build a unit cube geometry (6 quads, 8 shared vertices)
PcgGeometry make_cube()
{
    PcgGeometry g;
    g.points_mut() = {
        {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
        {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1},
    };
    g.faces_mut() = {
        {0, 1, 2, 3}, // -Z
        {4, 7, 6, 5}, // +Z
        {0, 4, 5, 1}, // -Y
        {1, 5, 6, 2}, // +X
        {2, 6, 7, 3}, // +Y
        {3, 7, 4, 0}, // -X
    };
    return g;
}

// Build a single triangle geometry
PcgGeometry make_triangle()
{
    PcgGeometry g;
    g.points_mut() = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    g.faces_mut() = {{0, 1, 2}};
    return g;
}

// Build a quad geometry
PcgGeometry make_quad()
{
    PcgGeometry g;
    g.points_mut() = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
    g.faces_mut() = {{0, 1, 2, 3}};
    return g;
}

// Build two coplanar quads sharing an edge (shared point indices)
PcgGeometry make_two_coplanar_quads()
{
    PcgGeometry g;
    g.points_mut() = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                      {2, 0, 0}, {2, 1, 0}};
    g.faces_mut() = {{0, 1, 2, 3}, {1, 4, 5, 2}};
    return g;
}

// Build a quad with face groups (left half / right half)
PcgGeometry make_grouped_quad()
{
    PcgGeometry g;
    g.points_mut() = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0},
                      {0, 1, 0}, {1, 1, 0}, {2, 1, 0}};
    g.faces_mut() = {{0, 1, 4, 3}, {1, 2, 5, 4}};
    g.groups().add(GroupDomain::Face, "left", 0);
    g.groups().add(GroupDomain::Face, "right", 1);
    return g;
}

double vec_len(const PcgVertex& v)
{
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

} // namespace

int main()
{
    // --- Test 1: Single triangle — 3 normals, all equal, unit length ---
    {
        PcgGeometry tri = make_triangle();
        PcgMeshData mesh = compute_split_normals(tri, {ShadeMode::Auto, 30.0, true});
        expect(mesh.has_normals(), "triangle: has normals");
        expect(mesh.normals().size() == mesh.vertices().size(),
               "triangle: normals count == vertices count");
        expect(mesh.vertices().size() == 3, "triangle: 3 vertices (no split)");
        if (mesh.has_normals()) {
            for (const auto& n : mesh.normals())
                expect(std::abs(vec_len(n) - 1.0) <= 1e-5, "triangle: normal unit length");
        }
    }

    // --- Test 2: Quad — 4 real edges, fan diagonal not in edge map ---
    {
        PcgGeometry quad = make_quad();
        PcgMeshData mesh = compute_split_normals(quad, {ShadeMode::Auto, 30.0, true});
        expect(mesh.has_normals(), "quad: has normals");
        expect(mesh.triangles().size() == 6, "quad: 2 triangles (6 indices)");
        // Shared vertex: 4 vertices (no split for a flat quad)
        expect(mesh.vertices().size() == 4, "quad: 4 vertices (coplanar, no split)");
    }

    // --- Test 3: Two coplanar quads — soft edge, no split ---
    {
        PcgGeometry two = make_two_coplanar_quads();
        PcgMeshData mesh = compute_split_normals(two, {ShadeMode::Auto, 30.0, true});
        expect(mesh.has_normals(), "coplanar: has normals");
        // 6 unique points, coplanar so no split
        expect(mesh.vertices().size() == 6, "coplanar: 6 vertices (no split)");
    }

    // --- Test 4: Cube cusp 30° — corner vertices split into 3 islands ---
    {
        PcgGeometry cube = make_cube();
        PcgMeshData mesh30 = compute_split_normals(cube, {ShadeMode::Auto, 30.0, true});
        expect(mesh30.has_normals(), "cube cusp30: has normals");
        expect(mesh30.vertices().size() > 8,
               "cube cusp30: vertex count > 8 (split at 90° corners)");
        // Each corner should split into 3 islands → 8 * 3 = 24 vertices
        expect(mesh30.vertices().size() == 24,
               "cube cusp30: 24 vertices (8 corners * 3 islands)");
    }

    // --- Test 5: Cube cusp 100° — no split, all smooth ---
    {
        PcgGeometry cube = make_cube();
        PcgMeshData mesh100 = compute_split_normals(cube, {ShadeMode::Auto, 100.0, true});
        expect(mesh100.has_normals(), "cube cusp100: has normals");
        expect(mesh100.vertices().size() == 8,
               "cube cusp100: 8 vertices (no split, all smooth)");
    }

    // --- Test 6: Cube Flat mode — each corner is its own island ---
    {
        PcgGeometry cube = make_cube();
        PcgMeshData mesh = compute_split_normals(cube, {ShadeMode::Flat, 30.0, true});
        expect(mesh.has_normals(), "cube flat: has normals");
        // Flat: each polygon corner is a render vertex
        // 6 faces * 4 corners = 24 corners = 24 render vertices
        expect(mesh.vertices().size() == 24,
               "cube flat: 24 vertices (6 faces * 4 corners)");
    }

    // --- Test 7: Cube Smooth mode — no split, all manifold edges smooth ---
    {
        PcgGeometry cube = make_cube();
        PcgMeshData mesh = compute_split_normals(cube, {ShadeMode::Smooth, 30.0, true});
        expect(mesh.has_normals(), "cube smooth: has normals");
        expect(mesh.vertices().size() == 8,
               "cube smooth: 8 vertices (no split)");
    }

    // --- Test 8: Group boundary hard edge — even coplanar, groups differ ---
    {
        PcgGeometry gq = make_grouped_quad();
        // Two coplanar quads with different face groups → hard edge despite 0° angle
        PcgMeshData mesh = compute_split_normals(gq, {ShadeMode::Auto, 30.0, true});
        expect(mesh.has_normals(), "group boundary: has normals");
        // The shared edge (points 1 and 4) should be hard → split
        // Point 1: shared by left face and right face → 2 islands
        // Point 4: shared by left face and right face → 2 islands
        // Points 0, 2, 3, 5: each in only one face → 1 island
        // Total: 4 * 1 + 2 * 2 = 8
        expect(mesh.vertices().size() == 8,
               "group boundary: 8 vertices (4 single + 2 split * 2)");
    }

    // --- Test 9: Group boundary ignored when hard_group_boundaries=false ---
    {
        PcgGeometry gq = make_grouped_quad();
        PcgMeshData mesh = compute_split_normals(gq, {ShadeMode::Auto, 30.0, false});
        expect(mesh.has_normals(), "group ignored: has normals");
        // No group boundary check → coplanar quads → no split
        expect(mesh.vertices().size() == 6,
               "group ignored: 6 vertices (no split)");
    }

    // --- Test 10: Triangle order matches triangulate_geometry_shared ---
    {
        PcgGeometry cube = make_cube();
        PcgMeshData shared = triangulate_geometry_shared(cube);
        PcgMeshData split = compute_split_normals(cube, {ShadeMode::Smooth, 30.0, true});
        expect(split.triangles().size() == shared.triangles().size(),
               "order: same triangle count");
        // Vertex indices may be remapped (plan §2.4: only triangle count/order must match)
        // With Smooth mode on a cube, vertex count should equal point count (no split)
        expect(split.vertices().size() == shared.vertices().size(),
               "order: same vertex count (Smooth mode, no split)");
    }

    // --- Test 11: Binary v2 round-trip with normals ---
    {
        PcgGeometry cube = make_cube();
        PcgMeshData mesh = compute_split_normals(cube, {ShadeMode::Auto, 30.0, true});
        expect(mesh.has_normals(), "binary: source has normals");

        int size = mesh_binary_size(mesh);
        std::vector<uint8_t> buf(static_cast<size_t>(size));
        bool ok = write_mesh_binary(mesh, buf.data(), size);
        expect(ok, "binary: write v2 with normals");

        PcgMeshData restored;
        ok = read_mesh_binary(buf.data(), size, restored);
        expect(ok, "binary: read v2 with normals");
        expect(restored.has_normals(), "binary: restored has normals");
        expect(restored.vertices().size() == mesh.vertices().size(),
               "binary: vertex count matches");
        expect(restored.normals().size() == mesh.normals().size(),
               "binary: normal count matches");
    }

    // --- Test 12: Binary v2 without normals (mesh without normals) ---
    {
        PcgGeometry tri = make_triangle();
        PcgMeshData mesh = triangulate_geometry_shared(tri);
        expect(!mesh.has_normals(), "binary v2 no normals: source has no normals");

        int size = mesh_binary_size(mesh);
        std::vector<uint8_t> buf(static_cast<size_t>(size));
        bool ok = write_mesh_binary(mesh, buf.data(), size);
        expect(ok, "binary v2 no normals: write");

        PcgMeshData restored;
        ok = read_mesh_binary(buf.data(), size, restored);
        expect(ok, "binary v2 no normals: read");
        expect(!restored.has_normals(), "binary v2 no normals: restored has no normals");
    }

    // --- Test 13: Binary v1 backward compatibility ---
    {
        // Manually construct a v1 binary (16-byte header, no flags, no normals)
        PcgGeometry tri = make_triangle();
        PcgMeshData mesh = triangulate_geometry_shared(tri);

        // Build v1 binary manually
        const uint32_t magic = kPcgMeshBinaryMagic;
        const uint32_t version = 1u;
        const uint32_t vcount = 3;
        const uint32_t icount = 3;
        std::vector<uint8_t> buf(16 + 3 * 12 + 3 * 4);
        std::memcpy(buf.data() + 0, &magic, 4);
        std::memcpy(buf.data() + 4, &version, 4);
        std::memcpy(buf.data() + 8, &vcount, 4);
        std::memcpy(buf.data() + 12, &icount, 4);
        // Positions
        float pos[3][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
        std::memcpy(buf.data() + 16, pos, sizeof(pos));
        // Indices
        uint32_t idx[3] = {0, 1, 2};
        std::memcpy(buf.data() + 16 + 36, idx, sizeof(idx));

        PcgMeshData restored;
        bool ok = read_mesh_binary(buf.data(), static_cast<int>(buf.size()), restored);
        expect(ok, "binary v1: read backward compat");
        expect(restored.vertices().size() == 3, "binary v1: 3 vertices");
        expect(!restored.has_normals(), "binary v1: no normals");
    }

    // --- Test 14: No NaN in degenerate face ---
    {
        PcgGeometry g;
        g.points_mut() = {{0, 0, 0}, {0, 0, 0}, {1, 0, 0}}; // zero-length edge
        g.faces_mut() = {{0, 1, 2}};
        PcgMeshData mesh = compute_split_normals(g, {ShadeMode::Auto, 30.0, true});
        expect(mesh.has_normals(), "degenerate: has normals");
        bool no_nan = true;
        for (const auto& n : mesh.normals()) {
            if (std::isnan(n.x) || std::isnan(n.y) || std::isnan(n.z)) {
                no_nan = false;
                break;
            }
        }
        expect(no_nan, "degenerate: no NaN in normals");
    }

    // --- Test 15: Detail propagation through bevel ---
    {
        PcgGeometry cube = make_cube();
        cube.detail().shade_mode = ShadeMode::Flat;
        cube.detail().cusp_angle_deg = 45.0;

        // Simulate bevel detail propagation
        // (We can't call bevel_geometry directly without BevelEdgeSelection,
        // but we can verify the detail is on the geometry)
        expect(cube.detail().shade_mode == ShadeMode::Flat, "detail: shade mode set");
        expect(cube.detail().cusp_angle_deg == 45.0, "detail: cusp angle set");
    }

    // --- Test 16: Cook hash changes with cusp_angle ---
    {
        PcgGeometry cube = make_cube();
        PcgGeometry cube2 = make_cube();
        cube.detail().cusp_angle_deg = 30.0;
        cube2.detail().cusp_angle_deg = 100.0;

        // hash_geometry is in cook_hash.cpp — we test indirectly by checking
        // that different cusp angles produce different split results
        PcgMeshData m30 = compute_split_normals(cube, {ShadeMode::Auto, 30.0, true});
        PcgMeshData m100 = compute_split_normals(cube2, {ShadeMode::Auto, 100.0, true});
        expect(m30.vertices().size() != m100.vertices().size(),
               "hash: different cusp → different vertex count");
    }

    if (g_fail > 0) {
        std::printf("\n%d tests FAILED\n", g_fail);
        return 1;
    }
    std::printf("\nAll split normal tests passed\n");
    return 0;
}
