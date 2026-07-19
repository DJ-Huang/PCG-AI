#pragma once

#include "data/pcg_mesh_data.hpp"
#include "data/pcg_geometry.hpp"

#include <string>

namespace pcg::internal::elements {

void vertex_color_mesh(data::PcgMeshData& mesh, double r, double g, double b, double a);
void assign_material(data::PcgMeshData& mesh, const std::string& material_name);
void assign_material(data::PcgGeometry& geometry,
                     const std::string& material_name,
                     const std::vector<std::string>& face_groups);

} // namespace pcg::internal::elements
