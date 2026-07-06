#pragma once

#include "data/pcg_mesh_data.hpp"

#include <cstdint>

namespace pcg::internal::data {

constexpr uint32_t kPcgMeshBinaryMagic = 0x4D474350u; // 'PCGM' little-endian
constexpr uint32_t kPcgMeshBinaryVersion = 1u;
constexpr int kPcgMeshBinaryHeaderSize = 16;

/** Bytes required for header + float32 positions + uint32 indices. */
int mesh_binary_size(const PcgMeshData& mesh);

/** Writes mesh as [magic|version|vert_count|index_count][float3*N][uint32*M]. */
bool write_mesh_binary(const PcgMeshData& mesh, void* buffer, int buffer_size);

/** Parses binary mesh written by write_mesh_binary. */
bool read_mesh_binary(const void* buffer, int buffer_size, PcgMeshData& out);

} // namespace pcg::internal::data
