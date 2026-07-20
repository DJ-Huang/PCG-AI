#include "elements/material_elements.hpp"
#include "elements/material_algorithms.hpp"
#include "elements/element_utils.hpp"

#include <algorithm>

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
        const double emission = ctx.node->data.value("emission", 0.0);
        const double emission_r = ctx.node->data.value("emissionColorR", 1.0);
        const double emission_g = ctx.node->data.value("emissionColorG", 1.0);
        const double emission_b = ctx.node->data.value("emissionColorB", 1.0);

        double out_r = r;
        double out_g = g;
        double out_b = b;
        double out_a = a;
        if (emission > 0.0) {
            out_r = emission_r;
            out_g = emission_g;
            out_b = emission_b;
            out_a = std::clamp(emission, 0.0, 1.0);
        }

        if (const data::PcgGeometry* geometry = ctx.inputs.find_geometry("in")) {
            data::PcgGeometry out = *geometry;
            out.set_colors(std::vector<data::PcgColor>(
                out.points().size(), data::PcgColor{out_r, out_g, out_b, out_a}));
            emit_geometry(ctx, std::move(out));
            return PCG_OK;
        }

        data::PcgMeshData mesh = get_mesh_input(ctx, "in", "VertexColor missing mesh input");
        vertex_color_mesh(mesh, r, g, b, a, emission, emission_r, emission_g, emission_b);
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

        const std::string material_name = ctx.node->data.value("materialName", "");
        const std::vector<std::string> face_groups = parse_name_list(ctx.node->data, "group");

        if (const data::PcgGeometry* geometry = ctx.inputs.find_geometry("in")) {
            data::PcgGeometry out = *geometry;
            assign_material(out, material_name, face_groups);
            emit_geometry(ctx, std::move(out));
            return PCG_OK;
        }

        if (!face_groups.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "AssignMaterial group selection requires geometry input");

        data::PcgMeshData mesh = get_mesh_input(ctx, "in", "AssignMaterial missing mesh input");
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
