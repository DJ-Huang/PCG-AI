#include "pcg_api.h"

#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_mesh_data.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

std::string read_file(const char* path)
{
    std::ifstream file(path);
    if (!file)
        return {};
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void expect_code(PcgResultCode actual, PcgResultCode expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL: %s expected %d got %d\n", label, static_cast<int>(expected),
                    static_cast<int>(actual));
        std::exit(1);
    }
}

PcgResultCode execute_mesh_graph(const char* json,
                                 int seed,
                                 pcg::internal::data::PcgMeshData& out_mesh,
                                 int& vertex_count,
                                 int& index_count,
                                 char* err,
                                 int err_size)
{
    std::vector<uint8_t> mesh_buf(8 * 1024 * 1024);
    int kind = 0;
    char json_out[4096];
    const PcgResultCode code = pcg_execute_graph_v7(
        json, seed, nullptr, 0, nullptr, 0, nullptr, 0, &kind, json_out, sizeof(json_out),
        mesh_buf.data(), static_cast<int>(mesh_buf.size()), nullptr, 0, nullptr, nullptr,
        &vertex_count, &index_count, nullptr, nullptr, 0, err, err_size);
    if (code != PCG_OK)
        return code;
    if (!pcg::internal::data::read_mesh_binary(mesh_buf.data(),
                                              static_cast<int>(mesh_buf.size()), out_mesh)) {
        std::snprintf(err, static_cast<size_t>(err_size), "failed to parse mesh binary");
        return PCG_ERR_EXECUTION;
    }
    return PCG_OK;
}

std::string with_bevel_amount(std::string graph, const char* amount)
{
    const std::string key = "\"amount\":";
    const size_t pos = graph.find(key);
    if (pos == std::string::npos)
        return graph;
    size_t start = pos + key.size();
    while (start < graph.size() && (graph[start] == ' ' || graph[start] == '\t'))
        ++start;
    size_t end = start;
    while (end < graph.size() &&
           (std::isdigit(static_cast<unsigned char>(graph[end])) || graph[end] == '.' ||
            graph[end] == '-' || graph[end] == 'e' || graph[end] == 'E' || graph[end] == '+'))
        ++end;
    graph.replace(start, end - start, amount);
    return graph;
}

int boundary_edge_count(const pcg::internal::data::PcgMeshData& mesh)
{
    struct EdgeUse {
        int count = 0;
    };
    std::unordered_map<uint64_t, EdgeUse> edges;
    auto edge_key = [](int a, int b) -> uint64_t {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        return (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
               static_cast<uint32_t>(hi);
    };

    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const int tri[3] = {mesh.triangles()[i], mesh.triangles()[i + 1], mesh.triangles()[i + 2]};
        for (int e = 0; e < 3; ++e)
            ++edges[edge_key(tri[e], tri[(e + 1) % 3])].count;
    }

    int boundary = 0;
    for (const auto& [_, use] : edges) {
        if (use.count == 1)
            ++boundary;
    }
    return boundary;
}

void dump_boundary_edges(const pcg::internal::data::PcgMeshData& mesh, int limit = 16)
{
    std::unordered_map<uint64_t, std::pair<int, int>> edges;
    std::unordered_map<uint64_t, int> counts;
    auto edge_key = [](int a, int b) -> uint64_t {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        return (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
               static_cast<uint32_t>(hi);
    };

    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const int tri[3] = {mesh.triangles()[i], mesh.triangles()[i + 1], mesh.triangles()[i + 2]};
        for (int e = 0; e < 3; ++e) {
            const int a = tri[e];
            const int b = tri[(e + 1) % 3];
            const uint64_t key = edge_key(a, b);
            edges[key] = {a, b};
            ++counts[key];
        }
    }

    int printed = 0;
    for (const auto& [key, count] : counts) {
        if (count != 1 || printed >= limit)
            continue;
        const auto [a_idx, b_idx] = edges[key];
        const auto& a = mesh.vertices()[static_cast<size_t>(a_idx)];
        const auto& b = mesh.vertices()[static_cast<size_t>(b_idx)];
        std::printf("  boundary[%d]: (%.3f, %.3f, %.3f) -> (%.3f, %.3f, %.3f)\n",
                    printed, a.x, a.y, a.z, b.x, b.y, b.z);
        ++printed;
    }
}

void dump_boundary_loops(const pcg::internal::data::PcgMeshData& mesh)
{
    std::unordered_map<uint64_t, int> counts;
    std::unordered_map<int, std::vector<int>> adjacency;
    auto edge_key = [](int a, int b) -> uint64_t {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        return (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
               static_cast<uint32_t>(hi);
    };
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const int tri[3] = {mesh.triangles()[i], mesh.triangles()[i + 1], mesh.triangles()[i + 2]};
        for (int e = 0; e < 3; ++e)
            ++counts[edge_key(tri[e], tri[(e + 1) % 3])];
    }
    for (const auto& [key, count] : counts) {
        if (count != 1)
            continue;
        const int a = static_cast<int>(key >> 32);
        const int b = static_cast<int>(key & 0xffffffffu);
        adjacency[a].push_back(b);
        adjacency[b].push_back(a);
    }
    std::unordered_map<int, bool> visited;
    int loop_count = 0;
    for (const auto& [start, _] : adjacency) {
        if (visited[start])
            continue;
        int current = start;
        int previous = -1;
        int length = 0;
        do {
            visited[current] = true;
            ++length;
            int next = -1;
            for (int candidate : adjacency[current]) {
                if (candidate != previous) {
                    next = candidate;
                    break;
                }
            }
            previous = current;
            current = next;
        } while (current >= 0 && current != start && length <= static_cast<int>(adjacency.size()));
        std::printf("  boundary loop[%d]: %d vertices\n", loop_count++, length);
    }
}

int manifold_winding_bad_count(const pcg::internal::data::PcgMeshData& mesh)
{
    struct DirCount {
        int ab = 0;
        int ba = 0;
    };
    std::unordered_map<uint64_t, DirCount> edges;
    auto edge_key = [](int a, int b) -> uint64_t {
        const int lo = std::min(a, b);
        const int hi = std::max(a, b);
        return (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
               static_cast<uint32_t>(hi);
    };

    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const int tri[3] = {mesh.triangles()[i], mesh.triangles()[i + 1], mesh.triangles()[i + 2]};
        for (int e = 0; e < 3; ++e) {
            const int a = tri[e];
            const int b = tri[(e + 1) % 3];
            DirCount& count = edges[edge_key(a, b)];
            if (a < b)
                ++count.ab;
            else
                ++count.ba;
        }
    }

    int bad = 0;
    for (const auto& [_, count] : edges) {
        if (count.ab + count.ba != 2)
            continue;
        if (count.ab != 1 || count.ba != 1)
            ++bad;
    }
    return bad;
}

double signed_volume(const pcg::internal::data::PcgMeshData& mesh)
{
    double vol = 0.0;
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const auto& a = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i])];
        const auto& b = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 1])];
        const auto& c = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 2])];
        vol += a.x * (b.y * c.z - b.z * c.y) +
               a.y * (b.z * c.x - b.x * c.z) +
               a.z * (b.x * c.y - b.y * c.x);
    }
    return vol / 6.0;
}

} // namespace

int main()
{
    const std::string bridge_graph = read_file("../../examples/bridge-demo.pcg");
    if (bridge_graph.empty()) {
        std::printf("FAIL: could not read examples/bridge-demo.pcg\n");
        return 1;
    }

    char err[512] = {};
    expect_code(pcg_validate_graph(bridge_graph.c_str(), err, sizeof(err)), PCG_OK,
                "bridge-demo validate");

    pcg::internal::data::PcgMeshData mesh_zero;
    int verts_zero = 0;
    int indices_zero = 0;
    const std::string graph_zero = with_bevel_amount(bridge_graph, "0");
    expect_code(execute_mesh_graph(graph_zero.c_str(), 42, mesh_zero, verts_zero, indices_zero, err,
                                   sizeof(err)),
                PCG_OK, "bridge-demo amount=0");

    pcg::internal::data::PcgMeshData mesh;
    int vertex_count = 0;
    int index_count = 0;
    const auto start = std::chrono::steady_clock::now();
    expect_code(execute_mesh_graph(bridge_graph.c_str(), 42, mesh, vertex_count, index_count, err,
                                   sizeof(err)),
                PCG_OK, "bridge-demo GroupCreate+Bevel execute");
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    if (vertex_count < 300 || index_count < 300) {
        std::printf("FAIL: bridge mesh too small (%d verts, %d indices)\n", vertex_count,
                    index_count);
        return 1;
    }

    // Bevel must change topology vs amount=0 (profile insertion / edge strips).
    if (vertex_count <= verts_zero + 50) {
        std::printf("FAIL: bevel did not increase topology enough (amount=0 -> %d, beveled -> %d)\n",
                    verts_zero, vertex_count);
        return 1;
    }

    if (elapsed_ms.count() > 1000) {
        std::printf("FAIL: bridge-demo cook too slow (%lld ms)\n",
                    static_cast<long long>(elapsed_ms.count()));
        return 1;
    }

    std::printf("PASS: bridge-demo GroupCreate+Bevel (%d verts, %d indices, %lld ms, zero=%d)\n",
                vertex_count, index_count, static_cast<long long>(elapsed_ms.count()), verts_zero);

    const std::string sedan_graph = read_file("../../examples/lowpoly-sedan.pcg");
    if (sedan_graph.empty()) {
        std::printf("FAIL: could not read examples/lowpoly-sedan.pcg\n");
        return 1;
    }

    expect_code(pcg_validate_graph(sedan_graph.c_str(), err, sizeof(err)), PCG_OK,
                "lowpoly-sedan validate");

    pcg::internal::data::PcgMeshData sedan_mesh;
    int sedan_vertices = 0;
    int sedan_indices = 0;
    expect_code(execute_mesh_graph(sedan_graph.c_str(), 42, sedan_mesh, sedan_vertices, sedan_indices,
                                   err, sizeof(err)),
                PCG_OK, "lowpoly-sedan execute");

    if (sedan_vertices < 1000 || sedan_indices < 3000) {
        std::printf("FAIL: lowpoly-sedan mesh too small (%d verts, %d indices)\n",
                    sedan_vertices, sedan_indices);
        return 1;
    }

    std::printf("PASS: lowpoly-sedan Sweep+Bevel (%d verts, %d indices)\n",
                sedan_vertices, sedan_indices);

    const char* sedan_body_graph = R"({
      "version": "1.0",
      "nodes": [
        {
          "id": "body_path",
          "type": "CreateSpline",
          "position": { "x": 200, "y": 0 },
          "data": {
            "mode": "line",
            "closed": false,
            "subdivisions": 1,
            "controlPoints": "[{\"x\":0,\"y\":0,\"z\":0},{\"x\":4.2,\"y\":0,\"z\":0}]",
            "editPlane": "none"
          }
        },
        {
          "id": "body_profile",
          "type": "CreateSpline",
          "position": { "x": 420, "y": 0 },
          "data": {
            "mode": "catmullRom",
            "closed": true,
            "subdivisions": 8,
            "controlPoints": "[{\"x\":0.9,\"y\":0,\"z\":0},{\"x\":0.95,\"y\":0.2,\"z\":0},{\"x\":0.85,\"y\":0.5,\"z\":0},{\"x\":0.6,\"y\":0.78,\"z\":0},{\"x\":0.25,\"y\":0.9,\"z\":0},{\"x\":-0.25,\"y\":0.9,\"z\":0},{\"x\":-0.6,\"y\":0.78,\"z\":0},{\"x\":-0.85,\"y\":0.5,\"z\":0},{\"x\":-0.95,\"y\":0.2,\"z\":0},{\"x\":-0.9,\"y\":0,\"z\":0}]",
            "editPlane": "xy"
          }
        },
        {
          "id": "body_sweep",
          "type": "SweepAlongSpline",
          "position": { "x": 200, "y": 160 },
          "data": {
            "surfaceShape": "crossSection",
            "sampleSpacing": 0.5,
            "capStart": true,
            "capEnd": true,
            "upX": 0, "upY": 1, "upZ": 0,
            "scaleStart": 1.0, "scaleEnd": 1.0,
            "profilePlane": "xy"
          }
        },
        {
          "id": "body_group",
          "type": "GroupCreate",
          "position": { "x": 200, "y": 320 },
          "data": {
            "outputGroup": "bevel_edges",
            "domain": "edge",
            "mode": "angle",
            "minEdgeAngle": 30,
            "includeUnshared": false,
            "fromFaceGroup": "",
            "fromEdgeGroup": "profile_corner"
          }
        },
        {
          "id": "body_bevel",
          "type": "BevelMesh",
          "position": { "x": 200, "y": 480 },
          "data": {
            "method": "edge",
            "amount": 0.06,
            "segments": 2,
            "clampOverlap": true,
            "edgeGroup": "bevel_edges",
            "excludeUnshared": true,
            "excludeGroups": "cap_start,cap_end"
          }
        },
        {
          "id": "out",
          "type": "Output",
          "position": { "x": 200, "y": 640 },
          "data": { "label": "SedanBody" }
        }
      ],
      "edges": [
        { "id": "e1", "source": "body_path", "target": "body_sweep", "sourceHandle": "out", "targetHandle": "backbone" },
        { "id": "e2", "source": "body_profile", "target": "body_sweep", "sourceHandle": "out", "targetHandle": "profile" },
        { "id": "e3", "source": "body_sweep", "target": "body_group", "sourceHandle": "out", "targetHandle": "in" },
        { "id": "e4", "source": "body_group", "target": "body_bevel", "sourceHandle": "out", "targetHandle": "in" },
        { "id": "e5", "source": "body_bevel", "target": "out", "sourceHandle": "out", "targetHandle": "in" }
      ]
    })";

    expect_code(pcg_validate_graph(sedan_body_graph, err, sizeof(err)), PCG_OK, "sedan body validate");

    pcg::internal::data::PcgMeshData sedan_body_mesh;
    int sedan_body_vertices = 0;
    int sedan_body_indices = 0;
    expect_code(execute_mesh_graph(sedan_body_graph, 42, sedan_body_mesh, sedan_body_vertices,
                                   sedan_body_indices, err, sizeof(err)),
                PCG_OK, "sedan body execute");

    const int sedan_body_boundary = boundary_edge_count(sedan_body_mesh);
    if (sedan_body_boundary != 0) {
        std::printf("FAIL: sedan body Sweep+Bevel has boundary edges (%d)\n", sedan_body_boundary);
        dump_boundary_edges(sedan_body_mesh);
        dump_boundary_loops(sedan_body_mesh);
        return 1;
    }

    const int sedan_body_bad = manifold_winding_bad_count(sedan_body_mesh);
    if (sedan_body_bad != 0) {
        std::printf("FAIL: sedan body Sweep+Bevel has opposite-winding manifold edges (%d)\n",
                    sedan_body_bad);
        return 1;
    }

    if (signed_volume(sedan_body_mesh) <= 0.0) {
        std::printf("FAIL: sedan body Sweep+Bevel signed volume <= 0\n");
        return 1;
    }

    std::printf("PASS: sedan body Sweep+Bevel (%d verts, %d indices)\n",
                sedan_body_vertices, sedan_body_indices);
    return 0;
}
