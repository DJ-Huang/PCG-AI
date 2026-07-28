#include "data/pcg_attribute_table.hpp"

#include <algorithm>
#include <stdexcept>

namespace pcg::internal::data {
namespace {

template <typename T>
std::vector<T> normalized_default(std::vector<T> value, int tuple_size)
{
    const size_t width = static_cast<size_t>(std::max(1, tuple_size));
    value.resize(width);
    if (value.size() > width)
        value.resize(width);
    return value;
}

template <typename T>
void resize_with_tuple_default(std::vector<T>& values,
                               size_t element_count,
                               const std::vector<T>& default_value)
{
    const size_t width = default_value.size();
    const size_t old_count = width == 0 ? 0 : values.size() / width;
    values.resize(element_count * width);
    for (size_t i = old_count; i < element_count; ++i) {
        std::copy(default_value.begin(), default_value.end(),
                  values.begin() + static_cast<std::ptrdiff_t>(i * width));
    }
}

template <typename T>
void append_element(std::vector<T>& destination,
                    const std::vector<T>& source,
                    size_t source_index,
                    size_t width)
{
    const size_t begin = source_index * width;
    destination.insert(destination.end(),
                       source.begin() + static_cast<std::ptrdiff_t>(begin),
                       source.begin() + static_cast<std::ptrdiff_t>(begin + width));
}

} // namespace

AttributeArray AttributeArray::make_int(AttributeSchema schema,
                                        std::vector<int64_t> default_value)
{
    schema.type = AttributeType::Int;
    schema.tuple_size = std::max(1, schema.tuple_size);
    AttributeArray result;
    result.schema_ = std::move(schema);
    result.default_int_ = normalized_default(std::move(default_value), result.schema_.tuple_size);
    return result;
}

AttributeArray AttributeArray::make_float(AttributeSchema schema,
                                          std::vector<double> default_value)
{
    schema.type = AttributeType::Float;
    schema.tuple_size = std::max(1, schema.tuple_size);
    AttributeArray result;
    result.schema_ = std::move(schema);
    result.default_float_ = normalized_default(std::move(default_value), result.schema_.tuple_size);
    return result;
}

AttributeArray AttributeArray::make_string(AttributeSchema schema,
                                           std::vector<std::string> default_value)
{
    schema.type = AttributeType::String;
    schema.tuple_size = std::max(1, schema.tuple_size);
    schema.transform_role = AttributeTransformRole::None;
    AttributeArray result;
    result.schema_ = std::move(schema);
    result.default_string_ = normalized_default(std::move(default_value), result.schema_.tuple_size);
    return result;
}

size_t AttributeArray::size() const
{
    const size_t width = static_cast<size_t>(schema_.tuple_size);
    switch (schema_.type) {
    case AttributeType::Int:
        return int_values_.size() / width;
    case AttributeType::Float:
        return float_values_.size() / width;
    case AttributeType::String:
        return string_values_.size() / width;
    }
    return 0;
}

bool AttributeArray::schema_compatible(const AttributeArray& other) const
{
    return schema_.name == other.schema_.name && schema_.owner == other.schema_.owner &&
           schema_.type == other.schema_.type && schema_.tuple_size == other.schema_.tuple_size &&
           schema_.transform_role == other.schema_.transform_role;
}

void AttributeArray::resize(size_t element_count)
{
    switch (schema_.type) {
    case AttributeType::Int:
        resize_with_tuple_default(int_values_, element_count, default_int_);
        break;
    case AttributeType::Float:
        resize_with_tuple_default(float_values_, element_count, default_float_);
        break;
    case AttributeType::String:
        resize_with_tuple_default(string_values_, element_count, default_string_);
        break;
    }
}

void AttributeArray::append_defaults(size_t element_count)
{
    resize(size() + element_count);
}

bool AttributeArray::append_from(const AttributeArray& other)
{
    if (!schema_compatible(other))
        return false;
    switch (schema_.type) {
    case AttributeType::Int:
        int_values_.insert(int_values_.end(), other.int_values_.begin(), other.int_values_.end());
        break;
    case AttributeType::Float:
        float_values_.insert(float_values_.end(), other.float_values_.begin(), other.float_values_.end());
        break;
    case AttributeType::String:
        string_values_.insert(string_values_.end(), other.string_values_.begin(), other.string_values_.end());
        break;
    }
    return true;
}

void AttributeArray::append_element_from(const AttributeArray& other, size_t source_index)
{
    if (!schema_compatible(other) || source_index >= other.size()) {
        append_defaults(1);
        return;
    }
    const size_t width = static_cast<size_t>(schema_.tuple_size);
    switch (schema_.type) {
    case AttributeType::Int:
        append_element(int_values_, other.int_values_, source_index, width);
        break;
    case AttributeType::Float:
        append_element(float_values_, other.float_values_, source_index, width);
        break;
    case AttributeType::String:
        append_element(string_values_, other.string_values_, source_index, width);
        break;
    }
}

void AttributeArray::set_element_from(const AttributeArray& other,
                                      size_t dest_index,
                                      size_t source_index)
{
    if (!schema_compatible(other) || dest_index >= size())
        return;
    const size_t width = static_cast<size_t>(schema_.tuple_size);
    const bool use_default = source_index >= other.size();
    switch (schema_.type) {
    case AttributeType::Int: {
        const auto& src = use_default ? default_int_ : other.int_values_;
        const size_t src_begin = use_default ? 0 : source_index * width;
        std::copy(src.begin() + static_cast<std::ptrdiff_t>(src_begin),
                  src.begin() + static_cast<std::ptrdiff_t>(src_begin + width),
                  int_values_.begin() + static_cast<std::ptrdiff_t>(dest_index * width));
        break;
    }
    case AttributeType::Float: {
        const auto& src = use_default ? default_float_ : other.float_values_;
        const size_t src_begin = use_default ? 0 : source_index * width;
        std::copy(src.begin() + static_cast<std::ptrdiff_t>(src_begin),
                  src.begin() + static_cast<std::ptrdiff_t>(src_begin + width),
                  float_values_.begin() + static_cast<std::ptrdiff_t>(dest_index * width));
        break;
    }
    case AttributeType::String: {
        const auto& src = use_default ? default_string_ : other.string_values_;
        const size_t src_begin = use_default ? 0 : source_index * width;
        std::copy(src.begin() + static_cast<std::ptrdiff_t>(src_begin),
                  src.begin() + static_cast<std::ptrdiff_t>(src_begin + width),
                  string_values_.begin() + static_cast<std::ptrdiff_t>(dest_index * width));
        break;
    }
    }
}

size_t AttributeTable::owner_index(AttributeOwner owner)
{
    return static_cast<size_t>(owner);
}

AttributeTable::AttributeMap& AttributeTable::map_for(AttributeOwner owner)
{
    return attributes_[owner_index(owner)];
}

const AttributeTable::AttributeMap& AttributeTable::map_for(AttributeOwner owner) const
{
    return attributes_[owner_index(owner)];
}

AttributeArray& AttributeTable::insert(AttributeArray attribute)
{
    auto& attributes = map_for(attribute.schema().owner);
    const std::string name = attribute.schema().name;
    const auto found = attributes.find(name);
    if (found != attributes.end()) {
        if (!found->second.schema_compatible(attribute))
            throw std::invalid_argument("Attribute schema conflict: " + name);
        return found->second;
    }
    return attributes.emplace(name, std::move(attribute)).first->second;
}

AttributeArray& AttributeTable::create_int(AttributeOwner owner,
                                           const std::string& name,
                                           int tuple_size,
                                           std::vector<int64_t> default_value,
                                           AttributeTransformRole role)
{
    return insert(AttributeArray::make_int({name, owner, AttributeType::Int, tuple_size, role},
                                           std::move(default_value)));
}

AttributeArray& AttributeTable::create_float(AttributeOwner owner,
                                             const std::string& name,
                                             int tuple_size,
                                             std::vector<double> default_value,
                                             AttributeTransformRole role)
{
    return insert(AttributeArray::make_float({name, owner, AttributeType::Float, tuple_size, role},
                                             std::move(default_value)));
}

AttributeArray& AttributeTable::create_string(AttributeOwner owner,
                                              const std::string& name,
                                              int tuple_size,
                                              std::vector<std::string> default_value)
{
    return insert(AttributeArray::make_string(
        {name, owner, AttributeType::String, tuple_size, AttributeTransformRole::None},
        std::move(default_value)));
}

AttributeArray* AttributeTable::find(AttributeOwner owner, const std::string& name)
{
    auto& attributes = map_for(owner);
    const auto found = attributes.find(name);
    return found == attributes.end() ? nullptr : &found->second;
}

const AttributeArray* AttributeTable::find(AttributeOwner owner, const std::string& name) const
{
    const auto& attributes = map_for(owner);
    const auto found = attributes.find(name);
    return found == attributes.end() ? nullptr : &found->second;
}

std::vector<std::string> AttributeTable::names(AttributeOwner owner) const
{
    std::vector<std::string> result;
    result.reserve(map_for(owner).size());
    for (const auto& entry : map_for(owner))
        result.push_back(entry.first);
    std::sort(result.begin(), result.end());
    return result;
}

void AttributeTable::erase(AttributeOwner owner, const std::string& name)
{
    map_for(owner).erase(name);
}

void AttributeTable::resize(AttributeOwner owner, size_t element_count)
{
    for (auto& entry : map_for(owner))
        entry.second.resize(element_count);
}

void AttributeTable::append_from(const AttributeTable& other,
                                 const AttributeCounts& current_counts,
                                 const AttributeCounts& other_counts)
{
    for (AttributeOwner owner : {AttributeOwner::Point, AttributeOwner::Vertex,
                                 AttributeOwner::Primitive}) {
        const size_t index = owner_index(owner);
        auto& destination = map_for(owner);
        const auto& source = other.map_for(owner);

        for (auto& entry : destination) {
            entry.second.resize(current_counts[index]);
            const auto found = source.find(entry.first);
            if (found != source.end() && entry.second.schema_compatible(found->second) &&
                found->second.size() == other_counts[index]) {
                entry.second.append_from(found->second);
            } else {
                entry.second.append_defaults(other_counts[index]);
            }
        }

        for (const auto& entry : source) {
            if (destination.count(entry.first) > 0)
                continue;
            AttributeArray added = entry.second;
            added.resize(0);
            added.resize(current_counts[index]);
            if (entry.second.size() == other_counts[index])
                added.append_from(entry.second);
            else
                added.append_defaults(other_counts[index]);
            destination.emplace(entry.first, std::move(added));
        }
    }

    auto& detail = map_for(AttributeOwner::Detail);
    for (const auto& entry : other.map_for(AttributeOwner::Detail)) {
        if (detail.count(entry.first) == 0)
            detail.emplace(entry.first, entry.second);
    }
}

AttributeTable AttributeTable::remap_from(const AttributeTable& source,
                                          const AttributeRemap& destination_to_source)
{
    AttributeTable result;
    for (AttributeOwner owner : {AttributeOwner::Point, AttributeOwner::Vertex,
                                 AttributeOwner::Primitive}) {
        const auto& remap = destination_to_source[owner_index(owner)];
        for (const auto& entry : source.map_for(owner)) {
            AttributeArray destination = entry.second;
            destination.resize(0);
            for (int source_index : remap) {
                if (source_index < 0)
                    destination.append_defaults(1);
                else
                    destination.append_element_from(entry.second,
                                                    static_cast<size_t>(source_index));
            }
            result.map_for(owner).emplace(entry.first, std::move(destination));
        }
    }
    result.map_for(AttributeOwner::Detail) = source.map_for(AttributeOwner::Detail);
    return result;
}

bool AttributeTable::validate(const AttributeCounts& counts, std::string* error) const
{
    for (AttributeOwner owner : {AttributeOwner::Point, AttributeOwner::Vertex,
                                 AttributeOwner::Primitive, AttributeOwner::Detail}) {
        const size_t expected = counts[owner_index(owner)];
        for (const auto& entry : map_for(owner)) {
            if (entry.second.size() == expected)
                continue;
            if (error) {
                *error = "Attribute cardinality mismatch for " + entry.first + ": expected " +
                         std::to_string(expected) + ", got " +
                         std::to_string(entry.second.size());
            }
            return false;
        }
    }
    return true;
}

bool AttributeTable::operator==(const AttributeTable& other) const
{
    for (AttributeOwner owner : {AttributeOwner::Point, AttributeOwner::Vertex,
                                 AttributeOwner::Primitive, AttributeOwner::Detail}) {
        const auto names_here = names(owner);
        if (names_here != other.names(owner))
            return false;
        for (const auto& name : names_here) {
            const auto* a = find(owner, name);
            const auto* b = other.find(owner, name);
            if (!a || !b || !a->schema_compatible(*b) || a->int_values() != b->int_values() ||
                a->float_values() != b->float_values() ||
                a->string_values() != b->string_values() ||
                a->default_int() != b->default_int() ||
                a->default_float() != b->default_float() ||
                a->default_string() != b->default_string())
                return false;
        }
    }
    return true;
}

} // namespace pcg::internal::data
