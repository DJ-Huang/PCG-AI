#include "mcp_service.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "server_auth.hpp"
#include "cook_service.hpp"
#include "kb_service.hpp"
#include "session_service.hpp"
#include "surface_reconstruction_service.hpp"

namespace pcg_server {
namespace {

using json = nlohmann::json;
constexpr const char* kProtocolVersion = "2025-06-18";

uint32_t ReadU32(const std::string& bytes, size_t offset) {
    if (offset + 4 > bytes.size()) return 0;
    const auto* p = reinterpret_cast<const uint8_t*>(bytes.data() + offset);
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

double ReadF64(const std::string& bytes, size_t offset) {
    uint64_t bits = 0;
    for (int i = 0; i < 8 && offset + static_cast<size_t>(i) < bytes.size(); ++i) {
        bits |= static_cast<uint64_t>(static_cast<uint8_t>(bytes[offset + i])) << (8 * i);
    }
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::string EncodeBase64(const std::vector<uint8_t>& input) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        const uint32_t a = input[i];
        const uint32_t b = i + 1 < input.size() ? input[i + 1] : 0;
        const uint32_t c = i + 2 < input.size() ? input[i + 2] : 0;
        const uint32_t value = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(value >> 18) & 63]);
        output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(i + 1 < input.size() ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(i + 2 < input.size() ? alphabet[value & 63] : '=');
    }
    return output;
}

json ToolResult(const json& value, bool is_error = false) {
    return {
        {"content", json::array({{{"type", "text"}, {"text", value.dump(2)}}})},
        {"structuredContent", value},
        {"isError", is_error},
    };
}

void RedactLargeGraphValues(json& value, const std::string& path, json& redacted) {
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            RedactLargeGraphValues(it.value(), path + "/" + it.key(), redacted);
        }
        return;
    }
    if (value.is_array()) {
        for (size_t index = 0; index < value.size(); ++index)
            RedactLargeGraphValues(value[index], path + "/" + std::to_string(index), redacted);
        return;
    }
    if (!value.is_string()) return;
    const std::string& text = value.get_ref<const std::string&>();
    if (text.size() <= 4096) return;
    const bool point_cloud = path.size() >= 11 &&
                             path.compare(path.size() - 11, 11, "/pointCloud") == 0;
    const bool data_uri = text.rfind("data:", 0) == 0;
    if (!point_cloud && !data_uri) return;
    redacted.push_back({{"path", path}, {"encodedCharacters", text.size()},
                        {"kind", point_cloud ? "oriented-point-cloud" : "data-uri"}});
    if (point_cloud) {
        value = "<redacted topology-free oriented point payload>";
        return;
    }
    const size_t comma = text.find(',');
    const size_t prefix_length = comma == std::string::npos
        ? std::min<size_t>(text.size(), 96)
        : std::min<size_t>(comma + 1, 96);
    value = text.substr(0, prefix_length) + "<redacted>";
}

json RedactGraphToolResult(json result) {
    json redacted = json::array();
    RedactLargeGraphValues(result, "", redacted);
    if (!redacted.empty()) {
        result["largePayloadsRedacted"] = true;
        result["redactedFields"] = std::move(redacted);
        result["redactionHint"] =
            "Use pcg_patch_node/pcg_apply_graph_ops so omitted payloads remain intact; "
            "use pcg_bake_oriented_sdf to refresh them.";
    }
    return result;
}

int CommandTimeout(const json& arguments) {
    return std::max(1000, std::min(30000, arguments.value("timeoutMs", 10000)));
}

struct SemanticBounds {
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double min_z = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    double max_z = -std::numeric_limits<double>::infinity();
    json sources = json::array();
    json anchors = json::object();
    bool valid = false;
};

bool ReadVec3(const json& value, double& x, double& y, double& z) {
    if (!value.is_array() || value.size() != 3) return false;
    if (!value[0].is_number() || !value[1].is_number() || !value[2].is_number()) return false;
    x = value[0].get<double>(); y = value[1].get<double>(); z = value[2].get<double>();
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

void UnionSemanticBounds(SemanticBounds& out, const json& semantic, const json& source) {
    if (!semantic.is_object() || !semantic.contains("bounds") || !semantic["bounds"].is_object()) return;
    const json& bounds = semantic["bounds"];
    double cx, cy, cz, sx, sy, sz;
    if (!ReadVec3(bounds.value("center", json::array()), cx, cy, cz) ||
        !ReadVec3(bounds.value("size", json::array()), sx, sy, sz) || sx <= 0 || sy <= 0 || sz <= 0) return;
    out.min_x = std::min(out.min_x, cx - sx / 2); out.max_x = std::max(out.max_x, cx + sx / 2);
    out.min_y = std::min(out.min_y, cy - sy / 2); out.max_y = std::max(out.max_y, cy + sy / 2);
    out.min_z = std::min(out.min_z, cz - sz / 2); out.max_z = std::max(out.max_z, cz + sz / 2);
    out.sources.push_back(source); out.valid = true;
    if (semantic.contains("anchors") && semantic["anchors"].is_object()) {
        for (auto it = semantic["anchors"].begin(); it != semantic["anchors"].end(); ++it) {
            double x, y, z;
            if (ReadVec3(it.value(), x, y, z)) out.anchors[it.key()] = json::array({x, y, z});
        }
    }
}

std::unordered_map<std::string, SemanticBounds> CollectSemanticBounds(const json& graph) {
    std::unordered_map<std::string, SemanticBounds> result;
    const auto collect_nodes = [&](const json& nodes, const std::string& scope) {
        if (!nodes.is_array()) return;
        for (const auto& node : nodes) {
            if (!node.is_object()) continue;
            const json semantic = node.value("data", json::object()).value("__semantic", json::object());
            const std::string id = semantic.value("componentId", "");
            if (!id.empty()) UnionSemanticBounds(result[id], semantic, {{"kind", "node"}, {"scope", scope}, {"nodeId", node.value("id", "")}});
        }
    };
    collect_nodes(graph.value("nodes", json::array()), "root");
    if (graph.contains("subgraphs") && graph["subgraphs"].is_array()) {
        for (const auto& subgraph : graph["subgraphs"]) {
            if (!subgraph.is_object()) continue;
            const std::string scope = "subgraph:" + subgraph.value("id", "");
            collect_nodes(subgraph.value("nodes", json::array()), scope);
            const json semantic = subgraph.value("semantic", json::object());
            const std::string id = semantic.value("componentId", "");
            if (!id.empty()) UnionSemanticBounds(result[id], semantic, {{"kind", "subgraph"}, {"scope", scope}, {"subgraphId", subgraph.value("id", "")}});
        }
    }
    return result;
}

json BoundsJson(const SemanticBounds& bounds) {
    const double cx = (bounds.min_x + bounds.max_x) / 2, cy = (bounds.min_y + bounds.max_y) / 2, cz = (bounds.min_z + bounds.max_z) / 2;
    const double sx = bounds.max_x - bounds.min_x, sy = bounds.max_y - bounds.min_y, sz = bounds.max_z - bounds.min_z;
    return {{"coordinateSpace", "unity"}, {"center", json::array({cx, cy, cz})}, {"size", json::array({sx, sy, sz})},
            {"min", json::array({bounds.min_x, bounds.min_y, bounds.min_z})}, {"max", json::array({bounds.max_x, bounds.max_y, bounds.max_z})},
            {"sources", bounds.sources}, {"anchors", bounds.anchors}};
}

bool UnionRequestedBounds(const std::unordered_map<std::string, SemanticBounds>& all, const json& ids, SemanticBounds& out, json& missing) {
    if (!ids.is_array() || ids.empty()) return false;
    for (const auto& id_value : ids) {
        if (!id_value.is_string()) { missing.push_back(id_value); continue; }
        const std::string id = id_value.get<std::string>();
        const auto found = all.find(id);
        if (found == all.end() || !found->second.valid) { missing.push_back(id); continue; }
        const auto& b = found->second;
        out.min_x = std::min(out.min_x, b.min_x); out.min_y = std::min(out.min_y, b.min_y); out.min_z = std::min(out.min_z, b.min_z);
        out.max_x = std::max(out.max_x, b.max_x); out.max_y = std::max(out.max_y, b.max_y); out.max_z = std::max(out.max_z, b.max_z); out.valid = true;
    }
    return out.valid && missing.empty();
}

json SolveSemanticCamera(const SemanticBounds& b, const json& spec) {
    const std::string view = spec.value("view", "three-quarter");
    const double aspect = std::max(0.1, spec.value("aspect", 9.0 / 16.0));
    const double margin = std::max(0.0, std::min(1.0, spec.value("margin", 0.15)));
    const double cx = (b.min_x + b.max_x) / 2, cy = (b.min_y + b.max_y) / 2, cz = (b.min_z + b.max_z) / 2;
    const double sx = b.max_x - b.min_x, sy = b.max_y - b.min_y, sz = b.max_z - b.min_z;
    const double pad = 1.0 + margin * 2.0;
    json camera = {{"target", json::array({cx, cy, -cz})}, {"up", json::array({0.0, 1.0, 0.0})}, {"near", 0.01}, {"far", 5000.0}, {"focusOnTarget", true}};
    if (view == "top" || view == "front" || view == "side") {
        double height = 1.0, distance = std::max({sx, sy, sz, 1.0}) * 3.0;
        if (view == "top") { height = std::max(sz, sx / aspect) * pad; camera["position"] = json::array({cx, cy + distance, -cz}); camera["up"] = json::array({0.0, 0.0, -1.0}); }
        else if (view == "side") { height = std::max(sy, sz / aspect) * pad; camera["position"] = json::array({cx + distance, cy, -cz}); }
        else { height = std::max(sy, sx / aspect) * pad; camera["position"] = json::array({cx, cy, -cz + distance}); }
        camera["projection"] = "orthographic"; camera["orthographicFrustumHeight"] = std::max(0.01, height); return camera;
    }
    const double fov = std::max(10.0, std::min(100.0, spec.value("fov", 48.0)));
    const double radius = std::sqrt(sx * sx + sy * sy + sz * sz) * 0.5 * pad;
    const double distance = std::max(1.0, radius / std::tan(fov * 3.14159265358979323846 / 360.0));
    camera["projection"] = "perspective"; camera["fov"] = fov;
    camera["position"] = json::array({cx + distance * 0.72, cy + distance * 0.38, -cz + distance * 0.58});
    return camera;
}

json WaitForAppliedCommand(const json& queued, int timeout_ms) {
    if (!queued.value("ok", false)) return ToolResult(queued, true);
    const json command = queued.value("command", queued.value("patch", json::object()));
    const uint64_t command_id = command.value("id", 0ull);
    if (command_id == 0) {
        return ToolResult({{"ok", false}, {"error", "invalid_command_receipt"}}, true);
    }
    json apply_result;
    if (!WaitForGraphCommandResult(
            command_id,
            std::chrono::milliseconds(timeout_ms),
            apply_result)) {
        const bool cancelled = CancelGraphCommand(command_id);
        return ToolResult({
            {"ok", false}, {"accepted", true}, {"applied", false},
            {"cancelled", cancelled}, {"error", "apply_timeout"}, {"commandId", command_id},
            {"hint", cancelled
                ? "The queued command was cancelled before the Web editor applied it."
                : "The Web editor may have fetched the command; refresh context before retrying."},
        }, true);
    }
    const bool ok = apply_result.value("ok", false);
    return ToolResult({
        {"ok", ok}, {"accepted", true}, {"applied", ok},
        {"commandId", command_id}, {"applyResult", std::move(apply_result)},
    }, !ok);
}

bool IsSafeRelativePcgPath(const std::string& value) {
    const std::filesystem::path path(value);
    if (value.empty() || path.is_absolute() || path.extension() != ".pcg") return false;
    for (const auto& component : path) {
        if (component == "..") return false;
    }
    return true;
}

json ErrorResponse(const json& id, int code, const std::string& message, const json& data = nullptr) {
    json error = {{"code", code}, {"message", message}};
    if (!data.is_null()) error["data"] = data;
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}};
}

json SuccessResponse(const json& id, const json& result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

json BuildToolDefinitions() {
    json tools = json::array({
        {
            {"name", "pcg_get_editor_context"},
            {"description", "Read the live Web editor path, selection, preview target, graph hash, and bridge status."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_get_node"},
            {"description", "Read a node from the graph currently open in the Web editor. Defaults to the selected node."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"nodeId", {{"type", "string"}, {"description", "Node id; omit to use the current selection."}}}}},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_list_nodes"},
            {"description", "List nodes in the graph or subgraph currently open in the Web editor."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_get_graph"},
            {"description", "Read the complete live Graph JSON document, including edges, parameters, and Subgraphs."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_get_node_types"},
            {"description", "Read manifest-backed node definitions, pins, properties, defaults, and ranges from the live Web editor. Filter by exact nodeType or category when possible."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"nodeType", {{"type", "string"}}},
                    {"category", {{"type", "string"}}},
                }},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_bake_oriented_sdf"},
            {"description", "Bake a workspace GLB/glTF/OBJ/FBX into a topology-free quantised oriented point cloud and atomically add an OrientedSdfSurface node to the live graph. The graph stores measured positions/normals/optional colours, never source faces or indices; pcg-core reconstructs a new sparse MLS-SDF + Surface Nets mesh at cook time."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"sourcePath", {{"type", "string"}, {"description", "Workspace-relative or absolute-in-workspace source mesh path."}}},
                    {"nodeId", {{"type", "string"}, {"default", "oriented_sdf_surface"}}},
                    {"title", {{"type", "string"}, {"default", "High-Fidelity SDF Surface"}}},
                    {"stageMaterial", {{"type", "boolean"}, {"default", true}, {"description", "Also add an unconnected Material node carrying embedded GLB PBR textures as data URIs."}}},
                    {"materialNodeId", {{"type", "string"}, {"default", "reconstructed_material"}}},
                    {"materialTitle", {{"type", "string"}, {"default", "Reconstructed Source Material"}}},
                    {"materialName", {{"type", "string"}, {"default", "ReconstructedSourceMaterial"}}},
                    {"position", {{"type", "object"}, {"properties", {
                        {"x", {{"type", "number"}}}, {"y", {{"type", "number"}}},
                    }}, {"additionalProperties", false}}},
                    {"cellSize", {{"type", "number"}, {"minimum", 0.0005}}},
                    {"sampleSpacing", {{"type", "number"}, {"minimum", 0.0001}, {"description", "Topology-free triangle-interior sample spacing. Defaults to 0.75 × cellSize; smaller values preserve sparse source triangles and UVs more faithfully."}}},
                    {"supportRadiusCells", {{"type", "number"}, {"minimum", 1.25}, {"maximum", 6.0}, {"default", 2.5}}},
                    {"isoOffset", {{"type", "number"}, {"default", 0.0}}},
                    {"maxActiveCells", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 5000000}, {"default", 3000000}}},
                    {"transferColors", {{"type", "boolean"}, {"default", true}}},
                    {"transferUvs", {{"type", "boolean"}, {"default", true}}},
                    {"flipUvV", {{"type", "boolean"}, {"description", "Flip source V during reconstruction; defaults on for GLB/glTF textures loaded through the Web material path."}}},
                    {"ifGraphHash", {{"type", "string"}, {"description", "Required optimistic-lock hash from context."}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 30000}}},
                }},
                {"required", json::array({"sourcePath", "ifGraphHash"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_capture_preview"},
            {"description", "Ask the live WebGL viewport to render and return its current PNG plus camera, mesh-band profile, and shading metadata. Optionally select an img2threejs-style beauty, silhouette, semantic-ID, depth, normal, or roughness/material-ID diagnostic pass."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                    {"width", {{"type", "integer"}, {"minimum", 16}, {"maximum", 8192}, {"description", "Output width in pixels; omit to use the viewport size."}}},
                    {"height", {{"type", "integer"}, {"minimum", 16}, {"maximum", 8192}, {"description", "Output height in pixels; omit to use the viewport size."}}},
                    {"transparent", {{"type", "boolean"}, {"description", "Alpha background PNG (drops the environment background)."}}},
                    {"dof", {{"type", "boolean"}, {"description", "Override the session depth-of-field switch for this capture."}}},
                    {"xray", {{"type", "boolean"}, {"description", "Temporary depth-transparent solid pass for layout or section review captures; the viewport returns to its normal shading afterwards."}}},
                    {"shadingMode", {{"type", "string"}, {"enum", {"solid", "material", "rendered"}}, {"description", "Temporary shading override for this capture; the interactive viewport mode is restored afterwards."}}},
                    {"renderPass", {{"type", "string"}, {"enum", {"beauty", "alpha-silhouette", "semantic-id", "depth", "normal", "roughness-material-id"}}, {"description", "Deterministic diagnostic pass. Helpers are hidden and the interactive viewport is restored afterwards."}}},
                    {"camera", {{"type", "object"}, {"description", "Camera override applied to the session camera before rendering; same fields as pcg_set_camera."}}},
                }},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_set_camera"},
            {"description", "Set the session physical camera in the live WebGL viewport: pose (position/target or azimuth/elevation/distance around target), lens (focalLengthMm or fov, sensorHeightMm), aperture (apertureFstop), focus (focusDistance or focusOnTarget), dofEnabled, exposure, near/far, projection. Session-scoped; never written into the .pcg document. Waits for the editor to apply and returns the effective state. Coordinates are viewport world space (three.js right-handed; Unity +Z flips to -Z)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"position", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                    {"target", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                    {"up", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}}},
                    {"azimuth", {{"type", "number"}, {"description", "Degrees around +Y; 0 = +Z. Requires/keeps target."}}},
                    {"elevation", {{"type", "number"}, {"description", "Degrees above the horizon; clamped to ±89.9."}}},
                    {"distance", {{"type", "number"}, {"description", "Distance to target in world units."}}},
                    {"projection", {{"type", "string"}, {"enum", json::array({"perspective", "orthographic"})}}},
                    {"focalLengthMm", {{"type", "number"}, {"minimum", 8}, {"maximum", 400}}},
                    {"fov", {{"type", "number"}, {"minimum", 1}, {"maximum", 170}, {"description", "Vertical FOV in degrees; alternative to focalLengthMm."}}},
                    {"sensorHeightMm", {{"type", "number"}, {"minimum", 5}, {"maximum", 70}, {"default", 24}}},
                    {"apertureFstop", {{"type", "number"}, {"minimum", 0.7}, {"maximum", 64}}},
                    {"focusDistance", {{"type", "number"}, {"description", "World-unit focus distance for depth of field."}}},
                    {"focusOnTarget", {{"type", "boolean"}, {"description", "Set focusDistance to the position↔target distance."}}},
                    {"dofEnabled", {{"type", "boolean"}}},
                    {"exposure", {{"type", "number"}, {"minimum", 0.05}, {"maximum", 8}}},
                    {"near", {{"type", "number"}}},
                    {"far", {{"type", "number"}}},
                    {"orthographicFrustumHeight", {{"type", "number"}, {"minimum", 0.001}, {"description", "Vertical world-space frame size for an orthographic camera."}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_get_camera"},
            {"description", "Read the current session camera state from the live WebGL viewport (last applied state, or the camera block of the latest screenshot metadata)."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_get_component_bounds"},
            {"description", "Query a semantic PCG component by stable componentId. Returns its aggregated Unity-space AABB, anchors, and contributing node/Subgraph sources. Components are declared on node data.__semantic or Subgraph semantic."},
            {"inputSchema", {{"type", "object"}, {"properties", {{"componentId", {{"type", "string"}}}}}, {"required", json::array({"componentId"})}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_solve_camera"},
            {"description", "Recommend a reproducible camera for semantic component bounds. This only returns a pose; call pcg_set_camera to apply it. Input and component bounds use Unity space; returned pose uses viewport/three.js space (+Z flipped)."},
            {"inputSchema", {{"type", "object"}, {"properties", {
                {"componentIds", {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 1}}},
                {"view", {{"type", "string"}, {"enum", json::array({"top", "front", "side", "three-quarter"})}, {"default", "three-quarter"}}},
                {"aspect", {{"type", "number"}, {"description", "Width / height; 9:16 is 0.5625."}}},
                {"margin", {{"type", "number"}, {"minimum", 0}, {"maximum", 1}}},
                {"fov", {{"type", "number"}, {"minimum", 10}, {"maximum", 100}}},
            }}, {"required", json::array({"componentIds"})}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_validate_camera_frame"},
            {"description", "Validate that required semantic components have explicit bounds and are included in a solved framing set. Reports screen-space occluder risks conservatively; use the captured preview for final depth-accurate visual acceptance."},
            {"inputSchema", {{"type", "object"}, {"properties", {
                {"requiredComponentIds", {{"type", "array"}, {"items", {{"type", "string"}}}, {"minItems", 1}}},
                {"occluderComponentIds", {{"type", "array"}, {"items", {{"type", "string"}}}}},
                {"camera", {{"type", "object"}, {"description", "Optional pose from pcg_solve_camera. Omit to solve from required components."}}},
                {"framingSpec", {{"type", "object"}, {"description", "Optional pcg_solve_camera fields used when camera is omitted."}}},
            }}, {"required", json::array({"requiredComponentIds"})}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_patch_node"},
            {"description", "Patch existing node data through one undoable Web-editor action and wait for the apply acknowledgement."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"nodeId", {{"type", "string"}}},
                    {"patch", {{"type", "object"}, {"description", "Node data properties to merge."}}},
                    {"ifGraphHash", {{"type", "string"}, {"description", "Required optimistic-lock hash from context."}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"required", json::array({"nodeId", "patch", "ifGraphHash"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_apply_graph_ops"},
            {"description", "Atomically author the current graph/subgraph with one Undo step. Supported op values: add_node, remove_node, patch_node, move_node, add_edge, remove_edge, upsert_parameter, remove_parameter. add_node takes node; add_edge takes edge with explicit id and handles. The whole batch succeeds or fails."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"operations", {{"type", "array"}, {"minItems", 1}, {"maxItems", 500}, {"items", {{"type", "object"}}}}},
                    {"ifGraphHash", {{"type", "string"}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"required", json::array({"operations", "ifGraphHash"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_replace_graph"},
            {"description", "Atomically replace the complete live v1/v2 Graph JSON document, including Subgraphs and parameters, with native validation, optimistic locking, and one Undo step. Run from root scope."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"graph", {{"type", "object"}}},
                    {"ifGraphHash", {{"type", "string"}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"required", json::array({"graph", "ifGraphHash"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_save_graph"},
            {"description", "Persist the complete live graph through the Web editor. Omit path to save the current named graph, or pass a workspace-relative .pcg path."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"path", {{"type", "string"}, {"description", "Optional workspace-relative .pcg path; absolute and parent-traversal paths are rejected."}}},
                    {"ifGraphHash", {{"type", "string"}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"required", json::array({"ifGraphHash"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_validate"},
            {"description", "Validate the full live editor graph with the native PCG validator."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_cook"},
            {"description", "Cook the full live editor graph with native pcg-core and return a compact result summary."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"seed", {{"type", "integer"}, {"default", 42}}}}},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_kb_status"},
            {"description", "Read PICG knowledge-base status: index root, chunk count, engine, last error."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_kb_reindex"},
            {"description", "Force a full rebuild of the PICG knowledge-base index from .picg/rules and .picg/kb."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_kb_search"},
            {"description", "BM25 search over PICG project rules and experience notes under .picg/. Returns ranked chunks with path/heading/score/excerpt."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"query", {{"type", "string"}}},
                    {"top_k", {{"type", "integer"}, {"minimum", 1}, {"maximum", 50}, {"default", 10}}},
                    {"category", {{"type", "string"}, {"description", "Optional filter: rules or kb."}}},
                }},
                {"required", json::array({"query"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_kb_list"},
            {"description", "List markdown files indexed in .picg/rules and .picg/kb."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"category", {{"type", "string"}}}}},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_kb_get"},
            {"description", "Read a full markdown file from the PICG knowledge base by .picg-relative path (e.g. rules/graph-authoring/bridge.md)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"path", {{"type", "string"}}}}},
                {"required", json::array({"path"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_golden_graph_list"},
            {"description", "List .pcg golden-graph templates under .picg/golden-graphs/, optionally filtered by class (weapon/vehicle/bridge/building/scatter/prop/other)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"class", {{"type", "string"}}}}},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_golden_graph_get"},
            {"description", "Fetch a golden-graph .pcg template by stem name (e.g. m9-bayonet) or relative path under .picg/golden-graphs/."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"name", {{"type", "string"}}}}},
                {"required", json::array({"name"})},
                {"additionalProperties", false},
            }},
        },
    });
    for (auto& tool : tools) {
        tool["inputSchema"]["properties"]["editorSessionId"] = {
            {"type", "string"},
            {"description", "Web editor page id. Omit when exactly one page is online; when multiple pages are listed, ask the user which one to use."},
        };
    }
    return tools;
}

json CallToolInternal(
    const std::string& name,
    const json& arguments,
    const std::string& bound_editor_session_id) {
    const std::string editor_session_id = bound_editor_session_id.empty()
        ? arguments.value("editorSessionId", "")
        : bound_editor_session_id;
    if (name == "pcg_get_editor_context") {
        const json result = GetEditorContext(editor_session_id);
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_list_nodes") {
        const json result = RedactGraphToolResult(ListEditorNodes(editor_session_id));
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_get_graph") {
        const json result = RedactGraphToolResult(GetEditorDocument(editor_session_id));
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_get_node_types") {
        const json result = GetEditorNodeTypes(
            arguments.value("nodeType", ""),
            arguments.value("category", ""),
            editor_session_id);
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_bake_oriented_sdf") {
        const std::string node_id = arguments.value("nodeId", "oriented_sdf_surface");
        if (node_id.empty()) {
            return ToolResult({{"ok", false}, {"error", "nodeId must not be empty"}}, true);
        }
        const json position = arguments.value("position", json{{"x", 0.0}, {"y", 0.0}});
        if (!position.is_object() || !position.contains("x") || !position.contains("y") ||
            !position["x"].is_number() || !position["y"].is_number() ||
            !std::isfinite(position["x"].get<double>()) ||
            !std::isfinite(position["y"].get<double>())) {
            return ToolResult({{"ok", false}, {"error", "position requires finite numeric x and y"}}, true);
        }
        json baked = BuildOrientedSdfNodeData(arguments);
        if (!baked.value("ok", false)) return ToolResult(baked, true);
        json node = {
            {"id", node_id},
            {"type", "OrientedSdfSurface"},
            {"position", {{"x", position["x"]}, {"y", position["y"]}}},
            {"data", std::move(baked["nodeData"])},
        };
        json operations = json::array({{{"op", "add_node"}, {"node", std::move(node)}}});
        const bool stage_material = arguments.value("stageMaterial", true);
        const std::string material_node_id = arguments.value("materialNodeId", "reconstructed_material");
        if (stage_material) {
            if (material_node_id.empty()) {
                return ToolResult({{"ok", false}, {"error", "materialNodeId must not be empty"}}, true);
            }
            operations.push_back({
                {"op", "add_node"},
                {"node", {
                    {"id", material_node_id},
                    {"type", "Material"},
                    {"position", {{"x", position["x"].get<double>() + 320.0}, {"y", position["y"]}}},
                    {"data", std::move(baked["suggestedMaterialData"])},
                }},
            });
        }
        const json queued = QueueGraphCommand(
            {{"type", "applyGraphOps"},
             {"operations", std::move(operations)}},
            arguments.value("ifGraphHash", ""), false, editor_session_id);
        json applied = WaitForAppliedCommand(queued, CommandTimeout(arguments));
        const bool failed = applied.value("isError", false);
        json compact = applied.value("structuredContent", json::object());
        compact["bake"] = {
            {"algorithm", baked.value("algorithm", "")},
            {"nodeId", node_id},
            {"nodeType", baked.value("nodeType", "")},
            {"sourcePointCount", baked.value("sourcePointCount", 0)},
            {"payloadBytes", baked.value("payloadBytes", 0)},
            {"topologyCopied", baked.value("topologyCopied", true)},
            {"hasSourceVertexColors", baked.value("hasSourceVertexColors", false)},
            {"hasSourceUvs", baked.value("hasSourceUvs", false)},
            {"embeddedTextureBytes", baked.value("embeddedTextureBytes", json::object())},
            {"bounds", baked.value("bounds", json::object())},
        };
        if (stage_material) compact["bake"]["materialNodeId"] = material_node_id;
        return ToolResult(compact, failed);
    }
    if (name == "pcg_get_node") {
        std::string node_id = arguments.value("nodeId", "");
        if (node_id.empty()) {
            const json context = GetEditorContext(editor_session_id);
            if (context.contains("session") && context["session"].is_object()) {
                node_id = context["session"].value("selectedNodeId", "");
            }
        }
        if (node_id.empty()) return ToolResult({{"ok", false}, {"error", "no_node_selected"}}, true);
        const json result = RedactGraphToolResult(GetEditorNode(node_id, editor_session_id));
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_patch_node") {
        const json queued = QueueNodePatch(
            arguments.value("nodeId", ""),
            arguments.value("patch", json()),
            arguments.value("ifGraphHash", ""),
            editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_apply_graph_ops") {
        const json operations = arguments.value("operations", json());
        if (!operations.is_array() || operations.empty() || operations.size() > 500) {
            return ToolResult({{"ok", false}, {"error", "operations must contain 1-500 items"}}, true);
        }
        static const std::vector<std::string> supported = {
            "add_node", "remove_node", "patch_node", "move_node",
            "add_edge", "remove_edge", "upsert_parameter", "remove_parameter",
        };
        for (const auto& operation : operations) {
            if (!operation.is_object() || !operation.contains("op") || !operation["op"].is_string() ||
                std::find(supported.begin(), supported.end(), operation["op"].get<std::string>()) == supported.end()) {
                return ToolResult({{"ok", false}, {"error", "unsupported graph operation"}, {"operation", operation}}, true);
            }
        }
        const json queued = QueueGraphCommand(
            {{"type", "applyGraphOps"}, {"operations", operations}},
            arguments.value("ifGraphHash", ""), false, editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_replace_graph") {
        const json graph = arguments.value("graph", json());
        if (!graph.is_object()) return ToolResult({{"ok", false}, {"error", "graph object is required"}}, true);
        httplib::Request validate_req;
        httplib::Response validate_res;
        validate_req.body = graph.dump();
        HandleValidate(validate_req, validate_res);
        const json validation = json::parse(validate_res.body, nullptr, false);
        if (validation.is_discarded() || !validation.value("ok", false)) {
            return ToolResult({{"ok", false}, {"error", "graph_validation_failed"}, {"validation", validation}}, true);
        }
        const json queued = QueueGraphCommand(
            {{"type", "replaceGraph"}, {"graph", graph}},
            arguments.value("ifGraphHash", ""),
            true,
            editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_save_graph") {
        const std::string path = arguments.value("path", "");
        if (!path.empty() && !IsSafeRelativePcgPath(path)) {
            return ToolResult({{"ok", false}, {"error", "path must be a workspace-relative .pcg file without parent traversal"}}, true);
        }
        json command = {{"type", "saveGraph"}};
        if (!path.empty()) command["path"] = path;
        const json queued = QueueGraphCommand(
            std::move(command),
            arguments.value("ifGraphHash", ""), false, editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_capture_preview") {
        const int requested_timeout = arguments.value("timeoutMs", 10000);
        const int timeout = std::max(1000, std::min(30000, requested_timeout));
        json options = json::object();
        if (arguments.contains("width")) options["width"] = arguments["width"];
        if (arguments.contains("height")) options["height"] = arguments["height"];
        if (arguments.contains("transparent")) options["transparent"] = arguments["transparent"];
        if (arguments.contains("dof")) options["dof"] = arguments["dof"];
        if (arguments.contains("xray")) options["xray"] = arguments["xray"];
        if (arguments.contains("shadingMode")) options["shadingMode"] = arguments["shadingMode"];
        if (arguments.contains("renderPass")) options["renderPass"] = arguments["renderPass"];
        if (arguments.contains("camera")) {
            if (!arguments["camera"].is_object()) {
                return ToolResult({{"ok", false}, {"error", "camera must be an object"}}, true);
            }
            options["camera"] = arguments["camera"];
        }
        const uint64_t request_id = RequestPreviewCapture(editor_session_id, options);
        if (request_id == 0) return ToolResult(GetEditorContext(editor_session_id), true);
        PreviewSnapshot snapshot;
        if (!WaitForPreview(request_id, std::chrono::milliseconds(timeout), snapshot, editor_session_id)) {
            return ToolResult({
                {"ok", false}, {"error", "preview_timeout"}, {"requestId", request_id},
                {"hint", "Keep the Web editor and Preview panel open."},
            }, true);
        }
        json result = {
            {"content", json::array({
                {{"type", "text"}, {"text", snapshot.metadata.dump(2)}},
                {{"type", "image"}, {"data", EncodeBase64(snapshot.png)}, {"mimeType", "image/png"}},
            })},
            {"structuredContent", {
                {"ok", true}, {"requestId", snapshot.request_id}, {"capturedAt", snapshot.captured_at},
                {"bytes", snapshot.png.size()}, {"metadata", snapshot.metadata},
            }},
            {"isError", false},
        };
        return result;
    }
    if (name == "pcg_set_camera") {
        static const std::vector<std::string> camera_keys = {
            "position", "target", "up", "azimuth", "elevation", "distance",
            "projection", "focalLengthMm", "fov", "sensorHeightMm", "apertureFstop",
            "focusDistance", "focusOnTarget", "dofEnabled", "exposure", "near", "far", "orthographicFrustumHeight",
        };
        json camera = json::object();
        for (const auto& key : camera_keys) {
            if (arguments.contains(key)) camera[key] = arguments[key];
        }
        if (camera.empty()) {
            return ToolResult({{"ok", false}, {"error", "at least one camera field is required"}}, true);
        }
        const uint64_t command_id = RequestCameraCommand(camera, editor_session_id);
        if (command_id == 0) return ToolResult(GetEditorContext(editor_session_id), true);
        json camera_state;
        const int timeout = CommandTimeout(arguments);
        if (!WaitForCameraState(command_id, std::chrono::milliseconds(timeout), camera_state, editor_session_id)) {
            return ToolResult({
                {"ok", false}, {"error", "camera_apply_timeout"}, {"commandId", command_id},
                {"hint", "Keep the Web editor and Preview panel open."},
            }, true);
        }
        return ToolResult({
            {"ok", true}, {"commandId", command_id}, {"camera", std::move(camera_state)},
        });
    }
    if (name == "pcg_get_camera") {
        const json result = GetCameraState(editor_session_id);
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_get_component_bounds") {
        const json graph = GetEditorGraph(editor_session_id);
        if (graph.is_null()) return ToolResult({{"ok", false}, {"error", "editor_offline"}}, true);
        const std::string component_id = arguments.value("componentId", "");
        const auto components = CollectSemanticBounds(graph);
        const auto found = components.find(component_id);
        if (component_id.empty() || found == components.end() || !found->second.valid) {
            json available = json::array();
            for (const auto& [id, bounds] : components) if (bounds.valid) available.push_back(id);
            return ToolResult({{"ok", false}, {"error", "semantic_component_bounds_unavailable"}, {"componentId", component_id}, {"availableComponentIds", available}, {"hint", "Declare data.__semantic.bounds on a node, or semantic.bounds on its Subgraph. Negative space requires an explicit bounds proxy."}}, true);
        }
        return ToolResult({{"ok", true}, {"componentId", component_id}, {"bounds", BoundsJson(found->second)}});
    }
    if (name == "pcg_solve_camera" || name == "pcg_validate_camera_frame") {
        const json graph = GetEditorGraph(editor_session_id);
        if (graph.is_null()) return ToolResult({{"ok", false}, {"error", "editor_offline"}}, true);
        const auto components = CollectSemanticBounds(graph);
        const json required = name == "pcg_solve_camera" ? arguments.value("componentIds", json::array()) : arguments.value("requiredComponentIds", json::array());
        SemanticBounds requested;
        json missing = json::array();
        if (!UnionRequestedBounds(components, required, requested, missing)) {
            return ToolResult({{"ok", false}, {"error", "required_component_bounds_unavailable"}, {"missingComponentIds", missing}, {"hint", "Give every camera-required component an explicit semantic bounds proxy."}}, true);
        }
        json framing = name == "pcg_solve_camera" ? arguments : arguments.value("framingSpec", json::object());
        framing["componentIds"] = required;
        json camera = arguments.contains("camera") && arguments["camera"].is_object()
            ? arguments["camera"] : SolveSemanticCamera(requested, framing);
        if (name == "pcg_solve_camera") {
            return ToolResult({{"ok", true}, {"componentIds", required}, {"unityBounds", BoundsJson(requested)}, {"camera", camera}, {"applyWith", "pcg_set_camera"}});
        }
        json occluder_risks = json::array();
        const json occluders = arguments.value("occluderComponentIds", json::array());
        for (const auto& id_value : occluders) {
            if (!id_value.is_string()) continue;
            const auto found = components.find(id_value.get<std::string>());
            if (found == components.end() || !found->second.valid) {
                occluder_risks.push_back({{"componentId", id_value}, {"risk", "bounds_unavailable"}});
            } else {
                // This is intentionally conservative: bounds are a screen-space overlap warning,
                // not a substitute for a depth-buffer capture.
                occluder_risks.push_back({{"componentId", id_value}, {"risk", "requires_preview_depth_check"}, {"bounds", BoundsJson(found->second)}});
            }
        }
        return ToolResult({{"ok", occluder_risks.empty()}, {"requiredComponentIds", required}, {"camera", camera}, {"unityBounds", BoundsJson(requested)}, {"occlusionRisks", occluder_risks}, {"finalAcceptance", "Capture the solved pose and reject if a required component is outside frame or visually occluded."}}, !occluder_risks.empty());
    }
    if (name == "pcg_validate") {
        const json graph = GetEditorGraph(editor_session_id);
        if (graph.is_null()) return ToolResult({{"ok", false}, {"error", "editor_offline"}}, true);
        httplib::Request validate_req;
        httplib::Response validate_res;
        validate_req.body = graph.dump();
        HandleValidate(validate_req, validate_res);
        const json result = json::parse(validate_res.body, nullptr, false);
        if (result.is_discarded()) return ToolResult({{"ok", false}, {"error", "invalid_validator_response"}}, true);
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_cook") {
        const json graph = GetEditorGraph(editor_session_id);
        if (graph.is_null()) return ToolResult({{"ok", false}, {"error", "editor_offline"}}, true);
        httplib::Request cook_req;
        httplib::Response cook_res;
        cook_req.headers.emplace("Content-Type", "multipart/form-data; boundary=pcg-mcp");
        cook_req.files.emplace("graph", httplib::MultipartFormData{"graph", graph.dump(), "graph.json", "application/json"});
        const json meta = {{"seed", arguments.value("seed", 42)}, {"job_id", "mcp-cook"}};
        cook_req.files.emplace("meta", httplib::MultipartFormData{"meta", meta.dump(), "meta.json", "application/json"});
        HandleCook(cook_req, cook_res);
        if (cook_res.status != 200 || cook_res.body.size() < 56) {
            return ToolResult({{"ok", false}, {"error", "cook_transport_error"}, {"detail", cook_res.body}}, true);
        }
        const int32_t code = static_cast<int32_t>(ReadU32(cook_res.body, 8));
        const uint32_t error_size = ReadU32(cook_res.body, 56);
        const std::string error = 60 + error_size <= cook_res.body.size()
            ? cook_res.body.substr(60, error_size)
            : "malformed cook error payload";
        json result = {
            {"ok", code == 0}, {"code", code}, {"kind", ReadU32(cook_res.body, 12)},
            {"nodesExecuted", static_cast<int32_t>(ReadU32(cook_res.body, 16))},
            {"nodesSkipped", static_cast<int32_t>(ReadU32(cook_res.body, 20))},
            {"graphExecuteMs", ReadF64(cook_res.body, 24)}, {"binaryWriteMs", ReadF64(cook_res.body, 32)},
            {"pointCount", ReadU32(cook_res.body, 40)}, {"vertexCount", static_cast<int32_t>(ReadU32(cook_res.body, 48))},
            {"indexCount", static_cast<int32_t>(ReadU32(cook_res.body, 52))}, {"resultBytes", cook_res.body.size()},
        };
        if (!error.empty()) result["error"] = error;
        return ToolResult(result, code != 0);
    }
    if (name == "pcg_kb_status") {
        return ToolResult(KbStatus(), false);
    }
    if (name == "pcg_kb_reindex") {
        const json result = KbReindex();
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_kb_search") {
        const json result = KbSearch(
            arguments.value("query", ""),
            arguments.value("top_k", 10),
            arguments.value("category", ""));
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_kb_list") {
        const json result = KbList(arguments.value("category", ""));
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_kb_get") {
        const json result = KbGet(arguments.value("path", ""));
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_golden_graph_list") {
        const json result = KbGoldenGraphList(arguments.value("class", ""));
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_golden_graph_get") {
        const json result = KbGoldenGraphGet(arguments.value("name", ""));
        return ToolResult(result, !result.value("ok", false));
    }
    return ToolResult({{"ok", false}, {"error", "unknown_tool"}, {"name", name}}, true);
}

json HandleMessage(const json& message) {
    const json id = message.contains("id") ? message["id"] : json(nullptr);
    if (!message.is_object() || message.value("jsonrpc", "") != "2.0" ||
        !message.contains("method") || !message["method"].is_string()) {
        return ErrorResponse(id, -32600, "Invalid Request");
    }
    const std::string method = message["method"].get<std::string>();
    if (method.rfind("notifications/", 0) == 0) return json();
    if (!message.contains("id")) return json();
    if (method == "initialize") {
        return SuccessResponse(id, {
            {"protocolVersion", kProtocolVersion},
            {"capabilities", {{"tools", {{"listChanged", false}}}}},
            {"serverInfo", {{"name", "pcg-server"}, {"version", "1.0.0"}}},
            {"instructions", "Use editor context first. Discover node schemas with pcg_get_node_types. Pass graphHash to every write for optimistic locking; prefer one atomic pcg_apply_graph_ops batch, then validate, cook, capture, and save."},
        });
    }
    if (method == "ping") return SuccessResponse(id, json::object());
    if (method == "tools/list") return SuccessResponse(id, {{"tools", GetPcgToolDefinitions()}});
    if (method == "tools/call") {
        const json params = message.value("params", json::object());
        if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
            return ErrorResponse(id, -32602, "Invalid tools/call parameters");
        }
        return SuccessResponse(id, CallPcgTool(
            params["name"].get<std::string>(), params.value("arguments", json::object())));
    }
    return ErrorResponse(id, -32601, "Method not found", {{"method", method}});
}

void SendMcpResponse(const httplib::Request& req, httplib::Response& res, const json& response) {
    if (response.is_null()) {
        res.status = 202;
        return;
    }
    res.status = 200;
    const std::string accept = req.get_header_value("Accept");
    if (accept.find("text/event-stream") != std::string::npos) {
        res.set_content("event: message\ndata: " + response.dump() + "\n\n", "text/event-stream");
    } else {
        res.set_content(response.dump(), "application/json");
    }
}

}  // namespace

json GetPcgToolDefinitions() {
    return BuildToolDefinitions();
}

json CallPcgTool(
    const std::string& name,
    const json& arguments,
    const std::string& editor_session_id) {
    return CallToolInternal(name, arguments, editor_session_id);
}

void HandleMcpPost(const httplib::Request& req, httplib::Response& res) {
    if (!CheckServerAuth(req, res)) return;
    const json message = json::parse(req.body, nullptr, false);
    if (message.is_discarded()) {
        SendMcpResponse(req, res, ErrorResponse(nullptr, -32700, "Parse error"));
        return;
    }
    if (message.is_array()) {
        json responses = json::array();
        for (const auto& item : message) {
            json response = HandleMessage(item);
            if (!response.is_null()) responses.push_back(std::move(response));
        }
        SendMcpResponse(req, res, responses.empty() ? json() : responses);
        return;
    }
    SendMcpResponse(req, res, HandleMessage(message));
}

void HandleMcpGet(const httplib::Request& req, httplib::Response& res) {
    if (!CheckServerAuth(req, res)) return;
    res.status = 405;
    res.set_header("Allow", "POST");
    res.set_content(
        R"({"jsonrpc":"2.0","error":{"code":-32000,"message":"This stateless Streamable HTTP endpoint accepts MCP messages via POST; SSE responses are negotiated with Accept: text/event-stream."},"id":null})",
        "application/json");
}

}  // namespace pcg_server