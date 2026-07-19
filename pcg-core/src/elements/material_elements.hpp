#pragma once

#include "elements/pcg_element.hpp"

#include <unordered_map>
#include <memory>
#include <string>

namespace pcg::internal::elements {

void register_material_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map);

} // namespace pcg::internal::elements
