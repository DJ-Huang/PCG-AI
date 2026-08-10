#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "httplib.h"

namespace pcg_server {

bool LoadAgentSession(const std::string& id, nlohmann::json& session);
bool SaveAgentSession(const nlohmann::json& session);
bool SaveAgentSessionAttachment(const std::string& session_id, const std::string& filename,
                                const std::string& content);
nlohmann::json PublicAgentSession(const nlohmann::json& session, bool include_messages);
void RecoverInterruptedAgentSessions();

void HandleAgentListSessions(const httplib::Request& req, httplib::Response& res);
void HandleAgentGetSession(const httplib::Request& req, httplib::Response& res);
void HandleAgentPatchSession(const httplib::Request& req, httplib::Response& res);
void HandleAgentDeleteSession(const httplib::Request& req, httplib::Response& res);

}  // namespace pcg_server
