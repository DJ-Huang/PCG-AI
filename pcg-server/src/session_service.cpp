#include "session_service.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>

#include "agent_service.hpp"

namespace pcg_server {
namespace {

using json = nlohmann::json;
constexpr auto kEditorOfflineAfter = std::chrono::seconds(15);

struct BridgeState {
    std::mutex mutex;
    std::condition_variable preview_changed;
    json session = json::object();
    bool has_session = false;
    std::chrono::steady_clock::time_point last_seen{};
    int64_t session_revision = 0;
    uint64_t capture_request_id = 0;
    PreviewSnapshot preview;
    uint64_t next_patch_id = 1;
    std::deque<json> patches;
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

bool IsOnlineLocked(const BridgeState& state) {
    return state.has_session &&
           std::chrono::steady_clock::now() - state.last_seen <= kEditorOfflineAfter;
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

json ContextLocked(const BridgeState& state) {
    if (!state.has_session) {
        return {{"ok", true}, {"online", false}, {"error", "editor_offline"}};
    }
    json session = state.session;
    session.erase("graph");
    return {
        {"ok", true},
        {"online", IsOnlineLocked(state)},
        {"session", std::move(session)},
        {"sessionRevision", state.session_revision},
        {"captureRequestId", state.capture_request_id},
        {"previewRequestId", state.preview.request_id},
        {"pendingPatchCount", state.patches.size()},
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
    const std::string incoming_id = body.value("sessionId", "");
    const int64_t incoming_revision = body.value("clientRevision", 0ll);
    if (state.has_session && !incoming_id.empty() &&
        incoming_id == state.session.value("sessionId", "") &&
        incoming_revision > 0 &&
        incoming_revision <= state.session.value("clientRevision", 0ll)) {
        JsonResponse(res, 409, {
            {"ok", false}, {"error", "stale_session_update"},
            {"clientRevision", incoming_revision},
            {"currentClientRevision", state.session.value("clientRevision", 0ll)},
        });
        return;
    }
    body["receivedAt"] = EpochMillis();
    state.session = std::move(body);
    state.has_session = true;
    state.last_seen = std::chrono::steady_clock::now();
    ++state.session_revision;
    JsonResponse(res, 200, ContextLocked(state));
}

void HandleGetSession(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    JsonResponse(res, 200, GetEditorContext());
}

void HandleSessionHeartbeat(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body;
    if (!ParseObjectBody(req, res, body)) return;
    const std::string session_id = body.value("sessionId", "");
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.has_session || session_id.empty() || session_id != state.session.value("sessionId", "")) {
        JsonResponse(res, 409, {{"ok", false}, {"error", "session_replaced"}});
        return;
    }
    state.last_seen = std::chrono::steady_clock::now();
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
        state.preview.request_id = body["requestId"].get<uint64_t>();
        state.preview.png = std::move(png);
        state.preview.metadata = body.value("metadata", json::object());
        state.preview.captured_at = EpochMillis();
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
        snapshot = state.preview;
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
    if (state.preview.png.empty()) {
        JsonResponse(res, 404, {{"ok", false}, {"error", "preview_unavailable"}});
        return;
    }
    JsonResponse(res, 200, {
        {"ok", true},
        {"requestId", state.preview.request_id},
        {"capturedAt", state.preview.captured_at},
        {"bytes", state.preview.png.size()},
        {"metadata", state.preview.metadata},
    });
}

void HandleRequestPreviewCapture(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const auto id = RequestPreviewCapture();
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
    const json result = QueueNodePatch(node_id, patch, expected);
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
    json patches = json::array();
    for (const auto& patch : state.patches) {
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
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    state.patches.erase(
        std::remove_if(state.patches.begin(), state.patches.end(), [&](const json& patch) {
            return std::find(ids.begin(), ids.end(), patch.value("id", 0ull)) != ids.end();
        }),
        state.patches.end());
    JsonResponse(res, 200, {{"ok", true}, {"acknowledged", ids.size()}});
}

json GetEditorContext() {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return ContextLocked(state);
}

json GetEditorNode(const std::string& node_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!IsOnlineLocked(state)) return {{"ok", false}, {"error", "editor_offline"}};
    const json* node = FindCurrentNode(state.session, node_id);
    if (node == nullptr) return {{"ok", false}, {"error", "node_not_found"}, {"nodeId", node_id}};
    return {{"ok", true}, {"node", *node}, {"graphHash", state.session.value("graphHash", "")}};
}

json ListEditorNodes() {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!IsOnlineLocked(state)) return {{"ok", false}, {"error", "editor_offline"}};
    const json* graph = ResolveCurrentGraph(state.session);
    if (graph == nullptr || !graph->contains("nodes")) return {{"ok", false}, {"error", "graph_unavailable"}};
    return {{"ok", true}, {"nodes", (*graph)["nodes"]}, {"graphHash", state.session.value("graphHash", "")}};
}

json QueueNodePatch(
    const std::string& node_id,
    const json& patch,
    const std::string& expected_graph_hash) {
    if (node_id.empty() || !patch.is_object()) {
        return {{"ok", false}, {"status", 400}, {"error", "nodeId and object patch are required"}};
    }
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!IsOnlineLocked(state)) return {{"ok", false}, {"status", 409}, {"error", "editor_offline"}};
    const std::string current_hash = state.session.value("graphHash", "");
    if (!expected_graph_hash.empty() && expected_graph_hash != current_hash) {
        return {
            {"ok", false}, {"status", 409}, {"error", "graph_conflict"},
            {"expectedGraphHash", expected_graph_hash}, {"currentGraphHash", current_hash},
        };
    }
    if (FindCurrentNode(state.session, node_id) == nullptr) {
        return {{"ok", false}, {"status", 404}, {"error", "node_not_found"}, {"nodeId", node_id}};
    }
    json queued = {
        {"id", state.next_patch_id++}, {"type", "setNodeParams"},
        {"nodeId", node_id}, {"patch", patch}, {"baseGraphHash", current_hash},
        {"editPath", state.session.value("editPath", json::array())}, {"createdAt", EpochMillis()},
    };
    state.patches.push_back(queued);
    return {{"ok", true}, {"accepted", true}, {"patch", std::move(queued)}};
}

json GetEditorGraph() {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (!IsOnlineLocked(state)) return json();
    return state.session.value("graph", json());
}

uint64_t RequestPreviewCapture() {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return ++state.capture_request_id;
}

bool WaitForPreview(
    uint64_t request_id,
    std::chrono::milliseconds timeout,
    PreviewSnapshot& snapshot) {
    auto& state = State();
    std::unique_lock<std::mutex> lock(state.mutex);
    const bool ready = state.preview_changed.wait_for(lock, timeout, [&] {
        return state.preview.request_id >= request_id && !state.preview.png.empty();
    });
    if (ready) snapshot = state.preview;
    return ready;
}

json GetBridgeHealth() {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return {
        {"editorOnline", IsOnlineLocked(state)}, {"sessionRevision", state.session_revision},
        {"captureRequestId", state.capture_request_id}, {"previewRequestId", state.preview.request_id},
        {"pendingPatchCount", state.patches.size()},
    };
}

}  // namespace pcg_server
