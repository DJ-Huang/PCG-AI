#include "agent_service.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pcg_server {
namespace {

// Auth skeleton: when PCG_AGENT_TOKEN is unset the service runs in dev mode
// (pass-through, one-time warning). When set, requests must carry a matching
// "Authorization: Bearer <token>" header or they get 401.
// Mock node-type picker: recognize a few manifest type names in the message.
std::string PickNodeType(const std::string& message) {
    static const std::vector<std::string> kKnown = {
        "SpawnPoints", "BoundsFromSpline", "ExtrudePolygon", "PlaceInScene",
        "ScatterOnSurface", "ScatterOnSpline", "Combine", "FilterByAttribute",
    };
    for (const auto& type : kKnown) {
        if (message.find(type) != std::string::npos) {
            return type;
        }
    }
    return "SpawnPoints";
}

bool WantsGraphAction(const std::string& message) {
    return message.find("add") != std::string::npos ||
           message.find("创建") != std::string::npos ||
           message.find("加") != std::string::npos;
}

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

    std::string message;
    int attachment_count = 0;
    const nlohmann::json parsed = nlohmann::json::parse(req.body, nullptr, false);
    if (parsed.is_object()) {
        if (parsed.contains("message") && parsed["message"].is_string()) {
            message = parsed["message"].get<std::string>();
        }
        if (parsed.contains("attachments") && parsed["attachments"].is_array()) {
            attachment_count = static_cast<int>(parsed["attachments"].size());
        }
    }

    std::string reply = "echo: " + message;
    if (attachment_count > 0) {
        reply += " (+" + std::to_string(attachment_count) + " attachment(s))";
    }
    reply += " — mock agent, no LLM connected";

    nlohmann::json actions = nlohmann::json::array();
    if (!message.empty() && WantsGraphAction(message)) {
        actions.push_back({
            {"type", "addNode"},
            {"nodeType", PickNodeType(message)},
        });
        reply += "; dispatching 1 graph action";
    }

    nlohmann::json body = {
        {"ok", true},
        {"reply", reply},
        {"actions", actions},
    };
    res.status = 200;
    res.set_content(body.dump(), "application/json");
}

void HandleAgentHealth(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) {
        return;
    }
    res.status = 200;
    res.set_content(R"({"ok":true,"service":"agent","mock":true})", "application/json");
}

}  // namespace pcg_server
