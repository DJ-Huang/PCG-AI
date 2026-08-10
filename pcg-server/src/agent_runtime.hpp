#pragma once

#include "httplib.h"

namespace pcg_server {

// Keeps browser OAuth redirect URIs aligned with the actual --port value.
void ConfigureAgentRuntime(int server_port);

// Describes the active secret backend without exposing credential contents.
const char* AgentCredentialStoreName();

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
