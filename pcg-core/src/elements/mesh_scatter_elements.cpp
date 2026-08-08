#include "elements/mesh_scatter_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/mesh_scatter_algorithms.hpp"
#include "mesh_runtime.hpp"

#include <algorithm>
#include <string>

namespace pcg::internal::elements {
namespace {

int clamp_sample_count(int value)
{
    return std::clamp(value, 0, 1'000'000);
}

class GetMeshDataElement final : public IPcgElement {
public:
    const char* type_name() const override { return "GetMeshData"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GetMeshData missing node");

        if (!ctx.meshes)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GetMeshData missing mesh runtime");

        const data::PcgMeshData* mesh = ctx.meshes->find(ctx.node->id);
        if (!mesh || mesh->vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GetMeshData missing mesh slot");

        emit_mesh(ctx, *mesh);
        return PCG_OK;
    }
};

class SampleMeshSurfaceElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SampleMeshSurface"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SampleMeshSurface missing node");

        SampleMeshSurfaceOptions opts;
        opts.count = clamp_sample_count(ctx.node->data.value("count", 100));
        opts.seed = mix_seed(ctx.graph_seed, ctx.node->data.value("seed", 0));
        opts.normal_offset = ctx.node->data.value("normalOffset", 0.0);
        opts.looseness = std::max(0.0, ctx.node->data.value("looseness", 0.0));
        opts.face_group = ctx.node->data.value("faceGroup", std::string());
        opts.exclude_groups = parse_name_list(ctx.node->data, "excludeGroups");
        opts.edge_margin = std::max(0.0, ctx.node->data.value("edgeMargin", 0.0));
        opts.is_cancel_requested = ctx.is_cancel_requested;

        // Prefer geometry so face groups survive; fall back to triangle soup when
        // only PcgMeshData is available (faceGroup ignored, edgeMargin uses mesh border).
        if (ctx.inputs.find_geometry("in") != nullptr) {
            const data::PcgGeometry geometry =
                get_geometry_input(ctx, "in", "SampleMeshSurface missing mesh input");
            if (geometry.points().empty() || geometry.faces().empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, "SampleMeshSurface mesh has no triangles");
            emit_points(ctx, sample_mesh_surface(geometry, opts));
        } else {
            const data::PcgMeshData mesh =
                get_mesh_input(ctx, "in", "SampleMeshSurface missing mesh input");
            if (mesh.vertices().empty() || mesh.triangles().size() < 3)
                return fail_ctx(ctx, PCG_ERR_EXECUTION, "SampleMeshSurface mesh has no triangles");
            emit_points(ctx, sample_mesh_surface(mesh, opts));
        }

        if (ctx.is_cancel_requested && ctx.is_cancel_requested())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Execution cancelled");
        return PCG_OK;
    }
};

class PointRelaxElement final : public IPcgElement {
public:
    const char* type_name() const override { return "PointRelax"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "PointRelax missing node");

        const bool has_points = ctx.inputs.find_points("in") != nullptr ||
                                ctx.inputs.find_json("in") != nullptr;
        if (!has_points)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "PointRelax missing points input");

        PointRelaxOptions opts;
        opts.max_iterations = std::clamp(ctx.node->data.value("maxIterations", 50), 0, 1000);
        opts.radius = std::max(0.000001, ctx.node->data.value("radius", 1.0));
        opts.use_pscale = ctx.node->data.value("usePscale", true);
        opts.is_cancel_requested = ctx.is_cancel_requested;

        const data::PcgPointData points =
            get_points_input(ctx, "in", "PointRelax missing points input");
        emit_points(ctx, relax_points(points, opts));

        if (ctx.is_cancel_requested && ctx.is_cancel_requested())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Execution cancelled");
        return PCG_OK;
    }
};

class PointsFromVolumeElement final : public IPcgElement {
public:
    const char* type_name() const override { return "PointsFromVolume"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "PointsFromVolume missing node");

        PointsFromVolumeOptions opts;
        opts.point_separation = std::max(0.001, ctx.node->data.value("pointSeparation", 0.5));
        opts.jitter = std::max(0.0, ctx.node->data.value("jitter", 0.0));
        opts.seed = mix_seed(ctx.graph_seed, ctx.node->data.value("seed", 0));
        opts.shell_only = ctx.node->data.value("shellOnly", false);
        opts.is_cancel_requested = ctx.is_cancel_requested;

        if (ctx.inputs.find_geometry("in") != nullptr) {
            const data::PcgGeometry geometry =
                get_geometry_input(ctx, "in", "PointsFromVolume missing mesh input");
            if (geometry.points().empty() || geometry.faces().empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, "PointsFromVolume mesh has no triangles");
            emit_points(ctx, sample_mesh_volume(geometry, opts));
        } else {
            const data::PcgMeshData mesh =
                get_mesh_input(ctx, "in", "PointsFromVolume missing mesh input");
            if (mesh.vertices().empty() || mesh.triangles().size() < 3)
                return fail_ctx(ctx, PCG_ERR_EXECUTION, "PointsFromVolume mesh has no triangles");
            emit_points(ctx, sample_mesh_volume(mesh, opts));
        }

        if (ctx.is_cancel_requested && ctx.is_cancel_requested())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Execution cancelled");
        return PCG_OK;
    }
};

} // namespace

void register_mesh_scatter_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("GetMeshData", std::make_unique<GetMeshDataElement>());
    map.emplace("SampleMeshSurface", std::make_unique<SampleMeshSurfaceElement>());
    map.emplace("PointRelax", std::make_unique<PointRelaxElement>());
    map.emplace("PointsFromVolume", std::make_unique<PointsFromVolumeElement>());
}

} // namespace pcg::internal::elements
