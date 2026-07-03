#pragma once

#include "data/pcg_context.hpp"
#include "pcg_api.h"

namespace pcg::internal::elements {

class IPcgElement {
public:
    virtual ~IPcgElement() = default;
    virtual const char* type_name() const = 0;
    virtual PcgResultCode execute(PcgContext& ctx) const = 0;
};

void register_builtin_elements();
const IPcgElement* find_element(const std::string& type);
bool is_known_element_type(const std::string& type);

} // namespace pcg::internal::elements
