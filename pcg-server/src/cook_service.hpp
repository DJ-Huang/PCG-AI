#pragma once

#include "httplib.h"

namespace pcg_server {

/** Handle POST /v1/cook multipart → length-prefixed binary response body. */
void HandleCook(const httplib::Request& req, httplib::Response& res);

/** Handle POST /v1/cancel (best-effort global cancel). */
void HandleCancel(const httplib::Request& req, httplib::Response& res);

/** Handle POST /v1/validate — body: graph JSON text. */
void HandleValidate(const httplib::Request& req, httplib::Response& res);

/** Handle POST /v1/cache/clear. */
void HandleCacheClear(const httplib::Request& req, httplib::Response& res);

/** Handle POST /v1/export-fbx multipart → FBX bytes. */
void HandleExportFbx(const httplib::Request& req, httplib::Response& res);

}  // namespace pcg_server
