#pragma once

#include "elements/pcg_element.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {

/// Registers the Houdini basic-modeling SOP compatibility layer.  Each SOP has
/// an independent manifest type while sharing the geometry/attribute kernels
/// implemented in houdini_sop_elements.cpp.
void register_houdini_sop_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map);

} // namespace pcg::internal::elements
