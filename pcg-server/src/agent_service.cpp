#include "agent_service.hpp"
#include "agent_runtime.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

namespace pcg_server {
namespace {

}  // namespace

bool CheckAgentAuth(const httplib::Request& req, httplib::Response& res) {
    const char* token_env = std::getenv("PCG_AGENT_TOKEN");
    if (token_env == nullptr || *token_env == '\0') {
        static bool warned = false;
        if (!warned) {
            std::cerr << "[pcg-server] warning: PCG_AGENT_TOKEN not set — agent bridge auth disabled (dev mode)"
                      << std::endl;
            warned = true;
        }
        return true;
    }
    const std::string expect = std::string("Bearer ") + token_env;
    if (req.get_header_value("Authorization") == expect) {
        return true;
    }
    res.status = 401;
    res.set_content(R"({"ok":false,"error":"unauthorized"})", "application/json");
    return false;
}

void HandleAgentChat(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) {
        return;
    }
    nlohmann::json body = {
        {"ok", false},
        {"error", {
            {"code", "legacy_chat_removed"},
            {"message", "Use POST /v1/agent/turns with multipart input."},
            {"retryable", false},
        }},
    };
    res.status = 410;
    res.set_content(body.dump(), "application/json");
}

void HandleAgentHealth(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) {
        return;
    }
    res.status = 200;
    nlohmann::json body = {
        {"ok", true},
        {"service", "agent"},
        {"mock", false},
        {"runtime", "embedded-cpp"},
        {"credentialStore", AgentCredentialStoreName()},
    };
    res.set_content(body.dump(), "application/json");
}

}  // namespace pcg_server
