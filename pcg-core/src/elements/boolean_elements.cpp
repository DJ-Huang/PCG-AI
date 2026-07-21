#include "elements/boolean_elements.hpp"
#include "elements/element_utils.hpp"
#include "elements/pcg_element.hpp"
#include "geometry/boolean_output.hpp"
#include "geometry/arrangement.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace pcg::internal::elements {

using namespace ::pcg::internal::geometry;

// ── boolean_geometry implementation ────────────────────────

data::PcgGeometry boolean_geometry(const data::PcgGeometry& a,
                                    const data::PcgGeometry& b,
                                    const BooleanOptions& opts)
{
    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        return {};

    // Best-effort: rematerialize user face groups onto boolean triangles using
    // face_origins. Detriangulation may still drop them; callers should rebuild
    // critical groups with GroupCreate when needed (PCG Block AI Core discipline).
    if (result.face_origins.size() == result.geometry.faces().size()) {
        auto rematerialize = [&](const data::PcgGeometry& source, int source_index) {
            for (const auto& name :
                 source.groups().group_names(geometry::GroupDomain::Face)) {
                if (name == BooleanGroups::A_INSIDE_B || name == BooleanGroups::A_OUTSIDE_B ||
                    name == BooleanGroups::B_INSIDE_A || name == BooleanGroups::B_OUTSIDE_A)
                    continue;
                const auto members =
                    source.groups().members(geometry::GroupDomain::Face, name);
                std::unordered_set<geometry::GroupId> member_set(members.begin(), members.end());
                for (size_t fi = 0; fi < result.face_origins.size(); ++fi) {
                    const auto& origin = result.face_origins[fi];
                    if (origin.source != source_index || origin.original_face < 0)
                        continue;
                    if (member_set.count(origin.original_face) > 0)
                        result.geometry.groups().add(geometry::GroupDomain::Face, name,
                                                     static_cast<geometry::GroupId>(fi));
                }
            }
        };
        rematerialize(a, 0);
        rematerialize(b, 1);
    }

    return finalize_boolean_output(result, opts.detriangulate);
}

namespace {

class BooleanMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "BooleanMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BooleanMesh missing node");

        const data::PcgGeometry a =
            get_geometry_input(ctx, "a", "BooleanMesh missing mesh A");
        if (a.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BooleanMesh missing mesh A");

        const data::PcgGeometry b =
            get_geometry_input(ctx, "b", "BooleanMesh missing mesh B");
        if (b.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BooleanMesh missing mesh B");

        BooleanOptions opts;

        const std::string op_str = ctx.node->data.value("operation", std::string("subtract"));
        if (op_str == "union")          opts.operation = BooleanOp::Union;
        else if (op_str == "intersect") opts.operation = BooleanOp::Intersect;
        else if (op_str == "shatter")   opts.operation = BooleanOp::Shatter;
        else                             opts.operation = BooleanOp::Subtract;

        const std::string treat_a = ctx.node->data.value("treatAAs", std::string("solid"));
        opts.treat_a_as = (treat_a == "surface") ? MeshTreatment::Surface : MeshTreatment::Solid;

        const std::string treat_b = ctx.node->data.value("treatBAs", std::string("solid"));
        opts.treat_b_as = (treat_b == "surface") ? MeshTreatment::Surface : MeshTreatment::Solid;

        opts.use_self = ctx.node->data.value("useSelf", false);

        const std::string detri = ctx.node->data.value("detriangulate", std::string("all"));
        if (detri == "unchanged")   opts.detriangulate = DetriangulateMode::Unchanged;
        else if (detri == "none")   opts.detriangulate = DetriangulateMode::None;
        else                         opts.detriangulate = DetriangulateMode::All;

        opts.weld_epsilon = ctx.node->data.value("weldEpsilon", 0.0001);
        opts.triangle_budget = ctx.node->data.value("triangleBudget", 500000);

        data::PcgGeometry result = boolean_geometry(a, b, opts);
        if (result.points().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BooleanMesh produced empty result");

        emit_geometry(ctx, std::move(result));
        return PCG_OK;
    }
};

} // namespace

void register_boolean_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("BooleanMesh", std::make_unique<BooleanMeshElement>());
}

} // namespace pcg::internal::elements
