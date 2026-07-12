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

        GroupCreateOptions opts;
        opts.output_group = ctx.node->data.value("outputGroup", std::string("bevel_edges"));
        opts.domain = ctx.node->data.value("domain", std::string("edge"));
        opts.mode = ctx.node->data.value("mode", std::string("angle"));
        opts.min_edge_angle_deg = ctx.node->data.value("minEdgeAngle", 30.0);
        opts.include_unshared = ctx.node->data.value("includeUnshared", false);
        opts.from_face_groups = parse_name_list(ctx.node->data, "fromFaceGroup");
        opts.from_edge_group = ctx.node->data.value("fromEdgeGroup", std::string(""));

        emit_geometry(ctx, group_create(input, opts));
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

} // namespace

void register_geometry_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("GroupCreate", std::make_unique<GroupCreateElement>());
    map.emplace("GroupCombine", std::make_unique<GroupCombineElement>());
}

} // namespace pcg::internal::elements
