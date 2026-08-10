#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "httplib.h"

namespace pcg_server {

/** MCP Streamable HTTP endpoint. POST responses may be JSON or SSE. */
void HandleMcpPost(const httplib::Request& req, httplib::Response& res);
void HandleMcpGet(const httplib::Request& req, httplib::Response& res);

/** Shared tool surface used by both MCP HTTP and the embedded LLM agent. */
nlohmann::json GetPcgToolDefinitions();
nlohmann::json CallPcgTool(
    const std::string& name,
    const nlohmann::json& arguments = nlohmann::json::object());
bool PcgToolRequiresApproval(const std::string& name);
bool PcgToolMutatesGraph(const std::string& name);

}  // namespace pcg_server
