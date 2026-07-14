// N1–N5: pcg_execute_graph_v8 geometry_binary export + v7 regression.

#include "pcg_api.h"

#include "data/pcg_geometry.hpp"
#include "data/pcg_geometry_binary.hpp"
#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_mesh_data.hpp"
#include "elements/mesh_algorithms.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

[[noreturn]] void fail(const char* msg)
{
    std::printf("FAIL: %s\n", msg);
    std::exit(1);
}

void expect_true(bool cond, const char* msg)
{
    if (!cond)
        fail(msg);
}

void expect_eq_int(int actual, int expected, const char* label)
{
    if (actual != expected) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s: expected %d got %d", label, expected, actual);
        fail(buf);
    }
}

const char* kFixtureA = R"({
  "version": "1.0",
  "nodes": [
    {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
     "data": {"width": 1.0, "height": 1.0, "depth": 1.0}},
    {"id": "bevel", "type": "BevelMesh", "position": {"x":200,"y":0},
     "data": {"amount": 0.0}},
    {"id": "out", "type": "Output", "position": {"x":400,"y":0},
     "data": {}}
  ],
  "edges": [
    {"id": "e1", "source": "box", "target": "bevel", "sourceHandle": "out", "targetHandle": "in"},
    {"id": "e2", "source": "bevel", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
  ]
})";

const char* kFixtureB = R"({
  "version": "1.0",
  "nodes": [
    {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
     "data": {"width": 1.0, "height": 1.0, "depth": 1.0}},
    {"id": "out", "type": "Output", "position": {"x":200,"y":0},
     "data": {}}
  ],
  "edges": [
    {"id": "e1", "source": "box", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
  ]
})";

struct ExecMeshResult {
    PcgResultCode code = PCG_ERR_EXECUTION;
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    int geometry_bytes = 0;
    std::vector<uint8_t> mesh_buf;
    std::vector<uint8_t> geometry_buf;
    char err[1024] = {};
};

ExecMeshResult execute_v8(const char* json,
                          void* geometry_buf,
                          int geometry_buf_size,
                          int* geometry_bytes_written)
{
    ExecMeshResult r;
    r.mesh_buf.assign(8 * 1024 * 1024, 0);
    char json_out[4096] = {};
    r.code = pcg_execute_graph_v8(
        json, 42, nullptr, 0, nullptr, 0, nullptr, 0, &r.kind, json_out, sizeof(json_out),
        r.mesh_buf.data(), static_cast<int>(r.mesh_buf.size()), nullptr, 0, nullptr, nullptr,
        &r.vertex_count, &r.index_count, nullptr, nullptr, 0, geometry_buf, geometry_buf_size,
        geometry_bytes_written, r.err, sizeof(r.err));
    if (geometry_bytes_written)
        r.geometry_bytes = *geometry_bytes_written;
    return r;
}

bool expect_outward_normals(const pcg::internal::data::PcgMeshData& mesh)
{
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    if (tris.size() < 3)
        return false;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const auto& a = verts[static_cast<size_t>(tris[i])];
        const auto& b = verts[static_cast<size_t>(tris[i + 1])];
        const auto& c = verts[static_cast<size_t>(tris[i + 2])];
        const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
        const double acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
        const double nx = aby * acz - abz * acy;
        const double ny = abz * acx - abx * acz;
        const double nz = abx * acy - aby * acx;
        const double cx = (a.x + b.x + c.x) / 3.0;
        const double cy = (a.y + b.y + c.y) / 3.0;
        const double cz = (a.z + b.z + c.z) / 3.0;
        if (nx * cx + ny * cy + nz * cz < 0.0)
            return false;
    }
    return true;
}

void assert_mesh_round_trip(const ExecMeshResult& r, int expected_indices)
{
    expect_eq_int(static_cast<int>(r.code), static_cast<int>(PCG_OK), "rc");
    expect_eq_int(r.kind, PCG_RESULT_KIND_MESH, "kind");
    expect_eq_int(r.index_count, expected_indices, "index_count");

    pcg::internal::data::PcgMeshData mesh;
    expect_true(pcg::internal::data::read_mesh_binary(r.mesh_buf.data(),
                                                      static_cast<int>(r.mesh_buf.size()), mesh),
                "read_mesh_binary");
    expect_eq_int(static_cast<int>(mesh.triangles().size()), expected_indices, "mesh indices");
    expect_true(expect_outward_normals(mesh), "sink mesh outward normals");
}

void test_n1_geometry_export()
{
    std::vector<uint8_t> geo_buf(8 * 1024 * 1024);
    int geometry_bytes = -1;
    const ExecMeshResult r =
        execute_v8(kFixtureA, geo_buf.data(), static_cast<int>(geo_buf.size()), &geometry_bytes);
    assert_mesh_round_trip(r, 36);
    expect_true(geometry_bytes > 0, "N1 geometry_bytes > 0");

    pcg::internal::data::PcgGeometry geometry;
    expect_true(pcg::internal::data::read_geometry_binary(geo_buf.data(), geometry_bytes, geometry),
                "N1 read_geometry_binary");
    expect_eq_int(static_cast<int>(geometry.points().size()), 8, "N1 points");
    expect_eq_int(static_cast<int>(geometry.faces().size()), 6, "N1 faces");
    for (size_t i = 0; i < geometry.faces().size(); ++i)
        expect_eq_int(static_cast<int>(geometry.faces()[i].size()), 4, "N1 face corner count");
    std::printf("PASS N1 geometry export\n");
}

void test_n2_create_box_exports_geometry()
{
    std::vector<uint8_t> geo_buf(8 * 1024 * 1024);
    int geometry_bytes = -1;
    const ExecMeshResult r =
        execute_v8(kFixtureB, geo_buf.data(), static_cast<int>(geo_buf.size()), &geometry_bytes);
    assert_mesh_round_trip(r, 36);
    expect_true(geometry_bytes > 0, "N2 geometry_bytes > 0");

    pcg::internal::data::PcgGeometry geometry;
    expect_true(pcg::internal::data::read_geometry_binary(geo_buf.data(), geometry_bytes, geometry),
                "N2 read_geometry_binary");
    expect_eq_int(static_cast<int>(geometry.points().size()), 8, "N2 points");
    expect_eq_int(static_cast<int>(geometry.faces().size()), 6, "N2 faces");
    for (size_t i = 0; i < geometry.faces().size(); ++i)
        expect_eq_int(static_cast<int>(geometry.faces()[i].size()), 4, "N2 face corner count");

    const auto triangulated =
        pcg::internal::data::triangulate_geometry(pcg::internal::elements::create_box_geometry(2.0, 2.0, 2.0));
    expect_true(expect_outward_normals(triangulated), "create_box_geometry triangulate outward");
    expect_true(expect_outward_normals(pcg::internal::elements::create_box_mesh(2.0, 2.0, 2.0)),
                "create_box_mesh outward");
    std::printf("PASS N2 CreateBoxMesh exports geometry_binary\n");
}

void test_n3_null_geometry_buffer()
{
    int geometry_bytes = -1;
    ExecMeshResult r = execute_v8(kFixtureA, nullptr, 0, &geometry_bytes);
    assert_mesh_round_trip(r, 36);
    expect_eq_int(geometry_bytes, 0, "N3 null buffer geometry_bytes");

    geometry_bytes = -1;
    r = execute_v8(kFixtureA, nullptr, 0, nullptr);
    assert_mesh_round_trip(r, 36);
    std::printf("PASS N3 null/0 geometry buffer\n");
}

void test_n4_tiny_geometry_buffer()
{
    uint8_t tiny[16] = {};
    int geometry_bytes = -1;
    const ExecMeshResult r = execute_v8(kFixtureA, tiny, 16, &geometry_bytes);
    assert_mesh_round_trip(r, 36);
    expect_eq_int(geometry_bytes, 0, "N4 tiny buffer geometry_bytes");
    std::printf("PASS N4 tiny geometry buffer\n");
}

void test_n5_v7_regression()
{
    std::vector<uint8_t> mesh_buf(8 * 1024 * 1024);
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    char json_out[4096] = {};
    char err[1024] = {};
    const PcgResultCode code = pcg_execute_graph_v7(
        kFixtureA, 42, nullptr, 0, nullptr, 0, nullptr, 0, &kind, json_out, sizeof(json_out),
        mesh_buf.data(), static_cast<int>(mesh_buf.size()), nullptr, 0, nullptr, nullptr,
        &vertex_count, &index_count, nullptr, nullptr, 0, err, sizeof(err));
    expect_eq_int(static_cast<int>(code), static_cast<int>(PCG_OK), "N5 rc");
    expect_eq_int(kind, PCG_RESULT_KIND_MESH, "N5 kind");
    expect_eq_int(index_count, 36, "N5 index_count");

    pcg::internal::data::PcgMeshData mesh;
    expect_true(pcg::internal::data::read_mesh_binary(mesh_buf.data(),
                                                      static_cast<int>(mesh_buf.size()), mesh),
                "N5 read_mesh_binary");
    expect_eq_int(static_cast<int>(mesh.triangles().size()), 36, "N5 mesh indices");
    std::printf("PASS N5 v7 regression\n");
}

} // namespace

int main()
{
    test_n1_geometry_export();
    test_n2_create_box_exports_geometry();
    test_n3_null_geometry_buffer();
    test_n4_tiny_geometry_buffer();
    test_n5_v7_regression();
    std::printf("All geometry export tests passed.\n");
    return 0;
}
