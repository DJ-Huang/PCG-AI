#pragma once

#include "data/pcg_mesh_data.hpp"

#include <cstdint>
#include <vector>

namespace pcg::internal::data {

constexpr uint32_t kPcgMeshBinaryMagic = 0x4D474350u; // 'PCGM' little-endian
constexpr uint32_t kPcgMeshBinaryVersion = 3u;
constexpr int kPcgMeshBinaryHeaderSize = 16;   // v1 header (backward compat)
constexpr int kPcgMeshBinaryV2HeaderSize = 20;  // v2 header (adds flags)
constexpr int kPcgMeshBinaryV3HeaderSize = 24;  // v3 header (adds material section size)
constexpr uint32_t kPcgMeshBinaryFlagHasNormals = 0x1u;
constexpr uint32_t kPcgMeshBinaryFlagHasColors  = 0x2u;
constexpr uint32_t kPcgMeshBinaryFlagHasUVs     = 0x4u;
constexpr uint32_t kPcgMeshBinaryFlagHasMaterials = 0x8u;

/** Bytes required for v2 header + float32 positions + uint32 indices + optional normals. */
int mesh_binary_size(const PcgMeshData& mesh);

/** Writes mesh as [magic|version|vert_count|index_count|flags][float3*N][uint32*M][float3*N]? */
bool write_mesh_binary(const PcgMeshData& mesh, void* buffer, int buffer_size);

/** Parses binary mesh written by write_mesh_binary (supports v1 and v2). */
bool read_mesh_binary(const void* buffer, int buffer_size, PcgMeshData& out);

/** Multi-prototype spawn pack: 'PCMS' + point counts + concatenated mesh binaries. */
constexpr uint32_t kPcgMultiSpawnMagic = 0x534D4350u; // 'PCMS'
constexpr uint32_t kPcgMultiSpawnVersion = 1u;

int multi_spawn_binary_size(const std::vector<PcgMeshData>& meshes,
                            const std::vector<int>& point_counts);

bool write_multi_spawn_binary(const std::vector<PcgMeshData>& meshes,
                              const std::vector<int>& point_counts,
                              void* buffer,
                              int buffer_size);

bool read_multi_spawn_binary(const void* buffer,
                             int buffer_size,
                             std::vector<PcgMeshData>& out_meshes,
                             std::vector<int>& out_point_counts);

} // namespace pcg::internal::data
