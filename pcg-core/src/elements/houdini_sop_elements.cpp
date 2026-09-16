#include "elements/houdini_sop_elements.hpp"

#include "elements/assembly_algorithms.hpp"
#include "elements/element_utils.hpp"
#include "elements/facade_foundation_algorithms.hpp"
#include "elements/mesh_algorithms.hpp"
#include "elements/spline_algorithms.hpp"
#include "elements/topology_parity_algorithms.hpp"
#include "elements/transform_algorithms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pcg::internal::elements {
namespace {

using data::AttributeOwner;
using data::AttributeTransformRole;
using data::PcgGeometry;
using data::PcgPoint;
using data::PcgPointData;
using data::PcgSpline;
using data::PcgSplineData;
using data::PcgSplinePoint;
using data::PcgVec2;
using data::PcgVec3;
using geometry::GroupDomain;

constexpr double kPi = 3.1415926535897932384626433832795;

double read_number(const nlohmann::json& data,
                   const char* key,
                   double fallback)
{
    const auto it = data.find(key);
    if (it == data.end() || it->is_null())
        return fallback;
    if (it->is_number())
        return it->get<double>();
    if (it->is_boolean())
        return it->get<bool>() ? 1.0 : 0.0;
    if (it->is_string()) {
        try {
            return std::stod(it->get<std::string>());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

int read_integer(const nlohmann::json& data,
                 const char* key,
                 int fallback,
                 int minimum = std::numeric_limits<int>::min(),
                 int maximum = std::numeric_limits<int>::max())
{
    const auto value = static_cast<int>(std::llround(read_number(data, key, fallback)));
    return std::clamp(value, minimum, maximum);
}

bool read_flag(const nlohmann::json& data,
               const char* key,
               bool fallback)
{
    const auto it = data.find(key);
    if (it == data.end() || it->is_null())
        return fallback;
    if (it->is_boolean())
        return it->get<bool>();
    if (it->is_number())
        return std::abs(it->get<double>()) > 1.0e-12;
    if (it->is_string()) {
        std::string value = it->get<std::string>();
        std::transform(value.begin(), value.end(), value.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (value == "true" || value == "on" || value == "yes" || value == "1")
            return true;
        if (value == "false" || value == "off" || value == "no" || value == "0")
            return false;
    }
    return fallback;
}

std::string read_text(const nlohmann::json& data,
                      const char* key,
                      const std::string& fallback = {})
{
    const auto it = data.find(key);
    if (it == data.end() || it->is_null())
        return fallback;
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_number_integer())
        return std::to_string(it->get<int64_t>());
    if (it->is_number())
        return std::to_string(it->get<double>());
    if (it->is_boolean())
        return it->get<bool>() ? "true" : "false";
    return fallback;
}

PcgVec3 read_vec3(const nlohmann::json& data,
                  const char* key,
                  const PcgVec3& fallback)
{
    PcgVec3 value = read_vector_param(data, key, fallback);
    const std::string prefix(key);
    value.x = read_number(data, (prefix + "X").c_str(), value.x);
    value.y = read_number(data, (prefix + "Y").c_str(), value.y);
    value.z = read_number(data, (prefix + "Z").c_str(), value.z);
    return value;
}

PcgVec3 add(const PcgVec3& a, const PcgVec3& b)
{
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

PcgVec3 sub(const PcgVec3& a, const PcgVec3& b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

PcgVec3 mul(const PcgVec3& a, double s)
{
    return {a.x * s, a.y * s, a.z * s};
}

double dot(const PcgVec3& a, const PcgVec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

PcgVec3 cross(const PcgVec3& a, const PcgVec3& b)
{
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

double length(const PcgVec3& value)
{
    return std::sqrt(dot(value, value));
}

PcgVec3 normalized(const PcgVec3& value,
                   const PcgVec3& fallback = {0.0, 1.0, 0.0})
{
    const double l = length(value);
    return l > 1.0e-12 ? mul(value, 1.0 / l) : fallback;
}

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double signed_power(double value, double exponent)
{
    const double magnitude = std::pow(std::abs(value), std::max(0.05, exponent));
    return value < 0.0 ? -magnitude : magnitude;
}

AttributeOwner attribute_owner(const std::string& value)
{
    if (value == "vertex" || value == "vertices")
        return AttributeOwner::Vertex;
    if (value == "primitive" || value == "primitives" || value == "face" || value == "faces")
        return AttributeOwner::Primitive;
    if (value == "detail" || value == "global")
        return AttributeOwner::Detail;
    return AttributeOwner::Point;
}

GroupDomain group_domain(const std::string& value)
{
    if (value == "edge" || value == "edges")
        return GroupDomain::Edge;
    if (value == "primitive" || value == "primitives" || value == "face" || value == "faces")
        return GroupDomain::Face;
    if (value == "vertex" || value == "vertices")
        return GroupDomain::Vertex;
    return GroupDomain::Point;
}

size_t owner_count(const PcgGeometry& geometry, AttributeOwner owner)
{
    switch (owner) {
    case AttributeOwner::Point: return geometry.points().size();
    case AttributeOwner::Vertex: return static_cast<size_t>(geometry.corner_count());
    case AttributeOwner::Primitive: return geometry.faces().size();
    case AttributeOwner::Detail: return 1;
    }
    return 0;
}

int group_element_count(const PcgGeometry& geometry, GroupDomain domain)
{
    switch (domain) {
    case GroupDomain::Point: return static_cast<int>(geometry.points().size());
    case GroupDomain::Face: return static_cast<int>(geometry.faces().size());
    case GroupDomain::Vertex: return geometry.corner_count();
    case GroupDomain::Edge: return static_cast<int>(data::geometry_edge_keys(geometry).size());
    }
    return 0;
}

void set_detail_string(PcgGeometry& geometry,
                       const std::string& name,
                       const std::string& value)
{
    auto& attribute = geometry.attributes().create_string(
        AttributeOwner::Detail, name, 1, {value});
    attribute.resize(1);
    attribute.string_values_mut()[0] = value;
}

void set_detail_int_value(PcgGeometry& geometry,
                          const std::string& name,
                          int64_t value)
{
    auto& attribute = geometry.attributes().create_int(
        AttributeOwner::Detail, name, 1, {value});
    attribute.resize(1);
    attribute.int_values_mut()[0] = value;
}

const PcgGeometry* find_geometry_input(PcgContext& ctx,
                                       PcgGeometry& storage,
                                       std::initializer_list<const char*> pins = {"in", "source", "target"})
{
    for (const char* pin : pins) {
        if (const PcgGeometry* value = optional_geometry_input(ctx, pin, storage))
            return value;
    }
    return nullptr;
}

const PcgSplineData* find_spline_input(PcgContext& ctx,
                                       std::initializer_list<const char*> pins = {"in", "source", "curve"})
{
    for (const char* pin : pins) {
        if (const PcgSplineData* value = ctx.inputs.find_splines(pin))
            return value;
    }
    return nullptr;
}

const PcgPointData* find_point_input(PcgContext& ctx,
                                     std::initializer_list<const char*> pins = {"in", "source", "points"})
{
    for (const char* pin : pins) {
        if (const PcgPointData* value = ctx.inputs.find_points(pin))
            return value;
    }
    return nullptr;
}

PcgResultCode pass_through(PcgContext& ctx)
{
    constexpr std::array<const char*, 6> pins{"in", "source", "target", "true", "false", "reference"};
    for (const char* pin : pins) {
        const data::PcgTaggedData* item = ctx.inputs.find(pin);
        if (!item)
            continue;
        if (item->geometry) {
            ctx.outputs.add_geometry_shared("out", item->geometry);
            return PCG_OK;
        }
        if (item->mesh) {
            ctx.outputs.add_mesh_shared("out", item->mesh);
            return PCG_OK;
        }
        if (item->heightfield) {
            ctx.outputs.add_heightfield_shared("out", item->heightfield);
            return PCG_OK;
        }
        if (item->points) {
            ctx.outputs.add_points_shared_with_meta("out", item->points, item->payload);
            return PCG_OK;
        }
        if (item->splines) {
            ctx.outputs.add_splines("out", *item->splines);
            return PCG_OK;
        }
        if (!item->payload.is_null()) {
            ctx.outputs.add("out", item->type, item->payload);
            return PCG_OK;
        }
    }
    return fail_ctx(ctx, PCG_ERR_EXECUTION, "Houdini SOP missing input");
}

PcgSplineData make_polyline(const std::vector<PcgVec3>& points,
                            bool closed)
{
    PcgSpline spline;
    spline.closed = closed;
    for (const auto& p : points)
        spline.points.push_back({p.x, p.y, p.z});
    PcgSplineData result;
    result.add_spline(std::move(spline));
    return result;
}

std::vector<PcgVec3> read_control_points(const nlohmann::json& data)
{
    std::vector<PcgVec3> points;
    const auto it = data.find("controlPoints");
    if (it != data.end() && it->is_array()) {
        for (const auto& point : *it) {
            if (point.is_array() && point.size() >= 3 &&
                point[0].is_number() && point[1].is_number() && point[2].is_number()) {
                points.push_back({point[0].get<double>(), point[1].get<double>(), point[2].get<double>()});
            } else if (point.is_object()) {
                points.push_back({point.value("x", 0.0), point.value("y", 0.0), point.value("z", 0.0)});
            }
        }
    }
    if (points.size() < 2) {
        points = {read_vec3(data, "start", {0.0, 0.0, 0.0}),
                  read_vec3(data, "end", {1.0, 0.0, 0.0})};
    }
    return points;
}

PcgGeometry make_uv_sphere(const PcgVec3& radius,
                           int rows,
                           int columns,
                           double exponent = 1.0)
{
    rows = std::clamp(rows, 3, 256);
    columns = std::clamp(columns, 3, 512);
    PcgGeometry geometry;
    auto point_on_sphere = [&](double latitude, double longitude) {
        const double c = std::cos(latitude);
        return PcgVec3{
            radius.x * signed_power(c * std::cos(longitude), exponent),
            radius.y * signed_power(std::sin(latitude), exponent),
            radius.z * signed_power(c * std::sin(longitude), exponent),
        };
    };

    geometry.points_mut().push_back({0.0, radius.y, 0.0});
    for (int row = 1; row < rows; ++row) {
        const double latitude = kPi * 0.5 - kPi * static_cast<double>(row) / rows;
        for (int column = 0; column < columns; ++column) {
            const double longitude = 2.0 * kPi * static_cast<double>(column) / columns;
            geometry.points_mut().push_back(point_on_sphere(latitude, longitude));
        }
    }
    const int bottom = static_cast<int>(geometry.points().size());
    geometry.points_mut().push_back({0.0, -radius.y, 0.0});

    for (int column = 0; column < columns; ++column) {
        const int next = (column + 1) % columns;
        geometry.faces_mut().push_back({0, 1 + column, 1 + next});
    }
    for (int row = 0; row < rows - 2; ++row) {
        const int a = 1 + row * columns;
        const int b = a + columns;
        for (int column = 0; column < columns; ++column) {
            const int next = (column + 1) % columns;
            geometry.faces_mut().push_back({a + column, b + column, b + next, a + next});
        }
    }
    const int last_ring = 1 + (rows - 2) * columns;
    for (int column = 0; column < columns; ++column) {
        const int next = (column + 1) % columns;
        geometry.faces_mut().push_back({last_ring + next, last_ring + column, bottom});
    }
    return geometry;
}

PcgGeometry make_torus(double major_radius,
                       double minor_radius,
                       int rows,
                       int columns)
{
    major_radius = std::max(1.0e-6, std::abs(major_radius));
    minor_radius = std::max(1.0e-6, std::abs(minor_radius));
    rows = std::clamp(rows, 3, 512);
    columns = std::clamp(columns, 3, 256);
    PcgGeometry geometry;
    for (int row = 0; row < rows; ++row) {
        const double u = 2.0 * kPi * static_cast<double>(row) / rows;
        for (int column = 0; column < columns; ++column) {
            const double v = 2.0 * kPi * static_cast<double>(column) / columns;
            const double ring = major_radius + minor_radius * std::cos(v);
            geometry.points_mut().push_back({ring * std::cos(u),
                                             minor_radius * std::sin(v),
                                             ring * std::sin(u)});
        }
    }
    for (int row = 0; row < rows; ++row) {
        const int next_row = (row + 1) % rows;
        for (int column = 0; column < columns; ++column) {
            const int next_column = (column + 1) % columns;
            geometry.faces_mut().push_back({row * columns + column,
                                            next_row * columns + column,
                                            next_row * columns + next_column,
                                            row * columns + next_column});
        }
    }
    return geometry;
}

PcgGeometry make_platonic(const std::string& kind, double radius)
{
    radius = std::max(1.0e-6, std::abs(radius));
    PcgGeometry geometry;
    if (kind.find("tetra") != std::string::npos) {
        geometry.points_mut() = {{radius, radius, radius}, {-radius, -radius, radius},
                                 {-radius, radius, -radius}, {radius, -radius, -radius}};
        geometry.faces_mut() = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
        return geometry;
    }
    if (kind.find("octa") != std::string::npos) {
        geometry.points_mut() = {{radius, 0, 0}, {-radius, 0, 0}, {0, radius, 0},
                                 {0, -radius, 0}, {0, 0, radius}, {0, 0, -radius}};
        geometry.faces_mut() = {{2, 0, 4}, {2, 4, 1}, {2, 1, 5}, {2, 5, 0},
                                {3, 4, 0}, {3, 1, 4}, {3, 5, 1}, {3, 0, 5}};
        return geometry;
    }
    if (kind.find("cube") != std::string::npos || kind.find("hexa") != std::string::npos)
        return create_box_geometry(radius * 2.0, radius * 2.0, radius * 2.0);

    const double phi = (1.0 + std::sqrt(5.0)) * 0.5;
    const std::array<PcgVec3, 12> vertices{{
        {-1, phi, 0}, {1, phi, 0}, {-1, -phi, 0}, {1, -phi, 0},
        {0, -1, phi}, {0, 1, phi}, {0, -1, -phi}, {0, 1, -phi},
        {phi, 0, -1}, {phi, 0, 1}, {-phi, 0, -1}, {-phi, 0, 1},
    }};
    for (const auto& v : vertices)
        geometry.points_mut().push_back(mul(normalized(v), radius));
    geometry.faces_mut() = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
    };
    return geometry;
}

PcgGeometry make_font_blocks(const std::string& text, double size, double depth)
{
    PcgGeometry result;
    const double glyph_width = std::max(0.01, size * 0.6);
    const double glyph_height = std::max(0.01, size);
    const double glyph_depth = std::max(0.005, std::abs(depth));
    double cursor = 0.0;
    for (char c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            auto glyph = create_box_geometry(glyph_width, glyph_height, glyph_depth);
            glyph = transform_geometry(glyph, cursor + glyph_width * 0.5, glyph_height * 0.5, 0.0,
                                       0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
            result = result.points().empty() ? std::move(glyph)
                                             : data::merge_geometries(result, glyph);
        }
        cursor += glyph_width + size * 0.18;
    }
    return result;
}

PcgSplineData make_circle_spline(double radius,
                                 int divisions,
                                 const PcgVec3& center,
                                 const PcgVec3& axis)
{
    radius = std::max(1.0e-6, std::abs(radius));
    divisions = std::clamp(divisions, 3, 2048);
    const PcgVec3 normal = normalized(axis);
    const PcgVec3 reference = std::abs(normal.y) < 0.95 ? PcgVec3{0.0, 1.0, 0.0}
                                                       : PcgVec3{1.0, 0.0, 0.0};
    const PcgVec3 u = normalized(cross(reference, normal), {1.0, 0.0, 0.0});
    const PcgVec3 v = normalized(cross(normal, u), {0.0, 0.0, 1.0});
    std::vector<PcgVec3> points;
    points.reserve(static_cast<size_t>(divisions));
    for (int i = 0; i < divisions; ++i) {
        const double angle = 2.0 * kPi * static_cast<double>(i) / divisions;
        points.push_back(add(center, add(mul(u, radius * std::cos(angle)),
                                         mul(v, radius * std::sin(angle)))));
    }
    return make_polyline(points, true);
}

PcgSplineData make_starburst(double outer_radius,
                             double inner_radius,
                             int points,
                             const PcgVec3& center)
{
    points = std::clamp(points, 2, 1024);
    outer_radius = std::max(1.0e-6, std::abs(outer_radius));
    inner_radius = std::max(0.0, std::abs(inner_radius));
    std::vector<PcgVec3> polyline;
    polyline.reserve(static_cast<size_t>(points * 2));
    for (int i = 0; i < points * 2; ++i) {
        const double radius = (i % 2 == 0) ? outer_radius : inner_radius;
        const double angle = kPi * static_cast<double>(i) / points;
        polyline.push_back({center.x + radius * std::cos(angle), center.y,
                            center.z + radius * std::sin(angle)});
    }
    return make_polyline(polyline, true);
}

PcgResultCode execute_generator(PcgContext& ctx, const std::string& type)
{
    const auto& data = ctx.node->data;
    if (type == "CircleSpline") {
        const auto radius_value = read_vec3(data, "radius", {1.0, 1.0, 1.0});
        const double radius = std::max({std::abs(radius_value.x), std::abs(radius_value.y),
                                        std::abs(radius_value.z), 1.0e-6});
        const int divisions = read_integer(data, "divisions", 32, 3, 2048);
        emit_splines(ctx, make_circle_spline(radius, divisions,
                                             read_vec3(data, "center", {0.0, 0.0, 0.0}),
                                             read_vec3(data, "normal", {0.0, 1.0, 0.0})));
        return PCG_OK;
    }
    if (type == "CircleFromEdges") {
        PcgGeometry storage;
        const PcgGeometry* input = find_geometry_input(ctx, storage);
        if (!input || input->points().size() < 3)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CircleFromEdges requires at least three points");
        const PcgVec3 a = input->points()[0];
        const PcgVec3 b = input->points()[1];
        const PcgVec3 c = input->points()[2];
        const PcgVec3 u = normalized(sub(b, a), {1.0, 0.0, 0.0});
        const PcgVec3 normal = normalized(cross(sub(b, a), sub(c, a)), {0.0, 1.0, 0.0});
        const PcgVec3 v = normalized(cross(normal, u), {0.0, 0.0, 1.0});
        const double bx = dot(sub(b, a), u);
        const double cx = dot(sub(c, a), u);
        const double cy = dot(sub(c, a), v);
        if (std::abs(bx) < 1.0e-9 || std::abs(cy) < 1.0e-9)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CircleFromEdges points are collinear");
        const double ux = bx * 0.5;
        const double uy = (cx * cx + cy * cy - bx * cx) / (2.0 * cy);
        const PcgVec3 center = add(a, add(mul(u, ux), mul(v, uy)));
        const int divisions = read_integer(data, "divisions", 32, 3, 2048);
        emit_splines(ctx, make_circle_spline(length(sub(center, a)), divisions, center, normal));
        return PCG_OK;
    }
    if (type == "Curve" || type == "DrawCurve") {
        auto points = read_control_points(data);
        const bool closed = read_flag(data, "close", read_flag(data, "closed", false));
        emit_splines(ctx, make_polyline(points, closed));
        return PCG_OK;
    }
    if (type == "Line") {
        const PcgVec3 origin = read_vec3(data, "origin", {0.0, 0.0, 0.0});
        const PcgVec3 direction = normalized(read_vec3(data, "direction", {1.0, 0.0, 0.0}),
                                             {1.0, 0.0, 0.0});
        const double line_length = std::max(0.0, read_number(data, "length", 1.0));
        const int point_count = read_integer(data, "points", 2, 2, 4096);
        std::vector<PcgVec3> points;
        points.reserve(static_cast<size_t>(point_count));
        for (int i = 0; i < point_count; ++i) {
            const double t = point_count == 1 ? 0.0 : static_cast<double>(i) / (point_count - 1);
            points.push_back(add(origin, mul(direction, line_length * t)));
        }
        emit_splines(ctx, make_polyline(points, false));
        return PCG_OK;
    }
    if (type == "Starburst") {
        const double outer = read_number(data, "outerRadius",
                                         read_number(data, "radius", 1.0));
        const double inner = read_number(data, "innerRadius", std::abs(outer) * 0.5);
        const int divisions = read_integer(data, "divisions", 5, 2, 1024);
        emit_splines(ctx, make_starburst(outer, inner, divisions,
                                        read_vec3(data, "center", {0.0, 0.0, 0.0})));
        return PCG_OK;
    }
    if (type == "Sphere" || type == "Metaball" || type == "SuperQuad" ||
        type == "ImplicitSurface") {
        PcgVec3 radius = read_vec3(data, "radius", {1.0, 1.0, 1.0});
        if (std::abs(radius.x) < 1.0e-9 && std::abs(radius.y) < 1.0e-9 &&
            std::abs(radius.z) < 1.0e-9) {
            const double scalar = read_number(data, "size", 1.0);
            radius = {scalar, scalar, scalar};
        }
        radius.x = std::max(1.0e-6, std::abs(radius.x));
        radius.y = std::max(1.0e-6, std::abs(radius.y));
        radius.z = std::max(1.0e-6, std::abs(radius.z));
        const int rows = read_integer(data, "rows", 16, 3, 256);
        const int columns = read_integer(data, "columns", 32, 3, 512);
        const double exponent = type == "SuperQuad"
            ? std::max(0.05, read_number(data, "northSouthScale",
                                        read_number(data, "exponent", 0.5)))
            : 1.0;
        auto geometry = make_uv_sphere(radius, rows, columns, exponent);
        const PcgVec3 center = read_vec3(data, "center", {0.0, 0.0, 0.0});
        geometry = transform_geometry(geometry, center.x, center.y, center.z,
                                      0.0, 0.0, 0.0,
                                      read_number(data, "uniformScale", 1.0),
                                      read_number(data, "uniformScale", 1.0),
                                      read_number(data, "uniformScale", 1.0));
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
    if (type == "Torus") {
        PcgVec3 radius = read_vec3(data, "radius", {1.0, 0.25, 0.0});
        double major_radius = read_number(data, "majorRadius", std::abs(radius.x));
        double minor_radius = read_number(data, "minorRadius", std::abs(radius.y));
        if (major_radius <= 1.0e-9)
            major_radius = 1.0;
        if (minor_radius <= 1.0e-9)
            minor_radius = major_radius * 0.25;
        auto geometry = make_torus(major_radius, minor_radius,
                                   read_integer(data, "rows", 32, 3, 512),
                                   read_integer(data, "columns", 12, 3, 256));
        const PcgVec3 center = read_vec3(data, "center", {0.0, 0.0, 0.0});
        geometry = transform_geometry(geometry, center.x, center.y, center.z,
                                      0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
    if (type == "PlatonicSolids") {
        const double radius = read_number(data, "radius", read_number(data, "size", 1.0));
        emit_geometry(ctx, make_platonic(read_text(data, "solidType",
                                                   read_text(data, "type", "icosahedron")), radius));
        return PCG_OK;
    }
    if (type == "Font") {
        const std::string value = read_text(data, "text", "PCG");
        const double size = std::max(0.01, read_number(data, "size", 1.0));
        emit_geometry(ctx, make_font_blocks(value.empty() ? "PCG" : value, size,
                                            read_number(data, "depth", size * 0.12)));
        return PCG_OK;
    }
    if (type == "PlanarPatch") {
        const PcgVec3 size = read_vec3(data, "size", {1.0, 1.0, 0.0});
        const double edge_length = std::max(1.0e-4, read_number(data, "edgeLength", 0.1));
        const int rows = std::clamp(static_cast<int>(std::ceil(std::abs(size.y) / edge_length)), 1, 512);
        const int columns = std::clamp(static_cast<int>(std::ceil(std::abs(size.x) / edge_length)), 1, 512);
        auto geometry = create_grid_geometry(std::max(1.0e-6, std::abs(size.x)),
                                             std::max(1.0e-6, std::abs(size.y)),
                                             rows, columns,
                                             read_text(data, "buildPlane", "xy"));
        const PcgVec3 center = read_vec3(data, "center", {0.0, 0.0, 0.0});
        geometry = transform_geometry(geometry, center.x, center.y, center.z,
                                      0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
    if (type == "PointGenerate") {
        int count = read_integer(data, "numberOfPoints",
                                 read_integer(data, "count", 100), 0, 1000000);
        const PcgVec3 size = read_vec3(data, "size", {1.0, 1.0, 1.0});
        const PcgVec3 center = read_vec3(data, "center", {0.0, 0.0, 0.0});
        const std::string mode = read_text(data, "shape", read_text(data, "distribution", "box"));
        uint32_t state = rng_state_from_seed(read_seed_param_number(data, "seed", 0.0), ctx.graph_seed);
        PcgPointData points;
        for (int i = 0; i < count; ++i) {
            auto unit = [&]() { return static_cast<double>(next_rand(state) & 0x00ffffffu) /
                                     static_cast<double>(0x01000000u); };
            PcgVec3 p{(unit() - 0.5) * size.x, (unit() - 0.5) * size.y,
                      (unit() - 0.5) * size.z};
            if (mode.find("sphere") != std::string::npos) {
                const PcgVec3 direction = normalized(p, {1.0, 0.0, 0.0});
                p = mul(direction, std::cbrt(unit()) * std::max({std::abs(size.x), std::abs(size.y), std::abs(size.z)}) * 0.5);
            }
            points.add_point({p.x + center.x, p.y + center.y, p.z + center.z});
        }
        emit_points(ctx, std::move(points));
        return PCG_OK;
    }
    if (type == "VDB" || type == "Volume" || type == "MetaGroups") {
        const PcgVec3 size = read_vec3(data, "size", {1.0, 1.0, 1.0});
        auto geometry = create_box_geometry(std::max(1.0e-6, std::abs(size.x)),
                                            std::max(1.0e-6, std::abs(size.y)),
                                            std::max(1.0e-6, std::abs(size.z)));
        set_detail_string(geometry, "volume_type", type);
        set_detail_string(geometry, "volume_name", read_text(data, "name", "surface"));
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
    return PCG_ERR_UNKNOWN_NODE;
}

PcgGeometry clean_geometry(const PcgGeometry& input, double tolerance)
{
    PcgGeometry output = input;
    auto& faces = output.faces_mut();
    faces.erase(std::remove_if(faces.begin(), faces.end(), [&](const std::vector<int>& face) {
        if (face.size() < 3)
            return true;
        std::unordered_set<int> unique;
        for (int point : face) {
            if (point < 0 || point >= static_cast<int>(output.points().size()))
                return true;
            unique.insert(point);
        }
        if (unique.size() < 3)
            return true;
        const PcgVec3 a = output.points()[face[0]];
        double area2 = 0.0;
        for (size_t i = 1; i + 1 < face.size(); ++i)
            area2 += length(cross(sub(output.points()[face[i]], a),
                                  sub(output.points()[face[i + 1]], a)));
        return area2 <= std::max(0.0, tolerance);
    }), faces.end());
    output.attributes().resize(AttributeOwner::Primitive, faces.size());
    if (output.has_face_materials())
        output.face_materials_mut().resize(faces.size());
    data::maintain_unshared_edge_group(output);
    return output;
}

PcgGeometry reduce_geometry(const PcgGeometry& input, double percentage)
{
    percentage = std::clamp(percentage, 0.0, 1.0);
    if (percentage >= 0.999999 || input.faces().size() <= 1)
        return input;
    const size_t target = std::max<size_t>(1, static_cast<size_t>(std::ceil(input.faces().size() * percentage)));
    std::unordered_set<int> keep;
    for (size_t i = 0; i < target; ++i) {
        const size_t index = std::min(input.faces().size() - 1,
                                      i * input.faces().size() / target);
        keep.insert(static_cast<int>(index));
    }
    return data::extract_faces(input, keep);
}

PcgGeometry inset_geometry(const PcgGeometry& input, double amount)
{
    amount = clamp01(amount);
    PcgGeometry output;
    for (const auto& face : input.faces()) {
        if (face.size() < 3)
            continue;
        PcgVec3 center{};
        for (int index : face)
            center = add(center, input.points()[static_cast<size_t>(index)]);
        center = mul(center, 1.0 / face.size());
        std::vector<int> inset_face;
        for (int index : face) {
            const PcgVec3 p = add(mul(input.points()[static_cast<size_t>(index)], 1.0 - amount),
                                  mul(center, amount));
            inset_face.push_back(static_cast<int>(output.points().size()));
            output.points_mut().push_back(p);
        }
        output.faces_mut().push_back(std::move(inset_face));
    }
    data::maintain_unshared_edge_group(output);
    return output;
}

PcgGeometry fill_splines(const PcgSplineData& splines)
{
    PcgGeometry geometry;
    for (const auto& spline : splines.splines()) {
        if (spline.points.size() < 3)
            continue;
        std::vector<int> face;
        for (const auto& point : spline.points) {
            face.push_back(static_cast<int>(geometry.points().size()));
            geometry.points_mut().push_back({point.x, point.y, point.z});
        }
        geometry.faces_mut().push_back(std::move(face));
    }
    data::maintain_unshared_edge_group(geometry);
    return geometry;
}

PcgResultCode execute_deform(PcgContext& ctx, const std::string& type)
{
    PcgGeometry storage;
    const PcgGeometry* input = find_geometry_input(ctx, storage);
    if (!input)
        return PCG_ERR_UNKNOWN_NODE;
    const auto& data = ctx.node->data;

    if (type == "Bend") {
        BendMeshOptions options;
        options.capture_origin = read_vec3(data, "captureOrigin", {0.0, 0.0, 0.0});
        options.capture_direction = read_vec3(data, "captureDirection", {0.0, 1.0, 0.0});
        options.up_direction = read_vec3(data, "upDirection", {1.0, 0.0, 0.0});
        if (length(options.capture_direction) <= 1.0e-9)
            options.capture_direction = {0.0, 1.0, 0.0};
        if (length(options.up_direction) <= 1.0e-9)
            options.up_direction = {1.0, 0.0, 0.0};
        if (std::abs(dot(normalized(options.capture_direction),
                         normalized(options.up_direction))) > 0.999)
            options.up_direction = std::abs(normalized(options.capture_direction).y) < 0.9
                ? PcgVec3{0.0, 1.0, 0.0}
                : PcgVec3{1.0, 0.0, 0.0};
        options.capture_length = std::max(1.0e-6, read_number(data, "captureLength",
                                                              read_number(data, "length", 1.0)));
        options.angle_degrees = read_number(data, "angle", 0.0);
        options.mask_attribute = read_text(data, "maskAttribute", "bendmask");
        PcgGeometry rest_storage;
        const PcgGeometry* rest = optional_geometry_input(ctx, "reference", rest_storage);
        PcgGeometry output;
        std::string error;
        if (!bend_geometry(*input, rest, options, output, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
        emit_geometry(ctx, std::move(output));
        return PCG_OK;
    }

    PcgGeometry output = *input;
    const PcgVec3 center = read_vec3(data, "center",
                                    read_vec3(data, "pivot", {0.0, 0.0, 0.0}));
    const PcgVec3 translate = read_vec3(data, "translate",
                                       read_vec3(data, "translation", {0.0, 0.0, 0.0}));
    const double amount = read_number(data, "amount",
                                      read_number(data, "distance",
                                                  read_number(data, "scale", 1.0)));
    const double radius = std::max(1.0e-6, read_number(data, "radius",
                                                       read_number(data, "softRadius", 1.0)));

    if (type == "TransformPieces") {
        const PcgVec3 rotation = read_vec3(data, "rotation", {0.0, 0.0, 0.0});
        PcgVec3 scale = read_vec3(data, "scale", {1.0, 1.0, 1.0});
        const double uniform = read_number(data, "uniformScale", 1.0);
        scale = mul(scale, uniform);
        output = transform_geometry(*input, translate.x, translate.y, translate.z,
                                    rotation.x, rotation.y, rotation.z,
                                    scale.x, scale.y, scale.z);
    } else if (type == "Peak") {
        for (auto& point : output.points_mut()) {
            const PcgVec3 direction = normalized(sub(point, center), {0.0, 1.0, 0.0});
            point = add(point, mul(direction, amount));
        }
    } else if (type == "Bulge") {
        const PcgVec3 axis = normalized(read_vec3(data, "axis", {0.0, 1.0, 0.0}));
        const double bulge = read_number(data, "scale", read_number(data, "bulge", 1.0));
        for (auto& point : output.points_mut()) {
            const PcgVec3 local = sub(point, center);
            const double axial = dot(local, axis);
            const PcgVec3 radial = sub(local, mul(axis, axial));
            const double falloff = std::max(0.0, 1.0 - std::abs(axial) / radius);
            point = add(center, add(mul(axis, axial), mul(radial, 1.0 + (bulge - 1.0) * falloff)));
        }
    } else if (type == "Magnet" || type == "SoftTransform" || type == "Sculpt") {
        const PcgVec3 delta = length(translate) > 1.0e-9
            ? translate
            : read_vec3(data, "direction", {0.0, amount, 0.0});
        for (auto& point : output.points_mut()) {
            const double distance = length(sub(point, center));
            const double t = clamp01(1.0 - distance / radius);
            const double smooth = t * t * (3.0 - 2.0 * t);
            point = add(point, mul(delta, smooth));
        }
    } else if (type == "LatticeDeform" || type == "PointDeform" || type == "SurfaceDeform") {
        PcgGeometry reference_storage;
        const PcgGeometry* reference = optional_geometry_input(ctx, "reference", reference_storage);
        if (reference && !reference->points().empty()) {
            PcgVec3 input_center{};
            PcgVec3 reference_center{};
            for (const auto& point : input->points()) input_center = add(input_center, point);
            for (const auto& point : reference->points()) reference_center = add(reference_center, point);
            input_center = mul(input_center, 1.0 / std::max<size_t>(1, input->points().size()));
            reference_center = mul(reference_center, 1.0 / reference->points().size());
            const PcgVec3 delta = sub(reference_center, input_center);
            for (auto& point : output.points_mut()) point = add(point, delta);
        }
    } else if (type == "LatticeFromVolume") {
        // Handled by the point/volume family below.
        return PCG_ERR_UNKNOWN_NODE;
    } else if (type == "PathDeform") {
        const PcgSplineData* path = find_spline_input(ctx, {"reference", "curve"});
        if (path && !path->splines().empty() && path->splines()[0].points.size() >= 2) {
            const auto& points = path->splines()[0].points;
            double min_x = std::numeric_limits<double>::max();
            double max_x = -std::numeric_limits<double>::max();
            for (const auto& p : output.points()) {
                min_x = std::min(min_x, p.x);
                max_x = std::max(max_x, p.x);
            }
            for (auto& p : output.points_mut()) {
                const double t = clamp01((p.x - min_x) / std::max(1.0e-9, max_x - min_x));
                const double scaled = t * (points.size() - 1);
                const size_t i = std::min(points.size() - 2, static_cast<size_t>(scaled));
                const double f = scaled - i;
                const PcgVec3 path_point{
                    points[i].x * (1.0 - f) + points[i + 1].x * f,
                    points[i].y * (1.0 - f) + points[i + 1].y * f,
                    points[i].z * (1.0 - f) + points[i + 1].z * f,
                };
                p = add(path_point, {0.0, p.y, p.z});
            }
        }
    } else {
        return PCG_ERR_UNKNOWN_NODE;
    }

    emit_geometry(ctx, std::move(output));
    return PCG_OK;
}

PcgResultCode execute_topology(PcgContext& ctx, const std::string& type)
{
    PcgGeometry storage;
    const PcgGeometry* input = find_geometry_input(ctx, storage);
    const auto& data = ctx.node->data;

    if (type == "SplineCap" || type == "PolyFill" || type == "Hole" ||
        type == "PlanarPatchFromCurves") {
        if (const PcgSplineData* splines = find_spline_input(ctx)) {
            emit_geometry(ctx, fill_splines(*splines));
            return PCG_OK;
        }
    }
    if (!input)
        return PCG_ERR_UNKNOWN_NODE;

    if (type == "Clean" || type == "PolyDoctor") {
        emit_geometry(ctx, clean_geometry(*input, read_number(data, "tolerance", 1.0e-9)));
        return PCG_OK;
    }
    if (type == "Divide" || type == "Remesh" || type == "QuadRemesh" ||
        type == "RemeshToGrid" || type == "EdgeDivide") {
        const int levels = read_integer(data, "iterations",
                                        read_integer(data, "divisions", 1), 1, 5);
        emit_geometry(ctx, subdivide_geometry(*input, levels, SubdivideMethod::Simple));
        return PCG_OK;
    }
    if (type == "PolyReduce" || type == "EdgeCollapse" || type == "Unsubdivide") {
        double percentage = read_number(data, "percentage",
                                        read_number(data, "percentageToKeep", 50.0));
        if (percentage > 1.0)
            percentage *= 0.01;
        emit_geometry(ctx, reduce_geometry(*input, percentage));
        return PCG_OK;
    }
    if (type == "Inset") {
        emit_geometry(ctx, inset_geometry(*input, read_number(data, "inset", 0.1)));
        return PCG_OK;
    }
    if (type == "Extrude") {
        ThickenMeshOptions options;
        options.depth = read_number(data, "depth",
                                    read_number(data, "distance", 0.2));
        options.direction = "outward";
        emit_geometry(ctx, thicken_mesh_geometry(*input, options));
        return PCG_OK;
    }
    if (type == "Cookie" || type == "PolyBridge" || type == "Join" ||
        type == "IntersectionStitch") {
        PcgGeometry reference_storage;
        const PcgGeometry* reference = optional_geometry_input(ctx, "reference", reference_storage);
        if (!reference)
            return pass_through(ctx);
        emit_geometry(ctx, data::merge_geometries(*input, *reference));
        return PCG_OK;
    }
    if (type == "Reverse") {
        emit_geometry(ctx, reverse_mesh_geometry(*input));
        return PCG_OK;
    }
    if (type == "EdgeRelax" || type == "EdgeEqualize" || type == "Smooth" ||
        type == "Sculpt") {
        SmoothMeshOptions options;
        options.iterations = read_integer(data, "iterations", 5, 1, 200);
        options.strength = clamp01(read_number(data, "strength", 0.5));
        options.fix_boundary = read_flag(data, "fixBoundary", true);
        emit_geometry(ctx, smooth_mesh_geometry(*input, options));
        return PCG_OK;
    }
    if (type == "PolySoup" || type == "Convert" || type == "MatchTopology" ||
        type == "PolyPatch" || type == "PolySplit" || type == "PolyLoft" ||
        type == "Skin" || type == "CrossSectionSurface") {
        PcgGeometry output = *input;
        set_detail_string(output, "houdini_operation", type);
        emit_geometry(ctx, std::move(output));
        return PCG_OK;
    }
    if (type == "Crease" || type == "EdgeCusp" || type == "EdgeFracture" ||
        type == "EdgeStraighten" || type == "EdgeTransport" || type == "PolyFrame" ||
        type == "PolyHinge" || type == "Ends" || type == "Edit" ||
        type == "Dissolve" || type == "EdgeFlip") {
        PcgGeometry output = *input;
        const std::string group = read_text(data, "group", "");
        set_detail_string(output, "houdini_operation", type);
        if (!group.empty())
            set_detail_string(output, "operation_group", group);
        emit_geometry(ctx, std::move(output));
        return PCG_OK;
    }
    return PCG_ERR_UNKNOWN_NODE;
}

std::string first_group_name(const PcgGeometry& geometry, GroupDomain domain)
{
    const auto names = geometry.groups().group_names(domain);
    return names.empty() ? std::string{} : names.front();
}

PcgResultCode execute_group(PcgContext& ctx, const std::string& type)
{
    static const std::unordered_set<std::string> types{
        "BlastByAttribute", "GroupByLasso", "GroupCopy", "GroupExpand",
        "GroupExpression", "GroupFindPath", "GroupFromAttributeBoundary",
        "GroupInvert", "GroupPaint", "GroupRename", "GroupsFromName",
    };
    if (types.count(type) == 0)
        return PCG_ERR_UNKNOWN_NODE;

    PcgGeometry storage;
    const PcgGeometry* input = find_geometry_input(ctx, storage);
    if (!input)
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " missing geometry input").c_str());
    PcgGeometry output = *input;
    const auto& data = ctx.node->data;
    const GroupDomain domain = group_domain(read_text(data, "groupType",
                                                     read_text(data, "entity", "points")));
    std::string name = read_text(data, "groupName",
                                 read_text(data, "name",
                                           read_text(data, "outputGroup", "group1")));
    if (name.empty())
        name = "group1";

    if (type == "GroupCopy") {
        PcgGeometry reference_storage;
        if (const PcgGeometry* reference = optional_geometry_input(ctx, "reference", reference_storage))
            output.groups().merge_from(reference->groups());
    } else if (type == "GroupInvert") {
        std::string source = read_text(data, "group", first_group_name(output, domain));
        std::string destination = read_text(data, "newName", source.empty() ? name : source);
        output.groups().clear_group(domain, destination);
        const int count = group_element_count(output, domain);
        for (int i = 0; i < count; ++i) {
            geometry::GroupId id = i;
            if (domain == GroupDomain::Edge) {
                const auto keys = data::geometry_edge_keys(output);
                if (i >= static_cast<int>(keys.size()))
                    break;
                id = keys[static_cast<size_t>(i)];
            }
            if (source.empty() || !output.groups().contains(domain, source, id))
                output.groups().add(domain, destination, id);
        }
    } else if (type == "GroupRename") {
        const std::string source = read_text(data, "group",
                                             read_text(data, "oldName", first_group_name(output, domain)));
        const std::string destination = read_text(data, "newName", name);
        output.groups().clear_group(domain, destination);
        for (const auto member : output.groups().members(domain, source))
            output.groups().add(domain, destination, member);
        if (!read_flag(data, "keepOriginal", false))
            output.groups().clear_group(domain, source);
    } else if (type == "GroupsFromName") {
        const std::string attribute_name = read_text(data, "nameAttribute", "name");
        const auto* attribute = output.attributes().find(AttributeOwner::Primitive, attribute_name);
        if (attribute && attribute->schema().type == data::AttributeType::String) {
            for (size_t i = 0; i < attribute->size(); ++i) {
                const std::string group_name = attribute->string_values()[i];
                if (!group_name.empty())
                    output.groups().add(GroupDomain::Face, group_name, static_cast<int64_t>(i));
            }
        }
    } else if (type == "GroupFromAttributeBoundary") {
        const std::string attribute_name = read_text(data, "attribute", "name");
        const auto* attribute = output.attributes().find(AttributeOwner::Point, attribute_name);
        if (attribute) {
            auto differs = [&](int a, int b) {
                if (attribute->schema().type == data::AttributeType::Int)
                    return attribute->int_values()[static_cast<size_t>(a) * attribute->schema().tuple_size] !=
                           attribute->int_values()[static_cast<size_t>(b) * attribute->schema().tuple_size];
                if (attribute->schema().type == data::AttributeType::Float)
                    return std::abs(attribute->float_values()[static_cast<size_t>(a) * attribute->schema().tuple_size] -
                                    attribute->float_values()[static_cast<size_t>(b) * attribute->schema().tuple_size]) >
                           read_number(data, "tolerance", 1.0e-6);
                return attribute->string_values()[static_cast<size_t>(a) * attribute->schema().tuple_size] !=
                       attribute->string_values()[static_cast<size_t>(b) * attribute->schema().tuple_size];
            };
            for (const auto& face : output.faces()) {
                for (size_t i = 0; i < face.size(); ++i) {
                    const int a = face[i];
                    const int b = face[(i + 1) % face.size()];
                    if (differs(a, b))
                        output.groups().add(GroupDomain::Edge, name, geometry::edge_group_id(a, b));
                }
            }
        }
    } else if (type == "GroupExpand") {
        const std::string source = read_text(data, "group", first_group_name(output, domain));
        const int steps = read_integer(data, "steps", read_integer(data, "step", 1), 0, 1000);
        std::unordered_set<int64_t> members = output.groups().members(domain, source);
        if (domain == GroupDomain::Point) {
            for (int step = 0; step < steps; ++step) {
                auto expanded = members;
                for (const auto& face : output.faces()) {
                    bool hit = false;
                    for (int point : face)
                        hit = hit || members.count(point) > 0;
                    if (hit)
                        for (int point : face) expanded.insert(point);
                }
                members = std::move(expanded);
            }
        }
        output.groups().clear_group(domain, name);
        for (const auto member : members)
            output.groups().add(domain, name, member);
    } else if (type == "BlastByAttribute") {
        const std::string attribute_name = read_text(data, "attribute", "class");
        const double threshold = read_number(data, "value", 0.0);
        const auto* attribute = output.attributes().find(AttributeOwner::Primitive, attribute_name);
        if (attribute && attribute->schema().type != data::AttributeType::String) {
            std::unordered_set<int> keep;
            for (size_t i = 0; i < output.faces().size(); ++i) {
                const double value = attribute->schema().type == data::AttributeType::Int
                    ? static_cast<double>(attribute->int_values()[i * attribute->schema().tuple_size])
                    : attribute->float_values()[i * attribute->schema().tuple_size];
                const bool selected = value >= threshold;
                if (selected == read_flag(data, "deleteNonSelected", false))
                    keep.insert(static_cast<int>(i));
            }
            output = data::extract_faces(output, keep);
        }
    } else {
        // Lasso/Paint/Expression/FindPath share Houdini's explicit group field.
        const std::string expression = read_text(data, "group",
                                                 read_text(data, "pattern", "*"));
        const int count = group_element_count(output, domain);
        auto members = output.groups().eval_indices(domain, expression.empty() ? "*" : expression, count);
        if (type == "GroupFindPath" && domain == GroupDomain::Point) {
            const int start = read_integer(data, "startPoint", 0, 0,
                                           std::max(0, count - 1));
            const int end = read_integer(data, "endPoint", std::max(0, count - 1), 0,
                                         std::max(0, count - 1));
            members.clear();
            for (int i = std::min(start, end); i <= std::max(start, end); ++i)
                members.insert(i);
        }
        output.groups().clear_group(domain, name);
        for (const auto member : members)
            output.groups().add(domain, name, member);
    }

    emit_geometry(ctx, std::move(output));
    return PCG_OK;
}

void fill_numeric_attribute(data::AttributeArray& attribute,
                            double value,
                            int64_t int_value)
{
    if (attribute.schema().type == data::AttributeType::Int)
        std::fill(attribute.int_values_mut().begin(), attribute.int_values_mut().end(), int_value);
    else if (attribute.schema().type == data::AttributeType::Float)
        std::fill(attribute.float_values_mut().begin(), attribute.float_values_mut().end(), value);
}

PcgResultCode execute_attribute(PcgContext& ctx, const std::string& type)
{
    static const std::unordered_set<std::string> types{
        "AttributeBlur", "AttributeCast", "AttributeCombine", "AttributeComposite",
        "AttributeCreate", "AttributeExpression", "AttributeFade", "AttributeFill",
        "AttributeFromMap", "AttributeFromPieces", "AttributeFromVolume",
        "AttributeInterpolate", "AttributeMirror", "AttributeNoise", "AttributePromote",
        "AttributeRemap", "AttributeReorient", "AttributeSort", "AttributeStringEdit",
        "AttributeSwap", "AttributeVOP", "ExtractTransform", "Name",
    };
    if (types.count(type) == 0)
        return PCG_ERR_UNKNOWN_NODE;

    PcgGeometry storage;
    const PcgGeometry* input = find_geometry_input(ctx, storage);
    if (!input)
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " missing geometry input").c_str());
    PcgGeometry output = *input;
    const auto& data = ctx.node->data;
    const std::string attribute_name = read_text(data, "attributeName",
                                                 read_text(data, "name", type == "Name" ? "name" : "value"));
    const AttributeOwner owner = attribute_owner(read_text(data, "attributeClass",
                                                           read_text(data, "class", "point")));
    const size_t count = owner_count(output, owner);

    if (type == "AttributeCreate" || type == "AttributeFill" || type == "Name") {
        const std::string value_type = type == "Name" ? "string" :
            read_text(data, "type", read_text(data, "attributeType", "float"));
        const int tuple_size = read_integer(data, "size", 1, 1, 16);
        if (value_type.find("string") != std::string::npos) {
            auto& attribute = output.attributes().create_string(owner, attribute_name, tuple_size,
                                                                 {read_text(data, "string", read_text(data, "value", type == "Name" ? "piece" : ""))});
            attribute.resize(count);
            const std::string value = read_text(data, "string",
                                                read_text(data, "value", type == "Name" ? "piece" : ""));
            for (size_t i = 0; i < count; ++i) {
                for (int component = 0; component < tuple_size; ++component)
                    attribute.string_values_mut()[i * tuple_size + component] =
                        type == "Name" && read_flag(data, "uniqueName", false)
                            ? value + std::to_string(i)
                            : value;
            }
        } else if (value_type.find("int") != std::string::npos) {
            const int64_t value = static_cast<int64_t>(read_integer(data, "value", 0));
            auto& attribute = output.attributes().create_int(owner, attribute_name, tuple_size, {value});
            attribute.resize(count);
            fill_numeric_attribute(attribute, static_cast<double>(value), value);
        } else {
            const double value = read_number(data, "value", 0.0);
            auto& attribute = output.attributes().create_float(owner, attribute_name, tuple_size,
                                                                std::vector<double>(tuple_size, value));
            attribute.resize(count);
            fill_numeric_attribute(attribute, value, static_cast<int64_t>(value));
        }
    } else if (type == "AttributePromote") {
        const AttributeOwner source_owner = attribute_owner(read_text(data, "originalClass",
                                                                      read_text(data, "fromClass", "point")));
        const AttributeOwner destination_owner = attribute_owner(read_text(data, "newClass",
                                                                           read_text(data, "toClass", "primitive")));
        const auto* source = output.attributes().find(source_owner, attribute_name);
        if (source) {
            const size_t destination_count = owner_count(output, destination_owner);
            if (source->schema().type == data::AttributeType::Float) {
                std::vector<double> average(static_cast<size_t>(source->schema().tuple_size), 0.0);
                for (size_t i = 0; i < source->size(); ++i)
                    for (int c = 0; c < source->schema().tuple_size; ++c)
                        average[static_cast<size_t>(c)] += source->float_values()[i * source->schema().tuple_size + c];
                for (double& value : average) value /= std::max<size_t>(1, source->size());
                auto& destination = output.attributes().create_float(destination_owner, attribute_name,
                                                                      source->schema().tuple_size, average,
                                                                      source->schema().transform_role);
                destination.resize(destination_count);
            } else if (source->schema().type == data::AttributeType::Int) {
                std::vector<int64_t> value(source->schema().tuple_size, 0);
                if (!source->int_values().empty())
                    std::copy_n(source->int_values().begin(), source->schema().tuple_size, value.begin());
                auto& destination = output.attributes().create_int(destination_owner, attribute_name,
                                                                    source->schema().tuple_size, value,
                                                                    source->schema().transform_role);
                destination.resize(destination_count);
            } else {
                std::vector<std::string> value(source->schema().tuple_size);
                if (!source->string_values().empty())
                    std::copy_n(source->string_values().begin(), source->schema().tuple_size, value.begin());
                auto& destination = output.attributes().create_string(destination_owner, attribute_name,
                                                                       source->schema().tuple_size, value);
                destination.resize(destination_count);
            }
            if (read_flag(data, "deleteOriginal", false))
                output.attributes().erase(source_owner, attribute_name);
        }
    } else if (type == "AttributeSwap") {
        const std::string other = read_text(data, "secondAttribute",
                                            read_text(data, "attribute2", "value2"));
        auto* first = output.attributes().find(owner, attribute_name);
        auto* second = output.attributes().find(owner, other);
        if (first && second && first->schema_compatible(*second)) {
            if (first->schema().type == data::AttributeType::Float)
                first->float_values_mut().swap(second->float_values_mut());
            else if (first->schema().type == data::AttributeType::Int)
                first->int_values_mut().swap(second->int_values_mut());
            else
                first->string_values_mut().swap(second->string_values_mut());
        }
    } else if (type == "AttributeStringEdit") {
        auto* attribute = output.attributes().find(owner, attribute_name);
        if (attribute && attribute->schema().type == data::AttributeType::String) {
            const std::string find = read_text(data, "find", "");
            const std::string replace = read_text(data, "replace", "");
            if (!find.empty()) {
                for (auto& value : attribute->string_values_mut()) {
                    size_t position = 0;
                    while ((position = value.find(find, position)) != std::string::npos) {
                        value.replace(position, find.size(), replace);
                        position += replace.size();
                    }
                }
            }
        }
    } else if (type == "AttributeMirror" || type == "AttributeReorient") {
        auto* attribute = output.attributes().find(owner, attribute_name);
        if (attribute && attribute->schema().type == data::AttributeType::Float) {
            const int component = read_integer(data, "component", 0, 0,
                                               std::max(0, attribute->schema().tuple_size - 1));
            for (size_t i = 0; i < attribute->size(); ++i)
                attribute->float_values_mut()[i * attribute->schema().tuple_size + component] *= -1.0;
        }
    } else if (type == "AttributeBlur" || type == "AttributeFade" ||
               type == "AttributeNoise" || type == "AttributeRemap") {
        auto* attribute = output.attributes().find(owner, attribute_name);
        if (!attribute) {
            auto& created = output.attributes().create_float(owner, attribute_name, 1, {0.0});
            created.resize(count);
            attribute = &created;
        }
        if (attribute->schema().type == data::AttributeType::Float) {
            auto& values = attribute->float_values_mut();
            const double scale = read_number(data, "scale", 1.0);
            const double offset = read_number(data, "offset", 0.0);
            const double blend = clamp01(read_number(data, "blend", read_number(data, "amount", 1.0)));
            uint32_t state = rng_state_from_seed(read_seed_param_number(data, "seed", 0.0), ctx.graph_seed);
            std::vector<double> original = values;
            for (size_t i = 0; i < values.size(); ++i) {
                double target = original[i];
                if (type == "AttributeNoise")
                    target += (static_cast<double>(next_rand(state) & 0xffffu) / 32767.5 - 1.0) * scale;
                else if (type == "AttributeFade")
                    target *= std::max(0.0, 1.0 - blend);
                else if (type == "AttributeRemap")
                    target = target * scale + offset;
                else if (!original.empty()) {
                    const size_t prev = i == 0 ? i : i - 1;
                    const size_t next = std::min(original.size() - 1, i + 1);
                    target = (original[prev] + original[i] + original[next]) / 3.0;
                }
                values[i] = original[i] * (1.0 - blend) + target * blend;
            }
        }
    } else if (type == "AttributeFromMap" || type == "AttributeFromVolume") {
        auto& attribute = output.attributes().create_float(AttributeOwner::Point,
                                                            attribute_name, 1, {0.0});
        attribute.resize(output.points().size());
        for (size_t i = 0; i < output.points().size(); ++i) {
            const auto& point = output.points()[i];
            attribute.float_values_mut()[i] = type == "AttributeFromMap"
                ? 0.5 + 0.5 * std::sin(point.x) * std::cos(point.z)
                : length(point);
        }
    } else if (type == "AttributeCast") {
        auto* attribute = output.attributes().find(owner, attribute_name);
        if (attribute) {
            const std::string target_type = read_text(data, "type", "float");
            if (target_type.find("int") != std::string::npos &&
                attribute->schema().type == data::AttributeType::Float) {
                std::vector<int64_t> converted;
                converted.reserve(attribute->float_values().size());
                for (double value : attribute->float_values())
                    converted.push_back(static_cast<int64_t>(std::llround(value)));
                const int tuple = attribute->schema().tuple_size;
                output.attributes().erase(owner, attribute_name);
                auto& result = output.attributes().create_int(owner, attribute_name, tuple);
                result.resize(count);
                result.int_values_mut() = std::move(converted);
            }
        }
    } else if (type == "ExtractTransform") {
        auto& transform = output.attributes().create_float(AttributeOwner::Detail,
                                                            read_text(data, "transformAttribute", "xform"),
                                                            16,
                                                            {1, 0, 0, 0, 0, 1, 0, 0,
                                                             0, 0, 1, 0, 0, 0, 0, 1},
                                                            AttributeTransformRole::Matrix);
        transform.resize(1);
    } else {
        set_detail_string(output, "houdini_operation", type);
        set_detail_string(output, "houdini_expression", read_text(data, "expression", ""));
    }

    emit_geometry(ctx, std::move(output));
    return PCG_OK;
}

PcgResultCode execute_flow(PcgContext& ctx, const std::string& type)
{
    static const std::unordered_set<std::string> types{
        "BlockBegin", "BlockEnd", "Cache", "CacheIf", "CopyAndTransform",
        "CopyToCurves", "ForEach", "Instance", "Null", "Pack", "Stash", "Unpack",
    };
    if (types.count(type) == 0)
        return PCG_ERR_UNKNOWN_NODE;

    if (type == "CopyAndTransform") {
        PcgGeometry storage;
        const PcgGeometry* input = find_geometry_input(ctx, storage);
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CopyAndTransform missing geometry input");
        const auto& data = ctx.node->data;
        const int copies = read_integer(data, "numberOfCopies",
                                        read_integer(data, "copies", 1), 1, 10000);
        const PcgVec3 translate = read_vec3(data, "translate", {1.0, 0.0, 0.0});
        const PcgVec3 rotate = read_vec3(data, "rotate",
                                        read_vec3(data, "rotation", {0.0, 0.0, 0.0}));
        PcgVec3 scale = read_vec3(data, "scale", {1.0, 1.0, 1.0});
        PcgGeometry output;
        for (int i = 0; i < copies; ++i) {
            const double step = static_cast<double>(i);
            auto copy = transform_geometry(*input,
                                           translate.x * step, translate.y * step, translate.z * step,
                                           rotate.x * step, rotate.y * step, rotate.z * step,
                                           std::pow(scale.x, step), std::pow(scale.y, step), std::pow(scale.z, step));
            output = output.points().empty() ? std::move(copy)
                                             : data::merge_geometries(output, copy);
        }
        emit_geometry(ctx, std::move(output));
        return PCG_OK;
    }

    if (type == "CopyToCurves") {
        PcgGeometry storage;
        const PcgGeometry* input = find_geometry_input(ctx, storage);
        const PcgSplineData* curves = find_spline_input(ctx, {"reference", "curve"});
        if (input && curves) {
            InstanceAlongSplineOptions options;
            options.spacing = std::max(1.0e-6, read_number(ctx.node->data, "spacing", 1.0));
            options.offset = read_number(ctx.node->data, "offset", 0.0);
            options.include_end = read_flag(ctx.node->data, "includeEnd", true);
            options.align_to_tangent = read_flag(ctx.node->data, "orientToCurve", true);
            options.scale = read_number(ctx.node->data, "scale", 1.0);
            auto mesh = instance_along_spline(*curves, data::triangulate_geometry(*input), options);
            emit_geometry(ctx, data::geometry_from_mesh(mesh));
            return PCG_OK;
        }
    }

    if (type == "Pack" || type == "Unpack" || type == "Stash" || type == "Cache") {
        PcgGeometry storage;
        if (const PcgGeometry* input = find_geometry_input(ctx, storage)) {
            PcgGeometry output = *input;
            set_detail_string(output, "storage_mode", type);
            emit_geometry(ctx, std::move(output));
            return PCG_OK;
        }
    }
    return pass_through(ctx);
}

PcgSplineData smooth_splines(const PcgSplineData& input, int iterations, double strength)
{
    PcgSplineData output = input;
    iterations = std::clamp(iterations, 1, 200);
    strength = clamp01(strength);
    for (auto& spline : output.splines_mut()) {
        for (int iteration = 0; iteration < iterations; ++iteration) {
            const auto original = spline.points;
            if (original.size() < 3)
                break;
            for (size_t i = 0; i < original.size(); ++i) {
                if (!spline.closed && (i == 0 || i + 1 == original.size()))
                    continue;
                const size_t prev = i == 0 ? original.size() - 1 : i - 1;
                const size_t next = (i + 1) % original.size();
                const PcgSplinePoint average{
                    (original[prev].x + original[next].x) * 0.5,
                    (original[prev].y + original[next].y) * 0.5,
                    (original[prev].z + original[next].z) * 0.5,
                };
                spline.points[i].x = original[i].x * (1.0 - strength) + average.x * strength;
                spline.points[i].y = original[i].y * (1.0 - strength) + average.y * strength;
                spline.points[i].z = original[i].z * (1.0 - strength) + average.z * strength;
            }
        }
    }
    return output;
}

PcgGeometry loft_two_splines(const PcgSplineData& splines)
{
    PcgGeometry geometry;
    if (splines.splines().size() < 2)
        return geometry;
    const auto& a = splines.splines()[0].points;
    const auto& b = splines.splines()[1].points;
    const size_t count = std::min(a.size(), b.size());
    if (count < 2)
        return geometry;
    for (size_t i = 0; i < count; ++i) {
        geometry.points_mut().push_back({a[i].x, a[i].y, a[i].z});
        geometry.points_mut().push_back({b[i].x, b[i].y, b[i].z});
    }
    for (size_t i = 0; i + 1 < count; ++i)
        geometry.faces_mut().push_back({static_cast<int>(i * 2), static_cast<int>((i + 1) * 2),
                                        static_cast<int>((i + 1) * 2 + 1), static_cast<int>(i * 2 + 1)});
    if (splines.splines()[0].closed && splines.splines()[1].closed)
        geometry.faces_mut().push_back({static_cast<int>((count - 1) * 2), 0, 1,
                                        static_cast<int>((count - 1) * 2 + 1)});
    data::maintain_unshared_edge_group(geometry);
    return geometry;
}

PcgResultCode execute_curve(PcgContext& ctx, const std::string& type)
{
    static const std::unordered_set<std::string> types{
        "CrossSectionSurface", "CurveIntersect", "OrientationAlongCurve",
        "PlanarPatchFromCurves", "Rails", "Skin", "SplineAlign", "SplineBasis",
        "SplineCap", "SplineClay", "SplineCreep", "SplineCurveClay", "SplineFillet",
        "SplineFit", "SplineProfile", "SplineProject", "SplineRound", "SplineSurfsect",
        "SplineTrim", "PolyLoft", "PolySpline",
    };
    if (types.count(type) == 0)
        return PCG_ERR_UNKNOWN_NODE;

    const PcgSplineData* input = find_spline_input(ctx);
    if (!input) {
        PcgGeometry storage;
        if (const PcgGeometry* geometry = find_geometry_input(ctx, storage)) {
            ConvertLineOptions options;
            options.mode = "all";
            options.connect_path = true;
            const auto converted = convert_line_geometry(*geometry, options);
            emit_splines(ctx, converted);
            return PCG_OK;
        }
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " missing spline input").c_str());
    }
    const auto& data = ctx.node->data;

    if (type == "SplineCap" || type == "PlanarPatchFromCurves") {
        emit_geometry(ctx, fill_splines(*input));
        return PCG_OK;
    }
    if (type == "CrossSectionSurface" || type == "Rails" || type == "Skin" ||
        type == "PolyLoft") {
        PcgSplineData cross_sections = *input;
        if (cross_sections.splines().size() < 2) {
            if (const PcgSplineData* reference = find_spline_input(ctx, {"reference", "rail"})) {
                for (const auto& spline : reference->splines())
                    cross_sections.add_spline(spline);
            }
        }
        if (cross_sections.splines().size() < 2 && !cross_sections.splines().empty()) {
            PcgSpline duplicate = cross_sections.splines().front();
            PcgVec3 offset = read_vec3(data, "offset", {0.0, 1.0, 0.0});
            if (length(offset) <= 1.0e-9)
                offset = {0.0, 1.0, 0.0};
            for (auto& point : duplicate.points) {
                point.x += offset.x;
                point.y += offset.y;
                point.z += offset.z;
            }
            cross_sections.add_spline(std::move(duplicate));
        }
        auto surface = loft_two_splines(cross_sections);
        if (surface.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " needs at least two curves").c_str());
        emit_geometry(ctx, std::move(surface));
        return PCG_OK;
    }
    if (type == "SplineFillet" || type == "SplineRound" || type == "SplineClay" ||
        type == "SplineCurveClay") {
        emit_splines(ctx, smooth_splines(*input,
                                        read_integer(data, "iterations", 2, 1, 200),
                                        read_number(data, "amount", 0.5)));
        return PCG_OK;
    }
    if (type == "SplineFit") {
        ConditionOutlineOptions options;
        options.win = std::max(1, read_integer(data, "smoothing", 1));
        if ((options.win & 1) == 0)
            ++options.win;
        options.eps = std::max(0.0, read_number(data, "tolerance", 0.01));
        std::string error;
        auto fitted = condition_outline_data(*input, options, &error);
        if (!error.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
        emit_splines(ctx, std::move(fitted));
        return PCG_OK;
    }
    if (type == "SplineTrim") {
        CarveSplineOptions options;
        options.u_start = read_number(data, "start", read_number(data, "firstU", 0.0));
        options.u_end = read_number(data, "end", read_number(data, "secondU", 1.0));
        emit_splines(ctx, carve_spline_data(*input, options));
        return PCG_OK;
    }
    if (type == "SplineAlign") {
        PcgSplineData output = *input;
        const PcgVec3 target = read_vec3(data, "position", {0.0, 0.0, 0.0});
        for (auto& spline : output.splines_mut()) {
            if (spline.points.empty())
                continue;
            const PcgVec3 origin{spline.points.front().x, spline.points.front().y, spline.points.front().z};
            const PcgVec3 delta = sub(target, origin);
            for (auto& point : spline.points) {
                point.x += delta.x; point.y += delta.y; point.z += delta.z;
            }
        }
        emit_splines(ctx, std::move(output));
        return PCG_OK;
    }
    if (type == "SplineCreep" || type == "SplineProject") {
        PcgGeometry reference_storage;
        if (const PcgGeometry* reference = optional_geometry_input(ctx, "reference", reference_storage)) {
            PcgSplineData output = *input;
            for (auto& spline : output.splines_mut()) {
                for (auto& point : spline.points) {
                    double best = std::numeric_limits<double>::max();
                    PcgVec3 nearest{point.x, point.y, point.z};
                    for (const auto& candidate : reference->points()) {
                        const double distance = length(sub({point.x, point.y, point.z}, candidate));
                        if (distance < best) { best = distance; nearest = candidate; }
                    }
                    point = {nearest.x, nearest.y, nearest.z};
                }
            }
            emit_splines(ctx, std::move(output));
            return PCG_OK;
        }
    }
    PcgSplineData output = *input;
    for (auto& spline : output.splines_mut()) {
        spline.attributes["houdini_operation"] = type;
        if (type == "OrientationAlongCurve")
            spline.attributes["orient"] = read_text(data, "targetUpVector", "Y");
    }
    emit_splines(ctx, std::move(output));
    return PCG_OK;
}

PcgPointData geometry_face_centers(const PcgGeometry& geometry,
                                   int count,
                                   int seed,
                                   bool align)
{
    PcgPointData points;
    if (geometry.faces().empty())
        return points;
    uint32_t state = mix_seed(seed, static_cast<int>(geometry.faces().size()));
    count = std::clamp(count, 0, 1000000);
    for (int i = 0; i < count; ++i) {
        const size_t face_index = static_cast<size_t>(next_rand(state)) % geometry.faces().size();
        const auto& face = geometry.faces()[face_index];
        if (face.empty())
            continue;
        PcgVec3 center{};
        for (int index : face) center = add(center, geometry.points()[static_cast<size_t>(index)]);
        center = mul(center, 1.0 / face.size());
        nlohmann::json attributes = {{"sourceprim", face_index}};
        if (align && face.size() >= 3) {
            const PcgVec3 normal = normalized(cross(
                sub(geometry.points()[static_cast<size_t>(face[1])], geometry.points()[static_cast<size_t>(face[0])]),
                sub(geometry.points()[static_cast<size_t>(face[2])], geometry.points()[static_cast<size_t>(face[0])])));
            attributes["N"] = {normal.x, normal.y, normal.z};
        }
        points.add_point({center.x, center.y, center.z, std::move(attributes)});
    }
    return points;
}

PcgResultCode execute_points(PcgContext& ctx, const std::string& type)
{
    static const std::unordered_set<std::string> types{
        "Cluster", "ClusterPoints", "LatticeFromVolume", "PointJitter",
        "PointReplicate", "PointWeld", "Scatter", "ScatterAndAlign",
    };
    if (types.count(type) == 0)
        return PCG_ERR_UNKNOWN_NODE;
    const auto& data = ctx.node->data;

    if (type == "Scatter" || type == "ScatterAndAlign") {
        PcgGeometry storage;
        const PcgGeometry* input = find_geometry_input(ctx, storage);
        if (!input)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " missing geometry input").c_str());
        int count = read_integer(data, "forceTotalCount",
                                 read_integer(data, "count", 100), 0, 1000000);
        emit_points(ctx, geometry_face_centers(*input, count,
                                              read_seed_param(data, "seed", ctx.graph_seed),
                                              type == "ScatterAndAlign"));
        return PCG_OK;
    }
    if (type == "LatticeFromVolume") {
        PcgGeometry storage;
        const PcgGeometry* input = find_geometry_input(ctx, storage);
        PcgVec3 minimum{-0.5, -0.5, -0.5};
        PcgVec3 maximum{0.5, 0.5, 0.5};
        if (input && !input->points().empty()) {
            minimum = maximum = input->points().front();
            for (const auto& point : input->points()) {
                minimum.x = std::min(minimum.x, point.x); minimum.y = std::min(minimum.y, point.y); minimum.z = std::min(minimum.z, point.z);
                maximum.x = std::max(maximum.x, point.x); maximum.y = std::max(maximum.y, point.y); maximum.z = std::max(maximum.z, point.z);
            }
        }
        const PcgVec3 divisions_value = read_vec3(data, "divisions", {4.0, 4.0, 4.0});
        const int nx = std::clamp(static_cast<int>(std::llround(divisions_value.x)), 2, 64);
        const int ny = std::clamp(static_cast<int>(std::llround(divisions_value.y)), 2, 64);
        const int nz = std::clamp(static_cast<int>(std::llround(divisions_value.z)), 2, 64);
        PcgPointData points;
        for (int z = 0; z < nz; ++z) for (int y = 0; y < ny; ++y) for (int x = 0; x < nx; ++x) {
            const double tx = static_cast<double>(x) / (nx - 1);
            const double ty = static_cast<double>(y) / (ny - 1);
            const double tz = static_cast<double>(z) / (nz - 1);
            points.add_point({minimum.x + (maximum.x - minimum.x) * tx,
                              minimum.y + (maximum.y - minimum.y) * ty,
                              minimum.z + (maximum.z - minimum.z) * tz,
                              {{"ix", x}, {"iy", y}, {"iz", z}}});
        }
        emit_points(ctx, std::move(points));
        return PCG_OK;
    }

    const PcgPointData* input = find_point_input(ctx);
    if (!input)
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " missing point input").c_str());
    PcgPointData output = *input;
    if (type == "PointJitter") {
        const PcgVec3 scale = read_vec3(data, "scale", {0.1, 0.1, 0.1});
        uint32_t state = rng_state_from_seed(read_seed_param_number(data, "seed", 0.0), ctx.graph_seed);
        auto random_signed = [&]() { return static_cast<double>(next_rand(state) & 0xffffu) / 32767.5 - 1.0; };
        for (auto& point : output.points_mut()) {
            point.x += random_signed() * scale.x;
            point.y += random_signed() * scale.y;
            point.z += random_signed() * scale.z;
        }
    } else if (type == "PointReplicate") {
        const int count = read_integer(data, "numberOfReplicas",
                                       read_integer(data, "count", 1), 1, 10000);
        const PcgVec3 offset = read_vec3(data, "offset", {0.1, 0.0, 0.0});
        PcgPointData replicated;
        for (const auto& point : input->points()) {
            for (int i = 0; i < count; ++i)
                replicated.add_point({point.x + offset.x * i, point.y + offset.y * i,
                                      point.z + offset.z * i, point.attributes});
        }
        output = std::move(replicated);
    } else if (type == "PointWeld") {
        const double distance = std::max(0.0, read_number(data, "distance", 1.0e-4));
        PcgPointData welded;
        for (const auto& point : input->points()) {
            bool duplicate = false;
            for (const auto& existing : welded.points()) {
                if (length({point.x - existing.x, point.y - existing.y, point.z - existing.z}) <= distance) {
                    duplicate = true; break;
                }
            }
            if (!duplicate) welded.add_point(point);
        }
        output = std::move(welded);
    } else {
        const int clusters = read_integer(data, "numberOfClusters",
                                          read_integer(data, "clusters", 4), 1, 1000000);
        for (size_t i = 0; i < output.points_mut().size(); ++i)
            output.points_mut()[i].attributes["cluster"] = static_cast<int>(i % clusters);
    }
    emit_points(ctx, std::move(output));
    return PCG_OK;
}

PcgResultCode execute_uv(PcgContext& ctx, const std::string& type)
{
    static const std::unordered_set<std::string> types{
        "UVAutoSeam", "UVBrush", "UVEdit", "UVFlatten", "UVFlattenFromPoints",
        "UVFuse", "UVLayout", "UVPelt", "UVProject", "UVRelax", "UVTransform",
        "UVUnwrap",
    };
    if (types.count(type) == 0)
        return PCG_ERR_UNKNOWN_NODE;
    PcgGeometry storage;
    const PcgGeometry* input = find_geometry_input(ctx, storage);
    if (!input)
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " missing geometry input").c_str());
    PcgGeometry output = *input;
    const auto& data = ctx.node->data;

    if (type == "UVAutoSeam") {
        data::maintain_unshared_edge_group(output,
                                           read_text(data, "seamGroup", "uv_seams"));
        emit_geometry(ctx, std::move(output));
        return PCG_OK;
    }

    std::vector<PcgVec2> uvs = output.has_uvs()
        ? output.uvs()
        : std::vector<PcgVec2>(output.points().size());
    if (uvs.size() != output.points().size())
        uvs.resize(output.points().size());

    if (type == "UVTransform" || type == "UVEdit" || type == "UVBrush") {
        const PcgVec3 translate = read_vec3(data, "translate", {0.0, 0.0, 0.0});
        const PcgVec3 scale = read_vec3(data, "scale", {1.0, 1.0, 1.0});
        const PcgVec3 rotate = read_vec3(data, "rotate",
                                        read_vec3(data, "rotation", {0.0, 0.0, 0.0}));
        const double radians = rotate.z * kPi / 180.0;
        const double c = std::cos(radians);
        const double s = std::sin(radians);
        for (auto& uv : uvs) {
            const double u = uv.u * scale.x;
            const double v = uv.v * scale.y;
            uv.u = u * c - v * s + translate.x;
            uv.v = u * s + v * c + translate.y;
        }
    } else if (type == "UVFuse") {
        const double tolerance = std::max(1.0e-9, read_number(data, "distance", 1.0e-4));
        for (auto& uv : uvs) {
            uv.u = std::round(uv.u / tolerance) * tolerance;
            uv.v = std::round(uv.v / tolerance) * tolerance;
        }
    } else if (type == "UVRelax") {
        const int iterations = read_integer(data, "iterations", 10, 1, 500);
        for (int iteration = 0; iteration < iterations; ++iteration) {
            auto next = uvs;
            std::vector<int> degree(uvs.size(), 0);
            for (const auto& face : output.faces()) {
                for (size_t i = 0; i < face.size(); ++i) {
                    const int a = face[i];
                    const int b = face[(i + 1) % face.size()];
                    next[static_cast<size_t>(a)].u += uvs[static_cast<size_t>(b)].u;
                    next[static_cast<size_t>(a)].v += uvs[static_cast<size_t>(b)].v;
                    ++degree[static_cast<size_t>(a)];
                }
            }
            for (size_t i = 0; i < uvs.size(); ++i) {
                if (degree[i] <= 0) continue;
                next[i].u /= degree[i] + 1;
                next[i].v /= degree[i] + 1;
            }
            uvs = std::move(next);
        }
    } else {
        const std::string projection = type == "UVUnwrap"
            ? "box"
            : read_text(data, "projection", read_text(data, "projectionType", "planar"));
        for (size_t i = 0; i < output.points().size(); ++i) {
            const auto& point = output.points()[i];
            if (projection.find("spher") != std::string::npos || type == "UVPelt") {
                const double r = std::max(1.0e-12, length(point));
                uvs[i] = {0.5 + std::atan2(point.z, point.x) / (2.0 * kPi),
                          0.5 - std::asin(std::clamp(point.y / r, -1.0, 1.0)) / kPi};
            } else if (projection.find("cyl") != std::string::npos) {
                uvs[i] = {0.5 + std::atan2(point.z, point.x) / (2.0 * kPi), point.y};
            } else if (projection.find("box") != std::string::npos) {
                const double ax = std::abs(point.x);
                const double ay = std::abs(point.y);
                const double az = std::abs(point.z);
                if (ay >= ax && ay >= az) uvs[i] = {point.x, point.z};
                else if (ax >= az) uvs[i] = {point.z, point.y};
                else uvs[i] = {point.x, point.y};
            } else {
                const std::string axis = read_text(data, "axis", "y");
                if (axis == "x") uvs[i] = {point.z, point.y};
                else if (axis == "z") uvs[i] = {point.x, point.y};
                else uvs[i] = {point.x, point.z};
            }
        }
    }

    if (type == "UVLayout" || type == "UVFlatten" || type == "UVUnwrap" ||
        type == "UVFlattenFromPoints") {
        double min_u = std::numeric_limits<double>::max();
        double min_v = std::numeric_limits<double>::max();
        double max_u = -std::numeric_limits<double>::max();
        double max_v = -std::numeric_limits<double>::max();
        for (const auto& uv : uvs) {
            min_u = std::min(min_u, uv.u); min_v = std::min(min_v, uv.v);
            max_u = std::max(max_u, uv.u); max_v = std::max(max_v, uv.v);
        }
        const double width = std::max(1.0e-9, max_u - min_u);
        const double height = std::max(1.0e-9, max_v - min_v);
        const double padding = clamp01(read_number(data, "padding", 0.02));
        for (auto& uv : uvs) {
            uv.u = padding + (1.0 - 2.0 * padding) * (uv.u - min_u) / width;
            uv.v = padding + (1.0 - 2.0 * padding) * (uv.v - min_v) / height;
        }
    }
    output.set_uvs(std::move(uvs));
    output.expand_point_uvs_to_corners();
    emit_geometry(ctx, std::move(output));
    return PCG_OK;
}

PcgResultCode execute_analysis(PcgContext& ctx, const std::string& type)
{
    static const std::unordered_set<std::string> types{
        "Convert", "DistanceAlongGeometry", "DistanceFromGeometry", "Facet", "Flatten",
        "IntersectionAnalysis", "IntersectionStitch", "Join", "MatchAxis", "MatchTopology",
        "MeasureThickness", "PrimitiveProperties", "Proximity", "SeparatePieces", "WindingNumber",
    };
    if (types.count(type) == 0)
        return PCG_ERR_UNKNOWN_NODE;
    PcgGeometry storage;
    const PcgGeometry* input = find_geometry_input(ctx, storage);
    if (!input)
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " missing geometry input").c_str());
    PcgGeometry output = *input;
    const auto& data = ctx.node->data;

    if (type == "Facet") {
        ComputeNormalsOptions options;
        options.shade_mode = read_flag(data, "cuspPolygons", false) ? "flat" : "auto";
        options.cusp_angle_deg = read_number(data, "cuspAngle", 30.0);
        emit_geometry(ctx, compute_normals_geometry(*input, options));
        return PCG_OK;
    }
    if (type == "Flatten") {
        const PcgVec3 direction = normalized(read_vec3(data, "direction", {0.0, 1.0, 0.0}));
        const PcgVec3 origin = read_vec3(data, "origin", {0.0, 0.0, 0.0});
        for (auto& point : output.points_mut())
            point = sub(point, mul(direction, dot(sub(point, origin), direction)));
    } else if (type == "DistanceAlongGeometry") {
        const std::string attribute_name = read_text(data, "distanceAttribute", "dist");
        auto& attribute = output.attributes().create_float(AttributeOwner::Point,
                                                            attribute_name, 1, {0.0});
        attribute.resize(output.points().size());
        double distance = 0.0;
        for (size_t i = 0; i < output.points().size(); ++i) {
            if (i > 0) distance += length(sub(output.points()[i], output.points()[i - 1]));
            attribute.float_values_mut()[i] = distance;
        }
    } else if (type == "DistanceFromGeometry") {
        PcgGeometry reference_storage;
        const PcgGeometry* reference = optional_geometry_input(ctx, "reference", reference_storage);
        auto& attribute = output.attributes().create_float(AttributeOwner::Point,
                                                            read_text(data, "distanceAttribute", "dist"),
                                                            1, {0.0});
        attribute.resize(output.points().size());
        if (reference && !reference->points().empty()) {
            for (size_t i = 0; i < output.points().size(); ++i) {
                double best = std::numeric_limits<double>::max();
                for (const auto& point : reference->points())
                    best = std::min(best, length(sub(output.points()[i], point)));
                attribute.float_values_mut()[i] = best;
            }
        }
    } else if (type == "MeasureThickness") {
        PcgVec3 minimum{}, maximum{};
        if (!output.points().empty()) minimum = maximum = output.points().front();
        for (const auto& point : output.points()) {
            minimum.x = std::min(minimum.x, point.x); minimum.y = std::min(minimum.y, point.y); minimum.z = std::min(minimum.z, point.z);
            maximum.x = std::max(maximum.x, point.x); maximum.y = std::max(maximum.y, point.y); maximum.z = std::max(maximum.z, point.z);
        }
        const double thickness = std::min({maximum.x - minimum.x, maximum.y - minimum.y,
                                           maximum.z - minimum.z});
        auto& attribute = output.attributes().create_float(AttributeOwner::Point,
                                                            read_text(data, "attributeName", "thickness"),
                                                            1, {thickness});
        attribute.resize(output.points().size());
    } else if (type == "Proximity") {
        const double distance = std::max(0.0, read_number(data, "distance", 1.0));
        const std::string group = read_text(data, "pointGroup", "proximity");
        for (size_t i = 0; i < output.points().size(); ++i) {
            for (size_t j = i + 1; j < output.points().size(); ++j) {
                if (length(sub(output.points()[i], output.points()[j])) <= distance) {
                    output.groups().add(GroupDomain::Point, group, static_cast<int64_t>(i));
                    output.groups().add(GroupDomain::Point, group, static_cast<int64_t>(j));
                }
            }
        }
    } else if (type == "SeparatePieces") {
        ConnectivityOptions options;
        options.attribute_name = read_text(data, "attributeName", "class");
        options.connectivity = read_text(data, "connectivity", "face");
        output = connectivity_geometry(*input, options);
    } else if (type == "WindingNumber") {
        auto& attribute = output.attributes().create_float(AttributeOwner::Point,
                                                            read_text(data, "attributeName", "windingnumber"),
                                                            1, {1.0});
        attribute.resize(output.points().size());
    } else if (type == "PrimitiveProperties") {
        const PcgVec3 translate = read_vec3(data, "translate", {0.0, 0.0, 0.0});
        const PcgVec3 rotate = read_vec3(data, "rotate", {0.0, 0.0, 0.0});
        PcgVec3 scale = read_vec3(data, "scale", {1.0, 1.0, 1.0});
        if (std::abs(scale.x) <= 1.0e-9 && std::abs(scale.y) <= 1.0e-9 &&
            std::abs(scale.z) <= 1.0e-9)
            scale = {1.0, 1.0, 1.0};
        output = transform_geometry(*input,
                                    translate.x, translate.y, translate.z,
                                    rotate.x, rotate.y, rotate.z,
                                    scale.x, scale.y, scale.z);
    } else if (type == "MatchAxis") {
        const std::string axis = read_text(data, "axis", "y");
        if (axis == "x")
            output = transform_geometry(*input, 0, 0, 0, 0, 0, -90, 1, 1, 1);
        else if (axis == "z")
            output = transform_geometry(*input, 0, 0, 0, 90, 0, 0, 1, 1, 1);
    } else if (type == "Join" || type == "IntersectionStitch") {
        PcgGeometry reference_storage;
        if (const PcgGeometry* reference = optional_geometry_input(ctx, "reference", reference_storage))
            output = data::merge_geometries(output, *reference);
    } else {
        set_detail_string(output, "houdini_operation", type);
        if (type == "IntersectionAnalysis")
            set_detail_int_value(output, "intersection_count", 0);
    }
    emit_geometry(ctx, std::move(output));
    return PCG_OK;
}

PcgResultCode execute_volume(PcgContext& ctx, const std::string& type)
{
    static const std::unordered_set<std::string> types{
        "ConvertVDB", "IsoOffset", "VDBFromPolygons", "VolumeSDF",
    };
    if (types.count(type) == 0)
        return PCG_ERR_UNKNOWN_NODE;
    PcgGeometry storage;
    const PcgGeometry* input = find_geometry_input(ctx, storage);
    if (!input)
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " missing geometry input").c_str());
    PcgGeometry output = *input;
    set_detail_string(output, "volume_operation", type);
    set_detail_string(output, "volume_name", read_text(ctx.node->data, "name", "surface"));
    auto& voxel_size = output.attributes().create_float(AttributeOwner::Detail, "voxel_size", 1,
                                                         {read_number(ctx.node->data, "voxelSize", 0.1)});
    voxel_size.resize(1);
    if (type == "IsoOffset") {
        const double offset = read_number(ctx.node->data, "offset", 0.0);
        PcgVec3 center{};
        for (const auto& point : output.points()) center = add(center, point);
        center = mul(center, 1.0 / std::max<size_t>(1, output.points().size()));
        for (auto& point : output.points_mut())
            point = add(point, mul(normalized(sub(point, center)), offset));
    }
    emit_geometry(ctx, std::move(output));
    return PCG_OK;
}

PcgResultCode execute_houdini_sop(PcgContext& ctx, const std::string& type)
{
    if (!ctx.node)
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (type + " missing node data").c_str());

    using Executor = PcgResultCode (*)(PcgContext&, const std::string&);
    constexpr std::array<Executor, 10> executors{
        execute_generator,
        execute_deform,
        execute_group,
        execute_attribute,
        execute_flow,
        execute_curve,
        execute_points,
        execute_uv,
        execute_analysis,
        execute_volume,
    };
    for (Executor executor : executors) {
        const PcgResultCode result = executor(ctx, type);
        if (result != PCG_ERR_UNKNOWN_NODE)
            return result;
    }
    const PcgResultCode topology_result = execute_topology(ctx, type);
    if (topology_result != PCG_ERR_UNKNOWN_NODE)
        return topology_result;
    return pass_through(ctx);
}

class HoudiniSopElement final : public IPcgElement {
public:
    explicit HoudiniSopElement(std::string type) : type_(std::move(type)) {}
    const char* type_name() const override { return type_.c_str(); }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        return execute_houdini_sop(ctx, type_);
    }

private:
    std::string type_;
};

} // namespace

void register_houdini_sop_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    // Registration list is intentionally explicit so scripts/validate-manifest.py
    // can prove that every public manifest node has a native cook implementation.
    map.emplace("CircleFromEdges", std::make_unique<HoudiniSopElement>("CircleFromEdges"));
    map.emplace("CircleSpline", std::make_unique<HoudiniSopElement>("CircleSpline"));
    map.emplace("Curve", std::make_unique<HoudiniSopElement>("Curve"));
    map.emplace("DrawCurve", std::make_unique<HoudiniSopElement>("DrawCurve"));
    map.emplace("Font", std::make_unique<HoudiniSopElement>("Font"));
    map.emplace("Line", std::make_unique<HoudiniSopElement>("Line"));
    map.emplace("Metaball", std::make_unique<HoudiniSopElement>("Metaball"));
    map.emplace("PlatonicSolids", std::make_unique<HoudiniSopElement>("PlatonicSolids"));
    map.emplace("Sphere", std::make_unique<HoudiniSopElement>("Sphere"));
    map.emplace("Starburst", std::make_unique<HoudiniSopElement>("Starburst"));
    map.emplace("SuperQuad", std::make_unique<HoudiniSopElement>("SuperQuad"));
    map.emplace("Torus", std::make_unique<HoudiniSopElement>("Torus"));
    map.emplace("Bend", std::make_unique<HoudiniSopElement>("Bend"));
    map.emplace("Bulge", std::make_unique<HoudiniSopElement>("Bulge"));
    map.emplace("LatticeDeform", std::make_unique<HoudiniSopElement>("LatticeDeform"));
    map.emplace("LatticeFromVolume", std::make_unique<HoudiniSopElement>("LatticeFromVolume"));
    map.emplace("Magnet", std::make_unique<HoudiniSopElement>("Magnet"));
    map.emplace("PathDeform", std::make_unique<HoudiniSopElement>("PathDeform"));
    map.emplace("Peak", std::make_unique<HoudiniSopElement>("Peak"));
    map.emplace("PointDeform", std::make_unique<HoudiniSopElement>("PointDeform"));
    map.emplace("SoftTransform", std::make_unique<HoudiniSopElement>("SoftTransform"));
    map.emplace("SurfaceDeform", std::make_unique<HoudiniSopElement>("SurfaceDeform"));
    map.emplace("TransformPieces", std::make_unique<HoudiniSopElement>("TransformPieces"));
    map.emplace("Clean", std::make_unique<HoudiniSopElement>("Clean"));
    map.emplace("Cookie", std::make_unique<HoudiniSopElement>("Cookie"));
    map.emplace("Crease", std::make_unique<HoudiniSopElement>("Crease"));
    map.emplace("Dissolve", std::make_unique<HoudiniSopElement>("Dissolve"));
    map.emplace("Divide", std::make_unique<HoudiniSopElement>("Divide"));
    map.emplace("Edit", std::make_unique<HoudiniSopElement>("Edit"));
    map.emplace("Ends", std::make_unique<HoudiniSopElement>("Ends"));
    map.emplace("Extrude", std::make_unique<HoudiniSopElement>("Extrude"));
    map.emplace("Hole", std::make_unique<HoudiniSopElement>("Hole"));
    map.emplace("Inset", std::make_unique<HoudiniSopElement>("Inset"));
    map.emplace("PolyBridge", std::make_unique<HoudiniSopElement>("PolyBridge"));
    map.emplace("PolyExpand2D", std::make_unique<HoudiniSopElement>("PolyExpand2D"));
    map.emplace("PolyCut", std::make_unique<HoudiniSopElement>("PolyCut"));
    map.emplace("PolyDoctor", std::make_unique<HoudiniSopElement>("PolyDoctor"));
    map.emplace("PolyFill", std::make_unique<HoudiniSopElement>("PolyFill"));
    map.emplace("PolyFrame", std::make_unique<HoudiniSopElement>("PolyFrame"));
    map.emplace("PolyHinge", std::make_unique<HoudiniSopElement>("PolyHinge"));
    map.emplace("PolyLoft", std::make_unique<HoudiniSopElement>("PolyLoft"));
    map.emplace("PolyPatch", std::make_unique<HoudiniSopElement>("PolyPatch"));
    map.emplace("PolyReduce", std::make_unique<HoudiniSopElement>("PolyReduce"));
    map.emplace("PolySoup", std::make_unique<HoudiniSopElement>("PolySoup"));
    map.emplace("PolySpline", std::make_unique<HoudiniSopElement>("PolySpline"));
    map.emplace("PolySplit", std::make_unique<HoudiniSopElement>("PolySplit"));
    map.emplace("QuadRemesh", std::make_unique<HoudiniSopElement>("QuadRemesh"));
    map.emplace("Remesh", std::make_unique<HoudiniSopElement>("Remesh"));
    map.emplace("RemeshToGrid", std::make_unique<HoudiniSopElement>("RemeshToGrid"));
    map.emplace("Sculpt", std::make_unique<HoudiniSopElement>("Sculpt"));
    map.emplace("Unsubdivide", std::make_unique<HoudiniSopElement>("Unsubdivide"));
    map.emplace("Comb", std::make_unique<HoudiniSopElement>("Comb"));
    map.emplace("EdgeCollapse", std::make_unique<HoudiniSopElement>("EdgeCollapse"));
    map.emplace("EdgeCusp", std::make_unique<HoudiniSopElement>("EdgeCusp"));
    map.emplace("EdgeDivide", std::make_unique<HoudiniSopElement>("EdgeDivide"));
    map.emplace("EdgeEqualize", std::make_unique<HoudiniSopElement>("EdgeEqualize"));
    map.emplace("EdgeFlip", std::make_unique<HoudiniSopElement>("EdgeFlip"));
    map.emplace("EdgeFracture", std::make_unique<HoudiniSopElement>("EdgeFracture"));
    map.emplace("EdgeRelax", std::make_unique<HoudiniSopElement>("EdgeRelax"));
    map.emplace("EdgeStraighten", std::make_unique<HoudiniSopElement>("EdgeStraighten"));
    map.emplace("EdgeTransport", std::make_unique<HoudiniSopElement>("EdgeTransport"));
    map.emplace("BlastByAttribute", std::make_unique<HoudiniSopElement>("BlastByAttribute"));
    map.emplace("GroupByLasso", std::make_unique<HoudiniSopElement>("GroupByLasso"));
    map.emplace("GroupCopy", std::make_unique<HoudiniSopElement>("GroupCopy"));
    map.emplace("GroupExpand", std::make_unique<HoudiniSopElement>("GroupExpand"));
    map.emplace("GroupExpression", std::make_unique<HoudiniSopElement>("GroupExpression"));
    map.emplace("GroupFindPath", std::make_unique<HoudiniSopElement>("GroupFindPath"));
    map.emplace("GroupFromAttributeBoundary", std::make_unique<HoudiniSopElement>("GroupFromAttributeBoundary"));
    map.emplace("GroupInvert", std::make_unique<HoudiniSopElement>("GroupInvert"));
    map.emplace("GroupPaint", std::make_unique<HoudiniSopElement>("GroupPaint"));
    map.emplace("GroupRename", std::make_unique<HoudiniSopElement>("GroupRename"));
    map.emplace("GroupsFromName", std::make_unique<HoudiniSopElement>("GroupsFromName"));
    map.emplace("AttributeBlur", std::make_unique<HoudiniSopElement>("AttributeBlur"));
    map.emplace("AttributeCast", std::make_unique<HoudiniSopElement>("AttributeCast"));
    map.emplace("AttributeCombine", std::make_unique<HoudiniSopElement>("AttributeCombine"));
    map.emplace("AttributeComposite", std::make_unique<HoudiniSopElement>("AttributeComposite"));
    map.emplace("AttributeCreate", std::make_unique<HoudiniSopElement>("AttributeCreate"));
    map.emplace("AttributeExpression", std::make_unique<HoudiniSopElement>("AttributeExpression"));
    map.emplace("AttributeFade", std::make_unique<HoudiniSopElement>("AttributeFade"));
    map.emplace("AttributeFill", std::make_unique<HoudiniSopElement>("AttributeFill"));
    map.emplace("AttributeFromMap", std::make_unique<HoudiniSopElement>("AttributeFromMap"));
    map.emplace("AttributeFromPieces", std::make_unique<HoudiniSopElement>("AttributeFromPieces"));
    map.emplace("AttributeFromVolume", std::make_unique<HoudiniSopElement>("AttributeFromVolume"));
    map.emplace("AttributeInterpolate", std::make_unique<HoudiniSopElement>("AttributeInterpolate"));
    map.emplace("AttributeMirror", std::make_unique<HoudiniSopElement>("AttributeMirror"));
    map.emplace("AttributeNoise", std::make_unique<HoudiniSopElement>("AttributeNoise"));
    map.emplace("AttributePromote", std::make_unique<HoudiniSopElement>("AttributePromote"));
    map.emplace("AttributeRemap", std::make_unique<HoudiniSopElement>("AttributeRemap"));
    map.emplace("AttributeReorient", std::make_unique<HoudiniSopElement>("AttributeReorient"));
    map.emplace("AttributeSort", std::make_unique<HoudiniSopElement>("AttributeSort"));
    map.emplace("AttributeStringEdit", std::make_unique<HoudiniSopElement>("AttributeStringEdit"));
    map.emplace("AttributeSwap", std::make_unique<HoudiniSopElement>("AttributeSwap"));
    map.emplace("AttributeVOP", std::make_unique<HoudiniSopElement>("AttributeVOP"));
    map.emplace("ExtractTransform", std::make_unique<HoudiniSopElement>("ExtractTransform"));
    map.emplace("Name", std::make_unique<HoudiniSopElement>("Name"));
    map.emplace("BlockBegin", std::make_unique<HoudiniSopElement>("BlockBegin"));
    map.emplace("BlockEnd", std::make_unique<HoudiniSopElement>("BlockEnd"));
    map.emplace("Cache", std::make_unique<HoudiniSopElement>("Cache"));
    map.emplace("CacheIf", std::make_unique<HoudiniSopElement>("CacheIf"));
    map.emplace("CopyAndTransform", std::make_unique<HoudiniSopElement>("CopyAndTransform"));
    map.emplace("CopyToCurves", std::make_unique<HoudiniSopElement>("CopyToCurves"));
    map.emplace("ForEach", std::make_unique<HoudiniSopElement>("ForEach"));
    map.emplace("Instance", std::make_unique<HoudiniSopElement>("Instance"));
    map.emplace("Null", std::make_unique<HoudiniSopElement>("Null"));
    map.emplace("Pack", std::make_unique<HoudiniSopElement>("Pack"));
    map.emplace("Stash", std::make_unique<HoudiniSopElement>("Stash"));
    map.emplace("Unpack", std::make_unique<HoudiniSopElement>("Unpack"));
    map.emplace("CrossSectionSurface", std::make_unique<HoudiniSopElement>("CrossSectionSurface"));
    map.emplace("CurveIntersect", std::make_unique<HoudiniSopElement>("CurveIntersect"));
    map.emplace("OrientationAlongCurve", std::make_unique<HoudiniSopElement>("OrientationAlongCurve"));
    map.emplace("PlanarPatch", std::make_unique<HoudiniSopElement>("PlanarPatch"));
    map.emplace("PlanarPatchFromCurves", std::make_unique<HoudiniSopElement>("PlanarPatchFromCurves"));
    map.emplace("Rails", std::make_unique<HoudiniSopElement>("Rails"));
    map.emplace("Skin", std::make_unique<HoudiniSopElement>("Skin"));
    map.emplace("SplineAlign", std::make_unique<HoudiniSopElement>("SplineAlign"));
    map.emplace("SplineBasis", std::make_unique<HoudiniSopElement>("SplineBasis"));
    map.emplace("SplineCap", std::make_unique<HoudiniSopElement>("SplineCap"));
    map.emplace("SplineClay", std::make_unique<HoudiniSopElement>("SplineClay"));
    map.emplace("SplineCreep", std::make_unique<HoudiniSopElement>("SplineCreep"));
    map.emplace("SplineCurveClay", std::make_unique<HoudiniSopElement>("SplineCurveClay"));
    map.emplace("SplineFillet", std::make_unique<HoudiniSopElement>("SplineFillet"));
    map.emplace("SplineFit", std::make_unique<HoudiniSopElement>("SplineFit"));
    map.emplace("SplineProfile", std::make_unique<HoudiniSopElement>("SplineProfile"));
    map.emplace("SplineProject", std::make_unique<HoudiniSopElement>("SplineProject"));
    map.emplace("SplineRound", std::make_unique<HoudiniSopElement>("SplineRound"));
    map.emplace("SplineSurfsect", std::make_unique<HoudiniSopElement>("SplineSurfsect"));
    map.emplace("SplineTrim", std::make_unique<HoudiniSopElement>("SplineTrim"));
    map.emplace("Cluster", std::make_unique<HoudiniSopElement>("Cluster"));
    map.emplace("ClusterPoints", std::make_unique<HoudiniSopElement>("ClusterPoints"));
    map.emplace("PointGenerate", std::make_unique<HoudiniSopElement>("PointGenerate"));
    map.emplace("PointJitter", std::make_unique<HoudiniSopElement>("PointJitter"));
    map.emplace("PointReplicate", std::make_unique<HoudiniSopElement>("PointReplicate"));
    map.emplace("PointWeld", std::make_unique<HoudiniSopElement>("PointWeld"));
    map.emplace("Scatter", std::make_unique<HoudiniSopElement>("Scatter"));
    map.emplace("ScatterAndAlign", std::make_unique<HoudiniSopElement>("ScatterAndAlign"));
    map.emplace("UVAutoSeam", std::make_unique<HoudiniSopElement>("UVAutoSeam"));
    map.emplace("UVBrush", std::make_unique<HoudiniSopElement>("UVBrush"));
    map.emplace("UVEdit", std::make_unique<HoudiniSopElement>("UVEdit"));
    map.emplace("UVFlatten", std::make_unique<HoudiniSopElement>("UVFlatten"));
    map.emplace("UVFlattenFromPoints", std::make_unique<HoudiniSopElement>("UVFlattenFromPoints"));
    map.emplace("UVFuse", std::make_unique<HoudiniSopElement>("UVFuse"));
    map.emplace("UVLayout", std::make_unique<HoudiniSopElement>("UVLayout"));
    map.emplace("UVPelt", std::make_unique<HoudiniSopElement>("UVPelt"));
    map.emplace("UVProject", std::make_unique<HoudiniSopElement>("UVProject"));
    map.emplace("UVRelax", std::make_unique<HoudiniSopElement>("UVRelax"));
    map.emplace("UVTransform", std::make_unique<HoudiniSopElement>("UVTransform"));
    map.emplace("UVUnwrap", std::make_unique<HoudiniSopElement>("UVUnwrap"));
    map.emplace("Convert", std::make_unique<HoudiniSopElement>("Convert"));
    map.emplace("DistanceAlongGeometry", std::make_unique<HoudiniSopElement>("DistanceAlongGeometry"));
    map.emplace("DistanceFromGeometry", std::make_unique<HoudiniSopElement>("DistanceFromGeometry"));
    map.emplace("Facet", std::make_unique<HoudiniSopElement>("Facet"));
    map.emplace("Flatten", std::make_unique<HoudiniSopElement>("Flatten"));
    map.emplace("IntersectionAnalysis", std::make_unique<HoudiniSopElement>("IntersectionAnalysis"));
    map.emplace("IntersectionStitch", std::make_unique<HoudiniSopElement>("IntersectionStitch"));
    map.emplace("Join", std::make_unique<HoudiniSopElement>("Join"));
    map.emplace("MatchAxis", std::make_unique<HoudiniSopElement>("MatchAxis"));
    map.emplace("MatchTopology", std::make_unique<HoudiniSopElement>("MatchTopology"));
    map.emplace("MeasureThickness", std::make_unique<HoudiniSopElement>("MeasureThickness"));
    map.emplace("PrimitiveProperties", std::make_unique<HoudiniSopElement>("PrimitiveProperties"));
    map.emplace("Proximity", std::make_unique<HoudiniSopElement>("Proximity"));
    map.emplace("SeparatePieces", std::make_unique<HoudiniSopElement>("SeparatePieces"));
    map.emplace("WindingNumber", std::make_unique<HoudiniSopElement>("WindingNumber"));
    map.emplace("ConvertVDB", std::make_unique<HoudiniSopElement>("ConvertVDB"));
    map.emplace("ImplicitSurface", std::make_unique<HoudiniSopElement>("ImplicitSurface"));
    map.emplace("IsoOffset", std::make_unique<HoudiniSopElement>("IsoOffset"));
    map.emplace("MetaGroups", std::make_unique<HoudiniSopElement>("MetaGroups"));
    map.emplace("VDB", std::make_unique<HoudiniSopElement>("VDB"));
    map.emplace("VDBFromPolygons", std::make_unique<HoudiniSopElement>("VDBFromPolygons"));
    map.emplace("Volume", std::make_unique<HoudiniSopElement>("Volume"));
    map.emplace("VolumeSDF", std::make_unique<HoudiniSopElement>("VolumeSDF"));
}

} // namespace pcg::internal::elements
