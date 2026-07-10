#include "pcg_api.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::string read_file(const char* path)
{
    std::ifstream file(path);
    if (!file)
        return {};
    std::ostringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

int main()
{
    const std::string graph = read_file("../../examples/stone-arch-bridge.pcg");
    if (graph.empty()) {
        std::printf("FAIL: could not read examples/stone-arch-bridge.pcg\n");
        return 1;
    }

    // 1. Validate
    char err[512] = {};
    if (pcg_validate_graph(graph.c_str(), err, sizeof(err)) != PCG_OK) {
        std::printf("FAIL: validate: %s\n", err);
        return 1;
    }
    std::printf("OK: validate passed\n");

    // 2. Execute
    std::vector<uint8_t> mesh_buf(8 * 1024 * 1024);
    int kind = 0;
    char json_out[4096] = {};
    int vertex_count = 0;
    int index_count = 0;

    const PcgResultCode code = pcg_execute_graph_v7(
        graph.c_str(), 42,
        nullptr, 0, nullptr, 0, nullptr, 0,
        &kind, json_out, sizeof(json_out),
        mesh_buf.data(), static_cast<int>(mesh_buf.size()),
        nullptr, 0, nullptr, nullptr,
        &vertex_count, &index_count,
        nullptr, nullptr, 0,
        err, sizeof(err));

    if (code != PCG_OK) {
        std::printf("FAIL: execute: %s\n", err);
        return 1;
    }

    std::printf("PASS: stone-arch-bridge execute (%d verts, %d indices, kind=%d)\n",
                vertex_count, index_count, kind);

    if (vertex_count < 100) {
        std::printf("FAIL: mesh too small (%d verts)\n", vertex_count);
        return 1;
    }

    std::printf("PASS: all checks passed\n");
    return 0;
}
