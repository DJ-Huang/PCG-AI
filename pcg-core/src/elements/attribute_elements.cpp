#include "elements/attribute_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/expression.hpp"
#include "geometry/bmesh.hpp"

#include <algorithm>
#include <cmath>
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
};

std::unordered_map<std::string, double> parse_parameters(const nlohmann::json& data,
                                                         std::string& error)
{
    std::unordered_map<std::string, double> result;
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
        if (it.value().is_number())
            result[it.key()] = it.value().get<double>();
        else if (it.value().is_boolean())
            result[it.key()] = it.value().get<bool>() ? 1.0 : 0.0;
        else {
            error = "parameter '" + it.key() + "' must be numeric";
            return {};
        }
    }
    return result;
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

EvalContext make_context(ElementVariables& variables,
                         const std::unordered_map<std::string, double>& parameters)
{
    EvalContext context;
    context.parameters = parameters;
    context.read_variable = [&variables](const std::string& name, double& value) {
        if (name == "@P.x" && variables.x) value = *variables.x;
        else if (name == "@P.y" && variables.y) value = *variables.y;
        else if (name == "@P.z" && variables.z) value = *variables.z;
        else if (name == "@curveu") value = variables.curve_u;
        else if (name == "@ptnum") value = variables.point_number;
        else if (name == "@numpt") value = variables.point_count;
        else if (name == "@primnum") value = variables.primitive_number;
        else if (name == "@numprim") value = variables.primitive_count;
        else if (variables.attributes && name.size() > 1 && name.front() == '@') {
            const std::string key = name.substr(1);
            if (!variables.attributes->contains(key) ||
                !json_number((*variables.attributes)[key], value))
                return false;
        } else {
            return false;
        }
        return true;
    };
    context.write_variable = [&variables](const std::string& name, double value) {
        if (name == "@P.x" && variables.x) *variables.x = value;
        else if (name == "@P.y" && variables.y) *variables.y = value;
        else if (name == "@P.z" && variables.z) *variables.z = value;
        else if (variables.attributes && name.size() > 1 && name.front() == '@' &&
                 name != "@curveu" && name != "@ptnum" && name != "@numpt" &&
                 name != "@primnum" && name != "@numprim") {
            (*variables.attributes)[name.substr(1)] = value;
        } else {
            return false;
        }
        return true;
    };
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
                        const std::unordered_map<std::string, double>& parameters,
                        bool& selected,
                        std::string& error)
{
    if (!program) {
        selected = true;
        return true;
    }
    EvalContext context = make_context(variables, parameters);
    double value = 0.0;
    if (!program->evaluate(context, value, error))
        return false;
    selected = value != 0.0;
    return true;
}

bool apply_wrangle_points(data::PcgPointData& points,
                          const Program& program,
                          const std::unordered_map<std::string, double>& parameters,
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
        EvalContext context = make_context(variables, parameters);
        if (!program.execute(context, error)) {
            error = "point " + std::to_string(index) + ": " + error;
            return false;
        }
    }
    return true;
}

bool apply_wrangle_splines(data::PcgSplineData& splines,
                           const Program& program,
                           const std::unordered_map<std::string, double>& parameters,
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
            EvalContext context = make_context(variables, parameters);
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
                            const std::unordered_map<std::string, double>& parameters,
                            std::string& error)
{
    const int count = static_cast<int>(geometry.points().size());
    for (int index = 0; index < count; ++index) {
        auto& point = geometry.points_mut()[static_cast<size_t>(index)];
        ElementVariables variables{&point.x, &point.y, &point.z, nullptr,
                                   indexed_curve_u(static_cast<size_t>(index), geometry.points().size()),
                                   index, count, 0, static_cast<int>(geometry.faces().size())};
        EvalContext context = make_context(variables, parameters);
        if (!program.execute(context, error)) {
            error = "geometry point " + std::to_string(index) + ": " + error;
            return false;
        }
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
                        const std::unordered_map<std::string, double>& parameters,
                        std::string& error)
{
    const int count = static_cast<int>(mesh.vertices().size());
    for (int index = 0; index < count; ++index) {
        auto& point = mesh.vertices_mut()[static_cast<size_t>(index)];
        ElementVariables variables{&point.x, &point.y, &point.z, nullptr,
                                   indexed_curve_u(static_cast<size_t>(index), mesh.vertices().size()),
                                   index, count, 0,
                                   static_cast<int>(mesh.triangles().size() / 3)};
        EvalContext context = make_context(variables, parameters);
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
                                const std::unordered_map<std::string, double>& parameters,
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
        if (!evaluate_selection(program, variables, parameters, expression_selected, error)) {
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
                                  const std::unordered_map<std::string, double>& parameters,
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
            if (!evaluate_selection(program, variables, parameters, expression_selected, error)) {
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

int encode_edge(int a, int b)
{
    return static_cast<int>(static_cast<int64_t>(std::min(a, b)) * 1000000 + std::max(a, b));
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
    output.detail() = source.detail();
    std::vector<int> point_remap(source.points().size(), -1);
    for (size_t index = 0; index < source.points().size(); ++index) {
        if (!keep_points[index]) continue;
        point_remap[index] = static_cast<int>(output.points().size());
        output.points_mut().push_back(source.points()[index]);
    }

    std::vector<int> face_remap(source.faces().size(), -1);
    std::vector<std::string> face_materials;
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
        output.faces_mut().push_back(std::move(face));
        if (source.has_face_materials())
            face_materials.push_back(source.face_materials()[face_index]);
    }

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
        for (int old_edge : source.groups().members(geometry::GroupDomain::Edge, name)) {
            const int old_a = old_edge / 1000000;
            const int old_b = old_edge % 1000000;
            if (old_a < 0 || old_b < 0 || static_cast<size_t>(old_a) >= point_remap.size() ||
                static_cast<size_t>(old_b) >= point_remap.size())
                continue;
            const int a = point_remap[static_cast<size_t>(old_a)];
            const int b = point_remap[static_cast<size_t>(old_b)];
            if (a >= 0 && b >= 0)
                output.groups().add(geometry::GroupDomain::Edge, name, encode_edge(a, b));
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
    if (source.has_face_materials()) output.set_face_materials(std::move(face_materials));
    else if (source.has_material()) output.set_material_name(source.material_name());
    return output;
}

data::PcgGeometry blast_geometry(const data::PcgGeometry& source,
                                 const std::string& entity,
                                 const std::string& group,
                                 const Program* program,
                                 const std::unordered_map<std::string, double>& parameters,
                                 bool delete_non_selected,
                                 bool remove_unused_points,
                                 std::string& error)
{
    const bool primitives = entity == "primitives";
    std::vector<bool> keep_points(source.points().size(), true);
    std::vector<bool> keep_faces(source.faces().size(), true);

    if (primitives) {
        const auto group_members = group.empty()
            ? std::unordered_set<int>{}
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
                                       static_cast<int>(source.faces().size())};
            bool expression_selected = true;
            if (!evaluate_selection(program, variables, parameters, expression_selected, error)) {
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
            ? std::unordered_set<int>{}
            : source.groups().eval(geometry::GroupDomain::Point, group);
        for (size_t point_index = 0; point_index < source.points().size(); ++point_index) {
            const auto& source_point = source.points()[point_index];
            double x = source_point.x, y = source_point.y, z = source_point.z;
            ElementVariables variables{&x, &y, &z, nullptr,
                                       indexed_curve_u(point_index, source.points().size()),
                                       static_cast<int>(point_index),
                                       static_cast<int>(source.points().size()), 0,
                                       static_cast<int>(source.faces().size())};
            bool expression_selected = true;
            if (!evaluate_selection(program, variables, parameters, expression_selected, error)) {
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
        if (run_over != "points")
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "AttributeWrangle currently supports runOver=points only");

        const std::string source = ctx.node->data.value("expression", std::string());
        Program program;
        std::string error;
        if (!Program::compile_statements(source, program, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            ("AttributeWrangle parse error: " + error).c_str());
        const auto parameters = parse_parameters(ctx.node->data, error);
        if (!error.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            ("AttributeWrangle " + error).c_str());

        if (input->points) {
            data::PcgPointData output = *input->points;
            if (!apply_wrangle_points(output, program, parameters, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("AttributeWrangle " + error).c_str());
            emit_points(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->splines) {
            data::PcgSplineData output = *input->splines;
            if (!apply_wrangle_splines(output, program, parameters, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("AttributeWrangle " + error).c_str());
            emit_splines(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->geometry) {
            data::PcgGeometry output = *input->geometry;
            if (!apply_wrangle_geometry(output, program, parameters, error))
                return fail_ctx(ctx, PCG_ERR_EXECUTION,
                                ("AttributeWrangle " + error).c_str());
            emit_geometry(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->mesh) {
            data::PcgMeshData output = mesh_without_stale_normals(*input->mesh);
            if (!apply_wrangle_mesh(output, program, parameters, error))
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
        const auto parameters = parse_parameters(ctx.node->data, error);
        if (!error.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Blast " + error).c_str());

        if (input->points) {
            auto output = blast_points(*input->points, group, program_ptr, parameters,
                                       delete_non_selected, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Blast " + error).c_str());
            emit_points(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->splines) {
            auto output = blast_splines(*input->splines, group, program_ptr, parameters,
                                        delete_non_selected, error);
            if (!error.empty())
                return fail_ctx(ctx, PCG_ERR_EXECUTION, ("Blast " + error).c_str());
            emit_splines(ctx, std::move(output));
            return PCG_OK;
        }
        if (input->geometry) {
            auto output = blast_geometry(*input->geometry, entity, group, program_ptr, parameters,
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

} // namespace

void register_attribute_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("AttributeWrangle", std::make_unique<AttributeWrangleElement>());
    map.emplace("Blast", std::make_unique<BlastElement>());
}

} // namespace pcg::internal::elements
