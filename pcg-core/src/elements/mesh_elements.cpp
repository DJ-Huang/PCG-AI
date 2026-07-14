#include "elements/mesh_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/spline_algorithms.hpp"
#include "texture_runtime.hpp"

namespace pcg::internal::elements {
namespace {

class CreateBoxMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CreateBoxMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateBoxMesh missing node");

        const double width = ctx.node->data.value("width", 2.0);
        const double height = ctx.node->data.value("height", 2.0);
        const double depth = ctx.node->data.value("depth", 2.0);
        if (width < 0.0 || height < 0.0 || depth < 0.0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateBoxMesh dimensions must be >= 0");

        data::PcgMeshData mesh = create_box_mesh(width, height, depth);

        // Merge optional input mesh
        if (const data::PcgMeshData* input = ctx.inputs.find_mesh("in"))
        {
            const int vertex_offset = static_cast<int>(mesh.vertices().size());
            for (const auto& v : input->vertices())
                mesh.vertices_mut().push_back(v);
            for (int idx : input->triangles())
                mesh.triangles_mut().push_back(idx + vertex_offset);
        }
        else if (const nlohmann::json* input = ctx.inputs.find_json("in"))
        {
            const data::PcgMeshData input_mesh = parse_mesh_input(*input);
            const int vertex_offset = static_cast<int>(mesh.vertices().size());
            for (const auto& v : input_mesh.vertices())
                mesh.vertices_mut().push_back(v);
            for (int idx : input_mesh.triangles())
                mesh.triangles_mut().push_back(idx + vertex_offset);
        }

        emit_mesh(ctx, std::move(mesh));
        return PCG_OK;
    }
};

class SubdivideMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SubdivideMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SubdivideMesh missing node");

        const int levels = ctx.node->data.value("levels", 1);
        const std::string method_str = ctx.node->data.value("method", std::string("catmullClark"));
        const SubdivideMethod method =
            method_str == "loop" ? SubdivideMethod::Loop :
            method_str == "simple" ? SubdivideMethod::Simple :
            SubdivideMethod::CatmullClark;

        const data::PcgMeshData mesh = get_mesh_input(ctx, "in", "SubdivideMesh missing mesh input");
        if (mesh.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SubdivideMesh missing mesh input");

        emit_mesh(ctx, subdivide_mesh(mesh, levels, method));
        return PCG_OK;
    }
};

class BevelMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "BevelMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BevelMesh missing node");

        const data::PcgGeometry geometry =
            get_geometry_input(ctx, "in", "BevelMesh missing mesh input");
        if (geometry.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BevelMesh missing mesh input");

        const double amount = ctx.node->data.value("amount", 0.1);
        const int segments = ctx.node->data.value("segments", 2);
        const std::string method_str = ctx.node->data.value("method", std::string("edge"));
        const BevelMethod method =
            method_str == "vertexPush" ? BevelMethod::VertexPush : BevelMethod::Edge;
        const std::string offset_type_str = ctx.node->data.value("offsetType", std::string("offset"));
        const BevelOffsetType offset_type =
            offset_type_str == "width" ? BevelOffsetType::Width : BevelOffsetType::Offset;
        const bool clamp_overlap = ctx.node->data.value("clampOverlap", true);
        const double angle_limit = ctx.node->data.value("angleLimit", 30.0);
        const float profile = static_cast<float>(ctx.node->data.value("profile", 0.5));
        const auto parse_miter = [](const std::string& s) {
            if (s == "patch") return BevelMiter::Patch;
            if (s == "arc") return BevelMiter::Arc;
            return BevelMiter::Sharp;
        };
        const BevelMiter miter_outer =
            parse_miter(ctx.node->data.value("miterOuter", std::string("sharp")));
        const BevelMiter miter_inner =
            parse_miter(ctx.node->data.value("miterInner", std::string("sharp")));
        const std::string vmesh_str = ctx.node->data.value("vmeshMethod", std::string("adj"));
        const BevelVMeshMethod vmesh_method =
            vmesh_str == "cutoff" ? BevelVMeshMethod::Cutoff : BevelVMeshMethod::Adj;

        BevelEdgeSelection edge_selection;
        edge_selection.edge_group = ctx.node->data.value("edgeGroup", std::string(""));
        edge_selection.exclude_unshared = ctx.node->data.value("excludeUnshared", true);
        edge_selection.exclude_groups = parse_name_list(ctx.node->data, "excludeGroups");

        emit_geometry(ctx, bevel_geometry(geometry, amount, segments, method, offset_type, clamp_overlap,
                                        angle_limit, profile, miter_outer, miter_inner, vmesh_method,
                                        ctx.is_cancel_requested, edge_selection));
        if (ctx.is_cancel_requested && ctx.is_cancel_requested())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Execution cancelled");
        return PCG_OK;
    }
};

class MeshNoiseDeformElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MeshNoiseDeform"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MeshNoiseDeform missing node");

        const data::PcgMeshData mesh = get_mesh_input(ctx, "in", "MeshNoiseDeform missing mesh input");
        if (mesh.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MeshNoiseDeform missing mesh input");

        NoiseDeformOptions opts;
        opts.intensity = ctx.node->data.value("intensity", 0.02);
        opts.noise_scale = ctx.node->data.value("scale", 2.0);
        opts.mid_level = ctx.node->data.value("midLevel", 0.5);
        opts.seed = ctx.graph_seed;

        const std::string noise_type_str = ctx.node->data.value("noiseType", std::string("perlin"));
        opts.noise_type =
            noise_type_str == "texture" ? NoiseDeformType::Texture : NoiseDeformType::Perlin;

        const std::string coords_str = ctx.node->data.value("textureCoords", std::string("local"));
        if (coords_str == "local")
            opts.texture_coords = TextureCoordsMode::Local;

        if (const nlohmann::json* tex_desc = ctx.inputs.find_json("texture")) {
            if (tex_desc->value("kind", "") == "texture") {
                opts.noise_type = NoiseDeformType::Texture;
                const std::string slot_id = tex_desc->value("slotId", "");
                opts.repeat_x = tex_desc->value("repeatX", 1.0);
                opts.repeat_y = tex_desc->value("repeatY", 1.0);
                if (!ctx.textures || slot_id.empty())
                    return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                    "MeshNoiseDeform texture input missing runtime pixels");
                opts.texture = ctx.textures->find(slot_id);
                if (!opts.texture)
                    return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                    "MeshNoiseDeform texture slot not uploaded");
            }
        }

        if (opts.noise_type == NoiseDeformType::Texture && !opts.texture)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MeshNoiseDeform requires a connected ImageTexture");

        emit_mesh(ctx, noise_deform_mesh(mesh, opts));
        return PCG_OK;
    }
};

class ImageTextureElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ImageTexture"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ImageTexture missing node");

        const double repeat_x = ctx.node->data.value("repeatX", 1.0);
        const double repeat_y = ctx.node->data.value("repeatY", 1.0);

        nlohmann::json out{
            {"kind", "texture"},
            {"slotId", ctx.node->id},
            {"repeatX", repeat_x},
            {"repeatY", repeat_y},
        };
        ctx.outputs.add("out", data::PcgDataType::Param, std::move(out));
        return PCG_OK;
    }
};

} // namespace

class CreateCylinderMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CreateCylinderMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateCylinderMesh missing node");

        const double radius = ctx.node->data.value("radius", 1.0);
        const double height = ctx.node->data.value("height", 2.0);
        const int radial_segments = ctx.node->data.value("radialSegments", 16);
        const int height_segments = ctx.node->data.value("heightSegments", 1);
        const bool cap_top = ctx.node->data.value("capTop", true);
        const bool cap_bottom = ctx.node->data.value("capBottom", true);

        data::PcgMeshData mesh = create_cylinder_mesh(radius, height,
                                                       radial_segments, height_segments,
                                                       cap_top, cap_bottom);
        if (mesh.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateCylinderMesh invalid parameters");

        if (const data::PcgMeshData* input = ctx.inputs.find_mesh("in"))
        {
            const int vertex_offset = static_cast<int>(mesh.vertices().size());
            for (const auto& v : input->vertices())
                mesh.vertices_mut().push_back(v);
            for (int idx : input->triangles())
                mesh.triangles_mut().push_back(idx + vertex_offset);
        }
        else if (const nlohmann::json* input = ctx.inputs.find_json("in"))
        {
            const data::PcgMeshData input_mesh = parse_mesh_input(*input);
            const int vertex_offset = static_cast<int>(mesh.vertices().size());
            for (const auto& v : input_mesh.vertices())
                mesh.vertices_mut().push_back(v);
            for (int idx : input_mesh.triangles())
                mesh.triangles_mut().push_back(idx + vertex_offset);
        }

        emit_mesh(ctx, std::move(mesh));
        return PCG_OK;
    }
};

class RevolveMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "RevolveMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "RevolveMesh missing node");

        data::PcgSplineData profile = get_splines_input(ctx, "profile", "RevolveMesh missing profile input");
        if (profile.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "RevolveMesh profile has no splines");

        RevolveGeometryOptions opts;
        opts.axis = ctx.node->data.value("axis", "y");
        opts.segments = ctx.node->data.value("segments", 16);
        opts.close_profile = ctx.node->data.value("closeProfile", false);
        opts.cap_start = ctx.node->data.value("capStart", false);
        opts.cap_end = ctx.node->data.value("capEnd", false);

        data::PcgGeometry geo = revolve_geometry(profile, opts);
        if (geo.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "RevolveMesh produced empty geometry");

        emit_geometry(ctx, std::move(geo));
        return PCG_OK;
    }
};

void register_mesh_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("CreateBoxMesh", std::make_unique<CreateBoxMeshElement>());
    map.emplace("CreateCylinderMesh", std::make_unique<CreateCylinderMeshElement>());
    map.emplace("RevolveMesh", std::make_unique<RevolveMeshElement>());
    map.emplace("SubdivideMesh", std::make_unique<SubdivideMeshElement>());
    map.emplace("BevelMesh", std::make_unique<BevelMeshElement>());
    map.emplace("MeshNoiseDeform", std::make_unique<MeshNoiseDeformElement>());
    map.emplace("ImageTexture", std::make_unique<ImageTextureElement>());
}

} // namespace pcg::internal::elements
