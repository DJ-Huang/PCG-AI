#include "pcg_api.h"

#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_mesh_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using pcg::internal::data::PcgMeshData;

void expect(bool condition, const char* message)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition)
        std::exit(1);
}

PcgMeshData execute_mesh_graph(const std::string& graph)
{
    std::vector<uint8_t> mesh_buffer(1024 * 1024);
    char json_output[1024] = {};
    char error[512] = {};
    int kind = PCG_RESULT_KIND_NONE;
    int vertex_count = 0;
    int index_count = 0;

    const PcgResultCode code = pcg_execute_graph_v7(
        graph.c_str(), 42, nullptr, 0, nullptr, 0, nullptr, 0, &kind,
        json_output, sizeof(json_output), mesh_buffer.data(),
        static_cast<int>(mesh_buffer.size()), nullptr, 0, nullptr, nullptr,
        &vertex_count, &index_count, nullptr, nullptr, 0, error, sizeof(error));
    expect(code == PCG_OK, error[0] == '\0' ? "compact Sweep+Bevel graph executes" : error);
    expect(kind == PCG_RESULT_KIND_MESH, "compact graph returns a mesh");

    PcgMeshData mesh;
    expect(pcg::internal::data::read_mesh_binary(
               mesh_buffer.data(), static_cast<int>(mesh_buffer.size()), mesh),
           "compact graph mesh binary is readable");
    return mesh;
}

std::vector<int> weld_by_position(const PcgMeshData& mesh)
{
    std::unordered_map<std::string, int> ids;
    std::vector<int> welded(mesh.vertices().size());
    int next_id = 0;
    for (size_t i = 0; i < mesh.vertices().size(); ++i) {
        const auto& vertex = mesh.vertices()[i];
        const auto quantize = [](float value) {
            return static_cast<long long>(std::llround(value * 1e6));
        };
        const std::string key = std::to_string(quantize(vertex.x)) + "," +
                                std::to_string(quantize(vertex.y)) + "," +
                                std::to_string(quantize(vertex.z));
        const auto [it, inserted] = ids.emplace(key, next_id);
        if (inserted)
            ++next_id;
        welded[i] = it->second;
    }
    return welded;
}

struct TopologyStats {
    int boundary_edges = 0;
    int bad_winding_edges = 0;
};

TopologyStats analyze_topology(const PcgMeshData& mesh)
{
    struct EdgeUse {
        int forward = 0;
        int backward = 0;
    };

    const std::vector<int> welded = weld_by_position(mesh);
    std::unordered_map<uint64_t, EdgeUse> edges;
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const int triangle[3] = {
            welded[static_cast<size_t>(mesh.triangles()[i])],
            welded[static_cast<size_t>(mesh.triangles()[i + 1])],
            welded[static_cast<size_t>(mesh.triangles()[i + 2])],
        };
        for (int edge = 0; edge < 3; ++edge) {
            const int a = triangle[edge];
            const int b = triangle[(edge + 1) % 3];
            const int lo = std::min(a, b);
            const int hi = std::max(a, b);
            const uint64_t key =
                (static_cast<uint64_t>(static_cast<uint32_t>(lo)) << 32) |
                static_cast<uint32_t>(hi);
            EdgeUse& use = edges[key];
            if (a == lo)
                ++use.forward;
            else
                ++use.backward;
        }
    }

    TopologyStats stats;
    for (const auto& [_, use] : edges) {
        const int count = use.forward + use.backward;
        if (count == 1)
            ++stats.boundary_edges;
        else if (count == 2 && (use.forward != 1 || use.backward != 1))
            ++stats.bad_winding_edges;
    }
    return stats;
}

double signed_volume(const PcgMeshData& mesh)
{
    double volume = 0.0;
    for (size_t i = 0; i + 2 < mesh.triangles().size(); i += 3) {
        const auto& a = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i])];
        const auto& b = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 1])];
        const auto& c = mesh.vertices()[static_cast<size_t>(mesh.triangles()[i + 2])];
        volume += a.x * (b.y * c.z - b.z * c.y) +
                  a.y * (b.z * c.x - b.x * c.z) +
                  a.z * (b.x * c.y - b.y * c.x);
    }
    return volume / 6.0;
}

std::string build_graph(bool cap_start, bool cap_end)
{
    std::string graph = R"({
      "version":"1.0",
      "nodes":[
        {"id":"path","type":"CreateSpline","data":{
          "mode":"line","closed":false,"subdivisions":1,
          "controlPoints":"[{\"x\":0,\"y\":0,\"z\":0},{\"x\":2,\"y\":0,\"z\":0}]",
          "editPlane":"none"
        }},
        {"id":"profile","type":"CreateSpline","data":{
          "mode":"polyline","closed":true,"subdivisions":1,
          "controlPoints":"[{\"x\":-0.4,\"y\":-0.3,\"z\":0},{\"x\":0.4,\"y\":-0.3,\"z\":0},{\"x\":0.4,\"y\":0.3,\"z\":0},{\"x\":-0.4,\"y\":0.3,\"z\":0}]",
          "editPlane":"xy"
        }},
        {"id":"sweep","type":"SweepAlongSpline","data":{
          "surfaceShape":"crossSection","sampleSpacing":1.0,
          "capStart":true,"capEnd":true,"profilePlane":"xy"
        }},
        {"id":"group","type":"GroupCreate","data":{
          "outputGroup":"bevel_edges","domain":"edge","mode":"angle",
          "minEdgeAngle":30,"includeUnshared":false
        }},
        {"id":"bevel","type":"BevelMesh","data":{
          "method":"edge","amount":0.05,"segments":1,"clampOverlap":true,
          "edgeGroup":"bevel_edges","excludeUnshared":true,
          "excludeGroups":"cap_start,cap_end"
        }},
        {"id":"out","type":"Output","data":{}}
      ],
      "edges":[
        {"id":"e1","source":"path","target":"sweep","sourceHandle":"out","targetHandle":"backbone"},
        {"id":"e2","source":"profile","target":"sweep","sourceHandle":"out","targetHandle":"profile"},
        {"id":"e3","source":"sweep","target":"group","sourceHandle":"out","targetHandle":"in"},
        {"id":"e4","source":"group","target":"bevel","sourceHandle":"out","targetHandle":"in"},
        {"id":"e5","source":"bevel","target":"out","sourceHandle":"out","targetHandle":"in"}
      ]
    })";

    const auto replace_bool = [&graph](const char* key, bool value) {
        const size_t position = graph.find(key);
        expect(position != std::string::npos, "cap parameter exists in compact fixture");
        const size_t value_start = position + std::string(key).size();
        graph.replace(value_start, 4, value ? "true" : "false");
    };
    replace_bool("\"capStart\":", cap_start);
    replace_bool("\"capEnd\":", cap_end);
    return graph;
}

} // namespace

int main()
{
    const PcgMeshData closed = execute_mesh_graph(build_graph(true, true));
    const TopologyStats closed_stats = analyze_topology(closed);
    expect(closed_stats.boundary_edges == 0, "closed Sweep+Bevel has no boundary edges");
    expect(closed_stats.bad_winding_edges == 0, "closed Sweep+Bevel has consistent winding");
    expect(signed_volume(closed) > 0.0, "closed Sweep+Bevel has positive volume");

    for (const auto& caps : {std::pair{true, false}, std::pair{false, true}}) {
        const PcgMeshData open = execute_mesh_graph(build_graph(caps.first, caps.second));
        const TopologyStats open_stats = analyze_topology(open);
        expect(open_stats.boundary_edges > 0, "single-cap Sweep+Bevel keeps one open end");
    }

    std::printf("PASS: compact Sweep+Bevel cap regression (3 cases)\n");
    return 0;
}
