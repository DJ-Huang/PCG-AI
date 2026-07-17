#include "elements/material_algorithms.hpp"

namespace pcg::internal::elements {

void vertex_color_mesh(data::PcgMeshData& mesh, double r, double g, double b, double a)
{
    const int vc = static_cast<int>(mesh.vertices().size());
    if (vc == 0)
        return;
    std::vector<data::PcgColor> colors(vc, {r, g, b, a});
    mesh.set_colors(colors);
}

void assign_material(data::PcgMeshData& mesh, const std::string& material_name)
{
    mesh.metadata().set("material", nlohmann::json(material_name));
    mesh.metadata().set("material_slots", nlohmann::json::array({material_name}));
    mesh.set_materials({material_name},
                       std::vector<uint32_t>(mesh.triangles().size() / 3, 0u));
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

} // namespace pcg::internal::elements
