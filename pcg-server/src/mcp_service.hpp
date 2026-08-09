#pragma once

#include "httplib.h"

namespace pcg_server {

/** MCP Streamable HTTP endpoint. POST responses may be JSON or SSE. */
void HandleMcpPost(const httplib::Request& req, httplib::Response& res);
void HandleMcpGet(const httplib::Request& req, httplib::Response& res);

}  // namespace pcg_server
