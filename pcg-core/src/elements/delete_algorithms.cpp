#include "elements/delete_algorithms.hpp"

#include "elements/color_ramp.hpp"
#include "elements/element_utils.hpp"
#include "elements/expression.hpp"
#include "geometry/element_pattern.hpp"
#include "data/pcg_attribute_table.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pcg::internal::elements {
namespace {

using expression::EvalContext;
using expression::Program;
using geometry::GroupDomain;

constexpr double kPi = 3.14159265358979323846;

const nlohmann::json* find_param(const nlohmann::json& data, const char* key)
{
    if (!data.is_object() || key == nullptr)
        return nullptr;
    const auto it = data.find(key);
    return it == data.end() || it->is_null() ? nullptr : &(*it);
}

std::string read_string_param(const nlohmann::json& data,
                              const char* key,
                              const std::string& default_value)
{
    const auto* value = find_param(data, key);
    return value != nullptr && value->is_string() ? value->get<std::string>() : default_value;
}

bool parse_number_string(const std::string& text, double& out)
{
    const char* begin = text.c_str();
    while (*begin != '\0' && std::isspace(static_cast<unsigned char>(*begin)))
        ++begin;
    if (*begin == '\0')
        return false;

    char* end = nullptr;
    const double parsed = std::strtod(begin, &end);
    if (end == begin || !std::isfinite(parsed))
        return false;
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)))
        ++end;
    if (*end != '\0')
        return false;

    out = parsed;
    return true;
}

double read_number_param(const nlohmann::json& data, const char* key, double default_value)
{
    const auto* value = find_param(data, key);
    if (value == nullptr)
        return default_value;
    if (value->is_number())
        return value->get<double>();
    if (value->is_string()) {
        double parsed = 0.0;
        if (parse_number_string(value->get_ref<const std::string&>(), parsed))
            return parsed;
    }
    return default_value;
}

int read_integer_param(const nlohmann::json& data, const char* key, int default_value)
{
    const double parsed = read_number_param(data, key, static_cast<double>(default_value));
    if (parsed < static_cast<double>(std::numeric_limits<int>::min()) ||
        parsed > static_cast<double>(std::numeric_limits<int>::max()))
        return default_value;
    return static_cast<int>(parsed);
}

bool read_bool_param(const nlohmann::json& data, const char* key, bool default_value)
{
    const auto* value = find_param(data, key);
    if (value == nullptr)
        return default_value;
    if (value->is_boolean())
        return value->get<bool>();
    if (value->is_number())
        return value->get<double>() != 0.0;
    if (!value->is_string())
        return default_value;

    std::string text = value->get<std::string>();
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (text == "true" || text == "1")
        return true;
    if (text == "false" || text == "0")
        return false;
    return default_value;
}

DeleteOptions parse_delete_options_impl(const nlohmann::json& data)
{
    DeleteOptions options;
    if (!data.is_object())
        return options;

    options.group = read_string_param(data, "group", "");
    options.entity = read_string_param(data, "entity", "points");
    options.delete_non_selected = read_bool_param(data, "deleteNonSelected", false);
    options.geometry_type = read_string_param(data, "geometryType", "all");

    options.number_enable = read_bool_param(data, "numberEnable", false);
    options.number_mode = read_string_param(data, "numberMode", "pattern");
    options.number_pattern = read_string_param(data, "numberPattern", "");
    options.number_range_start = read_integer_param(data, "numberRangeStart", 0);
    options.number_range_end = read_integer_param(data, "numberRangeEnd", 0);
    options.number_select_of = read_integer_param(data, "numberSelectOf", 1);
    options.number_select_offset = read_integer_param(data, "numberSelectOffset", 0);
    options.number_expression = read_string_param(data, "numberExpression", "");

    options.bounding_enable = read_bool_param(data, "boundingEnable", false);
    options.bounding_type = read_string_param(data, "boundingType", "box");
    options.bounding_center_x = read_number_param(data, "boundingCenterX", 0.0);
    options.bounding_center_y = read_number_param(data, "boundingCenterY", 0.0);
    options.bounding_center_z = read_number_param(data, "boundingCenterZ", 0.0);
    options.bounding_size_x = read_number_param(data, "boundingSizeX", 1.0);
    options.bounding_size_y = read_number_param(data, "boundingSizeY", 1.0);
    options.bounding_size_z = read_number_param(data, "boundingSizeZ", 1.0);
    options.bounding_radius = read_number_param(data, "boundingRadius", 1.0);

    options.normal_enable = read_bool_param(data, "normalEnable", false);
    options.normal_dir_x = read_number_param(data, "normalDirX", 0.0);
    options.normal_dir_y = read_number_param(data, "normalDirY", 1.0);
    options.normal_dir_z = read_number_param(data, "normalDirZ", 1.0);
    options.normal_spread = read_number_param(data, "normalSpread", 180.0);

    options.degenerate_duplicate_points =
        read_bool_param(data, "degenerateDuplicatePoints", false);
    options.degenerate_zero_area = read_bool_param(data, "degenerateZeroArea", false);
    options.degenerate_open_face_perimeter =
        read_bool_param(data, "degenerateOpenFacePerimeter", false);
    options.degenerate_tolerance = read_number_param(data, "degenerateTolerance", 0.0001);

    options.random_enable = read_bool_param(data, "randomEnable", false);
    options.random_seed = read_integer_param(data, "randomSeed", 0);
    options.random_seed_attribute = read_string_param(data, "randomSeedAttribute", "");
    options.random_percent = read_number_param(data, "randomPercent", 100.0);

    options.keep_points = read_bool_param(data, "keepPoints", false);
    options.delete_unused_groups = read_bool_param(data, "deleteUnusedGroups", false);
    return options;
}

data::PcgVec3 normalize_vec(data::PcgVec3 v)
{
    const double len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-12)
        return {0.0, 1.0, 0.0};
    return {v.x / len, v.y / len, v.z / len};
}

double dot_vec(const data::PcgVec3& a, const data::PcgVec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

data::PcgVec3 face_normal_vec(const data::PcgGeometry& geometry, size_t face_index)
{
    const auto& face = geometry.faces()[face_index];
    if (face.size() < 3)
        return {0.0, 1.0, 0.0};
    data::PcgVec3 n{};
    for (size_t i = 0; i < face.size(); ++i) {
        const auto& p0 = geometry.points()[static_cast<size_t>(face[i])];
        const auto& p1 = geometry.points()[static_cast<size_t>(face[(i + 1) % face.size()])];
        n.x += (p0.y - p1.y) * (p0.z + p1.z);
        n.y += (p0.z - p1.z) * (p0.x + p1.x);
        n.z += (p0.x - p1.x) * (p0.y + p1.y);
    }
    return normalize_vec(n);
}

double face_area_vec(const data::PcgGeometry& geometry, size_t face_index)
{
    const auto& face = geometry.faces()[face_index];
    if (face.size() < 3)
        return 0.0;
    const auto& p0 = geometry.points()[static_cast<size_t>(face[0])];
    double area = 0.0;
    for (size_t i = 1; i + 1 < face.size(); ++i) {
        const auto& p1 = geometry.points()[static_cast<size_t>(face[i])];
        const auto& p2 = geometry.points()[static_cast<size_t>(face[i + 1])];
        const double ax = p1.x - p0.x;
        const double ay = p1.y - p0.y;
        const double az = p1.z - p0.z;
        const double bx = p2.x - p0.x;
        const double by = p2.y - p0.y;
        const double bz = p2.z - p0.z;
        const double cx = ay * bz - az * by;
        const double cy = az * bx - ax * bz;
        const double cz = ax * by - ay * bx;
        area += 0.5 * std::sqrt(cx * cx + cy * cy + cz * cz);
    }
    return area;
}

bool has_active_delete_condition(const DeleteOptions& options)
{
    if (!options.group.empty())
        return true;
    if (options.number_enable)
        return true;
    if (options.bounding_enable)
        return true;
    if (options.normal_enable)
        return true;
    if (options.degenerate_duplicate_points || options.degenerate_zero_area ||
        options.degenerate_open_face_perimeter)
        return true;
    if (options.random_enable)
        return true;
    return false;
}

bool point_in_bounding(const data::PcgVec3& point, const DeleteOptions& options)
{
    const double dx = point.x - options.bounding_center_x;
    const double dy = point.y - options.bounding_center_y;
    const double dz = point.z - options.bounding_center_z;
    if (options.bounding_type == "sphere") {
        const double radius = std::max(0.0, options.bounding_radius);
        return dx * dx + dy * dy + dz * dz <= radius * radius;
    }
    const double half_x = std::max(0.0, options.bounding_size_x) * 0.5;
    const double half_y = std::max(0.0, options.bounding_size_y) * 0.5;
    const double half_z = std::max(0.0, options.bounding_size_z) * 0.5;
    return std::abs(dx) <= half_x && std::abs(dy) <= half_y && std::abs(dz) <= half_z;
}

bool normal_matches(const data::PcgVec3& normal, const DeleteOptions& options)
{
    const data::PcgVec3 target = normalize_vec(
        {options.normal_dir_x, options.normal_dir_y, options.normal_dir_z});
    const data::PcgVec3 n = normalize_vec(normal);
    const double dot = std::clamp(dot_vec(n, target), -1.0, 1.0);
    const double angle = std::acos(dot) * 180.0 / kPi;
    return angle <= std::max(0.0, options.normal_spread);
}

uint32_t stable_element_hash(int graph_seed, int node_seed, int element_id, int salt)
{
    uint32_t state = mix_seed(graph_seed, node_seed);
    state = mix_seed(static_cast<int>(state), element_id);
    state = mix_seed(static_cast<int>(state), salt);
    return state == 0 ? 0xA5A5A5A5u : state;
}

bool random_selected(int graph_seed, const DeleteOptions& options, int element_id, int salt)
{
    const double percent = std::clamp(options.random_percent, 0.0, 100.0);
    if (percent <= 0.0)
        return false;
    if (percent >= 100.0)
        return true;
    const uint32_t hash = stable_element_hash(graph_seed, options.random_seed, element_id, salt);
    const double threshold = percent / 100.0;
    return static_cast<double>(hash % 10000) / 10000.0 < threshold;
}

bool read_int_attribute(const data::AttributeTable& table,
                        data::AttributeOwner owner,
                        size_t index,
                        const std::string& name,
                        int& out)
{
    const data::AttributeArray* attr = table.find(owner, name);
    if (!attr || attr->schema().type != data::AttributeType::Int || index >= attr->size())
        return false;
    const size_t offset = index * static_cast<size_t>(std::max(1, attr->schema().tuple_size));
    if (offset >= attr->int_values().size())
        return false;
    out = static_cast<int>(attr->int_values()[offset]);
    return true;
}

int random_seed_for_element(const data::PcgGeometry& geometry,
                            const DeleteOptions& options,
                            data::AttributeOwner owner,
                            size_t index,
                            int fallback_id)
{
    if (options.random_seed_attribute.empty())
        return fallback_id;
    int attr_seed = 0;
    if (read_int_attribute(geometry.attributes(), owner, index, options.random_seed_attribute,
                           attr_seed))
        return attr_seed;
    return fallback_id;
}

void invert_selection(std::unordered_set<int>& selected, int element_count)
{
    std::unordered_set<int> inverted;
    for (int index = 0; index < element_count; ++index) {
        if (!selected.count(index))
            inverted.insert(index);
    }
    selected = std::move(inverted);
}

struct DeleteEvalVariables {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double curve_u = 0.0;
    int element_number = 0;
    int element_count = 0;
    const data::AttributeTable* table = nullptr;
    data::AttributeOwner owner = data::AttributeOwner::Point;
    size_t attribute_index = 0;
    const data::PcgGeometry* geometry = nullptr;
};

EvalContext make_delete_context(DeleteEvalVariables& variables)
{
    EvalContext context;
    context.read_variable = [&variables](const std::string& name, double& value) {
        if (name == "@P.x")
            value = variables.x;
        else if (name == "@P.y")
            value = variables.y;
        else if (name == "@P.z")
            value = variables.z;
        else if (name == "@curveu")
            value = variables.curve_u;
        else if (name == "@ptnum")
            value = variables.element_number;
        else if (name == "@numpt")
            value = variables.element_count;
        else if (name == "@primnum")
            value = variables.element_number;
        else if (name == "@numprim")
            value = variables.element_count;
        else if (name.rfind("@group.", 0) == 0 && variables.geometry) {
            const std::string group_name = name.substr(7);
            const GroupDomain domain =
                variables.owner == data::AttributeOwner::Primitive ? GroupDomain::Face
                                                                   : GroupDomain::Point;
            const geometry::GroupId id = static_cast<geometry::GroupId>(variables.element_number);
            value = variables.geometry->groups().contains(domain, group_name, id) ? 1.0 : 0.0;
            return true;
        } else if (name.size() > 1 && name.front() == '@' && variables.table) {
            const std::string key = name.substr(1);
            const data::AttributeArray* attr = variables.table->find(variables.owner, key);
            if (!attr || variables.attribute_index >= attr->size())
                return false;
            if (attr->schema().type == data::AttributeType::Float) {
                const size_t offset =
                    variables.attribute_index *
                    static_cast<size_t>(std::max(1, attr->schema().tuple_size));
                if (offset >= attr->float_values().size())
                    return false;
                value = attr->float_values()[offset];
                return true;
            }
            if (attr->schema().type == data::AttributeType::Int) {
                const size_t offset =
                    variables.attribute_index *
                    static_cast<size_t>(std::max(1, attr->schema().tuple_size));
                if (offset >= attr->int_values().size())
                    return false;
                value = static_cast<double>(attr->int_values()[offset]);
                return true;
            }
            return false;
        } else {
            return false;
        }
        return true;
    };
    return context;
}

bool evaluate_number_expression(const Program& program,
                                DeleteEvalVariables& variables,
                                bool& selected,
                                std::string& error)
{
    EvalContext context = make_delete_context(variables);
    double value = 0.0;
    if (!program.evaluate(context, value, error))
        return false;
    selected = value != 0.0;
    return true;
}

void add_number_selection(const data::PcgGeometry& geometry,
                          const DeleteOptions& options,
                          const std::string& entity,
                          std::unordered_set<int>& selected,
                          std::string& error)
{
    const bool points = entity == "points";
    const bool edges = entity == "edges";
    const int element_count =
        points ? static_cast<int>(geometry.points().size())
               : edges ? static_cast<int>(geometry.groups().members(GroupDomain::Edge, "").size())
                       : static_cast<int>(geometry.faces().size());
    if (element_count <= 0)
        return;

    if (options.number_mode == "pattern" && !options.number_pattern.empty()) {
        std::unordered_set<int> number_selected;
        if (geometry::compute_element_pattern(options.number_pattern, element_count,
                                              number_selected)) {
            for (int index : number_selected)
                selected.insert(index);
        }
        return;
    }
    if (options.number_mode == "range") {
        std::unordered_set<int> number_selected;
        geometry::parse_element_range(options.number_range_start, options.number_range_end,
                                      options.number_select_of, options.number_select_offset,
                                      element_count, number_selected);
        for (int index : number_selected)
            selected.insert(index);
        return;
    }
    if (options.number_mode == "expression" && !options.number_expression.empty()) {
        Program program;
        if (!Program::compile_expression(options.number_expression, program, error))
            return;
        for (int index = 0; index < element_count; ++index) {
            DeleteEvalVariables variables;
            variables.element_number = index;
            variables.element_count = element_count;
            variables.geometry = &geometry;
            if (points) {
                const auto& p = geometry.points()[static_cast<size_t>(index)];
                variables.x = p.x;
                variables.y = p.y;
                variables.z = p.z;
                variables.owner = data::AttributeOwner::Point;
                variables.attribute_index = static_cast<size_t>(index);
                variables.table = &geometry.attributes();
            } else if (!edges) {
                const auto& face = geometry.faces()[static_cast<size_t>(index)];
                for (int point_index : face) {
                    if (point_index < 0 ||
                        static_cast<size_t>(point_index) >= geometry.points().size())
                        continue;
                    const auto& p = geometry.points()[static_cast<size_t>(point_index)];
                    variables.x += p.x;
                    variables.y += p.y;
                    variables.z += p.z;
                }
                if (!face.empty()) {
                    const double inv = 1.0 / static_cast<double>(face.size());
                    variables.x *= inv;
                    variables.y *= inv;
                    variables.z *= inv;
                }
                variables.owner = data::AttributeOwner::Primitive;
                variables.attribute_index = static_cast<size_t>(index);
                variables.table = &geometry.attributes();
            }
            bool expression_selected = false;
            if (!evaluate_number_expression(program, variables, expression_selected, error))
                continue;
            if (expression_selected)
                selected.insert(index);
        }
    }
}

void add_group_selection_geometry(const data::PcgGeometry& geometry,
                                  const DeleteOptions& options,
                                  const std::string& entity,
                                  std::unordered_set<int>& selected)
{
    if (options.group.empty())
        return;
    const bool points = entity == "points";
    const bool edges = entity == "edges";
    const int element_count =
        points ? static_cast<int>(geometry.points().size())
               : edges ? static_cast<int>(geometry.groups().members(GroupDomain::Edge, "").size())
                       : static_cast<int>(geometry.faces().size());
    if (element_count <= 0)
        return;

    const GroupDomain domain =
        points ? GroupDomain::Point : edges ? GroupDomain::Edge : GroupDomain::Face;
    for (geometry::GroupId id :
         geometry.groups().eval_indices(domain, options.group, element_count))
        selected.insert(static_cast<int>(id));
}

void add_spline_primitive_group_selection(const data::PcgSplineData& source,
                                          const DeleteOptions& options,
                                          std::unordered_set<int>& selected)
{
    if (options.group.empty())
        return;

    const int spline_count = static_cast<int>(source.splines().size());
    if (spline_count <= 0)
        return;

    bool matched_named = false;
    for (int index = 0; index < spline_count; ++index) {
        const auto& spline = source.splines()[static_cast<size_t>(index)];
        if (!spline.attributes.contains("groups") || !spline.attributes["groups"].is_array())
            continue;
        for (const auto& group : spline.attributes["groups"]) {
            if (group.is_string() && group.get<std::string>() == options.group) {
                selected.insert(index);
                matched_named = true;
                break;
            }
        }
    }
    if (matched_named)
        return;

    std::unordered_set<int> pattern_selected;
    if (geometry::compute_element_pattern(options.group, spline_count, pattern_selected)) {
        for (int index : pattern_selected)
            selected.insert(index);
    }
}

void add_spline_primitive_number_selection(const data::PcgSplineData& source,
                                           const DeleteOptions& options,
                                           std::unordered_set<int>& selected,
                                           std::string& error)
{
    const int spline_count = static_cast<int>(source.splines().size());
    if (spline_count <= 0)
        return;

    if (options.number_mode == "pattern" && !options.number_pattern.empty()) {
        std::unordered_set<int> number_selected;
        if (geometry::compute_element_pattern(options.number_pattern, spline_count,
                                              number_selected)) {
            for (int index : number_selected)
                selected.insert(index);
        }
        return;
    }
    if (options.number_mode == "range") {
        std::unordered_set<int> number_selected;
        geometry::parse_element_range(options.number_range_start, options.number_range_end,
                                      options.number_select_of, options.number_select_offset,
                                      spline_count, number_selected);
        for (int index : number_selected)
            selected.insert(index);
        return;
    }
    if (options.number_mode == "expression" && !options.number_expression.empty()) {
        Program program;
        if (!Program::compile_expression(options.number_expression, program, error))
            return;
        for (int index = 0; index < spline_count; ++index) {
            DeleteEvalVariables variables;
            variables.element_number = index;
            variables.element_count = spline_count;
            bool expression_selected = false;
            if (!evaluate_number_expression(program, variables, expression_selected, error))
                continue;
            if (expression_selected)
                selected.insert(index);
        }
    }
}

void add_spline_primitive_selection(const data::PcgSplineData& source,
                                    const DeleteOptions& options,
                                    int graph_seed,
                                    std::unordered_set<int>& selected,
                                    std::string& error)
{
    add_spline_primitive_group_selection(source, options, selected);
    if (options.number_enable)
        add_spline_primitive_number_selection(source, options, selected, error);
    if (options.random_enable) {
        const int spline_count = static_cast<int>(source.splines().size());
        for (int index = 0; index < spline_count; ++index) {
            if (random_selected(graph_seed, options, index, 5))
                selected.insert(index);
        }
    }
}

void add_bounding_selection_geometry(const data::PcgGeometry& geometry,
                                     const DeleteOptions& options,
                                     const std::string& entity,
                                     std::unordered_set<int>& selected)
{
    if (!options.bounding_enable)
        return;
    if (entity == "points") {
        for (size_t index = 0; index < geometry.points().size(); ++index) {
            if (point_in_bounding(geometry.points()[index], options))
                selected.insert(static_cast<int>(index));
        }
        return;
    }
    if (entity == "primitives") {
        for (size_t face_index = 0; face_index < geometry.faces().size(); ++face_index) {
            const auto& face = geometry.faces()[face_index];
            double x = 0.0, y = 0.0, z = 0.0;
            int count = 0;
            for (int point_index : face) {
                if (point_index < 0 ||
                    static_cast<size_t>(point_index) >= geometry.points().size())
                    continue;
                const auto& p = geometry.points()[static_cast<size_t>(point_index)];
                x += p.x; y += p.y; z += p.z;
                ++count;
            }
            if (count == 0)
                continue;
            const data::PcgVec3 centroid{x / count, y / count, z / count};
            if (point_in_bounding(centroid, options))
                selected.insert(static_cast<int>(face_index));
        }
        return;
    }
    for (const auto& group_name : geometry.groups().group_names(GroupDomain::Edge)) {
        for (geometry::GroupId edge_id : geometry.groups().members(GroupDomain::Edge, group_name)) {
            const auto endpoints = geometry::edge_group_points(edge_id);
            const int a = endpoints[0];
            const int b = endpoints[1];
            if (a < 0 || b < 0 || static_cast<size_t>(a) >= geometry.points().size() ||
                static_cast<size_t>(b) >= geometry.points().size())
                continue;
            const auto& p0 = geometry.points()[static_cast<size_t>(a)];
            const auto& p1 = geometry.points()[static_cast<size_t>(b)];
            const data::PcgVec3 mid{(p0.x + p1.x) * 0.5, (p0.y + p1.y) * 0.5, (p0.z + p1.z) * 0.5};
            if (point_in_bounding(mid, options))
                selected.insert(static_cast<int>(edge_id));
        }
    }
}

void add_normal_selection_geometry(const data::PcgGeometry& geometry,
                                   const DeleteOptions& options,
                                   const std::string& entity,
                                   std::unordered_set<int>& selected)
{
    if (!options.normal_enable)
        return;
    if (entity == "primitives") {
        for (size_t face_index = 0; face_index < geometry.faces().size(); ++face_index) {
            if (normal_matches(face_normal_vec(geometry, face_index), options))
                selected.insert(static_cast<int>(face_index));
        }
        return;
    }
    if (entity == "points") {
        std::vector<data::PcgVec3> accum(geometry.points().size(), {0.0, 0.0, 0.0});
        std::vector<int> counts(geometry.points().size(), 0);
        for (size_t face_index = 0; face_index < geometry.faces().size(); ++face_index) {
            const data::PcgVec3 n = face_normal_vec(geometry, face_index);
            for (int point_index : geometry.faces()[face_index]) {
                if (point_index < 0 ||
                    static_cast<size_t>(point_index) >= accum.size())
                    continue;
                accum[static_cast<size_t>(point_index)].x += n.x;
                accum[static_cast<size_t>(point_index)].y += n.y;
                accum[static_cast<size_t>(point_index)].z += n.z;
                counts[static_cast<size_t>(point_index)] += 1;
            }
        }
        for (size_t index = 0; index < accum.size(); ++index) {
            if (counts[index] == 0)
                continue;
            data::PcgVec3 n = accum[index];
            n.x /= counts[index];
            n.y /= counts[index];
            n.z /= counts[index];
            if (normal_matches(n, options))
                selected.insert(static_cast<int>(index));
        }
        return;
    }
    for (const auto& group_name : geometry.groups().group_names(GroupDomain::Edge)) {
        for (geometry::GroupId edge_id :
             geometry.groups().members(GroupDomain::Edge, group_name)) {
            const auto endpoints = geometry::edge_group_points(edge_id);
            const int a = endpoints[0];
            const int b = endpoints[1];
            if (a < 0 || b < 0)
                continue;
            data::PcgVec3 n{0.0, 0.0, 0.0};
            int count = 0;
            for (size_t face_index = 0; face_index < geometry.faces().size(); ++face_index) {
                const auto& face = geometry.faces()[face_index];
                bool touches = false;
                for (int point_index : face) {
                    if (point_index == a || point_index == b) {
                        touches = true;
                        break;
                    }
                }
                if (!touches)
                    continue;
                const data::PcgVec3 fn = face_normal_vec(geometry, face_index);
                n.x += fn.x; n.y += fn.y; n.z += fn.z;
                ++count;
            }
            if (count > 0) {
                n.x /= count; n.y /= count; n.z /= count;
                if (normal_matches(n, options))
                    selected.insert(static_cast<int>(edge_id));
            }
        }
    }
}

void add_degenerate_selection_geometry(const data::PcgGeometry& geometry,
                                       const DeleteOptions& options,
                                       std::unordered_set<int>& selected)
{
  for (size_t face_index = 0; face_index < geometry.faces().size(); ++face_index) {
        const auto& face = geometry.faces()[face_index];
        if (options.degenerate_duplicate_points) {
            std::unordered_set<int> unique;
            for (int point_index : face)
                unique.insert(point_index);
            if (unique.size() < face.size())
                selected.insert(static_cast<int>(face_index));
        }
        if (options.degenerate_zero_area) {
            const double tol = std::max(0.0, options.degenerate_tolerance);
            if (face_area_vec(geometry, face_index) < tol * tol)
                selected.insert(static_cast<int>(face_index));
        }
        if (options.degenerate_open_face_perimeter && face.size() >= 3) {
            bool open = false;
            for (size_t i = 0; i < face.size(); ++i) {
                const int a = face[i];
                const int b = face[(i + 1) % face.size()];
                bool shared = false;
                for (size_t other = 0; other < geometry.faces().size(); ++other) {
                    if (other == face_index)
                        continue;
                    const auto& other_face = geometry.faces()[other];
                    for (size_t j = 0; j < other_face.size(); ++j) {
                        const int c = other_face[j];
                        const int d = other_face[(j + 1) % other_face.size()];
                        if ((a == c && b == d) || (a == d && b == c)) {
                            shared = true;
                            break;
                        }
                    }
                    if (shared)
                        break;
                }
                if (!shared) {
                    open = true;
                    break;
                }
            }
            if (open)
                selected.insert(static_cast<int>(face_index));
        }
    }
}

void add_random_selection_geometry(const data::PcgGeometry& geometry,
                                   const DeleteOptions& options,
                                   const std::string& entity,
                                   int graph_seed,
                                   std::unordered_set<int>& selected)
{
    if (!options.random_enable)
        return;
    if (entity == "points") {
        for (size_t index = 0; index < geometry.points().size(); ++index) {
            const int seed_id =
                random_seed_for_element(geometry, options, data::AttributeOwner::Point, index,
                                        static_cast<int>(index));
            if (random_selected(graph_seed, options, seed_id, 1))
                selected.insert(static_cast<int>(index));
        }
        return;
    }
    if (entity == "primitives") {
        for (size_t index = 0; index < geometry.faces().size(); ++index) {
            const int seed_id =
                random_seed_for_element(geometry, options, data::AttributeOwner::Primitive, index,
                                        static_cast<int>(index));
            if (random_selected(graph_seed, options, seed_id, 2))
                selected.insert(static_cast<int>(index));
        }
        return;
    }
    for (const auto& group_name : geometry.groups().group_names(GroupDomain::Edge)) {
        for (geometry::GroupId edge_id :
             geometry.groups().members(GroupDomain::Edge, group_name)) {
            const auto endpoints = geometry::edge_group_points(edge_id);
            const int seed_id = endpoints[0] ^ (endpoints[1] << 16);
            if (random_selected(graph_seed, options, seed_id, 3))
                selected.insert(static_cast<int>(edge_id));
        }
    }
}

data::PcgGeometry rebuild_geometry_delete(const data::PcgGeometry& source,
                                          const std::vector<bool>& initial_keep_points,
                                          const std::vector<bool>& keep_faces,
                                          bool remove_unused_points)
{
    std::vector<bool> keep_points = initial_keep_points;
    if (remove_unused_points) {
        std::fill(keep_points.begin(), keep_points.end(), false);
        for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
            if (!keep_faces[face_index])
                continue;
            for (int point_index : source.faces()[face_index]) {
                if (point_index >= 0 &&
                    static_cast<size_t>(point_index) < keep_points.size() &&
                    initial_keep_points[static_cast<size_t>(point_index)])
                    keep_points[static_cast<size_t>(point_index)] = true;
            }
        }
    }

    data::PcgGeometry output;
    data::GeometryElementRemap topology_remap;
    std::vector<int> point_remap(source.points().size(), -1);
    for (size_t index = 0; index < source.points().size(); ++index) {
        if (!keep_points[index])
            continue;
        point_remap[index] = static_cast<int>(output.points().size());
        output.points_mut().push_back(source.points()[index]);
        topology_remap.points.push_back(static_cast<int>(index));
    }

    std::vector<int> face_remap(source.faces().size(), -1);
    std::vector<std::string> face_materials;
    std::vector<int> source_corner_offsets(source.faces().size(), 0);
    int corner_cursor = 0;
    for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
        source_corner_offsets[face_index] = corner_cursor;
        corner_cursor += static_cast<int>(source.faces()[face_index].size());
    }
    for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
        if (!keep_faces[face_index])
            continue;
        std::vector<int> face;
        bool valid = true;
        for (int old_point : source.faces()[face_index]) {
            if (old_point < 0 || static_cast<size_t>(old_point) >= point_remap.size() ||
                point_remap[static_cast<size_t>(old_point)] < 0) {
                valid = false;
                break;
            }
            face.push_back(point_remap[static_cast<size_t>(old_point)]);
        }
        if (!valid || face.size() < 3)
            continue;
        face_remap[face_index] = static_cast<int>(output.faces().size());
        for (size_t c = 0; c < source.faces()[face_index].size(); ++c)
            topology_remap.vertices.push_back(
                source_corner_offsets[face_index] + static_cast<int>(c));
        topology_remap.primitives.push_back(static_cast<int>(face_index));
        output.faces_mut().push_back(std::move(face));
        if (source.has_face_materials())
            face_materials.push_back(source.face_materials()[face_index]);
    }

    data::propagate_geometry_data(source, output, topology_remap);

    for (const auto& name : source.groups().group_names(GroupDomain::Point)) {
        for (int old_index : source.groups().members(GroupDomain::Point, name)) {
            if (old_index >= 0 && static_cast<size_t>(old_index) < point_remap.size() &&
                point_remap[static_cast<size_t>(old_index)] >= 0)
                output.groups().add(GroupDomain::Point, name,
                                    point_remap[static_cast<size_t>(old_index)]);
        }
    }
    for (const auto& name : source.groups().group_names(GroupDomain::Face)) {
        for (int old_index : source.groups().members(GroupDomain::Face, name)) {
            if (old_index >= 0 && static_cast<size_t>(old_index) < face_remap.size() &&
                face_remap[static_cast<size_t>(old_index)] >= 0)
                output.groups().add(GroupDomain::Face, name,
                                    face_remap[static_cast<size_t>(old_index)]);
        }
    }
    for (const auto& name : source.groups().group_names(GroupDomain::Edge)) {
        for (geometry::GroupId old_edge : source.groups().members(GroupDomain::Edge, name)) {
            const auto endpoints = geometry::edge_group_points(old_edge);
            const int old_a = endpoints[0];
            const int old_b = endpoints[1];
            if (old_a < 0 || old_b < 0 || static_cast<size_t>(old_a) >= point_remap.size() ||
                static_cast<size_t>(old_b) >= point_remap.size())
                continue;
            const int a = point_remap[static_cast<size_t>(old_a)];
            const int b = point_remap[static_cast<size_t>(old_b)];
            if (a >= 0 && b >= 0)
                output.groups().add(GroupDomain::Edge, name, geometry::edge_group_id(a, b));
        }
    }

    if (source.has_colors() && source.colors().size() == source.points().size()) {
        std::vector<data::PcgColor> colors;
        colors.reserve(output.points().size());
        for (size_t index = 0; index < source.colors().size(); ++index)
            if (keep_points[index])
                colors.push_back(source.colors()[index]);
        output.set_colors(std::move(colors));
    }
    if (source.has_uvs() && source.uvs().size() == source.points().size()) {
        std::vector<data::PcgVec2> uvs;
        uvs.reserve(output.points().size());
        for (size_t index = 0; index < source.uvs().size(); ++index)
            if (keep_points[index])
                uvs.push_back(source.uvs()[index]);
        output.set_uvs(std::move(uvs));
    }
    if (source.has_corner_uvs()) {
        std::vector<data::PcgVec2> corner_uvs;
        corner_uvs.reserve(static_cast<size_t>(output.corner_count()));
        size_t corner_offset = 0;
        for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
            const size_t face_corners = source.faces()[face_index].size();
            if (keep_faces[face_index] && face_remap[face_index] >= 0) {
                for (size_t c = 0; c < face_corners; ++c) {
                    if (corner_offset + c < source.corner_uvs().size())
                        corner_uvs.push_back(source.corner_uvs()[corner_offset + c]);
                    else
                        corner_uvs.push_back(data::PcgVec2{0.0, 0.0});
                }
            }
            corner_offset += face_corners;
        }
        output.set_corner_uvs(std::move(corner_uvs));
    }
    if (source.has_face_materials())
        output.set_face_materials(std::move(face_materials));
    else if (source.has_material())
        output.set_material_name(source.material_name());
    return output;
}

void append_spline_run(data::PcgSplineData& output,
                       const data::PcgSpline& source,
                       std::vector<data::PcgSplinePoint>& run)
{
    if (run.size() < 2) {
        run.clear();
        return;
    }
    data::PcgSpline spline;
    spline.points = std::move(run);
    spline.closed = false;
    spline.attributes = source.attributes;
    output.add_spline(std::move(spline));
    run.clear();
}

} // namespace

void remove_empty_groups(data::PcgGeometry& geometry)
{
    for (const auto domain : {geometry::GroupDomain::Point, geometry::GroupDomain::Edge,
                              geometry::GroupDomain::Face, geometry::GroupDomain::Vertex}) {
        for (const auto& name : geometry.groups().group_names(domain)) {
            if (geometry.groups().members(domain, name).empty())
                geometry.groups().clear_group(domain, name);
        }
    }
}

DeleteOptions parse_delete_options(const nlohmann::json& data)
{
    return parse_delete_options_impl(data);
}

data::PcgGeometry delete_geometry(const data::PcgGeometry& source,
                                  const DeleteOptions& options,
                                  int graph_seed,
                                  std::string& error)
{
    if (!has_active_delete_condition(options))
        return source;

    const std::string entity = options.entity;
    std::unordered_set<int> selected;

    add_group_selection_geometry(source, options, entity, selected);
    if (options.number_enable)
        add_number_selection(source, options, entity, selected, error);
    add_bounding_selection_geometry(source, options, entity, selected);
    add_normal_selection_geometry(source, options, entity, selected);
    if (entity == "primitives" &&
        (options.degenerate_duplicate_points || options.degenerate_zero_area ||
         options.degenerate_open_face_perimeter))
        add_degenerate_selection_geometry(source, options, selected);
    add_random_selection_geometry(source, options, entity, graph_seed, selected);

    const int element_count =
        entity == "points" ? static_cast<int>(source.points().size())
                           : entity == "edges" ? static_cast<int>(source.groups().group_names(
                                                    GroupDomain::Edge).size())
                                               : static_cast<int>(source.faces().size());
    if (options.delete_non_selected && element_count > 0)
        invert_selection(selected, element_count);

    std::vector<bool> keep_points(source.points().size(), true);
    std::vector<bool> keep_faces(source.faces().size(), true);

    if (entity == "points") {
        for (int index : selected) {
            if (index >= 0 && static_cast<size_t>(index) < keep_points.size())
                keep_points[static_cast<size_t>(index)] = false;
        }
        for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
            for (int point_index : source.faces()[face_index]) {
                if (point_index < 0 ||
                    static_cast<size_t>(point_index) >= keep_points.size() ||
                    !keep_points[static_cast<size_t>(point_index)]) {
                    keep_faces[face_index] = false;
                    break;
                }
            }
        }
    } else if (entity == "primitives") {
        for (int index : selected) {
            if (index >= 0 && static_cast<size_t>(index) < keep_faces.size())
                keep_faces[static_cast<size_t>(index)] = false;
        }
    } else if (entity == "edges") {
        for (int edge_id : selected) {
            const auto endpoints = geometry::edge_group_points(static_cast<geometry::GroupId>(edge_id));
            const int a = endpoints[0];
            const int b = endpoints[1];
            if (a >= 0 && static_cast<size_t>(a) < keep_points.size())
                keep_points[static_cast<size_t>(a)] = false;
            if (b >= 0 && static_cast<size_t>(b) < keep_points.size())
                keep_points[static_cast<size_t>(b)] = false;
        }
        for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
            for (int point_index : source.faces()[face_index]) {
                if (point_index < 0 ||
                    static_cast<size_t>(point_index) >= keep_points.size() ||
                    !keep_points[static_cast<size_t>(point_index)]) {
                    keep_faces[face_index] = false;
                    break;
                }
            }
        }
    }

    const bool remove_unused =
        entity == "primitives" && !options.keep_points;
    data::PcgGeometry output =
        rebuild_geometry_delete(source, keep_points, keep_faces, remove_unused);
    if (options.delete_unused_groups)
        remove_empty_groups(output);
    return output;
}

void add_point_data_group_selection(const data::PcgPointData& source,
                                    const std::string& group,
                                    std::unordered_set<int>& selected)
{
    if (group.empty())
        return;

    bool matched_named = false;
    for (size_t index = 0; index < source.points().size(); ++index) {
        const auto& point = source.points()[index];
        if (!point.attributes.contains("groups") || !point.attributes["groups"].is_array())
            continue;
        for (const auto& entry : point.attributes["groups"]) {
            if (entry.is_string() && entry.get<std::string>() == group) {
                selected.insert(static_cast<int>(index));
                matched_named = true;
                break;
            }
        }
    }
    if (matched_named)
        return;

    // Houdini Group field: numeric / range patterns ("0", "0-2", "!*") when no named group hits.
    std::unordered_set<int> pattern_selected;
    if (geometry::compute_element_pattern(group, static_cast<int>(source.points().size()),
                                          pattern_selected)) {
        for (int index : pattern_selected)
            selected.insert(index);
    }
}

void add_spline_point_group_selection(const data::PcgSpline& spline,
                                      const std::string& group,
                                      std::unordered_set<int>& selected)
{
    if (group.empty())
        return;

    bool matched_named = false;
    if (spline.attributes.contains("groups") && spline.attributes["groups"].is_array()) {
        for (const auto& entry : spline.attributes["groups"]) {
            if (!entry.is_string() || entry.get<std::string>() != group)
                continue;
            // Named group on the spline detail selects every point of that spline.
            for (size_t index = 0; index < spline.points.size(); ++index)
                selected.insert(static_cast<int>(index));
            matched_named = true;
            break;
        }
    }
    if (matched_named)
        return;

    // Houdini Group field: numeric / range patterns ("0", "0-2", "!*") as local @ptnum.
    std::unordered_set<int> pattern_selected;
    if (geometry::compute_element_pattern(group, static_cast<int>(spline.points.size()),
                                          pattern_selected)) {
        for (int index : pattern_selected)
            selected.insert(index);
    }
}

data::PcgPointData delete_points(const data::PcgPointData& source,
                                 const DeleteOptions& options,
                                 int graph_seed,
                                 std::string& error)
{
    (void)error;
    if (!has_active_delete_condition(options))
        return source;

    std::unordered_set<int> selected;
    add_point_data_group_selection(source, options.group, selected);
    if (options.number_enable) {
        std::unordered_set<int> number_selected;
        if (options.number_mode == "pattern" && !options.number_pattern.empty())
            geometry::compute_element_pattern(options.number_pattern,
                                              static_cast<int>(source.points().size()),
                                              number_selected);
        else if (options.number_mode == "range")
            geometry::parse_element_range(options.number_range_start, options.number_range_end,
                                          options.number_select_of, options.number_select_offset,
                                          static_cast<int>(source.points().size()),
                                          number_selected);
        for (int index : number_selected)
            selected.insert(index);
    }
    if (options.bounding_enable) {
        for (size_t index = 0; index < source.points().size(); ++index) {
            const auto& p = source.points()[index];
            const data::PcgVec3 vertex{p.x, p.y, p.z};
            if (point_in_bounding(vertex, options))
                selected.insert(static_cast<int>(index));
        }
    }
    if (options.random_enable) {
        for (size_t index = 0; index < source.points().size(); ++index) {
            if (random_selected(graph_seed, options, static_cast<int>(index), 4))
                selected.insert(static_cast<int>(index));
        }
    }

    const int count = static_cast<int>(source.points().size());
    if (options.delete_non_selected && count > 0)
        invert_selection(selected, count);

    data::PcgPointData output;
    output.metadata() = source.metadata();
    for (size_t index = 0; index < source.points().size(); ++index) {
        const bool remove = selected.count(static_cast<int>(index)) > 0;
        if (!remove)
            output.add_point(source.points()[index]);
    }
    return output;
}

data::PcgSplineData delete_splines(const data::PcgSplineData& source,
                                   const DeleteOptions& options,
                                   int graph_seed,
                                   std::string& error)
{
    if (!has_active_delete_condition(options))
        return source;

    if (options.entity == "primitives") {
        const int spline_count = static_cast<int>(source.splines().size());
        std::unordered_set<int> selected;
        add_spline_primitive_selection(source, options, graph_seed, selected, error);
        if (options.delete_non_selected && spline_count > 0)
            invert_selection(selected, spline_count);

        data::PcgSplineData output;
        output.metadata() = source.metadata();
        for (int index = 0; index < spline_count; ++index) {
            if (selected.count(index))
                continue;
            output.add_spline(source.splines()[static_cast<size_t>(index)]);
        }
        return output;
    }

    data::PcgSplineData output;
    output.metadata() = source.metadata();

    for (size_t spline_index = 0; spline_index < source.splines().size(); ++spline_index) {
        const auto& spline = source.splines()[spline_index];
        const int point_count = static_cast<int>(spline.points.size());
        if (point_count <= 0)
            continue;

        // Build the full point selection once per spline (Houdini Group + Number/…).
        std::unordered_set<int> selected;
        add_spline_point_group_selection(spline, options.group, selected);

        if (options.number_enable) {
            std::unordered_set<int> number_selected;
            if (options.number_mode == "pattern" && !options.number_pattern.empty()) {
                geometry::compute_element_pattern(options.number_pattern, point_count,
                                                  number_selected);
            } else if (options.number_mode == "range") {
                geometry::parse_element_range(options.number_range_start,
                                              options.number_range_end,
                                              options.number_select_of,
                                              options.number_select_offset, point_count,
                                              number_selected);
            } else if (options.number_mode == "expression" &&
                       !options.number_expression.empty()) {
                Program program;
                if (!Program::compile_expression(options.number_expression, program, error))
                    return {};
                for (int index = 0; index < point_count; ++index) {
                    DeleteEvalVariables variables;
                    variables.element_number = index;
                    variables.element_count = point_count;
                    const auto& p = spline.points[static_cast<size_t>(index)];
                    variables.x = p.x;
                    variables.y = p.y;
                    variables.z = p.z;
                    bool expression_selected = false;
                    if (!evaluate_number_expression(program, variables, expression_selected,
                                                    error))
                        continue;
                    if (expression_selected)
                        number_selected.insert(index);
                }
            }
            for (int index : number_selected)
                selected.insert(index);
        }

        if (options.bounding_enable) {
            for (int index = 0; index < point_count; ++index) {
                const auto& p = spline.points[static_cast<size_t>(index)];
                const data::PcgVec3 vertex{p.x, p.y, p.z};
                if (point_in_bounding(vertex, options))
                    selected.insert(index);
            }
        }
        if (options.random_enable) {
            for (int index = 0; index < point_count; ++index) {
                if (random_selected(graph_seed, options, index, 6))
                    selected.insert(index);
            }
        }

        if (options.delete_non_selected)
            invert_selection(selected, point_count);

        std::vector<bool> keep(static_cast<size_t>(point_count), true);
        bool removed_any = false;
        for (int index = 0; index < point_count; ++index) {
            if (selected.count(index) == 0)
                continue;
            keep[static_cast<size_t>(index)] = false;
            removed_any = true;
        }

        if (!removed_any) {
            output.add_spline(spline);
            continue;
        }

        std::vector<data::PcgSplinePoint> run;
        if (!spline.closed) {
            for (size_t index = 0; index < spline.points.size(); ++index) {
                if (keep[index])
                    run.push_back(spline.points[index]);
                else
                    append_spline_run(output, spline, run);
            }
            append_spline_run(output, spline, run);
            continue;
        }

        size_t first_removed = 0;
        while (first_removed < keep.size() && keep[first_removed])
            ++first_removed;
        for (size_t step = 1; step <= keep.size(); ++step) {
            const size_t index = (first_removed + step) % keep.size();
            if (keep[index])
                run.push_back(spline.points[index]);
            else
                append_spline_run(output, spline, run);
        }
        append_spline_run(output, spline, run);
    }
    return output;
}

} // namespace pcg::internal::elements
