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
constexpr uint32_t kChunkColors = 7u;
constexpr uint32_t kChunkUVs = 8u;
constexpr uint32_t kChunkMaterial = 9u;
constexpr uint32_t kChunkFaceMaterials = 10u;
constexpr uint32_t kChunkCornerUVs = 11u;
constexpr uint32_t kChunkAttributes = 12u;

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
    bool write_u64(uint64_t v) { return write(&v, 8); }
    bool write_i64(int64_t v) { return write(&v, 8); }
    bool write_f64(double v) { return write(&v, 8); }
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
    bool read_u64(uint64_t& v) { return read(&v, 8); }
    bool read_i64(int64_t& v) { return read(&v, 8); }
    bool read_f64(double& v) { return read(&v, 8); }
};

int encoded_string_size(const std::string& value)
{
    return 4 + static_cast<int>(value.size());
}

int encoded_attribute_size(const AttributeArray& attribute)
{
    // owner, type, tuple size, transform role, name size, element count.
    int size = 6 * 4 + static_cast<int>(attribute.schema().name.size());
    const int tuple_size = attribute.schema().tuple_size;
    const int value_count = static_cast<int>(attribute.size()) * tuple_size;
    switch (attribute.schema().type) {
    case AttributeType::Int:
        size += tuple_size * 8 + value_count * 8;
        break;
    case AttributeType::Float:
        size += tuple_size * 8 + value_count * 8;
        break;
    case AttributeType::String:
        for (const auto& value : attribute.default_string())
            size += encoded_string_size(value);
        for (const auto& value : attribute.string_values())
            size += encoded_string_size(value);
        break;
    }
    return size;
}

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
         {geometry::GroupDomain::Point, geometry::GroupDomain::Edge,
          geometry::GroupDomain::Face, geometry::GroupDomain::Vertex}) {
        for (const std::string& name : geometry.groups().group_names(domain)) {
            ++group_count;
            member_count += static_cast<int>(geometry.groups().members(domain, name).size());
            name_bytes += static_cast<int>(name.size()) + 1;
        }
    }
    size += 8 + group_count * 8 + name_bytes;
    size += 8 + member_count * 8;

    const PcgMeshData tri = triangulate_geometry(geometry);
    size += 8 + static_cast<int>(tri.triangles().size()) * 4;

    if (geometry.has_colors())
        size += 8 + static_cast<int>(geometry.points().size()) * 16;
    if (geometry.has_uvs())
        size += 8 + static_cast<int>(geometry.points().size()) * 8;
    if (geometry.has_corner_uvs())
        size += 8 + geometry.corner_count() * 8;
    if (geometry.has_material())
        size += 8 + static_cast<int>(geometry.material_name().size()) + 1;
    if (geometry.has_face_materials()) {
        size += 8;
        for (const std::string& name : geometry.face_materials())
            size += 4 + static_cast<int>(name.size());
    }
    int attribute_count = 0;
    int attribute_bytes = 4;
    for (AttributeOwner owner : {AttributeOwner::Point, AttributeOwner::Vertex,
                                 AttributeOwner::Primitive, AttributeOwner::Detail}) {
        for (const auto& name : geometry.attributes().names(owner)) {
            const auto* attribute = geometry.attributes().find(owner, name);
            if (!attribute)
                continue;
            ++attribute_count;
            attribute_bytes += encoded_attribute_size(*attribute);
        }
    }
    if (attribute_count > 0)
        size += 8 + attribute_bytes;
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
                  geometry::GroupDomain::Face, geometry::GroupDomain::Vertex}) {
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
                  geometry::GroupDomain::Face, geometry::GroupDomain::Vertex}) {
                for (const std::string& name : geometry.groups().group_names(domain)) {
                    for (geometry::GroupId id : geometry.groups().members(domain, name)) {
                        if (!w.write_u64(static_cast<uint64_t>(id)))
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

    if (geometry.has_colors()) {
        if (!write_chunk(kChunkColors, [&] {
                for (const auto& c : geometry.colors()) {
                    const float rgba[4] = {static_cast<float>(c.r), static_cast<float>(c.g),
                                            static_cast<float>(c.b), static_cast<float>(c.a)};
                    if (!w.write(rgba, 16))
                        return false;
                }
                return true;
            }))
            return false;
    }

    if (geometry.has_uvs()) {
        if (!write_chunk(kChunkUVs, [&] {
                for (const auto& uv : geometry.uvs()) {
                    const float uv_arr[2] = {static_cast<float>(uv.u), static_cast<float>(uv.v)};
                    if (!w.write(uv_arr, 8))
                        return false;
                }
                return true;
            }))
            return false;
    }

    if (geometry.has_corner_uvs()) {
        if (!write_chunk(kChunkCornerUVs, [&] {
                for (const auto& uv : geometry.corner_uvs()) {
                    const float uv_arr[2] = {static_cast<float>(uv.u), static_cast<float>(uv.v)};
                    if (!w.write(uv_arr, 8))
                        return false;
                }
                return true;
            }))
            return false;
    }

    if (geometry.has_material()) {
        if (!write_chunk(kChunkMaterial, [&] {
                const std::string& name = geometry.material_name();
                if (!w.write(name.c_str(), static_cast<int>(name.size()) + 1))
                    return false;
                return true;
            }))
            return false;
    }

    if (geometry.has_face_materials()) {
        if (!write_chunk(kChunkFaceMaterials, [&] {
                for (const std::string& name : geometry.face_materials()) {
                    if (!w.write_u32(static_cast<uint32_t>(name.size())))
                        return false;
                    if (!name.empty() && !w.write(name.data(), static_cast<int>(name.size())))
                        return false;
                }
                return true;
            }))
            return false;
    }

    uint32_t attribute_count = 0;
    for (AttributeOwner owner : {AttributeOwner::Point, AttributeOwner::Vertex,
                                 AttributeOwner::Primitive, AttributeOwner::Detail})
        attribute_count += static_cast<uint32_t>(geometry.attributes().names(owner).size());
    if (attribute_count > 0) {
        if (!write_chunk(kChunkAttributes, [&] {
                if (!w.write_u32(attribute_count))
                    return false;
                for (AttributeOwner owner : {AttributeOwner::Point, AttributeOwner::Vertex,
                                             AttributeOwner::Primitive, AttributeOwner::Detail}) {
                    for (const auto& name : geometry.attributes().names(owner)) {
                        const auto* attribute = geometry.attributes().find(owner, name);
                        if (!attribute)
                            return false;
                        const auto& schema = attribute->schema();
                        if (!w.write_u32(static_cast<uint32_t>(schema.owner)) ||
                            !w.write_u32(static_cast<uint32_t>(schema.type)) ||
                            !w.write_u32(static_cast<uint32_t>(schema.tuple_size)) ||
                            !w.write_u32(static_cast<uint32_t>(schema.transform_role)) ||
                            !w.write_u32(static_cast<uint32_t>(schema.name.size())) ||
                            !w.write_u32(static_cast<uint32_t>(attribute->size())) ||
                            (!schema.name.empty() &&
                             !w.write(schema.name.data(), static_cast<int>(schema.name.size()))))
                            return false;

                        const auto write_string = [&](const std::string& value) {
                            return w.write_u32(static_cast<uint32_t>(value.size())) &&
                                   (value.empty() ||
                                    w.write(value.data(), static_cast<int>(value.size())));
                        };
                        switch (schema.type) {
                        case AttributeType::Int:
                            for (int64_t value : attribute->default_int())
                                if (!w.write_i64(value)) return false;
                            for (int64_t value : attribute->int_values())
                                if (!w.write_i64(value)) return false;
                            break;
                        case AttributeType::Float:
                            for (double value : attribute->default_float())
                                if (!w.write_f64(value)) return false;
                            for (double value : attribute->float_values())
                                if (!w.write_f64(value)) return false;
                            break;
                        case AttributeType::String:
                            for (const auto& value : attribute->default_string())
                                if (!write_string(value)) return false;
                            for (const auto& value : attribute->string_values())
                                if (!write_string(value)) return false;
                            break;
                        }
                    }
                }
                return true;
            }))
            return false;
    }

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
    if (magic != kPcgGeometryBinaryMagic ||
        (version != kPcgGeometryBinaryVersion &&
         version != kPcgGeometryBinaryPreviousVersion))
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
    std::vector<geometry::GroupId> group_members;
    std::vector<std::string> face_materials;
    std::vector<PcgVec2> deferred_corner_uvs;

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
            const int stride = version >= 3 ? 8 : 4;
            if (chunk_size % static_cast<uint32_t>(stride) != 0)
                return false;
            const int member_count = chunk_size / stride;
            group_members.resize(static_cast<size_t>(member_count));
            for (int i = 0; i < member_count; ++i) {
                if (version >= 3) {
                    uint64_t value = 0;
                    if (!r.read_u64(value))
                        return false;
                    group_members[static_cast<size_t>(i)] =
                        static_cast<geometry::GroupId>(value);
                } else {
                    uint32_t value = 0;
                    if (!r.read_u32(value))
                        return false;
                    group_members[static_cast<size_t>(i)] =
                        static_cast<geometry::GroupId>(value);
                }
            }
        } else if (chunk_id == kChunkColors) {
            std::vector<PcgColor> colors;
            const int color_count = chunk_size / 16;
            colors.reserve(static_cast<size_t>(color_count));
            for (int i = 0; i < color_count; ++i) {
                float rgba[4];
                if (!r.read(rgba, 16))
                    return false;
                colors.push_back(PcgColor{rgba[0], rgba[1], rgba[2], rgba[3]});
            }
            geometry.set_colors(std::move(colors));
        } else if (chunk_id == kChunkUVs) {
            std::vector<PcgVec2> uvs;
            const int uv_count = chunk_size / 8;
            uvs.reserve(static_cast<size_t>(uv_count));
            for (int i = 0; i < uv_count; ++i) {
                float uv_arr[2];
                if (!r.read(uv_arr, 8))
                    return false;
                uvs.push_back(PcgVec2{uv_arr[0], uv_arr[1]});
            }
            geometry.set_uvs(std::move(uvs));
        } else if (chunk_id == kChunkCornerUVs) {
            deferred_corner_uvs.clear();
            const int uv_count = chunk_size / 8;
            deferred_corner_uvs.reserve(static_cast<size_t>(uv_count));
            for (int i = 0; i < uv_count; ++i) {
                float uv_arr[2];
                if (!r.read(uv_arr, 8))
                    return false;
                deferred_corner_uvs.push_back(PcgVec2{uv_arr[0], uv_arr[1]});
            }
        } else if (chunk_id == kChunkMaterial) {
            std::string name;
            while (r.offset < chunk_end) {
                char ch = 0;
                if (!r.read(&ch, 1))
                    return false;
                if (ch == '\0')
                    break;
                name.push_back(ch);
            }
            geometry.set_material_name(std::move(name));
        } else if (chunk_id == kChunkFaceMaterials) {
            face_materials.clear();
            face_materials.reserve(face_count);
            for (uint32_t i = 0; i < face_count; ++i) {
                uint32_t name_size = 0;
                if (!r.read_u32(name_size) ||
                    name_size > static_cast<uint32_t>(chunk_end - r.offset))
                    return false;
                face_materials.emplace_back(reinterpret_cast<const char*>(r.base + r.offset),
                                            name_size);
                r.offset += static_cast<int>(name_size);
            }
            if (r.offset != chunk_end)
                return false;
        } else if (chunk_id == kChunkAttributes) {
            uint32_t attribute_count = 0;
            if (!r.read_u32(attribute_count))
                return false;
            for (uint32_t attribute_index = 0; attribute_index < attribute_count;
                 ++attribute_index) {
                uint32_t owner_value = 0;
                uint32_t type_value = 0;
                uint32_t tuple_size = 0;
                uint32_t role_value = 0;
                uint32_t name_size = 0;
                uint32_t element_count = 0;
                if (!r.read_u32(owner_value) || !r.read_u32(type_value) ||
                    !r.read_u32(tuple_size) || !r.read_u32(role_value) ||
                    !r.read_u32(name_size) || !r.read_u32(element_count) ||
                    owner_value > static_cast<uint32_t>(AttributeOwner::Detail) ||
                    type_value > static_cast<uint32_t>(AttributeType::String) ||
                    role_value > static_cast<uint32_t>(AttributeTransformRole::Matrix) ||
                    tuple_size == 0 || tuple_size > 16 ||
                    name_size > static_cast<uint32_t>(chunk_end - r.offset))
                    return false;

                std::string name(reinterpret_cast<const char*>(r.base + r.offset), name_size);
                r.offset += static_cast<int>(name_size);
                const auto owner = static_cast<AttributeOwner>(owner_value);
                const auto type = static_cast<AttributeType>(type_value);
                const auto role = static_cast<AttributeTransformRole>(role_value);
                const uint64_t value_count = static_cast<uint64_t>(tuple_size) * element_count;
                if (value_count > static_cast<uint64_t>(chunk_end - r.offset))
                    return false;

                const auto read_string = [&](std::string& value) {
                    uint32_t size = 0;
                    if (!r.read_u32(size) || size > static_cast<uint32_t>(chunk_end - r.offset))
                        return false;
                    value.assign(reinterpret_cast<const char*>(r.base + r.offset), size);
                    r.offset += static_cast<int>(size);
                    return true;
                };
                if (type == AttributeType::Int) {
                    std::vector<int64_t> defaults(tuple_size);
                    for (auto& value : defaults)
                        if (!r.read_i64(value)) return false;
                    auto& attribute = geometry.attributes().create_int(
                        owner, name, static_cast<int>(tuple_size), std::move(defaults), role);
                    attribute.int_values_mut().resize(static_cast<size_t>(value_count));
                    for (auto& value : attribute.int_values_mut())
                        if (!r.read_i64(value)) return false;
                } else if (type == AttributeType::Float) {
                    std::vector<double> defaults(tuple_size);
                    for (auto& value : defaults)
                        if (!r.read_f64(value)) return false;
                    auto& attribute = geometry.attributes().create_float(
                        owner, name, static_cast<int>(tuple_size), std::move(defaults), role);
                    attribute.float_values_mut().resize(static_cast<size_t>(value_count));
                    for (auto& value : attribute.float_values_mut())
                        if (!r.read_f64(value)) return false;
                } else {
                    std::vector<std::string> defaults(tuple_size);
                    for (auto& value : defaults)
                        if (!read_string(value)) return false;
                    auto& attribute = geometry.attributes().create_string(
                        owner, name, static_cast<int>(tuple_size), std::move(defaults));
                    attribute.string_values_mut().resize(static_cast<size_t>(value_count));
                    for (auto& value : attribute.string_values_mut())
                        if (!read_string(value)) return false;
                }
            }
            if (r.offset != chunk_end)
                return false;
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

    if (!face_materials.empty())
        geometry.set_face_materials(std::move(face_materials));

    if (!deferred_corner_uvs.empty())
        geometry.set_corner_uvs(std::move(deferred_corner_uvs));

    size_t member_cursor = 0;
    for (const auto& def : group_defs) {
        for (uint32_t i = 0; i < def.count; ++i) {
            if (member_cursor >= group_members.size())
                return false;
            geometry.groups().add(def.domain, def.name,
                                  group_members[member_cursor++]);
        }
    }

    if (member_cursor != group_members.size() ||
        !geometry.validate_attributes())
        return false;

    out = std::move(geometry);
    return true;
}

} // namespace pcg::internal::data
