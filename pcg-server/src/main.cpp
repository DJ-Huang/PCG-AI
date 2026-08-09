#include "cook_service.hpp"
#include "agent_service.hpp"
#include "mcp_service.hpp"
#include "session_service.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

#include <nlohmann/json.hpp>

#include "httplib.h"
#include "pcg_api.h"
#include "pcg_fbx_api.h"

namespace {

int ParsePort(int argc, char** argv, int fallback) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--port" || arg == "-p") && i + 1 < argc) {
            return std::atoi(argv[++i]);
        }
        if (arg == "--help" || arg == "-h") {
            std::cout
                << "Usage: pcg-server [--port 17890]\n"
                << "  GET  /v1/health\n"
                << "  POST /v1/cook\n"
                << "  POST /v1/cancel\n"
                << "  POST /v1/validate\n"
                << "  POST /v1/cache/clear\n"
                << "  POST /v1/export-fbx\n"
                << "  POST /v1/agent/chat (mock agent; Bearer PCG_AGENT_TOKEN when set)\n"
                << "  GET  /v1/agent/health\n"
                << "  PUT|GET /v1/session\n"
                << "  PUT|GET /v1/preview/screenshot\n"
                << "  POST /v1/preview/request-capture\n"
                << "  PATCH /v1/graph/nodes/:id\n"
                << "  POST /mcp (MCP Streamable HTTP; optional SSE response)\n";
            std::exit(0);
        }
    }
    if (const char* env = std::getenv("PCG_SERVER_PORT")) {
        const int port = std::atoi(env);
        if (port > 0) {
            return port;
        }
    }
    return fallback;
}

}  // namespace

int main(int argc, char** argv) {
    const int port = ParsePort(argc, argv, 17890);
    httplib::Server svr;

    svr.Get("/v1/health", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json body = {
            {"ok", true},
            {"version", pcg_get_version()},
            {"fbx_version", pcg_fbx_get_version()},
            {"api", "v1"},
            {"mcp", {{"enabled", true}, {"endpoint", "/mcp"}, {"transport", "streamable-http"}}},
            {"agent_bridge", pcg_server::GetBridgeHealth()},
        };
        res.set_content(body.dump(), "application/json");
    });

    svr.Post("/v1/cook", pcg_server::HandleCook);
    svr.Post("/v1/cancel", pcg_server::HandleCancel);
    svr.Post("/v1/validate", pcg_server::HandleValidate);
    svr.Post("/v1/cache/clear", pcg_server::HandleCacheClear);
    svr.Post("/v1/agent/chat", pcg_server::HandleAgentChat);
    svr.Get("/v1/agent/health", pcg_server::HandleAgentHealth);
    svr.Post("/v1/export-fbx", pcg_server::HandleExportFbx);
    svr.Put("/v1/session", pcg_server::HandlePutSession);
    svr.Get("/v1/session", pcg_server::HandleGetSession);
    svr.Post("/v1/session/heartbeat", pcg_server::HandleSessionHeartbeat);
    svr.Put("/v1/preview/screenshot", pcg_server::HandlePutPreviewScreenshot);
    svr.Get("/v1/preview/screenshot", pcg_server::HandleGetPreviewScreenshot);
    svr.Get("/v1/preview/metadata", pcg_server::HandleGetPreviewMetadata);
    svr.Post("/v1/preview/request-capture", pcg_server::HandleRequestPreviewCapture);
    svr.Patch(R"(/v1/graph/nodes/(.+))", pcg_server::HandlePatchNode);
    svr.Get("/v1/graph/patches", pcg_server::HandleGetGraphPatches);
    svr.Post("/v1/graph/patches/ack", pcg_server::HandleAckGraphPatches);
    svr.Post("/mcp", pcg_server::HandleMcpPost);
    svr.Get("/mcp", pcg_server::HandleMcpGet);

    svr.set_payload_max_length(512ull * 1024ull * 1024ull);
    svr.set_read_timeout(600, 0);
    svr.set_write_timeout(600, 0);

    std::cout << "[pcg-server] " << pcg_get_version()
              << " + " << pcg_fbx_get_version()
              << " listening on http://127.0.0.1:" << port << std::endl;

    if (!svr.listen("127.0.0.1", port)) {
        std::cerr << "[pcg-server] failed to bind 127.0.0.1:" << port << std::endl;
        return 1;
    }
    return 0;
}
