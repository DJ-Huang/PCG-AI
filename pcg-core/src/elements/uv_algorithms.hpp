#pragma once

#include "data/pcg_mesh_data.hpp"

#include <string>

namespace pcg::internal::elements {

void generate_uv(data::PcgMeshData& mesh,
                 const std::string& projection,
                 const std::string& axis,
                 double scale_u, double scale_v,
                 double offset_u, double offset_v);

void project_texture_uv(data::PcgMeshData& mesh,
                        const std::string& direction,
                        double scale_u, double scale_v,
                        double offset_u, double offset_v,
                        double repeat_x, double repeat_y);

} // namespace pcg::internal::elements
