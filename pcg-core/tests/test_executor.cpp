#include "pcg_api.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string read_file(const char* path)
{
    std::ifstream file(path);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void expect_code(PcgResultCode actual, PcgResultCode expected, const char* label)
{
    if (actual != expected) {
        std::printf("FAIL: %s expected %d got %d\n", label, static_cast<int>(expected), static_cast<int>(actual));
        std::exit(1);
    }
}

} // namespace

int main()
{
    const char* ver = pcg_get_version();
    assert(ver != nullptr);
    assert(std::strstr(ver, "pcg-core") != nullptr);
    std::printf("PASS: %s\n", ver);

    char err[512] = {};
    expect_code(pcg_validate_graph("{", err, sizeof(err)), PCG_ERR_INVALID_JSON, "invalid json");

    const char* cycle_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "a", "type": "SpawnPoints", "position": {"x":0,"y":0}, "data": {"count": 1, "radius": 1}},
        {"id": "b", "type": "PlaceInScene", "position": {"x":0,"y":0}, "data": {"prefab": "x", "scale": 1}}
      ],
      "edges": [
        {"id": "e1", "source": "a", "target": "b"},
        {"id": "e2", "source": "b", "target": "a"}
      ]
    })";
    expect_code(pcg_validate_graph(cycle_graph, err, sizeof(err)), PCG_ERR_CYCLE_DETECTED, "cycle graph");

    const char* unknown_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "a", "type": "UnknownBlock", "position": {"x":0,"y":0}, "data": {}}
      ],
      "edges": []
    })";
    expect_code(pcg_validate_graph(unknown_graph, err, sizeof(err)), PCG_ERR_UNKNOWN_NODE, "unknown node");

    const std::string example_graph = read_file("../../schema/example.pcg");
    assert(!example_graph.empty());
    expect_code(pcg_validate_graph(example_graph.c_str(), err, sizeof(err)), PCG_OK, "example graph validate");

    char out[65536] = {};
    expect_code(pcg_execute_graph(example_graph.c_str(), 42, out, sizeof(out)), PCG_OK, "example graph execute");
    assert(std::strstr(out, "\"points\"") != nullptr);
    assert(std::strstr(out, "\"pointCount\"") != nullptr);
    std::printf("PASS: execute_graph -> pointCount present\n");

    const char* mini_graph = R"({
      "version": "1.0",
      "nodes": [
        {"id": "n1", "type": "SpawnPoints", "position": {"x":0,"y":0}, "data": {"count": 3, "radius": 2.0}},
        {"id": "n2", "type": "PlaceInScene", "position": {"x":0,"y":0}, "data": {"prefab": "Tree", "scale": 2.0}}
      ],
      "edges": [
        {"id": "e1", "source": "n1", "target": "n2"}
      ]
    })";
    expect_code(pcg_execute_graph(mini_graph, 99, out, sizeof(out)), PCG_OK, "mini graph execute");
    assert(std::strstr(out, "\"prefab\":\"Tree\"") != nullptr || std::strstr(out, "\"prefab\": \"Tree\"") != nullptr);
    std::printf("PASS: mini graph prefab preserved\n");

    const char* typed_v2_graph = R"({
      "version": "2.0",
      "nodes": [
        {"id":"a","type":"SpawnPoints","position":{"x":0,"y":0},"data":{}},
        {"id":"b","type":"PlaceInScene","position":{"x":200,"y":0},"data":{}}
      ],
      "edges": [{"id":"e","source":"a","target":"b","sourceHandle":"out",
        "targetHandle":"in","sourcePinType":"SpatialPoint","targetPinType":"SpatialPoint"}]
    })";
    expect_code(pcg_validate_graph(typed_v2_graph, err, sizeof(err)), PCG_OK,
                "typed v2 graph validate");

    const char* invalid_handle_graph = R"({
      "version":"1.0",
      "nodes":[{"id":"a","type":"SpawnPoints","data":{}},{"id":"b","type":"PlaceInScene","data":{}}],
      "edges":[{"source":"a","target":"b","sourceHandle":"missing","targetHandle":"in"}]
    })";
    expect_code(pcg_validate_graph(invalid_handle_graph, err, sizeof(err)), PCG_ERR_INVALID_JSON,
                "invalid source handle");

    const char* incompatible_pin_graph = R"({
      "version":"1.0",
      "nodes":[{"id":"a","type":"CreateBoxMesh","data":{}},{"id":"b","type":"PlaceInScene","data":{}}],
      "edges":[{"source":"a","target":"b","sourceHandle":"out","targetHandle":"in"}]
    })";
    expect_code(pcg_validate_graph(incompatible_pin_graph, err, sizeof(err)), PCG_ERR_INVALID_JSON,
                "incompatible pin types");

    const char* multiple_input_graph = R"({
      "version":"1.0",
      "nodes":[{"id":"a","type":"SpawnPoints","data":{}},{"id":"b","type":"SpawnPoints","data":{}},
        {"id":"c","type":"PlaceInScene","data":{}}],
      "edges":[{"source":"a","target":"c"},{"source":"b","target":"c"}]
    })";
    expect_code(pcg_validate_graph(multiple_input_graph, err, sizeof(err)), PCG_ERR_INVALID_JSON,
                "non-variadic input cardinality");
    std::printf("PASS: runtime graph pin contracts\n");

    const std::string roundtrip_graph = read_file("fixtures/roundtrip.pcg");
    assert(!roundtrip_graph.empty());
    expect_code(pcg_validate_graph(roundtrip_graph.c_str(), err, sizeof(err)), PCG_OK, "roundtrip graph validate");
    expect_code(pcg_execute_graph(roundtrip_graph.c_str(), 42, out, sizeof(out)), PCG_OK, "roundtrip graph execute");
    assert(std::strstr(out, "\"prefab\":\"Rock\"") != nullptr || std::strstr(out, "\"prefab\": \"Rock\"") != nullptr);
    assert(std::strstr(out, "\"pointCount\"") != nullptr);
    std::printf("PASS: Unity/Web round-trip fixture executes\n");

    return 0;
}
