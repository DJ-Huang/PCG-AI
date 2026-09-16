#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "httplib.h"

namespace pcg_server {

/** MCP Streamable HTTP endpoint. POST responses may be JSON or SSE. */
void HandleMcpPost(const httplib::Request& req, httplib::Response& res);
void HandleMcpGet(const httplib::Request& req, httplib::Response& res);

/** Tool surface for external editor integrations over MCP HTTP. */
nlohmann::json GetPcgToolDefinitions();
nlohmann::json CallPcgTool(
    const std::string& name,
    const nlohmann::json& arguments = nlohmann::json::object(),
    const std::string& editor_session_id = "");

}  // namespace pcg_server
