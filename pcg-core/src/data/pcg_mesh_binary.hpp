#pragma once

#include "data/pcg_mesh_data.hpp"

#include <cstdint>

namespace pcg::internal::data {

constexpr uint32_t kPcgMeshBinaryMagic = 0x4D474350u; // 'PCGM' little-endian
constexpr uint32_t kPcgMeshBinaryVersion = 2u;
constexpr int kPcgMeshBinaryHeaderSize = 16;   // v1 header (backward compat)
constexpr int kPcgMeshBinaryV2HeaderSize = 20;  // v2 header (adds flags)
constexpr uint32_t kPcgMeshBinaryFlagHasNormals = 0x1u;
constexpr uint32_t kPcgMeshBinaryFlagHasColors  = 0x2u;
constexpr uint32_t kPcgMeshBinaryFlagHasUVs     = 0x4u;

/** Bytes required for v2 header + float32 positions + uint32 indices + optional normals. */
int mesh_binary_size(const PcgMeshData& mesh);

/** Writes mesh as [magic|version|vert_count|index_count|flags][float3*N][uint32*M][float3*N]? */
bool write_mesh_binary(const PcgMeshData& mesh, void* buffer, int buffer_size);

/** Parses binary mesh written by write_mesh_binary (supports v1 and v2). */
bool read_mesh_binary(const void* buffer, int buffer_size, PcgMeshData& out);

} // namespace pcg::internal::data
