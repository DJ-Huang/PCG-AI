#include "agent_session_store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <chrono>
#include <regex>
#include <vector>

#ifdef __APPLE__
#include <sys/stat.h>
#endif

#include "agent_service.hpp"

namespace pcg_server {
namespace {

using json = nlohmann::json;

std::filesystem::path SessionsRoot() {
    if (const char* override_path = std::getenv("PCG_AGENT_SESSIONS_PATH")) {
        if (*override_path) return std::filesystem::path(override_path);
    }
    const char* home = std::getenv("HOME");
    const auto root = home && *home ? std::filesystem::path(home) : std::filesystem::temp_directory_path();
    return root / "Library" / "Application Support" / "PCG-AI" / "Agent" / "Sessions";
}

bool ValidId(const std::string& id) {
    static const std::regex pattern(R"(^session-[A-Za-z0-9_-]{8,96}$)");
    return std::regex_match(id, pattern);
}

std::filesystem::path SessionPath(const std::string& id) {
    return SessionsRoot() / (id + ".json");
}

void JsonResponse(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(-1, ' ', false, json::error_handler_t::replace), "application/json");
}

json Error(const std::string& code, const std::string& message) {
    return {{"ok", false}, {"error", {{"code", code}, {"message", message}, {"retryable", false}}}};
}

std::string Match(const httplib::Request& req, size_t index) {
    return req.matches.size() > index ? req.matches[index].str() : "";
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::string TrimTitle(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '\r'), value.end());
    const size_t newline = value.find('\n');
    if (newline != std::string::npos) value.resize(newline);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    if (value.size() > 80) {
        size_t end = 77;
        while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) --end;
        value.resize(end);
        value += "...";
    }
    return value.empty() ? "New chat" : value;
}

int64_t QueryInteger(const httplib::Request& req, const char* name, int64_t fallback) {
    if (!req.has_param(name)) return fallback;
    const std::string value = req.get_param_value(name);
    if (value.empty()) return fallback;
    try {
        size_t consumed = 0;
        const int64_t parsed = std::stoll(value, &consumed);
        return consumed == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

}  // namespace

bool LoadAgentSession(const std::string& id, json& session) {
    if (!ValidId(id)) return false;
    std::ifstream input(SessionPath(id));
    if (!input) return false;
    session = json::parse(input, nullptr, false);
    return session.is_object() && session.value("id", "") == id;
}

bool SaveAgentSession(const json& session) {
    const std::string id = session.value("id", "");
    if (!ValidId(id)) return false;
    const auto root = SessionsRoot();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) return false;
#ifdef __APPLE__
    chmod(root.c_str(), S_IRWXU);
#endif
    const auto target = SessionPath(id);
    const auto temporary = target.string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return false;
        output << session.dump(2, ' ', false, json::error_handler_t::replace);
        output.close();
        if (!output) return false;
    }
#ifdef __APPLE__
    chmod(temporary.c_str(), S_IRUSR | S_IWUSR);
#endif
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        std::filesystem::remove(temporary);
        return false;
    }
    return true;
}

bool SaveAgentSessionAttachment(const std::string& session_id, const std::string& filename,
                                const std::string& content) {
    if (!ValidId(session_id)) return false;
    const auto directory = SessionsRoot() / session_id / "attachments";
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;
#ifdef __APPLE__
    chmod((SessionsRoot() / session_id).c_str(), S_IRWXU);
    chmod(directory.c_str(), S_IRWXU);
#endif
    std::string safe_name = std::filesystem::path(filename).filename().string();
    safe_name = std::regex_replace(safe_name, std::regex(R"([^A-Za-z0-9._-])"), "_");
    if (safe_name.empty() || safe_name == "." || safe_name == "..") safe_name = "attachment";
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto target = directory / (std::to_string(stamp) + "-" + safe_name);
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (!output) return false;
#ifdef __APPLE__
    chmod(target.c_str(), S_IRUSR | S_IWUSR);
#endif
    return true;
}

json PublicAgentSession(const json& session, bool include_messages) {
    json result = {
        {"id", session.value("id", "")},
        {"title", session.value("title", "New chat")},
        {"createdAt", session.value("createdAt", 0LL)},
        {"updatedAt", session.value("updatedAt", 0LL)},
        {"providerId", session.value("providerId", "")},
        {"modelId", session.value("modelId", "")},
        {"graphName", session.value("graphName", "")},
        {"status", session.value("status", "idle")},
    };
    if (include_messages) {
        result["messages"] = session.value("messages", json::array());
        for (auto& message : result["messages"]) message.erase("_historyStart");
    }
    return result;
}

void RecoverInterruptedAgentSessions() {
    std::error_code ec;
    const auto root = SessionsRoot();
    if (!std::filesystem::exists(root, ec)) return;
    for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
        if (ec || entry.path().extension() != ".json") continue;
        json session;
        const std::string id = entry.path().stem().string();
        if (!LoadAgentSession(id, session) || session.value("status", "") != "running") continue;
        session["status"] = "interrupted";
        auto& messages = session["messages"];
        if (messages.is_array() && !messages.empty() && messages.back().value("role", "") == "assistant") {
            messages.back()["status"] = "interrupted";
        }
        SaveAgentSession(session);
    }
}

void HandleAgentListSessions(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string query = Lower(req.has_param("query") ? req.get_param_value("query") : "");
    const int64_t requested_limit = QueryInteger(req, "limit", 50);
    const int limit = static_cast<int>(std::max<int64_t>(1, std::min<int64_t>(100, requested_limit)));
    const int64_t cursor = std::max<int64_t>(0, QueryInteger(req, "cursor", 0));
    std::vector<json> sessions;
    std::error_code ec;
    const auto root = SessionsRoot();
    if (std::filesystem::exists(root, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(root, ec)) {
            if (ec || entry.path().extension() != ".json") continue;
            json session;
            if (!LoadAgentSession(entry.path().stem().string(), session)) continue;
            const int64_t updated = session.value("updatedAt", 0LL);
            if (cursor > 0 && updated >= cursor) continue;
            if (!query.empty() && Lower(session.value("title", "")).find(query) == std::string::npos) continue;
            sessions.push_back(PublicAgentSession(session, false));
        }
    }
    std::sort(sessions.begin(), sessions.end(), [](const json& left, const json& right) {
        return left.value("updatedAt", 0LL) > right.value("updatedAt", 0LL);
    });
    const bool more = sessions.size() > static_cast<size_t>(limit);
    if (more) sessions.resize(static_cast<size_t>(limit));
    const int64_t next = more && !sessions.empty() ? sessions.back().value("updatedAt", 0LL) : 0;
    JsonResponse(res, 200, {{"ok", true}, {"sessions", sessions}, {"nextCursor", next}});
}

void HandleAgentGetSession(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json session;
    if (!LoadAgentSession(Match(req, 1), session)) {
        JsonResponse(res, 404, Error("session_not_found", "Chat session was not found."));
        return;
    }
    JsonResponse(res, 200, {{"ok", true}, {"session", PublicAgentSession(session, true)}});
}

void HandleAgentPatchSession(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json session;
    if (!LoadAgentSession(Match(req, 1), session)) {
        JsonResponse(res, 404, Error("session_not_found", "Chat session was not found."));
        return;
    }
    const json body = json::parse(req.body, nullptr, false);
    if (!body.is_object() || !body.contains("title") || !body["title"].is_string()) {
        JsonResponse(res, 400, Error("invalid_request", "A title is required."));
        return;
    }
    session["title"] = TrimTitle(body.value("title", ""));
    if (!SaveAgentSession(session)) {
        JsonResponse(res, 500, Error("session_write_failed", "Could not save the chat session."));
        return;
    }
    JsonResponse(res, 200, {{"ok", true}, {"session", PublicAgentSession(session, false)}});
}

void HandleAgentDeleteSession(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string id = Match(req, 1);
    if (!ValidId(id)) {
        JsonResponse(res, 404, Error("session_not_found", "Chat session was not found."));
        return;
    }
    std::error_code ec;
    const bool removed = std::filesystem::remove(SessionPath(id), ec);
    std::filesystem::remove_all(SessionsRoot() / id, ec);
    if (ec) {
        JsonResponse(res, 500, Error("session_delete_failed", "Could not delete the chat session."));
        return;
    }
    JsonResponse(res, removed ? 200 : 404,
        removed ? json{{"ok", true}, {"sessionId", id}} : Error("session_not_found", "Chat session was not found."));
}

}  // namespace pcg_server
