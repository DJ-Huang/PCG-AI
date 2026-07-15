#pragma once

#include "data/pcg_mesh_data.hpp"
#include "data/pcg_geometry.hpp"

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

// Geometry-native UV generation — operates on PcgGeometry points directly,
// avoiding geometry→mesh→geometry round-trip that triangulates n-gon faces.
std::vector<data::PcgVec2> generate_uv_geometry(const data::PcgGeometry& geometry,
                                                 const std::string& projection,
                                                 const std::string& axis,
                                                 double scale_u, double scale_v,
                                                 double offset_u, double offset_v);

std::vector<data::PcgVec2> project_texture_uv_geometry(const data::PcgGeometry& geometry,
                                                        const std::string& direction,
                                                        double scale_u, double scale_v,
                                                        double offset_u, double offset_v,
                                                        double repeat_x, double repeat_y);

} // namespace pcg::internal::elements
