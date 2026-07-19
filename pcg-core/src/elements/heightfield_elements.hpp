#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {

class IPcgElement;

void register_heightfield_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map);

} // namespace pcg::internal::elements
