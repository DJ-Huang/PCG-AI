#pragma once

#include "httplib.h"

namespace pcg_server {

/** Third-party cloud generation vendors (Tripo, …). API keys live in the
    Agent credential store (or PCG_TRIPO_API_KEY env) — never in .pcg files,
    never echoed back over HTTP. Cook/preview never call these endpoints. */
void HandleThirdPartyTripoStatus(const httplib::Request& req, httplib::Response& res);
void HandleThirdPartyTripoConfigPut(const httplib::Request& req, httplib::Response& res);
void HandleThirdPartyTripoConfigDelete(const httplib::Request& req, httplib::Response& res);

/** POST /v1/third-party/tripo/generate — synchronous upload → create → poll →
    download → cache; returns a workspace-relative GLB path. Never triggered by
    cook/preview; only the web Inspector Generate action calls this. */
void HandleThirdPartyTripoGenerate(const httplib::Request& req, httplib::Response& res);

/** GET /v1/third-party/cache/<file> — serves a cached GLB (octet-stream) so the
    web editor can load it for the reference overlay. Path must stay inside the
    Tripo cache directory. */
void HandleThirdPartyCacheGet(const httplib::Request& req, httplib::Response& res);

}  // namespace pcg_server
