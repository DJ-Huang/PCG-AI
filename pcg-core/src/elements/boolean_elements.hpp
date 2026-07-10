#pragma once

#include "elements/pcg_element.hpp"
#include "data/pcg_geometry.hpp"
#include "geometry/arrangement.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {

/// Execute boolean operation on two geometries.
data::PcgGeometry boolean_geometry(const data::PcgGeometry& a,
                                    const data::PcgGeometry& b,
                                    const geometry::BooleanOptions& opts);

void register_boolean_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map);

} // namespace pcg::internal::elements
