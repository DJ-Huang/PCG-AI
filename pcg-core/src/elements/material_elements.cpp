#include "elements/material_elements.hpp"
#include "elements/material_algorithms.hpp"
#include "elements/element_utils.hpp"

#include <algorithm>

namespace pcg::internal::elements {

namespace {

nlohmann::json build_material_definition(const nlohmann::json& data)
{
    nlohmann::json definition = {
        {"kind", "pcg.material"},
        {"version", 1},
        {"name", data.value("materialName", std::string("Material"))},
        {"shaderId", data.value("shaderId", std::string("pcg.standard-pbr"))},
        {"baseColor", data.value("baseColor", std::string("#b8c2cc"))},
        {"baseColorMap", data.value("baseColorMap", std::string())},
        {"metallic", std::clamp(data.value("metallic", 0.0), 0.0, 1.0)},
        {"metallicMap", data.value("metallicMap", std::string())},
        {"roughness", std::clamp(data.value("roughness", 0.5), 0.0, 1.0)},
        {"roughnessMap", data.value("roughnessMap", std::string())},
        {"normalMap", data.value("normalMap", std::string())},
        {"normalScale", std::max(0.0, data.value("normalScale", 1.0))},
        {"aoMap", data.value("aoMap", std::string())},
        {"aoIntensity", std::max(0.0, data.value("aoIntensity", 1.0))},
        {"emissiveColor", data.value("emissiveColor", std::string("#000000"))},
        {"emissiveMap", data.value("emissiveMap", std::string())},
        {"emissiveIntensity", std::max(0.0, data.value("emissiveIntensity", 0.0))},
        {"opacity", std::clamp(data.value("opacity", 1.0), 0.0, 1.0)},
        {"alphaMode", data.value("alphaMode", std::string("opaque"))},
        {"alphaCutoff", std::clamp(data.value("alphaCutoff", 0.5), 0.0, 1.0)},
        {"doubleSided", data.value("doubleSided", false)},
        {"unityShaderGuid", data.value("unityShaderGuid", std::string())},
        {"unityShaderName", data.value("unityShaderName", std::string())},
        {"unityPropertiesJson", data.value("unityPropertiesJson", std::string("{}"))},
    };
    return definition;
}

} // namespace

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

class MaterialElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Material"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Material missing node");
        ctx.outputs.add("out", data::PcgDataType::Param,
                        build_material_definition(ctx.node->data));
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

        const nlohmann::json* material_input = ctx.inputs.find_json("material");
        nlohmann::json material_definition = material_input && material_input->is_object()
            ? *material_input
            : nlohmann::json();
        std::string material_name = ctx.node->data.value("materialName", std::string());
        if (material_name.empty() && material_definition.is_object())
            material_name = material_definition.value("name", std::string("Material"));
        const std::vector<std::string> face_groups = parse_name_list(ctx.node->data, "group");

        if (const data::PcgGeometry* geometry = ctx.inputs.find_geometry("in")) {
            data::PcgGeometry out = *geometry;
            assign_material(out, material_name, face_groups);
            attach_material_definition(out, material_name, material_definition);
            emit_geometry(ctx, std::move(out));
            return PCG_OK;
        }

        if (!face_groups.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "AssignMaterial group selection requires geometry input");

        data::PcgMeshData mesh = get_mesh_input(ctx, "in", "AssignMaterial missing mesh input");
        assign_material(mesh, material_name);
        attach_material_definition(mesh, material_name, material_definition);
        emit_mesh(ctx, std::move(mesh));
        return PCG_OK;
    }
};

void register_material_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("VertexColor", std::make_unique<VertexColorElement>());
    map.emplace("Material", std::make_unique<MaterialElement>());
    map.emplace("AssignMaterial", std::make_unique<AssignMaterialElement>());
}

} // namespace pcg::internal::elements
