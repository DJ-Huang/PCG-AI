#pragma once

#include "data/pcg_mesh_data.hpp"

#include <string>

namespace pcg::internal::elements {

void vertex_color_mesh(data::PcgMeshData& mesh, double r, double g, double b, double a);
void assign_material(data::PcgMeshData& mesh, const std::string& material_name);

} // namespace pcg::internal::elements
