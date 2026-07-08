#pragma once

#include "data/pcg_point_data.hpp"

#include <cstdint>

namespace pcg::internal::data {

uint32_t detect_point_attr_flags(const PcgPointData& points);
int point_binary_size(const PcgPointData& points, uint32_t flags);
bool write_point_binary(const PcgPointData& points, void* buffer, int buffer_size, uint32_t* out_flags);

} // namespace pcg::internal::data
