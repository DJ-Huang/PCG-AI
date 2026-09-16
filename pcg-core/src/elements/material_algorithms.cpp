#include "elements/material_algorithms.hpp"

#include <algorithm>

namespace pcg::internal::elements {

void vertex_color_mesh(data::PcgMeshData& mesh,
                       double r, double g, double b, double a,
                       double emission,
                       double emission_r, double emission_g, double emission_b)
{
    const int vc = static_cast<int>(mesh.vertices().size());
    if (vc == 0)
        return;

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

    std::vector<data::PcgColor> colors(vc, {out_r, out_g, out_b, out_a});
    mesh.set_colors(colors);
}

void assign_material(data::PcgMeshData& mesh, const std::string& material_name)
{
    mesh.metadata().set("material", nlohmann::json(material_name));
    mesh.metadata().set("material_slots", nlohmann::json::array({material_name}));
    mesh.set_materials({material_name},
                       std::vector<uint32_t>(mesh.triangles().size() / 3, 0u));
}

namespace {

void attach_material_definition(data::PcgMetadata& metadata,
                                const std::string& material_name,
                                const nlohmann::json& definition)
{
    if (material_name.empty() || !definition.is_object())
        return;
    nlohmann::json library = metadata.has("pbrMaterials") &&
                                     metadata.get("pbrMaterials").is_object()
        ? metadata.get("pbrMaterials")
        : nlohmann::json::object();
    library[material_name] = definition;
    metadata.set("pbrMaterials", library);
}

} // namespace

void attach_material_definition(data::PcgMeshData& mesh,
                                const std::string& material_name,
                                const nlohmann::json& definition)
{
    attach_material_definition(mesh.metadata(), material_name, definition);
}

void assign_material(data::PcgGeometry& geometry,
                     const std::string& material_name,
                     const std::vector<std::string>& face_groups)
{
    if (face_groups.empty()) {
        geometry.set_material_name(material_name);
        return;
    }

    std::vector<std::string> materials = geometry.has_face_materials()
        ? geometry.face_materials()
        : std::vector<std::string>(geometry.faces().size(),
                                   geometry.has_material() ? geometry.material_name() : "");
    for (const std::string& expression : face_groups) {
        const auto members = geometry.groups().eval(geometry::GroupDomain::Face, expression);
        for (int face_index : members) {
            if (face_index >= 0 && static_cast<size_t>(face_index) < materials.size())
                materials[static_cast<size_t>(face_index)] = material_name;
        }
    }
    geometry.set_face_materials(std::move(materials));
}

void attach_material_definition(data::PcgGeometry& geometry,
                                const std::string& material_name,
                                const nlohmann::json& definition)
{
    attach_material_definition(geometry.metadata(), material_name, definition);
}

} // namespace pcg::internal::elements
