#include "elements/vehicle_modeling_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/vehicle_modeling_algorithms.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace pcg::internal::elements {
namespace {

std::vector<data::PcgSplinePoint> parse_bezier_points(const nlohmann::json& data, const char* key)
{
    std::vector<data::PcgSplinePoint> controls;
    try {
        const auto parsed = nlohmann::json::parse(data.value(key, std::string("[]")));
        if (!parsed.is_array()) return controls;
        for (const auto& item : parsed)
            controls.push_back({item.value("x", 0.0), item.value("y", 0.0), item.value("z", 0.0)});
    } catch (...) {
    }
    return controls;
}

data::PcgSplineData create_bezier_spline(const std::vector<data::PcgSplinePoint>& controls,
                                         const std::vector<data::PcgSplinePoint>& tangents,
                                         int subdivisions,
                                         bool closed)
{
    data::PcgSplineData output;
    if (controls.size() < 2) return output;
    data::PcgSpline spline;
    spline.closed = closed;
    const int steps = std::max(1, subdivisions);

    if (tangents.size() == controls.size()) {
        const int segment_count = closed ? static_cast<int>(controls.size())
                                         : static_cast<int>(controls.size()) - 1;
        for (int segment = 0; segment < segment_count; ++segment) {
            const int next = (segment + 1) % static_cast<int>(controls.size());
            const auto& p0 = controls[static_cast<size_t>(segment)];
            const auto& p1 = controls[static_cast<size_t>(next)];
            const auto& m0 = tangents[static_cast<size_t>(segment)];
            const auto& m1 = tangents[static_cast<size_t>(next)];
            for (int step = 0; step <= steps; ++step) {
                if (segment > 0 && step == 0) continue;
                if (closed && segment + 1 == segment_count && step == steps) continue;
                const double t = static_cast<double>(step) / steps;
                const double t2 = t * t;
                const double t3 = t2 * t;
                const double h00 = 2.0*t3 - 3.0*t2 + 1.0;
                const double h10 = t3 - 2.0*t2 + t;
                const double h01 = -2.0*t3 + 3.0*t2;
                const double h11 = t3 - t2;
                spline.points.push_back({
                    h00*p0.x + h10*m0.x + h01*p1.x + h11*m1.x,
                    h00*p0.y + h10*m0.y + h01*p1.y + h11*m1.y,
                    h00*p0.z + h10*m0.z + h01*p1.z + h11*m1.z,
                });
            }
        }
        output.add_spline(std::move(spline));
        return output;
    }

    if (controls.size() < 4 || (controls.size() - 1) % 3 != 0) return output;
    const int segment_count = static_cast<int>((controls.size() - 1) / 3);
    for (int segment = 0; segment < segment_count; ++segment) {
        const auto& p0 = controls[static_cast<size_t>(segment * 3)];
        const auto& p1 = controls[static_cast<size_t>(segment * 3 + 1)];
        const auto& p2 = controls[static_cast<size_t>(segment * 3 + 2)];
        const auto& p3 = controls[static_cast<size_t>(segment * 3 + 3)];
        for (int step = 0; step <= steps; ++step) {
            if (segment > 0 && step == 0) continue;
            const double t = static_cast<double>(step) / steps;
            const double s = 1.0 - t;
            spline.points.push_back({
                s*s*s*p0.x + 3.0*s*s*t*p1.x + 3.0*s*t*t*p2.x + t*t*t*p3.x,
                s*s*s*p0.y + 3.0*s*s*t*p1.y + 3.0*s*t*t*p2.y + t*t*t*p3.y,
                s*s*s*p0.z + 3.0*s*s*t*p1.z + 3.0*s*t*t*p2.z + t*t*t*p3.z,
            });
        }
    }
    if (closed && spline.points.size() > 1) {
        const auto& first = spline.points.front();
        const auto& last = spline.points.back();
        const double dx = first.x - last.x;
        const double dy = first.y - last.y;
        const double dz = first.z - last.z;
        if (dx*dx + dy*dy + dz*dz < 0.000000000001)
            spline.points.pop_back();
    }
    output.add_spline(std::move(spline));
    return output;
}

class CreateBezierSplineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CreateBezierSpline"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node) return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateBezierSpline missing node");
        auto spline = create_bezier_spline(
            parse_bezier_points(ctx.node->data, "controlPoints"),
            parse_bezier_points(ctx.node->data, "tangents"),
            ctx.node->data.value("subdivisions", 12), ctx.node->data.value("closed", false));
        if (spline.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                "CreateBezierSpline requires anchors plus matching tangents, or 3n+1 cubic controls");
        emit_splines(ctx, std::move(spline));
        return PCG_OK;
    }
};

class LoftMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "LoftMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node) return fail_ctx(ctx, PCG_ERR_EXECUTION, "LoftMesh missing node");
        std::vector<data::PcgSpline> profiles;
        for (const auto& item : ctx.inputs.items()) {
            if (item.tag != "profiles" || !item.splines.has_value()) continue;
            for (const auto& spline : item.splines->splines()) profiles.push_back(spline);
        }
        if (profiles.size() < 2)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LoftMesh requires at least two profile splines");
        LoftMeshOptions options;
        options.columns = ctx.node->data.value("columns", 32);
        options.sort_axis = ctx.node->data.value("sortAxis", std::string("x"));
        options.closed_profile = ctx.node->data.value("closedProfile", true);
        options.cap_start = ctx.node->data.value("capStart", true);
        options.cap_end = ctx.node->data.value("capEnd", true);
        options.auto_align = ctx.node->data.value("autoAlign", true);
        options.shade_mode = ctx.node->data.value("shadeMode", std::string("auto"));
        options.cusp_angle_deg = ctx.node->data.value("cuspAngle", 30.0);
        auto geometry = loft_splines(profiles, options);
        if (geometry.points().empty()) return fail_ctx(ctx, PCG_ERR_EXECUTION, "LoftMesh invalid profiles");
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
};

class MirrorMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MirrorMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "MirrorMesh missing mesh input");
        if (input.points().empty()) return PCG_ERR_EXECUTION;
        MirrorMeshOptions options;
        options.axis = ctx.node->data.value("axis", std::string("z"));
        options.offset = ctx.node->data.value("offset", 0.0);
        options.merge_original = ctx.node->data.value("mergeOriginal", true);
        options.weld_seam = ctx.node->data.value("weldSeam", true);
        options.weld_tolerance = ctx.node->data.value("weldTolerance", 0.0001);
        emit_geometry(ctx, mirror_geometry(input, options));
        return PCG_OK;
    }
};

class FuseMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "FuseMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "FuseMesh missing mesh input");
        if (input.points().empty()) return PCG_ERR_EXECUTION;
        emit_geometry(ctx, fuse_geometry(input, FuseMeshOptions{
            ctx.node->data.value("tolerance", 0.0001),
            ctx.node->data.value("removeDegenerate", true)}));
        return PCG_OK;
    }
};

class PolyExtrudeElement final : public IPcgElement {
public:
    const char* type_name() const override { return "PolyExtrude"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "PolyExtrude missing mesh input");
        if (input.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "PolyExtrude input has no points");
        PolyExtrudeOptions options;
        options.face_group = ctx.node->data.value("faceGroup", std::string());
        options.distance = ctx.node->data.value("distance", 0.02);
        options.distance_attribute = ctx.node->data.value("distanceAttribute", std::string());
        options.inset = ctx.node->data.value("inset", 0.0);
        options.keep_original = ctx.node->data.value("keepOriginal", false);
        options.top_group = ctx.node->data.value("topGroup", std::string("extrude_top"));
        options.side_group = ctx.node->data.value("sideGroup", std::string("extrude_side"));
        emit_geometry(ctx, poly_extrude_geometry(input, options));
        return PCG_OK;
    }
};

class CopyMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CopyMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "CopyMesh missing mesh input");
        if (input.points().empty()) return PCG_ERR_EXECUTION;
        CopyMeshOptions options;
        options.mode = ctx.node->data.value("mode", std::string("circular"));
        options.count = ctx.node->data.value("count", 6);
        options.axis = ctx.node->data.value("axis", std::string("x"));
        options.angle = ctx.node->data.value("angle", 360.0);
        options.translate_x = ctx.node->data.value("translateX", 0.0);
        options.translate_y = ctx.node->data.value("translateY", 0.0);
        options.translate_z = ctx.node->data.value("translateZ", 0.0);
        options.center_x = ctx.node->data.value("centerX", 0.0);
        options.center_y = ctx.node->data.value("centerY", 0.0);
        options.center_z = ctx.node->data.value("centerZ", 0.0);
        emit_geometry(ctx, copy_geometry(input, options));
        return PCG_OK;
    }
};

class ShellMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ShellMesh"; }
    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "ShellMesh missing mesh input");
        if (input.points().empty()) return PCG_ERR_EXECUTION;
        ShellMeshOptions options;
        options.thickness = ctx.node->data.value("thickness", 0.02);
        options.direction = ctx.node->data.value("direction", std::string("centered"));
        options.close_boundaries = ctx.node->data.value("closeBoundaries", true);
        options.outer_group = ctx.node->data.value("outerGroup", std::string("shell_outer"));
        options.inner_group = ctx.node->data.value("innerGroup", std::string("shell_inner"));
        options.rim_group = ctx.node->data.value("rimGroup", std::string("shell_rim"));
        auto geometry = shell_geometry(input, options);
        if (geometry.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ShellMesh thickness must be positive");
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
};

} // namespace

void register_vehicle_modeling_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("CreateBezierSpline", std::make_unique<CreateBezierSplineElement>());
    map.emplace("LoftMesh", std::make_unique<LoftMeshElement>());
    map.emplace("MirrorMesh", std::make_unique<MirrorMeshElement>());
    map.emplace("FuseMesh", std::make_unique<FuseMeshElement>());
    map.emplace("PolyExtrude", std::make_unique<PolyExtrudeElement>());
    map.emplace("CopyMesh", std::make_unique<CopyMeshElement>());
    map.emplace("ShellMesh", std::make_unique<ShellMeshElement>());
}

} // namespace pcg::internal::elements
