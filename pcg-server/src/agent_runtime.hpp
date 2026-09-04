#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "httplib.h"

namespace pcg_server {

// Keeps browser OAuth redirect URIs aligned with the actual --port value.
void ConfigureAgentRuntime(int server_port);

// Describes the active secret backend without exposing credential contents.
const char* AgentCredentialStoreName();

// Namespaced secrets in the Agent credential store (file chmod 600, or macOS
// Keychain when PCG_AGENT_CREDENTIAL_STORE=keychain). Names must be unique
// across consumers (e.g. "thirdParty.tripo"). These functions never log or
// transmit the stored value; callers decide what to expose.
bool LoadProtectedCredential(const std::string& name, nlohmann::json& value);
bool StoreProtectedCredential(const std::string& name, const nlohmann::json& value);
bool DeleteProtectedCredential(const std::string& name);

void HandleAgentProviders(const httplib::Request& req, httplib::Response& res);
void HandleAgentConnectKey(const httplib::Request& req, httplib::Response& res);
void HandleAgentDeleteConnection(const httplib::Request& req, httplib::Response& res);
void HandleAgentValidateProvider(const httplib::Request& req, httplib::Response& res);
void HandleAgentGetSettings(const httplib::Request& req, httplib::Response& res);
void HandleAgentPutSettings(const httplib::Request& req, httplib::Response& res);
void HandleAgentOAuthStart(const httplib::Request& req, httplib::Response& res);
void HandleAgentOAuthStatus(const httplib::Request& req, httplib::Response& res);
void HandleAgentOAuthCallback(const httplib::Request& req, httplib::Response& res);
void HandleAgentTurn(const httplib::Request& req, httplib::Response& res);
void HandleAgentTurnDecision(const httplib::Request& req, httplib::Response& res);
void HandleAgentTurnCancel(const httplib::Request& req, httplib::Response& res);

}  // namespace pcg_server
