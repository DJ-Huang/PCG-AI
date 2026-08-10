#include "mcp_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

#include "agent_service.hpp"
#include "cook_service.hpp"
#include "session_service.hpp"

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

int CommandTimeout(const json& arguments) {
    return std::max(1000, std::min(30000, arguments.value("timeoutMs", 10000)));
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
    return json::array({
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
            {"name", "pcg_capture_preview"},
            {"description", "Ask the live WebGL viewport to render and return its current PNG plus camera and shading metadata."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {{"timeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 30000}, {"default", 10000}}}}},
                {"additionalProperties", false},
            }},
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
    });
}

json CallToolInternal(const std::string& name, const json& arguments) {
    if (name == "pcg_get_editor_context") return ToolResult(GetEditorContext());
    if (name == "pcg_list_nodes") {
        const json result = ListEditorNodes();
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_get_graph") {
        const json result = GetEditorDocument();
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_get_node_types") {
        const json result = GetEditorNodeTypes(
            arguments.value("nodeType", ""),
            arguments.value("category", ""));
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_get_node") {
        std::string node_id = arguments.value("nodeId", "");
        if (node_id.empty()) {
            const json context = GetEditorContext();
            if (context.contains("session") && context["session"].is_object()) {
                node_id = context["session"].value("selectedNodeId", "");
            }
        }
        if (node_id.empty()) return ToolResult({{"ok", false}, {"error", "no_node_selected"}}, true);
        const json result = GetEditorNode(node_id);
        return ToolResult(result, !result.value("ok", false));
    }
    if (name == "pcg_patch_node") {
        const json queued = QueueNodePatch(
            arguments.value("nodeId", ""),
            arguments.value("patch", json()),
            arguments.value("ifGraphHash", ""));
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
            arguments.value("ifGraphHash", ""));
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
            true);
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
            arguments.value("ifGraphHash", ""));
        return WaitForAppliedCommand(queued, CommandTimeout(arguments));
    }
    if (name == "pcg_capture_preview") {
        const int requested_timeout = arguments.value("timeoutMs", 10000);
        const int timeout = std::max(1000, std::min(30000, requested_timeout));
        const uint64_t request_id = RequestPreviewCapture();
        PreviewSnapshot snapshot;
        if (!WaitForPreview(request_id, std::chrono::milliseconds(timeout), snapshot)) {
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
    if (name == "pcg_validate") {
        const json graph = GetEditorGraph();
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
        const json graph = GetEditorGraph();
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

json CallPcgTool(const std::string& name, const json& arguments) {
    return CallToolInternal(name, arguments);
}

bool PcgToolRequiresApproval(const std::string& name) {
    return name == "pcg_patch_node" ||
           name == "pcg_apply_graph_ops" ||
           name == "pcg_replace_graph" ||
           name == "pcg_save_graph";
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
