#include "elements/boolean_elements.hpp"
#include "elements/element_utils.hpp"
#include "elements/pcg_element.hpp"
#include "elements/topology_parity_algorithms.hpp"
#include "geometry/boolean_output.hpp"
#include "geometry/arrangement.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace pcg::internal::elements {

using namespace ::pcg::internal::geometry;

namespace {

// Boolean rebuilds topology and drops AttributeTable. Lot-city SwitchIf flags
// (hasBalcony / hasFireEscape / roofType / numFloors) live on detail or prim0 —
// promote them onto the result so downstream SwitchIf still resolves.
void preserve_control_attributes(const data::PcgGeometry& source, data::PcgGeometry& dest)
{
    auto promote_number = [&](data::AttributeOwner owner, const std::string& name) {
        if (dest.attributes().find(data::AttributeOwner::Detail, name) != nullptr)
            return;
        const data::AttributeArray* attr = source.attributes().find(owner, name);
        if (!attr || attr->size() == 0)
            return;
        if (attr->schema().type == data::AttributeType::Int)
            set_detail_int(dest, name, attr->int_values()[0]);
        else if (attr->schema().type == data::AttributeType::Float)
            set_detail_float(dest, name, attr->float_values()[0]);
    };

    for (const auto& name : source.attributes().names(data::AttributeOwner::Detail))
        promote_number(data::AttributeOwner::Detail, name);
    for (const auto& name : source.attributes().names(data::AttributeOwner::Primitive))
        promote_number(data::AttributeOwner::Primitive, name);
}

const char* boolean_error_label(BooleanErrorType error)
{
    switch (error) {
    case BooleanErrorType::Ok: return "ok";
    case BooleanErrorType::NonManifold: return "non-manifold";
    case BooleanErrorType::PrecisionOverflow: return "precision overflow";
    case BooleanErrorType::TriangleBudgetExceeded: return "triangle budget exceeded";
    case BooleanErrorType::SelfIntersectionUnresolved: return "self-intersection unresolved";
    case BooleanErrorType::InvalidInput: return "invalid input";
    case BooleanErrorType::Cancelled: return "cancelled";
    case BooleanErrorType::Timeout: return "timeout";
    }
    return "unknown";
}

} // namespace

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

    data::PcgGeometry output = finalize_boolean_output(result, opts.detriangulate);
    preserve_control_attributes(a, output);
    return output;
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
        opts.timeout_ms = ctx.node->data.value("timeoutMs", 0);
        opts.is_cancel_requested = ctx.is_cancel_requested;

        const std::string on_failure =
            ctx.node->data.value("onFailure", std::string("error"));

        BooleanResult raw = execute_boolean(a, b, opts);
        if (raw.error != BooleanErrorType::Ok) {
            const std::string detail = raw.message.empty()
                ? boolean_error_label(raw.error)
                : raw.message;
            if (on_failure == "passthroughA") {
                // Observable: empty result is avoided; cook continues with A.
                // Detail is preserved in the error string only when failing hard.
                emit_geometry(ctx, data::PcgGeometry(a));
                return PCG_OK;
            }
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            ("BooleanMesh failed: " + detail).c_str());
        }

        // Rematerialize groups (same as boolean_geometry).
        if (raw.face_origins.size() == raw.geometry.faces().size()) {
            auto rematerialize = [&](const data::PcgGeometry& source, int source_index) {
                for (const auto& name :
                     source.groups().group_names(geometry::GroupDomain::Face)) {
                    if (name == BooleanGroups::A_INSIDE_B || name == BooleanGroups::A_OUTSIDE_B ||
                        name == BooleanGroups::B_INSIDE_A || name == BooleanGroups::B_OUTSIDE_A)
                        continue;
                    const auto members =
                        source.groups().members(geometry::GroupDomain::Face, name);
                    std::unordered_set<geometry::GroupId> member_set(members.begin(), members.end());
                    for (size_t fi = 0; fi < raw.face_origins.size(); ++fi) {
                        const auto& origin = raw.face_origins[fi];
                        if (origin.source != source_index || origin.original_face < 0)
                            continue;
                        if (member_set.count(origin.original_face) > 0)
                            raw.geometry.groups().add(geometry::GroupDomain::Face, name,
                                                      static_cast<geometry::GroupId>(fi));
                    }
                }
            };
            rematerialize(a, 0);
            rematerialize(b, 1);
        }

        data::PcgGeometry result = finalize_boolean_output(raw, opts.detriangulate);
        preserve_control_attributes(a, result);
        if (result.points().empty()) {
            if (on_failure == "passthroughA") {
                emit_geometry(ctx, data::PcgGeometry(a));
                return PCG_OK;
            }
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BooleanMesh produced empty result");
        }

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
