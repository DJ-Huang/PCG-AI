#include "elements/add_elements.hpp"

#include "elements/add_algorithms.hpp"
#include "elements/element_utils.hpp"
#include "elements/pcg_element.hpp"

namespace pcg::internal::elements {
namespace {

class AddElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Add"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "Add missing node");

        data::PcgGeometry storage;
        const data::PcgGeometry* input = optional_geometry_input(ctx, "in", storage);
        const data::PcgGeometry base = input ? *input : data::PcgGeometry{};

        AddOptions options;
        options.delete_primitives_keep_points =
            ctx.node->data.value("deletePrimitivesKeepPoints", false);
        options.points = parse_add_points(ctx.node->data);
        options.polygons_spec = ctx.node->data.value("polygons", std::string(""));

        emit_geometry(ctx, add_geometry(base, options));
        return PCG_OK;
    }
};

} // namespace

void register_add_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("Add", std::make_unique<AddElement>());
}

} // namespace pcg::internal::elements
