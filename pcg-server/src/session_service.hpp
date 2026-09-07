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
void HandlePostCameraCommand(const httplib::Request& req, httplib::Response& res);
void HandlePutCameraState(const httplib::Request& req, httplib::Response& res);
void HandleGetCameraState(const httplib::Request& req, httplib::Response& res);
void HandlePatchNode(const httplib::Request& req, httplib::Response& res);
void HandleGetGraphPatches(const httplib::Request& req, httplib::Response& res);
void HandleAckGraphPatches(const httplib::Request& req, httplib::Response& res);

nlohmann::json GetEditorContext(const std::string& editor_session_id = "");
nlohmann::json GetEditorNode(
    const std::string& node_id,
    const std::string& editor_session_id = "");
nlohmann::json ListEditorNodes(const std::string& editor_session_id = "");
nlohmann::json GetEditorNodeTypes(
    const std::string& node_type = "",
    const std::string& category = "",
    const std::string& editor_session_id = "");
nlohmann::json GetEditorDocument(const std::string& editor_session_id = "");
nlohmann::json GetEditorShot(const std::string& editor_session_id = "");
nlohmann::json QueueNodePatch(
    const std::string& node_id,
    const nlohmann::json& patch,
    const std::string& expected_graph_hash,
    const std::string& editor_session_id = "");
nlohmann::json QueueGraphCommand(
    nlohmann::json command,
    const std::string& expected_graph_hash,
    bool require_root_scope = false,
    const std::string& editor_session_id = "");
nlohmann::json QueueShotCommand(
    nlohmann::json command,
    const std::string& expected_shot_hash,
    const std::string& editor_session_id = "");
nlohmann::json QueuePreviewShotCommand(
    nlohmann::json command,
    const std::string& editor_session_id = "");
bool WaitForGraphCommandResult(
    uint64_t command_id,
    std::chrono::milliseconds timeout,
    nlohmann::json& result);
bool CancelGraphCommand(uint64_t command_id);
nlohmann::json GetEditorGraph(const std::string& editor_session_id = "");
uint64_t RequestPreviewCapture(
    const std::string& editor_session_id = "",
    const nlohmann::json& options = nlohmann::json::object());
uint64_t RequestCameraCommand(
    const nlohmann::json& camera,
    const std::string& editor_session_id = "");
bool WaitForCameraState(
    uint64_t command_id,
    std::chrono::milliseconds timeout,
    nlohmann::json& state,
    const std::string& editor_session_id = "");
nlohmann::json GetCameraState(const std::string& editor_session_id = "");
bool WaitForPreview(
    uint64_t request_id,
    std::chrono::milliseconds timeout,
    PreviewSnapshot& snapshot,
    const std::string& editor_session_id = "");
nlohmann::json GetBridgeHealth();

}  // namespace pcg_server
