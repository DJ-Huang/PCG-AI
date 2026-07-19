#pragma once

#include "elements/pcg_element.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {

void register_vehicle_modeling_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map);

} // namespace pcg::internal::elements
