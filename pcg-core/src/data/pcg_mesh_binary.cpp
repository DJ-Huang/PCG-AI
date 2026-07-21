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
    const int header_size = mesh.has_materials() ? kPcgMeshBinaryV3HeaderSize
                                                 : kPcgMeshBinaryV2HeaderSize;
    int size = header_size + vertex_count * 3 * static_cast<int>(sizeof(float)) +
               index_count * static_cast<int>(sizeof(uint32_t));
    if (mesh.has_normals())
        size += vertex_count * 3 * static_cast<int>(sizeof(float));
    if (mesh.has_colors())
        size += vertex_count * 4 * static_cast<int>(sizeof(float));
    if (mesh.has_uvs())
        size += vertex_count * 2 * static_cast<int>(sizeof(float));
    if (mesh.has_materials()) {
        size += 4;
        for (const std::string& name : mesh.material_slots())
            size += 4 + static_cast<int>(name.size());
        size += static_cast<int>(mesh.triangle_materials().size()) * 4;
    }
    return size;
}

bool write_mesh_binary(const PcgMeshData& mesh, void* buffer, int buffer_size)
{
    const bool write_materials = mesh.has_materials();
    const int header_size = write_materials ? kPcgMeshBinaryV3HeaderSize
                                            : kPcgMeshBinaryV2HeaderSize;
    if (!buffer || buffer_size < header_size)
        return false;

    const int vertex_count = static_cast<int>(mesh.vertices().size());
    const int index_count = static_cast<int>(mesh.triangles().size());
    const int required = mesh_binary_size(mesh);
    if (buffer_size < required)
        return false;

    const uint32_t flags = mesh.has_normals() ? kPcgMeshBinaryFlagHasNormals : 0u;
    const uint32_t flags2 = flags | (mesh.has_colors() ? kPcgMeshBinaryFlagHasColors : 0u) |
                            (mesh.has_uvs() ? kPcgMeshBinaryFlagHasUVs : 0u) |
                            (write_materials ? kPcgMeshBinaryFlagHasMaterials : 0u);

    uint32_t material_section_size = 0;
    if (write_materials) {
        material_section_size = 4u + static_cast<uint32_t>(mesh.triangle_materials().size() * 4u);
        for (const std::string& name : mesh.material_slots())
            material_section_size += 4u + static_cast<uint32_t>(name.size());
    }

    auto* bytes = static_cast<uint8_t*>(buffer);
    write_u32(bytes + 0, kPcgMeshBinaryMagic);
    write_u32(bytes + 4, write_materials ? kPcgMeshBinaryVersion : 2u);
    write_u32(bytes + 8, static_cast<uint32_t>(vertex_count));
    write_u32(bytes + 12, static_cast<uint32_t>(index_count));
    write_u32(bytes + 16, flags2);
    if (write_materials)
        write_u32(bytes + 20, material_section_size);

    int offset = header_size;
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

    if (mesh.has_colors()) {
        for (const auto& color : mesh.colors()) {
            const float c[4] = {
                static_cast<float>(color.r),
                static_cast<float>(color.g),
                static_cast<float>(color.b),
                static_cast<float>(color.a),
            };
            std::memcpy(bytes + offset, c, sizeof(c));
            offset += static_cast<int>(sizeof(c));
        }
    }

    if (mesh.has_uvs()) {
        for (const auto& uv : mesh.uvs()) {
            const float u[2] = {
                static_cast<float>(uv.u),
                static_cast<float>(uv.v),
            };
            std::memcpy(bytes + offset, u, sizeof(u));
            offset += static_cast<int>(sizeof(u));
        }
    }

    if (write_materials) {
        write_u32(bytes + offset, static_cast<uint32_t>(mesh.material_slots().size()));
        offset += 4;
        for (const std::string& name : mesh.material_slots()) {
            write_u32(bytes + offset, static_cast<uint32_t>(name.size()));
            offset += 4;
            if (!name.empty()) {
                std::memcpy(bytes + offset, name.data(), name.size());
                offset += static_cast<int>(name.size());
            }
        }
        for (uint32_t slot : mesh.triangle_materials()) {
            write_u32(bytes + offset, slot);
            offset += 4;
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

    if (version == 2u || version == 3u) {
        const int header_size = version == 3u ? kPcgMeshBinaryV3HeaderSize : kPcgMeshBinaryV2HeaderSize;
        if (buffer_size < header_size)
            return false;

        uint32_t flags = 0;
        if (!read_u32(bytes + 16, buffer_size - 16, flags))
            return false;

        const bool has_normals = (flags & kPcgMeshBinaryFlagHasNormals) != 0u;
        const bool has_colors  = (flags & kPcgMeshBinaryFlagHasColors)  != 0u;
        const bool has_uvs     = (flags & kPcgMeshBinaryFlagHasUVs)     != 0u;
        const bool has_materials = version == 3u &&
                                   (flags & kPcgMeshBinaryFlagHasMaterials) != 0u;
        uint32_t material_section_size = 0;
        if (version == 3u && !read_u32(bytes + 20, buffer_size - 20, material_section_size))
            return false;

        const int vc = static_cast<int>(vertex_count);
        const int required = header_size +
                              vc * 3 * static_cast<int>(sizeof(float)) +
                              static_cast<int>(index_count) * static_cast<int>(sizeof(uint32_t)) +
                              (has_normals ? vc * 3 * static_cast<int>(sizeof(float)) : 0) +
                              (has_colors  ? vc * 4 * static_cast<int>(sizeof(float)) : 0) +
                              (has_uvs     ? vc * 2 * static_cast<int>(sizeof(float)) : 0) +
                              static_cast<int>(material_section_size);
        if (buffer_size < required)
            return false;

        if (index_count % 3 != 0)
            return false;

        out = PcgMeshData{};
        int offset = header_size;

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

        if (has_colors) {
            std::vector<PcgColor> colors;
            colors.reserve(vertex_count);
            for (uint32_t i = 0; i < vertex_count; ++i) {
                float c[4] = {};
                std::memcpy(c, bytes + offset, sizeof(c));
                offset += static_cast<int>(sizeof(c));
                colors.push_back(PcgColor{
                    static_cast<double>(c[0]),
                    static_cast<double>(c[1]),
                    static_cast<double>(c[2]),
                    static_cast<double>(c[3]),
                });
            }
            out.set_colors(std::move(colors));
        }

        if (has_uvs) {
            std::vector<PcgVec2> uvs;
            uvs.reserve(vertex_count);
            for (uint32_t i = 0; i < vertex_count; ++i) {
                float uv[2] = {};
                std::memcpy(uv, bytes + offset, sizeof(uv));
                offset += static_cast<int>(sizeof(uv));
                uvs.push_back(PcgVec2{
                    static_cast<double>(uv[0]),
                    static_cast<double>(uv[1]),
                });
            }
            out.set_uvs(std::move(uvs));
        }

        if (has_materials) {
            const int section_end = offset + static_cast<int>(material_section_size);
            uint32_t slot_count = 0;
            if (section_end > buffer_size || !read_u32(bytes + offset, section_end - offset, slot_count))
                return false;
            offset += 4;
            std::vector<std::string> slots;
            slots.reserve(slot_count);
            for (uint32_t i = 0; i < slot_count; ++i) {
                uint32_t name_size = 0;
                if (!read_u32(bytes + offset, section_end - offset, name_size))
                    return false;
                offset += 4;
                if (name_size > static_cast<uint32_t>(section_end - offset))
                    return false;
                slots.emplace_back(reinterpret_cast<const char*>(bytes + offset), name_size);
                offset += static_cast<int>(name_size);
            }
            std::vector<uint32_t> triangle_materials;
            triangle_materials.reserve(index_count / 3u);
            for (uint32_t i = 0; i < index_count / 3u; ++i) {
                uint32_t slot = 0;
                if (!read_u32(bytes + offset, section_end - offset, slot) || slot >= slot_count)
                    return false;
                offset += 4;
                triangle_materials.push_back(slot);
            }
            if (offset != section_end)
                return false;
            out.set_materials(std::move(slots), std::move(triangle_materials));
            if (!out.has_materials())
                return false;
        } else if (material_section_size != 0u) {
            return false;
        }

        return true;
    }

    return false;
}

int multi_spawn_binary_size(const std::vector<PcgMeshData>& meshes,
                            const std::vector<int>& point_counts)
{
    if (meshes.empty() || meshes.size() != point_counts.size())
        return 0;

    // magic|version|count + point_counts[N] + mesh_sizes[N] + binaries
    int size = 12 + static_cast<int>(meshes.size()) * 8;
    for (const auto& mesh : meshes)
        size += mesh_binary_size(mesh);
    return size;
}

bool write_multi_spawn_binary(const std::vector<PcgMeshData>& meshes,
                              const std::vector<int>& point_counts,
                              void* buffer,
                              int buffer_size)
{
    const int required = multi_spawn_binary_size(meshes, point_counts);
    if (required <= 0 || buffer == nullptr || buffer_size < required)
        return false;

    auto* bytes = static_cast<uint8_t*>(buffer);
    write_u32(bytes + 0, kPcgMultiSpawnMagic);
    write_u32(bytes + 4, kPcgMultiSpawnVersion);
    write_u32(bytes + 8, static_cast<uint32_t>(meshes.size()));
    int offset = 12;
    for (int count : point_counts) {
        write_u32(bytes + offset, static_cast<uint32_t>(count < 0 ? 0 : count));
        offset += 4;
    }

    std::vector<int> mesh_sizes;
    mesh_sizes.reserve(meshes.size());
    for (const auto& mesh : meshes) {
        const int mesh_size = mesh_binary_size(mesh);
        mesh_sizes.push_back(mesh_size);
        write_u32(bytes + offset, static_cast<uint32_t>(mesh_size));
        offset += 4;
    }

    for (size_t i = 0; i < meshes.size(); ++i) {
        if (!write_mesh_binary(meshes[i], bytes + offset, mesh_sizes[i]))
            return false;
        offset += mesh_sizes[i];
    }
    return offset == required;
}

bool read_multi_spawn_binary(const void* buffer,
                             int buffer_size,
                             std::vector<PcgMeshData>& out_meshes,
                             std::vector<int>& out_point_counts)
{
    out_meshes.clear();
    out_point_counts.clear();
    if (buffer == nullptr || buffer_size < 12)
        return false;

    const auto* bytes = static_cast<const uint8_t*>(buffer);
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t count = 0;
    if (!read_u32(bytes + 0, buffer_size, magic) || magic != kPcgMultiSpawnMagic)
        return false;
    if (!read_u32(bytes + 4, buffer_size - 4, version) || version != kPcgMultiSpawnVersion)
        return false;
    if (!read_u32(bytes + 8, buffer_size - 8, count) || count == 0)
        return false;

    const int header = 12 + static_cast<int>(count) * 8;
    if (buffer_size < header)
        return false;

    out_point_counts.resize(count);
    out_meshes.resize(count);
    int offset = 12;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t pc = 0;
        if (!read_u32(bytes + offset, buffer_size - offset, pc))
            return false;
        out_point_counts[i] = static_cast<int>(pc);
        offset += 4;
    }

    std::vector<uint32_t> mesh_sizes(count);
    for (uint32_t i = 0; i < count; ++i) {
        if (!read_u32(bytes + offset, buffer_size - offset, mesh_sizes[i]))
            return false;
        offset += 4;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (offset + static_cast<int>(mesh_sizes[i]) > buffer_size)
            return false;
        if (!read_mesh_binary(bytes + offset, static_cast<int>(mesh_sizes[i]), out_meshes[i]))
            return false;
        offset += static_cast<int>(mesh_sizes[i]);
    }
    return true;
}

} // namespace pcg::internal::data
