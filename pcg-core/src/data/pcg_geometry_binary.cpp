#include "data/pcg_geometry_binary.hpp"

#include "geometry/group_table.hpp"

#include <cstring>
#include <string>
#include <vector>

namespace pcg::internal::data {
namespace {

constexpr uint32_t kChunkPoints = 1u;
constexpr uint32_t kChunkFaceOffsets = 2u;
constexpr uint32_t kChunkFaceIndices = 3u;
constexpr uint32_t kChunkGroupDefs = 4u;
constexpr uint32_t kChunkGroupMembers = 5u;
constexpr uint32_t kChunkTriangulation = 6u;

struct Writer {
    uint8_t* base = nullptr;
    int capacity = 0;
    int offset = 0;

    bool write(const void* data, int size)
    {
        if (offset + size > capacity)
            return false;
        std::memcpy(base + offset, data, size);
        offset += size;
        return true;
    }

    bool write_u32(uint32_t v) { return write(&v, 4); }
};

struct Reader {
    const uint8_t* base = nullptr;
    int capacity = 0;
    int offset = 0;

    bool read(void* data, int size)
    {
        if (offset + size > capacity)
            return false;
        std::memcpy(data, base + offset, size);
        offset += size;
        return true;
    }

    bool read_u32(uint32_t& v) { return read(&v, 4); }
};

} // namespace

int geometry_binary_size(const PcgGeometry& geometry)
{
    int size = kPcgGeometryBinaryHeaderSize;
    size += 8 + static_cast<int>(geometry.points().size()) * 12;

    int total_indices = 0;
    for (const auto& face : geometry.faces())
        total_indices += static_cast<int>(face.size());
    size += 8 + static_cast<int>(geometry.faces().size()) * 4;
    size += 8 + total_indices * 4;

    int group_count = 0;
    int member_count = 0;
    int name_bytes = 0;
    for (geometry::GroupDomain domain :
         {geometry::GroupDomain::Point, geometry::GroupDomain::Edge, geometry::GroupDomain::Face}) {
        for (const std::string& name : geometry.groups().group_names(domain)) {
            ++group_count;
            member_count += static_cast<int>(geometry.groups().members(domain, name).size());
            name_bytes += static_cast<int>(name.size()) + 1;
        }
    }
    size += 8 + group_count * 8 + name_bytes;
    size += 8 + member_count * 4;

    const PcgMeshData tri = triangulate_geometry(geometry);
    size += 8 + static_cast<int>(tri.triangles().size()) * 4;
    return size;
}

bool write_geometry_binary(const PcgGeometry& geometry, void* buffer, int buffer_size)
{
    if (!buffer || buffer_size < kPcgGeometryBinaryHeaderSize)
        return false;

    Writer w{static_cast<uint8_t*>(buffer), buffer_size, 0};
    if (!w.write_u32(kPcgGeometryBinaryMagic))
        return false;
    if (!w.write_u32(kPcgGeometryBinaryVersion))
        return false;
    if (!w.write_u32(static_cast<uint32_t>(geometry.points().size())))
        return false;
    if (!w.write_u32(static_cast<uint32_t>(geometry.faces().size())))
        return false;

    const auto write_chunk = [&](uint32_t id, const auto& payload_fn) {
        const int start = w.offset;
        if (!w.write_u32(id))
            return false;
        const int size_pos = w.offset;
        if (!w.write_u32(0))
            return false;
        if (!payload_fn())
            return false;
        const uint32_t chunk_size = static_cast<uint32_t>(w.offset - start - 8);
        std::memcpy(w.base + size_pos, &chunk_size, 4);
        return true;
    };

    if (!write_chunk(kChunkPoints, [&] {
            for (const auto& p : geometry.points()) {
                const float xyz[3] = {static_cast<float>(p.x), static_cast<float>(p.y),
                                      static_cast<float>(p.z)};
                if (!w.write(xyz, 12))
                    return false;
            }
            return true;
        }))
        return false;

    if (!write_chunk(kChunkFaceOffsets, [&] {
            uint32_t cursor = 0;
            for (const auto& face : geometry.faces()) {
                if (!w.write_u32(cursor))
                    return false;
                cursor += static_cast<uint32_t>(face.size());
            }
            return true;
        }))
        return false;

    if (!write_chunk(kChunkFaceIndices, [&] {
            for (const auto& face : geometry.faces()) {
                for (int idx : face) {
                    const uint32_t v = static_cast<uint32_t>(idx);
                    if (!w.write_u32(v))
                        return false;
                }
            }
            return true;
        }))
        return false;

    if (!write_chunk(kChunkGroupDefs, [&] {
            for (geometry::GroupDomain domain :
                 {geometry::GroupDomain::Point, geometry::GroupDomain::Edge,
                  geometry::GroupDomain::Face}) {
                for (const std::string& name : geometry.groups().group_names(domain)) {
                    const uint32_t domain_u = static_cast<uint32_t>(domain);
                    const uint32_t count = static_cast<uint32_t>(
                        geometry.groups().members(domain, name).size());
                    if (!w.write_u32(domain_u) || !w.write_u32(count))
                        return false;
                    if (!w.write(name.c_str(), static_cast<int>(name.size()) + 1))
                        return false;
                }
            }
            return true;
        }))
        return false;

    if (!write_chunk(kChunkGroupMembers, [&] {
            for (geometry::GroupDomain domain :
                 {geometry::GroupDomain::Point, geometry::GroupDomain::Edge,
                  geometry::GroupDomain::Face}) {
                for (const std::string& name : geometry.groups().group_names(domain)) {
                    for (int id : geometry.groups().members(domain, name)) {
                        const uint32_t v = static_cast<uint32_t>(id);
                        if (!w.write_u32(v))
                            return false;
                    }
                }
            }
            return true;
        }))
        return false;

    const PcgMeshData tri = triangulate_geometry(geometry);
    if (!write_chunk(kChunkTriangulation, [&] {
            for (int idx : tri.triangles()) {
                const uint32_t v = static_cast<uint32_t>(idx);
                if (!w.write_u32(v))
                    return false;
            }
            return true;
        }))
        return false;

    return true;
}

bool read_geometry_binary(const void* buffer, int buffer_size, PcgGeometry& out)
{
    if (!buffer || buffer_size < kPcgGeometryBinaryHeaderSize)
        return false;

    Reader r{static_cast<const uint8_t*>(buffer), buffer_size, 0};
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t point_count = 0;
    uint32_t face_count = 0;
    if (!r.read_u32(magic) || !r.read_u32(version) || !r.read_u32(point_count) ||
        !r.read_u32(face_count))
        return false;
    if (magic != kPcgGeometryBinaryMagic || version != kPcgGeometryBinaryVersion)
        return false;

    PcgGeometry geometry;
    std::vector<uint32_t> face_offsets;
    std::vector<uint32_t> face_indices;
    struct GroupDef {
        geometry::GroupDomain domain;
        uint32_t count;
        std::string name;
    };
    std::vector<GroupDef> group_defs;
    std::vector<uint32_t> group_members;

    while (r.offset + 8 <= r.capacity) {
        uint32_t chunk_id = 0;
        uint32_t chunk_size = 0;
        if (!r.read_u32(chunk_id) || !r.read_u32(chunk_size))
            break;
        const int chunk_end = r.offset + static_cast<int>(chunk_size);
        if (chunk_end > r.capacity)
            return false;

        if (chunk_id == kChunkPoints) {
            geometry.points_mut().resize(point_count);
            for (uint32_t i = 0; i < point_count; ++i) {
                float xyz[3];
                if (!r.read(xyz, 12))
                    return false;
                geometry.points_mut()[i] = {xyz[0], xyz[1], xyz[2]};
            }
        } else if (chunk_id == kChunkFaceOffsets) {
            face_offsets.resize(face_count);
            for (uint32_t i = 0; i < face_count; ++i) {
                if (!r.read_u32(face_offsets[i]))
                    return false;
            }
        } else if (chunk_id == kChunkFaceIndices) {
            const int index_count = chunk_size / 4;
            face_indices.resize(static_cast<size_t>(index_count));
            for (int i = 0; i < index_count; ++i) {
                if (!r.read_u32(face_indices[static_cast<size_t>(i)]))
                    return false;
            }
        } else if (chunk_id == kChunkGroupDefs) {
            while (r.offset < chunk_end) {
                GroupDef def;
                uint32_t domain_u = 0;
                if (!r.read_u32(domain_u) || !r.read_u32(def.count))
                    return false;
                def.domain = static_cast<geometry::GroupDomain>(domain_u);
                std::string name;
                while (r.offset < chunk_end) {
                    char ch = 0;
                    if (!r.read(&ch, 1))
                        return false;
                    if (ch == '\0')
                        break;
                    name.push_back(ch);
                }
                def.name = std::move(name);
                group_defs.push_back(std::move(def));
            }
        } else if (chunk_id == kChunkGroupMembers) {
            const int member_count = chunk_size / 4;
            group_members.resize(static_cast<size_t>(member_count));
            for (int i = 0; i < member_count; ++i) {
                if (!r.read_u32(group_members[static_cast<size_t>(i)]))
                    return false;
            }
        } else {
            r.offset = chunk_end;
        }

        r.offset = chunk_end;
    }

    geometry.faces_mut().resize(face_count);
    for (uint32_t fi = 0; fi < face_count; ++fi) {
        const uint32_t start = face_offsets[fi];
        const uint32_t end = fi + 1 < face_count ? face_offsets[fi + 1] : static_cast<uint32_t>(face_indices.size());
        auto& face = geometry.faces_mut()[fi];
        for (uint32_t i = start; i < end; ++i)
            face.push_back(static_cast<int>(face_indices[i]));
    }

    size_t member_cursor = 0;
    for (const auto& def : group_defs) {
        for (uint32_t i = 0; i < def.count; ++i) {
            if (member_cursor >= group_members.size())
                return false;
            geometry.groups().add(def.domain, def.name,
                                  static_cast<int>(group_members[member_cursor++]));
        }
    }

    out = std::move(geometry);
    return true;
}

} // namespace pcg::internal::data
