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

        data::PcgMeshData mesh = create_box_mesh(width, height, depth);

        // Merge optional input mesh
        if (const nlohmann::json* input = ctx.inputs.find_json("in"))
        {
            const data::PcgMeshData input_mesh = parse_mesh_input(*input);
            const int vertex_offset = static_cast<int>(mesh.vertices().size());
            for (const auto& v : input_mesh.vertices())
                mesh.vertices_mut().push_back(v);
            for (int idx : input_mesh.triangles())
                mesh.triangles_mut().push_back(idx + vertex_offset);
        }

        emit_mesh(ctx, std::move(mesh));
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
        const double angle_limit = ctx.node->data.value("angleLimit", 30.0);
        const float profile = static_cast<float>(ctx.node->data.value("profile", 0.5));
        const auto parse_miter = [](const std::string& s) {
            if (s == "patch") return BevelMiter::Patch;
            if (s == "arc") return BevelMiter::Arc;
            return BevelMiter::Sharp;
        };
        const BevelMiter miter_outer =
            parse_miter(ctx.node->data.value("miterOuter", std::string("sharp")));
        const BevelMiter miter_inner =
            parse_miter(ctx.node->data.value("miterInner", std::string("sharp")));
        const std::string vmesh_str = ctx.node->data.value("vmeshMethod", std::string("adj"));
        const BevelVMeshMethod vmesh_method =
            vmesh_str == "cutoff" ? BevelVMeshMethod::Cutoff : BevelVMeshMethod::Adj;
        emit_mesh(ctx, bevel_mesh(mesh, amount, segments, method, offset_type, clamp_overlap,
                                  angle_limit, profile, miter_outer, miter_inner, vmesh_method));
        return PCG_OK;
    }
};

class MeshNoiseDeformElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MeshNoiseDeform"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MeshNoiseDeform missing node");

        const nlohmann::json* input =
            require_input_json(ctx, "in", "MeshNoiseDeform missing mesh input");
        const data::PcgMeshData mesh = parse_mesh_input(*input);
        if (mesh.vertices().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MeshNoiseDeform missing mesh input");

        const double intensity = ctx.node->data.value("intensity", 0.02);
        const double noise_scale = ctx.node->data.value("scale", 2.0);
        const std::string noise_type_str = ctx.node->data.value("noiseType", std::string("perlin"));
        const NoiseDeformType noise_type =
            noise_type_str == "perlin" ? NoiseDeformType::Perlin : NoiseDeformType::Perlin;

        emit_mesh(ctx, noise_deform_mesh(mesh, intensity, noise_scale, noise_type, ctx.graph_seed));
        return PCG_OK;
    }
};

} // namespace

void register_mesh_elements(std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("CreateBoxMesh", std::make_unique<CreateBoxMeshElement>());
    map.emplace("SubdivideMesh", std::make_unique<SubdivideMeshElement>());
    map.emplace("BevelMesh", std::make_unique<BevelMeshElement>());
    map.emplace("MeshNoiseDeform", std::make_unique<MeshNoiseDeformElement>());
}

} // namespace pcg::internal::elements
