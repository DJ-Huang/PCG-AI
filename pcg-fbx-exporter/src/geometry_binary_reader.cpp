#include "geometry_binary_reader.hpp"

#include <cstring>
#include <limits>

namespace pcg::fbx {
namespace {

constexpr uint32_t kMagic = 0x47475043u;
constexpr uint32_t kVersion = 2u;
constexpr uint32_t kPoints = 1u;
constexpr uint32_t kFaceOffsets = 2u;
constexpr uint32_t kFaceIndices = 3u;
constexpr uint32_t kColors = 7u;
constexpr uint32_t kUvs = 8u;
constexpr uint32_t kMaterial = 9u;
constexpr uint32_t kFaceMaterials = 10u;
constexpr uint32_t kCornerUvs = 11u;

class Reader {
public:
    Reader(const void* data, int size)
        : m_Data(static_cast<const uint8_t*>(data)), m_Size(size)
    {
    }

    int remaining() const { return m_Size - m_Offset; }
    int offset() const { return m_Offset; }

    bool read(void* destination, int bytes)
    {
        if (bytes < 0 || bytes > remaining())
            return false;
        std::memcpy(destination, m_Data + m_Offset, static_cast<size_t>(bytes));
        m_Offset += bytes;
        return true;
    }

    bool read_u32(uint32_t& value) { return read(&value, 4); }

    bool skip_to(int absolute_offset)
    {
        if (absolute_offset < m_Offset || absolute_offset > m_Size)
            return false;
        m_Offset = absolute_offset;
        return true;
    }

private:
    const uint8_t* m_Data;
    int m_Size;
    int m_Offset = 0;
};

bool checked_count(uint32_t count, uint32_t stride, int remaining)
{
    return count <= static_cast<uint32_t>(std::numeric_limits<int>::max()) / stride &&
           static_cast<uint64_t>(count) * stride <= static_cast<uint64_t>(remaining);
}

} // namespace

bool read_geometry_binary(const void* data, int size, Geometry& geometry, std::string& error)
{
    geometry = {};
    error.clear();
    if (!data || size < 16) {
        error = "Geometry binary is empty or shorter than its header.";
        return false;
    }

    Reader reader(data, size);
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t point_count = 0;
    uint32_t face_count = 0;
    if (!reader.read_u32(magic) || !reader.read_u32(version) ||
        !reader.read_u32(point_count) || !reader.read_u32(face_count)) {
        error = "Geometry binary header is truncated.";
        return false;
    }
    if (magic != kMagic || version != kVersion) {
        error = "Unsupported geometry binary magic or version (expected PCGG v2).";
        return false;
    }

    std::vector<uint32_t> offsets;
    std::vector<uint32_t> indices;
    while (reader.remaining() >= 8) {
        uint32_t chunk_id = 0;
        uint32_t chunk_size = 0;
        if (!reader.read_u32(chunk_id) || !reader.read_u32(chunk_size) ||
            chunk_size > static_cast<uint32_t>(reader.remaining())) {
            error = "Geometry binary contains a truncated chunk.";
            return false;
        }
        const int chunk_end = reader.offset() + static_cast<int>(chunk_size);

        if (chunk_id == kPoints) {
            if (!checked_count(point_count, 12, reader.remaining())) {
                error = "Point chunk size does not match the header.";
                return false;
            }
            geometry.points.resize(point_count);
            for (auto& point : geometry.points) {
                if (!reader.read(point.data(), 12))
                    return false;
            }
        } else if (chunk_id == kFaceOffsets) {
            if (!checked_count(face_count, 4, reader.remaining())) {
                error = "Face offset chunk size does not match the header.";
                return false;
            }
            offsets.resize(face_count);
            for (auto& offset : offsets) {
                if (!reader.read_u32(offset))
                    return false;
            }
        } else if (chunk_id == kFaceIndices) {
            if (chunk_size % 4u != 0u) {
                error = "Face index chunk is not uint32 aligned.";
                return false;
            }
            indices.resize(chunk_size / 4u);
            for (auto& index : indices) {
                if (!reader.read_u32(index))
                    return false;
            }
        } else if (chunk_id == kColors) {
            if (!checked_count(point_count, 16, reader.remaining())) {
                error = "Color chunk size does not match the point count.";
                return false;
            }
            geometry.colors.resize(point_count);
            for (auto& color : geometry.colors) {
                if (!reader.read(color.data(), 16))
                    return false;
            }
        } else if (chunk_id == kUvs) {
            if (!checked_count(point_count, 8, reader.remaining())) {
                error = "UV chunk size does not match the point count.";
                return false;
            }
            geometry.uvs.resize(point_count);
            for (auto& uv : geometry.uvs) {
                if (!reader.read(uv.data(), 8))
                    return false;
            }
        } else if (chunk_id == kCornerUvs) {
            if (chunk_size % 8u != 0u) {
                error = "Corner UV chunk is not float2 aligned.";
                return false;
            }
            geometry.corner_uvs.resize(chunk_size / 8u);
            for (auto& uv : geometry.corner_uvs) {
                if (!reader.read(uv.data(), 8))
                    return false;
            }
        } else if (chunk_id == kMaterial) {
            std::vector<char> text(chunk_size);
            if (!text.empty() && !reader.read(text.data(), static_cast<int>(text.size())))
                return false;
            if (!text.empty() && text.back() == '\0')
                text.pop_back();
            geometry.material.assign(text.begin(), text.end());
        } else if (chunk_id == kFaceMaterials) {
            geometry.face_materials.clear();
            geometry.face_materials.reserve(face_count);
            for (uint32_t i = 0; i < face_count && reader.offset() < chunk_end; ++i) {
                uint32_t length = 0;
                if (!reader.read_u32(length) || length > static_cast<uint32_t>(chunk_end - reader.offset())) {
                    error = "Face material chunk is malformed.";
                    return false;
                }
                std::string name(length, '\0');
                if (length > 0 && !reader.read(name.data(), static_cast<int>(length)))
                    return false;
                geometry.face_materials.push_back(std::move(name));
            }
        }

        if (!reader.skip_to(chunk_end)) {
            error = "Geometry binary chunk reader overran its boundary.";
            return false;
        }
    }

    if (geometry.points.size() != point_count || offsets.size() != face_count) {
        error = "Geometry binary is missing points or face offsets.";
        return false;
    }
    geometry.faces.resize(face_count);
    for (uint32_t face = 0; face < face_count; ++face) {
        const uint32_t begin = offsets[face];
        const uint32_t end = face + 1u < face_count ? offsets[face + 1u]
                                                    : static_cast<uint32_t>(indices.size());
        if (begin > end || end > indices.size() || end - begin < 3u) {
            error = "Geometry contains an invalid or degenerate polygon.";
            return false;
        }
        auto& polygon = geometry.faces[face];
        polygon.assign(indices.begin() + begin, indices.begin() + end);
        for (uint32_t index : polygon) {
            if (index >= point_count) {
                error = "Geometry face references an out-of-range point.";
                return false;
            }
        }
    }
    if (!geometry.face_materials.empty() && geometry.face_materials.size() != face_count) {
        error = "Face material count does not match face count.";
        return false;
    }
    if (!geometry.corner_uvs.empty()) {
        size_t corner_count = 0;
        for (const auto& face : geometry.faces)
            corner_count += face.size();
        if (geometry.corner_uvs.size() != corner_count) {
            error = "Corner UV count does not match total face corners.";
            return false;
        }
        // Assimp mesh UV is per-point; expand corner → point (first writer wins)
        // when point UV channel is empty so exporters still emit UV0.
        if (geometry.uvs.empty()) {
            geometry.uvs.assign(point_count, {{0.f, 0.f}});
            size_t cursor = 0;
            for (const auto& face : geometry.faces) {
                for (uint32_t index : face) {
                    if (index < point_count)
                        geometry.uvs[index] = geometry.corner_uvs[cursor];
                    ++cursor;
                }
            }
        }
    }
    return true;
}

} // namespace pcg::fbx
