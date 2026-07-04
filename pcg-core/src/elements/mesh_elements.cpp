#include "elements/mesh_elements.hpp"

#include "elements/element_utils.hpp"
#include "elements/mesh_algorithms.hpp"

namespace pcg::internal::elements {
namespace {

class CreateBoxMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "CreateBoxMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateBoxMesh missing node");

        const double width = ctx.node->data.value("width", 2.0);
        const double height = ctx.node->data.value("height", 2.0);
        const double depth = ctx.node->data.value("depth", 2.0);
        if (width < 0.0 || height < 0.0 || depth < 0.0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "CreateBoxMesh dimensions must be >= 0");

        emit_mesh(ctx, create_box_mesh(width, height, depth));
        return PCG_OK;
    }
};

class SubdivideMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "SubdivideMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SubdivideMesh missing node");

        const nlohmann::json* input = require_input_json(ctx, "in", "SubdivideMesh missing mesh input");
        const data::PcgMeshData mesh = parse_mesh_input(*input);
        if (mesh.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "SubdivideMesh missing mesh input");

        const int levels = ctx.node->data.value("levels", 1);
        emit_mesh(ctx, subdivide_mesh(mesh, levels));
        return PCG_OK;
    }
};

class BevelMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "BevelMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BevelMesh missing node");

        const nlohmann::json* input = require_input_json(ctx, "in", "BevelMesh missing mesh input");
        const data::PcgMeshData mesh = parse_mesh_input(*input);
        if (mesh.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BevelMesh missing mesh input");

        const double amount = ctx.node->data.value("amount", 0.1);
        const int segments = ctx.node->data.value("segments", 2);
        const std::string method_str = ctx.node->data.value("method", std::string("edge"));
        const BevelMethod method =
            method_str == "vertexPush" ? BevelMethod::VertexPush : BevelMethod::Edge;
        const std::string offset_type_str = ctx.node->data.value("offsetType", std::string("offset"));
        const BevelOffsetType offset_type =
            offset_type_str == "width" ? BevelOffsetType::Width : BevelOffsetType::Offset;
        const bool clamp_overlap = ctx.node->data.value("clampOverlap", true);
        emit_mesh(ctx, bevel_mesh(mesh, amount, segments, method, offset_type, clamp_overlap));
        return PCG_OK;
    }
};

} // namespace

void register_mesh_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("CreateBoxMesh", std::make_unique<CreateBoxMeshElement>());
    map.emplace("SubdivideMesh", std::make_unique<SubdivideMeshElement>());
    map.emplace("BevelMesh", std::make_unique<BevelMeshElement>());
}

} // namespace pcg::internal::elements
