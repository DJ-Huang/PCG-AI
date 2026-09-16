#include "pcg_api.h"
#include "data/pcg_mesh_binary.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using json = nlohmann::json;

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

struct ExecResult {
    PcgResultCode code = PCG_OK;
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    std::vector<uint8_t> mesh_buf;
    std::vector<char> json_buf;
    std::string error;
};

ExecResult execute_graph(const char* graph_json, int seed)
{
    ExecResult r;
    r.json_buf.resize(8 * 1024 * 1024);
    r.mesh_buf.resize(8 * 1024 * 1024);
    char err_buf[1024] = {};

    r.code = pcg_execute_graph_v7(
        graph_json, seed,
        nullptr, 0,
        nullptr, 0,
        nullptr, 0,
        &r.kind, r.json_buf.data(), static_cast<int>(r.json_buf.size()),
        r.mesh_buf.data(), static_cast<int>(r.mesh_buf.size()),
        nullptr, 0,
        nullptr, nullptr, &r.vertex_count, &r.index_count,
        nullptr,
        nullptr, 0,
        err_buf, sizeof(err_buf));

    r.error = err_buf;
    return r;
}

uint32_t read_flags(const std::vector<uint8_t>& mesh_buf)
{
    if (mesh_buf.size() < 20)
        return 0;
    uint32_t version = 0;
    std::memcpy(&version, mesh_buf.data() + 4, 4);
    if (version != 2 && version != 3)
        return 0;
    uint32_t flags = 0;
    std::memcpy(&flags, mesh_buf.data() + 16, 4);
    return flags;
}

json parse_json(const std::vector<char>& buf)
{
    int len = 0;
    while (len < static_cast<int>(buf.size()) && buf[len] != 0)
        ++len;
    return json::parse(buf.data(), buf.data() + len);
}

} // namespace

int main()
{
    // --- Test 1: CreateCylinderMesh → Output ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "cyl", "type": "CreateCylinderMesh", "position": {"x":0,"y":0},
             "data": {"radius": 1.0, "height": 2.0, "radialSegments": 8, "heightSegments": 1, "capTop": true, "capBottom": true}},
            {"id": "out", "type": "Output", "position": {"x":200,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "cyl", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_OK, "graph1: cylinder → output succeeds");
        expect(r.kind == PCG_RESULT_KIND_MESH, "graph1: result is mesh");
        expect(r.vertex_count == 48, "graph1: 48 vertices (split normals)");
        expect(r.index_count == 84, "graph1: 84 indices (28 triangles)");
    }

    // --- Test 2: CreateSpline → RevolveMesh → BevelMesh → Output ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "spline", "type": "CreateSpline", "position": {"x":0,"y":0},
             "data": {"mode": "linear", "closed": false, "subdivisions": 4, "start_x": 1.0, "start_y": -1.0, "start_z": 0.0, "end_x": 1.0, "end_y": 1.0, "end_z": 0.0}},
            {"id": "rev", "type": "RevolveMesh", "position": {"x":200,"y":0},
             "data": {"axis": "y", "segments": 8, "capStart": true, "capEnd": true}},
            {"id": "bevel", "type": "BevelMesh", "position": {"x":400,"y":0},
             "data": {"amount": 0.1, "segments": 2}},
            {"id": "out", "type": "Output", "position": {"x":600,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "spline", "target": "rev", "sourceHandle": "out", "targetHandle": "profile"},
            {"id": "e2", "source": "rev", "target": "bevel", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e3", "source": "bevel", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_OK, "graph2: revolve → bevel succeeds");
        expect(r.kind == PCG_RESULT_KIND_MESH, "graph2: result is mesh");
        expect(r.vertex_count > 0, "graph2: has vertices");
    }

    // --- Test 3: CreateSpiralSpline → SweepAlongSpline → Output ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "spiral", "type": "CreateSpiralSpline", "position": {"x":0,"y":0},
             "data": {"radius": 1.0, "pitch": 0.5, "turns": 2.0, "pointsPerTurn": 12, "axis": "y"}},
            {"id": "sweep", "type": "SweepAlongSpline", "position": {"x":200,"y":0},
             "data": {"surfaceShape": "circle", "radius": 0.2, "columns": 8, "sampleSpacing": 0.5, "capStart": true, "capEnd": true}},
            {"id": "out", "type": "Output", "position": {"x":400,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "spiral", "target": "sweep", "sourceHandle": "out", "targetHandle": "backbone"},
            {"id": "e2", "source": "sweep", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_OK, "graph3: spiral → sweep succeeds");
        expect(r.vertex_count > 0, "graph3: has vertices");
    }

    // --- Test 3b: CreateSpline → TaperedSweep → Output ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "path", "type": "CreateSpline", "position": {"x":0,"y":0},
             "data": {"mode": "catmullRom", "subdivisions": 6,
                      "controlPoints": "[{\"x\":0,\"y\":0,\"z\":0},{\"x\":0.3,\"y\":0.4,\"z\":0.1},{\"x\":-0.1,\"y\":0.8,\"z\":0.25},{\"x\":0,\"y\":1.2,\"z\":0.4}]"}},
            {"id": "taper", "type": "TaperedSweep", "position": {"x":0,"y":160},
             "data": {"stations": "[{\"u\":0,\"rx\":0.22,\"rz\":0.16,\"twist\":0},{\"u\":0.55,\"rx\":0.12,\"rz\":0.08,\"twist\":20},{\"u\":1,\"rx\":0,\"rz\":0,\"twist\":35}]",
                      "radiusScale": 0.9, "radialSegments": 10, "sampleSpacing": 0.12,
                      "capStart": true, "capEnd": true, "shadeMode": "smooth"}},
            {"id": "out", "type": "Output", "position": {"x":0,"y":320}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "path", "target": "taper", "sourceHandle": "out", "targetHandle": "backbone"},
            {"id": "e2", "source": "taper", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_OK, "graph3b: spline → tapered sweep succeeds");
        expect(r.kind == PCG_RESULT_KIND_MESH, "graph3b: result is mesh");
        expect(r.vertex_count > 0 && r.index_count > 0, "graph3b: has tapered geometry");
        expect((read_flags(r.mesh_buf) & 0x4u) != 0, "graph3b: tapered sweep exports UVs");
    }

    // --- Test 4: CreateCylinderMesh → UVTexture → VertexColor + Material → AssignMaterial → Output ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "cyl", "type": "CreateCylinderMesh", "position": {"x":0,"y":0},
             "data": {"radius": 1.0, "height": 2.0, "radialSegments": 8, "heightSegments": 1, "capTop": true, "capBottom": true}},
            {"id": "uv", "type": "UVTexture", "position": {"x":200,"y":0},
             "data": {"projection": "cylindrical", "axis": "y", "scaleU": 1.0, "scaleV": 1.0}},
            {"id": "vc", "type": "VertexColor", "position": {"x":400,"y":0},
             "data": {"r": 1.0, "g": 0.5, "b": 0.0, "a": 0.8}},
            {"id": "pbr", "type": "Material", "position": {"x":400,"y":160},
             "data": {"materialName": "yellow_paint", "shaderId": "pcg.standard-pbr",
                      "baseColor": "#ffcc33", "metallic": 0.25, "roughness": 0.35}},
            {"id": "mat", "type": "AssignMaterial", "position": {"x":600,"y":0},
             "data": {"materialName": ""}},
            {"id": "out", "type": "Output", "position": {"x":800,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "cyl", "target": "uv", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e2", "source": "uv", "target": "vc", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e3", "source": "vc", "target": "mat", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e3m", "source": "pbr", "target": "mat", "sourceHandle": "out", "targetHandle": "material"},
            {"id": "e4", "source": "mat", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_OK, "graph4: cylinder → uv → color → material → output succeeds");
        expect(r.kind == PCG_RESULT_KIND_MESH, "graph4: result is mesh");

        uint32_t flags = read_flags(r.mesh_buf);
        expect((flags & 0x2u) != 0, "graph4: binary has colors flag");
        expect((flags & 0x4u) != 0, "graph4: binary has uvs flag");

        auto j = parse_json(r.json_buf);
        expect(j.contains("mesh_metadata"), "graph4: JSON has mesh_metadata");
        if (j.contains("mesh_metadata") && j["mesh_metadata"].is_object()) {
            expect(j["mesh_metadata"].contains("material"), "graph4: mesh_metadata has material");
            if (j["mesh_metadata"].contains("material")) {
                expect(j["mesh_metadata"]["material"] == "yellow_paint",
                       "graph4: material name = yellow_paint");
            }
            expect(j["mesh_metadata"].contains("pbrMaterials"),
                   "graph4: mesh_metadata has PBR material library");
            if (j["mesh_metadata"].contains("pbrMaterials")) {
                const auto& library = j["mesh_metadata"]["pbrMaterials"];
                expect(library.contains("yellow_paint"),
                       "graph4: PBR material keyed by assigned slot name");
                if (library.contains("yellow_paint")) {
                    expect(library["yellow_paint"].value("roughness", -1.0) == 0.35,
                           "graph4: PBR roughness propagated");
                }
            }
        }
    }

    // --- Test 5: ImageTexture → ProjectTexture.texture + Cylinder → ProjectTexture.in → Output ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "tex", "type": "ImageTexture", "position": {"x":0,"y":0},
             "data": {"repeatX": 2.0, "repeatY": 3.0}},
            {"id": "cyl", "type": "CreateCylinderMesh", "position": {"x":0,"y":200},
             "data": {"radius": 1.0, "height": 2.0, "radialSegments": 8, "heightSegments": 1, "capTop": true, "capBottom": true}},
            {"id": "proj", "type": "ProjectTexture", "position": {"x":400,"y":0},
             "data": {"direction": "z", "scaleU": 1.0, "scaleV": 1.0}},
            {"id": "out", "type": "Output", "position": {"x":600,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "tex", "target": "proj", "sourceHandle": "out", "targetHandle": "texture"},
            {"id": "e2", "source": "cyl", "target": "proj", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e3", "source": "proj", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_OK, "graph5: imageTexture → projectTexture succeeds");
        expect(r.kind == PCG_RESULT_KIND_MESH, "graph5: result is mesh");

        uint32_t flags = read_flags(r.mesh_buf);
        expect((flags & 0x4u) != 0, "graph5: binary has uvs flag");
    }

    // --- Test 6: ProjectTexture without texture connection returns error ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "cyl", "type": "CreateCylinderMesh", "position": {"x":0,"y":0},
             "data": {"radius": 1.0, "height": 2.0, "radialSegments": 8, "heightSegments": 1, "capTop": true, "capBottom": true}},
            {"id": "proj", "type": "ProjectTexture", "position": {"x":200,"y":0},
             "data": {"direction": "z"}},
            {"id": "out", "type": "Output", "position": {"x":400,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "cyl", "target": "proj", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e2", "source": "proj", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_ERR_EXECUTION, "graph6: missing texture → error");
    }

    // --- Test 7: Unsupported projection (bogus / legacy box) is rejected ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "cyl", "type": "CreateCylinderMesh", "position": {"x":0,"y":0},
             "data": {"radius": 1.0, "height": 2.0, "radialSegments": 8, "heightSegments": 1, "capTop": true, "capBottom": true}},
            {"id": "uv", "type": "UVTexture", "position": {"x":200,"y":0},
             "data": {"projection": "bogus", "axis": "y"}},
            {"id": "out", "type": "Output", "position": {"x":400,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "cyl", "target": "uv", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e2", "source": "uv", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_ERR_EXECUTION, "graph7: unsupported projection is rejected");
    }

    // --- Test 7b: Legacy box projection is rejected (no fake constant UV) ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
             "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
            {"id": "uv", "type": "UVTexture", "position": {"x":200,"y":0},
             "data": {"projection": "box", "axis": "y"}},
            {"id": "out", "type": "Output", "position": {"x":400,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "box", "target": "uv", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e2", "source": "uv", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_ERR_EXECUTION, "graph7b: box projection rejected until P4");
    }

    // --- Test 8: FaceGroupByNormal drives a material override ---
    {
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":0},
             "data": {"width": 2.0, "height": 2.0, "depth": 2.0}},
            {"id": "top", "type": "FaceGroupByNormal", "position": {"x":200,"y":0},
             "data": {"outputGroup": "top", "directionX": 0.0, "directionY": 1.0, "directionZ": 0.0, "spreadAngle": 5.0}},
            {"id": "base", "type": "AssignMaterial", "position": {"x":400,"y":0},
             "data": {"group": "", "materialName": "body"}},
            {"id": "topMat", "type": "AssignMaterial", "position": {"x":600,"y":0},
             "data": {"group": "top", "materialName": "top_paint"}},
            {"id": "out", "type": "Output", "position": {"x":800,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e1", "source": "box", "target": "top", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e2", "source": "top", "target": "base", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e3", "source": "base", "target": "topMat", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e4", "source": "topMat", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_OK, "graph8: normal group → material override succeeds");
        expect((read_flags(r.mesh_buf) & 0x8u) != 0, "graph8: binary has materials flag");

        pcg::internal::data::PcgMeshData mesh;
        const bool parsed = pcg::internal::data::read_mesh_binary(
            r.mesh_buf.data(), static_cast<int>(r.mesh_buf.size()), mesh);
        expect(parsed, "graph8: multi-material binary parses");
        if (parsed) {
            expect(mesh.material_slots().size() == 2, "graph8: two material slots emitted");
            int top_triangle_count = 0;
            for (size_t i = 0; i < mesh.material_slots().size(); ++i) {
                if (mesh.material_slots()[i] != "top_paint")
                    continue;
                for (uint32_t slot : mesh.triangle_materials())
                    if (slot == i)
                        ++top_triangle_count;
            }
            expect(top_triangle_count == 2, "graph8: top quad maps to two top-material triangles");
        }
    }

    // --- Test 9: ProjectTexture reference crop SSOT (not mesh AABB) ---
    {
        // Box spans X∈[0,0.1], Y∈[0,0.1], Z∈[-0.05,0.05] after translate.
        // Crop frame is X∈[0,1], Y∈[0,1] → tip corner world (0.1,0.1) → UV (0.1,0.1).
        // Mesh-AABB mode would map that corner to UV (1,1).
        const char* graph = R"({
          "version": "1.0",
          "nodes": [
            {"id": "tex", "type": "ImageTexture", "position": {"x":0,"y":0},
             "data": {"repeatX": 1.0, "repeatY": 1.0}},
            {"id": "box", "type": "CreateBoxMesh", "position": {"x":0,"y":200},
             "data": {"width": 0.1, "height": 0.1, "depth": 0.1}},
            {"id": "xf", "type": "TransformMesh", "position": {"x":200,"y":200},
             "data": {"translate": {"x": 0.05, "y": 0.05, "z": 0.0},
                      "scale": {"x": 1.0, "y": 1.0, "z": 1.0}}},
            {"id": "proj", "type": "ProjectTexture", "position": {"x":400,"y":0},
             "data": {"direction": "z", "scaleU": 1.0, "scaleV": 1.0,
                      "offsetU": 0.0, "offsetV": 0.0,
                      "useReferenceBounds": true,
                      "refMinU": 0.0, "refMaxU": 1.0,
                      "refMinV": 0.0, "refMaxV": 1.0}},
            {"id": "out", "type": "Output", "position": {"x":600,"y":0}, "data": {}}
          ],
          "edges": [
            {"id": "e0", "source": "box", "target": "xf", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e1", "source": "tex", "target": "proj", "sourceHandle": "out", "targetHandle": "texture"},
            {"id": "e2", "source": "xf", "target": "proj", "sourceHandle": "out", "targetHandle": "in"},
            {"id": "e3", "source": "proj", "target": "out", "sourceHandle": "out", "targetHandle": "in"}
          ]
        })";
        auto r = execute_graph(graph, 42);
        expect(r.code == PCG_OK, "graph9: crop ProjectTexture succeeds");
        expect((read_flags(r.mesh_buf) & 0x4u) != 0, "graph9: binary has uvs flag");

        pcg::internal::data::PcgMeshData mesh;
        const bool parsed = pcg::internal::data::read_mesh_binary(
            r.mesh_buf.data(), static_cast<int>(r.mesh_buf.size()), mesh);
        expect(parsed && mesh.has_uvs(), "graph9: mesh parses with uvs");
        if (parsed && mesh.has_uvs()) {
            double max_u = -1e9, max_v = -1e9;
            for (const auto& uv : mesh.uvs()) {
                max_u = std::max(max_u, uv.u);
                max_v = std::max(max_v, uv.v);
            }
            // Corner at world ~0.1 maps into crop [0,1] → ~0.1, not AABB-normalized 1.0
            expect(max_u < 0.25 && max_u > 0.05, "graph9: max U follows crop (~0.1), not AABB");
            expect(max_v < 0.25 && max_v > 0.05, "graph9: max V follows crop (~0.1), not AABB");
        }
    }

    if (g_fail > 0) {
        std::printf("\n%d tests FAILED\n", g_fail);
        return 1;
    }
    std::printf("\nAll new nodes graph tests passed\n");
    return 0;
}
