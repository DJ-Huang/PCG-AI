#include "data/pcg_mesh_binary.hpp"

#include <cstring>

namespace pcg::internal::data {
namespace {

void write_u32(uint8_t* dst, uint32_t value)
{
    std::memcpy(dst, &value, sizeof(value));
}

bool read_u32(const uint8_t* src, int remaining, uint32_t& out)
{
    if (remaining < static_cast<int>(sizeof(uint32_t)))
        return false;
    std::memcpy(&out, src, sizeof(uint32_t));
    return true;
}

} // namespace

int mesh_binary_size(const PcgMeshData& mesh)
{
    const int vertex_count = static_cast<int>(mesh.vertices().size());
    const int index_count = static_cast<int>(mesh.triangles().size());
    int size = kPcgMeshBinaryV2HeaderSize + vertex_count * 3 * static_cast<int>(sizeof(float)) +
               index_count * static_cast<int>(sizeof(uint32_t));
    if (mesh.has_normals())
        size += vertex_count * 3 * static_cast<int>(sizeof(float));
    return size;
}

bool write_mesh_binary(const PcgMeshData& mesh, void* buffer, int buffer_size)
{
    if (!buffer || buffer_size < kPcgMeshBinaryV2HeaderSize)
        return false;

    const int vertex_count = static_cast<int>(mesh.vertices().size());
    const int index_count = static_cast<int>(mesh.triangles().size());
    const int required = mesh_binary_size(mesh);
    if (buffer_size < required)
        return false;

    const uint32_t flags = mesh.has_normals() ? kPcgMeshBinaryFlagHasNormals : 0u;

    auto* bytes = static_cast<uint8_t*>(buffer);
    write_u32(bytes + 0, kPcgMeshBinaryMagic);
    write_u32(bytes + 4, kPcgMeshBinaryVersion);
    write_u32(bytes + 8, static_cast<uint32_t>(vertex_count));
    write_u32(bytes + 12, static_cast<uint32_t>(index_count));
    write_u32(bytes + 16, flags);

    int offset = kPcgMeshBinaryV2HeaderSize;
    for (const auto& vertex : mesh.vertices()) {
        const float position[3] = {
            static_cast<float>(vertex.x),
            static_cast<float>(vertex.y),
            static_cast<float>(vertex.z),
        };
        std::memcpy(bytes + offset, position, sizeof(position));
        offset += static_cast<int>(sizeof(position));
    }

    for (const int index : mesh.triangles()) {
        const uint32_t value = static_cast<uint32_t>(index);
        std::memcpy(bytes + offset, &value, sizeof(value));
        offset += static_cast<int>(sizeof(value));
    }

    if (mesh.has_normals()) {
        for (const auto& normal : mesh.normals()) {
            const float n[3] = {
                static_cast<float>(normal.x),
                static_cast<float>(normal.y),
                static_cast<float>(normal.z),
            };
            std::memcpy(bytes + offset, n, sizeof(n));
            offset += static_cast<int>(sizeof(n));
        }
    }

    return true;
}

bool read_mesh_binary(const void* buffer, int buffer_size, PcgMeshData& out)
{
    if (!buffer || buffer_size < kPcgMeshBinaryHeaderSize)
        return false;

    const auto* bytes = static_cast<const uint8_t*>(buffer);

    uint32_t magic = 0;
    if (!read_u32(bytes + 0, buffer_size - 0, magic) || magic != kPcgMeshBinaryMagic)
        return false;

    uint32_t version = 0;
    if (!read_u32(bytes + 4, buffer_size - 4, version))
        return false;

    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    if (!read_u32(bytes + 8, buffer_size - 8, vertex_count))
        return false;
    if (!read_u32(bytes + 12, buffer_size - 12, index_count))
        return false;

    if (version == 1u) {
        const int required = kPcgMeshBinaryHeaderSize +
                             static_cast<int>(vertex_count) * 3 * static_cast<int>(sizeof(float)) +
                             static_cast<int>(index_count) * static_cast<int>(sizeof(uint32_t));
        if (buffer_size < required)
            return false;

        if (index_count % 3 != 0)
            return false;

        out = PcgMeshData{};
        int offset = kPcgMeshBinaryHeaderSize;

        for (uint32_t i = 0; i < vertex_count; ++i) {
            float position[3] = {};
            std::memcpy(position, bytes + offset, sizeof(position));
            offset += static_cast<int>(sizeof(position));
            out.add_vertex(PcgVertex{
                static_cast<double>(position[0]),
                static_cast<double>(position[1]),
                static_cast<double>(position[2]),
            });
        }

        for (uint32_t i = 0; i < index_count; ++i) {
            uint32_t index = 0;
            std::memcpy(&index, bytes + offset, sizeof(index));
            offset += static_cast<int>(sizeof(index));
            if (index >= vertex_count)
                return false;
            out.triangles_mut().push_back(static_cast<int>(index));
        }

        return true;
    }

    if (version == 2u) {
        if (buffer_size < kPcgMeshBinaryV2HeaderSize)
            return false;

        uint32_t flags = 0;
        if (!read_u32(bytes + 16, buffer_size - 16, flags))
            return false;

        const bool has_normals = (flags & kPcgMeshBinaryFlagHasNormals) != 0u;
        const int required = kPcgMeshBinaryV2HeaderSize +
                              static_cast<int>(vertex_count) * 3 * static_cast<int>(sizeof(float)) +
                              static_cast<int>(index_count) * static_cast<int>(sizeof(uint32_t)) +
                              (has_normals ? static_cast<int>(vertex_count) * 3 * static_cast<int>(sizeof(float)) : 0);
        if (buffer_size < required)
            return false;

        if (index_count % 3 != 0)
            return false;

        out = PcgMeshData{};
        int offset = kPcgMeshBinaryV2HeaderSize;

        for (uint32_t i = 0; i < vertex_count; ++i) {
            float position[3] = {};
            std::memcpy(position, bytes + offset, sizeof(position));
            offset += static_cast<int>(sizeof(position));
            out.add_vertex(PcgVertex{
                static_cast<double>(position[0]),
                static_cast<double>(position[1]),
                static_cast<double>(position[2]),
            });
        }

        for (uint32_t i = 0; i < index_count; ++i) {
            uint32_t index = 0;
            std::memcpy(&index, bytes + offset, sizeof(index));
            offset += static_cast<int>(sizeof(index));
            if (index >= vertex_count)
                return false;
            out.triangles_mut().push_back(static_cast<int>(index));
        }

        if (has_normals) {
            std::vector<PcgVertex> normals;
            normals.reserve(vertex_count);
            for (uint32_t i = 0; i < vertex_count; ++i) {
                float n[3] = {};
                std::memcpy(n, bytes + offset, sizeof(n));
                offset += static_cast<int>(sizeof(n));
                normals.push_back(PcgVertex{
                    static_cast<double>(n[0]),
                    static_cast<double>(n[1]),
                    static_cast<double>(n[2]),
                });
            }
            out.set_normals(std::move(normals));
        }

        return true;
    }

    return false;
}

} // namespace pcg::internal::data
