#include "mcp_service.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "agent_service.hpp"
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
        camera["projection"] = "orthographic";
        camera["sensorFit"] = "vertical";
        camera["orthographicScale"] = std::max(0.01, height);
        return camera;
    }
    const double fov = std::max(10.0, std::min(100.0, spec.value("fov", 48.0)));
    const double radius = std::sqrt(sx * sx + sy * sy + sz * sz) * 0.5 * pad;
    const double distance = std::max(1.0, radius / std::tan(fov * 3.14159265358979323846 / 360.0));
    camera["projection"] = "perspective"; camera["fov"] = fov;
    camera["position"] = json::array({cx + distance * 0.72, cy + distance * 0.38, -cz + distance * 0.58});
    return camera;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string StableContentHash(const std::string& value) {
    uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : value) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string RevisionArgument(const json& arguments, const std::string& key = "sinceRevision") {
    const auto found = arguments.find(key);
    if (found == arguments.end()) return "";
    if (found->is_string()) return found->get<std::string>();
    if (found->is_number_integer()) return std::to_string(found->get<int64_t>());
    if (found->is_number_unsigned()) return std::to_string(found->get<uint64_t>());
    return "";
}

json SelectResponseFields(const json& value, const json& arguments) {
    const json fields = arguments.value("fields", json::array());
    if (!fields.is_array() || fields.empty() || !value.is_object()) return value;
    json selected = json::object();
    for (const auto* key : {"ok", "revision", "graphHash", "shotHash", "contentHash", "notModified"}) {
        if (value.contains(key)) selected[key] = value[key];
    }
    for (const auto& field : fields) if (field.is_string() && value.contains(field.get<std::string>())) selected[field.get<std::string>()] = value[field.get<std::string>()];
    return selected;
}

bool ReadTextFile(const std::filesystem::path& path, std::string& text) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

json ReadJsonFile(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) return json();
    json value = json::parse(stream, nullptr, false);
    return value.is_discarded() ? json() : value;
}

std::filesystem::path ProjectRoot() {
    return GetKbRoot().parent_path();
}

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

json ParseInlineList(std::string value) {
    json result = json::array();
    value = Trim(value);
    if (value.size() < 2 || value.front() != '[' || value.back() != ']') return result;
    std::stringstream stream(value.substr(1, value.size() - 2));
    std::string item;
    while (std::getline(stream, item, ',')) {
        item = Trim(item);
        if (!item.empty()) result.push_back(item);
    }
    return result;
}

json LoadRecipeById(const std::string& recipe_id, bool include_content) {
    if (recipe_id.empty()) return {{"ok", false}, {"error", "recipe_id_required"}};
    const auto root = GetKbRoot() / "kb" / "recipes";
    std::error_code error;
    if (!std::filesystem::exists(root, error)) {
        return {{"ok", false}, {"error", "recipe_root_missing"}, {"recipeId", recipe_id}};
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root, error)) {
        if (error || !entry.is_regular_file() || entry.path().extension() != ".md") continue;
        std::string text;
        if (!ReadTextFile(entry.path(), text)) continue;
        std::stringstream lines(text);
        std::string line;
        bool frontmatter = false;
        bool closed = false;
        json meta = json::object();
        std::string body;
        while (std::getline(lines, line)) {
            if (!frontmatter && Trim(line) == "---") { frontmatter = true; continue; }
            if (frontmatter && !closed && Trim(line) == "---") { closed = true; continue; }
            if (frontmatter && !closed) {
                const auto colon = line.find(':');
                if (colon == std::string::npos) continue;
                const std::string key = Trim(line.substr(0, colon));
                const std::string value = Trim(line.substr(colon + 1));
                if (key == "version") {
                    try { meta[key] = std::stoi(value); } catch (...) { meta[key] = value; }
                } else if (key == "roles" || key == "root_node_types") {
                    meta[key] = ParseInlineList(value);
                } else meta[key] = value;
            } else if (closed) body += line + "\n";
        }
        if (meta.value("recipe_id", "") != recipe_id) continue;
        const auto relative = std::filesystem::relative(entry.path(), GetKbRoot(), error);
        json result = {
            {"ok", true}, {"recipeId", recipe_id}, {"version", meta.value("version", 0)},
            {"roles", meta.value("roles", json::array())},
            {"rootNodeTypes", meta.value("root_node_types", json::array())},
            {"path", error ? entry.path().filename().generic_string() : relative.generic_string()},
            {"contentHash", StableContentHash(text)},
        };
        std::string summary;
        std::stringstream body_lines(body);
        while (std::getline(body_lines, line)) {
            line = Trim(line);
            if (!line.empty() && line.front() != '#') { summary = line; break; }
        }
        result["summary"] = summary;
        if (include_content) result["content"] = body;
        return result;
    }
    return {{"ok", false}, {"error", "recipe_not_found"}, {"recipeId", recipe_id}};
}

json ListLibraryItems(const json& arguments) {
    const auto index_path = ProjectRoot() / "library" / "library-index.json";
    std::string index_text;
    ReadTextFile(index_path, index_text);
    const json index = json::parse(index_text, nullptr, false);
    if (!index.is_object() || !index.contains("items") || !index["items"].is_array()) {
        return {{"ok", false}, {"error", "library_index_unavailable"}};
    }
    const std::string category = Lower(arguments.value("category", ""));
    const std::string query = Lower(arguments.value("query", ""));
    const std::string role = Lower(arguments.value("role", ""));
    const bool full = arguments.value("detail", "compact") == "full";
    json items = json::array();
    for (const auto& item : index["items"]) {
        if (!item.is_object()) continue;
        const json semantic = item.value("semantic", json::object());
        if (!category.empty() && Lower(item.value("category", "")) != category) continue;
        if (!role.empty() && Lower(semantic.value("role", "")) != role) continue;
        const std::string haystack = Lower(item.value("id", "") + " " + item.value("displayName", "") + " " +
            item.value("description", "") + " " + semantic.value("componentId", ""));
        if (!query.empty() && haystack.find(query) == std::string::npos) continue;
        json compact = {
            {"id", item.value("id", "")}, {"name", item.value("displayName", "")},
            {"category", item.value("category", "")}, {"contentHash", item.value("contentHash", "")},
            {"parameters", item.value("parameters", json::array())}, {"semantic", semantic},
            {"recipeId", semantic.value("recipeId", "")},
        };
        items.push_back(full ? item : compact);
    }
    const std::string revision = StableContentHash(index_text);
    if (RevisionArgument(arguments) == revision) return {{"ok", true}, {"notModified", true}, {"revision", revision}};
    return SelectResponseFields({{"ok", true}, {"items", items}, {"count", items.size()}, {"indexVersion", index.value("version", 0)}, {"revision", revision}}, arguments);
}

json Vec3Or(const json& value, const json& fallback) {
    double x, y, z;
    return ReadVec3(value, x, y, z) ? json::array({x, y, z}) : fallback;
}

struct SceneComponentRecord {
    json semantic = json::object();
    json owner = json::object();
    json members = json::array();
    json dependencies = json::array();
};

std::unordered_map<std::string, SceneComponentRecord> CollectSceneComponents(const json& graph) {
    std::unordered_map<std::string, SceneComponentRecord> result;
    if (!graph.is_object() || !graph.value("nodes", json::array()).is_array()) return result;
    std::unordered_map<std::string, json> nodes;
    for (const auto& node : graph["nodes"]) if (node.is_object()) nodes[node.value("id", "")] = node;
    std::unordered_map<std::string, std::string> member_owner;
    for (const auto& [node_id, node] : nodes) {
        const json semantic = node.value("data", json::object()).value("__semantic", json::object());
        const std::string component_id = semantic.value("componentId", "");
        if (component_id.empty()) continue;
        auto& record = result[component_id];
        record.semantic = semantic;
        const json data = node.value("data", json::object());
        record.owner = {
            {"kind", "node"}, {"nodeId", node_id}, {"nodeType", node.value("type", "")},
            {"worldTransform", {{"position", Vec3Or(data.value("translate", json::array()), json::array({0, 0, 0}))},
                {"rotationEulerDeg", Vec3Or(data.value("rotation", json::array()), json::array({0, 0, 0}))},
                {"scale", Vec3Or(data.value("scale", json::array()), json::array({1, 1, 1}))}}},
        };
        const json declared = semantic.value("memberNodeIds", json::array({node_id}));
        const json members = declared.is_array() && !declared.empty() ? declared : json::array({node_id});
        for (const auto& member_id : members) {
            if (!member_id.is_string()) continue;
            const auto found = nodes.find(member_id.get<std::string>());
            if (found == nodes.end()) continue;
            record.members.push_back({{"nodeId", member_id}, {"nodeType", found->second.value("type", "")}});
            member_owner[member_id.get<std::string>()] = component_id;
        }
    }
    for (const auto& edge : graph.value("edges", json::array())) {
        if (!edge.is_object()) continue;
        const auto source = member_owner.find(edge.value("source", ""));
        const auto target = member_owner.find(edge.value("target", ""));
        if (source == member_owner.end() || target == member_owner.end() || source->second == target->second) continue;
        auto& dependencies = result[target->second].dependencies;
        if (std::find(dependencies.begin(), dependencies.end(), source->second) == dependencies.end()) dependencies.push_back(source->second);
    }
    return result;
}

json ComponentSummary(const std::string& component_id, const SceneComponentRecord& record, const json& shot) {
    json result = {
        {"componentId", component_id}, {"label", record.semantic.value("label", "")},
        {"role", record.semantic.value("role", "")}, {"zone", record.semantic.value("zone", "")},
        {"intent", record.semantic.value("intent", "")}, {"recipeId", record.semantic.value("recipeId", "")},
        {"owner", record.owner}, {"memberCount", record.members.size()}, {"dependencies", record.dependencies},
    };
    result["worldTransform"] = record.owner.value("worldTransform", json::object());
    if (record.semantic.contains("bounds")) result["bounds"] = record.semantic["bounds"];
    if (record.semantic.contains("anchors")) result["anchors"] = record.semantic["anchors"];
    if (record.semantic.contains("camera")) result["camera"] = record.semantic["camera"];
    if (shot.is_object()) {
        for (const auto& component : shot.value("components", json::array())) {
            if (component.is_object() && component.value("componentId", "") == component_id) {
                result["worldTransform"] = component.value("transform", json::object());
                result["visibility"] = {{"fromSeconds", component.value("visibleFromSeconds", 0.0)}, {"untilSeconds", component.value("visibleUntilSeconds", shot.value("durationSeconds", 0.0))}};
            }
        }
        for (const auto& animation : shot.value("objectAnimations", json::array())) {
            if (animation.is_object() && animation.value("componentId", "") == component_id) result["animation"] = animation;
        }
    }
    return result;
}

json DescribeScene(const json& document, const json& shot, const json& arguments) {
    if (!document.value("ok", false)) return document;
    const json graph = document.value("graph", json::object());
    const auto components = CollectSceneComponents(graph);
    json list = json::array();
    json roles = json::object();
    json characters = json::array();
    std::set<std::string> recipe_ids;
    for (const auto& [id, component] : components) {
        json summary = ComponentSummary(id, component, shot);
        const std::string role = summary.value("role", "unclassified");
        roles[role].push_back(id);
        if (role == "character" || role == "extra") characters.push_back(id);
        if (!summary.value("recipeId", "").empty()) recipe_ids.insert(summary.value("recipeId", ""));
        list.push_back(std::move(summary));
    }
    const size_t node_count = graph.value("nodes", json::array()).size();
    std::set<std::string> classified;
    for (const auto& [_, component] : components) for (const auto& member : component.members) classified.insert(member.value("nodeId", ""));
    json result = {
        {"ok", true}, {"revision", document.value("graphHash", "")}, {"graphHash", document.value("graphHash", "")},
        {"shotHash", shot.value("shotHash", "")}, {"components", list}, {"componentTree", roles},
        {"characters", characters}, {"recipeIds", json(recipe_ids)},
        {"componentCount", list.size()}, {"nodeCount", node_count}, {"unclassifiedNodeCount", node_count - std::min(node_count, classified.size())},
        {"warnings", json::array()},
    };
    const std::string since = RevisionArgument(arguments);
    if (!since.empty() && since == result.value("revision", "")) return {{"ok", true}, {"notModified", true}, {"revision", since}};
    return SelectResponseFields(result, arguments);
}

bool HasNodeId(const json& graph, const std::string& node_id) {
    for (const auto& node : graph.value("nodes", json::array())) if (node.is_object() && node.value("id", "") == node_id) return true;
    return false;
}

const json* ResolveSemanticScope(const json& graph, const json& edit_path) {
    if (!edit_path.is_array() || edit_path.empty()) return &graph;
    const json& id_value = edit_path.back();
    if (!id_value.is_string()) return nullptr;
    for (const auto& subgraph : graph.value("subgraphs", json::array())) {
        if (subgraph.is_object() && subgraph.value("id", "") == id_value.get<std::string>()) return &subgraph;
    }
    return nullptr;
}

std::string LibraryDefinitionId(const std::string& id) {
    std::string value = "lib_";
    for (const unsigned char c : id) value.push_back(std::isalnum(c) ? static_cast<char>(c) : '_');
    return value;
}

json BuildLibraryInstances(json graph, const json& requests) {
    const json index = ReadJsonFile(ProjectRoot() / "library" / "library-index.json");
    if (!index.is_object() || !index.value("items", json::array()).is_array()) return {{"ok", false}, {"error", "library_index_unavailable"}};
    if (!requests.is_array() || requests.empty() || requests.size() > 100) return {{"ok", false}, {"error", "items must contain 1-100 entries"}};
    if (!graph.contains("subgraphs") || !graph["subgraphs"].is_array()) graph["subgraphs"] = json::array();
    json created = json::array();
    json skipped = json::array();
    size_t index_position = 0;
    for (const auto& request : requests) {
        if (!request.is_object()) return {{"ok", false}, {"error", "library item request must be an object"}, {"operationIndex", index_position}};
        const std::string wanted = request.value("libraryId", request.value("name", ""));
        const std::string instance_id = request.value("instanceId", "");
        if (wanted.empty() || instance_id.empty()) return {{"ok", false}, {"error", "libraryId and instanceId are required"}, {"operationIndex", index_position}};
        const json* item = nullptr;
        for (const auto& candidate : index["items"]) if (candidate.value("id", "") == wanted || candidate.value("displayName", "") == wanted) { item = &candidate; break; }
        if (item == nullptr) return {{"ok", false}, {"error", "library_item_not_found"}, {"libraryId", wanted}, {"operationIndex", index_position}};
        const std::string instance_node_id = instance_id + "__asset";
        const std::string owner_node_id = instance_id + "__transform";
        const auto existing_components = CollectSceneComponents(graph);
        if (existing_components.find(instance_id) != existing_components.end() || HasNodeId(graph, instance_node_id) || HasNodeId(graph, owner_node_id)) {
            skipped.push_back(instance_id);
            ++index_position;
            continue;
        }
        const std::string definition_id = LibraryDefinitionId(item->value("id", ""));
        bool has_definition = false;
        for (const auto& definition : graph["subgraphs"]) if (definition.value("id", "") == definition_id) has_definition = true;
        if (!has_definition) {
            const json asset = ReadJsonFile(ProjectRoot() / "library" / item->value("file", ""));
            if (!asset.is_object()) return {{"ok", false}, {"error", "library_asset_unavailable"}, {"libraryId", wanted}, {"operationIndex", index_position}};
            graph["subgraphs"].push_back({
                {"id", definition_id}, {"name", asset.value("name", item->value("displayName", ""))},
                {"inputs", asset.value("inputs", json::array())}, {"outputs", asset.value("outputs", json::array())},
                {"nodes", asset.value("nodes", json::array())}, {"edges", asset.value("edges", json::array())},
                {"parameters", asset.value("parameters", json::array())}, {"semantic", item->value("semantic", json::object())},
            });
        }
        const double x = request.value("layoutX", static_cast<double>(index_position) * 520.0);
        const double y = request.value("layoutY", 0.0);
        json semantic = item->value("semantic", json::object());
        if (request.contains("semantic") && request["semantic"].is_object()) semantic.update(request["semantic"]);
        semantic["componentId"] = instance_id;
        semantic["memberNodeIds"] = json::array({instance_node_id, owner_node_id});
        json instance_data = {{"subgraphId", definition_id}, {"__nodeTitle", item->value("displayName", instance_id)}};
        if (request.contains("parameters") && request["parameters"].is_object()) instance_data["subgraphParameterOverrides"] = request["parameters"];
        graph["nodes"].push_back({{"id", instance_node_id}, {"type", "Subgraph"}, {"position", {{"x", x}, {"y", y}}}, {"data", instance_data}});
        graph["nodes"].push_back({{"id", owner_node_id}, {"type", "TransformMesh"}, {"position", {{"x", x + 280.0}, {"y", y}}}, {"data", {
            {"translate", Vec3Or(request.value("position", json::array()), json::array({0, 0, 0}))},
            {"rotation", Vec3Or(request.value("rotation", json::array()), json::array({0, 0, 0}))},
            {"scale", Vec3Or(request.value("scale", json::array()), json::array({1, 1, 1}))},
            {"__nodeTitle", instance_id}, {"__semantic", semantic},
        }}});
        const json outputs = item->value("outputs", json::array());
        const std::string output_id = !outputs.empty() ? outputs[0].value("id", "out") : "out";
        graph["edges"].push_back({{"id", "e_" + instance_id + "__placed"}, {"source", instance_node_id}, {"target", owner_node_id}, {"sourceHandle", output_id}, {"targetHandle", "in"}});
        created.push_back({{"componentId", instance_id}, {"libraryId", item->value("id", "")}, {"ownerNodeId", owner_node_id}, {"definitionId", definition_id}, {"semantic", semantic}});
        ++index_position;
    }
    return {{"ok", true}, {"graph", graph}, {"created", created}, {"skipped", skipped}, {"changeCount", created.size()}};
}

bool PathInside(const std::filesystem::path& child, const std::filesystem::path& root) {
    const auto child_text = child.lexically_normal().generic_string();
    std::string root_text = root.lexically_normal().generic_string();
    if (!root_text.empty() && root_text.back() != '/') root_text.push_back('/');
    return child_text == root.lexically_normal().generic_string() || child_text.rfind(root_text, 0) == 0;
}

json ResolveOutputTarget(const std::string& root_id, const std::string& relative_path) {
    if (root_id.empty() || relative_path.empty()) return {{"ok", false}, {"error", "outputRootId and relativePath are required"}};
    const std::filesystem::path relative(relative_path);
    if (relative.is_absolute()) return {{"ok", false}, {"error", "absolute_relative_path_forbidden"}};
    for (const auto& part : relative) if (part == "..") return {{"ok", false}, {"error", "path_traversal_forbidden"}};
    const json config = ReadJsonFile(GetKbRoot() / "output-roots.json");
    const json roots = config.value("roots", json::object());
    if (!roots.contains(root_id) || !roots[root_id].is_string()) return {{"ok", false}, {"error", "output_root_not_allowed"}, {"outputRootId", root_id}};
    std::filesystem::path root = roots[root_id].get<std::string>();
    if (root.is_relative()) root = ProjectRoot() / root;
    std::error_code error;
    root = std::filesystem::weakly_canonical(root, error);
    if (error) return {{"ok", false}, {"error", "output_root_unavailable"}, {"detail", error.message()}};
    const auto candidate = (root / relative).lexically_normal();
    if (!PathInside(candidate, root)) return {{"ok", false}, {"error", "output_path_escape"}};
    const auto parent = std::filesystem::weakly_canonical(candidate.parent_path(), error);
    if (!error && !PathInside(parent, root)) return {{"ok", false}, {"error", "symlink_escape_forbidden"}};
    return {{"ok", true}, {"root", root.generic_string()}, {"target", candidate.generic_string()}};
}

bool WriteTextBatchAtomically(
    const std::vector<std::pair<std::filesystem::path, std::string>>& files,
    std::string& error_message) {
    struct StagedFile { std::filesystem::path target; std::filesystem::path staged; std::filesystem::path backup; bool installed = false; bool backed_up = false; };
    std::vector<StagedFile> staged_files;
    const std::string suffix = ".picg-stage-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    std::error_code error;
    for (const auto& [target, value] : files) {
        std::filesystem::create_directories(target.parent_path(), error);
        if (error) { error_message = error.message(); break; }
        StagedFile entry{target, target.string() + suffix, target.string() + suffix + ".backup"};
        std::ofstream stream(entry.staged, std::ios::binary | std::ios::trunc);
        if (!stream) { error_message = "cannot open staged output"; break; }
        stream << value;
        if (!stream.good()) { error_message = "failed writing staged output"; break; }
        staged_files.push_back(std::move(entry));
    }
    if (error_message.empty()) {
        for (auto& entry : staged_files) {
            if (std::filesystem::exists(entry.target)) {
                std::filesystem::rename(entry.target, entry.backup, error);
                if (error) { error_message = error.message(); break; }
                entry.backed_up = true;
            }
            std::filesystem::rename(entry.staged, entry.target, error);
            if (error) { error_message = error.message(); break; }
            entry.installed = true;
        }
    }
    if (!error_message.empty()) {
        for (auto it = staged_files.rbegin(); it != staged_files.rend(); ++it) {
            if (it->installed) std::filesystem::remove(it->target, error);
            if (it->backed_up) std::filesystem::rename(it->backup, it->target, error);
            std::filesystem::remove(it->staged, error);
        }
        return false;
    }
    for (const auto& entry : staged_files) if (entry.backed_up) std::filesystem::remove(entry.backup, error);
    return true;
}

json SaveProjectFiles(const json& arguments, const std::string& editor_session_id) {
    const json document = GetEditorDocument(editor_session_id);
    const json shot_document = GetEditorShot(editor_session_id);
    if (!document.value("ok", false)) return document;
    if (!shot_document.value("ok", false)) return shot_document;
    const std::string expected_graph = arguments.value("ifGraphHash", "");
    const std::string expected_shot = arguments.value("ifShotHash", "");
    if (expected_graph.empty() || expected_shot.empty()) return {{"ok", false}, {"error", "ifGraphHash and ifShotHash are required"}};
    if (expected_graph != document.value("graphHash", "")) return {{"ok", false}, {"error", "graph_conflict"}, {"currentGraphHash", document.value("graphHash", "")}};
    if (expected_shot != shot_document.value("shotHash", "")) return {{"ok", false}, {"error", "shot_conflict"}, {"currentShotHash", shot_document.value("shotHash", "")}};
    json resolved = ResolveOutputTarget(arguments.value("outputRootId", ""), arguments.value("relativePath", ""));
    if (!resolved.value("ok", false)) return resolved;
    std::filesystem::path stem = resolved.value("target", "");
    if (stem.has_extension()) stem.replace_extension();
    const auto graph_path = stem.string() + ".picg";
    const auto shot_path = stem.string() + ".picgshot";
    const auto project_path = stem.string() + ".picgproject";
    json shot = shot_document.value("shot", json::object());
    shot["graphPath"] = std::filesystem::path(graph_path).filename().generic_string();
    shot["graphHash"] = document.value("graphHash", "");
    const json project = {{"format", "PICG-project"}, {"version", "1.0"}, {"graph", document.value("graph", json::object())}, {"shot", shot}, {"graphPath", std::filesystem::path(graph_path).filename().generic_string()}};
    std::string error_message;
    const std::vector<std::pair<std::filesystem::path, std::string>> files = {
        {graph_path, document.value("graph", json::object()).dump(2) + "\n"},
        {shot_path, shot.dump(2) + "\n"},
        {project_path, project.dump(2) + "\n"},
    };
    if (!WriteTextBatchAtomically(files, error_message)) {
        return {{"ok", false}, {"error", "project_write_failed"}, {"detail", error_message}};
    }
    return {{"ok", true}, {"graphHash", expected_graph}, {"shotHash", expected_shot}, {"paths", {{"graph", graph_path}, {"shot", shot_path}, {"project", project_path}}}};
}

json CopyExportedShot(const json& applied, const json& arguments) {
    if (applied.value("isError", false) || !arguments.contains("outputRootId")) return applied;
    const json structured = applied.value("structuredContent", json::object());
    const json detail = structured.value("applyResult", json::object()).value("detail", json::object());
    const std::string source_relative = detail.value("path", "");
    if (source_relative.empty()) return applied;
    const auto export_root = (ProjectRoot() / "exports" / "previs").lexically_normal();
    const auto source = (ProjectRoot() / source_relative).lexically_normal();
    if (!PathInside(source, export_root) || !std::filesystem::is_regular_file(source)) {
        return ToolResult({{"ok", false}, {"error", "encoded_video_unavailable"}, {"sourcePath", source.generic_string()}}, true);
    }
    std::filesystem::path relative_file(arguments.value("relativePath", source.filename().generic_string()));
    if (!relative_file.has_extension()) relative_file.replace_extension(source.extension());
    if (relative_file.extension() != ".mp4" && relative_file.extension() != ".webm") {
        return ToolResult({{"ok", false}, {"error", "video_output_must_be_mp4_or_webm"}}, true);
    }
    json resolved = ResolveOutputTarget(arguments.value("outputRootId", ""), relative_file.generic_string());
    if (!resolved.value("ok", false)) return ToolResult(resolved, true);
    const std::filesystem::path target = resolved.value("target", "");
    std::error_code error;
    std::filesystem::create_directories(target.parent_path(), error);
    if (!error) std::filesystem::copy_file(source, target, std::filesystem::copy_options::overwrite_existing, error);
    if (error) return ToolResult({{"ok", false}, {"error", "video_copy_failed"}, {"detail", error.message()}}, true);
    json result = applied;
    result["structuredContent"]["videoPath"] = target.generic_string();
    result["structuredContent"]["sourcePath"] = source.generic_string();
    return result;
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
    if (value.empty() || path.is_absolute() || (path.extension() != ".pcg" && path.extension() != ".picg")) return false;
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
            {"description", "Read the live Web editor path, selection, preview target, graph hash, shot hash, active camera, and bridge status. Shot/camera keyframes live in the shot sidecar, never in the .pcg graph."},
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
            {"description", "Set the session physical camera in the live WebGL viewport using Blender camera semantics: pose, projection, focalLengthMm/fov, sensorWidthMm/sensorHeightMm/sensorFit, shiftX/shiftY, aperture, focus/DOF, clipping, exposure, and orthographicScale. Session-scoped; never written into the .pcg document. Waits for the editor to apply and returns the effective state. Coordinates are viewport world space (three.js right-handed; Unity +Z flips to -Z)."},
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
                    {"sensorWidthMm", {{"type", "number"}, {"minimum", 1}, {"maximum", 100}, {"default", 36}}},
                    {"sensorHeightMm", {{"type", "number"}, {"minimum", 5}, {"maximum", 70}, {"default", 24}}},
                    {"sensorFit", {{"type", "string"}, {"enum", json::array({"auto", "horizontal", "vertical"})}, {"default", "auto"}}},
                    {"shiftX", {{"type", "number"}, {"minimum", -2}, {"maximum", 2}}},
                    {"shiftY", {{"type", "number"}, {"minimum", -2}, {"maximum", 2}}},
                    {"apertureFstop", {{"type", "number"}, {"minimum", 0.7}, {"maximum", 64}}},
                    {"apertureBlades", {{"type", "integer"}, {"minimum", 0}, {"maximum", 16}}},
                    {"apertureRotationDeg", {{"type", "number"}, {"minimum", -180}, {"maximum", 180}}},
                    {"apertureRatio", {{"type", "number"}, {"minimum", 0.01}, {"maximum", 1}}},
                    {"focusDistance", {{"type", "number"}, {"description", "World-unit focus distance for depth of field."}}},
                    {"focusOnTarget", {{"type", "boolean"}, {"description", "Set focusDistance to the position↔target distance."}}},
                    {"dofEnabled", {{"type", "boolean"}}},
                    {"exposure", {{"type", "number"}, {"minimum", 0.05}, {"maximum", 8}}},
                    {"near", {{"type", "number"}}},
                    {"far", {{"type", "number"}}},
                    {"orthographicScale", {{"type", "number"}, {"minimum", 0.001}, {"description", "Blender ortho_scale: extent along the fitted sensor axis."}}},
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
            {"name", "pcg_get_shot"},
            {"description", "Read the live shot sidecar: cameras, camera graph wires, active camera, and keyframes. This is not part of the .pcg graph. Use shotHash as ifShotHash for later writes."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_set_camera_keyframes"},
            {"description", "Write keyframes for one camera station in the live shot sidecar. mode=replace replaces that camera's track; mode=upsert inserts or overwrites keys at matching times. Defaults to the active camera. Requires ifShotHash from pcg_get_shot or pcg_get_editor_context."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"cameraId", {{"type", "string"}, {"description", "Camera station id; omit to use the active camera."}}},
                    {"mode", {{"type", "string"}, {"enum", json::array({"replace", "upsert"})}, {"default", "replace"}}},
                    {"keyframes", {{"type", "array"}, {"minItems", 0}, {"maxItems", 500}, {"items", {{"type", "object"}, {"properties", {
                        {"id", {{"type", "string"}}},
                        {"timeSeconds", {{"type", "number"}}},
                        {"interpolation", {{"type", "string"}, {"enum", json::array({"linear", "ease-in", "ease-out", "ease-in-out", "step"})}}},
                        {"value", {{"type", "object"}, {"description", "CameraCommand fields: position, target, focalLengthMm, etc."}}},
                    }}, {"required", json::array({"timeSeconds", "value"})}}}}},
                    {"seekTimeSeconds", {{"type", "number"}, {"description", "Optional playhead time after applying keys."}}},
                    {"ifShotHash", {{"type", "string"}, {"description", "Required optimistic-lock hash from pcg_get_shot / editor context."}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"required", json::array({"keyframes", "ifShotHash"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_upsert_camera"},
            {"description", "Add or update a camera station on the Cameras graph. Session-scoped shot sidecar; never written into .pcg. Requires ifShotHash."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"id", {{"type", "string"}}},
                    {"name", {{"type", "string"}}},
                    {"presetId", {{"type", "string"}, {"description", "Catalog body id from the Cameras-tab palette (e.g. arri_alexa_35, full_frame). Applies sensor size and default lens."}}},
                    {"position", {{"type", "object"}, {"properties", {
                        {"x", {{"type", "number"}}}, {"y", {{"type", "number"}}},
                    }}, {"additionalProperties", false}}},
                    {"camera", {{"type", "object"}, {"description", "Optional rest-pose CameraCommand."}}},
                    {"ifShotHash", {{"type", "string"}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"required", json::array({"ifShotHash"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_connect_cameras"},
            {"description", "Wire two camera stations, or a camera to shot_output, on the Cameras graph. Requires ifShotHash."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"source", {{"type", "string"}}},
                    {"target", {{"type", "string"}}},
                    {"ifShotHash", {{"type", "string"}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"required", json::array({"source", "target", "ifShotHash"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_select_camera"},
            {"description", "Select a camera station so Preview keys and plays that camera's track. Requires ifShotHash."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"cameraId", {{"type", "string"}}},
                    {"ifShotHash", {{"type", "string"}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"required", json::array({"cameraId", "ifShotHash"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_preview_shot"},
            {"description", "Seek or play the live shot in the Web preview. Optional cameraId selects that station first. Does not mutate the shot document."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"cameraId", {{"type", "string"}}},
                    {"timeSeconds", {{"type", "number"}}},
                    {"play", {{"type", "boolean"}, {"default", false}}},
                    {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
                }},
                {"additionalProperties", false},
            }},
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
            {"description", "Force a full rebuild of the PICG knowledge-base index from .pcg-ai/rules and .pcg-ai/kb."},
            {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
        },
        {
            {"name", "pcg_kb_search"},
            {"description", "BM25 search over PICG project rules and experience notes under .pcg-ai/. Returns ranked chunks with path/heading/score/excerpt."},
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
            {"description", "List markdown files indexed in .pcg-ai/rules and .pcg-ai/kb."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"category", {{"type", "string"}}}}},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_kb_get"},
            {"description", "Read a full markdown file from the PICG knowledge base by .pcg-ai-relative path (e.g. rules/graph-authoring/bridge.md)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"path", {{"type", "string"}}}}},
                {"required", json::array({"path"})},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_golden_graph_list"},
            {"description", "List .pcg golden-graph templates under .pcg-ai/golden-graphs/, optionally filtered by class (weapon/vehicle/bridge/building/scatter/prop/other)."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"class", {{"type", "string"}}}}},
                {"additionalProperties", false},
            }},
        },
        {
            {"name", "pcg_golden_graph_get"},
            {"description", "Fetch a golden-graph .pcg template by stem name (e.g. m9-bayonet) or relative path under .pcg-ai/golden-graphs/."},
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
    if (name == "pcg_list_editor_sessions") {
        const json result = ListEditorSessions();
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_bind_editor_session") {
        const json result = BindEditorSession(arguments.value("editorSessionId", ""));
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_list_library_items") {
        const json result = ListLibraryItems(arguments);
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_get_recipe") {
        json result = LoadRecipeById(arguments.value("recipeId", ""), arguments.value("detail", "compact") == "full");
        if (result.value("ok", false) && arguments.contains("expectedVersion") && arguments["expectedVersion"].is_number_integer() &&
            arguments["expectedVersion"].get<int>() != result.value("version", 0)) {
            return ToolResult({{"ok", false}, {"error", "recipe_version_mismatch"}, {"recipeId", arguments.value("recipeId", "")},
                {"expectedVersion", arguments["expectedVersion"]}, {"actualVersion", result.value("version", 0)}, {"contentHash", result.value("contentHash", "")}}, true);
        }
        const std::string since = RevisionArgument(arguments);
        if (result.value("ok", false) && !since.empty() && since == result.value("contentHash", "")) {
            result = {{"ok", true}, {"notModified", true}, {"recipeId", arguments.value("recipeId", "")}, {"contentHash", since}};
        }
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_describe_scene") {
        const json document = GetEditorDocument(editor_session_id);
        const json shot_document = GetEditorShot(editor_session_id);
        json shot = shot_document.value("shot", json::object());
        shot["shotHash"] = shot_document.value("shotHash", "");
        const json result = DescribeScene(document, shot, arguments);
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_get_component") {
        const json document = GetEditorDocument(editor_session_id);
        if (!document.value("ok", false)) return ToolResult(document, true);
        const json shot_document = GetEditorShot(editor_session_id);
        json shot = shot_document.value("shot", json::object());
        shot["shotHash"] = shot_document.value("shotHash", "");
        const std::string component_id = arguments.value("componentId", "");
        const auto components = CollectSceneComponents(document.value("graph", json::object()));
        const auto found = components.find(component_id);
        if (found == components.end()) {
            json available = json::array();
            for (const auto& [id, _] : components) available.push_back(id);
            return ToolResult({{"ok", false}, {"error", "component_not_found"}, {"componentId", component_id}, {"availableComponentIds", available}}, true);
        }
        json result = ComponentSummary(component_id, found->second, shot);
        result["ok"] = true;
        result["graphHash"] = document.value("graphHash", "");
        result["shotHash"] = shot_document.value("shotHash", "");
        if (arguments.value("includeMembers", false) || arguments.value("detail", "compact") == "full") result["members"] = found->second.members;
        if (arguments.value("includeConnections", false) || arguments.value("detail", "compact") == "full") result["dependencies"] = found->second.dependencies;
        if (arguments.value("includeRecipe", false) && !found->second.semantic.value("recipeId", "").empty()) {
            result["recipe"] = LoadRecipeById(found->second.semantic.value("recipeId", ""), arguments.value("detail", "compact") == "full");
        }
        return ToolResult(result);
    }
    if (name == "pcg_upsert_component_semantics") {
        const json document = GetEditorDocument(editor_session_id);
        if (!document.value("ok", false)) return ToolResult(document, true);
        json graph = document.value("graph", json::object());
        const json* scope = ResolveSemanticScope(graph, document.value("editPath", json::array()));
        if (scope == nullptr) return ToolResult({{"ok", false}, {"error", "semantic_scope_unavailable"}, {"editPath", document.value("editPath", json::array())}}, true);
        const std::string owner_id = arguments.value("ownerNodeId", "");
        const json semantic = arguments.value("semantic", json::object());
        const std::string component_id = semantic.value("componentId", "");
        if (owner_id.empty() || component_id.empty()) return ToolResult({{"ok", false}, {"error", "ownerNodeId and semantic.componentId are required"}}, true);
        json members = semantic.value("memberNodeIds", json::array({owner_id}));
        if (!members.is_array() || members.empty() || std::find(members.begin(), members.end(), owner_id) == members.end()) {
            return ToolResult({{"ok", false}, {"error", "owner_missing_from_members"}, {"fieldPath", "semantic.memberNodeIds"}}, true);
        }
        std::set<std::string> member_ids;
        for (const auto& member : members) {
            if (!member.is_string() || !HasNodeId(*scope, member.get<std::string>())) return ToolResult({{"ok", false}, {"error", "member_node_not_found"}, {"memberNodeId", member}, {"editPath", document.value("editPath", json::array())}}, true);
            if (!member_ids.insert(member.get<std::string>()).second) return ToolResult({{"ok", false}, {"error", "duplicate_member"}, {"memberNodeId", member}}, true);
        }
        if (!semantic.value("recipeId", "").empty()) {
            const json recipe = LoadRecipeById(semantic.value("recipeId", ""), false);
            if (!recipe.value("ok", false)) return ToolResult(recipe, true);
        }
        for (const auto& node : scope->value("nodes", json::array())) {
            const std::string node_id = node.value("id", "");
            const json existing = node.value("data", json::object()).value("__semantic", json::object());
            if (node_id != owner_id && existing.value("componentId", "") == component_id) return ToolResult({{"ok", false}, {"error", "duplicate_component_owner"}, {"existingOwnerNodeId", node_id}}, true);
            if (node_id == owner_id || !existing.value("memberNodeIds", json::array()).is_array()) continue;
            for (const auto& existing_member : existing["memberNodeIds"]) if (existing_member.is_string() && member_ids.count(existing_member.get<std::string>())) {
                return ToolResult({{"ok", false}, {"error", "duplicate_member_ownership"}, {"memberNodeId", existing_member}, {"existingOwnerNodeId", node_id}}, true);
            }
        }
        const json queued = QueueGraphCommand({{"type", "applyGraphOps"}, {"operations", json::array({{{"op", "patch_node"}, {"nodeId", owner_id}, {"patch", {{"__semantic", semantic}}}}})}}, arguments.value("ifGraphHash", ""), false, editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_instantiate_library_items") {
        const json document = GetEditorDocument(editor_session_id);
        if (!document.value("ok", false)) return ToolResult(document, true);
        const json built = BuildLibraryInstances(document.value("graph", json::object()), arguments.value("items", json::array()));
        if (!built.value("ok", false)) return ToolResult(built, true);
        if (built.value("changeCount", 0) == 0) return ToolResult({{"ok", true}, {"graphHash", document.value("graphHash", "")}, {"changeCount", 0}, {"skipped", built.value("skipped", json::array())}});
        const json queued = QueueGraphCommand({{"type", "replaceGraph"}, {"graph", built["graph"]}}, arguments.value("ifGraphHash", ""), true, editor_session_id);
        json applied = WaitForAppliedCommand(queued, CommandTimeout(arguments));
        if (!applied.value("isError", false)) {
            applied["structuredContent"]["components"] = built["created"];
            applied["structuredContent"]["changeCount"] = built["changeCount"];
        }
        return applied;
    }
    if (name == "pcg_apply_previs_spec") {
        const json spec = arguments.value("spec", json::object());
        const json document = GetEditorDocument(editor_session_id);
        if (!spec.is_object() || !document.value("ok", false)) return ToolResult(document.value("ok", false) ? json{{"ok", false}, {"error", "spec object is required"}} : document, true);
        json instances = json::array();
        for (const auto* key : {"libraryItems", "instances", "rooms", "props", "characters"}) {
            const json entries = spec.value(key, json::array());
            if (entries.is_array()) for (const auto& entry : entries) instances.push_back(entry);
        }
        json graph = document.value("graph", json::object());
        json built = {{"ok", true}, {"graph", graph}, {"created", json::array()}, {"changeCount", 0}};
        if (!instances.empty()) built = BuildLibraryInstances(graph, instances);
        if (!built.value("ok", false)) return ToolResult(built, true);
        const json requested_shot_operations = spec.value("shotOperations", json::array());
        if (!requested_shot_operations.is_array()) return ToolResult({{"ok", false}, {"error", "spec.shotOperations must be an array"}}, true);
        json shot_operations = json::array();
        if (spec.contains("shot") && spec["shot"].is_object()) {
            json operation = spec["shot"];
            operation["op"] = "set_shot";
            shot_operations.push_back(std::move(operation));
        }
        for (const auto& instance : instances) {
            if (!instance.is_object() || instance.value("instanceId", "").empty()) continue;
            json operation = {{"op", "upsert_component"}, {"componentId", instance.value("instanceId", "")}};
            if (instance.contains("libraryId")) operation["assetId"] = instance["libraryId"];
            json semantic = instance.value("semantic", json::object());
            for (const auto& created : built.value("created", json::array())) if (created.value("componentId", "") == instance.value("instanceId", "")) semantic = created.value("semantic", semantic);
            if (semantic.contains("role")) operation["role"] = semantic["role"];
            if (semantic.contains("bounds")) operation["bounds"] = semantic["bounds"];
            if (semantic.contains("anchors")) operation["anchors"] = semantic["anchors"];
            operation["transform"] = {{"position", Vec3Or(instance.value("position", json::array()), json::array({0, 0, 0}))}, {"rotationEulerDeg", Vec3Or(instance.value("rotation", json::array()), json::array({0, 0, 0}))}, {"scale", Vec3Or(instance.value("scale", json::array()), json::array({1, 1, 1}))}};
            shot_operations.push_back(std::move(operation));
        }
        if (spec.contains("camera") && spec["camera"].is_object()) {
            json operation = spec["camera"];
            operation["op"] = "upsert_camera";
            if (!operation.contains("id")) operation["id"] = "previs_camera";
            shot_operations.push_back(std::move(operation));
        }
        for (const auto& operation : requested_shot_operations) shot_operations.push_back(operation);
        if (shot_operations.empty()) shot_operations.push_back({{"op", "set_shot"}});
        const json queued = QueuePrevisCommand({{"type", "applyPrevisSpec"}, {"graph", built["graph"]}, {"shotOperations", shot_operations}},
            arguments.value("ifGraphHash", ""), arguments.value("ifShotHash", ""), editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_save_project") {
        const json result = SaveProjectFiles(arguments, editor_session_id);
        return ToolResult(result, !result.value("ok", false));
    }
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
        if (arguments.value("detail", "full") == "compact" && result.value("ok", false)) {
            json compact = DescribeScene(result, json::object(), arguments);
            compact["graphPath"] = result.value("graphPath", "");
            compact["editPath"] = result.value("editPath", json::array());
            compact["editorSessionId"] = result.value("editorSessionId", "");
            return ToolResult(compact);
        }
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
            "projection", "focalLengthMm", "fov", "sensorWidthMm", "sensorHeightMm",
            "sensorFit", "shiftX", "shiftY", "apertureFstop", "apertureBlades",
            "apertureRotationDeg", "apertureRatio", "focusDistance", "focusOnTarget",
            "dofEnabled", "exposure", "near", "far", "orthographicScale",
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
    if (name == "pcg_get_shot") {
        const json result = GetEditorShot(editor_session_id);
        if (arguments.value("detail", "full") == "compact" && result.value("ok", false)) {
            const json shot = result.value("shot", json::object());
            return ToolResult({
                {"ok", true}, {"shotHash", result.value("shotHash", "")}, {"graphHash", result.value("graphHash", "")},
                {"graphPath", result.value("graphPath", "")}, {"editorSessionId", result.value("editorSessionId", "")},
                {"shot", {{"name", shot.value("name", "")}, {"durationSeconds", shot.value("durationSeconds", 0.0)},
                    {"fps", shot.value("fps", 24)}, {"width", shot.value("width", 1920)}, {"height", shot.value("height", 1080)},
                    {"activeCameraId", shot.value("activeCameraId", "")}, {"componentCount", shot.value("components", json::array()).size()},
                    {"objectAnimationCount", shot.value("objectAnimations", json::array()).size()}, {"cameraCount", shot.value("cameras", json::array()).size()}}},
            });
        }
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_apply_shot_ops") {
        const json operations = arguments.value("operations", json());
        if (!operations.is_array() || operations.empty() || operations.size() > 100) {
            return ToolResult({{"ok", false}, {"error", "operations must contain 1-100 operations"}}, true);
        }
        const json queued = QueueShotCommand({{"type", "applyShotOps"}, {"operations", operations}},
            arguments.value("ifShotHash", ""), editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_set_camera_keyframes") {
        const json keyframes = arguments.value("keyframes", json());
        if (!keyframes.is_array() || keyframes.size() > 500) {
            return ToolResult({{"ok", false}, {"error", "keyframes must be an array of 0-500 items"}}, true);
        }
        json command = {
            {"type", "setCameraKeyframes"},
            {"mode", arguments.value("mode", "replace")},
            {"keyframes", keyframes},
        };
        if (arguments.contains("cameraId") && arguments["cameraId"].is_string()) {
            command["cameraId"] = arguments["cameraId"];
        }
        if (arguments.contains("seekTimeSeconds") && arguments["seekTimeSeconds"].is_number()) {
            command["seekTimeSeconds"] = arguments["seekTimeSeconds"];
        }
        const json queued = QueueShotCommand(
            std::move(command), arguments.value("ifShotHash", ""), editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_upsert_camera") {
        json camera = json::object();
        if (arguments.contains("id")) camera["id"] = arguments["id"];
        if (arguments.contains("name")) camera["name"] = arguments["name"];
        if (arguments.contains("presetId") && arguments["presetId"].is_string()) {
            camera["presetId"] = arguments["presetId"];
        }
        if (arguments.contains("position")) camera["position"] = arguments["position"];
        if (arguments.contains("camera")) camera["camera"] = arguments["camera"];
        const json queued = QueueShotCommand(
            {{"type", "upsertCamera"}, {"camera", std::move(camera)}},
            arguments.value("ifShotHash", ""), editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_connect_cameras") {
        const json queued = QueueShotCommand(
            {{"type", "connectCameras"},
             {"source", arguments.value("source", "")},
             {"target", arguments.value("target", "")}},
            arguments.value("ifShotHash", ""), editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_select_camera") {
        const json queued = QueueShotCommand(
            {{"type", "selectCamera"}, {"cameraId", arguments.value("cameraId", "")}},
            arguments.value("ifShotHash", ""), editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_export_shot") {
        const json queued = QueueShotCommand({{"type", "exportShot"}}, arguments.value("ifShotHash", ""), editor_session_id);
        const json applied = WaitForAppliedCommand(queued, std::max(1000, std::min(120000, arguments.value("timeoutMs", 120000))));
        return CopyExportedShot(applied, arguments);
    }
    if (name == "pcg_preview_shot") {
        json command = {{"type", "previewShot"}, {"play", arguments.value("play", false)}};
        if (arguments.contains("cameraId") && arguments["cameraId"].is_string()) {
            command["cameraId"] = arguments["cameraId"];
        }
        if (arguments.contains("timeSeconds") && arguments["timeSeconds"].is_number()) {
            command["timeSeconds"] = arguments["timeSeconds"];
        }
        const json queued = QueuePreviewShotCommand(std::move(command), editor_session_id);
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
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
            {"serverInfo", {{"name", "picg-server"}, {"version", "1.0.0"}}},
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

std::string CanonicalPcgToolName(const std::string& name) {
    return name.rfind("picg_", 0) == 0 ? "pcg_" + name.substr(5) : name;
}

json GetPcgToolDefinitions() {
    json definitions = BuildToolDefinitions();
    definitions.push_back({
        {"name", "pcg_apply_shot_ops"},
        {"description", "Atomically edit the live PICG shot. All operations validate before a single undoable commit. Object operations: upsert_component; set_object_keyframes; set_action_clip; set_visibility_range. Camera operations: set_shot; upsert_camera; upsert_motion_curve; set_transform; set_keyframes; connect; disconnect; remove_node; select. Coordinates are meters in Three.js world space; object and camera transforms use Euler degrees. Read picg_get_shot for ifShotHash."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"operations", {{"type", "array"}, {"minItems", 1}, {"maxItems", 100}, {"items", {
                    {"type", "object"}, {"properties", {{"op", {{"type", "string"}, {"enum", json::array({"set_shot", "upsert_component", "set_object_keyframes", "set_action_clip", "set_visibility_range", "upsert_camera", "upsert_motion_curve", "set_transform", "set_keyframes", "connect", "disconnect", "remove_node", "select"})}}}}},
                    {"required", json::array({"op"})}
                }}}},
                {"ifShotHash", {{"type", "string"}}},
                {"editorSessionId", {{"type", "string"}}},
                {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}}
            }},
            {"required", json::array({"operations", "ifShotHash"})}, {"additionalProperties", false}
        }}
    });
    auto& operation = definitions.back()["inputSchema"]["properties"]["operations"]["items"];
    auto& fields = operation["properties"];
    for (const auto* field : {"id", "nodeId", "cameraId", "componentId", "assetId", "clip", "edgeId", "source", "target", "name", "presetId", "role"}) fields[field] = {{"type", "string"}};
    for (const auto* field : {"durationSeconds", "fps", "width", "height", "startSeconds", "playbackRate", "fromSeconds", "untilSeconds"}) fields[field] = {{"type", "number"}};
    const json vector = {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}, {"items", {{"type", "number"}}}};
    fields["translation"] = vector;
    fields["rotationEulerDeg"] = vector;
    fields["controlPoints"] = {{"type", "array"}, {"minItems", 2}, {"maxItems", 500}, {"items", vector}};
    fields["closed"] = {{"type", "boolean"}};
    fields["lookMode"] = {{"type", "string"}, {"enum", json::array({"tangent", "target"})}};
    fields["mode"] = {{"type", "string"}, {"enum", json::array({"replace", "upsert"})}};
    fields["loop"] = {{"type", "boolean"}};
    fields["transform"] = {{"type", "object"}};
    fields["bounds"] = {{"type", "object"}};
    fields["anchors"] = {{"type", "object"}};
    for (const auto& definition : definitions) {
        const auto name = definition.value("name", "");
        if (name == "pcg_set_camera") {
            fields["camera"] = definition["inputSchema"];
            fields["camera"]["properties"].erase("timeoutMs");
            fields["camera"]["properties"].erase("editorSessionId");
            fields["camera"]["properties"]["pathProgress"] = {{"type", "number"}, {"minimum", 0}, {"maximum", 1}};
        }
        if (name == "pcg_set_camera_keyframes") fields["keyframes"] = definition["inputSchema"]["properties"]["keyframes"];
    }
    fields["keyframes"] = {{"type", "array"}, {"maxItems", 500}, {"items", {{"type", "object"}}}};
    operation["additionalProperties"] = false;
    definitions.push_back({
        {"name", "pcg_export_shot"},
        {"description", "Encode the active camera's entire shot at its configured frame rate and resolution, and save a playable MP4 (or WebM fallback) under exports/previs. Returns frameCount, format, workspace-relative path and local video URL. Requires an open Preview and current ifShotHash. Does not modify the shot. Do not repeat after a timeout while the editor still shows encoding."},
        {"inputSchema", {{"type", "object"}, {"properties", {
            {"ifShotHash", {{"type", "string"}}}, {"editorSessionId", {{"type", "string"}}},
            {"outputRootId", {{"type", "string"}}}, {"relativePath", {{"type", "string"}}},
            {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 120000}, {"default", 120000}}}
        }}, {"required", json::array({"ifShotHash"})}, {"additionalProperties", false}}}
    });
    const json read_options = {
        {"detail", {{"type", "string"}, {"enum", json::array({"compact", "full"})}, {"default", "compact"}}},
        {"sinceRevision", {{"oneOf", json::array({{{"type", "string"}}, {{"type", "integer"}}})}}},
        {"fields", {{"type", "array"}, {"items", {{"type", "string"}}}}},
        {"editorSessionId", {{"type", "string"}}},
    };
    const json transform_vector = {{"type", "array"}, {"minItems", 3}, {"maxItems", 3}, {"items", {{"type", "number"}}}};
    const json library_request = {
        {"type", "object"},
        {"properties", {
            {"libraryId", {{"type", "string"}}}, {"name", {{"type", "string"}}}, {"instanceId", {{"type", "string"}}},
            {"parameters", {{"type", "object"}}}, {"position", transform_vector}, {"rotation", transform_vector}, {"scale", transform_vector},
            {"semantic", {{"type", "object"}}}, {"layoutX", {{"type", "number"}}}, {"layoutY", {{"type", "number"}}},
        }},
        {"required", json::array({"instanceId"})}, {"additionalProperties", false},
    };
    definitions.push_back({
        {"name", "pcg_list_editor_sessions"},
        {"description", "List online PICG editor pages and the persistent page binding."},
        {"inputSchema", {{"type", "object"}, {"properties", json::object()}, {"additionalProperties", false}}},
    });
    definitions.push_back({
        {"name", "pcg_bind_editor_session"},
        {"description", "Bind future MCP calls without editorSessionId to one online editor page. Pass an empty id to clear."},
        {"inputSchema", {{"type", "object"}, {"properties", {{"editorSessionId", {{"type", "string"}}}}}, {"required", json::array({"editorSessionId"})}, {"additionalProperties", false}}},
    });
    definitions.push_back({
        {"name", "pcg_list_library_items"},
        {"description", "Query reusable PICG Library assets without loading their complete Subgraph documents."},
        {"inputSchema", {{"type", "object"}, {"properties", {
            {"category", {{"type", "string"}}}, {"query", {{"type", "string"}}}, {"role", {{"type", "string"}}},
            {"detail", read_options["detail"]}, {"sinceRevision", read_options["sinceRevision"]}, {"fields", read_options["fields"]},
        }}, {"additionalProperties", false}}},
    });
    definitions.push_back({
        {"name", "pcg_instantiate_library_items"},
        {"description", "Atomically instantiate 1-100 Library assets, deduplicate definitions, create final transforms and attach unique scene semantics. Repeated stable instanceIds are idempotent."},
        {"inputSchema", {{"type", "object"}, {"properties", {
            {"items", {{"type", "array"}, {"minItems", 1}, {"maxItems", 100}, {"items", library_request}}},
            {"ifGraphHash", {{"type", "string"}}}, {"editorSessionId", {{"type", "string"}}},
            {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
        }}, {"required", json::array({"items", "ifGraphHash"})}, {"additionalProperties", false}}},
    });
    definitions.push_back({
        {"name", "pcg_describe_scene"},
        {"description", "Return a compact semantic component tree, transforms, bounds, recipes, dependencies and unclassified count; no full graph or Wiki prose."},
        {"inputSchema", {{"type", "object"}, {"properties", read_options}, {"additionalProperties", false}}},
    });
    json component_properties = read_options;
    component_properties["componentId"] = {{"type", "string"}};
    component_properties["includeMembers"] = {{"type", "boolean"}, {"default", false}};
    component_properties["includeConnections"] = {{"type", "boolean"}, {"default", false}};
    component_properties["includeRecipe"] = {{"type", "boolean"}, {"default", false}};
    definitions.push_back({
        {"name", "pcg_get_component"},
        {"description", "Expand one semantic scene component by stable componentId without returning the entire Graph."},
        {"inputSchema", {{"type", "object"}, {"properties", component_properties}, {"required", json::array({"componentId"})}, {"additionalProperties", false}}},
    });
    definitions.push_back({
        {"name", "pcg_get_recipe"},
        {"description", "Read an exact shared semantic Recipe. Compact returns metadata, summary and content hash; full includes the body."},
        {"inputSchema", {{"type", "object"}, {"properties", {{"recipeId", {{"type", "string"}}}, {"expectedVersion", {{"type", "integer"}, {"minimum", 1}}}, {"detail", read_options["detail"]}, {"sinceRevision", read_options["sinceRevision"]}}}, {"required", json::array({"recipeId"})}, {"additionalProperties", false}}},
    });
    definitions.push_back({
        {"name", "pcg_upsert_component_semantics"},
        {"description", "Validate and atomically attach complete component semantics to one owner node in the current Graph or Subgraph scope."},
        {"inputSchema", {{"type", "object"}, {"properties", {
            {"ownerNodeId", {{"type", "string"}}}, {"semantic", {{"type", "object"}}}, {"ifGraphHash", {{"type", "string"}}},
            {"editorSessionId", {{"type", "string"}}}, {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
        }}, {"required", json::array({"ownerNodeId", "semantic", "ifGraphHash"})}, {"additionalProperties", false}}},
    });
    definitions.push_back({
        {"name", "pcg_apply_previs_spec"},
        {"description", "Atomically apply a high-level previs spec across Graph and Shot. Uses Library instance declarations plus shot operations; stable ids make repeated submits idempotent."},
        {"inputSchema", {{"type", "object"}, {"properties", {
            {"spec", {{"type", "object"}, {"properties", {
                {"shot", {{"type", "object"}}}, {"camera", {{"type", "object"}}},
                {"libraryItems", {{"type", "array"}, {"items", library_request}}}, {"instances", {{"type", "array"}, {"items", library_request}}},
                {"rooms", {{"type", "array"}, {"items", library_request}}}, {"props", {{"type", "array"}, {"items", library_request}}},
                {"characters", {{"type", "array"}, {"items", library_request}}},
                {"shotOperations", {{"type", "array"}, {"maxItems", 100}, {"items", {{"type", "object"}}}}},
            }}, {"additionalProperties", false}}}, {"ifGraphHash", {{"type", "string"}}}, {"ifShotHash", {{"type", "string"}}},
            {"editorSessionId", {{"type", "string"}}}, {"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}},
        }}, {"required", json::array({"spec", "ifGraphHash", "ifShotHash"})}, {"additionalProperties", false}}},
    });
    definitions.push_back({
        {"name", "pcg_save_project"},
        {"description", "Save matching .picg, .picgshot and .picgproject files beneath a configured allowlisted output root; absolute paths and traversal are rejected."},
        {"inputSchema", {{"type", "object"}, {"properties", {
            {"outputRootId", {{"type", "string"}}}, {"relativePath", {{"type", "string"}}},
            {"ifGraphHash", {{"type", "string"}}}, {"ifShotHash", {{"type", "string"}}}, {"editorSessionId", {{"type", "string"}}},
        }}, {"required", json::array({"outputRootId", "relativePath", "ifGraphHash", "ifShotHash"})}, {"additionalProperties", false}}},
    });
    for (auto& definition : definitions) {
        const std::string tool_name = definition.value("name", "");
        if (tool_name != "pcg_get_graph" && tool_name != "pcg_get_shot") continue;
        auto& properties = definition["inputSchema"]["properties"];
        properties["detail"] = read_options["detail"];
        properties["sinceRevision"] = read_options["sinceRevision"];
        properties["fields"] = read_options["fields"];
    }
    // Advertise PICG names while preserving every existing pcg_* caller.
    for (auto& definition : definitions) {
        const std::string name = definition.value("name", "");
        if (name.rfind("pcg_", 0) == 0) definition["name"] = "picg_" + name.substr(4);
        std::string description = definition.value("description", "");
        size_t at = 0;
        while ((at = description.find("pcg_", at)) != std::string::npos) {
            description.replace(at, 4, "picg_"); at += 5;
        }
        definition["description"] = description;
    }
    return definitions;
}

json CallPcgTool(
    const std::string& name,
    const json& arguments,
    const std::string& editor_session_id) {
    json adjusted = arguments;
    if (name.rfind("picg_", 0) == 0 && !adjusted.contains("detail")) {
        const std::string canonical = CanonicalPcgToolName(name);
        if (canonical == "pcg_get_graph" || canonical == "pcg_get_shot" || canonical == "pcg_describe_scene" ||
            canonical == "pcg_get_component" || canonical == "pcg_get_recipe" || canonical == "pcg_list_library_items") {
            adjusted["detail"] = "compact";
        }
    }
    return CallToolInternal(CanonicalPcgToolName(name), adjusted, editor_session_id);
}

bool PcgToolRequiresApproval(const std::string& name) {
    return PcgToolMutatesGraph(name) || PcgToolMutatesShot(name) || CanonicalPcgToolName(name) == "pcg_export_shot";
}

bool PcgToolMutatesGraph(const std::string& requested_name) {
    const auto name = CanonicalPcgToolName(requested_name);
    return name == "pcg_patch_node" ||
           name == "pcg_bake_oriented_sdf" ||
           name == "pcg_apply_graph_ops" ||
           name == "pcg_replace_graph" ||
           name == "pcg_instantiate_library_items" ||
           name == "pcg_upsert_component_semantics" ||
           name == "pcg_apply_previs_spec" ||
           name == "pcg_save_project" ||
           name == "pcg_save_graph";
}

bool PcgToolMutatesShot(const std::string& requested_name) {
    const auto name = CanonicalPcgToolName(requested_name);
    return name == "pcg_apply_shot_ops" || name == "pcg_apply_previs_spec" || name == "pcg_set_camera_keyframes" ||
           name == "pcg_upsert_camera" ||
           name == "pcg_connect_cameras" ||
           name == "pcg_select_camera";
}

void HandleMcpPost(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
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
    if (!CheckAgentAuth(req, res)) return;
    res.status = 405;
    res.set_header("Allow", "POST");
    res.set_content(
        R"({"jsonrpc":"2.0","error":{"code":-32000,"message":"This stateless Streamable HTTP endpoint accepts MCP messages via POST; SSE responses are negotiated with Accept: text/event-stream."},"id":null})",
        "application/json");
}

}  // namespace pcg_server
