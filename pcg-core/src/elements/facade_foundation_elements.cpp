#include "elements/facade_foundation_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/facade_foundation_algorithms.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace pcg::internal::elements {
namespace {

class ForEachBeginElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ForEachBegin"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        // Passthrough marker — graph_executor owns real piece/count looping.
        if (auto geometry = ctx.inputs.find_geometry_shared("in")) {
            ctx.outputs.add_geometry_shared("out", geometry);
            return PCG_OK;
        }
        const auto input = get_geometry_input(ctx, "in", "ForEachBegin missing mesh input");
        emit_geometry(ctx, input);
        return PCG_OK;
    }
};

class ForEachEndElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ForEachEnd"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (auto geometry = ctx.inputs.find_geometry_shared("in")) {
            ctx.outputs.add_geometry_shared("out", geometry);
            return PCG_OK;
        }
        const auto input = get_geometry_input(ctx, "in", "ForEachEnd missing mesh input");
        emit_geometry(ctx, input);
        return PCG_OK;
    }
};

class PrimitiveTransformElement final : public IPcgElement {
public:
    const char* type_name() const override { return "PrimitiveTransform"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input =
            get_geometry_input(ctx, "in", "PrimitiveTransform missing mesh input");
        if (input.faces().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "PrimitiveTransform input has no faces");

        PrimitiveTransformOptions options;
        options.scale = ctx.node->data.value("scale", 0.85);
        options.face_group = ctx.node->data.value("faceGroup", std::string());
        emit_geometry(ctx, primitive_transform_geometry(input, options));
        return PCG_OK;
    }
};

class ConvertLineElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ConvertLine"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "ConvertLine missing mesh input");
        ConvertLineOptions options;
        options.edge_group = ctx.node->data.value(
            "group", ctx.node->data.value("edgeGroup", std::string()));
        if (ctx.node->data.contains("mode"))
            options.mode = ctx.node->data.value("mode", std::string("unshared"));
        else if (!options.edge_group.empty())
            options.mode = "group";
        else
            options.mode = "all";
        options.connect_path = ctx.node->data.value("connectPath", true);
        options.max_distance = ctx.node->data.value("maxDistance", 1e-4);
        options.connect_only_to_other_end_points =
            ctx.node->data.value("connectOnlyToOtherEndPoints", false);
        options.keep_group_order = ctx.node->data.value("keepGroupOrder", false);
        options.make_isolated_loops_closed =
            ctx.node->data.value("makeIsolatedLoopsClosed", false);
        options.remove_unused_points = ctx.node->data.value("removeUnusedPoints", true);
        options.compute_length = ctx.node->data.value("computeLength", false);
        options.length_attribute =
            ctx.node->data.value("lengthAttribute", std::string("restlength"));
        auto splines = convert_line_geometry(input, options);
        if (splines.splines().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ConvertLine produced no lines");
        emit_splines(ctx, std::move(splines));
        return PCG_OK;
    }
};

class ExtractCentroidElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ExtractCentroid"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input =
            get_geometry_input(ctx, "in", "ExtractCentroid missing mesh input");
        ExtractCentroidOptions options;
        options.method = ctx.node->data.value("method", std::string("primitives"));
        options.face_group = ctx.node->data.value("faceGroup", std::string());
        emit_points(ctx, extract_centroid_geometry(input, options));
        return PCG_OK;
    }
};

class GroupTransferElement final : public IPcgElement {
public:
    const char* type_name() const override { return "GroupTransfer"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto target =
            get_geometry_input(ctx, "target", "GroupTransfer missing target input");
        const auto source =
            get_geometry_input(ctx, "source", "GroupTransfer missing source input");
        GroupTransferOptions options;
        options.transfer_primitives = ctx.node->data.value("transferPrimitiveGroups", true);
        options.primitive_groups = ctx.node->data.value("primitiveGroups", std::string());
        options.primitive_group_prefix =
            ctx.node->data.value("primitiveGroupPrefix", std::string());
        options.transfer_points = ctx.node->data.value("transferPointGroups", true);
        options.point_groups = ctx.node->data.value("pointGroups", std::string());
        options.point_group_prefix = ctx.node->data.value("pointGroupPrefix", std::string());
        options.transfer_edges = ctx.node->data.value("transferEdgeGroups", true);
        options.edge_groups = ctx.node->data.value("edgeGroups", std::string());
        options.edge_group_prefix = ctx.node->data.value("edgeGroupPrefix", std::string());
        options.group_name_conflict =
            ctx.node->data.value("groupNameConflict", std::string("skip"));
        options.enable_distance_threshold =
            ctx.node->data.value("enableDistanceThreshold", true);
        options.distance_threshold = ctx.node->data.value("distanceThreshold", 10.0);
        options.create_empty_groups = ctx.node->data.value("createEmptyGroups", true);

        // Legacy single-group API: groupName + domain + distance.
        const std::string legacy_name = ctx.node->data.value("groupName", std::string());
        if (!legacy_name.empty()) {
            const std::string domain = ctx.node->data.value("domain", std::string("face"));
            options.transfer_primitives = domain == "face";
            options.transfer_points = domain == "point";
            options.transfer_edges = false;
            options.primitive_groups = domain == "face" ? legacy_name : std::string();
            options.point_groups = domain == "point" ? legacy_name : std::string();
            options.edge_groups.clear();
            options.enable_distance_threshold = true;
            if (ctx.node->data.contains("distance"))
                options.distance_threshold = ctx.node->data.value("distance", 0.01);
            options.group_name_conflict = "overwrite";
            options.create_empty_groups = false;
        }

        if (!options.transfer_primitives && !options.transfer_points && !options.transfer_edges)
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "GroupTransfer: enable at least one of Primitive/Point/Edge Groups");

        emit_geometry(ctx, group_transfer_geometry(target, source, options));
        return PCG_OK;
    }
};

class ClipElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Clip"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        const auto input = get_geometry_input(ctx, "in", "Clip missing mesh input");
        ClipOptions options;
        options.origin_x = ctx.node->data.value("originX", 0.0);
        options.origin_y = ctx.node->data.value("originY", 0.0);
        options.origin_z = ctx.node->data.value("originZ", 0.0);
        options.normal_x = ctx.node->data.value("normalX", 0.0);
        options.normal_y = ctx.node->data.value("normalY", 1.0);
        options.normal_z = ctx.node->data.value("normalZ", 0.0);
        options.keep_positive = ctx.node->data.value("keepPositive", true);
        emit_geometry(ctx, clip_geometry(input, options));
        return PCG_OK;
    }
};

} // namespace

void register_facade_foundation_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("ForEachBegin", std::make_unique<ForEachBeginElement>());
    map.emplace("ForEachEnd", std::make_unique<ForEachEndElement>());
    map.emplace("PrimitiveTransform", std::make_unique<PrimitiveTransformElement>());
    map.emplace("ConvertLine", std::make_unique<ConvertLineElement>());
    map.emplace("ExtractCentroid", std::make_unique<ExtractCentroidElement>());
    map.emplace("GroupTransfer", std::make_unique<GroupTransferElement>());
    map.emplace("Clip", std::make_unique<ClipElement>());
}

} // namespace pcg::internal::elements
