#include "cook_service.hpp"
#include "kb_service.hpp"
#include "mcp_service.hpp"
#include "session_service.hpp"
#include "surface_reconstruction_service.hpp"
#include "third_party_service.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "httplib.h"
#include "pcg_api.h"
#include "pcg_fbx_api.h"

namespace {

// Initialize the HTTP client before request threads start. It outlives the
// server so ongoing Tripo requests finish before global curl cleanup.
struct CurlRuntime {
    const CURLcode status = curl_global_init(CURL_GLOBAL_DEFAULT);
    ~CurlRuntime() {
        if (status == CURLE_OK) curl_global_cleanup();
    }
};

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
                << "  GET  /v1/kb/status\n"
                << "  POST /v1/kb/reindex\n"
                << "  GET|POST /v1/kb/search\n"
                << "  GET  /v1/kb/list\n"
                << "  GET|POST /v1/kb/get\n"
                << "  GET  /v1/golden-graphs/list\n"
                << "  GET|POST /v1/golden-graphs/get\n"
                << "  GET  /v1/third-party/tripo/status\n"
                << "  PUT|DELETE /v1/third-party/tripo/config\n"
                << "  POST /v1/third-party/tripo/generate\n"
                << "  GET  /v1/third-party/cache/<file>\n"
                << "  POST /v1/reconstruct/oriented-sdf\n"
                << "  PUT|GET /v1/session\n"
                << "  PUT|GET /v1/preview/screenshot\n"
                << "  POST /v1/preview/request-capture\n"
                << "  PATCH /v1/graph/nodes/:id\n"
                << "  POST /mcp (MCP Streamable HTTP; optional SSE response)\n"
                << "  Set PCG_SERVER_TOKEN to protect editor, MCP and 3D generation APIs.\n";
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
    const CurlRuntime curl;
    if (curl.status != CURLE_OK) {
        std::cerr << "[pcg-server] could not initialize HTTP client" << std::endl;
        return 1;
    }
    pcg_server::ConfigureKbRoot(std::filesystem::current_path());
    pcg_server::ConfigureSurfaceReconstructionRoot(std::filesystem::current_path());
    httplib::Server svr;
    svr.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr error) {
        try {
            if (error) std::rethrow_exception(error);
        } catch (const std::exception& exception) {
            std::cerr << "[pcg-server] request failed: " << exception.what() << std::endl;
        } catch (...) {
            std::cerr << "[pcg-server] request failed with an unknown exception" << std::endl;
        }
        res.status = 500;
        res.set_content(nlohmann::json{
            {"ok", false},
            {"error", {{"code", "internal_error"},
                       {"message", "The local server encountered an internal error."},
                       {"retryable", true}}},
        }.dump(), "application/json");
    });

    svr.Get("/v1/health", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json body = {
            {"ok", true},
            {"version", pcg_get_version()},
            {"fbx_version", pcg_fbx_get_version()},
            {"api", "v1"},
            {"mcp", {{"enabled", true}, {"endpoint", "/mcp"}, {"transport", "streamable-http"}}},
            {"editor_bridge", pcg_server::GetBridgeHealth()},
        };
        res.set_content(body.dump(), "application/json");
    });

    svr.Post("/v1/cook", pcg_server::HandleCook);
    svr.Post("/v1/cancel", pcg_server::HandleCancel);
    svr.Post("/v1/validate", pcg_server::HandleValidate);
    svr.Post("/v1/cache/clear", pcg_server::HandleCacheClear);
    svr.Post("/v1/export-fbx", pcg_server::HandleExportFbx);
    svr.Get("/v1/kb/status", pcg_server::HandleKbStatus);
    svr.Post("/v1/kb/reindex", pcg_server::HandleKbReindex);
    svr.Get("/v1/kb/search", pcg_server::HandleKbSearch);
    svr.Post("/v1/kb/search", pcg_server::HandleKbSearch);
    svr.Get("/v1/kb/list", pcg_server::HandleKbList);
    svr.Get("/v1/kb/get", pcg_server::HandleKbGet);
    svr.Post("/v1/kb/get", pcg_server::HandleKbGet);
    svr.Get("/v1/golden-graphs/list", pcg_server::HandleKbGoldenGraphList);
    svr.Get("/v1/golden-graphs/get", pcg_server::HandleKbGoldenGraphGet);
    svr.Post("/v1/golden-graphs/get", pcg_server::HandleKbGoldenGraphGet);
    svr.Get("/v1/third-party/tripo/status", pcg_server::HandleThirdPartyTripoStatus);
    svr.Put("/v1/third-party/tripo/config", pcg_server::HandleThirdPartyTripoConfigPut);
    svr.Delete("/v1/third-party/tripo/config", pcg_server::HandleThirdPartyTripoConfigDelete);
    svr.Post("/v1/third-party/tripo/generate", pcg_server::HandleThirdPartyTripoGenerate);
    svr.Get(R"(/v1/third-party/cache/([^/]+))", pcg_server::HandleThirdPartyCacheGet);
    svr.Post("/v1/reconstruct/oriented-sdf", pcg_server::HandleBuildOrientedSdfNodeData);
    svr.Get("/v1/assets/preserved-gltf", pcg_server::HandlePreservedGltfGet);
    svr.Put("/v1/session", pcg_server::HandlePutSession);
    svr.Get("/v1/session", pcg_server::HandleGetSession);
    svr.Post("/v1/session/heartbeat", pcg_server::HandleSessionHeartbeat);
    svr.Put("/v1/preview/screenshot", pcg_server::HandlePutPreviewScreenshot);
    svr.Get("/v1/preview/screenshot", pcg_server::HandleGetPreviewScreenshot);
    svr.Get("/v1/preview/metadata", pcg_server::HandleGetPreviewMetadata);
    svr.Post("/v1/preview/request-capture", pcg_server::HandleRequestPreviewCapture);
    svr.Post("/v1/camera/command", pcg_server::HandlePostCameraCommand);
    svr.Put("/v1/camera/state", pcg_server::HandlePutCameraState);
    svr.Get("/v1/camera/state", pcg_server::HandleGetCameraState);
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
