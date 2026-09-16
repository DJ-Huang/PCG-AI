#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "httplib.h"

namespace pcg_server {

/** Project-local knowledge base rooted at <workspace>/.picg/. Indexes rules/
    and kb/ markdown via BM25; serves golden-graphs .pcg templates by class. */
void ConfigureKbRoot(const std::filesystem::path& workspace_root);
std::filesystem::path GetKbRoot();

nlohmann::json KbStatus();
nlohmann::json KbReindex();
nlohmann::json KbSearch(const std::string& query, int top_k, const std::string& category);
nlohmann::json KbList(const std::string& category);
nlohmann::json KbGet(const std::string& relative_path);
nlohmann::json KbGoldenGraphList(const std::string& class_filter);
nlohmann::json KbGoldenGraphGet(const std::string& name);

void HandleKbStatus(const httplib::Request& req, httplib::Response& res);
void HandleKbReindex(const httplib::Request& req, httplib::Response& res);
void HandleKbSearch(const httplib::Request& req, httplib::Response& res);
void HandleKbList(const httplib::Request& req, httplib::Response& res);
void HandleKbGet(const httplib::Request& req, httplib::Response& res);
void HandleKbGoldenGraphList(const httplib::Request& req, httplib::Response& res);
void HandleKbGoldenGraphGet(const httplib::Request& req, httplib::Response& res);

}  // namespace pcg_server
