#include "kb_service.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace pcg_server {
namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr double kBm25K1 = 1.5;
constexpr double kBm25B = 0.75;
constexpr size_t kMaxFileBytes = 2 * 1024 * 1024;

struct Chunk {
    std::string path;
    std::string heading;
    std::string text;
    std::string category;
    std::unordered_map<std::string, int> tf;
    int length = 0;
};

struct KbState {
    fs::path workspace_root;
    fs::path kb_root;
    std::vector<Chunk> chunks;
    std::unordered_map<std::string, int> df;
    double avg_length = 1.0;
    bool indexed = false;
    std::string last_error;
    std::mutex mu;
};

KbState& State() {
    static KbState state;
    return state;
}

bool IsAbsolutePath(const std::string& p) {
    if (p.empty()) return false;
    if (p[0] == '/' || p[0] == '\\') return true;
    if (p.size() > 2 && std::isalpha(static_cast<unsigned char>(p[0])) && p[1] == ':') return true;
    return false;
}

std::string ToLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool IsCjk(unsigned char a, unsigned char b) {
    return a >= 0xE4 && a <= 0xE9 && b >= 0x80 && b <= 0xBF;
}

std::vector<std::string> Tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::string cur;
    const auto flush = [&]() {
        if (cur.size() >= 2) tokens.push_back(cur);
        cur.clear();
    };
    const std::string lower = ToLower(input);
    for (size_t i = 0; i < lower.size();) {
        const unsigned char c = static_cast<unsigned char>(lower[i]);
        if (c < 0x80) {
            if (std::isalnum(c) || c == '_' || c == '-') {
                cur.push_back(static_cast<char>(c));
            } else {
                flush();
            }
            ++i;
            continue;
        }
        flush();
        size_t seq = 0;
        while (i + 2 < lower.size() && seq < 64) {
            const unsigned char a = static_cast<unsigned char>(lower[i]);
            const unsigned char b = static_cast<unsigned char>(lower[i + 1]);
            if (!IsCjk(a, b)) break;
            tokens.emplace_back(lower.substr(i, 3));
            if (i + 5 < lower.size()) {
                const unsigned char a2 = static_cast<unsigned char>(lower[i + 3]);
                const unsigned char b2 = static_cast<unsigned char>(lower[i + 4]);
                if (IsCjk(a2, b2)) tokens.emplace_back(lower.substr(i, 6));
            }
            i += 3;
            ++seq;
        }
        if (seq == 0) ++i;
    }
    flush();
    return tokens;
}

std::string ReadFile(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return {};
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    std::string content = buffer.str();
    if (content.size() > kMaxFileBytes) content.resize(kMaxFileBytes);
    return content;
}

std::string CategoryOf(const fs::path& relative) {
    const std::string first = relative.begin()->string();
    return first;
}

std::vector<Chunk> ChunkMarkdown(const fs::path& relative, const std::string& text) {
    std::vector<Chunk> out;
    std::istringstream stream(text);
    std::string line;
    std::string current_heading;
    std::string body;
    const auto flush_chunk = [&]() {
        if (body.size() < 16) { body.clear(); return; }
        Chunk chunk;
        chunk.path = relative.generic_string();
        chunk.heading = current_heading;
        chunk.text = body;
        chunk.category = CategoryOf(relative);
        for (const auto& tok : Tokenize(current_heading + " " + body)) {
            chunk.tf[tok] += 1;
            chunk.length += 1;
        }
        if (chunk.length > 0) out.push_back(std::move(chunk));
        body.clear();
    };
    while (std::getline(stream, line)) {
        if (line.rfind("#", 0) == 0) {
            flush_chunk();
            current_heading = line;
            continue;
        }
        body += line;
        body += '\n';
        if (body.size() > 1500) flush_chunk();
    }
    flush_chunk();
    return out;
}

void CollectMarkdown(const fs::path& dir, const fs::path& base, std::vector<fs::path>& out) {
    if (!fs::exists(dir)) return;
    std::error_code ec;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".md") continue;
        const auto rel = fs::relative(entry.path(), base, ec);
        if (ec) continue;
        out.push_back(rel);
    }
}

int ReindexLocked(KbState& state) {
    state.chunks.clear();
    state.df.clear();
    state.avg_length = 1.0;
    state.last_error.clear();
    if (state.kb_root.empty() || !fs::exists(state.kb_root)) {
        state.last_error = "kb_root_missing";
        state.indexed = false;
        return 0;
    }
    std::vector<fs::path> files;
    CollectMarkdown(state.kb_root / "rules", state.kb_root, files);
    CollectMarkdown(state.kb_root / "kb", state.kb_root, files);
    std::sort(files.begin(), files.end());
    long long total_length = 0;
    for (const auto& rel : files) {
        const auto text = ReadFile(state.kb_root / rel);
        if (text.empty()) continue;
        for (auto& chunk : ChunkMarkdown(rel, text)) {
            total_length += chunk.length;
            std::unordered_set<std::string> seen;
            for (const auto& [tok, _] : chunk.tf) seen.insert(tok);
            for (const auto& tok : seen) state.df[tok] += 1;
            state.chunks.push_back(std::move(chunk));
        }
    }
    if (!state.chunks.empty()) {
        state.avg_length = static_cast<double>(total_length) / state.chunks.size();
        if (state.avg_length < 1.0) state.avg_length = 1.0;
    }
    state.indexed = true;
    return static_cast<int>(state.chunks.size());
}

double Bm25Score(const KbState& state, const Chunk& chunk, const std::vector<std::string>& query_terms) {
    const int n_docs = static_cast<int>(state.chunks.size());
    double score = 0.0;
    for (const auto& term : query_terms) {
        const auto df_it = state.df.find(term);
        if (df_it == state.df.end()) continue;
        const auto tf_it = chunk.tf.find(term);
        if (tf_it == chunk.tf.end()) continue;
        const double idf = std::log(1.0 + (n_docs - df_it->second + 0.5) / (df_it->second + 0.5));
        const double tf = static_cast<double>(tf_it->second);
        const double norm = 1.0 - kBm25B + kBm25B * chunk.length / state.avg_length;
        score += idf * (tf * (kBm25K1 + 1.0)) / (tf + kBm25K1 * norm);
    }
    return score;
}

size_t Utf8SafePrefixLen(const std::string& s, size_t max_len) {
    if (s.size() <= max_len) return s.size();
    size_t end = max_len;
    while (end > 0 && (static_cast<unsigned char>(s[end]) & 0xC0) == 0x80) --end;
    if (end > 0 && (static_cast<unsigned char>(s[end - 1]) & 0x80) != 0) {
        size_t lead = end - 1;
        while (lead > 0 && (static_cast<unsigned char>(s[lead]) & 0xC0) == 0x80) --lead;
        const unsigned char lead_byte = static_cast<unsigned char>(s[lead]);
        size_t expected = 1;
        if ((lead_byte & 0xE0) == 0xC0) expected = 2;
        else if ((lead_byte & 0xF0) == 0xE0) expected = 3;
        else if ((lead_byte & 0xF8) == 0xF0) expected = 4;
        if (lead + expected > end) end = lead;
    }
    return end;
}

json ChunkToJson(const Chunk& chunk, double score) {
    std::string excerpt = chunk.text;
    if (excerpt.size() > 480) {
        excerpt = excerpt.substr(0, Utf8SafePrefixLen(excerpt, 480)) + "...";
    }
    return {
        {"path", chunk.path},
        {"heading", chunk.heading},
        {"category", chunk.category},
        {"score", score},
        {"excerpt", excerpt},
    };
}

bool IsSafeRelative(const std::string& p, const std::string& ext) {
    if (p.empty() || IsAbsolutePath(p)) return false;
    const fs::path path(p);
    if (!ext.empty() && path.extension() != ext) return false;
    for (const auto& component : path) {
        if (component == "..") return false;
    }
    return true;
}

void WriteJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

json ParamOrQuery(const httplib::Request& req, const char* name, const std::string& fallback = "") {
    if (req.has_param(name)) return req.get_param_value(name);
    if (!req.body.empty()) {
        const json body = json::parse(req.body, nullptr, false);
        if (!body.is_discarded() && body.is_object() && body.contains(name)) return body[name];
    }
    return fallback;
}

}  // namespace

void ConfigureKbRoot(const fs::path& workspace_root) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    state.workspace_root = workspace_root;
    state.kb_root = workspace_root / ".pcg-ai";
    state.indexed = false;
}

fs::path GetKbRoot() {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    return state.kb_root;
}

json KbStatus() {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    return {
        {"ok", !state.kb_root.empty()},
        {"kb_root", state.kb_root.generic_string()},
        {"indexed", state.indexed},
        {"chunks", state.chunks.size()},
        {"engine", "bm25"},
        {"last_error", state.last_error},
    };
}

json KbReindex() {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    const int count = ReindexLocked(state);
    return {
        {"ok", state.indexed},
        {"chunks", count},
        {"kb_root", state.kb_root.generic_string()},
        {"engine", "bm25"},
        {"last_error", state.last_error},
    };
}

json KbSearch(const std::string& query, int top_k, const std::string& category) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    if (!state.indexed) ReindexLocked(state);
    if (!state.indexed) return {{"ok", false}, {"error", state.last_error.empty() ? "index_unavailable" : state.last_error}};
    if (query.empty()) return {{"ok", false}, {"error", "query_required"}};
    if (top_k <= 0) top_k = 10;
    top_k = std::min(top_k, 50);
    const auto terms = Tokenize(query);
    std::vector<std::pair<double, size_t>> hits;
    for (size_t i = 0; i < state.chunks.size(); ++i) {
        const auto& chunk = state.chunks[i];
        if (!category.empty() && chunk.category != category) continue;
        const double score = Bm25Score(state, chunk, terms);
        if (score > 0.0) hits.emplace_back(score, i);
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    if (hits.size() > static_cast<size_t>(top_k)) hits.resize(top_k);
    json results = json::array();
    for (const auto& [score, idx] : hits) results.push_back(ChunkToJson(state.chunks[idx], score));
    return {
        {"ok", true},
        {"engine", "bm25"},
        {"query", query},
        {"top_k", top_k},
        {"total_hits", hits.size()},
        {"results", std::move(results)},
    };
}

json KbList(const std::string& category) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    if (state.kb_root.empty()) return {{"ok", false}, {"error", "kb_root_not_configured"}};
    std::vector<fs::path> files;
    CollectMarkdown(state.kb_root / "rules", state.kb_root, files);
    CollectMarkdown(state.kb_root / "kb", state.kb_root, files);
    std::sort(files.begin(), files.end());
    json out = json::array();
    for (const auto& rel : files) {
        const auto cat = CategoryOf(rel);
        if (!category.empty() && cat != category) continue;
        out.push_back({
            {"path", rel.generic_string()},
            {"category", cat},
        });
    }
    return {{"ok", true}, {"count", out.size()}, {"files", std::move(out)}};
}

json KbGet(const std::string& relative_path) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    if (state.kb_root.empty()) return {{"ok", false}, {"error", "kb_root_not_configured"}};
    if (!IsSafeRelative(relative_path, ".md")) {
        return {{"ok", false}, {"error", "path must be a .pcg-ai-relative .md without parent traversal"}};
    }
    const auto full = state.kb_root / relative_path;
    if (!fs::exists(full)) return {{"ok", false}, {"error", "not_found"}, {"path", relative_path}};
    return {
        {"ok", true},
        {"path", relative_path},
        {"content", ReadFile(full)},
    };
}

json KbGoldenGraphList(const std::string& class_filter) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    if (state.kb_root.empty()) return {{"ok", false}, {"error", "kb_root_not_configured"}};
    const auto dir = state.kb_root / "golden-graphs";
    json out = json::array();
    if (fs::exists(dir)) {
        std::error_code ec;
        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() != ".pcg") continue;
            const auto rel = fs::relative(entry.path(), dir, ec);
            if (ec) continue;
            const std::string cls = rel.parent_path().empty() ? "" : rel.parent_path().begin()->string();
            if (!class_filter.empty() && cls != class_filter) continue;
            out.push_back({
                {"name", entry.path().stem().string()},
                {"path", rel.generic_string()},
                {"class", cls},
                {"bytes", entry.file_size()},
            });
        }
    }
    std::sort(out.begin(), out.end(), [](const json& a, const json& b) {
        return a.value("path", "") < b.value("path", "");
    });
    return {{"ok", true}, {"count", out.size()}, {"graphs", std::move(out)}};
}

json KbGoldenGraphGet(const std::string& name) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mu);
    if (state.kb_root.empty()) return {{"ok", false}, {"error", "kb_root_not_configured"}};
    if (name.empty() || IsAbsolutePath(name) || name.find("..") != std::string::npos) {
        return {{"ok", false}, {"error", "name must be a stem or relative .pcg path without parent traversal"}};
    }
    const auto dir = state.kb_root / "golden-graphs";
    if (!fs::exists(dir)) return {{"ok", false}, {"error", "golden_graphs_missing"}};
    std::error_code ec;
    fs::path found;
    for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().extension() != ".pcg") continue;
        const auto rel = fs::relative(entry.path(), dir, ec);
        if (ec) continue;
        if (entry.path().stem().string() == name || rel.generic_string() == name) {
            found = entry.path();
            break;
        }
    }
    if (found.empty()) return {{"ok", false}, {"error", "not_found"}, {"name", name}};
    const auto content = ReadFile(found);
    const json parsed = json::parse(content, nullptr, false);
    return {
        {"ok", true},
        {"name", found.stem().string()},
        {"path", fs::relative(found, dir).generic_string()},
        {"bytes", content.size()},
        {"valid_json", !parsed.is_discarded()},
        {"content", content},
    };
}

void HandleKbStatus(const httplib::Request&, httplib::Response& res) {
    WriteJson(res, KbStatus());
}

void HandleKbReindex(const httplib::Request&, httplib::Response& res) {
    WriteJson(res, KbReindex());
}

void HandleKbSearch(const httplib::Request& req, httplib::Response& res) {
    const std::string query = ParamOrQuery(req, "query");
    const std::string category = ParamOrQuery(req, "category");
    int top_k = 10;
    const auto tk = ParamOrQuery(req, "top_k");
    if (tk.is_number_integer()) top_k = tk.get<int>();
    else if (tk.is_string()) top_k = std::atoi(tk.get<std::string>().c_str());
    WriteJson(res, KbSearch(query, top_k, category));
}

void HandleKbList(const httplib::Request& req, httplib::Response& res) {
    WriteJson(res, KbList(ParamOrQuery(req, "category")));
}

void HandleKbGet(const httplib::Request& req, httplib::Response& res) {
    std::string path = ParamOrQuery(req, "path");
    if (path.empty()) path = ParamOrQuery(req, "relative_path");
    WriteJson(res, KbGet(path));
}

void HandleKbGoldenGraphList(const httplib::Request& req, httplib::Response& res) {
    std::string cls = ParamOrQuery(req, "class");
    if (cls.empty()) cls = ParamOrQuery(req, "class_filter");
    WriteJson(res, KbGoldenGraphList(cls));
}

void HandleKbGoldenGraphGet(const httplib::Request& req, httplib::Response& res) {
    WriteJson(res, KbGoldenGraphGet(ParamOrQuery(req, "name")));
}

}  // namespace pcg_server
