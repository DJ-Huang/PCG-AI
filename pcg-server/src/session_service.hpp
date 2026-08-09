#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "httplib.h"

namespace pcg_server {

struct PreviewSnapshot {
    uint64_t request_id = 0;
    std::vector<uint8_t> png;
    nlohmann::json metadata = nlohmann::json::object();
    int64_t captured_at = 0;
};

void HandlePutSession(const httplib::Request& req, httplib::Response& res);
void HandleGetSession(const httplib::Request& req, httplib::Response& res);
void HandleSessionHeartbeat(const httplib::Request& req, httplib::Response& res);
void HandlePutPreviewScreenshot(const httplib::Request& req, httplib::Response& res);
void HandleGetPreviewScreenshot(const httplib::Request& req, httplib::Response& res);
void HandleGetPreviewMetadata(const httplib::Request& req, httplib::Response& res);
void HandleRequestPreviewCapture(const httplib::Request& req, httplib::Response& res);
void HandlePatchNode(const httplib::Request& req, httplib::Response& res);
void HandleGetGraphPatches(const httplib::Request& req, httplib::Response& res);
void HandleAckGraphPatches(const httplib::Request& req, httplib::Response& res);

nlohmann::json GetEditorContext();
nlohmann::json GetEditorNode(const std::string& node_id);
nlohmann::json ListEditorNodes();
nlohmann::json GetEditorNodeTypes(
    const std::string& node_type = "",
    const std::string& category = "");
nlohmann::json GetEditorDocument();
nlohmann::json QueueNodePatch(
    const std::string& node_id,
    const nlohmann::json& patch,
    const std::string& expected_graph_hash);
nlohmann::json QueueGraphCommand(
    nlohmann::json command,
    const std::string& expected_graph_hash,
    bool require_root_scope = false);
bool WaitForGraphCommandResult(
    uint64_t command_id,
    std::chrono::milliseconds timeout,
    nlohmann::json& result);
bool CancelGraphCommand(uint64_t command_id);
nlohmann::json GetEditorGraph();
uint64_t RequestPreviewCapture();
bool WaitForPreview(
    uint64_t request_id,
    std::chrono::milliseconds timeout,
    PreviewSnapshot& snapshot);
nlohmann::json GetBridgeHealth();

}  // namespace pcg_server
