#include "elements/assembly_elements.hpp"

#include "asset_path.hpp"
#include "elements/assembly_algorithms.hpp"
#include "elements/element_utils.hpp"
#include "elements/lot_subdivision_algorithms.hpp"
#include "mesh_runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace pcg::internal::elements {
namespace {

// Use shared optional_geometry_input from element_utils.hpp (Geometry/Mesh/Spline/Point).

PcgResultCode execute_import_like(PcgContext& ctx, const char* label,
                                  bool preserve_source_rig = false)
{
    if (!ctx.node)
        return fail_ctx(ctx, PCG_ERR_EXECUTION, (std::string(label) + " missing node").c_str());

    ImportMeshOptions options;
    options.scale = ctx.node->data.value("scale", 1.0);
    options.axis_conversion = ctx.node->data.value("axisConversion", "none");
    if (!std::isfinite(options.scale) || options.scale <= 1.0e-9)
        return fail_ctx(ctx, PCG_ERR_EXECUTION,
                        (std::string(label) + " scale must be finite and greater than zero").c_str());
    if (options.axis_conversion != "none" && options.axis_conversion != "zUpToYUp" &&
        options.axis_conversion != "yUpToZUp")
        return fail_ctx(ctx, PCG_ERR_EXECUTION,
                        (std::string(label) + " axisConversion is invalid").c_str());

    data::PcgGeometry geometry;
    if (ctx.meshes) {
        if (const auto* runtime_mesh = ctx.meshes->find(ctx.node->id)) {
            geometry = data::geometry_from_mesh(*runtime_mesh);
            data::GeometryAffineTransform transform;
            const double scale = options.scale;
            if (options.axis_conversion == "zUpToYUp") {
                transform.linear = {scale, 0.0, 0.0,
                                    0.0, 0.0, scale,
                                    0.0, -scale, 0.0};
            } else if (options.axis_conversion == "yUpToZUp") {
                transform.linear = {scale, 0.0, 0.0,
                                    0.0, 0.0, -scale,
                                    0.0, scale, 0.0};
            } else {
                transform.linear = {scale, 0.0, 0.0,
                                    0.0, scale, 0.0,
                                    0.0, 0.0, scale};
            }
            for (auto& point : geometry.points_mut())
                point = data::transform_position(transform, point);
            data::transform_geometry_attributes(geometry, transform);
            if (preserve_source_rig) {
                geometry.metadata().set("pcg_source_rig", {
                    {"schemaVersion", 1},
                    {"route", "preservedGltf"},
                    {"sourceNode", ctx.node->id},
                    {"path", ctx.node->data.value("path", "")},
                    {"projectRoot", ctx.node->data.value("projectRoot", "")},
                    {"scale", options.scale},
                    {"axisConversion", options.axis_conversion},
                    {"continuousShell", true},
                    {"componentSplitting", false},
                });
            }
            emit_geometry(ctx, std::move(geometry));
            return PCG_OK;
        }
    }

    std::string error;
    if (!import_geometry_file(resolve_asset_path(ctx.node->data), options, geometry, error)) {
        if (error.rfind("ImportMesh", 0) == 0)
            error.replace(0, std::strlen("ImportMesh"), label);
        return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
    }
    if (preserve_source_rig) {
        geometry.metadata().set("pcg_source_rig", {
            {"schemaVersion", 1},
            {"route", "preservedGltf"},
            {"sourceNode", ctx.node->id},
            {"path", ctx.node->data.value("path", "")},
            {"projectRoot", ctx.node->data.value("projectRoot", "")},
            {"scale", options.scale},
            {"axisConversion", options.axis_conversion},
            {"continuousShell", true},
            {"componentSplitting", false},
        });
    }
    emit_geometry(ctx, std::move(geometry));
    return PCG_OK;
}

class ImportMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ImportMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        return execute_import_like(ctx, "ImportMesh");
    }
};

class PreserveGltfRigElement final : public IPcgElement {
public:
    const char* type_name() const override { return "PreserveGltfRig"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node) return fail_ctx(ctx, PCG_ERR_EXECUTION, "PreserveGltfRig missing node");
        auto extension = resolve_asset_path(ctx.node->data).extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (extension != ".glb") {
            return fail_ctx(ctx, PCG_ERR_EXECUTION,
                            "PreserveGltfRig path must be a self-contained .glb asset");
        }
        return execute_import_like(ctx, "PreserveGltfRig", true);
    }
};

bool is_finite_vec3(const nlohmann::json& value)
{
    return value.is_array() && value.size() == 3 &&
        std::all_of(value.begin(), value.end(), [](const auto& component) {
            return component.is_number() && std::isfinite(component.template get<double>());
        });
}

bool validate_action_rig(const nlohmann::json& rig, std::string& error)
{
    const int schema_version = rig.is_object() ? rig.value("schemaVersion", 0) : 0;
    if (schema_version != 1 && schema_version != 2) {
        error = "ActionRig rigJson must be a schemaVersion 1 or 2 object";
        return false;
    }

    std::unordered_set<std::string> bone_ids;
    std::unordered_map<std::string, std::string> parents;
    if (schema_version == 1) {
        if (!rig.contains("bones") || !rig["bones"].is_array() || rig["bones"].empty()) {
            error = "ActionRig schemaVersion 1 must contain at least one bone";
            return false;
        }
        for (const auto& bone : rig["bones"]) {
            if (!bone.is_object()) {
                error = "ActionRig bones must be objects";
                return false;
            }
            const auto id = bone.value("id", "");
            if (id.empty() || !bone_ids.emplace(id).second) {
                error = id.empty() ? "ActionRig bone id cannot be empty"
                                   : "ActionRig bone ids must be unique";
                return false;
            }
            if (!is_finite_vec3(bone.value("head", nlohmann::json{})) ||
                !is_finite_vec3(bone.value("tail", nlohmann::json{}))) {
                error = "ActionRig bone head and tail must be finite vec3 arrays";
                return false;
            }
            const double radius = bone.value("radius", 0.1);
            if (!std::isfinite(radius) || radius <= 0.0) {
                error = "ActionRig bone radius must be finite and greater than zero";
                return false;
            }
            if (bone.contains("parent") && !bone["parent"].is_null()) {
                if (!bone["parent"].is_string()) {
                    error = "ActionRig bone parent must be a bone id or null";
                    return false;
                }
                parents[id] = bone["parent"].get<std::string>();
            }
        }
    } else {
        if (!rig.contains("componentTree") || !rig["componentTree"].is_array() ||
            rig["componentTree"].empty()) {
            error = "ActionRig schemaVersion 2 must contain a non-empty componentTree";
            return false;
        }
        std::unordered_set<std::string> component_ids;
        std::unordered_map<std::string, std::string> component_parents;
        size_t root_count = 0;
        for (const auto& component : rig["componentTree"]) {
            if (!component.is_object()) {
                error = "ActionRig componentTree entries must be objects";
                return false;
            }
            const auto id = component.value("id", "");
            if (id.empty() || !component_ids.emplace(id).second ||
                !is_finite_vec3(component.value("pivot", nlohmann::json{}))) {
                error = "ActionRig componentTree requires unique ids and finite pivots";
                return false;
            }
            const double radius = component.value("radius", 0.1);
            if (!std::isfinite(radius) || radius <= 0.0 ||
                (component.contains("tip") && !is_finite_vec3(component["tip"]))) {
                error = "ActionRig componentTree radius/tip is invalid";
                return false;
            }
            const auto skin = component.value("skin", "smooth");
            if (skin != "smooth" && skin != "rigid") {
                error = "ActionRig componentTree skin must be smooth or rigid";
                return false;
            }
            if (component.contains("parent") && !component["parent"].is_null()) {
                if (!component["parent"].is_string()) {
                    error = "ActionRig componentTree parent must be a component id or null";
                    return false;
                }
                component_parents[id] = component["parent"].get<std::string>();
            } else {
                ++root_count;
                bone_ids.emplace(id);
            }
            if (component.value("joint", false)) bone_ids.emplace(id);
        }
        if (root_count != 1) {
            error = "ActionRig componentTree must contain exactly one root";
            return false;
        }
        for (const auto& [id, parent] : component_parents) {
            if (id == parent || component_ids.find(parent) == component_ids.end()) {
                error = "ActionRig componentTree parent must reference another component";
                return false;
            }
        }
        std::unordered_map<std::string, int> component_visit_state;
        std::function<bool(const std::string&)> visit_component = [&](const std::string& id) {
            const int state = component_visit_state[id];
            if (state == 1) return false;
            if (state == 2) return true;
            component_visit_state[id] = 1;
            const auto parent = component_parents.find(id);
            if (parent != component_parents.end() && !visit_component(parent->second)) return false;
            component_visit_state[id] = 2;
            return true;
        };
        for (const auto& id : component_ids) {
            if (!visit_component(id)) {
                error = "ActionRig componentTree must not contain cycles";
                return false;
            }
        }
    }
    for (const auto& [id, parent] : parents) {
        if (id == parent || bone_ids.find(parent) == bone_ids.end()) {
            error = "ActionRig bone parent must reference a different existing bone";
            return false;
        }
    }

    std::unordered_map<std::string, int> visit_state;
    std::function<bool(const std::string&)> visit = [&](const std::string& id) {
        const int state = visit_state[id];
        if (state == 1) return false;
        if (state == 2) return true;
        visit_state[id] = 1;
        const auto parent = parents.find(id);
        if (parent != parents.end() && !visit(parent->second)) return false;
        visit_state[id] = 2;
        return true;
    };
    for (const auto& id : bone_ids) {
        if (!visit(id)) {
            error = "ActionRig bone hierarchy must not contain cycles";
            return false;
        }
    }

    if (rig.contains("components")) {
        if (!rig["components"].is_array()) {
            error = "ActionRig components must be an array";
            return false;
        }
        std::unordered_set<std::string> component_ids;
        for (const auto& component : rig["components"]) {
            if (!component.is_object()) {
                error = "ActionRig components must be objects";
                return false;
            }
            const auto id = component.value("id", "");
            const auto bone = component.value("bone", "");
            if (id.empty() || !component_ids.emplace(id).second ||
                bone_ids.find(bone) == bone_ids.end()) {
                error = "ActionRig components require unique ids and existing bone ids";
                return false;
            }
            if (component.contains("pivot") && !is_finite_vec3(component["pivot"])) {
                error = "ActionRig component pivot must be a finite vec3 array";
                return false;
            }
        }
    }

    if (rig.contains("clips")) {
        if (!rig["clips"].is_array()) {
            error = "ActionRig clips must be an array";
            return false;
        }
        for (const auto& clip : rig["clips"]) {
            if (!clip.is_object() || clip.value("name", "").empty() ||
                !clip.contains("duration") || !clip["duration"].is_number()) {
                error = "ActionRig clips require a name and numeric duration";
                return false;
            }
            const double duration = clip["duration"].get<double>();
            if (!std::isfinite(duration) || duration <= 0.0 ||
                !clip.contains("tracks") || !clip["tracks"].is_array()) {
                error = "ActionRig clip duration must be positive and tracks must be an array";
                return false;
            }
            for (const auto& track : clip["tracks"]) {
                if (!track.is_object()) {
                    error = "ActionRig clip tracks must be objects";
                    return false;
                }
                const auto bone = track.value("bone", "");
                const auto property = track.value("property", "");
                const int tuple_size = property == "quaternion" ? 4 : 3;
                if (bone_ids.find(bone) == bone_ids.end() ||
                    (property != "quaternion" && property != "position" &&
                     property != "scale") ||
                    !track.contains("times") || !track["times"].is_array() ||
                    track["times"].empty() || !track.contains("values") ||
                    !track["values"].is_array() ||
                    track["values"].size() != track["times"].size() * tuple_size) {
                    error = "ActionRig clip tracks require a valid bone, property, times, and packed values";
                    return false;
                }
            }
        }
    }
    return true;
}

class ActionRigElement final : public IPcgElement {
public:
    const char* type_name() const override { return "ActionRig"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ActionRig missing node");
        auto geometry = get_geometry_input(ctx, "in", "ActionRig missing geometry input");
        if (geometry.points().empty()) return PCG_ERR_EXECUTION;

        const auto& data = ctx.node->data;
        const auto rig_text = data.value("rigJson", "");
        nlohmann::json rig;
        try {
            rig = nlohmann::json::parse(rig_text);
        } catch (const nlohmann::json::exception&) {
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ActionRig rigJson is not valid JSON");
        }
        std::string error;
        if (!validate_action_rig(rig, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());

        const auto skin_mode = data.value("skinMode", "distance");
        const auto component_mode = data.value("componentMode", "dominantBone");
        const int max_influences = data.value("maxInfluences", 4);
        const double falloff = data.value("falloff", 4.0);
        const int geodesic_resolution = data.value("geodesicResolution", 40);
        const double playback_speed = data.value("playbackSpeed", 1.0);
        if (skin_mode != "geodesic" && skin_mode != "distance" && skin_mode != "rigid")
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ActionRig skinMode is invalid");
        if (component_mode != "none" && component_mode != "dominantBone" &&
            component_mode != "semanticRegion")
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ActionRig componentMode is invalid");
        if (max_influences < 1 || max_influences > 4)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ActionRig maxInfluences must be between 1 and 4");
        if (!std::isfinite(falloff) || falloff <= 0.0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ActionRig falloff must be finite and positive");
        if (geodesic_resolution < 16 || geodesic_resolution > 96)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ActionRig geodesicResolution must be between 16 and 96");
        if (!std::isfinite(playback_speed))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "ActionRig playbackSpeed must be finite");

        geometry.metadata().set("pcg_action_runtime", {
            {"schemaVersion", 1},
            {"sourceNode", ctx.node->id},
            {"rig", std::move(rig)},
            {"skinMode", skin_mode},
            {"maxInfluences", max_influences},
            {"falloff", falloff},
            {"geodesicResolution", geodesic_resolution},
            {"componentMode", component_mode},
            {"splitComponents", data.value("splitComponents", true)},
            {"autoplay", data.value("autoplay", "")},
            {"playbackSpeed", playback_speed},
        });
        emit_geometry(ctx, std::move(geometry));
        return PCG_OK;
    }
};

class Meshy3DGeneratorElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Meshy3DGenerator"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        // Unity PcgMeshyResolver downloads/caches GLB and injects absolute path before cook.
        return execute_import_like(ctx, "Meshy3DGenerator");
    }
};

class Tripo3DGeneratorElement final : public IPcgElement {
public:
    const char* type_name() const override { return "Tripo3DGenerator"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        return execute_import_like(ctx, "Tripo3DGenerator");
    }
};

class MeshyTextTo3DElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MeshyTextTo3D"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        // Unity PcgMeshyTextTo3DResolver downloads/caches GLB and injects absolute path before cook.
        return execute_import_like(ctx, "MeshyTextTo3D");
    }
};

class MeshyMeshOpsElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MeshyMeshOps"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        return execute_import_like(ctx, "MeshyMeshOps");
    }
};

class MeshyRetextureElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MeshyRetexture"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        return execute_import_like(ctx, "MeshyRetexture");
    }
};

class MatchSizeElement final : public IPcgElement {
public:
    const char* type_name() const override { return "MatchSize"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "MatchSize missing node");
        const auto source = get_geometry_input(ctx, "source", "MatchSize missing source input");
        if (source.points().empty())
            return PCG_ERR_EXECUTION;

        data::PcgGeometry reference_storage;
        const auto* reference = optional_geometry_input(ctx, "reference", reference_storage);
        const auto& data = ctx.node->data;
        MatchSizeOptions options;
        options.justify_with = data.value("justifyWith", "inputIfWired");
        options.group = data.value("group", "");
        options.group_type = data.value("groupType", "guess");
        options.use_groups_for_bounds = data.value("useGroupsForBounds", false);
        options.source_group = data.value("sourceGroup", "");
        options.source_group_type = data.value("sourceGroupType", "guess");
        options.target_group = data.value("targetGroup", "");
        options.target_group_type = data.value("targetGroupType", "guess");
        options.translate = data.value("translate", true);
        options.scale_to_fit = data.value("scaleToFit", true);
        options.uniform_scale = data.value("uniformScale", true);
        if (data.contains("scaleAxis")) {
            options.scale_axis = data.value("scaleAxis", "bestFit");
        } else {
            const auto legacy_mode = data.value("uniformScaleMode", "fit");
            if (legacy_mode == "fill")
                options.scale_axis = "fill";
            else
                options.scale_axis = "bestFit";
        }
        options.scale_x = data.value("scaleX", true);
        options.scale_y = data.value("scaleY", true);
        options.scale_z = data.value("scaleZ", true);
        options.justify_x = data.contains("justifyX")
            ? data.value("justifyX", "center")
            : data.value("sourceJustifyX", "center");
        options.justify_y = data.contains("justifyY")
            ? data.value("justifyY", "center")
            : data.value("sourceJustifyY", "center");
        options.justify_z = data.contains("justifyZ")
            ? data.value("justifyZ", "center")
            : data.value("sourceJustifyZ", "center");
        const bool legacy_justify =
            !data.contains("justifyX") && data.contains("sourceJustifyX");
        options.target_justify_x =
            data.value("targetJustifyX", legacy_justify ? "center" : "same");
        options.target_justify_y =
            data.value("targetJustifyY", legacy_justify ? "center" : "same");
        options.target_justify_z =
            data.value("targetJustifyZ", legacy_justify ? "center" : "same");
        options.offset = read_vector_param(data, "offset", {0.0, 0.0, 0.0});
        if (data.contains("targetPosition") || data.contains("targetPositionX")) {
            options.target_position = read_vector_param(data, "targetPosition", {0.0, 0.0, 0.0});
        } else {
            options.target_position = read_vector_param(data, "targetCenter", {0.0, 0.0, 0.0});
        }
        options.target_size = read_vector_param(data, "targetSize", {1.0, 1.0, 1.0});
        options.restore_transform = data.value("restoreTransform", false);
        options.restore_attribute = data.value("restoreAttribute", "xform");
        options.stash_transform = data.value("stashTransform", true);
        options.stash_attribute = data.value("stashAttribute", "xform");

        data::PcgGeometry output;
        std::string error;
        if (!match_size_geometry(source, reference, options, output, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
        emit_geometry(ctx, std::move(output));
        return PCG_OK;
    }
};

class LotSubdivisionElement final : public IPcgElement {
public:
    const char* type_name() const override { return "LotSubdivision"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision missing node");

        const auto input =
            get_geometry_input(ctx, "in", "LotSubdivision missing mesh input");
        if (input.faces().empty())
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision input has no faces");

        LotSubdivisionOptions options;
        options.min_size = ctx.node->data.value("minSize", 1.0);
        options.iterations = ctx.node->data.value("iterations", 3);
        options.irregularity = ctx.node->data.value("irregularity", 0.5);
        // Must use read_seed_param_number — data.value("seed", 0) truncates 2.3 → 2.
        options.seed = read_seed_param_number(ctx.node->data, "seed", 0.0);
        options.graph_seed = ctx.graph_seed;
        options.alignment = ctx.node->data.value("alignment", "longestEdge");

        if (!std::isfinite(options.min_size) || options.min_size < 0.0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision minSize must be >= 0");
        if (options.iterations < 0)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision iterations must be >= 0");
        options.irregularity = std::clamp(options.irregularity, 0.0, 1.0);
        if (options.alignment != "longestEdge" && options.alignment != "boundingBox")
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "LotSubdivision alignment is invalid");

        emit_geometry(ctx, lot_subdivide_geometry(input, options));
        return PCG_OK;
    }
};

class BendMeshElement final : public IPcgElement {
public:
    const char* type_name() const override { return "BendMesh"; }

    PcgResultCode execute(PcgContext& ctx) const override
    {
        if (!ctx.node)
            return fail_ctx(ctx, PCG_ERR_EXECUTION, "BendMesh missing node");
        const auto source = get_geometry_input(ctx, "source", "BendMesh missing source input");
        if (source.points().empty())
            return PCG_ERR_EXECUTION;

        data::PcgGeometry rest_storage;
        const auto* rest = optional_geometry_input(ctx, "rest", rest_storage);
        BendMeshOptions options;
        options.capture_origin = read_vector_param(ctx.node->data, "captureOrigin", {0.0, 0.0, 0.0});
        options.capture_direction = read_vector_param(ctx.node->data, "captureDirection", {0.0, 1.0, 0.0});
        options.up_direction = read_vector_param(ctx.node->data, "upDirection", {1.0, 0.0, 0.0});
        options.capture_length = ctx.node->data.value("captureLength", 1.0);
        options.angle_degrees = ctx.node->data.value("angle", 0.0);
        options.mask_attribute = ctx.node->data.value("maskAttribute", "bendmask");

        data::PcgGeometry output;
        std::string error;
        if (!bend_geometry(source, rest, options, output, error))
            return fail_ctx(ctx, PCG_ERR_EXECUTION, error.c_str());
        emit_geometry(ctx, std::move(output));
        return PCG_OK;
    }
};

} // namespace

void register_assembly_elements(
    std::unordered_map<std::string, std::unique_ptr<IPcgElement>>& map)
{
    map.emplace("ImportMesh", std::make_unique<ImportMeshElement>());
    map.emplace("PreserveGltfRig", std::make_unique<PreserveGltfRigElement>());
    map.emplace("ActionRig", std::make_unique<ActionRigElement>());
    map.emplace("Meshy3DGenerator", std::make_unique<Meshy3DGeneratorElement>());
    map.emplace("Tripo3DGenerator", std::make_unique<Tripo3DGeneratorElement>());
    map.emplace("MeshyTextTo3D", std::make_unique<MeshyTextTo3DElement>());
    map.emplace("MeshyMeshOps", std::make_unique<MeshyMeshOpsElement>());
    map.emplace("MeshyRetexture", std::make_unique<MeshyRetextureElement>());
    map.emplace("MatchSize", std::make_unique<MatchSizeElement>());
    map.emplace("BendMesh", std::make_unique<BendMeshElement>());
    map.emplace("LotSubdivision", std::make_unique<LotSubdivisionElement>());
}

} // namespace pcg::internal::elements
