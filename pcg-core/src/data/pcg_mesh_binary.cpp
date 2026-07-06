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
    return kPcgMeshBinaryHeaderSize + vertex_count * 3 * static_cast<int>(sizeof(float)) +
           index_count * static_cast<int>(sizeof(uint32_t));
}

bool write_mesh_binary(const PcgMeshData& mesh, void* buffer, int buffer_size)
{
    if (!buffer || buffer_size < kPcgMeshBinaryHeaderSize)
        return false;

    const int vertex_count = static_cast<int>(mesh.vertices().size());
    const int index_count = static_cast<int>(mesh.triangles().size());
    const int required = mesh_binary_size(mesh);
    if (buffer_size < required)
        return false;

    auto* bytes = static_cast<uint8_t*>(buffer);
    write_u32(bytes + 0, kPcgMeshBinaryMagic);
    write_u32(bytes + 4, kPcgMeshBinaryVersion);
    write_u32(bytes + 8, static_cast<uint32_t>(vertex_count));
    write_u32(bytes + 12, static_cast<uint32_t>(index_count));

    int offset = kPcgMeshBinaryHeaderSize;
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

    return true;
}

bool read_mesh_binary(const void* buffer, int buffer_size, PcgMeshData& out)
{
    if (!buffer || buffer_size < kPcgMeshBinaryHeaderSize)
        return false;

    const auto* bytes = static_cast<const uint8_t*>(buffer);
    int remaining = buffer_size;

    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t vertex_count = 0;
    uint32_t index_count = 0;
    if (!read_u32(bytes + 0, remaining, magic) || magic != kPcgMeshBinaryMagic)
        return false;
    if (!read_u32(bytes + 4, remaining, version) || version != kPcgMeshBinaryVersion)
        return false;
    if (!read_u32(bytes + 8, remaining, vertex_count))
        return false;
    if (!read_u32(bytes + 12, remaining, index_count))
        return false;

    const int required = kPcgMeshBinaryHeaderSize +
                         static_cast<int>(vertex_count) * 3 * static_cast<int>(sizeof(float)) +
                         static_cast<int>(index_count) * static_cast<int>(sizeof(uint32_t));
    if (buffer_size < required)
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
        out.triangles_mut().push_back(static_cast<int>(index));
    }

    return true;
}

} // namespace pcg::internal::data
