#include "elements/uv_elements.hpp"
#include "elements/uv_algorithms.hpp"
#include "elements/element_utils.hpp"

namespace pcg::internal::elements {
namespace {

bool is_supported_projection(const std::string& projection)
{
    return projection == "planar" || projection == "cylindrical" || projection == "spherical";
}

} // namespace

class UVTextureElement final : public IPcgElement {
public:
    const char* type_name() const override { return "UVTexture"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "UVTexture missing node");

        const std::string projection = ctx.node->data.value("projection", "planar");
        if (!is_supported_projection(projection)) {
            const std::string message =
                "UVTexture unsupported projection '" + projection +
                "' (box deferred; use planar/cylindrical/spherical)";
            return fail_ctx(ctx, PCG_ERR_EXECUTION, message.c_str());
        }

        const std::string axis = ctx.node->data.value("axis", "y");
        const double scale_u = ctx.node->data.value("scaleU", 1.0);
        const double scale_v = ctx.node->data.value("scaleV", 1.0);
        const double offset_u = ctx.node->data.value("offsetU", 0.0);
        const double offset_v = ctx.node->data.value("offsetV", 0.0);

        if (const data::PcgGeometry* geometry = ctx.inputs.find_geometry("in")) {
            data::PcgGeometry out = *geometry;
            out.set_uvs(generate_uv_geometry(*geometry, projection, axis,
                                             scale_u, scale_v, offset_u, offset_v));
            out.expand_point_uvs_to_corners();
            emit_geometry(ctx, std::move(out));
            return PCG_OK;
        }

        data::PcgMeshData mesh = get_mesh_input(ctx, "in", "UVTexture missing mesh input");
        generate_uv(mesh, projection, axis, scale_u, scale_v, offset_u, offset_v);
        emit_mesh(ctx, std::move(mesh));
        return PCG_OK;
    }
};

class ProjectTextureElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ProjectTexture"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ProjectTexture missing node");

        const nlohmann::json* tex_json = ctx.inputs.find_json("texture");
        if (!tex_json) {
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "ProjectTexture requires texture input connection");
        }

        double repeat_x = 1.0, repeat_y = 1.0;
        if (tex_json->is_object()) {
            repeat_x = tex_json->value("repeatX", 1.0);
            repeat_y = tex_json->value("repeatY", 1.0);
        }

        const std::string direction = ctx.node->data.value("direction", "z");
        const double scale_u = ctx.node->data.value("scaleU", 1.0);
        const double scale_v = ctx.node->data.value("scaleV", 1.0);
        const double offset_u = ctx.node->data.value("offsetU", 0.0);
        const double offset_v = ctx.node->data.value("offsetV", 0.0);

        ProjectTextureCropBounds crop;
        crop.use_reference = ctx.node->data.value("useReferenceBounds", false);
        crop.min_u = ctx.node->data.value("refMinU", 0.0);
        crop.max_u = ctx.node->data.value("refMaxU", 1.0);
        crop.min_v = ctx.node->data.value("refMinV", 0.0);
        crop.max_v = ctx.node->data.value("refMaxV", 1.0);

        if (const data::PcgGeometry* geometry = ctx.inputs.find_geometry("in")) {
            data::PcgGeometry out = *geometry;
            out.set_uvs(project_texture_uv_geometry(*geometry, direction,
                                                     scale_u, scale_v, offset_u, offset_v,
                                                     repeat_x, repeat_y, crop));
            out.expand_point_uvs_to_corners();
            emit_geometry(ctx, std::move(out));
            return PCG_OK;
        }

        data::PcgMeshData mesh = get_mesh_input(ctx, "in", "ProjectTexture missing mesh input");
        project_texture_uv(mesh, direction, scale_u, scale_v,
                           offset_u, offset_v, repeat_x, repeat_y, crop);
        emit_mesh(ctx, std::move(mesh));
        return PCG_OK;
    }
};

void register_uv_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("UVTexture", std::make_unique<UVTextureElement>());
    map.emplace("ProjectTexture", std::make_unique<ProjectTextureElement>());
}

} // namespace pcg::internal::elements
