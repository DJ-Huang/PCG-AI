#include "elements/delete_algorithms.hpp"
#include "elements/attribute_algorithms.hpp"
#include "elements/attribute_elements.hpp"

#include "elements/color_ramp.hpp"
#include "elements/element_utils.hpp"
#include "elements/expression.hpp"
#include "data/pcg_attribute_table.hpp"
#include "geometry/bmesh.hpp"
#include "geometry/group_table.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pcg::internal::elements {
namespace {

using expression::EvalContext;
using expression::Program;

struct WrangleEnvironment {
    std::unordered_map<std::string, double> parameters;
    ColorRampMap ramps;
    std::string group;
    std::string group_type; // points | primitives | edges | guess
    /// Houdini-style op inputs 0..N; index 0 is the main geometry being wrangled.
    std::vector<const data::PcgGeometry*> input_geometries;
};

struct ParamSpec {
    double literal = 0.0;
    bool has_literal = false;
    std::string expr;
};

struct ElementVariables {
    double* x = nullptr;
    double* y = nullptr;
    double* z = nullptr;
    nlohmann::json* attributes = nullptr;
    double curve_u = 0.0;
    int point_number = 0;
    int point_count = 0;
    int primitive_number = 0;
    int primitive_count = 0;
    data::AttributeTable* attribute_table = nullptr;
    data::AttributeOwner attribute_owner = data::AttributeOwner::Point;
    size_t attribute_index = 0;
    data::PcgGeometry* geometry = nullptr;
};

bool read_group_membership(ElementVariables& variables, const std::string& group_name, double& value)
{
    if (!variables.geometry || group_name.empty())
        return false;
    const geometry::GroupDomain domain =
        variables.attribute_owner == data::AttributeOwner::Primitive
            ? geometry::GroupDomain::Face
            : geometry::GroupDomain::Point;
    const geometry::GroupId id = variables.attribute_owner == data::AttributeOwner::Primitive
        ? static_cast<geometry::GroupId>(variables.primitive_number)
        : static_cast<geometry::GroupId>(variables.point_number);
    value = variables.geometry->groups().contains(domain, group_name, id) ? 1.0 : 0.0;
    return true;
}

bool write_group_membership(ElementVariables& variables, const std::string& group_name, double value)
{
    if (!variables.geometry || group_name.empty())
        return false;
    const geometry::GroupDomain domain =
        variables.attribute_owner == data::AttributeOwner::Primitive
            ? geometry::GroupDomain::Face
            : geometry::GroupDomain::Point;
    const geometry::GroupId id = variables.attribute_owner == data::AttributeOwner::Primitive
        ? static_cast<geometry::GroupId>(variables.primitive_number)
        : static_cast<geometry::GroupId>(variables.point_number);
    if (value != 0.0)
        variables.geometry->groups().add(domain, group_name, id);
    else
        variables.geometry->groups().remove(domain, group_name, id);
    return true;
}

bool read_table_attribute(ElementVariables& variables, const std::string& key, double& value)
{
    if (!variables.attribute_table)
        return false;
    auto try_read = [&](data::AttributeOwner owner, size_t index) -> bool {
        const data::AttributeArray* attr = variables.attribute_table->find(owner, key);
        if (!attr || index >= attr->size())
            return false;
        if (attr->schema().type == data::AttributeType::Float) {
            const size_t offset = index *
                                  static_cast<size_t>(std::max(1, attr->schema().tuple_size));
            if (offset >= attr->float_values().size())
                return false;
            value = attr->float_values()[offset];
            return true;
        }
        if (attr->schema().type == data::AttributeType::Int) {
            const size_t offset = index *
                                  static_cast<size_t>(std::max(1, attr->schema().tuple_size));
            if (offset >= attr->int_values().size())
                return false;
            value = static_cast<double>(attr->int_values()[offset]);
            return true;
        }
        return false;
    };
    if (try_read(variables.attribute_owner, variables.attribute_index))
        return true;
    // Houdini-style promotion: point/prim wrangles can read detail attributes.
    if (variables.attribute_owner != data::AttributeOwner::Detail)
        return try_read(data::AttributeOwner::Detail, 0);
    return false;
}

bool write_table_attribute(ElementVariables& variables, const std::string& key, double value)
{
    if (!variables.attribute_table)
        return false;
    data::AttributeArray* attr =
        variables.attribute_table->find(variables.attribute_owner, key);
    if (!attr) {
        attr = &variables.attribute_table->create_float(variables.attribute_owner, key, 1, {0.0});
        const size_t count = variables.attribute_owner == data::AttributeOwner::Detail
            ? 1
            : (variables.attribute_owner == data::AttributeOwner::Primitive
                   ? static_cast<size_t>(std::max(1, variables.primitive_count))
                   : static_cast<size_t>(std::max(1, variables.point_count)));
        attr->resize(count);
    }
    if (attr->schema().type == data::AttributeType::Float) {
        const size_t tuple = static_cast<size_t>(std::max(1, attr->schema().tuple_size));
        const size_t offset = variables.attribute_index * tuple;
        if (offset >= attr->float_values_mut().size())
            attr->resize(variables.attribute_index + 1);
        if (offset < attr->float_values_mut().size()) {
            attr->float_values_mut()[offset] = value;
            return true;
        }
    } else if (attr->schema().type == data::AttributeType::Int) {
        const size_t tuple = static_cast<size_t>(std::max(1, attr->schema().tuple_size));
        const size_t offset = variables.attribute_index * tuple;
        if (offset >= attr->int_values_mut().size())
            attr->resize(variables.attribute_index + 1);
        if (offset < attr->int_values_mut().size()) {
            attr->int_values_mut()[offset] = static_cast<int64_t>(value);
            return true;
        }
    }
    return false;
}

bool read_table_vector(ElementVariables& variables,
                       const std::string& key,
                       std::array<double, 3>& value)
{
    if (!variables.attribute_table)
        return false;
    auto try_read = [&](data::AttributeOwner owner, size_t index) -> bool {
        const data::AttributeArray* attr = variables.attribute_table->find(owner, key);
        if (!attr || attr->schema().type != data::AttributeType::Float ||
            attr->schema().tuple_size < 3 || index >= attr->size())
            return false;
        const size_t offset = index * 3;
        if (offset + 2 >= attr->float_values().size())
            return false;
        value = {attr->float_values()[offset], attr->float_values()[offset + 1],
                 attr->float_values()[offset + 2]};
        return true;
    };
    if (try_read(variables.attribute_owner, variables.attribute_index))
        return true;
    if (variables.attribute_owner != data::AttributeOwner::Detail)
        return try_read(data::AttributeOwner::Detail, 0);
    return false;
}

bool write_table_vector(ElementVariables& variables,
                        const std::string& key,
                        const std::array<double, 3>& value)
{
    if (!variables.attribute_table)
        return false;
    data::AttributeArray* attr =
        variables.attribute_table->find(variables.attribute_owner, key);
    if (!attr) {
        attr = &variables.attribute_table->create_float(variables.attribute_owner, key, 3,
                                                        {0.0, 0.0, 0.0});
        const size_t count = variables.attribute_owner == data::AttributeOwner::Detail
            ? 1
            : (variables.attribute_owner == data::AttributeOwner::Primitive
                   ? static_cast<size_t>(std::max(1, variables.primitive_count))
                   : static_cast<size_t>(std::max(1, variables.point_count)));
        attr->resize(count);
    }
    if (attr->schema().type != data::AttributeType::Float || attr->schema().tuple_size < 3)
        return false;
    const size_t offset = variables.attribute_index * 3;
    if (offset + 2 >= attr->float_values_mut().size())
        attr->resize(variables.attribute_index + 1);
    if (offset + 2 < attr->float_values_mut().size()) {
        attr->float_values_mut()[offset] = value[0];
        attr->float_values_mut()[offset + 1] = value[1];
        attr->float_values_mut()[offset + 2] = value[2];
        return true;
    }
    return false;
}

std::unordered_map<std::string, ParamSpec> parse_parameter_specs(const nlohmann::json& data,
                                                                  std::string& error)
{
    std::unordered_map<std::string, ParamSpec> result;
    if (!data.contains("parameters"))
        return result;

    nlohmann::json parameters;
    const auto& raw = data["parameters"];
    if (raw.is_object()) {
        parameters = raw;
    } else if (raw.is_string()) {
        const std::string text = raw.get<std::string>();
        if (text.empty())
            return result;
        try {
            parameters = nlohmann::json::parse(text);
        } catch (const std::exception& exception) {
            error = std::string("parameters must be a JSON object: ") + exception.what();
            return {};
        }
    } else {
        error = "parameters must be a JSON object or JSON string";
        return {};
    }

    if (!parameters.is_object()) {
        error = "parameters must decode to a JSON object";
        return {};
    }
    for (auto it = parameters.begin(); it != parameters.end(); ++it) {
        ParamSpec spec;
        if (it.value().is_number()) {
            spec.literal = it.value().get<double>();
            spec.has_literal = true;
        } else if (it.value().is_boolean()) {
            spec.literal = it.value().get<bool>() ? 1.0 : 0.0;
            spec.has_literal = true;
        } else if (it.value().is_string()) {
            spec.expr = it.value().get<std::string>();
        } else if (it.value().is_object()) {
            if (it.value().contains("expr") && it.value()["expr"].is_string())
                spec.expr = it.value()["expr"].get<std::string>();
            if (it.value().contains("value") && it.value()["value"].is_number()) {
                spec.literal = it.value()["value"].get<double>();
                spec.has_literal = true;
            } else if (it.value().contains("value") && it.value()["value"].is_boolean()) {
                spec.literal = it.value()["value"].get<bool>() ? 1.0 : 0.0;
                spec.has_literal = true;
            }
            if (spec.expr.empty() && !spec.has_literal) {
                error = "parameter '" + it.key() + "' object needs expr and/or value";
                return {};
            }
        } else {
            error = "parameter '" + it.key() + "' must be numeric, string expr, or {expr,value}";
            return {};
        }
        result[it.key()] = std::move(spec);
    }
    return result;
}

bool read_detail_component(const data::AttributeTable* table,
                           const std::string& name,
                           int component,
                           double& out)
{
    if (!table || name.empty() || component < 0)
        return false;
    const data::AttributeArray* attr = table->find(data::AttributeOwner::Detail, name);
    if (!attr || attr->size() == 0)
        return false;
    const int tuple = std::max(1, attr->schema().tuple_size);
    if (component >= tuple)
        return false;
    if (attr->schema().type == data::AttributeType::Float) {
        const size_t offset = static_cast<size_t>(component);
        if (offset >= attr->float_values().size())
            return false;
        out = attr->float_values()[offset];
        return true;
    }
    if (attr->schema().type == data::AttributeType::Int) {
        const size_t offset = static_cast<size_t>(component);
        if (offset >= attr->int_values().size())
            return false;
        out = static_cast<double>(attr->int_values()[offset]);
        return true;
    }
    return false;
}

bool geometry_bbox_size(const data::PcgGeometry& geometry, std::array<double, 3>& size)
{
    if (geometry.points().empty()) {
        size = {0.0, 0.0, 0.0};
        return true;
    }
    double min_x = geometry.points()[0].x;
    double min_y = geometry.points()[0].y;
    double min_z = geometry.points()[0].z;
    double max_x = min_x;
    double max_y = min_y;
    double max_z = min_z;
    for (const auto& point : geometry.points()) {
        min_x = std::min(min_x, point.x);
        min_y = std::min(min_y, point.y);
        min_z = std::min(min_z, point.z);
        max_x = std::max(max_x, point.x);
        max_y = std::max(max_y, point.y);
        max_z = std::max(max_z, point.z);
    }
    size = {max_x - min_x, max_y - min_y, max_z - min_z};
    return true;
}

void bind_geometry_callbacks(EvalContext& context, const WrangleEnvironment& environment,
                             data::PcgGeometry* current_geometry);

EvalContext make_context(ElementVariables& variables, const WrangleEnvironment& environment);

const data::PcgGeometry* resolve_input_geometry(const WrangleEnvironment& environment,
                                                 int geo_index,
                                                 data::PcgGeometry* current_geometry)
{
    if (geo_index < 0)
        return current_geometry;
    if (geo_index < static_cast<int>(environment.input_geometries.size()))
        return environment.input_geometries[static_cast<size_t>(geo_index)];
    return nullptr;
}

void bind_geometry_callbacks(EvalContext& context, const WrangleEnvironment& environment,
                             data::PcgGeometry* current_geometry)
{
    context.detail = [&environment, current_geometry](int geo_index, const std::string& name,
                                                      int component, double& value) {
        const data::PcgGeometry* geo =
            resolve_input_geometry(environment, geo_index, current_geometry);
        if (!geo)
            return false;
        return read_detail_component(&geo->attributes(), name, component, value);
    };
    context.nedgesgroup = [&environment, current_geometry](int geo_index, const std::string& group,
                                                           double& value) {
        const data::PcgGeometry* geo =
            resolve_input_geometry(environment, geo_index < 0 ? 0 : geo_index, current_geometry);
        if (!geo) {
            // Missing/empty secondary input → 0 edges (Houdini-friendly empty).
            value = 0.0;
            return true;
        }
        value = static_cast<double>(
            geo->groups().members(geometry::GroupDomain::Edge, group).size());
        return true;
    };
    context.getbbox_size = [&environment, current_geometry](int geo_index,
                                                            std::array<double, 3>& size) {
        const data::PcgGeometry* geo =
            resolve_input_geometry(environment, geo_index < 0 ? 0 : geo_index, current_geometry);
        if (!geo) {
            // Missing/empty secondary input → zero bbox instead of hard fail.
            size = {0.0, 0.0, 0.0};
            return true;
        }
        return geometry_bbox_size(*geo, size);
    };
}

bool resolve_parameters(const std::unordered_map<std::string, ParamSpec>& specs,
                       WrangleEnvironment& environment,
                       std::string& error)
{
    environment.parameters.clear();
    const data::PcgGeometry* geometry =
        environment.input_geometries.empty() ? nullptr : environment.input_geometries[0];
    for (const auto& [name, spec] : specs) {
        if (spec.expr.empty()) {
            if (!spec.has_literal) {
                error = "parameter '" + name + "' has no value";
                return false;
            }
            environment.parameters[name] = spec.literal;
            continue;
        }

        Program program;
        if (!Program::compile_expression(spec.expr, program, error)) {
            error = "parameter '" + name + "' expr parse error: " + error;
            return false;
        }

        ElementVariables variables{nullptr, nullptr, nullptr, nullptr,
                                   0.0, 0, geometry ? static_cast<int>(geometry->points().size()) : 0,
                                   0, geometry ? static_cast<int>(geometry->faces().size()) : 0,
                                   geometry ? const_cast<data::AttributeTable*>(&geometry->attributes())
                                            : nullptr,
                                   data::AttributeOwner::Detail, 0};
        variables.geometry = const_cast<data::PcgGeometry*>(geometry);
        EvalContext context = make_context(variables, environment);
        double value = 0.0;
        if (!program.evaluate(context, value, error)) {
            error = "parameter '" + name + "' expr failed: " + error;
            return false;
        }
        environment.parameters[name] = value;
    }
    return true;
}

WrangleEnvironment build_wrangle_environment(const nlohmann::json& data,
                                             std::vector<const data::PcgGeometry*> input_geometries,
                                             std::string& error)
{
    WrangleEnvironment environment;
    environment.group = data.value("group", std::string());
    environment.group_type = data.value("groupType", std::string("guess"));
    environment.input_geometries = std::move(input_geometries);
    const auto specs = parse_parameter_specs(data, error);
    if (!error.empty())
        return {};
    if (!resolve_parameters(specs, environment, error))
        return {};
    if (!parse_color_ramps(data, environment.ramps, error))
        return {};
    return environment;
}

bool json_number(const nlohmann::json& value, double& out)
{
    if (value.is_number()) {
        out = value.get<double>();
        return true;
    }
    if (value.is_boolean()) {
        out = value.get<bool>() ? 1.0 : 0.0;
        return true;
    }
    return false;
}

EvalContext make_context(ElementVariables& variables, const WrangleEnvironment& environment)
{
    EvalContext context;
    context.parameters = environment.parameters;
    context.ramps = environment.ramps;
    context.read_variable = [&variables](const std::string& name, double& value) {
        if (name == "@P.x" && variables.x) value = *variables.x;
        else if (name == "@P.y" && variables.y) value = *variables.y;
        else if (name == "@P.z" && variables.z) value = *variables.z;
        else if (name == "@curveu") value = variables.curve_u;
        else if (name == "@ptnum") value = variables.point_number;
        else if (name == "@numpt") value = variables.point_count;
        else if (name == "@primnum") value = variables.primitive_number;
        else if (name == "@numprim") value = variables.primitive_count;
        else if (name.rfind("@group.", 0) == 0)
            return read_group_membership(variables, name.substr(7), value);
        else if (name.size() > 1 && name.front() == '@') {
            const std::string key = name.substr(1);
            if (variables.attributes && variables.attributes->contains(key) &&
                json_number((*variables.attributes)[key], value))
                return true;
            return read_table_attribute(variables, key, value);
        } else {
            return false;
        }
        return true;
    };
    context.write_variable = [&variables](const std::string& name, double value) {
        if (name == "@P.x" && variables.x) *variables.x = value;
        else if (name == "@P.y" && variables.y) *variables.y = value;
        else if (name == "@P.z" && variables.z) *variables.z = value;
        else if (name.rfind("@group.", 0) == 0)
            return write_group_membership(variables, name.substr(7), value);
        else if (variables.attributes && name.size() > 1 && name.front() == '@' &&
                 name != "@curveu" && name != "@ptnum" && name != "@numpt" &&
                 name != "@primnum" && name != "@numprim") {
            (*variables.attributes)[name.substr(1)] = value;
        } else if (name.size() > 1 && name.front() == '@' &&
                   name != "@curveu" && name != "@ptnum" && name != "@numpt" &&
                   name != "@primnum" && name != "@numprim") {
            return write_table_attribute(variables, name.substr(1), value);
        } else {
            return false;
        }
        return true;
    };
    context.read_vector = [&variables](const std::string& name, std::array<double, 3>& value) {
        if (name.size() <= 1 || name.front() != '@')
            return false;
        return read_table_vector(variables, name.substr(1), value);
    };
    context.write_vector = [&variables](const std::string& name,
                                        const std::array<double, 3>& value) {
        if (name.size() <= 1 || name.front() != '@')
            return false;
        return write_table_vector(variables, name.substr(1), value);
    };
    bind_geometry_callbacks(context, environment, variables.geometry);
    return context;
}

std::vector<double> spline_curve_u(const data::PcgSpline& spline)
{
    std::vector<double> result(spline.points.size(), 0.0);
    if (spline.points.size() < 2)
        return result;
    double length = 0.0;
    for (size_t i = 1; i < spline.points.size(); ++i) {
        const double dx = spline.points[i].x - spline.points[i - 1].x;
        const double dy = spline.points[i].y - spline.points[i - 1].y;
        const double dz = spline.points[i].z - spline.points[i - 1].z;
        length += std::sqrt(dx * dx + dy * dy + dz * dz);
        result[i] = length;
    }
    if (length > 0.000000000001) {
        for (double& value : result)
            value /= length;
    } else {
        const double denominator = static_cast<double>(spline.points.size() - 1);
        for (size_t i = 0; i < result.size(); ++i)
            result[i] = static_cast<double>(i) / denominator;
    }
    return result;
}

double indexed_curve_u(size_t index, size_t count)
{
    return count <= 1 ? 0.0 : static_cast<double>(index) / static_cast<double>(count - 1);
}

bool evaluate_selection(const Program* program,
                        ElementVariables& variables,
                        const WrangleEnvironment& environment,
                        bool& selected,
                        std::string& error)
{
    if (!program) {
        selected = true;
        return true;
    }
    EvalContext context = make_context(variables, environment);
    double value = 0.0;
    if (!program->evaluate(context, value, error))
        return false;
    selected = value != 0.0;
    return true;
}

bool apply_wrangle_points(data::PcgPointData& points,
                          const Program& program,
                          const WrangleEnvironment& environment,
                          std::string& error)
{
    const int count = static_cast<int>(points.points().size());
    for (int index = 0; index < count; ++index) {
        auto& point = points.points_mut()[static_cast<size_t>(index)];
        double curve_u = indexed_curve_u(static_cast<size_t>(index), points.points().size());
        if (point.attributes.contains("curveu"))
            json_number(point.attributes["curveu"], curve_u);
        ElementVariables variables{&point.x, &point.y, &point.z, &point.attributes,
                                   curve_u, index, count, 0, 0};
        EvalContext context = make_context(variables, environment);
        if (!program.execute(context, error)) {
            error = "point " + std::to_string(index) + ": " + error;
            return false;
        }
    }
    return true;
}

bool apply_wrangle_splines(data::PcgSplineData& splines,
                           const Program& program,
                           const WrangleEnvironment& environment,
                           std::string& error)
{
    for (size_t spline_index = 0; spline_index < splines.splines().size(); ++spline_index) {
        auto& spline = splines.splines_mut()[spline_index];
        const auto curve_u = spline_curve_u(spline);
        const int count = static_cast<int>(spline.points.size());
        for (int index = 0; index < count; ++index) {
            auto& point = spline.points[static_cast<size_t>(index)];
            ElementVariables variables{&point.x, &point.y, &point.z, nullptr,
                                       curve_u[static_cast<size_t>(index)], index, count,
                                       static_cast<int>(spline_index),
                                       static_cast<int>(splines.splines().size())};
            EvalContext context = make_context(variables, environment);
            if (!program.execute(context, error)) {
                error = "spline " + std::to_string(spline_index) + " point " +
                        std::to_string(index) + ": " + error;
                return false;
            }
        }
    }
    return true;
}

bool apply_wrangle_geometry(data::PcgGeometry& geometry,
                            const Program& program,
                            const WrangleEnvironment& environment,
                            std::string& error)
{
    const int count = static_cast<int>(geometry.points().size());
    std::unordered_set<geometry::GroupId> selected;
    if (!environment.group.empty())
        selected = geometry.groups().eval_indices(geometry::GroupDomain::Point,
                                                  environment.group, count);
    for (int index = 0; index < count; ++index) {
        if (!environment.group.empty() &&
            selected.find(static_cast<geometry::GroupId>(index)) == selected.end())
            continue;
        auto& point = geometry.points_mut()[static_cast<size_t>(index)];
        ElementVariables variables{&point.x, &point.y, &point.z, nullptr,
                                   indexed_curve_u(static_cast<size_t>(index), geometry.points().size()),
                                   index, count, 0, static_cast<int>(geometry.faces().size()),
                                   &geometry.attributes(), data::AttributeOwner::Point,
                                   static_cast<size_t>(index)};
        variables.geometry = &geometry;
        EvalContext context = make_context(variables, environment);
        if (!program.execute(context, error)) {
            error = "geometry point " + std::to_string(index) + ": " + error;
            return false;
        }
    }
    return true;
}

bool apply_wrangle_geometry_primitives(data::PcgGeometry& geometry,
                                       const Program& program,
                                       const WrangleEnvironment& environment,
                                       std::string& error)
{
    const int prim_count = static_cast<int>(geometry.faces().size());
    const int point_count = static_cast<int>(geometry.points().size());
    std::unordered_set<geometry::GroupId> selected;
    if (!environment.group.empty())
        selected = geometry.groups().eval_indices(geometry::GroupDomain::Face,
                                                  environment.group, prim_count);
    for (int prim = 0; prim < prim_count; ++prim) {
        if (!environment.group.empty() &&
            selected.find(static_cast<geometry::GroupId>(prim)) == selected.end())
            continue;
        const auto& face = geometry.faces()[static_cast<size_t>(prim)];
        if (face.empty())
            continue;
        data::PcgVec3 center{};
        for (int pi : face) {
            const auto& p = geometry.points()[static_cast<size_t>(pi)];
            center.x += p.x;
            center.y += p.y;
            center.z += p.z;
        }
        const double inv = 1.0 / static_cast<double>(face.size());
        center.x *= inv;
        center.y *= inv;
        center.z *= inv;
        const data::PcgVec3 old_center = center;
        ElementVariables variables{&center.x, &center.y, &center.z, nullptr,
                                   0.0, 0, point_count, prim, prim_count,
                                   &geometry.attributes(), data::AttributeOwner::Primitive,
                                   static_cast<size_t>(prim)};
        variables.geometry = &geometry;
        EvalContext context = make_context(variables, environment);
        if (!program.execute(context, error)) {
            error = "geometry prim " + std::to_string(prim) + ": " + error;
            return false;
        }
        const double dx = center.x - old_center.x;
        const double dy = center.y - old_center.y;
        const double dz = center.z - old_center.z;
        if (dx != 0.0 || dy != 0.0 || dz != 0.0) {
            for (int pi : face) {
                auto& p = geometry.points_mut()[static_cast<size_t>(pi)];
                p.x += dx;
                p.y += dy;
                p.z += dz;
            }
        }
    }
    return true;
}

bool apply_wrangle_geometry_detail(data::PcgGeometry& geometry,
                                   const Program& program,
                                   const WrangleEnvironment& environment,
                                   std::string& error)
{
    ElementVariables variables{nullptr, nullptr, nullptr, nullptr,
                               0.0, 0, static_cast<int>(geometry.points().size()),
                               0, static_cast<int>(geometry.faces().size()),
                               &geometry.attributes(), data::AttributeOwner::Detail, 0};
    variables.geometry = &geometry;
    EvalContext context = make_context(variables, environment);
    if (!program.execute(context, error)) {
        error = std::string("geometry detail: ") + error;
        return false;
    }
    return true;
}

data::PcgMeshData mesh_without_stale_normals(const data::PcgMeshData& source)
{
    data::PcgMeshData output;
    output.vertices_mut() = source.vertices();
    output.triangles_mut() = source.triangles();
    output.metadata() = source.metadata();
    if (source.has_colors()) output.set_colors(source.colors());
    if (source.has_uvs()) output.set_uvs(source.uvs());
    if (source.has_materials())
        output.set_materials(source.material_slots(), source.triangle_materials());
    return output;
}

bool apply_wrangle_mesh(data::PcgMeshData& mesh,
                        const Program& program,
                        const WrangleEnvironment& environment,
                        std::string& error)
{
    const int count = static_cast<int>(mesh.vertices().size());
    for (int index = 0; index < count; ++index) {
        auto& point = mesh.vertices_mut()[static_cast<size_t>(index)];
        ElementVariables variables{&point.x, &point.y, &point.z, nullptr,
                                   indexed_curve_u(static_cast<size_t>(index), mesh.vertices().size()),
                                   index, count, 0,
                                   static_cast<int>(mesh.triangles().size() / 3)};
        EvalContext context = make_context(variables, environment);
        if (!program.execute(context, error)) {
            error = "mesh point " + std::to_string(index) + ": " + error;
            return false;
        }
    }
    return true;
}

bool point_group_selected(const nlohmann::json& attributes, const std::string& group)
{
    if (group.empty())
        return true;
    if (!attributes.contains(group))
        return false;
    double value = 0.0;
    if (json_number(attributes[group], value))
        return value != 0.0;
    return attributes[group].is_string() && !attributes[group].get<std::string>().empty();
}

data::PcgPointData blast_points(const data::PcgPointData& source,
                                const std::string& group,
                                const Program* program,
                                const WrangleEnvironment& environment,
                                bool delete_non_selected,
                                std::string& error)
{
    data::PcgPointData output;
    output.metadata() = source.metadata();
    const int count = static_cast<int>(source.points().size());
    for (int index = 0; index < count; ++index) {
        const auto& point = source.points()[static_cast<size_t>(index)];
        auto attrs = point.attributes;
        double x = point.x;
        double y = point.y;
        double z = point.z;
        double curve_u = indexed_curve_u(static_cast<size_t>(index), source.points().size());
        if (attrs.contains("curveu")) json_number(attrs["curveu"], curve_u);
        ElementVariables variables{&x, &y, &z, &attrs, curve_u, index, count, 0, 0};
        bool expression_selected = true;
        if (!evaluate_selection(program, variables, environment, expression_selected, error)) {
            error = "point " + std::to_string(index) + ": " + error;
            return {};
        }
        const bool selected = point_group_selected(point.attributes, group) && expression_selected;
        const bool remove = delete_non_selected ? !selected : selected;
        if (!remove)
            output.add_point(point);
    }
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

data::PcgSplineData blast_splines(const data::PcgSplineData& source,
                                  const std::string& group,
                                  const Program* program,
                                  const WrangleEnvironment& environment,
                                  bool delete_non_selected,
                                  std::string& error)
{
    data::PcgSplineData output;
    output.metadata() = source.metadata();
    for (size_t spline_index = 0; spline_index < source.splines().size(); ++spline_index) {
        const auto& spline = source.splines()[spline_index];
        const auto curve_u = spline_curve_u(spline);
        std::vector<bool> keep(spline.points.size(), true);
        bool removed_any = false;
        for (size_t index = 0; index < spline.points.size(); ++index) {
            double x = spline.points[index].x;
            double y = spline.points[index].y;
            double z = spline.points[index].z;
            ElementVariables variables{&x, &y, &z, nullptr, curve_u[index],
                                       static_cast<int>(index), static_cast<int>(spline.points.size()),
                                       static_cast<int>(spline_index),
                                       static_cast<int>(source.splines().size())};
            bool expression_selected = true;
            if (!evaluate_selection(program, variables, environment, expression_selected, error)) {
                error = "spline " + std::to_string(spline_index) + " point " +
                        std::to_string(index) + ": " + error;
                return {};
            }
            const bool group_selected = group.empty() || point_group_selected(spline.attributes, group);
            const bool selected = group_selected && expression_selected;
            const bool remove = delete_non_selected ? !selected : selected;
            keep[index] = !remove;
            removed_any = removed_any || remove;
        }

        if (!removed_any) {
            output.add_spline(spline);
            continue;
        }

        std::vector<data::PcgSplinePoint> run;
        if (!spline.closed) {
            for (size_t index = 0; index < spline.points.size(); ++index) {
                if (keep[index]) run.push_back(spline.points[index]);
                else append_spline_run(output, spline, run);
            }
            append_spline_run(output, spline, run);
            continue;
        }

        size_t first_removed = 0;
        while (first_removed < keep.size() && keep[first_removed]) ++first_removed;
        for (size_t step = 1; step <= keep.size(); ++step) {
            const size_t index = (first_removed + step) % keep.size();
            if (keep[index]) run.push_back(spline.points[index]);
            else append_spline_run(output, spline, run);
        }
        append_spline_run(output, spline, run);
    }
    return output;
}

data::PcgGeometry rebuild_geometry(const data::PcgGeometry& source,
                                   const std::vector<bool>& initial_keep_points,
                                   const std::vector<bool>& keep_faces,
                                   bool remove_unused_points)
{
    std::vector<bool> keep_points = initial_keep_points;
    if (remove_unused_points) {
        std::fill(keep_points.begin(), keep_points.end(), false);
        for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
            if (!keep_faces[face_index]) continue;
            for (int point_index : source.faces()[face_index]) {
                if (point_index >= 0 && static_cast<size_t>(point_index) < keep_points.size() &&
                    initial_keep_points[static_cast<size_t>(point_index)])
                    keep_points[static_cast<size_t>(point_index)] = true;
            }
        }
    }

    data::PcgGeometry output;
    data::GeometryElementRemap topology_remap;
    std::vector<int> point_remap(source.points().size(), -1);
    for (size_t index = 0; index < source.points().size(); ++index) {
        if (!keep_points[index]) continue;
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
        if (!keep_faces[face_index]) continue;
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
        if (!valid || face.size() < 3) continue;
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

    for (const auto& name : source.groups().group_names(geometry::GroupDomain::Point)) {
        for (int old_index : source.groups().members(geometry::GroupDomain::Point, name)) {
            if (old_index >= 0 && static_cast<size_t>(old_index) < point_remap.size() &&
                point_remap[static_cast<size_t>(old_index)] >= 0)
                output.groups().add(geometry::GroupDomain::Point, name,
                                    point_remap[static_cast<size_t>(old_index)]);
        }
    }
    for (const auto& name : source.groups().group_names(geometry::GroupDomain::Face)) {
        for (int old_index : source.groups().members(geometry::GroupDomain::Face, name)) {
            if (old_index >= 0 && static_cast<size_t>(old_index) < face_remap.size() &&
                face_remap[static_cast<size_t>(old_index)] >= 0)
                output.groups().add(geometry::GroupDomain::Face, name,
                                    face_remap[static_cast<size_t>(old_index)]);
        }
    }
    for (const auto& name : source.groups().group_names(geometry::GroupDomain::Edge)) {
        for (geometry::GroupId old_edge :
             source.groups().members(geometry::GroupDomain::Edge, name)) {
            const auto endpoints = geometry::edge_group_points(old_edge);
            const int old_a = endpoints[0];
            const int old_b = endpoints[1];
            if (old_a < 0 || old_b < 0 || static_cast<size_t>(old_a) >= point_remap.size() ||
                static_cast<size_t>(old_b) >= point_remap.size())
                continue;
            const int a = point_remap[static_cast<size_t>(old_a)];
            const int b = point_remap[static_cast<size_t>(old_b)];
            if (a >= 0 && b >= 0)
                output.groups().add(geometry::GroupDomain::Edge, name,
                                    geometry::edge_group_id(a, b));
        }
    }

    if (source.has_colors() && source.colors().size() == source.points().size()) {
        std::vector<data::PcgColor> colors;
        colors.reserve(output.points().size());
        for (size_t index = 0; index < source.colors().size(); ++index)
            if (keep_points[index]) colors.push_back(source.colors()[index]);
        output.set_colors(std::move(colors));
    }
    if (source.has_uvs() && source.uvs().size() == source.points().size()) {
        std::vector<data::PcgVec2> uvs;
        uvs.reserve(output.points().size());
        for (size_t index = 0; index < source.uvs().size(); ++index)
            if (keep_points[index]) uvs.push_back(source.uvs()[index]);
        output.set_uvs(std::move(uvs));
    }
    if (source.has_corner_uvs()) {
        std::vector<data::PcgVec2> corner_uvs;
        corner_uvs.reserve(static_cast<size_t>(output.corner_count()));
        size_t corner_cursor = 0;
        for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
            const size_t face_corners = source.faces()[face_index].size();
            if (keep_faces[face_index] && face_remap[face_index] >= 0) {
                for (size_t c = 0; c < face_corners; ++c) {
                    if (corner_cursor + c < source.corner_uvs().size())
                        corner_uvs.push_back(source.corner_uvs()[corner_cursor + c]);
                    else
                        corner_uvs.push_back(data::PcgVec2{0.0, 0.0});
                }
            }
            corner_cursor += face_corners;
        }
        output.set_corner_uvs(std::move(corner_uvs));
    }
    if (source.has_face_materials()) output.set_face_materials(std::move(face_materials));
    else if (source.has_material()) output.set_material_name(source.material_name());
    return output;
}

data::PcgGeometry blast_geometry(const data::PcgGeometry& source,
                                 const std::string& entity,
                                 const std::string& group,
                                 const Program* program,
                                 const WrangleEnvironment& environment,
                                 bool delete_non_selected,
                                 bool remove_unused_points,
                                 std::string& error)
{
    const bool primitives = entity == "primitives";
    std::vector<bool> keep_points(source.points().size(), true);
    std::vector<bool> keep_faces(source.faces().size(), true);

    if (primitives) {
        const auto group_members = group.empty()
            ? std::unordered_set<geometry::GroupId>{}
            : source.groups().eval(geometry::GroupDomain::Face, group);
        for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
            double x = 0.0, y = 0.0, z = 0.0;
            const auto& face = source.faces()[face_index];
            for (int point_index : face) {
                if (point_index < 0 || static_cast<size_t>(point_index) >= source.points().size()) continue;
                const auto& point = source.points()[static_cast<size_t>(point_index)];
                x += point.x; y += point.y; z += point.z;
            }
            if (!face.empty()) {
                const double inverse = 1.0 / static_cast<double>(face.size());
                x *= inverse; y *= inverse; z *= inverse;
            }
            ElementVariables variables{&x, &y, &z, nullptr,
                                       indexed_curve_u(face_index, source.faces().size()),
                                       0, static_cast<int>(source.points().size()),
                                       static_cast<int>(face_index),
                                       static_cast<int>(source.faces().size()),
                                       const_cast<data::AttributeTable*>(&source.attributes()),
                                       data::AttributeOwner::Primitive,
                                       face_index};
            bool expression_selected = true;
            if (!evaluate_selection(program, variables, environment, expression_selected, error)) {
                error = "primitive " + std::to_string(face_index) + ": " + error;
                return {};
            }
            const bool group_selected = group.empty() ||
                group_members.count(static_cast<int>(face_index)) > 0;
            const bool selected = group_selected && expression_selected;
            const bool remove = delete_non_selected ? !selected : selected;
            keep_faces[face_index] = !remove;
        }
    } else {
        const auto group_members = group.empty()
            ? std::unordered_set<geometry::GroupId>{}
            : source.groups().eval(geometry::GroupDomain::Point, group);
        for (size_t point_index = 0; point_index < source.points().size(); ++point_index) {
            const auto& source_point = source.points()[point_index];
            double x = source_point.x, y = source_point.y, z = source_point.z;
            ElementVariables variables{&x, &y, &z, nullptr,
                                       indexed_curve_u(point_index, source.points().size()),
                                       static_cast<int>(point_index),
                                       static_cast<int>(source.points().size()), 0,
                                       static_cast<int>(source.faces().size()),
                                       const_cast<data::AttributeTable*>(&source.attributes()),
                                       data::AttributeOwner::Point,
                                       point_index};
            bool expression_selected = true;
            if (!evaluate_selection(program, variables, environment, expression_selected, error)) {
                error = "geometry point " + std::to_string(point_index) + ": " + error;
                return {};
            }
            const bool group_selected = group.empty() ||
                group_members.count(static_cast<int>(point_index)) > 0;
            const bool selected = group_selected && expression_selected;
            const bool remove = delete_non_selected ? !selected : selected;
            keep_points[point_index] = !remove;
        }
        for (size_t face_index = 0; face_index < source.faces().size(); ++face_index) {
            for (int point_index : source.faces()[face_index]) {
                if (point_index < 0 || static_cast<size_t>(point_index) >= keep_points.size() ||
                    !keep_points[static_cast<size_t>(point_index)]) {
                    keep_faces[face_index] = false;
                    break;
                }
            }
        }
    }

    return rebuild_geometry(source, keep_points, keep_faces,
                            primitives && remove_unused_points);
}

bool read_split_bool(const nlohmann::json& data, const char* key, bool default_value)
{
    if (!data.contains(key))
        return default_value;
    const auto& value = data.at(key);
    if (value.is_boolean())
        return value.get<bool>();
    if (value.is_number_integer())
        return value.get<int>() != 0;
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        if (text == "true" || text == "1")
            return true;
        if (text == "false" || text == "0")
            return false;
    }
    return default_value;
}

std::string read_split_group_type(const nlohmann::json& data)
{
    if (data.contains("groupType") && data["groupType"].is_string()) {
        const auto value = data["groupType"].get<std::string>();
        if (!value.empty())
            return value;
    }
    // Legacy PCG Split used `entity` instead of Houdini `groupType`.
    if (data.contains("entity") && data["entity"].is_string()) {
        const auto value = data["entity"].get<std::string>();
        if (!value.empty())
            return value;
    }
    return "guess";
}

std::string resolve_split_entity(const data::PcgGeometry* geometry,
                                 const std::string& group,
                                 const std::string& group_type)
{
    if (group_type == "points")
        return "points";
    if (group_type == "primitives")
        return "primitives";
    // Guess from Group (Houdini default).
    if (geometry && !group.empty()) {
        if (geometry->groups().has_group(geometry::GroupDomain::Face, group))
            return "primitives";
        if (geometry->groups().has_group(geometry::GroupDomain::Point, group))
            return "points";
    }
    return "primitives";
}

void apply_split_unused_groups(data::PcgGeometry& output,
                               const data::PcgGeometry& source,
                               bool delete_unused_groups)
{
    if (delete_unused_groups) {
        remove_empty_groups(output);
        return;
    }
    // Houdini default (off): keep empty group names that existed on the source.
    for (const auto domain : {geometry::GroupDomain::Point, geometry::GroupDomain::Edge,
                              geometry::GroupDomain::Face, geometry::GroupDomain::Vertex}) {
        for (const auto& name : source.groups().group_names(domain)) {
            if (!output.groups().has_group(domain, name))
                output.groups().ensure_group(domain, name);
        }
    }
}

class AttributeWrangleElement final : public IPcgElement {
public:
    const char* type_name() const override { return "AttributeWrangle"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AttributeWrangle missing node");
        const data::PcgTaggedData* input = ctx.inputs.find("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "AttributeWrangle missing input");
        const std::string run_over = ctx.node->data.value("runOver", std::string("points"));
        if (run_over != "points" && run_over != "primitives" && run_over != "detail")
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "AttributeWrangle runOver must be points, primitives, or detail");

        const std::string source = ctx.node->data.value("expression", std::string());
        Program program;
        std::string error;
        if (!Program::compile_statements(source, program, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            ("AttributeWrangle parse error: " + error).c_str());
        // Houdini-style inputs 0–3: pin in/in1/in2/in3 (mesh pins convert to geometry).
        data::PcgGeometry input_storage[4];
        static constexpr const char* kInputPins[] = {"in", "in1", "in2", "in3"};
        std::vector<const data::PcgGeometry*> input_geometries(4, nullptr);
        for (int i = 0; i < 4; ++i)
            input_geometries[static_cast<size_t>(i)] =
                optional_geometry_input(ctx, kInputPins[i], input_storage[i]);
        const auto environment =
            build_wrangle_environment(ctx.node->data, std::move(input_geometries), error);
        if (!error.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            ("AttributeWrangle " + error).c_str());

        if (run_over != "points" && !input->geometry)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "AttributeWrangle primitives/detail require Geometry input");

        if (input->points) {
            if (run_over != "points")
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                "AttributeWrangle Point input only supports runOver=points");
            data::PcgPointData output = *input->points;
            if (!apply_wrangle_points(output, program, environment, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("AttributeWrangle " + error).c_str());
            emit_points(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->splines) {
            if (run_over != "points")
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                "AttributeWrangle Spline input only supports runOver=points");
            data::PcgSplineData output = *input->splines;
            if (!apply_wrangle_splines(output, program, environment, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("AttributeWrangle " + error).c_str());
            emit_splines(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->geometry) {
            data::PcgGeometry output = *input->geometry;
            bool ok = false;
            if (run_over == "primitives")
                ok = apply_wrangle_geometry_primitives(output, program, environment, error);
            else if (run_over == "detail")
                ok = apply_wrangle_geometry_detail(output, program, environment, error);
            else
                ok = apply_wrangle_geometry(output, program, environment, error);
            if (!ok)
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("AttributeWrangle " + error).c_str());
            emit_geometry(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->mesh) {
            if (run_over != "points")
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                "AttributeWrangle Mesh input only supports runOver=points");
            data::PcgMeshData output = mesh_without_stale_normals(*input->mesh);
            if (!apply_wrangle_mesh(output, program, environment, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("AttributeWrangle " + error).c_str());
            emit_mesh(ctx, std::move(output));
            return PCG_OK;
        }
        return fail_ctx(ctx, PCG_ERR_EXECUTION,
                        "AttributeWrangle supports Point, Spline, Mesh, or Geometry input");
    }
};

class BlastElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Blast"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Blast missing node");
        const data::PcgTaggedData* input = ctx.inputs.find("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Blast missing input");

        const std::string entity = ctx.node->data.value("entity", std::string("points"));
        const std::string group = ctx.node->data.value("group", std::string());
        const std::string source = ctx.node->data.value("expression", std::string());
        const bool delete_non_selected = ctx.node->data.value("deleteNonSelected", false);
        const bool remove_unused_points = ctx.node->data.value("removeUnusedPoints", true);

        Program program;
        Program* program_ptr = nullptr;
        std::string error;
        if (!source.empty()) {
            if (!Program::compile_expression(source, program, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("Blast parse error: " + error).c_str());
            program_ptr = &program;
        }
        if (group.empty() && !program_ptr)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "Blast requires a group or selection expression");
        data::PcgGeometry input_storage;
        const auto environment = build_wrangle_environment(
            ctx.node->data,
            {optional_geometry_input(ctx, "in", input_storage)},
            error);
        if (!error.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Blast " + error).c_str());

        if (input->points) {
            auto output = blast_points(*input->points, group, program_ptr, environment,
                                       delete_non_selected, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Blast " + error).c_str());
            emit_points(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->splines) {
            auto output = blast_splines(*input->splines, group, program_ptr, environment,
                                        delete_non_selected, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Blast " + error).c_str());
            emit_splines(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->geometry) {
            auto output = blast_geometry(*input->geometry, entity, group, program_ptr, environment,
                                         delete_non_selected, remove_unused_points, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Blast " + error).c_str());
            emit_geometry(ctx, std::move(output));
            return PCG_OK;
        }
        return fail_ctx(ctx, PCG_ERR_EXECUTION,
                        "Blast supports Point, Spline, or Geometry input");
    }
};

class DeleteElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Delete"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Delete missing node");
        const data::PcgTaggedData* input = ctx.inputs.find("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Delete missing input");

        const DeleteOptions options = parse_delete_options(ctx.node->data);
        std::string error;

        if (input->points) {
            auto output = delete_points(*input->points, options, ctx.graph_seed, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Delete " + error).c_str());
            emit_points(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->splines) {
            auto output = delete_splines(*input->splines, options, ctx.graph_seed, error);
            emit_splines(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->geometry) {
            auto output = delete_geometry(*input->geometry, options, ctx.graph_seed, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Delete " + error).c_str());
            emit_geometry(ctx, std::move(output));
            return PCG_OK;
        }
        return fail_ctx(ctx, PCG_ERR_EXECUTION,
                        "Delete supports Point, Spline, or Geometry input");
    }
};

class SplitElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Split"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Split missing node");
        const data::PcgTaggedData* input = ctx.inputs.find("in");
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Split missing input");

        const std::string group = ctx.node->data.value("group", std::string());
        const std::string group_type = read_split_group_type(ctx.node->data);
        // Optional legacy Blast-style expression (not in Houdini Split UI).
        const std::string source = ctx.node->data.value("expression", std::string());
        const bool invert_selection =
            read_split_bool(ctx.node->data, "invertSelection", false);
        const bool delete_unused_groups =
            read_split_bool(ctx.node->data, "deleteUnusedGroups", false);
        // Houdini Split always drops unused points when splitting primitives.
        // Legacy graphs may still carry removeUnusedPoints.
        const bool remove_unused_points =
            read_split_bool(ctx.node->data, "removeUnusedPoints", true);

        Program program;
        Program* program_ptr = nullptr;
        std::string error;
        if (!source.empty()) {
            if (!Program::compile_expression(source, program, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("Split parse error: " + error).c_str());
            program_ptr = &program;
        }
        // Empty group = select all (Houdini). Expression remains optional.
        data::PcgGeometry input_storage;
        const auto environment = build_wrangle_environment(
            ctx.node->data,
            {optional_geometry_input(ctx, "in", input_storage)},
            error);
        if (!error.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Split " + error).c_str());

        // Houdini Split: first output = selection (after Invert), second = complement.
        if (input->points) {
            auto selected = blast_points(*input->points, group, program_ptr, environment,
                                         true, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Split " + error).c_str());
            auto remainder = blast_points(*input->points, group, program_ptr, environment,
                                          false, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Split " + error).c_str());
            if (invert_selection)
                std::swap(selected, remainder);
            ctx.outputs.add_points("out", std::move(selected));
            ctx.outputs.add_points("rest", std::move(remainder));
            return PCG_OK;
        }
        if (input->splines) {
            auto selected = blast_splines(*input->splines, group, program_ptr, environment,
                                          true, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Split " + error).c_str());
            auto remainder = blast_splines(*input->splines, group, program_ptr, environment,
                                           false, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Split " + error).c_str());
            if (invert_selection)
                std::swap(selected, remainder);
            ctx.outputs.add_splines("out", std::move(selected));
            ctx.outputs.add_splines("rest", std::move(remainder));
            return PCG_OK;
        }
        if (input->geometry) {
            const std::string entity =
                resolve_split_entity(input->geometry.get(), group, group_type);
            auto selected = blast_geometry(*input->geometry, entity, group, program_ptr,
                                           environment, true, remove_unused_points, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Split " + error).c_str());
            auto remainder = blast_geometry(*input->geometry, entity, group, program_ptr,
                                            environment, false, remove_unused_points, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Split " + error).c_str());
            if (invert_selection)
                std::swap(selected, remainder);
            apply_split_unused_groups(selected, *input->geometry, delete_unused_groups);
            apply_split_unused_groups(remainder, *input->geometry, delete_unused_groups);
            ctx.outputs.add_geometry("out", std::move(selected));
            ctx.outputs.add_geometry("rest", std::move(remainder));
            return PCG_OK;
        }
        return fail_ctx(ctx, PCG_ERR_EXECUTION,
                        "Split supports Point, Spline, or Geometry input");
    }
};

class AttributeTransferElement final : public IPcgElement {
public:
    const char* type_name() const override { return "AttributeTransfer"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        AttributeTransferOptions options;
        options.source_group = ctx.node->data.value("sourceGroup", std::string());
        options.source_group_type =
            ctx.node->data.value("sourceGroupType", std::string("primitives"));
        options.destination_group = ctx.node->data.value("destinationGroup", std::string());
        options.destination_group_type =
            ctx.node->data.value("destinationGroupType", std::string("primitives"));
        options.transfer_detail = ctx.node->data.value("transferDetail", true);
        options.detail_attributes = ctx.node->data.value("detailAttributes", std::string("*"));
        options.transfer_primitives = ctx.node->data.value("transferPrimitives", false);
        options.primitive_attributes =
            ctx.node->data.value("primitiveAttributes", std::string("*"));
        options.transfer_points = ctx.node->data.value("transferPoints", false);
        options.point_attributes = ctx.node->data.value("pointAttributes", std::string("*"));
        options.transfer_vertices = ctx.node->data.value("transferVertices", false);
        options.vertex_attributes = ctx.node->data.value("vertexAttributes", std::string("*"));
        options.allow_p_attribute = ctx.node->data.value("allowPAttribute", false);
        options.copy_local_variables = ctx.node->data.value("copyLocalVariables", true);
        options.kernel_function = ctx.node->data.value("kernelFunction", std::string("elendt"));
        options.kernel_radius = ctx.node->data.value("kernelRadius", 10.0);
        options.max_sample_count = ctx.node->data.value("maxSampleCount", 1);
        options.enable_distance_threshold =
            ctx.node->data.value("enableDistanceThreshold", true);
        options.distance_threshold = ctx.node->data.value("distanceThreshold", 10.0);
        options.blend_width = ctx.node->data.value("blendWidth", 0.0);
        options.uniform_bias = ctx.node->data.value("uniformBias", 0.5);

        if (!options.transfer_detail && !options.transfer_primitives &&
            !options.transfer_points && !options.transfer_vertices)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "AttributeTransfer: enable at least one attribute class");

        // Point target: transfer source mesh attributes onto point cloud.
        if (ctx.inputs.find_points("target") != nullptr) {
            const auto target_points =
                get_points_input(ctx, "target", "AttributeTransfer missing target input");
            const auto source =
                get_geometry_input(ctx, "source", "AttributeTransfer missing source input");

            data::PcgGeometry target_geo;
            target_geo.points_mut().reserve(target_points.points().size());
            for (const auto& p : target_points.points())
                target_geo.points_mut().push_back({p.x, p.y, p.z});

            const auto transferred = attribute_transfer_geometry(target_geo, source, options);

            data::PcgPointData output = target_points;
            auto& out_pts = output.points_mut();
            const auto point_attr_names =
                transferred.attributes().names(data::AttributeOwner::Point);
            for (size_t i = 0; i < out_pts.size() && i < transferred.points().size(); ++i) {
                for (const auto& name : point_attr_names) {
                    const auto* attr =
                        transferred.attributes().find(data::AttributeOwner::Point, name);
                    if (!attr)
                        continue;
                    const int tuple = std::max(1, attr->schema().tuple_size);
                    const size_t base = i * static_cast<size_t>(tuple);
                    if (attr->schema().type == data::AttributeType::Float) {
                        const auto& vals = attr->float_values();
                        if (tuple == 1) {
                            out_pts[i].attributes[name] = vals.size() > base ? vals[base] : 0.0;
                        } else {
                            nlohmann::json arr = nlohmann::json::array();
                            for (int c = 0; c < tuple && base + c < vals.size(); ++c)
                                arr.push_back(vals[base + static_cast<size_t>(c)]);
                            out_pts[i].attributes[name] = arr;
                        }
                    } else if (attr->schema().type == data::AttributeType::Int) {
                        const auto& vals = attr->int_values();
                        if (tuple == 1) {
                            out_pts[i].attributes[name] =
                                vals.size() > base ? static_cast<double>(vals[base]) : 0.0;
                        } else {
                            nlohmann::json arr = nlohmann::json::array();
                            for (int c = 0; c < tuple && base + c < vals.size(); ++c)
                                arr.push_back(static_cast<double>(vals[base + static_cast<size_t>(c)]));
                            out_pts[i].attributes[name] = arr;
                        }
                    }
                }
            }

            // Transfer detail attributes as uniform point attributes.
            const auto detail_names =
                transferred.attributes().names(data::AttributeOwner::Detail);
            for (const auto& name : detail_names) {
                const auto* attr =
                    transferred.attributes().find(data::AttributeOwner::Detail, name);
                if (!attr)
                    continue;
                for (auto& pt : out_pts) {
                    if (attr->schema().type == data::AttributeType::Float) {
                        const auto& vals = attr->float_values();
                        pt.attributes[name] = vals.empty() ? 0.0 : vals[0];
                    } else if (attr->schema().type == data::AttributeType::Int) {
                        const auto& vals = attr->int_values();
                        pt.attributes[name] = vals.empty() ? 0.0 : static_cast<double>(vals[0]);
                    }
                }
            }

            emit_points(ctx, std::move(output));
            return PCG_OK;
        }

        const auto target =
            get_geometry_input(ctx, "target", "AttributeTransfer missing target input");
        const auto source =
            get_geometry_input(ctx, "source", "AttributeTransfer missing source input");
        emit_geometry(ctx, attribute_transfer_geometry(target, source, options));
        return PCG_OK;
    }
};

} // namespace

void register_attribute_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("AttributeWrangle", std::make_unique<AttributeWrangleElement>());
    map.emplace("AttributeTransfer", std::make_unique<AttributeTransferElement>());
    map.emplace("Blast", std::make_unique<BlastElement>());
    map.emplace("Delete", std::make_unique<DeleteElement>());
    map.emplace("Split", std::make_unique<SplitElement>());
}

} // namespace pcg::internal::elements
