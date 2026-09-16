#include "pcg_api.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

int g_failures = 0;

void expect(bool condition, const char* message)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", message);
    if (!condition)
        ++g_failures;
}

struct CookResult {
    PcgResultCode code = PCG_OK;
    int kind = 0;
    int vertex_count = 0;
    int index_count = 0;
    std::string error;
};

CookResult cook(const json& nodes, const json& edges, int seed = 17)
{
    const std::string graph = json{{"version", "1.0"}, {"nodes", nodes}, {"edges", edges}}.dump();
    std::vector<char> json_output(1024 * 1024);
    std::vector<uint8_t> mesh_output(16 * 1024 * 1024);
    char error[2048] = {};
    CookResult result;
    result.code = pcg_execute_graph_v7(
        graph.c_str(), seed,
        nullptr, 0, nullptr, 0, nullptr, 0,
        &result.kind, json_output.data(), static_cast<int>(json_output.size()),
        mesh_output.data(), static_cast<int>(mesh_output.size()),
        nullptr, 0, nullptr, nullptr, &result.vertex_count, &result.index_count,
        nullptr, nullptr, 0, error, sizeof(error));
    result.error = error;
    return result;
}

json node(const char* id, const char* type, json data = json::object())
{
    return {{"id", id}, {"type", type}, {"position", {{"x", 0}, {"y", 0}}}, {"data", std::move(data)}};
}

json edge(const char* id, const char* source, const char* target,
          const char* source_handle = "out", const char* target_handle = "in")
{
    return {{"id", id}, {"source", source}, {"target", target},
            {"sourceHandle", source_handle}, {"targetHandle", target_handle}};
}

} // namespace

int main()
{
    {
        const auto result = cook(
            json::array({node("sphere", "Sphere", {
                {"radius", {2.0, 1.0, 1.5}}, {"rows", 12}, {"columns", 20},
                {"center", {1.0, 2.0, 3.0}}, {"uniformScale", 1.0},
            }), node("out", "Output")}),
            json::array({edge("e1", "sphere", "out")}));
        expect(result.code == PCG_OK, "Sphere cooks through the public graph executor");
        expect(result.vertex_count > 100 && result.index_count > 300,
               "Sphere emits a tessellated mesh");
    }

    {
        const auto result = cook(
            json::array({
                node("torus", "Torus", {{"majorRadius", 2.0}, {"minorRadius", 0.4},
                                          {"rows", 24}, {"columns", 12}}),
                node("uv", "UVUnwrap", {{"layout", "strip"}, {"scale", "uniform"}}),
                node("out", "Output"),
            }),
            json::array({edge("e1", "torus", "uv"), edge("e2", "uv", "out")}));
        expect(result.code == PCG_OK, "Torus to UV Unwrap cooks successfully");
        expect(result.vertex_count > 0 && result.index_count > 0,
               "UV Unwrap preserves renderable topology");
    }

    {
        const auto result = cook(
            json::array({
                node("box", "CreateBoxMesh", {{"width", 2.0}, {"height", 2.0}, {"depth", 2.0}}),
                node("bulge", "Bulge", {{"magnitude", 0.35}, {"center", {0.0, 0.0, 0.0}}}),
                node("attrib", "AttributeCreate", {{"name", "density"}, {"class", "point"},
                                                      {"value", 0.75}}),
                node("out", "Output"),
            }),
            json::array({edge("e1", "box", "bulge"), edge("e2", "bulge", "attrib"),
                         edge("e3", "attrib", "out")}));
        expect(result.code == PCG_OK, "Bulge and Attribute Create compose in a mesh chain");
        expect(result.vertex_count > 0, "deform and attribute chain keeps mesh geometry");
    }

    {
        const auto result = cook(
            json::array({
                node("points", "PointGenerate", {{"npts", 32}, {"size", {2.0, 1.0, 3.0}}, {"seed", 9}}),
                node("jitter", "PointJitter", {{"scale", {0.2, 0.1, 0.2}}, {"seed", 5}}),
                node("out", "Output"),
            }),
            json::array({edge("e1", "points", "jitter"), edge("e2", "jitter", "out")}));
        expect(result.code == PCG_OK, "Point Generate and Point Jitter cook successfully");
        expect(result.kind != 0, "point chain produces a typed result");
    }

    {
        const auto result = cook(
            json::array({
                node("line", "Line", {{"origin", {0.0, 0.0, 0.0}},
                                        {"direction", {0.0, 1.0, 0.0}}, {"length", 4.0}, {"points", 9}}),
                node("fit", "SplineFit", {{"tolerance", 0.01}, {"smoothing", 3}}),
                node("out", "Output"),
            }),
            json::array({edge("e1", "line", "fit"), edge("e2", "fit", "out")}));
        expect(result.code == PCG_OK, "Line and Spline Fit cook successfully");
        expect(result.kind != 0, "spline chain produces a typed result");
    }

    if (g_failures == 0)
        std::printf("All Houdini SOP compatibility tests passed.\n");
    return g_failures == 0 ? 0 : 1;
}
