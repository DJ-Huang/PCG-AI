#include "session_service.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <iterator>
#include <mutex>
#include <unordered_map>

#include "agent_service.hpp"

namespace pcg_server {
namespace {

using json = nlohmann::json;
constexpr auto kEditorOfflineAfter = std::chrono::seconds(15);
constexpr auto kEditorRetention = std::chrono::minutes(5);

struct EditorState {
    json session = json::object();
    std::chrono::steady_clock::time_point last_seen{};
    int64_t session_revision = 0;
    uint64_t capture_request_id = 0;
    PreviewSnapshot preview;
    std::deque<json> patches;
};

struct BridgeState {
    std::mutex mutex;
    std::condition_variable preview_changed;
    std::unordered_map<std::string, EditorState> editors;
    uint64_t next_patch_id = 1;
    std::condition_variable command_changed;
    std::unordered_map<uint64_t, json> command_results;
    std::deque<uint64_t> command_result_order;
};

BridgeState& State() {
    static BridgeState state;
    return state;
}

int64_t EpochMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool IsOnlineLocked(const EditorState& editor) {
    return !editor.session.empty() &&
           std::chrono::steady_clock::now() - editor.last_seen <= kEditorOfflineAfter;
}

json EditorChoicesLocked(const BridgeState& state) {
    json choices = json::array();
    for (const auto& [id, editor] : state.editors) {
        if (!IsOnlineLocked(editor)) continue;
        const json graph = editor.session.value("graph", json::object());
        choices.push_back({
            {"editorSessionId", id},
            {"graphPath", editor.session.value("graphPath", "")},
            {"graphHash", editor.session.value("graphHash", "")},
            {"nodeCount", graph.value("nodes", json::array()).size()},
            {"updatedAt", editor.session.value("updatedAt", 0ll)},
        });
    }
    return choices;
}

json EditorSelectionErrorLocked(const BridgeState& state, const std::string& requested_id) {
    const json choices = EditorChoicesLocked(state);
    if (!requested_id.empty()) {
        return {
            {"ok", false}, {"error", "editor_session_unavailable"},
            {"editorSessionId", requested_id}, {"editors", choices},
        };
    }
    if (choices.empty()) return {{"ok", false}, {"error", "editor_offline"}, {"editors", choices}};
    return {
        {"ok", false}, {"error", "editor_session_required"},
        {"message", "Multiple Web editor pages are online. Ask the user which page to use, then pass editorSessionId."},
        {"editors", choices},
    };
}

EditorState* ResolveEditorLocked(BridgeState& state, const std::string& requested_id) {
    if (!requested_id.empty()) {
        const auto it = state.editors.find(requested_id);
        return it != state.editors.end() && IsOnlineLocked(it->second) ? &it->second : nullptr;
    }
    EditorState* resolved = nullptr;
    for (auto& [_, editor] : state.editors) {
        if (!IsOnlineLocked(editor)) continue;
        if (resolved != nullptr) return nullptr;
        resolved = &editor;
    }
    return resolved;
}

const EditorState* ResolveEditorLocked(const BridgeState& state, const std::string& requested_id) {
    return ResolveEditorLocked(const_cast<BridgeState&>(state), requested_id);
}

void JsonResponse(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

bool ParseObjectBody(const httplib::Request& req, httplib::Response& res, json& body) {
    body = json::parse(req.body, nullptr, false);
    if (!body.is_object()) {
        JsonResponse(res, 400, {{"ok", false}, {"error", "expected a JSON object body"}});
        return false;
    }
    return true;
}

int Base64Value(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

bool DecodeBase64(const std::string& input, std::vector<uint8_t>& output) {
    output.clear();
    uint32_t value = 0;
    int count = 0;
    for (const unsigned char c : input) {
        if (c == '=') break;
        const int decoded = Base64Value(c);
        if (decoded < 0) return false;
        value = (value << 6) | static_cast<uint32_t>(decoded);
        if (++count == 4) {
            output.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
            output.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
            output.push_back(static_cast<uint8_t>(value & 0xff));
            value = 0;
            count = 0;
        }
    }
    if (count == 2) {
        output.push_back(static_cast<uint8_t>((value >> 4) & 0xff));
    } else if (count == 3) {
        output.push_back(static_cast<uint8_t>((value >> 10) & 0xff));
        output.push_back(static_cast<uint8_t>((value >> 2) & 0xff));
    } else if (count != 0) {
        return false;
    }
    return true;
}

json* ResolveCurrentGraph(json& session) {
    if (!session.contains("graph") || !session["graph"].is_object()) return nullptr;
    json* graph = &session["graph"];
    if (!session.contains("editPath") || !session["editPath"].is_array() ||
        session["editPath"].empty()) {
        return graph;
    }
    const auto& tail = session["editPath"].back();
    if (!tail.is_string() || !graph->contains("subgraphs") || !(*graph)["subgraphs"].is_array()) {
        return nullptr;
    }
    const std::string id = tail.get<std::string>();
    for (auto& subgraph : (*graph)["subgraphs"]) {
        if (subgraph.is_object() && subgraph.value("id", "") == id) return &subgraph;
    }
    return nullptr;
}

const json* ResolveCurrentGraph(const json& session) {
    return ResolveCurrentGraph(const_cast<json&>(session));
}

const json* FindCurrentNode(const json& session, const std::string& node_id) {
    const json* graph = ResolveCurrentGraph(session);
    if (graph == nullptr || !graph->contains("nodes") || !(*graph)["nodes"].is_array()) return nullptr;
    for (const auto& node : (*graph)["nodes"]) {
        if (node.is_object() && node.value("id", "") == node_id) return &node;
    }
    return nullptr;
}

json QueueGraphCommandLocked(
    BridgeState& state,
    EditorState& editor,
    json command,
    const std::string& expected_graph_hash,
    bool require_root_scope) {
    if (!IsOnlineLocked(editor)) {
        return {{"ok", false}, {"status", 409}, {"error", "editor_offline"}};
    }
    const std::string current_hash = editor.session.value("graphHash", "");
    if (expected_graph_hash.empty()) {
        return {{"ok", false}, {"status", 428}, {"error", "ifGraphHash is required"}};
    }
    if (expected_graph_hash != current_hash) {
        return {
            {"ok", false}, {"status", 409}, {"error", "graph_conflict"},
            {"expectedGraphHash", expected_graph_hash}, {"currentGraphHash", current_hash},
        };
    }
    const json edit_path = editor.session.value("editPath", json::array());
    if (require_root_scope && edit_path.is_array() && !edit_path.empty()) {
        return {{"ok", false}, {"status", 409}, {"error", "root_scope_required"}, {"editPath", edit_path}};
    }
    command["id"] = state.next_patch_id++;
    command["baseGraphHash"] = current_hash;
    command["editPath"] = edit_path;
    command["createdAt"] = EpochMillis();
    command["editorSessionId"] = editor.session.value("sessionId", "");
    editor.patches.push_back(command);
    return {{"ok", true}, {"accepted", true}, {"command", std::move(command)}};
}

json ContextLocked(const BridgeState& state, const std::string& editor_session_id) {
    const EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    if (editor == nullptr) return EditorSelectionErrorLocked(state, editor_session_id);
    json session = editor->session;
    session.erase("graph");
    if (session.contains("nodeManifest") && session["nodeManifest"].is_object()) {
        session["nodeManifestVersion"] = session["nodeManifest"].value("version", "");
    }
    session.erase("nodeManifest");
    return {
        {"ok", true},
        {"online", IsOnlineLocked(*editor)},
        {"session", std::move(session)},
        {"sessionRevision", editor->session_revision},
        {"captureRequestId", editor->capture_request_id},
        {"previewRequestId", editor->preview.request_id},
        {"pendingPatchCount", editor->patches.size()},
        {"serverTime", EpochMillis()},
    };
}

}  // namespace

void HandlePutSession(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body;
    if (!ParseObjectBody(req, res, body)) return;
    if (!body.contains("graph") || !body["graph"].is_object() ||
        !body.contains("graphHash") || !body["graphHash"].is_string()) {
        JsonResponse(res, 400, {{"ok", false}, {"error", "session requires graph and graphHash"}});
        return;
    }
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto now = std::chrono::steady_clock::now();
    for (auto it = state.editors.begin(); it != state.editors.end();) {
        it = now - it->second.last_seen > kEditorRetention ? state.editors.erase(it) : std::next(it);
    }
    const std::string incoming_id = body.value("sessionId", "");
    if (incoming_id.empty()) {
        JsonResponse(res, 400, {{"ok", false}, {"error", "sessionId is required"}});
        return;
    }
    auto& editor = state.editors[incoming_id];
    const int64_t incoming_revision = body.value("clientRevision", 0ll);
    if (!editor.session.empty() && incoming_revision > 0 &&
        incoming_revision <= editor.session.value("clientRevision", 0ll)) {
        JsonResponse(res, 409, {
            {"ok", false}, {"error", "stale_session_update"},
            {"clientRevision", incoming_revision},
            {"currentClientRevision", editor.session.value("clientRevision", 0ll)},
        });
        return;
    }
    body["receivedAt"] = EpochMillis();
    editor.session = std::move(body);
    editor.last_seen = now;
    ++editor.session_revision;
    JsonResponse(res, 200, ContextLocked(state, incoming_id));
}

void HandleGetSession(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string editor_session_id = req.has_param("sessionId") ? req.get_param_value("sessionId") : "";
    const json context = GetEditorContext(editor_session_id);
    JsonResponse(res, context.value("ok", false) ? 200 : 409, context);
}

void HandleSessionHeartbeat(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body;
    if (!ParseObjectBody(req, res, body)) return;
    const std::string session_id = body.value("sessionId", "");
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const auto it = state.editors.find(session_id);
    if (session_id.empty() || it == state.editors.end()) {
        JsonResponse(res, 409, {{"ok", false}, {"error", "session_missing"}});
        return;
    }
    it->second.last_seen = std::chrono::steady_clock::now();
    JsonResponse(res, 200, {{"ok", true}, {"serverTime", EpochMillis()}});
}

void HandlePutPreviewScreenshot(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body;
    if (!ParseObjectBody(req, res, body)) return;
    if (!body.contains("requestId") || !body["requestId"].is_number_unsigned() ||
        !body.contains("pngBase64") || !body["pngBase64"].is_string()) {
        JsonResponse(res, 400, {{"ok", false}, {"error", "requestId and pngBase64 are required"}});
        return;
    }
    std::vector<uint8_t> png;
    if (!DecodeBase64(body["pngBase64"].get<std::string>(), png) || png.size() < 8 ||
        png[0] != 0x89 || png[1] != 'P' || png[2] != 'N' || png[3] != 'G') {
        JsonResponse(res, 400, {{"ok", false}, {"error", "invalid PNG base64 payload"}});
        return;
    }
    auto& state = State();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        const std::string editor_session_id = body.value("sessionId", "");
        EditorState* editor = ResolveEditorLocked(state, editor_session_id);
        if (editor == nullptr) {
            JsonResponse(res, 409, EditorSelectionErrorLocked(state, editor_session_id));
            return;
        }
        editor->preview.request_id = body["requestId"].get<uint64_t>();
        editor->preview.png = std::move(png);
        editor->preview.metadata = body.value("metadata", json::object());
        editor->preview.captured_at = EpochMillis();
    }
    state.preview_changed.notify_all();
    JsonResponse(res, 200, {{"ok", true}, {"requestId", body["requestId"]}});
}

void HandleGetPreviewScreenshot(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    PreviewSnapshot snapshot;
    auto& state = State();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        const std::string editor_session_id = req.has_param("sessionId") ? req.get_param_value("sessionId") : "";
        const EditorState* editor = ResolveEditorLocked(state, editor_session_id);
        if (editor == nullptr) {
            JsonResponse(res, 409, EditorSelectionErrorLocked(state, editor_session_id));
            return;
        }
        snapshot = editor->preview;
    }
    if (snapshot.png.empty()) {
        JsonResponse(res, 404, {{"ok", false}, {"error", "preview_unavailable"}});
        return;
    }
    res.status = 200;
    res.set_header("X-PCG-Capture-Request-Id", std::to_string(snapshot.request_id));
    res.set_content(
        std::string(reinterpret_cast<const char*>(snapshot.png.data()), snapshot.png.size()),
        "image/png");
}

void HandleGetPreviewMetadata(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const std::string editor_session_id = req.has_param("sessionId") ? req.get_param_value("sessionId") : "";
    const EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    if (editor == nullptr) {
        JsonResponse(res, 409, EditorSelectionErrorLocked(state, editor_session_id));
        return;
    }
    if (editor->preview.png.empty()) {
        JsonResponse(res, 404, {{"ok", false}, {"error", "preview_unavailable"}});
        return;
    }
    JsonResponse(res, 200, {
        {"ok", true},
        {"requestId", editor->preview.request_id},
        {"capturedAt", editor->preview.captured_at},
        {"bytes", editor->preview.png.size()},
        {"metadata", editor->preview.metadata},
    });
}

void HandleRequestPreviewCapture(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body = json::object();
    if (!req.body.empty() && !ParseObjectBody(req, res, body)) return;
    const auto id = RequestPreviewCapture(body.value("sessionId", ""));
    if (id == 0) {
        JsonResponse(res, 409, GetEditorContext(body.value("sessionId", "")));
        return;
    }
    JsonResponse(res, 202, {{"ok", true}, {"requestId", id}});
}

void HandlePatchNode(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body;
    if (!ParseObjectBody(req, res, body)) return;
    if (!body.contains("patch") || !body["patch"].is_object()) {
        JsonResponse(res, 400, {{"ok", false}, {"error", "object patch is required"}});
        return;
    }
    const std::string node_id = req.matches.size() > 1 ? req.matches[1].str() : "";
    const json patch = body["patch"];
    const std::string expected = body.value("ifGraphHash", "");
    const json result = QueueNodePatch(node_id, patch, expected, body.value("editorSessionId", ""));
    JsonResponse(res, result.value("ok", false) ? 202 : result.value("status", 400), result);
}

void HandleGetGraphPatches(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    uint64_t after = 0;
    try {
        if (req.has_param("after")) after = std::stoull(req.get_param_value("after"));
    } catch (...) {
        JsonResponse(res, 400, {{"ok", false}, {"error", "invalid after cursor"}});
        return;
    }
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const std::string editor_session_id = req.has_param("sessionId") ? req.get_param_value("sessionId") : "";
    EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    if (editor == nullptr) {
        JsonResponse(res, 409, EditorSelectionErrorLocked(state, editor_session_id));
        return;
    }
    json patches = json::array();
    for (const auto& patch : editor->patches) {
        if (patch.value("id", 0ull) > after) patches.push_back(patch);
    }
    JsonResponse(res, 200, {{"ok", true}, {"patches", patches}});
}

void HandleAckGraphPatches(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body;
    if (!ParseObjectBody(req, res, body)) return;
    if (!body.contains("ids") || !body["ids"].is_array()) {
        JsonResponse(res, 400, {{"ok", false}, {"error", "ids array is required"}});
        return;
    }
    std::vector<uint64_t> ids;
    for (const auto& id : body["ids"]) {
        if (id.is_number_unsigned()) ids.push_back(id.get<uint64_t>());
    }
    if (ids.empty()) {
        JsonResponse(res, 400, {{"ok", false}, {"error", "ids must contain at least one command id"}});
        return;
    }
    std::sort(ids.begin(), ids.end());
    if (std::adjacent_find(ids.begin(), ids.end()) != ids.end()) {
        JsonResponse(res, 400, {{"ok", false}, {"error", "duplicate command ids are not allowed"}});
        return;
    }
    auto& state = State();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        const std::string editor_session_id = body.value("sessionId", "");
        EditorState* editor = ResolveEditorLocked(state, editor_session_id);
        if (editor == nullptr) {
            JsonResponse(res, 409, EditorSelectionErrorLocked(state, editor_session_id));
            return;
        }
        json unknown_ids = json::array();
        for (const uint64_t id : ids) {
            const bool pending = std::any_of(
                editor->patches.begin(), editor->patches.end(), [&](const json& command) {
                    return command.value("id", 0ull) == id;
                });
            if (!pending) unknown_ids.push_back(id);
        }
        if (!unknown_ids.empty()) {
            JsonResponse(res, 409, {
                {"ok", false}, {"error", "unknown_command_ids"},
                {"unknownIds", std::move(unknown_ids)},
            });
            return;
        }
        const json results = body.value("results", json::array());
        if (!results.is_array()) {
            JsonResponse(res, 400, {{"ok", false}, {"error", "results must be an array"}});
            return;
        }
        for (const auto& candidate : results) {
            const uint64_t result_id = candidate.is_object() ? candidate.value("id", 0ull) : 0ull;
            if (result_id == 0 || !std::binary_search(ids.begin(), ids.end(), result_id)) {
                JsonResponse(res, 400, {{"ok", false}, {"error", "result id must match an acknowledged command id"}});
                return;
            }
        }
        for (const uint64_t id : ids) {
            json result = {{"id", id}, {"ok", true}};
            for (const auto& candidate : results) {
                if (candidate.value("id", 0ull) == id) {
                    result = candidate;
                    break;
                }
            }
            state.command_results[id] = std::move(result);
            state.command_result_order.push_back(id);
        }
        while (state.command_result_order.size() > 256) {
            const uint64_t expired = state.command_result_order.front();
            state.command_result_order.pop_front();
            state.command_results.erase(expired);
        }
        editor->patches.erase(
            std::remove_if(editor->patches.begin(), editor->patches.end(), [&](const json& patch) {
                return std::find(ids.begin(), ids.end(), patch.value("id", 0ull)) != ids.end();
            }),
            editor->patches.end());
    }
    state.command_changed.notify_all();
    JsonResponse(res, 200, {{"ok", true}, {"acknowledged", ids.size()}});
}

json GetEditorContext(const std::string& editor_session_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return ContextLocked(state, editor_session_id);
}

json GetEditorNode(const std::string& node_id, const std::string& editor_session_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    if (editor == nullptr) return EditorSelectionErrorLocked(state, editor_session_id);
    const json* node = FindCurrentNode(editor->session, node_id);
    if (node == nullptr) return {{"ok", false}, {"error", "node_not_found"}, {"nodeId", node_id}};
    return {{"ok", true}, {"node", *node}, {"graphHash", editor->session.value("graphHash", "")}};
}

json ListEditorNodes(const std::string& editor_session_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    if (editor == nullptr) return EditorSelectionErrorLocked(state, editor_session_id);
    const json* graph = ResolveCurrentGraph(editor->session);
    if (graph == nullptr || !graph->contains("nodes")) return {{"ok", false}, {"error", "graph_unavailable"}};
    return {{"ok", true}, {"nodes", (*graph)["nodes"]}, {"graphHash", editor->session.value("graphHash", "")}};
}

json GetEditorNodeTypes(
    const std::string& node_type,
    const std::string& category,
    const std::string& editor_session_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    if (editor == nullptr) return EditorSelectionErrorLocked(state, editor_session_id);
    const json manifest = editor->session.value("nodeManifest", json());
    if (!manifest.is_object() || !manifest.contains("nodes") || !manifest["nodes"].is_array()) {
        return {{"ok", false}, {"error", "manifest_unavailable"}};
    }
    json nodes = json::array();
    for (const auto& definition : manifest["nodes"]) {
        if (!definition.is_object()) continue;
        if (!node_type.empty() && definition.value("type", "") != node_type) continue;
        if (!category.empty() && definition.value("category", "") != category) continue;
        nodes.push_back(definition);
    }
    const size_t count = nodes.size();
    return {
        {"ok", true}, {"version", manifest.value("version", "")},
        {"nodes", std::move(nodes)}, {"count", count},
    };
}

json GetEditorDocument(const std::string& editor_session_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    if (editor == nullptr) return EditorSelectionErrorLocked(state, editor_session_id);
    return {
        {"ok", true}, {"graph", editor->session.value("graph", json())},
        {"graphHash", editor->session.value("graphHash", "")},
        {"graphPath", editor->session.value("graphPath", "")},
        {"editPath", editor->session.value("editPath", json::array())},
        {"editorSessionId", editor->session.value("sessionId", "")},
    };
}

json QueueNodePatch(
    const std::string& node_id,
    const json& patch,
    const std::string& expected_graph_hash,
    const std::string& editor_session_id) {
    if (node_id.empty() || !patch.is_object()) {
        return {{"ok", false}, {"status", 400}, {"error", "nodeId and object patch are required"}};
    }
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    if (editor == nullptr) {
        json error = EditorSelectionErrorLocked(state, editor_session_id);
        error["status"] = 409;
        return error;
    }
    if (FindCurrentNode(editor->session, node_id) == nullptr) {
        return {{"ok", false}, {"status", 404}, {"error", "node_not_found"}, {"nodeId", node_id}};
    }
    json result = QueueGraphCommandLocked(
        state,
        *editor,
        {{"type", "setNodeParams"}, {"nodeId", node_id}, {"patch", patch}},
        expected_graph_hash,
        false);
    if (result.contains("command")) result["patch"] = result["command"];
    return result;
}

json QueueGraphCommand(
    json command,
    const std::string& expected_graph_hash,
    bool require_root_scope,
    const std::string& editor_session_id) {
    if (!command.is_object() || !command.contains("type") || !command["type"].is_string()) {
        return {{"ok", false}, {"status", 400}, {"error", "command type is required"}};
    }
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    if (editor == nullptr) {
        json error = EditorSelectionErrorLocked(state, editor_session_id);
        error["status"] = 409;
        return error;
    }
    return QueueGraphCommandLocked(state, *editor, std::move(command), expected_graph_hash, require_root_scope);
}

bool WaitForGraphCommandResult(
    uint64_t command_id,
    std::chrono::milliseconds timeout,
    json& result) {
    auto& state = State();
    std::unique_lock<std::mutex> lock(state.mutex);
    const bool ready = state.command_changed.wait_for(lock, timeout, [&] {
        return state.command_results.find(command_id) != state.command_results.end();
    });
    if (!ready) return false;
    result = state.command_results[command_id];
    state.command_results.erase(command_id);
    state.command_result_order.erase(
        std::remove(state.command_result_order.begin(), state.command_result_order.end(), command_id),
        state.command_result_order.end());
    return true;
}

bool CancelGraphCommand(uint64_t command_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    for (auto& [_, editor] : state.editors) {
        const auto before = editor.patches.size();
        editor.patches.erase(
            std::remove_if(editor.patches.begin(), editor.patches.end(), [&](const json& command) {
                return command.value("id", 0ull) == command_id;
            }),
            editor.patches.end());
        if (editor.patches.size() != before) return true;
    }
    return false;
}

json GetEditorGraph(const std::string& editor_session_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    return editor == nullptr ? json() : editor->session.value("graph", json());
}

uint64_t RequestPreviewCapture(const std::string& editor_session_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    EditorState* editor = ResolveEditorLocked(state, editor_session_id);
    return editor == nullptr ? 0 : ++editor->capture_request_id;
}

bool WaitForPreview(
    uint64_t request_id,
    std::chrono::milliseconds timeout,
    PreviewSnapshot& snapshot,
    const std::string& editor_session_id) {
    auto& state = State();
    std::unique_lock<std::mutex> lock(state.mutex);
    const bool ready = state.preview_changed.wait_for(lock, timeout, [&] {
        const EditorState* editor = ResolveEditorLocked(state, editor_session_id);
        return editor != nullptr && editor->preview.request_id >= request_id && !editor->preview.png.empty();
    });
    if (ready) snapshot = ResolveEditorLocked(state, editor_session_id)->preview;
    return ready;
}

json GetBridgeHealth() {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const json editors = EditorChoicesLocked(state);
    size_t pending_patch_count = 0;
    uint64_t capture_request_id = 0;
    uint64_t preview_request_id = 0;
    int64_t session_revision = 0;
    for (const auto& [_, editor] : state.editors) {
        if (!IsOnlineLocked(editor)) continue;
        pending_patch_count += editor.patches.size();
        capture_request_id = std::max(capture_request_id, editor.capture_request_id);
        preview_request_id = std::max(preview_request_id, editor.preview.request_id);
        session_revision = std::max(session_revision, editor.session_revision);
    }
    return {
        {"editorOnline", !editors.empty()}, {"editorCount", editors.size()}, {"editors", editors},
        {"sessionRevision", session_revision},
        {"captureRequestId", capture_request_id}, {"previewRequestId", preview_request_id},
        {"pendingPatchCount", pending_patch_count},
    };
}

}  // namespace pcg_server
