#include "elements/uv_elements.hpp"
#include "elements/uv_algorithms.hpp"
#include "elements/element_utils.hpp"

namespace pcg::internal::elements {

class UVTextureElement final : public IPcgElement {
public:
    const char* type_name() const override { return "UVTexture"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "UVTexture missing node");

        data::PcgMeshData mesh = get_mesh_input(ctx, "in", "UVTexture missing mesh input");

        const std::string projection = ctx.node->data.value("projection", "planar");
        const std::string axis = ctx.node->data.value("axis", "y");
        const double scale_u = ctx.node->data.value("scaleU", 1.0);
        const double scale_v = ctx.node->data.value("scaleV", 1.0);
        const double offset_u = ctx.node->data.value("offsetU", 0.0);
        const double offset_v = ctx.node->data.value("offsetV", 0.0);

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

        data::PcgMeshData mesh = get_mesh_input(ctx, "in", "ProjectTexture missing mesh input");

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

        project_texture_uv(mesh, direction, scale_u, scale_v,
                           offset_u, offset_v, repeat_x, repeat_y);
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
