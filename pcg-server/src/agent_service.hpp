#pragma once

#include "httplib.h"

namespace pcg_server {

/** Shared bearer-token gate for all agent bridge endpoints. */
bool CheckAgentAuth(const httplib::Request& req, httplib::Response& res);

/// POST /v1/agent/chat — mock agent echo + example graph actions.
void HandleAgentChat(const httplib::Request& req, httplib::Response& res);

/// GET /v1/agent/health — reserved liveness probe for the agent service.
void HandleAgentHealth(const httplib::Request& req, httplib::Response& res);

}  // namespace pcg_server
