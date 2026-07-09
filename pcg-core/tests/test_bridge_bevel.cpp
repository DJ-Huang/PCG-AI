#include "pcg_api.h"

#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_mesh_data.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
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
    return 0;
}
