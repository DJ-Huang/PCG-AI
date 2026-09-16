#include "server_auth.hpp"

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>

namespace pcg_server {

bool CheckServerAuth(const httplib::Request& req, httplib::Response& res) {
    const char* token = std::getenv("PCG_SERVER_TOKEN");
    // Do not silently disable existing installations' authentication.
    if (token == nullptr || *token == '\0') token = std::getenv("PCG_AGENT_TOKEN");
    if (token == nullptr || *token == '\0') {
        static std::once_flag warning;
        std::call_once(warning, [] {
            std::cerr << "[pcg-server] warning: PCG_SERVER_TOKEN not set — editor API auth disabled (localhost dev mode)"
                      << std::endl;
        });
        return true;
    }
    if (req.get_header_value("Authorization") == std::string("Bearer ") + token) return true;
    res.status = 401;
    res.set_content(R"({"ok":false,"error":"unauthorized"})", "application/json");
    return false;
}

}  // namespace pcg_server
