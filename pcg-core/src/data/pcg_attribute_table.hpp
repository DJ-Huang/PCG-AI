#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace pcg::internal::data {

enum class AttributeOwner {
    Point,
    Vertex,
    Primitive,
    Detail,
};

enum class AttributeType {
    Int,
    Float,
    String,
};

enum class AttributeTransformRole {
    None,
    Position,
    Vector,
    Normal,
    Quaternion,
    Matrix,
};

struct AttributeSchema {
    std::string name;
    AttributeOwner owner = AttributeOwner::Point;
    AttributeType type = AttributeType::Float;
    int tuple_size = 1;
    AttributeTransformRole transform_role = AttributeTransformRole::None;
};

class AttributeArray {
public:
    static AttributeArray make_int(AttributeSchema schema,
                                   std::vector<int64_t> default_value = {});
    static AttributeArray make_float(AttributeSchema schema,
                                     std::vector<double> default_value = {});
    static AttributeArray make_string(AttributeSchema schema,
                                      std::vector<std::string> default_value = {});

    const AttributeSchema& schema() const { return schema_; }
    size_t size() const;
    bool schema_compatible(const AttributeArray& other) const;

    void resize(size_t element_count);
    void append_defaults(size_t element_count);
    bool append_from(const AttributeArray& other);
    void append_element_from(const AttributeArray& other, size_t source_index);

    const std::vector<int64_t>& int_values() const { return int_values_; }
    std::vector<int64_t>& int_values_mut() { return int_values_; }
    const std::vector<double>& float_values() const { return float_values_; }
    std::vector<double>& float_values_mut() { return float_values_; }
    const std::vector<std::string>& string_values() const { return string_values_; }
    std::vector<std::string>& string_values_mut() { return string_values_; }

    const std::vector<int64_t>& default_int() const { return default_int_; }
    const std::vector<double>& default_float() const { return default_float_; }
    const std::vector<std::string>& default_string() const { return default_string_; }

private:
    AttributeSchema schema_;
    std::vector<int64_t> int_values_;
    std::vector<double> float_values_;
    std::vector<std::string> string_values_;
    std::vector<int64_t> default_int_;
    std::vector<double> default_float_;
    std::vector<std::string> default_string_;
};

using AttributeRemap = std::array<std::vector<int>, 4>;
using AttributeCounts = std::array<size_t, 4>;

class AttributeTable {
public:
    AttributeArray& create_int(AttributeOwner owner,
                               const std::string& name,
                               int tuple_size = 1,
                               std::vector<int64_t> default_value = {},
                               AttributeTransformRole role = AttributeTransformRole::None);
    AttributeArray& create_float(AttributeOwner owner,
                                 const std::string& name,
                                 int tuple_size = 1,
                                 std::vector<double> default_value = {},
                                 AttributeTransformRole role = AttributeTransformRole::None);
    AttributeArray& create_string(AttributeOwner owner,
                                  const std::string& name,
                                  int tuple_size = 1,
                                  std::vector<std::string> default_value = {});

    AttributeArray* find(AttributeOwner owner, const std::string& name);
    const AttributeArray* find(AttributeOwner owner, const std::string& name) const;
    std::vector<std::string> names(AttributeOwner owner) const;
    void erase(AttributeOwner owner, const std::string& name);
    void clear() { attributes_ = {}; }

    void resize(AttributeOwner owner, size_t element_count);
    void append_from(const AttributeTable& other,
                     const AttributeCounts& current_counts,
                     const AttributeCounts& other_counts);
    static AttributeTable remap_from(const AttributeTable& source,
                                     const AttributeRemap& destination_to_source);

    bool validate(const AttributeCounts& counts, std::string* error = nullptr) const;
    bool operator==(const AttributeTable& other) const;

private:
    using AttributeMap = std::unordered_map<std::string, AttributeArray>;

    static size_t owner_index(AttributeOwner owner);
    AttributeMap& map_for(AttributeOwner owner);
    const AttributeMap& map_for(AttributeOwner owner) const;
    AttributeArray& insert(AttributeArray attribute);

    std::array<AttributeMap, 4> attributes_;
};

} // namespace pcg::internal::data
