#pragma once

#include "data/pcg_heightfield.hpp"

#include <cstdint>

namespace pcg::internal::data {

constexpr uint32_t kPcgHeightFieldBinaryMagic = 0x48474350u; // 'PCGH'
constexpr uint32_t kPcgHeightFieldBinaryVersion = 1u;
constexpr int kPcgHeightFieldBinaryHeaderSize = 72;

int heightfield_binary_size(const PcgHeightField& heightfield);
bool write_heightfield_binary(const PcgHeightField& heightfield,
                              void* buffer,
                              int buffer_size);
bool read_heightfield_binary(const void* buffer,
                             int buffer_size,
                             PcgHeightField& out_heightfield);

} // namespace pcg::internal::data
