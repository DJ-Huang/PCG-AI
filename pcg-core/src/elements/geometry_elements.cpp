#include "elements/geometry_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/geometry_algorithms.hpp"
#include "elements/pcg_element.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

class GroupCreateElement final : public IPcgElement {
public:
    const char* type_name() const override { return "GroupCreate"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GroupCreate missing node");

        const data::PcgGeometry input =
            get_geometry_input(ctx, "in", "GroupCreate missing geometry input");
        if (input.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GroupCreate missing geometry input");

        data::PcgGeometry bounding_storage;
        const data::PcgGeometry* bounding =
            optional_geometry_input(ctx, "bounding", bounding_storage);

        GroupCreateOptions opts;
        opts.output_group = ctx.node->data.value("outputGroup", std::string("bevel_edges"));
        opts.domain = ctx.node->data.value("domain", std::string("edge"));
        opts.initial_merge = ctx.node->data.value("initialMerge", std::string("replace"));

        const bool has_new_enables =
            ctx.node->data.contains("enableBaseGroup") || ctx.node->data.contains("enableBounding")
            || ctx.node->data.contains("enableNormals") || ctx.node->data.contains("enableEdges")
            || ctx.node->data.contains("enableRandom");

        opts.mode = ctx.node->data.value("mode", std::string("angle"));
        opts.min_edge_angle_deg = ctx.node->data.value("minEdgeAngle", 30.0);
        opts.include_unshared = ctx.node->data.value("includeUnshared", false);
        opts.from_face_groups = parse_name_list(ctx.node->data, "fromFaceGroup");
        opts.from_edge_groups = parse_name_list(ctx.node->data, "fromEdgeGroup");
        opts.base_groups = parse_name_list(ctx.node->data, "baseGroup");

        opts.enable_base_group = ctx.node->data.value("enableBaseGroup", false);
        opts.enable_bounding = ctx.node->data.value("enableBounding", false);
        opts.enable_normals = ctx.node->data.value("enableNormals", false);
        opts.enable_edges = ctx.node->data.value("enableEdges", true);
        opts.enable_random = ctx.node->data.value("enableRandom", false);

        opts.direction_x = ctx.node->data.value("directionX", 0.0);
        opts.direction_y = ctx.node->data.value("directionY", 1.0);
        opts.direction_z = ctx.node->data.value("directionZ", 0.0);
        opts.spread_angle_deg = ctx.node->data.value("spreadAngle", 30.0);
        opts.random_chance = ctx.node->data.value("randomChance", 1.0);
        opts.random_seed = ctx.node->data.value("randomSeed", 0);

        if (!has_new_enables) {
            // Legacy graphs: mode drives which filter is active.
            if (opts.mode == "all") {
                opts.enable_edges = false;
                opts.enable_base_group = true;
                opts.base_groups.clear();
            } else {
                opts.enable_edges = true;
                opts.enable_base_group = false;
            }
            opts.enable_bounding = false;
            opts.enable_normals = false;
            opts.enable_random = false;
        }

        if (opts.enable_bounding && bounding == nullptr)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "GroupCreate enableBounding requires bounding input");

        emit_geometry(ctx, group_create(input, opts, bounding));
        return PCG_OK;
    }
};

class GroupCombineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "GroupCombine"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GroupCombine missing node");

        const data::PcgGeometry input =
            get_geometry_input(ctx, "in", "GroupCombine missing geometry input");
        if (input.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GroupCombine missing geometry input");

        GroupCombineOptions opts;
        opts.output_group = ctx.node->data.value("outputGroup", std::string("combined"));
        opts.domain = ctx.node->data.value("domain", std::string("edge"));
        opts.operation = ctx.node->data.value("operation", std::string("union"));
        opts.source_groups = parse_name_list(ctx.node->data, "sourceGroups");

        emit_geometry(ctx, group_combine(input, opts));
        return PCG_OK;
    }
};

class FaceGroupByNormalElement final : public IPcgElement {
public:
    const char* type_name() const override { return "FaceGroupByNormal"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "FaceGroupByNormal missing node");

        const data::PcgGeometry input =
            get_geometry_input(ctx, "in", "FaceGroupByNormal missing geometry input");
        if (input.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "FaceGroupByNormal missing geometry input");

        FaceGroupByNormalOptions opts;
        opts.output_group = ctx.node->data.value("outputGroup", std::string("material_faces"));
        opts.direction_x = ctx.node->data.value("directionX", 0.0);
        opts.direction_y = ctx.node->data.value("directionY", 1.0);
        opts.direction_z = ctx.node->data.value("directionZ", 0.0);
        opts.spread_angle_deg = ctx.node->data.value("spreadAngle", 30.0);

        emit_geometry(ctx, face_group_by_normal(input, opts));
        return PCG_OK;
    }
};

class GroupPromoteElement final : public IPcgElement {
public:
    const char* type_name() const override { return "GroupPromote"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GroupPromote missing node");

        const data::PcgGeometry input =
            get_geometry_input(ctx, "in", "GroupPromote missing geometry input");
        if (input.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GroupPromote missing geometry input");

        GroupPromoteOptions opts;
        opts.from_domain = ctx.node->data.value("convertFrom", std::string("point"));
        opts.to_domain = ctx.node->data.value("to", std::string("edge"));
        opts.group_name = ctx.node->data.value("groupName", std::string(""));
        opts.new_name = ctx.node->data.value("newName", std::string(""));
        opts.keep_original_group = ctx.node->data.value("keepOriginalGroup", false);
        opts.include_only_on_boundary = ctx.node->data.value("includeOnlyOnBoundary", false);
        opts.include_unshared_edges = ctx.node->data.value("includeUnsharedEdges", true);
        opts.include_all_unshared_curve_edges =
            ctx.node->data.value("includeAllUnsharedCurveEdges", true);
        opts.use_connectivity_attribute =
            ctx.node->data.value("useConnectivityAttribute", false);
        opts.connectivity_attribute =
            ctx.node->data.value("connectivityAttribute", std::string("uv"));
        opts.connectivity_attribute_tolerance =
            ctx.node->data.value("connectivityAttributeTolerance", 0.0001);
        opts.include_all_primitives_sharing_attribute_boundary_points =
            ctx.node->data.value("includeAllPrimitivesSharingAttributeBoundaryPoints", false);
        opts.include_only_entirely_contained =
            ctx.node->data.value("includeOnlyEntirelyContained", true);
        opts.include_only_primitives_sharing_edge =
            ctx.node->data.value("includeOnlyPrimitivesSharingEdge", false);
        opts.remove_degenerate_bridges = ctx.node->data.value("removeDegenerateBridges", false);
        opts.output_as_integer_attribute = ctx.node->data.value("outputAsIntegerAttribute", false);

        if (opts.group_name.empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GroupPromote requires groupName");

        emit_geometry(ctx, group_promote(input, opts));
        return PCG_OK;
    }
};

class GroupDeleteElement final : public IPcgElement {
public:
    const char* type_name() const override { return "GroupDelete"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "GroupDelete missing node");

        data::PcgGeometry storage;
        const data::PcgGeometry* input = optional_geometry_input(ctx, "in", storage);
        if (input == nullptr) {
            const data::PcgTaggedData* tagged = ctx.inputs.find("in");
            if (tagged != nullptr && tagged->geometry)
                input = tagged->geometry.get();
            else if (tagged != nullptr && tagged->mesh && !tagged->mesh->vertices().empty()) {
                storage = data::geometry_from_mesh(*tagged->mesh);
                input = &storage;
            }
        }

        if (input == nullptr) {
            const data::PcgTaggedData* tagged = ctx.inputs.find("in");
            if (tagged != nullptr && tagged->splines) {
                emit_splines(ctx, *tagged->splines);
                return PCG_OK;
            }
            if (tagged != nullptr && tagged->points) {
                emit_points(ctx, *tagged->points);
                return PCG_OK;
            }
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "GroupDelete missing geometry input (connect Geometry/Mesh to input)");
        }

        GroupDeleteOptions opts;
        opts.rules = parse_group_delete_rules(ctx.node->data);
        opts.delete_unused_groups = ctx.node->data.value("deleteUnusedGroups", false);

        emit_geometry(ctx, group_delete(*input, opts));
        return PCG_OK;
    }
};

} // namespace

void register_geometry_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("GroupCreate", std::make_unique<GroupCreateElement>());
    map.emplace("GroupCombine", std::make_unique<GroupCombineElement>());
    map.emplace("FaceGroupByNormal", std::make_unique<FaceGroupByNormalElement>());
    map.emplace("GroupPromote", std::make_unique<GroupPromoteElement>());
    map.emplace("GroupDelete", std::make_unique<GroupDeleteElement>());
}

} // namespace pcg::internal::elements
