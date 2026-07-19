#pragma once

#include "data/pcg_geometry.hpp"

#include <cstdint>

namespace pcg::internal::data {

constexpr uint32_t kPcgGeometryBinaryMagic = 0x47475043u; // 'PCGG' little-endian
constexpr uint32_t kPcgGeometryBinaryVersion = 3u;
constexpr uint32_t kPcgGeometryBinaryPreviousVersion = 2u;
constexpr int kPcgGeometryBinaryHeaderSize = 16;

int geometry_binary_size(const PcgGeometry& geometry);
bool write_geometry_binary(const PcgGeometry& geometry, void* buffer, int buffer_size);
bool read_geometry_binary(const void* buffer, int buffer_size, PcgGeometry& out);

} // namespace pcg::internal::data
