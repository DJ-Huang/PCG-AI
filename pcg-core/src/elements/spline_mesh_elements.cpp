#include "elements/spline_mesh_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/pcg_element.hpp"
#include "elements/spline_algorithms.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

class SweepAlongSplineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SweepAlongSpline"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SweepAlongSpline missing node");

        const data::PcgSplineData backbone =
            get_splines_input(ctx, "backbone", "SweepAlongSpline missing backbone input");
        if (backbone.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SweepAlongSpline missing backbone input");

        const data::PcgSplineData* profile_spline = ctx.inputs.find_splines("profile");

        SweepAlongSplineOptions opts;
        opts.surface_shape = ctx.node->data.value("surfaceShape", "crossSection");
        opts.profile_width = ctx.node->data.value("profileWidth", 6.0);
        opts.profile_height = ctx.node->data.value("profileHeight", 0.4);
        opts.radius = ctx.node->data.value("radius", 1.0);
        opts.columns = ctx.node->data.value("columns", 16);
        opts.sample_spacing = ctx.node->data.value("sampleSpacing", 1.0);
        opts.cap_start = ctx.node->data.value("capStart", false);
        opts.cap_end = ctx.node->data.value("capEnd", false);
        opts.use_profile_spline =
            profile_spline != nullptr && !profile_spline->splines().empty() &&
            opts.surface_shape == "crossSection";
        opts.up_x = ctx.node->data.value("upX", 0.0);
        opts.up_y = ctx.node->data.value("upY", 1.0);
        opts.up_z = ctx.node->data.value("upZ", 0.0);
        opts.twist_degrees = ctx.node->data.value("twist", 0.0);
        opts.scale_start = ctx.node->data.value("scaleStart", 1.0);
        opts.scale_end = ctx.node->data.value("scaleEnd", 1.0);
        opts.profile_plane = ctx.node->data.value("profilePlane", "xy");

        emit_mesh(ctx, sweep_along_spline(backbone, profile_spline, opts));
        return PCG_OK;
    }
};

class ExtrudeAlongSplineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ExtrudeAlongSpline"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ExtrudeAlongSpline missing node");

        const data::PcgSplineData splines =
            get_splines_input(ctx, "spline", "ExtrudeAlongSpline missing spline input");
        if (splines.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ExtrudeAlongSpline missing spline input");

        const data::PcgMeshData* profile_mesh = nullptr;
        if (const data::PcgMeshData* mesh = ctx.inputs.find_mesh("profile"))
            profile_mesh = mesh;
        else if (auto shared = ctx.inputs.find_mesh_shared("profile"))
            profile_mesh = shared.get();

        ExtrudeAlongSplineOptions opts;
        opts.profile_width = ctx.node->data.value("profileWidth", 2.0);
        opts.profile_height = ctx.node->data.value("profileHeight", 0.3);
        opts.sample_spacing = ctx.node->data.value("sampleSpacing", 1.0);
        opts.cap_start = ctx.node->data.value("capStart", true);
        opts.cap_end = ctx.node->data.value("capEnd", true);
        opts.use_profile_mesh = profile_mesh != nullptr;
        opts.up_x = ctx.node->data.value("upX", 0.0);
        opts.up_y = ctx.node->data.value("upY", 1.0);
        opts.up_z = ctx.node->data.value("upZ", 0.0);
        opts.twist_degrees = ctx.node->data.value("twist", 0.0);
        opts.scale_start = ctx.node->data.value("scaleStart", 1.0);
        opts.scale_end = ctx.node->data.value("scaleEnd", 1.0);
        opts.profile_plane = ctx.node->data.value("profilePlane", "auto");

        emit_mesh(ctx, extrude_along_spline(splines, profile_mesh, opts));
        return PCG_OK;
    }
};

class TransformMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "TransformMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "TransformMesh missing node");

        const data::PcgMeshData mesh = get_mesh_input(ctx, "in", "TransformMesh missing mesh input");
        if (mesh.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "TransformMesh missing mesh input");

        TransformMeshOptions opts;
        opts.translate_x = ctx.node->data.value("translateX", 0.0);
        opts.translate_y = ctx.node->data.value("translateY", 0.0);
        opts.translate_z = ctx.node->data.value("translateZ", 0.0);
        opts.rotation_x_deg = ctx.node->data.value("rotationX", 0.0);
        opts.rotation_y_deg = ctx.node->data.value("rotationY", 0.0);
        opts.rotation_z_deg = ctx.node->data.value("rotationZ", 0.0);
        opts.scale_x = ctx.node->data.value("scaleX", 1.0);
        opts.scale_y = ctx.node->data.value("scaleY", 1.0);
        opts.scale_z = ctx.node->data.value("scaleZ", 1.0);

        emit_mesh(ctx, transform_mesh(mesh, opts));
        return PCG_OK;
    }
};

class MergeMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MergeMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MergeMesh missing node");

        const data::PcgMeshData mesh_a = get_mesh_input(ctx, "a", "MergeMesh missing mesh input A");
        const data::PcgMeshData mesh_b = get_mesh_input(ctx, "b", "MergeMesh missing mesh input B");
        if (mesh_a.vertices().empty() && mesh_b.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MergeMesh missing mesh inputs");

        emit_mesh(ctx, merge_meshes(mesh_a, mesh_b));
        return PCG_OK;
    }
};

class CrossSectionProfileElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CrossSectionProfile"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CrossSectionProfile missing node");

        const data::PcgMeshData mesh =
            get_mesh_input(ctx, "in", "CrossSectionProfile missing mesh input");
        if (mesh.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CrossSectionProfile missing mesh input");

        CrossSectionProfileOptions opts;
        opts.plane = ctx.node->data.value("plane", "auto");
        opts.weld_epsilon = ctx.node->data.value("weldEpsilon", 1e-4);
        opts.center = ctx.node->data.value("center", true);

        emit_mesh(ctx, extract_cross_section_profile(mesh, opts));
        return PCG_OK;
    }
};

class InstanceAlongSplineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "InstanceAlongSpline"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "InstanceAlongSpline missing node");

        const data::PcgSplineData splines =
            get_splines_input(ctx, "spline", "InstanceAlongSpline missing spline input");
        const data::PcgMeshData prototype =
            get_mesh_input(ctx, "mesh", "InstanceAlongSpline missing prototype mesh");
        if (splines.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "InstanceAlongSpline missing spline input");
        if (prototype.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "InstanceAlongSpline missing prototype mesh");

        InstanceAlongSplineOptions opts;
        opts.spacing = ctx.node->data.value("spacing", 5.0);
        opts.offset = ctx.node->data.value("offset", 0.0);
        opts.include_end = ctx.node->data.value("includeEnd", false);
        opts.align_to_tangent = ctx.node->data.value("alignToTangent", true);
        opts.scale = ctx.node->data.value("scale", 1.0);

        emit_mesh(ctx, instance_along_spline(splines, prototype, opts));
        return PCG_OK;
    }
};

} // namespace

void register_spline_mesh_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("SweepAlongSpline", std::make_unique<SweepAlongSplineElement>());
    map.emplace("ExtrudeAlongSpline", std::make_unique<ExtrudeAlongSplineElement>());
    map.emplace("TransformMesh", std::make_unique<TransformMeshElement>());
    map.emplace("MergeMesh", std::make_unique<MergeMeshElement>());
    map.emplace("CrossSectionProfile", std::make_unique<CrossSectionProfileElement>());
    map.emplace("InstanceAlongSpline", std::make_unique<InstanceAlongSplineElement>());
}

} // namespace pcg::internal::elements
