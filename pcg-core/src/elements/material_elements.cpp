#include "elements/material_elements.hpp"
#include "elements/material_algorithms.hpp"
#include "elements/element_utils.hpp"

namespace pcg::internal::elements {

class VertexColorElement final : public IPcgElement {
public:
    const char* type_name() const override { return "VertexColor"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "VertexColor missing node");

        const double r = ctx.node->data.value("r", 1.0);
        const double g = ctx.node->data.value("g", 1.0);
        const double b = ctx.node->data.value("b", 1.0);
        const double a = ctx.node->data.value("a", 1.0);

        if (const data::PcgGeometry* geometry = ctx.inputs.find_geometry("in")) {
            data::PcgGeometry out = *geometry;
            out.set_colors(std::vector<data::PcgColor>(
                out.points().size(), data::PcgColor{r, g, b, a}));
            emit_geometry(ctx, std::move(out));
            return PCG_OK;
        }

        data::PcgMeshData mesh = get_mesh_input(ctx, "in", "VertexColor missing mesh input");
        vertex_color_mesh(mesh, r, g, b, a);
        emit_mesh(ctx, std::move(mesh));
        return PCG_OK;
    }
};

class AssignMaterialElement final : public IPcgElement {
public:
    const char* type_name() const override { return "AssignMaterial"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AssignMaterial missing node");

        data::PcgMeshData mesh = get_mesh_input(ctx, "in", "AssignMaterial missing mesh input");

        const std::string material_name = ctx.node->data.value("materialName", "");
        assign_material(mesh, material_name);
        emit_mesh(ctx, std::move(mesh));
        return PCG_OK;
    }
};

void register_material_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("VertexColor", std::make_unique<VertexColorElement>());
    map.emplace("AssignMaterial", std::make_unique<AssignMaterialElement>());
}

} // namespace pcg::internal::elements
