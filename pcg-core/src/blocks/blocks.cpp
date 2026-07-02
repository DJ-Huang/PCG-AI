#include "blocks/blocks.hpp"

#include "internal/error_util.hpp"

#include <cmath>
#include <cstdint>

namespace pcg::internal::blocks {
namespace {

constexpr double kPi = 3.14159265358979323846;

PcgResultCode fail(char* err_buf, int err_buf_size, PcgResultCode code, const char* message)
{
    write_error(err_buf, err_buf_size, message);
    return code;
}

uint32_t next_rand(uint32_t& state)
{
    state = state * 1664525u + 1013904223u;
    return state;
}

int clamp_count(int value)
{
    if (value < 0)
        return 0;
    if (value > 10000)
        return 10000;
    return value;
}

} // namespace

PcgResultCode execute_parse_config(const GraphNode& node,
                                   int graph_seed,
                                   nlohmann::json& out,
                                   char* err_buf,
                                   int err_buf_size)
{
    const int seed = node.data.value("seed", graph_seed);
    const double density = node.data.value("density", 0.5);

    if (density < 0.0 || density > 1.0)
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "ParseConfig density out of range");

    out = nlohmann::json{
        {"seed", seed},
        {"density", density},
    };
    return PCG_OK;
}

PcgResultCode execute_spawn_points(const GraphNode& node,
                                   const nlohmann::json& config,
                                   int graph_seed,
                                   nlohmann::json& out,
                                   char* err_buf,
                                   int err_buf_size)
{
    const int base_count = node.data.value("count", 100);
    const double radius = node.data.value("radius", 10.0);
    const double density = config.value("density", 0.5);
    const int config_seed = config.value("seed", graph_seed);

    if (radius < 0.0)
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "SpawnPoints radius must be >= 0");

    const int count = clamp_count(static_cast<int>(std::lround(base_count * density)));
    uint32_t rng = static_cast<uint32_t>(config_seed) ^ static_cast<uint32_t>(graph_seed * 2654435761u);

    nlohmann::json points = nlohmann::json::array();
    for (int i = 0; i < count; ++i) {
        const double t = (count <= 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(count);
        const double angle = t * 2.0 * kPi + (next_rand(rng) % 1000) / 1000.0 * 0.25;
        const double radial = radius * (0.25 + (next_rand(rng) % 1000) / 1000.0 * 0.75);

        points.push_back(nlohmann::json{
            {"x", radial * std::cos(angle)},
            {"y", 0.0},
            {"z", radial * std::sin(angle)},
        });
    }

    out = nlohmann::json{
        {"points", std::move(points)},
    };
    return PCG_OK;
}

PcgResultCode execute_place_in_scene(const GraphNode& node,
                                     const nlohmann::json& points_payload,
                                     nlohmann::json& out,
                                     char* err_buf,
                                     int err_buf_size)
{
    if (!points_payload.contains("points") || !points_payload["points"].is_array())
        return fail(err_buf, err_buf_size, PCG_ERR_EXECUTION, "PlaceInScene missing points input");

    const std::string prefab = node.data.value("prefab", "");
    const double scale = node.data.value("scale", 1.0);

    out = nlohmann::json{
        {"status", "ok"},
        {"prefab", prefab},
        {"scale", scale},
        {"pointCount", points_payload["points"].size()},
        {"points", points_payload["points"]},
    };
    return PCG_OK;
}

} // namespace pcg::internal::blocks
