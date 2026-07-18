#include "elements/element_utils.hpp"
#include "elements/heightfield_algorithms.hpp"
#include "elements/pcg_element.hpp"
#include "elements/primitive_elements.hpp"
#include "heightfield_runtime.hpp"

#include <cmath>
#include <memory>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

int clamp_count(int value, int max_value = 10000)
{
    if (value < 0)
        return 0;
    if (value > max_value)
        return max_value;
    return value;
}

class CreatePointGridElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CreatePointGrid"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreatePointGrid missing node");

        const int count_x = clamp_count(ctx.node->data.value("pointCountX", 10), 256);
        const int count_y = clamp_count(ctx.node->data.value("pointCountY", 10), 256);
        const double spacing = ctx.node->data.value("spacing", 2.0);
        if (spacing < 0.0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreatePointGrid spacing must be >= 0");

        data::PcgPointData points;

        // Merge optional input points
        if (const data::PcgPointData* input = ctx.inputs.find_points("in"))
            for (const auto& p : input->points())
                points.add_point(p);
        else if (const nlohmann::json* json_input = ctx.inputs.find_json("in"))
            for (const auto& p : parse_point_input(*json_input).points())
                points.add_point(p);

        const double origin_x = -((count_x - 1) * spacing) * 0.5;
        const double origin_z = -((count_y - 1) * spacing) * 0.5;
        for (int y = 0; y < count_y; ++y) {
            for (int x = 0; x < count_x; ++x) {
                points.add_point(data::PcgPoint{
                    origin_x + x * spacing,
                    0.0,
                    origin_z + y * spacing,
                });
            }
        }

        emit_points(ctx, std::move(points));
        return PCG_OK;
    }
};

class CreatePointsElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CreatePoints"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreatePoints missing node");

        const double x = ctx.node->data.value("x", 0.0);
        const double y = ctx.node->data.value("y", 0.0);
        const double z = ctx.node->data.value("z", 0.0);
        const int count = clamp_count(ctx.node->data.value("count", 1), 1000);
        const double jitter = ctx.node->data.value("jitter", 0.0);

        uint32_t rng = mix_seed(ctx.graph_seed, static_cast<int>(x * 17 + z * 31));
        data::PcgPointData points;

        // Merge optional input points
        if (const data::PcgPointData* input = ctx.inputs.find_points("in"))
            for (const auto& p : input->points())
                points.add_point(p);
        else if (const nlohmann::json* json_input = ctx.inputs.find_json("in"))
            for (const auto& p : parse_point_input(*json_input).points())
                points.add_point(p);

        for (int i = 0; i < count; ++i) {
            const double jx = jitter > 0.0 ? ((next_rand(rng) % 1000) / 500.0 - 1.0) * jitter : 0.0;
            const double jz = jitter > 0.0 ? ((next_rand(rng) % 1000) / 500.0 - 1.0) * jitter : 0.0;
            points.add_point(data::PcgPoint{x + jx, y, z + jz});
        }

        emit_points(ctx, std::move(points));
        return PCG_OK;
    }
};

class SurfaceSamplerElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SurfaceSampler"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SurfaceSampler missing node");

        const int subdivisions = clamp_count(ctx.node->data.value("subdivisions", 8), 128);
        const double extent = ctx.node->data.value("extent", 10.0);
        if (extent < 0.0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SurfaceSampler extent must be >= 0");

        data::PcgPointData points;

        // Merge optional input points
        if (const data::PcgPointData* input = ctx.inputs.find_points("in"))
            for (const auto& p : input->points())
                points.add_point(p);
        else if (const nlohmann::json* json_input = ctx.inputs.find_json("in"))
            for (const auto& p : parse_point_input(*json_input).points())
                points.add_point(p);

        const double step = subdivisions <= 1 ? extent : extent / static_cast<double>(subdivisions - 1);
        for (int iz = 0; iz < subdivisions; ++iz) {
            for (int ix = 0; ix < subdivisions; ++ix) {
                const double px = ix * step - extent * 0.5;
                const double pz = iz * step - extent * 0.5;
                points.add_point(data::PcgPoint{px, 0.0, pz});
            }
        }

        emit_points(ctx, std::move(points));
        return PCG_OK;
    }
};

class CopyAttributesElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CopyAttributes"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CopyAttributes missing node");

        data::PcgPointData points = get_points_input(ctx, "in", "CopyAttributes missing points input");
        if (points.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CopyAttributes missing points input");
        const auto names = parse_name_list(ctx.node->data, "attributeNames");
        const nlohmann::json& source = ctx.node->data.contains("values") && ctx.node->data["values"].is_object()
            ? ctx.node->data["values"]
            : ctx.node->data;

        for (auto& point : points.points_mut()) {
            for (const auto& name : names) {
                if (source.contains(name))
                    point.attributes[name] = source[name];
            }
        }

        emit_points(ctx, std::move(points));
        return PCG_OK;
    }
};

class DeleteAttributesElement final : public IPcgElement {
public:
    const char* type_name() const override { return "DeleteAttributes"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "DeleteAttributes missing node");

        data::PcgPointData points = get_points_input(ctx, "in", "DeleteAttributes missing points input");
        if (points.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "DeleteAttributes missing points input");
        const auto names = parse_name_list(ctx.node->data, "attributeNames");

        for (auto& point : points.points_mut()) {
            for (const auto& name : names)
                point.attributes.erase(name);
        }

        emit_points(ctx, std::move(points));
        return PCG_OK;
    }
};

class BreakAttributesElement final : public IPcgElement {
public:
    const char* type_name() const override { return "BreakAttributes"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BreakAttributes missing node");

        data::PcgPointData points = get_points_input(ctx, "in", "BreakAttributes missing points input");
        if (points.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BreakAttributes missing points input");
        const std::string name = ctx.node->data.value("attributeName", "");
        if (name.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BreakAttributes attributeName required");

        nlohmann::json broken = nlohmann::json::array();
        for (const auto& point : points.points()) {
            if (point.attributes.contains(name))
                broken.push_back(point.attributes[name]);
            else
                broken.push_back(nullptr);
        }

        points.metadata().set(name, broken);
        emit_points(ctx, std::move(points));
        return PCG_OK;
    }
};

class DensityFilterElement final : public IPcgElement {
public:
    const char* type_name() const override { return "DensityFilter"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "DensityFilter missing node");

        data::PcgPointData source = get_points_input(ctx, "in", "DensityFilter missing points input");
        if (source.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "DensityFilter missing points input");

        const double density = ctx.node->data.value("density", 1.0);
        if (density < 0.0 || density > 1.0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "DensityFilter density out of range");

        data::PcgPointData filtered;
        uint32_t rng = mix_seed(ctx.graph_seed, 17);

        for (const auto& point : source.points()) {
            const double threshold = density >= 1.0 ? 0.0 : (next_rand(rng) % 10000) / 10000.0;
            if (threshold <= density)
                filtered.add_point(point);
        }

        emit_points(ctx, std::move(filtered));
        return PCG_OK;
    }
};

class AttributeFilterElement final : public IPcgElement {
public:
    const char* type_name() const override { return "AttributeFilter"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AttributeFilter missing node");

        data::PcgPointData source = get_points_input(ctx, "in", "AttributeFilter missing points input");
        if (source.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AttributeFilter missing points input");

        const std::string name = ctx.node->data.value("attributeName", "");
        const std::string match_value = ctx.node->data.value("matchValue", "");
        if (name.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AttributeFilter attributeName required");

        data::PcgPointData filtered;
        for (const auto& point : source.points()) {
            if (!point.attributes.contains(name))
                continue;
            if (point.attributes[name].is_string() && point.attributes[name].get<std::string>() == match_value)
                filtered.add_point(point);
            else if (point.attributes[name] == match_value)
                filtered.add_point(point);
        }

        emit_points(ctx, std::move(filtered));
        return PCG_OK;
    }
};

class TransformPointsElement final : public IPcgElement {
public:
    const char* type_name() const override { return "TransformPoints"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "TransformPoints missing node");

        data::PcgPointData source = get_points_input(ctx, "in", "TransformPoints missing points input");
        if (source.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "TransformPoints missing points input");

        const double tx = ctx.node->data.value("translateX", 0.0);
        const double ty = ctx.node->data.value("translateY", 0.0);
        const double tz = ctx.node->data.value("translateZ", 0.0);
        const double scale = ctx.node->data.value("scale", 1.0);
        const double rot_y = ctx.node->data.value("rotationY", 0.0) * (3.14159265358979323846 / 180.0);
        const double cos_r = std::cos(rot_y);
        const double sin_r = std::sin(rot_y);

        data::PcgPointData transformed;
        for (const auto& point : source.points()) {
            const double sx = point.x * scale;
            const double sy = point.y * scale;
            const double sz = point.z * scale;
            transformed.add_point(data::PcgPoint{
                sx * cos_r - sz * sin_r + tx,
                sy + ty,
                sx * sin_r + sz * cos_r + tz,
                point.attributes,
            });
        }

        emit_points(ctx, std::move(transformed));
        return PCG_OK;
    }
};

double sample_legacy_terrain_height(const nlohmann::json* terrain, double x, double z, int seed)
{
    if (terrain && terrain->contains("heights") && (*terrain)["heights"].is_array()) {
        const auto& heights = (*terrain)["heights"];
        const int grid = terrain->value("gridSize", 0);
        const double cell = terrain->value("cellSize", 1.0);
        if (grid > 0) {
            const int ix = static_cast<int>(std::floor((x / cell) + grid * 0.5));
            const int iz = static_cast<int>(std::floor((z / cell) + grid * 0.5));
            if (ix >= 0 && ix < grid && iz >= 0 && iz < grid) {
                const size_t index = static_cast<size_t>(iz * grid + ix);
                if (index < heights.size())
                    return heights[index].get<double>();
            }
        }
    }

    return simple_noise(x, z, seed);
}

class ProjectPointsElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ProjectPoints"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ProjectPoints missing node");

        data::PcgPointData source = get_points_input(ctx, "in", "ProjectPoints missing points input");
        if (source.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ProjectPoints missing points input");
        const data::PcgHeightField* heightfield = ctx.inputs.find_heightfield("terrain");
        const nlohmann::json* terrain = ctx.inputs.find_json("terrain");
        const bool use_terrain = ctx.node->data.value(
            "useTerrain", heightfield != nullptr || terrain != nullptr);
        const double base_y = ctx.node->data.value("baseY", 0.0);

        data::PcgPointData projected;
        for (const auto& point : source.points()) {
            double y = base_y;
            if (use_terrain && heightfield) {
                if (!heightfield->sample_scalar_world("height", point.x, point.y, point.z, y))
                    return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                    "ProjectPoints failed to sample height layer");
            } else if (use_terrain) {
                y = sample_legacy_terrain_height(terrain, point.x, point.z, ctx.graph_seed);
            }
            projected.add_point(data::PcgPoint{point.x, y, point.z, point.attributes});
        }

        emit_points(ctx, std::move(projected));
        return PCG_OK;
    }
};

class GetTerrainDataElement final : public IPcgElement {
public:
    const char* type_name() const override { return "GetTerrainData"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GetTerrainData missing node");

        if (ctx.heightfields) {
            if (const data::PcgHeightField* bound = ctx.heightfields->find(ctx.node->id)) {
                emit_heightfield(ctx, *bound);
                return PCG_OK;
            }
        }

        const int grid = clamp_count(ctx.node->data.value("gridSize", 32), 256);
        const double cell = ctx.node->data.value("cellSize", 2.0);
        const double amplitude = ctx.node->data.value("amplitude", 5.0);
        const int seed = ctx.node->data.value("seed", ctx.graph_seed);
        if (grid < 2 || !std::isfinite(cell) || cell <= 0.0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GetTerrainData grid and cellSize must be > 0");

        data::PcgHeightField heightfield(
            grid,
            grid,
            static_cast<double>(grid - 1) * cell,
            static_cast<double>(grid - 1) * cell,
            data::PcgVec3{-0.5 * cell, 0.0, -0.5 * cell},
            data::HeightFieldSampling::Corner,
            data::HeightFieldOrientation::ZX);
        auto& height_layer = heightfield.create_layer("height", 1, 0.0f);
        heightfield.create_layer("mask", 1, 0.0f);
        for (int z = 0; z < grid; ++z) {
            for (int x = 0; x < grid; ++x) {
                const double wx = (x - grid * 0.5) * cell;
                const double wz = (z - grid * 0.5) * cell;
                const std::size_t index = static_cast<std::size_t>(z) *
                                          static_cast<std::size_t>(grid) +
                                          static_cast<std::size_t>(x);
                height_layer.values[index] =
                    static_cast<float>(simple_noise(wx, wz, seed) * amplitude);
            }
        }

        emit_heightfield(ctx, std::move(heightfield));
        return PCG_OK;
    }
};

class SampleSurfaceElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SampleSurface"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SampleSurface missing node");

        data::PcgPointData source = get_points_input(ctx, "in", "SampleSurface missing points input");
        if (source.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SampleSurface missing points input");

        const data::PcgHeightField* heightfield = ctx.inputs.find_heightfield("terrain");
        const nlohmann::json* terrain = ctx.inputs.find_json("terrain");
        if (!heightfield && !terrain) {
            fail_ctx(ctx, PCG_ERR_EXECUTION, "SampleSurface missing terrain input");
            return PCG_ERR_EXECUTION;
        }

        const int seed = terrain ? terrain->value("seed", ctx.graph_seed) : ctx.graph_seed;
        const double offset_y = ctx.node->data.value("offsetY", 0.0);
        double blend = ctx.node->data.value("blend", 1.0);
        if (blend < 0.0) blend = 0.0;
        if (blend > 1.0) blend = 1.0;

        data::PcgPointData sampled;
        for (const auto& point : source.points()) {
            double terrain_y = 0.0;
            if (heightfield) {
                if (!heightfield->sample_scalar_world(
                        "height", point.x, point.y, point.z, terrain_y)) {
                    return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                    "SampleSurface failed to sample height layer");
                }
            } else {
                terrain_y = sample_legacy_terrain_height(terrain, point.x, point.z, seed);
            }
            sampled.add_point(data::PcgPoint{
                point.x,
                point.y * (1.0 - blend) + terrain_y * blend + offset_y,
                point.z,
                point.attributes,
            });
        }

        emit_points(ctx, std::move(sampled));
        return PCG_OK;
    }
};

class StaticMeshSpawnerElement final : public IPcgElement {
public:
    const char* type_name() const override { return "StaticMeshSpawner"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "StaticMeshSpawner missing node");

        auto input_points = ctx.inputs.find_points_shared("in");
        if (!input_points || input_points->points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "StaticMeshSpawner missing points input");

        auto points = std::make_shared<data::PcgPointData>(*input_points);

        const std::string prefab = ctx.node->data.value("prefab", "");
        const std::string mesh = ctx.node->data.value("mesh", "");
        const double scale = ctx.node->data.value("scale", 1.0);

        for (auto& point : points->points_mut()) {
            if (!prefab.empty())
                point.attributes["prefab"] = prefab;
            if (!mesh.empty())
                point.attributes["mesh"] = mesh;
            point.attributes["scale"] = scale;
        }

        nlohmann::json sidecar{
            {"status", "ok"},
            {"prefab", prefab},
            {"mesh", mesh},
            {"scale", scale},
            {"pointCount", points->points().size()},
        };

        if (auto prototype = ctx.inputs.find_mesh_shared("mesh")) {
            emit_mesh_shared(ctx, "spawnMesh", prototype);
        } else if (const nlohmann::json* mesh_json = ctx.inputs.find_json("mesh")) {
            data::PcgMeshData spawn_mesh = parse_mesh_input(*mesh_json);
            if (!spawn_mesh.vertices().empty() && spawn_mesh.triangles().size() >= 3)
                ctx.outputs.add_mesh("spawnMesh", std::move(spawn_mesh));
        }

        emit_points_shared_with_meta(ctx, std::move(points), std::move(sidecar));
        return PCG_OK;
    }
};

} // namespace

void register_phase41_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("CreatePointGrid", std::make_unique<CreatePointGridElement>());
    map.emplace("CreatePoints", std::make_unique<CreatePointsElement>());
    map.emplace("SurfaceSampler", std::make_unique<SurfaceSamplerElement>());
    map.emplace("CopyAttributes", std::make_unique<CopyAttributesElement>());
    map.emplace("DeleteAttributes", std::make_unique<DeleteAttributesElement>());
    map.emplace("BreakAttributes", std::make_unique<BreakAttributesElement>());
    map.emplace("DensityFilter", std::make_unique<DensityFilterElement>());
    map.emplace("AttributeFilter", std::make_unique<AttributeFilterElement>());
    map.emplace("TransformPoints", std::make_unique<TransformPointsElement>());
    map.emplace("ProjectPoints", std::make_unique<ProjectPointsElement>());
    map.emplace("GetTerrainData", std::make_unique<GetTerrainDataElement>());
    map.emplace("SampleSurface", std::make_unique<SampleSurfaceElement>());
    map.emplace("StaticMeshSpawner", std::make_unique<StaticMeshSpawnerElement>());
}

} // namespace pcg::internal::elements
