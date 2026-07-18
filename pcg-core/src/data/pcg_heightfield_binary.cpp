#include "data/pcg_heightfield_binary.hpp"

#include <cstring>
#include <limits>

namespace pcg::internal::data {
namespace {

template <typename T>
void write_value(uint8_t*& cursor, T value)
{
    std::memcpy(cursor, &value, sizeof(T));
    cursor += sizeof(T);
}

template <typename T>
bool read_value(const uint8_t*& cursor, const uint8_t* end, T& value)
{
    if (cursor > end || static_cast<std::size_t>(end - cursor) < sizeof(T))
        return false;
    std::memcpy(&value, cursor, sizeof(T));
    cursor += sizeof(T);
    return true;
}

bool add_checked(int64_t& total, int64_t amount)
{
    if (amount < 0 || total > std::numeric_limits<int>::max() - amount)
        return false;
    total += amount;
    return true;
}

} // namespace

int heightfield_binary_size(const PcgHeightField& heightfield)
{
    if (!heightfield.valid())
        return 0;

    int64_t total = kPcgHeightFieldBinaryHeaderSize;
    for (const auto& [name, layer] : heightfield.layers()) {
        const int64_t layer_header = 5 * static_cast<int64_t>(sizeof(uint32_t));
        const int64_t value_bytes =
            static_cast<int64_t>(layer.values.size()) * static_cast<int64_t>(sizeof(float));
        if (!add_checked(total, layer_header) ||
            !add_checked(total, static_cast<int64_t>(name.size())) ||
            !add_checked(total, value_bytes)) {
            return 0;
        }
    }
    return static_cast<int>(total);
}

bool write_heightfield_binary(const PcgHeightField& heightfield,
                              void* buffer,
                              int buffer_size)
{
    const int required = heightfield_binary_size(heightfield);
    if (!buffer || required <= 0 || buffer_size < required)
        return false;

    auto* cursor = static_cast<uint8_t*>(buffer);
    write_value(cursor, kPcgHeightFieldBinaryMagic);
    write_value(cursor, kPcgHeightFieldBinaryVersion);
    write_value(cursor, static_cast<int32_t>(heightfield.resolution_x()));
    write_value(cursor, static_cast<int32_t>(heightfield.resolution_z()));
    write_value(cursor, static_cast<int32_t>(heightfield.layers().size()));
    write_value(cursor, static_cast<uint32_t>(heightfield.sampling()));
    write_value(cursor, static_cast<uint32_t>(heightfield.orientation()));
    write_value(cursor, uint32_t{0});
    write_value(cursor, heightfield.size_x());
    write_value(cursor, heightfield.size_z());
    write_value(cursor, heightfield.center().x);
    write_value(cursor, heightfield.center().y);
    write_value(cursor, heightfield.center().z);

    for (const auto& [name, layer] : heightfield.layers()) {
        write_value(cursor, static_cast<uint32_t>(name.size()));
        write_value(cursor, static_cast<int32_t>(layer.tuple_size));
        write_value(cursor, static_cast<uint32_t>(layer.border_type));
        write_value(cursor, layer.border_value);
        write_value(cursor, static_cast<uint32_t>(layer.values.size()));
        if (!name.empty()) {
            std::memcpy(cursor, name.data(), name.size());
            cursor += name.size();
        }
        if (!layer.values.empty()) {
            const std::size_t bytes = layer.values.size() * sizeof(float);
            std::memcpy(cursor, layer.values.data(), bytes);
            cursor += bytes;
        }
    }
    return cursor == static_cast<uint8_t*>(buffer) + required;
}

bool read_heightfield_binary(const void* buffer,
                             int buffer_size,
                             PcgHeightField& out_heightfield)
{
    if (!buffer || buffer_size < kPcgHeightFieldBinaryHeaderSize)
        return false;

    const auto* cursor = static_cast<const uint8_t*>(buffer);
    const auto* end = cursor + buffer_size;
    uint32_t magic = 0;
    uint32_t version = 0;
    int32_t resolution_x = 0;
    int32_t resolution_z = 0;
    int32_t layer_count = 0;
    uint32_t sampling = 0;
    uint32_t orientation = 0;
    uint32_t reserved = 0;
    double size_x = 0.0;
    double size_z = 0.0;
    PcgVec3 center;
    if (!read_value(cursor, end, magic) ||
        !read_value(cursor, end, version) ||
        !read_value(cursor, end, resolution_x) ||
        !read_value(cursor, end, resolution_z) ||
        !read_value(cursor, end, layer_count) ||
        !read_value(cursor, end, sampling) ||
        !read_value(cursor, end, orientation) ||
        !read_value(cursor, end, reserved) ||
        !read_value(cursor, end, size_x) ||
        !read_value(cursor, end, size_z) ||
        !read_value(cursor, end, center.x) ||
        !read_value(cursor, end, center.y) ||
        !read_value(cursor, end, center.z)) {
        return false;
    }

    if (magic != kPcgHeightFieldBinaryMagic ||
        version != kPcgHeightFieldBinaryVersion ||
        resolution_x < 2 || resolution_z < 2 ||
        layer_count < 0 || layer_count > 4096 ||
        sampling > static_cast<uint32_t>(HeightFieldSampling::Corner) ||
        orientation > static_cast<uint32_t>(HeightFieldOrientation::YZ)) {
        return false;
    }

    PcgHeightField parsed(
        resolution_x,
        resolution_z,
        size_x,
        size_z,
        center,
        static_cast<HeightFieldSampling>(sampling),
        static_cast<HeightFieldOrientation>(orientation));

    const uint64_t sample_count =
        static_cast<uint64_t>(resolution_x) * static_cast<uint64_t>(resolution_z);
    for (int i = 0; i < layer_count; ++i) {
        uint32_t name_size = 0;
        int32_t tuple_size = 0;
        uint32_t border_type = 0;
        float border_value = 0.0f;
        uint32_t value_count = 0;
        if (!read_value(cursor, end, name_size) ||
            !read_value(cursor, end, tuple_size) ||
            !read_value(cursor, end, border_type) ||
            !read_value(cursor, end, border_value) ||
            !read_value(cursor, end, value_count)) {
            return false;
        }
        const uint64_t expected_values = sample_count * static_cast<uint64_t>(tuple_size);
        if (name_size == 0 || tuple_size <= 0 || tuple_size > 64 ||
            border_type > static_cast<uint32_t>(HeightFieldBorderType::Streak) ||
            value_count != expected_values ||
            cursor > end || static_cast<uint64_t>(end - cursor) <
                static_cast<uint64_t>(name_size) + expected_values * sizeof(float)) {
            return false;
        }

        std::string name(reinterpret_cast<const char*>(cursor), name_size);
        cursor += name_size;
        auto& layer = parsed.create_layer(name, tuple_size, 0.0f);
        layer.border_type = static_cast<HeightFieldBorderType>(border_type);
        layer.border_value = border_value;
        const std::size_t value_bytes = static_cast<std::size_t>(value_count) * sizeof(float);
        std::memcpy(layer.values.data(), cursor, value_bytes);
        cursor += value_bytes;
    }

    if (!parsed.valid())
        return false;
    out_heightfield = std::move(parsed);
    return true;
}

} // namespace pcg::internal::data
