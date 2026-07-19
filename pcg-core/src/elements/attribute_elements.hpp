#pragma once

#include "elements/pcg_element.hpp"

#include <memory>
#include <unordered_map>

namespace pcg::internal::elements {

void register_attribute_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map);

} // namespace pcg::internal::elements
