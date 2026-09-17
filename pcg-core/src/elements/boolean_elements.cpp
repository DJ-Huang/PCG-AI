#include "elements/boolean_elements.hpp"
#include "elements/element_utils.hpp"
#include "elements/pcg_element.hpp"
#include "elements/topology_parity_algorithms.hpp"
#include "geometry/boolean_output.hpp"
#include "geometry/arrangement.hpp"
#include "cook_diagnostics.hpp"

#include <algorithm>
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

// Best-effort group rematerialization. Detriangulation can still drop groups;
// callers should rebuild critical selections with GroupCreate when needed.
void rematerialize_groups(BooleanResult& result, const data::PcgGeometry& a,
                          const data::PcgGeometry& b)
{
    if (result.face_origins.size() != result.geometry.faces().size())
        return;
    auto rematerialize = [&](const data::PcgGeometry& source, int source_index) {
        for (const auto& name : source.groups().group_names(geometry::GroupDomain::Face)) {
            if (name == BooleanGroups::A_INSIDE_B || name == BooleanGroups::A_OUTSIDE_B ||
                name == BooleanGroups::B_INSIDE_A || name == BooleanGroups::B_OUTSIDE_A)
                continue;
            const auto members = source.groups().members(geometry::GroupDomain::Face, name);
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

// Conservative no-op detection. Equal counts or equal volume alone are NOT
// proof of an unchanged surface. Retessellated no-ops may remain "success".
bool identical_surface(const data::PcgGeometry& a, const data::PcgGeometry& b)
{
    if (a.faces() != b.faces() || a.points().size() != b.points().size())
        return false;
    for (size_t i = 0; i < a.points().size(); ++i) {
        const auto& p = a.points()[i];
        const auto& q = b.points()[i];
        if (p.x != q.x || p.y != q.y || p.z != q.z)
            return false;
    }
    return true;
}

bool separated_bounds(const data::PcgGeometry& a, const data::PcgGeometry& b, double epsilon)
{
    if (a.points().empty() || b.points().empty())
        return false;
    auto bounds = [](const data::PcgGeometry& g) {
        auto lo = g.points().front();
        auto hi = lo;
        for (const auto& p : g.points()) {
            lo.x = std::min(lo.x, p.x); lo.y = std::min(lo.y, p.y); lo.z = std::min(lo.z, p.z);
            hi.x = std::max(hi.x, p.x); hi.y = std::max(hi.y, p.y); hi.z = std::max(hi.z, p.z);
        }
        return std::make_pair(lo, hi);
    };
    const auto aa = bounds(a);
    const auto bb = bounds(b);
    const double e = std::max(0.0, epsilon);
    return aa.second.x + e < bb.first.x || bb.second.x + e < aa.first.x ||
           aa.second.y + e < bb.first.y || bb.second.y + e < aa.first.y ||
           aa.second.z + e < bb.first.z || bb.second.z + e < aa.first.z;
}

} // namespace

data::PcgGeometry boolean_geometry(const data::PcgGeometry& a,
                                    const data::PcgGeometry& b,
                                    const BooleanOptions& opts)
{
    auto result = execute_boolean(a, b, opts);
    if (result.error != BooleanErrorType::Ok)
        return {};
    rematerialize_groups(result, a, b);
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
        const std::string on_failure = ctx.node->data.value("onFailure", std::string("error"));

        const nlohmann::json settings = {
            {"operation", op_str}, {"treatAAs", treat_a}, {"treatBAs", treat_b},
            {"useSelf", opts.use_self}, {"detriangulate", detri},
            {"weldEpsilon", opts.weld_epsilon}, {"triangleBudget", opts.triangle_budget},
            {"timeoutMs", opts.timeout_ms}, {"onFailure", on_failure},
        };
        auto record = [&](const char* outcome, const std::string& reason, bool fallback) {
            ctx.outputs.add(kNodeDiagnosticTag, data::PcgDataType::Unknown, {
                {"node_id", ctx.node->id}, {"node_type", type_name()},
                {"outcome", outcome}, {"fallback_used", fallback},
                {"reason", reason}, {"effective_settings", settings},
            });
        };
        auto hard_failure = [&](const std::string& reason) {
            record("failure", reason, false);
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                ("BooleanMesh [" + ctx.node->id + "] failed: " + reason).c_str());
        };
        if (op_str != "union" && op_str != "intersect" && op_str != "shatter" && op_str != "subtract")
            return hard_failure("invalid operation");
        if ((treat_a != "solid" && treat_a != "surface") ||
            (treat_b != "solid" && treat_b != "surface") ||
            (detri != "all" && detri != "none" && detri != "unchanged") ||
            (on_failure != "error" && on_failure != "passthroughA"))
            return hard_failure("invalid Boolean settings");
        if (ctx.is_cancel_requested && ctx.is_cancel_requested())
            return hard_failure("cancelled");

        const data::PcgGeometry a = get_geometry_input(ctx, "a", "BooleanMesh missing mesh A");
        const data::PcgGeometry b = get_geometry_input(ctx, "b", "BooleanMesh missing mesh B");
        // Missing inputs are not empty sets. Typed empty outputs from a previous
        // Boolean are legitimate operands and must remain composable.
        if (a.points().empty() && !ctx.inputs.find_geometry("a") && !ctx.inputs.find_mesh("a"))
            return hard_failure("missing mesh A");
        if (b.points().empty() && !ctx.inputs.find_geometry("b") && !ctx.inputs.find_mesh("b"))
            return hard_failure("missing mesh B");
        const bool empty_a = a.points().empty() && a.faces().empty();
        const bool empty_b = b.points().empty() && b.faces().empty();
        if (!opts.use_self && (empty_a || empty_b) && opts.operation != BooleanOp::Shatter) {
            data::PcgGeometry result;
            if (opts.operation == BooleanOp::Union)
                result = empty_a ? b : a;
            else if (opts.operation == BooleanOp::Subtract && !empty_a)
                result = a;
            preserve_control_attributes(a, result);
            const bool empty = result.faces().empty();
            emit_geometry(ctx, std::move(result));
            record(empty ? "empty" : (empty_b ? "noop" : "success"), "empty-set identity", false);
            return PCG_OK;
        }

        auto failure = [&](const std::string& reason, bool cancelled = false) {
            // Cancellation is an instruction to stop, never a preview fallback.
            if (on_failure == "passthroughA" && !cancelled) {
                emit_geometry(ctx, data::PcgGeometry(a));
                record("degraded", reason, true);
                return PCG_OK;
            }
            return hard_failure(reason);
        };
        BooleanResult raw = execute_boolean(a, b, opts);
        if (raw.error != BooleanErrorType::Ok) {
            const std::string reason = raw.message.empty()
                ? boolean_error_label(raw.error) : raw.message;
            return failure(reason, raw.error == BooleanErrorType::Cancelled);
        }
        if (ctx.is_cancel_requested && ctx.is_cancel_requested())
            return hard_failure("cancelled");

        // A successful kernel can produce the empty set. Never resurrect A.
        if (raw.geometry.faces().empty()) {
            data::PcgGeometry result;
            preserve_control_attributes(a, result);
            emit_geometry(ctx, std::move(result));
            record("empty", "kernel returned the empty set", false);
            return PCG_OK;
        }
        rematerialize_groups(raw, a, b);
        data::PcgGeometry result = finalize_boolean_output(raw, opts.detriangulate);
        if (result.points().empty() || result.faces().empty())
            return failure("finalization discarded a non-empty kernel result");
        preserve_control_attributes(a, result);
        const bool no_op = identical_surface(a, result) ||
            (opts.operation == BooleanOp::Subtract && !opts.use_self &&
             opts.treat_a_as == MeshTreatment::Solid && opts.treat_b_as == MeshTreatment::Solid &&
             separated_bounds(a, b, opts.weld_epsilon));
        emit_geometry(ctx, std::move(result));
        record(no_op ? "noop" : "success",
               no_op ? "unchanged surface or separated solid subtraction" : "kernel and finalization succeeded",
               false);
        return PCG_OK;
    }
};

} // namespace

void register_boolean_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("BooleanMesh", std::make_unique<BooleanMeshElement>());
}

} // namespace pcg::internal::elements
