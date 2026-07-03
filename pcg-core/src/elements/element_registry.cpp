#include "elements/pcg_element.hpp"
#include "elements/primitive_elements.hpp"
#include "elements/structural_elements.hpp"

#include "internal/error_util.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

PcgResultCode fail(PcgContext& ctx, PcgResultCode code, const char* message)
{
    write_error(ctx.err_buf, ctx.err_buf_size, message);
    return code;
}

class ParseConfigElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ParseConfig"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail(ctx, PCG_ERR_EXECUTION, "ParseConfig missing node");

        const int seed = ctx.node->data.value("seed", ctx.graph_seed);
        const double density = ctx.node->data.value("density", 0.5);

        if (density < 0.0 || density > 1.0)
            return fail(ctx, PCG_ERR_EXECUTION, "ParseConfig density out of range");

        ctx.outputs.add_param("out", data::PcgParamData(nlohmann::json{
            {"seed", seed},
            {"density", density},
        }));
        return PCG_OK;
    }
};

class SpawnPointsElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SpawnPoints"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail(ctx, PCG_ERR_EXECUTION, "SpawnPoints missing node");

        const nlohmann::json* config = ctx.inputs.find_json("in");
        if (!config)
            return fail(ctx, PCG_ERR_EXECUTION, "SpawnPoints missing config input");

        const int base_count = ctx.node->data.value("count", 100);
        const double radius = ctx.node->data.value("radius", 10.0);
        const double density = config->value("density", 0.5);
        const int config_seed = config->value("seed", ctx.graph_seed);

        if (radius < 0.0)
            return fail(ctx, PCG_ERR_EXECUTION, "SpawnPoints radius must be >= 0");

        int count = static_cast<int>(std::lround(base_count * density));
        if (count < 0)
            count = 0;
        if (count > 10000)
            count = 10000;

        uint32_t rng = static_cast<uint32_t>(config_seed) ^
                       static_cast<uint32_t>(ctx.graph_seed * 2654435761u);

        data::PcgPointData points;
        constexpr double kPi = 3.14159265358979323846;
        for (int i = 0; i < count; ++i) {
            rng = rng * 1664525u + 1013904223u;
            const double t = (count <= 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(count);
            const double angle = t * 2.0 * kPi + (rng % 1000) / 1000.0 * 0.25;
            rng = rng * 1664525u + 1013904223u;
            const double radial = radius * (0.25 + (rng % 1000) / 1000.0 * 0.75);

            points.add_point(data::PcgPoint{
                radial * std::cos(angle),
                0.0,
                radial * std::sin(angle),
            });
        }

        ctx.outputs.add_points("out", std::move(points));
        return PCG_OK;
    }
};

class PlaceInSceneElement final : public IPcgElement {
public:
    const char* type_name() const override { return "PlaceInScene"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail(ctx, PCG_ERR_EXECUTION, "PlaceInScene missing node");

        const nlohmann::json* points_payload = ctx.inputs.find_json("in");
        if (!points_payload)
            return fail(ctx, PCG_ERR_EXECUTION, "PlaceInScene missing points input");

        if (!points_payload->contains("points") || !(*points_payload)["points"].is_array())
            return fail(ctx, PCG_ERR_EXECUTION, "PlaceInScene missing points input");

        const std::string prefab = ctx.node->data.value("prefab", "");
        const double scale = ctx.node->data.value("scale", 1.0);

        nlohmann::json out{
            {"status", "ok"},
            {"prefab", prefab},
            {"scale", scale},
            {"pointCount", (*points_payload)["points"].size()},
            {"points", (*points_payload)["points"]},
        };
        ctx.outputs.add("out", data::PcgDataType::Point, std::move(out));
        return PCG_OK;
    }
};

std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& registry()
{
    static std::unordered_map<std::string, std::unique_ptr<IPcgElement>> instance;
    return instance;
}

} // namespace

void register_builtin_elements()
{
    auto& map = registry();
    if (!map.empty())
        return;

    map.emplace("ParseConfig", std::make_unique<ParseConfigElement>());
    map.emplace("SpawnPoints", std::make_unique<SpawnPointsElement>());
    map.emplace("PlaceInScene", std::make_unique<PlaceInSceneElement>());
    register_phase41_elements(map);
    register_phase42_elements(map);
}

const IPcgElement* find_element(const std::string& type)
{
    register_builtin_elements();
    const auto it = registry().find(type);
    return it == registry().end() ? nullptr : it->second.get();
}

bool is_known_element_type(const std::string& type)
{
    return find_element(type) != nullptr;
}

} // namespace pcg::internal::elements
