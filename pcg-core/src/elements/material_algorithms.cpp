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
}

} // namespace pcg::internal::elements
