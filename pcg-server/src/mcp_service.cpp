#include "mcp_service.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
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

json ErrorResponse(const json& id, int code, const std::string& message, const json& data = nullptr) {
    json error = {{"code", code}, {"message", message}};
    if (!data.is_null()) error["data"] = data;
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", std::move(error)}};
}

json SuccessResponse(const json& id, const json& result) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", result}};
}

json ToolDefinitions() {
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
            {"description", "Queue a parameter patch for a node. The Web editor applies it as one undoable action."},
            {"inputSchema", {
                {"type", "object"},
                {"properties", {
                    {"nodeId", {{"type", "string"}}},
                    {"patch", {{"type", "object"}, {"description", "Node data properties to merge."}}},
                    {"ifGraphHash", {{"type", "string"}, {"description", "Optional optimistic-lock hash from context."}}},
                }},
                {"required", json::array({"nodeId", "patch"})},
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

json CallTool(const std::string& name, const json& arguments) {
    if (name == "pcg_get_editor_context") return ToolResult(GetEditorContext());
    if (name == "pcg_list_nodes") {
        const json result = ListEditorNodes();
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
        const json result = QueueNodePatch(
            arguments.value("nodeId", ""),
            arguments.value("patch", json()),
            arguments.value("ifGraphHash", ""));
        return ToolResult(result, !result.value("ok", false));
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
            {"instructions", "Use editor context first. Pass graphHash to patch calls for optimistic locking."},
        });
    }
    if (method == "ping") return SuccessResponse(id, json::object());
    if (method == "tools/list") return SuccessResponse(id, {{"tools", ToolDefinitions()}});
    if (method == "tools/call") {
        const json params = message.value("params", json::object());
        if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
            return ErrorResponse(id, -32602, "Invalid tools/call parameters");
        }
        return SuccessResponse(id, CallTool(
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
