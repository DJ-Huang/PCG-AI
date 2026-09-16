#pragma once

#include "httplib.h"

namespace pcg_server {

// Shared bearer-token gate for editor integrations and 3D generation services.
// PCG_SERVER_TOKEN is preferred; the old variable remains a compatibility alias.
bool CheckServerAuth(const httplib::Request& req, httplib::Response& res);

}  // namespace pcg_server
