#include "elements/pcg_element.hpp"
#include "elements/primitive_elements.hpp"
#include "elements/mesh_elements.hpp"
#include "elements/mesh_scatter_elements.hpp"
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

class SpawnPointsElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SpawnPoints"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail(ctx, PCG_ERR_EXECUTION, "SpawnPoints missing node");

        int count = ctx.node->data.value("count", 100);
        if (count < 0)
            count = 0;
        if (count > 10000)
            count = 10000;
        const double radius = ctx.node->data.value("radius", 10.0);

        if (radius < 0.0)
            return fail(ctx, PCG_ERR_EXECUTION, "SpawnPoints radius must be >= 0");

        uint32_t rng = static_cast<uint32_t>(ctx.graph_seed) ^
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

        const data::PcgPointData* points_payload = ctx.inputs.find_points("in");
        if (!points_payload) {
            const nlohmann::json* json_payload = ctx.inputs.find_json("in");
            if (!json_payload)
                return fail(ctx, PCG_ERR_EXECUTION, "PlaceInScene missing points input");

            if (!json_payload->contains("points") || !(*json_payload)["points"].is_array())
                return fail(ctx, PCG_ERR_EXECUTION, "PlaceInScene missing points input");

            const std::string prefab = ctx.node->data.value("prefab", "");
            const double scale = ctx.node->data.value("scale", 1.0);

            nlohmann::json out{
                {"status", "ok"},
                {"prefab", prefab},
                {"scale", scale},
                {"pointCount", (*json_payload)["points"].size()},
                {"points", (*json_payload)["points"]},
            };
            ctx.outputs.add("out", data::PcgDataType::Point, std::move(out));
            return PCG_OK;
        }

        const std::string prefab = ctx.node->data.value("prefab", "");
        const double scale = ctx.node->data.value("scale", 1.0);

        nlohmann::json sidecar{
            {"status", "ok"},
            {"prefab", prefab},
            {"scale", scale},
            {"pointCount", points_payload->points().size()},
        };
        ctx.outputs.add_points_with_meta("out", *points_payload, std::move(sidecar));
        return PCG_OK;
    }
};

/// Houdini-style terminal node. Passes input through to output unchanged.
/// The execution engine prefers Output nodes as the graph's sink.
class OutputElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Output"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (auto mesh = ctx.inputs.find_mesh_shared("in")) {
            ctx.outputs.add_mesh_shared("out", mesh);
            if (auto spawn_mesh = ctx.inputs.find_mesh_shared("spawnMesh"))
                ctx.outputs.add_mesh_shared("spawnMesh", spawn_mesh);
            return PCG_OK;
        }

        if (const data::PcgTaggedData* in_item = ctx.inputs.find("in");
            in_item && in_item->points) {
            ctx.outputs.add_points_with_meta("out", *in_item->points, in_item->payload);
            if (auto spawn_mesh = ctx.inputs.find_mesh_shared("spawnMesh"))
                ctx.outputs.add_mesh_shared("spawnMesh", spawn_mesh);
            return PCG_OK;
        }

        if (const data::PcgSplineData* splines = ctx.inputs.find_splines("in")) {
            ctx.outputs.add_splines("out", *splines);
            return PCG_OK;
        }

        const nlohmann::json* input = ctx.inputs.find_json("in");
        if (!input)
            return fail(ctx, PCG_ERR_EXECUTION, "Output missing input");

        ctx.outputs.add("out", data::PcgDataType::Unknown, *input);
        if (auto spawn_mesh = ctx.inputs.find_mesh_shared("spawnMesh"))
            ctx.outputs.add_mesh_shared("spawnMesh", spawn_mesh);
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

    map.emplace("SpawnPoints", std::make_unique<SpawnPointsElement>());
    map.emplace("PlaceInScene", std::make_unique<PlaceInSceneElement>());
    map.emplace("Output", std::make_unique<OutputElement>());
    register_phase41_elements(map);
    register_phase42_elements(map);
    register_mesh_elements(map);
    register_mesh_scatter_elements(map);
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
