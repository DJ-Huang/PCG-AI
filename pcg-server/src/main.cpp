#include "cook_service.hpp"
#include "agent_service.hpp"

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
                << "  GET  /v1/agent/health\n";
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
