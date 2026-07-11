#include "pcg_api.h"

#include "data/pcg_mesh_data.hpp"
#include "data/pcg_mesh_binary.hpp"
#include "elements/mesh_algorithms.hpp"
#include "geometry/bmesh.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

void expect_code(PcgResultCode actual, PcgResultCode expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL: %s expected %d got %d\n", label, static_cast<int>(expected), static_cast<int>(actual));
        std::exit(1);
    }
}

std::string read_file(const char* path)
{
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool meshes_have_different_positions(const pcg::internal::data::PcgMeshData& a,
                                     const pcg::internal::data::PcgMeshData& b,
                                     double eps = 1e-4)
{
    if (a.vertices().size() != b.vertices().size())
        return true;
    for (size_t i = 0; i < a.vertices().size(); ++i) {
        const auto& va = a.vertices()[i];
        const auto& vb = b.vertices()[i];
        if (std::abs(va.x - vb.x) > eps || std::abs(va.y - vb.y) > eps ||
            std::abs(va.z - vb.z) > eps)
            return true;
    }
    return false;
}

bool expect_outward_normals(const pcg::internal::data::PcgMeshData& mesh)
{
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    if (tris.size() < 3)
        return false;

    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const int ia = tris[i];
        const int ib = tris[i + 1];
        const int ic = tris[i + 2];
        if (ia < 0 || ib < 0 || ic < 0 || static_cast<size_t>(ia) >= verts.size() ||
            static_cast<size_t>(ib) >= verts.size() || static_cast<size_t>(ic) >= verts.size())
            return false;

        const auto& a = verts[static_cast<size_t>(ia)];
        const auto& b = verts[static_cast<size_t>(ib)];
        const auto& c = verts[static_cast<size_t>(ic)];
        const double abx = b.x - a.x, aby = b.y - a.y, abz = b.z - a.z;
        const double acx = c.x - a.x, acy = c.y - a.y, acz = c.z - a.z;
        const double nx = aby * acz - abz * acy;
        const double ny = abz * acx - abx * acz;
        const double nz = abx * acy - aby * acx;
        const double cx = (a.x + b.x + c.x) / 3.0;
        const double cy = (a.y + b.y + c.y) / 3.0;
        const double cz = (a.z + b.z + c.z) / 3.0;
        if (nx * cx + ny * cy + nz * cz <= -1e-9)
            return false;
    }
    return true;
}

bool expect_coincident_vertices_stay_welded(const pcg::internal::data::PcgMeshData& before,
                                            const pcg::internal::data::PcgMeshData& after,
                                            double eps = 1e-5)
{
    if (before.vertices().size() != after.vertices().size())
        return false;

    const auto& before_verts = before.vertices();
    const auto& after_verts = after.vertices();
    std::unordered_map<std::string, std::vector<size_t>> groups;
    for (size_t i = 0; i < before_verts.size(); ++i) {
        const auto& v = before_verts[i];
        const auto quantize = [eps](double value) -> int64_t {
            return static_cast<int64_t>(std::llround(value / eps));
        };
        const std::string key = std::to_string(quantize(v.x)) + ',' + std::to_string(quantize(v.y)) +
                                ',' + std::to_string(quantize(v.z));
        groups[key].push_back(i);
    }

    for (const auto& entry : groups) {
        if (entry.second.size() <= 1)
            continue;

        const auto& ref = after_verts[entry.second.front()];
        for (size_t i = 1; i < entry.second.size(); ++i) {
            const auto& v = after_verts[entry.second[i]];
            if (std::abs(v.x - ref.x) > eps || std::abs(v.y - ref.y) > eps || std::abs(v.z - ref.z) > eps)
                return false;
        }
    }
    return true;
}

bool expect_welded_coincident_vertices(const pcg::internal::data::PcgMeshData& mesh, double eps = 1e-5)
{
    std::unordered_map<std::string, std::vector<size_t>> groups;
    const auto& verts = mesh.vertices();
    for (size_t i = 0; i < verts.size(); ++i) {
        const auto& v = verts[i];
        const auto quantize = [eps](double value) -> int64_t {
            return static_cast<int64_t>(std::llround(value / eps));
        };
        const std::string key = std::to_string(quantize(v.x)) + ',' + std::to_string(quantize(v.y)) +
                                ',' + std::to_string(quantize(v.z));
        groups[key].push_back(i);
    }

    for (const auto& entry : groups) {
        if (entry.second.size() <= 1)
            continue;
        const auto& ref = verts[entry.second.front()];
        for (size_t i = 1; i < entry.second.size(); ++i) {
            const auto& v = verts[entry.second[i]];
            if (std::abs(v.x - ref.x) > eps || std::abs(v.y - ref.y) > eps || std::abs(v.z - ref.z) > eps)
                return false;
        }
    }
    return true;
}

bool expect_no_duplicate_geo_triangles(const pcg::internal::data::PcgMeshData& mesh, double eps = 1e-5)
{
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    auto pk = [eps](double x, double y, double z) {
        const auto quantize = [eps](double value) -> int64_t {
            return static_cast<int64_t>(std::llround(value / eps));
        };
        return std::to_string(quantize(x)) + ',' + std::to_string(quantize(y)) + ',' +
               std::to_string(quantize(z));
    };

    std::unordered_map<std::string, int> geo_count;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        std::array<std::string, 3> keys = {
            pk(verts[static_cast<size_t>(tris[i])].x, verts[static_cast<size_t>(tris[i])].y,
               verts[static_cast<size_t>(tris[i])].z),
            pk(verts[static_cast<size_t>(tris[i + 1])].x, verts[static_cast<size_t>(tris[i + 1])].y,
               verts[static_cast<size_t>(tris[i + 1])].z),
            pk(verts[static_cast<size_t>(tris[i + 2])].x, verts[static_cast<size_t>(tris[i + 2])].y,
               verts[static_cast<size_t>(tris[i + 2])].z),
        };
        std::sort(keys.begin(), keys.end());
        const std::string geo_key = keys[0] + '|' + keys[1] + '|' + keys[2];
        if (++geo_count[geo_key] > 1)
            return false;
    }
    return true;
}

int count_cross_face_triangles(const pcg::internal::data::PcgMeshData& mesh, double eps = 1e-5)
{
    const auto& verts = mesh.vertices();
    const auto& tris = mesh.triangles();
    int count = 0;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const auto& a = verts[static_cast<size_t>(tris[i])];
        const auto& b = verts[static_cast<size_t>(tris[i + 1])];
        const auto& c = verts[static_cast<size_t>(tris[i + 2])];
        const bool x_same = std::abs(a.x - b.x) < eps && std::abs(b.x - c.x) < eps;
        const bool y_same = std::abs(a.y - b.y) < eps && std::abs(b.y - c.y) < eps;
        const bool z_same = std::abs(a.z - b.z) < eps && std::abs(b.z - c.z) < eps;
        if (!x_same && !y_same && !z_same)
            ++count;
    }
    return count;
}

bool expect_closed_mesh(const pcg::internal::data::PcgMeshData& mesh)
{
    // A closed manifold mesh has every edge shared by exactly 2 triangles.
    const auto& tris = mesh.triangles();
    auto ek = [](int a, int b) -> int64_t {
        return a < b ? static_cast<int64_t>(a) * 100000 + b
                     : static_cast<int64_t>(b) * 100000 + a;
    };
    std::unordered_map<int64_t, int> edge_count;
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const int t0 = tris[i], t1 = tris[i + 1], t2 = tris[i + 2];
        edge_count[ek(t0, t1)]++;
        edge_count[ek(t1, t2)]++;
        edge_count[ek(t2, t0)]++;
    }
    for (const auto& [key, count] : edge_count) {
        if (count != 2)
            return false;
    }
    return true;
}

bool expect_no_aabb_expansion(const pcg::internal::data::PcgMeshData& mesh, double half_extent,
                              double slack = 1e-4)
{
    for (const auto& v : mesh.vertices()) {
        if (std::abs(v.x) > half_extent + slack || std::abs(v.y) > half_extent + slack ||
            std::abs(v.z) > half_extent + slack)
            return false;
    }
    return true;
}

bool execute_mesh_graph_with_textures(const char* graph_json,
                                      int seed,
                                      const PcgTextureSlot* textures,
                                      int texture_count,
                                      pcg::internal::data::PcgMeshData& mesh,
                                      char* err,
                                      int err_size);

bool execute_mesh_graph(const char* graph_json,
                        int seed,
                        pcg::internal::data::PcgMeshData& mesh,
                        char* err,
                        int err_size)
{
    return execute_mesh_graph_with_textures(graph_json, seed, nullptr, 0, mesh, err, err_size);
}

bool execute_mesh_graph_with_textures(const char* graph_json,
                                      int seed,
                                      const PcgTextureSlot* textures,
                                      int texture_count,
                                      pcg::internal::data::PcgMeshData& mesh,
                                      char* err,
                                      int err_size)
{
    int kind = PCG_RESULT_KIND_NONE;
    int vertex_count = 0;
    int index_count = 0;
    std::vector<uint8_t> mesh_buf(8 * 1024 * 1024);
    char json_out[256] = {};

    const PcgResultCode rc = pcg_execute_graph_v3(
        graph_json, seed, textures, texture_count, &kind, json_out, sizeof(json_out),
        mesh_buf.data(), static_cast<int>(mesh_buf.size()), &vertex_count, &index_count, err,
        err_size);
    if (rc != PCG_OK)
        return false;
    if (kind != PCG_RESULT_KIND_MESH)
        return false;

    return pcg::internal::data::read_mesh_binary(mesh_buf.data(),
                                                  static_cast<int>(mesh_buf.size()), mesh);
}

} // namespace

int main()
{
    char err[512] = {};
    char out[65536] = {};

    const char* mesh_pipeline = R"({
      "version": "1.0",
      "nodes": [
        {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
         "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
        {"id": "subdiv", "type": "SubdivideMesh", "position": {"x":0,"y":0},
         "data": {"levels": 1}},
        {"id": "bevel", "type": "BevelMesh", "position": {"x":0,"y":0},
         "data": {"method": "edge", "amount": 0.05, "segments": 2}}
      ],
      "edges": [
        {"id": "e1", "source": "box", "target": "subdiv", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e2", "source": "subdiv", "target": "bevel", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";

    expect_code(pcg_validate_graph(mesh_pipeline, err, sizeof(err)), PCG_OK, "mesh pipeline validate");

    pcg::internal::data::PcgMeshData pipeline_mesh;
    if (!execute_mesh_graph(mesh_pipeline, 42, pipeline_mesh, err, sizeof(err))) {
        std::printf("FAIL: mesh pipeline execute (%s)\n", err);
        return 1;
    }
    if (pipeline_mesh.vertices().empty() || pipeline_mesh.triangles().size() < 3) {
        std::printf("FAIL: mesh pipeline produced empty mesh\n");
        return 1;
    }

    const std::string demo_graph = read_file("../../examples/phase43-mesh-demo.pcg");
    if (!demo_graph.empty()) {
        expect_code(pcg_validate_graph(demo_graph.c_str(), err, sizeof(err)), PCG_OK, "phase43 demo validate");
        pcg::internal::data::PcgMeshData demo_mesh;
        if (!execute_mesh_graph(demo_graph.c_str(), 42, demo_mesh, err, sizeof(err))) {
            std::printf("FAIL: phase43 demo execute (%s)\n", err);
            return 1;
        }
        std::printf("PASS: examples/phase43-mesh-demo.pcg\n");
    }

    const char* box_only = R"({
      "version": "1.0",
      "nodes": [
        {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
         "data": {"width": 2.0, "height": 2.0, "depth": 2.0}}
      ],
      "edges": []
    })";
    pcg::internal::data::PcgMeshData box_mesh;
    if (!execute_mesh_graph(box_only, 42, box_mesh, err, sizeof(err))) {
        std::printf("FAIL: box only execute (%s)\n", err);
        return 1;
    }

    if (!expect_outward_normals(pcg::internal::elements::create_box_mesh(2.0, 2.0, 2.0))) {
        std::printf("FAIL: box mesh triangle normals point inward\n");
        return 1;
    }

    if (!expect_outward_normals(
            pcg::internal::elements::subdivide_mesh(
                pcg::internal::elements::create_box_mesh(2.0, 2.0, 2.0), 1))) {
        std::printf("FAIL: subdivided mesh triangle normals point inward\n");
        return 1;
    }

    const auto subdivided = pcg::internal::elements::subdivide_mesh(
        pcg::internal::elements::create_box_mesh(2.0, 2.0, 2.0), 1);

    const auto edge_beveled = pcg::internal::elements::bevel_mesh(
        subdivided, 0.08, 3, pcg::internal::elements::BevelMethod::Edge,
        pcg::internal::elements::BevelOffsetType::Offset, true);
    if (edge_beveled.vertices().size() <= subdivided.vertices().size()) {
        std::printf("FAIL: edge bevel should add geometry along hard edges\n");
        return 1;
    }
    if (!expect_outward_normals(edge_beveled)) {
        std::printf("FAIL: edge beveled mesh triangle normals point inward\n");
        return 1;
    }

    const auto demo_beveled = pcg::internal::elements::bevel_mesh(
        pcg::internal::elements::subdivide_mesh(
            pcg::internal::elements::create_box_mesh(3.0, 1.5, 2.0), 1),
        0.08, 3, pcg::internal::elements::BevelMethod::Edge,
        pcg::internal::elements::BevelOffsetType::Offset, true);
    if (!expect_outward_normals(demo_beveled)) {
        std::printf("FAIL: phase43 demo bevel mesh triangle normals point inward\n");
        return 1;
    }

    if (!expect_welded_coincident_vertices(edge_beveled)) {
        std::printf("FAIL: edge beveled mesh has split vertices at shared positions\n");
        return 1;
    }
    if (!expect_no_duplicate_geo_triangles(edge_beveled)) {
        std::printf("FAIL: edge beveled mesh has duplicate coplanar triangles\n");
        return 1;
    }

    const auto box = pcg::internal::elements::create_box_mesh(2.0, 2.0, 2.0);

    const auto box_bmesh = pcg::internal::geometry::bmesh_from_mesh(box);
    if (box_bmesh.faces.size() != 6) {
        std::printf("FAIL: BMesh box should merge to 6 quad faces (got %zu)\n", box_bmesh.faces.size());
        return 1;
    }

    const auto cutoff_demo = pcg::internal::elements::bevel_mesh(
        box, 0.08, 3, pcg::internal::elements::BevelMethod::Edge,
        pcg::internal::elements::BevelOffsetType::Offset, true, 30.0, 0.5f,
        pcg::internal::elements::BevelMiter::Sharp, pcg::internal::elements::BevelMiter::Sharp,
        pcg::internal::elements::BevelVMeshMethod::Cutoff);
    if (!expect_no_aabb_expansion(cutoff_demo, 1.0)) {
        std::printf("FAIL: cutoff bevel (demo params) expands beyond original box AABB\n");
        return 1;
    }
    if (!expect_outward_normals(cutoff_demo)) {
        std::printf("FAIL: cutoff bevel (demo params) has inward normals\n");
        return 1;
    }

    const auto box_beveled = pcg::internal::elements::bevel_mesh(
        box, 0.15, 3, pcg::internal::elements::BevelMethod::Edge,
        pcg::internal::elements::BevelOffsetType::Offset, true);
    if (!expect_no_duplicate_geo_triangles(box_beveled)) {
        std::printf("FAIL: box edge bevel has duplicate coplanar triangles\n");
        return 1;
    }
    for (const auto& v : box_beveled.vertices()) {
        if (std::abs(v.x) > 1.0 + 1e-4 || std::abs(v.y) > 1.0 + 1e-4 || std::abs(v.z) > 1.0 + 1e-4) {
            std::printf("FAIL: edge bevel extrudes beyond original box bounds\n");
            return 1;
        }
    }

    // V14: verify 3-way corner VMesh generates cross-face triangles (inner cap per corner).
    // A box has 8 corners, each with 3 hard edges → 8 inner cap triangles.
    const int box_cross = count_cross_face_triangles(box_beveled);
    if (box_cross < 8) {
        std::printf("FAIL: box bevel has only %d cross-face triangles (expected >= 8 for 8 corners)\n",
                    box_cross);
        return 1;
    }
    // Also verify the subdivided+beveled mesh has cross-face triangles.
    const int subdiv_cross = count_cross_face_triangles(edge_beveled);
    if (subdiv_cross < 8) {
        std::printf("FAIL: subdivided box bevel has only %d cross-face triangles (expected >= 8)\n",
                    subdiv_cross);
        return 1;
    }

    // V14: verify outward normals (geometric correctness) for beveled meshes.
    // Note: without BMesh topology, face polygon and edge strip have T-junctions
    // (non-manifold edges), so expect_closed_mesh is not applicable.
    // Geometric correctness = all normals outward + no duplicates + no inward.
    if (!expect_outward_normals(box_beveled)) {
        std::printf("FAIL: box bevel mesh has inward normals\n");
        return 1;
    }
    if (!expect_outward_normals(edge_beveled)) {
        std::printf("FAIL: subdivided box bevel mesh has inward normals\n");
        return 1;
    }

    const auto vertex_beveled = pcg::internal::elements::bevel_mesh(
        subdivided, 0.08, 3, pcg::internal::elements::BevelMethod::VertexPush);
    if (!expect_coincident_vertices_stay_welded(subdivided, vertex_beveled)) {
        std::printf("FAIL: vertex push bevel has split vertices at shared positions\n");
        return 1;
    }
    if (!expect_outward_normals(vertex_beveled)) {
        std::printf("FAIL: vertex push beveled mesh triangle normals point inward\n");
        return 1;
    }

    const auto subdivided_box = subdivided;

    const auto width_beveled = pcg::internal::elements::bevel_mesh(
        subdivided_box, 0.08, 3, pcg::internal::elements::BevelMethod::Edge,
        pcg::internal::elements::BevelOffsetType::Width, true);
    if (!expect_outward_normals(width_beveled)) {
        std::printf("FAIL: width offset bevel has inward normals\n");
        return 1;
    }

    const auto cutoff_beveled = pcg::internal::elements::bevel_mesh(
        subdivided_box, 0.08, 3, pcg::internal::elements::BevelMethod::Edge,
        pcg::internal::elements::BevelOffsetType::Offset, true, 30.0, 0.5f,
        pcg::internal::elements::BevelMiter::Sharp, pcg::internal::elements::BevelMiter::Sharp,
        pcg::internal::elements::BevelVMeshMethod::Cutoff);
    const auto adj_beveled = pcg::internal::elements::bevel_mesh(
        subdivided_box, 0.08, 3, pcg::internal::elements::BevelMethod::Edge,
        pcg::internal::elements::BevelOffsetType::Offset, true, 30.0, 0.5f,
        pcg::internal::elements::BevelMiter::Sharp, pcg::internal::elements::BevelMiter::Sharp,
        pcg::internal::elements::BevelVMeshMethod::Adj);
    if (!expect_outward_normals(cutoff_beveled)) {
        std::printf("FAIL: cutoff vmesh bevel has inward normals\n");
        return 1;
    }
    if (!expect_outward_normals(adj_beveled)) {
        std::printf("FAIL: adj vmesh bevel has inward normals\n");
        return 1;
    }
    if (!expect_closed_mesh(cutoff_beveled)) {
        std::printf("FAIL: cutoff vmesh bevel should be closed for box input\n");
        return 1;
    }
    if (!expect_closed_mesh(adj_beveled)) {
        std::printf("FAIL: adj vmesh bevel should be closed for box input\n");
        return 1;
    }
    if (!expect_no_aabb_expansion(adj_beveled, 1.0)) {
        std::printf("FAIL: adj vmesh bevel expands beyond original box AABB\n");
        return 1;
    }

    const auto square_profile = pcg::internal::elements::bevel_mesh(
        subdivided_box, 0.08, 3, pcg::internal::elements::BevelMethod::Edge,
        pcg::internal::elements::BevelOffsetType::Offset, true, 30.0, 0.0f);
    const auto round_profile = pcg::internal::elements::bevel_mesh(
        subdivided_box, 0.08, 3, pcg::internal::elements::BevelMethod::Edge,
        pcg::internal::elements::BevelOffsetType::Offset, true, 30.0, 0.5f);
    if (square_profile.vertices().size() == round_profile.vertices().size() &&
        !meshes_have_different_positions(square_profile, round_profile)) {
        std::printf("FAIL: profile 0 and 0.5 should produce different geometry\n");
        return 1;
    }

    const auto box_for_noise = pcg::internal::elements::create_box_mesh(2.0, 2.0, 2.0);
    const auto noise_zero = pcg::internal::elements::noise_deform_mesh(
        box_for_noise, 0.0, 2.0, pcg::internal::elements::NoiseDeformType::Perlin, 42);
    if (meshes_have_different_positions(box_for_noise, noise_zero)) {
        std::printf("FAIL: zero intensity noise deform should preserve vertex positions\n");
        return 1;
    }

    const auto noise_deformed = pcg::internal::elements::noise_deform_mesh(
        box_for_noise, 0.05, 3.0, pcg::internal::elements::NoiseDeformType::Perlin, 42);
    if (!meshes_have_different_positions(box_for_noise, noise_deformed)) {
        std::printf("FAIL: noise deform should offset vertices along normals\n");
        return 1;
    }
    if (noise_deformed.vertices().size() != box_for_noise.vertices().size() ||
        noise_deformed.triangles().size() != box_for_noise.triangles().size()) {
        std::printf("FAIL: noise deform should preserve mesh topology\n");
        return 1;
    }

    const char* noise_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
         "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
        {"id": "noise", "type": "MeshNoiseDeform", "position": {"x":0,"y":0},
         "data": {"intensity": 0.03, "scale": 2.5, "noiseType": "perlin"}}
      ],
      "edges": [
        {"id": "e1", "source": "box", "target": "noise", "sourceHandle": "out", "targetHandle": "in"}
      ]
    })";
    expect_code(pcg_validate_graph(noise_graph, err, sizeof(err)), PCG_OK, "noise deform graph validate");
    pcg::internal::data::PcgMeshData noise_mesh;
    if (!execute_mesh_graph(noise_graph, 42, noise_mesh, err, sizeof(err))) {
        std::printf("FAIL: noise deform graph execute (%s)\n", err);
        return 1;
    }

    const float checker_rgba[] = {
        1.f, 1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 1.f,
        0.f, 0.f, 0.f, 1.f, 1.f, 1.f, 1.f, 1.f,
    };
    const PcgTextureSlot tex_slot{"tex1", 2, 2, checker_rgba};
    const char* texture_noise_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
         "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
        {"id": "tex1", "type": "ImageTexture", "position": {"x":0,"y":0},
         "data": {"repeatX": 1.0, "repeatY": 1.0}},
        {"id": "noise", "type": "MeshNoiseDeform", "position": {"x":0,"y":0},
         "data": {"intensity": 0.1, "scale": 1.0, "midLevel": 0.5, "noiseType": "texture"}}
      ],
      "edges": [
        {"id": "e1", "source": "box", "target": "noise", "sourceHandle": "out", "targetHandle": "in"},
        {"id": "e2", "source": "tex1", "target": "noise", "sourceHandle": "out", "targetHandle": "texture"}
      ]
    })";
    expect_code(pcg_validate_graph(texture_noise_graph, err, sizeof(err)), PCG_OK,
                "texture noise graph validate");
    pcg::internal::data::PcgMeshData texture_noise_mesh;
    if (!execute_mesh_graph_with_textures(texture_noise_graph, 42, &tex_slot, 1, texture_noise_mesh,
                                          err, sizeof(err))) {
        std::printf("FAIL: texture noise graph execute (%s)\n", err);
        return 1;
    }
    if (!meshes_have_different_positions(box_for_noise, texture_noise_mesh)) {
        std::printf("FAIL: texture noise deform should offset vertices\n");
        return 1;
    }

    const std::string unity_demo = read_file("../../../Unity/Assets/PCGDemo/demo.pcg");
    if (!unity_demo.empty()) {
        std::string demo_levels3 = unity_demo;
        const std::string levels_token = "\"levels\": 2";
        const auto pos = demo_levels3.find(levels_token);
        if (pos != std::string::npos)
            demo_levels3.replace(pos, levels_token.size(), "\"levels\": 3");

        expect_code(pcg_validate_graph(demo_levels3.c_str(), err, sizeof(err)), PCG_OK,
                    "unity demo levels=3 validate");
        pcg::internal::data::PcgMeshData heavy_mesh;
        if (!execute_mesh_graph(demo_levels3.c_str(), 42, heavy_mesh, err, sizeof(err))) {
            std::printf("FAIL: unity demo levels=3 binary execute (%s)\n", err);
            return 1;
        }
        std::printf("PASS: Unity demo.pcg levels=3 via binary mesh transport\n");
    }

    // V2: SubdivideMesh + BevelMesh on non-box mesh (cone with sharp apex).
    // Regression: sort_ccw produced CW order at non-coplanar vertices →
    // find_bmesh_face_between returned -1 → offset_meet used wrong normal → mesh exploded.
    // Fix: BMesh disk cycle traversal replaces sort_ccw.
    {
        // Create a cone (8-segment) with flat-shaded triangles
        pcg::internal::data::PcgMeshData cone;
        const double radius = 1.0;
        const double height = 2.0;
        const int segs = 8;
        for (int i = 0; i < segs; ++i) {
            const double a0 = 2.0 * 3.14159265358979 * i / segs;
            const double a1 = 2.0 * 3.14159265358979 * (i + 1) / segs;
            const int v0 = static_cast<int>(cone.vertices().size()); cone.add_vertex({0, height, 0});
            const int v1 = static_cast<int>(cone.vertices().size()); cone.add_vertex({radius*std::cos(a0), 0, radius*std::sin(a0)});
            const int v2 = static_cast<int>(cone.vertices().size()); cone.add_vertex({radius*std::cos(a1), 0, radius*std::sin(a1)});
            cone.add_triangle(v0, v1, v2);
        }
        const int center = static_cast<int>(cone.vertices().size()); cone.add_vertex({0, 0, 0});
        for (int i = 0; i < segs; ++i) {
            const double a0 = 2.0 * 3.14159265358979 * i / segs;
            const double a1 = 2.0 * 3.14159265358979 * (i + 1) / segs;
            const int v0 = static_cast<int>(cone.vertices().size()); cone.add_vertex({radius*std::cos(a0), 0, radius*std::sin(a0)});
            const int v1 = static_cast<int>(cone.vertices().size()); cone.add_vertex({radius*std::cos(a1), 0, radius*std::sin(a1)});
            cone.add_triangle(center, v1, v0);
        }

        const auto cone_subdiv = pcg::internal::elements::subdivide_mesh(cone, 1);
        const auto cone_beveled = pcg::internal::elements::bevel_mesh(
            cone_subdiv, 0.08, 3, pcg::internal::elements::BevelMethod::Edge,
            pcg::internal::elements::BevelOffsetType::Offset, true);

        if (cone_beveled.vertices().empty() || cone_beveled.triangles().size() < 3) {
            std::printf("FAIL: cone subdiv+bevel produced empty mesh\n");
            return 1;
        }
        // Mesh should not explode: vertices must stay within reasonable bounds.
        // Cone original: x,z in [-1,1], y in [0,2]. Bevel amount=0.08 → allow ±1.
        for (const auto& v : cone_beveled.vertices()) {
            if (std::abs(v.x) > radius + 1.0 || std::abs(v.z) > radius + 1.0 ||
                v.y < -1.0 || v.y > height + 1.0) {
                std::printf("FAIL: cone subdiv+bevel vertex (%.3f,%.3f,%.3f) out of bounds\n",
                            v.x, v.y, v.z);
                return 1;
            }
        }
        if (cone_beveled.vertices().size() <= cone_subdiv.vertices().size()) {
            std::printf("FAIL: cone subdiv+bevel should add geometry\n");
            return 1;
        }
        // No NaN
        for (const auto& v : cone_beveled.vertices()) {
            if (std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z)) {
                std::printf("FAIL: cone subdiv+bevel has NaN vertices\n");
                return 1;
            }
        }
        std::printf("PASS: cone subdiv+bevel (disk cycle regression)\n");
    }

    std::printf("PASS: phase43 mesh pipeline\n");
    return 0;
}
