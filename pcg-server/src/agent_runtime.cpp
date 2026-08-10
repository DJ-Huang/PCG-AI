#include "agent_runtime.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#include <Security/Security.h>
#include <sys/stat.h>
#endif

#include "agent_service.hpp"
#include "mcp_service.hpp"
#include "session_service.hpp"

namespace pcg_server {
namespace {

using json = nlohmann::json;
using Clock = std::chrono::system_clock;

constexpr size_t kMaxAttachmentBytes = 10 * 1024 * 1024;
constexpr size_t kMaxAttachmentTotalBytes = 24 * 1024 * 1024;
constexpr size_t kMaxAttachments = 8;
constexpr int kMaxToolRounds = 12;
constexpr int kMaxToolCalls = 32;
constexpr int kTurnTimeoutSeconds = 300;
constexpr const char* kKeychainService = "PCG-AI Agent";

struct HttpResult {
    long status = 0;
    std::string body;
    std::string error;
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

struct OAuthAttempt {
    std::string id;
    std::string provider;
    std::string status = "pending";
    std::string url;
    std::string user_code;
    std::string device_code;
    std::string verifier;
    std::string state;
    std::string error;
    int interval_seconds = 5;
    int64_t expires_at = 0;
};

struct TurnState {
    std::mutex mutex;
    std::atomic<bool> cancelled{false};
    std::string id;
    std::string session_id;
    std::string provider;
    std::string model;
    json history = json::array();
    json pending = json::array();
    int tool_rounds = 0;
    int tool_calls = 0;
    int64_t started_at = 0;
};

struct RuntimeState {
    std::mutex mutex;
    bool loaded = false;
    std::string provider_id;
    std::string model_id;
    std::string custom_base_url;
    json provider_meta = json::object();
    std::unordered_map<std::string, OAuthAttempt> oauth;
    std::unordered_map<std::string, std::shared_ptr<TurnState>> turns;
    std::unordered_map<std::string, json> sessions;
    std::unordered_map<std::string, std::shared_ptr<std::mutex>> refresh_mutexes;
    int server_port = 17890;
};

RuntimeState& State() {
    static RuntimeState state;
    return state;
}

int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        Clock::now().time_since_epoch()).count();
}

std::string RandomId(const std::string& prefix) {
    std::random_device rd;
    std::ostringstream out;
    out << prefix << '-';
    for (int i = 0; i < 4; ++i) {
        out << std::hex << std::setw(8) << std::setfill('0') << rd();
    }
    return out.str();
}

std::string EncodeBase64(const std::string& input) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    for (size_t i = 0; i < input.size(); i += 3) {
        const uint32_t a = static_cast<unsigned char>(input[i]);
        const uint32_t b = i + 1 < input.size() ? static_cast<unsigned char>(input[i + 1]) : 0;
        const uint32_t c = i + 2 < input.size() ? static_cast<unsigned char>(input[i + 2]) : 0;
        const uint32_t value = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(value >> 18) & 63]);
        output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(i + 1 < input.size() ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(i + 2 < input.size() ? alphabet[value & 63] : '=');
    }
    return output;
}

std::string Base64Url(const std::string& input) {
    std::string value = EncodeBase64(input);
    std::replace(value.begin(), value.end(), '+', '-');
    std::replace(value.begin(), value.end(), '/', '_');
    while (!value.empty() && value.back() == '=') value.pop_back();
    return value;
}

std::string DecodeBase64Url(std::string input) {
    std::replace(input.begin(), input.end(), '-', '+');
    std::replace(input.begin(), input.end(), '_', '/');
    while (input.size() % 4 != 0) input.push_back('=');
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    uint32_t accumulator = 0;
    int bits = 0;
    for (const unsigned char c : input) {
        if (c == '=') break;
        const auto position = alphabet.find(static_cast<char>(c));
        if (position == std::string::npos) return {};
        accumulator = (accumulator << 6) | static_cast<uint32_t>(position);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<char>((accumulator >> bits) & 0xff));
        }
    }
    return output;
}

json JwtClaims(const std::string& token) {
    const size_t first = token.find('.');
    const size_t second = first == std::string::npos ? std::string::npos : token.find('.', first + 1);
    if (first == std::string::npos || second == std::string::npos) return json::object();
    json claims = json::parse(DecodeBase64Url(token.substr(first + 1, second - first - 1)), nullptr, false);
    return claims.is_object() ? claims : json::object();
}

json EnrichOAuthCredential(json credential, const json& token_response) {
    const std::string identity_token = token_response.value(
        "id_token", token_response.value("access_token", credential.value("access", "")));
    const json claims = JwtClaims(identity_token);
    std::string account_id = claims.value("chatgpt_account_id", "");
    const json auth_claim = claims.value("https://api.openai.com/auth", json::object());
    if (account_id.empty() && auth_claim.is_object()) account_id = auth_claim.value("chatgpt_account_id", "");
    if (account_id.empty()) {
        const json organizations = claims.value("organizations", json::array());
        if (organizations.is_array() && !organizations.empty()) account_id = organizations[0].value("id", "");
    }
    if (!account_id.empty()) credential["accountId"] = account_id;
    const std::string email = claims.value("email", "");
    if (!email.empty()) credential["accountLabel"] = email;
    return credential;
}

std::string RandomUrlToken(size_t bytes = 32) {
    std::string raw(bytes, '\0');
#ifdef __APPLE__
    if (SecRandomCopyBytes(kSecRandomDefault, bytes,
            reinterpret_cast<uint8_t*>(raw.data())) != errSecSuccess) {
        raw.clear();
    }
#endif
    if (raw.empty()) {
        std::random_device rd;
        raw.resize(bytes);
        for (char& value : raw) value = static_cast<char>(rd());
    }
    return Base64Url(raw);
}

std::string Sha256Base64Url(const std::string& input) {
#ifdef __APPLE__
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(input.data(), static_cast<CC_LONG>(input.size()), digest);
    return Base64Url(std::string(reinterpret_cast<char*>(digest), sizeof(digest)));
#else
    return {};
#endif
}

std::filesystem::path ConfigPath() {
    if (const char* override_path = std::getenv("PCG_AGENT_CONFIG_PATH")) {
        if (*override_path) return std::filesystem::path(override_path);
    }
    const char* home = std::getenv("HOME");
    const std::filesystem::path root = home && *home
        ? std::filesystem::path(home)
        : std::filesystem::temp_directory_path();
    return root / "Library" / "Application Support" / "PCG-AI" / "agent.json";
}

const char* KeychainService() {
    const char* override_service = std::getenv("PCG_AGENT_KEYCHAIN_SERVICE");
    return override_service && *override_service ? override_service : kKeychainService;
}

void SaveConfigLocked(RuntimeState& state) {
    const auto path = ConfigPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    const json value = {
        {"providerId", state.provider_id},
        {"modelId", state.model_id},
        {"customBaseUrl", state.custom_base_url},
        {"providers", state.provider_meta},
    };
    std::ofstream output(path, std::ios::trunc);
    if (!output) return;
    output << value.dump(2);
    output.close();
#ifdef __APPLE__
    chmod(path.c_str(), S_IRUSR | S_IWUSR);
#endif
}

void LoadConfigLocked(RuntimeState& state) {
    if (state.loaded) return;
    state.loaded = true;
    std::ifstream input(ConfigPath());
    if (!input) return;
    json value = json::parse(input, nullptr, false);
    if (!value.is_object()) return;
    state.provider_id = value.value("providerId", "");
    state.model_id = value.value("modelId", "");
    state.custom_base_url = value.value("customBaseUrl", "");
    state.provider_meta = value.value("providers", json::object());
    if (!state.provider_meta.is_object()) state.provider_meta = json::object();
}

bool CredentialGet(const std::string& provider, json& value) {
#ifdef __APPLE__
    CFStringRef service = CFStringCreateWithCString(
        kCFAllocatorDefault, KeychainService(), kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(provider.data()),
        static_cast<CFIndex>(provider.size()), kCFStringEncodingUTF8, false);
    if (!service || !account) {
        if (service) CFRelease(service);
        if (account) CFRelease(account);
        return false;
    }
    const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecReturnData, kSecMatchLimit};
    const void* values[] = {kSecClassGenericPassword, service, account, kCFBooleanTrue, kSecMatchLimitOne};
    CFDictionaryRef query = CFDictionaryCreate(
        kCFAllocatorDefault, keys, values, 5,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFTypeRef found = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &found);
    CFRelease(query);
    CFRelease(service);
    CFRelease(account);
    if (status != errSecSuccess || !found || CFGetTypeID(found) != CFDataGetTypeID()) {
        if (found) CFRelease(found);
        return false;
    }
    const auto data = static_cast<CFDataRef>(found);
    const std::string raw(
        reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
        static_cast<size_t>(CFDataGetLength(data)));
    CFRelease(found);
    value = json::parse(raw, nullptr, false);
    return value.is_object();
#else
    (void)provider;
    (void)value;
    return false;
#endif
}

bool CredentialSet(const std::string& provider, const json& value) {
#ifdef __APPLE__
    const std::string raw = value.dump();
    CFStringRef service = CFStringCreateWithCString(
        kCFAllocatorDefault, KeychainService(), kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(provider.data()),
        static_cast<CFIndex>(provider.size()), kCFStringEncodingUTF8, false);
    CFDataRef data = CFDataCreate(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(raw.data()),
        static_cast<CFIndex>(raw.size()));
    if (!service || !account || !data) {
        if (service) CFRelease(service);
        if (account) CFRelease(account);
        if (data) CFRelease(data);
        return false;
    }
    const void* query_keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
    const void* query_values[] = {kSecClassGenericPassword, service, account};
    CFDictionaryRef query = CFDictionaryCreate(
        kCFAllocatorDefault, query_keys, query_values, 3,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const void* update_keys[] = {kSecValueData};
    const void* update_values[] = {data};
    CFDictionaryRef update = CFDictionaryCreate(
        kCFAllocatorDefault, update_keys, update_values, 1,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    OSStatus status = SecItemUpdate(query, update);
    if (status == errSecItemNotFound) {
        const void* add_keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData};
        const void* add_values[] = {kSecClassGenericPassword, service, account, data};
        CFDictionaryRef add = CFDictionaryCreate(
            kCFAllocatorDefault, add_keys, add_values, 4,
            &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        status = SecItemAdd(add, nullptr);
        CFRelease(add);
    }
    CFRelease(update);
    CFRelease(query);
    CFRelease(data);
    CFRelease(account);
    CFRelease(service);
    return status == errSecSuccess;
#else
    (void)provider;
    (void)value;
    return false;
#endif
}

bool CredentialDelete(const std::string& provider) {
#ifdef __APPLE__
    CFStringRef service = CFStringCreateWithCString(
        kCFAllocatorDefault, KeychainService(), kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(provider.data()),
        static_cast<CFIndex>(provider.size()), kCFStringEncodingUTF8, false);
    if (!service || !account) {
        if (service) CFRelease(service);
        if (account) CFRelease(account);
        return false;
    }
    const void* keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
    const void* values[] = {kSecClassGenericPassword, service, account};
    CFDictionaryRef query = CFDictionaryCreate(
        kCFAllocatorDefault, keys, values, 3,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    CFRelease(account);
    CFRelease(service);
    return status == errSecSuccess || status == errSecItemNotFound;
#else
    (void)provider;
    return false;
#endif
}

std::string UrlEncode(const std::string& value);

std::string OAuthRedirectUri(const std::string& attempt_id) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    return "http://localhost:" + std::to_string(state.server_port) +
        "/v1/agent/oauth/callback/openai?attemptId=" + UrlEncode(attempt_id);
}

size_t CurlWrite(char* data, size_t size, size_t count, void* user) {
    const size_t bytes = size * count;
    static_cast<std::string*>(user)->append(data, bytes);
    return bytes;
}

int CurlProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* cancelled = static_cast<const std::atomic<bool>*>(user);
    return cancelled && cancelled->load() ? 1 : 0;
}

std::string UrlEncode(const std::string& value) {
    CURL* curl = curl_easy_init();
    if (!curl) return {};
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    const std::string result = encoded ? encoded : "";
    if (encoded) curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

HttpResult Http(
    const std::string& method,
    const std::string& url,
    const std::vector<std::string>& headers = {},
    const std::string& body = {},
    int timeout_seconds = 30,
    const std::atomic<bool>* cancelled = nullptr) {
    static const int initialized = [] { return curl_global_init(CURL_GLOBAL_DEFAULT); }();
    (void)initialized;
    HttpResult result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "http_client_unavailable";
        return result;
    }
    curl_slist* header_list = nullptr;
    for (const auto& header : headers) header_list = curl_slist_append(header_list, header.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PCG-AI-Agent/1.0");
    if (!body.empty() || method == "POST" || method == "PUT") {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    }
    if (cancelled) {
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancelled);
    }
    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) result.error = cancelled && cancelled->load()
        ? "cancelled"
        : curl_easy_strerror(code);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return result;
}

void JsonResponse(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

json ErrorBody(const std::string& code, const std::string& message, bool retryable = false) {
    return {{"ok", false}, {"error", {{"code", code}, {"message", message}, {"retryable", retryable}}}};
}

std::string Match(const httplib::Request& req, size_t index) {
    return req.matches.size() > index ? req.matches[index].str() : "";
}

bool ParseObjectBody(const httplib::Request& req, httplib::Response& res, json& value) {
    value = json::parse(req.body, nullptr, false);
    if (value.is_object()) return true;
    JsonResponse(res, 400, ErrorBody("invalid_request", "Expected a JSON object."));
    return false;
}

bool IsSupportedKeyProvider(const std::string& id) {
    return id == "openai" || id == "anthropic" || id == "google" ||
           id == "openrouter" || id == "kimi-coding" || id == "openai-compatible";
}

std::string NormalizeBaseUrl(const std::string& raw) {
    if (raw.empty() || raw.find('@') != std::string::npos || raw.find('\\') != std::string::npos ||
        raw.find('?') != std::string::npos || raw.find('#') != std::string::npos ||
        std::any_of(raw.begin(), raw.end(), [](unsigned char c) { return std::isspace(c) != 0; })) return {};
    const size_t scheme_end = raw.find("://");
    if (scheme_end == std::string::npos) return {};
    const std::string scheme = raw.substr(0, scheme_end);
    const size_t authority_start = scheme_end + 3;
    const size_t authority_end = raw.find('/', authority_start);
    std::string authority = raw.substr(authority_start, authority_end - authority_start);
    std::string lowered_authority = authority;
    std::transform(lowered_authority.begin(), lowered_authority.end(), lowered_authority.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (authority.empty() || lowered_authority.find("%40") != std::string::npos) return {};
    const bool https = scheme == "https";
    const bool loopback = scheme == "http" && std::regex_match(
        lowered_authority, std::regex(R"((localhost|127\.0\.0\.1|\[::1\])(:[0-9]{1,5})?)"));
    if (!https && !loopback) return {};
    std::string value = raw;
    while (value.size() > 8 && value.back() == '/') value.pop_back();
    return value;
}

std::string ProviderBaseUrl(const std::string& id) {
    if (id == "openai") return "https://api.openai.com/v1";
    if (id == "anthropic") return "https://api.anthropic.com/v1";
    if (id == "google") return "https://generativelanguage.googleapis.com/v1beta";
    if (id == "openrouter") return "https://openrouter.ai/api/v1";
    if (id == "kimi-coding") return "https://api.kimi.com/coding/v1";
    if (id == "github-copilot") return "https://api.githubcopilot.com";
    if (id == "xai") return "https://api.x.ai/v1";
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    LoadConfigLocked(state);
    return id == "openai-compatible" ? state.custom_base_url : "";
}

json Model(const std::string& id, const std::string& name, bool image = true) {
    return {
        {"id", id}, {"name", name.empty() ? id : name},
        {"capabilities", {
            {"toolCall", true}, {"textInput", true}, {"imageInput", image},
            {"contextTokens", 0}, {"outputTokens", 0},
        }},
    };
}

json ParseModels(const std::string& provider, const json& body) {
    json models = json::array();
    if (provider == "google") {
        for (const auto& item : body.value("models", json::array())) {
            if (!item.is_object()) continue;
            std::string id = item.value("name", "");
            if (id.rfind("models/", 0) == 0) id = id.substr(7);
            const auto methods = item.value("supportedGenerationMethods", std::vector<std::string>{});
            if (std::find(methods.begin(), methods.end(), "generateContent") == methods.end()) continue;
            models.push_back(Model(id, item.value("displayName", id), id.find("gemini") != std::string::npos));
        }
        return models;
    }
    const json items = body.value("data", json::array());
    if (!items.is_array()) return models;
    for (const auto& item : items) {
        if (!item.is_object()) continue;
        const std::string id = item.value("id", "");
        if (id.empty()) continue;
        const bool image = id.find("embedding") == std::string::npos;
        models.push_back(Model(id, item.value("display_name", item.value("name", id)), image));
    }
    return models;
}

std::vector<std::string> CredentialHeaders(const std::string& provider, const json& credential) {
    const std::string type = credential.value("type", "");
    const std::string key = type == "api" ? credential.value("key", "") : credential.value("access", "");
    if (provider == "anthropic" || provider == "kimi-coding") {
        return {"x-api-key: " + key, "anthropic-version: 2023-06-01", "Content-Type: application/json"};
    }
    if (provider == "google") return {"x-goog-api-key: " + key, "Content-Type: application/json"};
    std::vector<std::string> headers = {"Authorization: Bearer " + key, "Content-Type: application/json"};
    if (provider == "openrouter") {
        headers.push_back("HTTP-Referer: http://127.0.0.1");
        headers.push_back("X-Title: PCG-AI");
    }
    if (provider == "github-copilot") {
        headers.push_back("Openai-Intent: conversation-edits");
        headers.push_back("X-GitHub-Api-Version: 2025-04-01");
    }
    if (provider == "openai" && type == "oauth" && credential.contains("accountId")) {
        headers.push_back("ChatGPT-Account-Id: " + credential.value("accountId", ""));
    }
    return headers;
}

bool EnsureFreshOAuthCredential(const std::string& provider, json& credential, json* error = nullptr) {
    if (credential.value("type", "") != "oauth") return true;
    const int64_t expires = credential.value("expires", 0LL);
    if (expires == 0 || expires > NowMs() + 120000) return true;
    if (provider != "openai" && provider != "xai") return true;
    const std::string refresh = credential.value("refresh", "");
    if (refresh.empty()) {
        if (error) *error = ErrorBody("oauth_expired", "OAuth credential expired and has no refresh token.");
        return false;
    }
    std::shared_ptr<std::mutex> refresh_mutex;
    {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        auto& slot = state.refresh_mutexes[provider];
        if (!slot) slot = std::make_shared<std::mutex>();
        refresh_mutex = slot;
    }
    std::lock_guard<std::mutex> refresh_lock(*refresh_mutex);
    json current;
    if (!CredentialGet(provider, current)) {
        if (error) *error = ErrorBody("not_connected", "Provider credential is no longer available.");
        return false;
    }
    if (current.value("expires", 0LL) > NowMs() + 120000) {
        credential = std::move(current);
        return true;
    }
    const char* client = std::getenv(provider == "openai"
        ? "PCG_OPENAI_OAUTH_CLIENT_ID" : "PCG_XAI_OAUTH_CLIENT_ID");
    if (!client || !*client) {
        if (error) *error = ErrorBody("oauth_refresh_unavailable", "OAuth Client ID is unavailable for token refresh.");
        return false;
    }
    const std::string token_url = provider == "openai"
        ? "https://auth.openai.com/oauth/token" : "https://auth.x.ai/oauth2/token";
    const std::string body = "grant_type=refresh_token&refresh_token=" +
        UrlEncode(current.value("refresh", "")) + "&client_id=" + UrlEncode(client);
    const HttpResult response = Http("POST", token_url,
        {"Accept: application/json", "Content-Type: application/x-www-form-urlencoded"}, body);
    const json parsed = json::parse(response.body, nullptr, false);
    if (!response.ok() || !parsed.is_object() || !parsed.contains("access_token")) {
        if (error) *error = ErrorBody("oauth_refresh_failed", "Provider rejected the OAuth token refresh.", true);
        return false;
    }
    current["access"] = parsed.value("access_token", "");
    current["refresh"] = parsed.value("refresh_token", current.value("refresh", ""));
    current["expires"] = NowMs() + parsed.value("expires_in", 3600) * 1000LL;
    current = EnrichOAuthCredential(std::move(current), parsed);
    if (!CredentialSet(provider, current)) {
        if (error) *error = ErrorBody("keychain_write_failed", "Could not update the refreshed credential in macOS Keychain.");
        return false;
    }
    credential = std::move(current);
    return true;
}

json ValidateCredential(const std::string& provider, const json& credential) {
    if (provider == "openai" && credential.value("type", "") == "oauth") {
        return {{"ok", true}, {"models", json::array({Model("gpt-5.4", "GPT-5.4"), Model("gpt-5.4-mini", "GPT-5.4 Mini")})}};
    }
    std::string url = ProviderBaseUrl(provider);
    if (url.empty()) return ErrorBody("invalid_base_url", "A valid HTTPS or loopback Base URL is required.");
    if (provider == "google") url += "/models";
    else url += "/models";
    HttpResult response = Http("GET", url, CredentialHeaders(provider, credential));
    if (!response.ok()) {
        const std::string code = response.status == 401 || response.status == 403
            ? "invalid_credential"
            : response.status == 429 ? "rate_limited" : "provider_unavailable";
        return ErrorBody(code, "Provider validation failed (HTTP " + std::to_string(response.status) + ").", response.status >= 500);
    }
    const json parsed = json::parse(response.body, nullptr, false);
    if (!parsed.is_object()) return ErrorBody("invalid_provider_response", "Provider returned invalid model data.");
    json models = ParseModels(provider, parsed);
    if (models.empty()) return ErrorBody("no_models", "Provider authentication succeeded but no usable models were returned.");
    return {{"ok", true}, {"models", std::move(models)}};
}

void StoreValidation(const std::string& provider, const json& validation) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    LoadConfigLocked(state);
    json& meta = state.provider_meta[provider];
    std::string status = "connected";
    if (!validation.value("ok", false)) {
        const std::string code = validation.value("error", json::object()).value("code", "");
        status = code == "oauth_expired" ? "expired" :
            code == "provider_unavailable" ? "unavailable" : "invalid";
    }
    meta["status"] = status;
    if (validation.contains("models")) meta["models"] = validation["models"];
    if (validation.contains("error")) meta["error"] = validation["error"];
    else meta.erase("error");
    if (state.provider_id.empty() && validation.value("ok", false)) {
        state.provider_id = provider;
        if (validation["models"].is_array() && !validation["models"].empty()) {
            state.model_id = validation["models"][0].value("id", "");
        }
    }
    SaveConfigLocked(state);
}

json AuthMethods(const std::string& provider) {
    json methods = json::array();
    if (IsSupportedKeyProvider(provider)) methods.push_back({{"type", "api"}, {"label", "API Key"}, {"available", true}});
    const char* env = nullptr;
    std::string label;
    std::string flow;
    if (provider == "openai") {
        env = std::getenv("PCG_OPENAI_OAUTH_CLIENT_ID");
        label = "ChatGPT Plus/Pro";
        flow = "browser";
    } else if (provider == "github-copilot") {
        env = std::getenv("PCG_GITHUB_OAUTH_CLIENT_ID");
        label = "GitHub Copilot";
        flow = "device";
    } else if (provider == "xai") {
        env = std::getenv("PCG_XAI_OAUTH_CLIENT_ID");
        label = "xAI SuperGrok";
        flow = "device";
    }
    if (!label.empty()) methods.push_back({
        {"type", "oauth"}, {"label", label}, {"flow", flow},
        {"available", env && *env},
        {"unavailableReason", env && *env ? "" : "PCG-owned OAuth Client ID is not configured"},
    });
    return methods;
}

json PublicProviders() {
    static const std::vector<std::pair<std::string, std::string>> providers = {
        {"openai", "OpenAI"}, {"anthropic", "Anthropic"}, {"google", "Google Gemini"},
        {"openrouter", "OpenRouter"}, {"kimi-coding", "Kimi for Coding"},
        {"openai-compatible", "OpenAI Compatible"},
        {"github-copilot", "GitHub Copilot"}, {"xai", "xAI"},
    };
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    LoadConfigLocked(state);
    json result = json::array();
    for (const auto& entry : providers) {
        json credential;
        const bool connected = CredentialGet(entry.first, credential);
        const json meta = state.provider_meta.value(entry.first, json::object());
        result.push_back({
            {"id", entry.first}, {"name", entry.second},
            {"authMethods", AuthMethods(entry.first)},
            {"connection", {
                {"status", connected ? meta.value("status", "connected") : "unavailable"},
                {"authType", connected ? credential.value("type", "") : ""},
                {"accountLabel", connected ? credential.value("accountLabel", "") : ""},
                {"error", meta.value("error", json())},
            }},
            {"models", meta.value("models", json::array())},
            {"baseUrl", entry.first == "openai-compatible" ? state.custom_base_url : ""},
        });
    }
    return result;
}

std::string Sse(const std::string& event, const json& data) {
    return "event: " + event + "\ndata: " + data.dump() + "\n\n";
}

json AgentTools() {
    json result = json::array();
    for (const auto& tool : GetPcgToolDefinitions()) {
        result.push_back({
            {"type", "function"},
            {"function", {
                {"name", tool.value("name", "")},
                {"description", tool.value("description", "")},
                {"parameters", tool.value("inputSchema", json::object())},
            }},
        });
    }
    return result;
}

const char* SystemPrompt() {
    return
        "You are the embedded PCG-AI graph authoring agent. Always inspect the live editor context and "
        "manifest-backed node types before authoring. Never invent node types, properties, or pin IDs. "
        "Every write must use the latest graphHash. Prefer one atomic pcg_apply_graph_ops batch. "
        "After edits, validate, cook, and capture the preview. On conflicts or timeouts, refresh context "
        "instead of replaying a stale write. Explain failures plainly.";
}

json OpenAiMessages(const json& history) {
    json messages = json::array({{{"role", "system"}, {"content", SystemPrompt()}}});
    for (const auto& message : history) {
        const std::string role = message.value("role", "");
        if (role == "user") {
            json content = json::array();
            for (const auto& part : message.value("content", json::array())) {
                if (part.value("type", "") == "text") content.push_back({{"type", "text"}, {"text", part.value("text", "")}});
                if (part.value("type", "") == "image") content.push_back({
                    {"type", "image_url"},
                    {"image_url", {{"url", "data:" + part.value("mimeType", "image/png") + ";base64," + part.value("data", "")}}},
                });
            }
            messages.push_back({{"role", "user"}, {"content", std::move(content)}});
        } else if (role == "assistant") {
            json item = {{"role", "assistant"}, {"content", message.value("text", "")}};
            if (message.contains("toolCalls")) {
                item["tool_calls"] = json::array();
                for (const auto& call : message["toolCalls"]) {
                    item["tool_calls"].push_back({
                        {"id", call.value("id", "")}, {"type", "function"},
                        {"function", {{"name", call.value("name", "")}, {"arguments", call.value("arguments", json::object()).dump()}}},
                    });
                }
            }
            messages.push_back(std::move(item));
        } else if (role == "tool") {
            messages.push_back({
                {"role", "tool"}, {"tool_call_id", message.value("toolCallId", "")},
                {"content", message.value("content", "")},
            });
        }
    }
    return messages;
}

json OpenAiResponseInput(const json& history) {
    json input = json::array();
    for (const auto& message : history) {
        const std::string role = message.value("role", "");
        if (role == "user") {
            json content = json::array();
            for (const auto& part : message.value("content", json::array())) {
                if (part.value("type", "") == "text") {
                    content.push_back({{"type", "input_text"}, {"text", part.value("text", "")}});
                } else if (part.value("type", "") == "image") {
                    content.push_back({
                        {"type", "input_image"},
                        {"image_url", "data:" + part.value("mimeType", "image/png") + ";base64," + part.value("data", "")},
                    });
                }
            }
            input.push_back({{"role", "user"}, {"content", std::move(content)}});
        } else if (role == "assistant") {
            if (!message.value("text", "").empty()) {
                input.push_back({{"role", "assistant"}, {"content", json::array({{
                    {"type", "output_text"}, {"text", message.value("text", "")},
                }})}});
            }
            for (const auto& call : message.value("toolCalls", json::array())) {
                input.push_back({
                    {"type", "function_call"}, {"call_id", call.value("id", "")},
                    {"name", call.value("name", "")},
                    {"arguments", call.value("arguments", json::object()).dump()},
                });
            }
        } else if (role == "tool") {
            input.push_back({
                {"type", "function_call_output"},
                {"call_id", message.value("toolCallId", "")},
                {"output", message.value("content", "")},
            });
        }
    }
    return input;
}

json OpenAiResponseTools() {
    json tools = json::array();
    for (const auto& tool : GetPcgToolDefinitions()) {
        tools.push_back({
            {"type", "function"}, {"name", tool.value("name", "")},
            {"description", tool.value("description", "")},
            {"parameters", tool.value("inputSchema", json::object())},
            {"strict", false},
        });
    }
    return tools;
}

json AnthropicMessages(const json& history) {
    json messages = json::array();
    for (const auto& message : history) {
        const std::string role = message.value("role", "");
        if (role == "user") {
            json content = json::array();
            for (const auto& part : message.value("content", json::array())) {
                if (part.value("type", "") == "text") content.push_back({{"type", "text"}, {"text", part.value("text", "")}});
                if (part.value("type", "") == "image") content.push_back({
                    {"type", "image"},
                    {"source", {{"type", "base64"}, {"media_type", part.value("mimeType", "image/png")}, {"data", part.value("data", "")}}},
                });
            }
            messages.push_back({{"role", "user"}, {"content", std::move(content)}});
        } else if (role == "assistant") {
            json content = json::array();
            if (!message.value("text", "").empty()) content.push_back({{"type", "text"}, {"text", message.value("text", "")}});
            for (const auto& call : message.value("toolCalls", json::array())) content.push_back({
                {"type", "tool_use"}, {"id", call.value("id", "")},
                {"name", call.value("name", "")}, {"input", call.value("arguments", json::object())},
            });
            messages.push_back({{"role", "assistant"}, {"content", std::move(content)}});
        } else if (role == "tool") {
            messages.push_back({{"role", "user"}, {"content", json::array({{
                {"type", "tool_result"}, {"tool_use_id", message.value("toolCallId", "")},
                {"content", message.value("content", "")}, {"is_error", message.value("isError", false)},
            }})}});
        }
    }
    return messages;
}

json GeminiContents(const json& history) {
    json contents = json::array();
    for (const auto& message : history) {
        const std::string role = message.value("role", "");
        if (role == "user") {
            json parts = json::array();
            for (const auto& part : message.value("content", json::array())) {
                if (part.value("type", "") == "text") parts.push_back({{"text", part.value("text", "")}});
                if (part.value("type", "") == "image") parts.push_back({{"inlineData", {
                    {"mimeType", part.value("mimeType", "image/png")}, {"data", part.value("data", "")},
                }}});
            }
            contents.push_back({{"role", "user"}, {"parts", std::move(parts)}});
        } else if (role == "assistant") {
            json parts = json::array();
            if (!message.value("text", "").empty()) parts.push_back({{"text", message.value("text", "")}});
            for (const auto& call : message.value("toolCalls", json::array())) parts.push_back({{"functionCall", {
                {"name", call.value("name", "")}, {"args", call.value("arguments", json::object())},
            }}});
            contents.push_back({{"role", "model"}, {"parts", std::move(parts)}});
        } else if (role == "tool") {
            contents.push_back({{"role", "user"}, {"parts", json::array({{{"functionResponse", {
                {"name", message.value("name", "")},
                {"response", {{"result", message.value("content", "")}}},
            }}}})}});
        }
    }
    return contents;
}

using EventEmitter = std::function<bool(const std::string&, const json&)>;

json NormalizeCompletion(const std::string& provider, const json& body) {
    json result = {{"text", ""}, {"toolCalls", json::array()}};
    if (provider == "openai-responses") {
        for (const auto& item : body.value("output", json::array())) {
            const std::string type = item.value("type", "");
            if (type == "message") {
                for (const auto& part : item.value("content", json::array())) {
                    if (part.value("type", "") == "output_text") {
                        result["text"] = result.value("text", "") + part.value("text", "");
                    }
                }
            } else if (type == "function_call") {
                json arguments = json::parse(item.value("arguments", "{}"), nullptr, false);
                if (!arguments.is_object()) arguments = json::object();
                result["toolCalls"].push_back({
                    {"id", item.value("call_id", item.value("id", RandomId("call")))},
                    {"name", item.value("name", "")}, {"arguments", std::move(arguments)},
                });
            }
        }
        return result;
    }
    if (provider == "anthropic") {
        for (const auto& part : body.value("content", json::array())) {
            if (part.value("type", "") == "text") result["text"] = result.value("text", "") + part.value("text", "");
            if (part.value("type", "") == "tool_use") result["toolCalls"].push_back({
                {"id", part.value("id", RandomId("call"))}, {"name", part.value("name", "")},
                {"arguments", part.value("input", json::object())},
            });
        }
        return result;
    }
    if (provider == "google") {
        const auto candidates = body.value("candidates", json::array());
        if (candidates.empty()) return result;
        for (const auto& part : candidates[0].value("content", json::object()).value("parts", json::array())) {
            if (part.contains("text")) result["text"] = result.value("text", "") + part.value("text", "");
            if (part.contains("functionCall")) {
                const json call = part["functionCall"];
                result["toolCalls"].push_back({
                    {"id", RandomId("call")}, {"name", call.value("name", "")},
                    {"arguments", call.value("args", json::object())},
                });
            }
        }
        return result;
    }
    const auto choices = body.value("choices", json::array());
    if (choices.empty()) return result;
    const json message = choices[0].value("message", json::object());
    if (message["content"].is_string()) result["text"] = message["content"];
    for (const auto& call : message.value("tool_calls", json::array())) {
        const json fn = call.value("function", json::object());
        json arguments = json::parse(fn.value("arguments", "{}"), nullptr, false);
        if (!arguments.is_object()) arguments = json::object();
        result["toolCalls"].push_back({
            {"id", call.value("id", RandomId("call"))}, {"name", fn.value("name", "")},
            {"arguments", std::move(arguments)},
        });
    }
    return result;
}

struct CompletionStream {
    std::string provider;
    std::string turn_id;
    EventEmitter emit;
    std::string buffer;
    std::string response_body;
    std::string text;
    std::map<std::string, json> calls;
    bool writable = true;
};

void AppendStreamText(CompletionStream& stream, const std::string& delta) {
    if (delta.empty() || !stream.writable) return;
    stream.text += delta;
    stream.writable = stream.emit("message.delta", {{"turnId", stream.turn_id}, {"text", delta}});
}

void AppendStreamCall(CompletionStream& stream, const std::string& key,
                      const std::string& id, const std::string& name,
                      const std::string& arguments) {
    json& call = stream.calls[key.empty() ? id : key];
    if (!id.empty()) call["id"] = id;
    if (!name.empty()) call["name"] = name;
    call["argumentsText"] = call.value("argumentsText", "") + arguments;
}

void ParseCompletionEvent(CompletionStream& stream, const std::string& payload) {
    if (payload.empty() || payload == "[DONE]") return;
    stream.response_body = payload;
    const json event = json::parse(payload, nullptr, false);
    if (!event.is_object()) return;
    if (stream.provider == "anthropic") {
        const std::string type = event.value("type", "");
        if (type == "content_block_start") {
            const json block = event.value("content_block", json::object());
            if (block.value("type", "") == "tool_use") {
                const std::string key = std::to_string(event.value("index", 0));
                AppendStreamCall(stream, key, block.value("id", ""), block.value("name", ""),
                    block.contains("input") && block["input"].is_object() && !block["input"].empty()
                        ? block["input"].dump() : "");
            }
        } else if (type == "content_block_delta") {
            const json delta = event.value("delta", json::object());
            if (delta.value("type", "") == "text_delta") AppendStreamText(stream, delta.value("text", ""));
            if (delta.value("type", "") == "input_json_delta") {
                AppendStreamCall(stream, std::to_string(event.value("index", 0)), "", "", delta.value("partial_json", ""));
            }
        }
        return;
    }
    if (stream.provider == "openai-responses") {
        const std::string type = event.value("type", "");
        if (type == "response.output_text.delta") AppendStreamText(stream, event.value("delta", ""));
        if (type == "response.output_item.added") {
            const json item = event.value("item", json::object());
            if (item.value("type", "") == "function_call") {
                AppendStreamCall(stream, item.value("id", item.value("call_id", "")),
                    item.value("call_id", item.value("id", "")), item.value("name", ""), item.value("arguments", ""));
            }
        }
        if (type == "response.function_call_arguments.delta") {
            AppendStreamCall(stream, event.value("item_id", event.value("call_id", "")),
                event.value("call_id", ""), "", event.value("delta", ""));
        }
        return;
    }
    if (stream.provider == "google") {
        const json candidates = event.value("candidates", json::array());
        if (candidates.empty()) return;
        for (const auto& part : candidates[0].value("content", json::object()).value("parts", json::array())) {
            if (part.contains("text")) AppendStreamText(stream, part.value("text", ""));
            if (part.contains("functionCall")) {
                const json call = part["functionCall"];
                const std::string key = RandomId("call");
                AppendStreamCall(stream, key, key, call.value("name", ""), call.value("args", json::object()).dump());
            }
        }
        return;
    }
    const json choices = event.value("choices", json::array());
    if (choices.empty()) return;
    const json delta = choices[0].value("delta", json::object());
    if (delta.contains("content") && delta["content"].is_string()) AppendStreamText(stream, delta["content"]);
    for (const auto& call : delta.value("tool_calls", json::array())) {
        const std::string key = std::to_string(call.value("index", 0));
        const json fn = call.value("function", json::object());
        AppendStreamCall(stream, key, call.value("id", ""), fn.value("name", ""), fn.value("arguments", ""));
    }
}

size_t CurlCompletionWrite(char* data, size_t size, size_t count, void* user) {
    const size_t bytes = size * count;
    auto& stream = *static_cast<CompletionStream*>(user);
    stream.buffer.append(data, bytes);
    size_t crlf = stream.buffer.find("\r\n");
    while (crlf != std::string::npos) {
        stream.buffer.replace(crlf, 2, "\n");
        crlf = stream.buffer.find("\r\n", crlf + 1);
    }
    size_t boundary = stream.buffer.find("\n\n");
    while (boundary != std::string::npos) {
        const std::string block = stream.buffer.substr(0, boundary);
        stream.buffer.erase(0, boundary + 2);
        std::string payload;
        std::istringstream lines(block);
        std::string line;
        while (std::getline(lines, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("data:", 0) == 0) {
                if (!payload.empty()) payload.push_back('\n');
                payload += line.substr(line.size() > 5 && line[5] == ' ' ? 6 : 5);
            }
        }
        ParseCompletionEvent(stream, payload);
        if (!stream.writable) return 0;
        boundary = stream.buffer.find("\n\n");
    }
    return bytes;
}

HttpResult HttpStreamCompletion(const std::string& url, const std::vector<std::string>& headers,
                                const std::string& body, CompletionStream& stream,
                                const std::atomic<bool>* cancelled) {
    HttpResult result;
    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = "http_client_unavailable";
        return result;
    }
    curl_slist* header_list = nullptr;
    for (const auto& header : headers) header_list = curl_slist_append(header_list, header.c_str());
    header_list = curl_slist_append(header_list, "Accept: text/event-stream");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlCompletionWrite);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(kTurnTimeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https,http");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "PCG-AI-Agent/1.0");
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancelled);
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    if (code != CURLE_OK) result.error = cancelled && cancelled->load() ? "cancelled" : curl_easy_strerror(code);
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);
    return result;
}

json Complete(TurnState& turn, const EventEmitter& emit) {
    json credential;
    if (!CredentialGet(turn.provider, credential)) return ErrorBody("not_connected", "Connect the selected Provider first.");
    json refresh_error;
    if (!EnsureFreshOAuthCredential(turn.provider, credential, &refresh_error)) return refresh_error;
    const std::string base = ProviderBaseUrl(turn.provider);
    json request;
    std::string url;
    if (turn.provider == "anthropic" || turn.provider == "kimi-coding") {
        url = base + "/messages";
        json tools = json::array();
        for (const auto& tool : GetPcgToolDefinitions()) tools.push_back({
            {"name", tool.value("name", "")}, {"description", tool.value("description", "")},
            {"input_schema", tool.value("inputSchema", json::object())},
        });
        request = {{"model", turn.model}, {"max_tokens", 8192}, {"system", SystemPrompt()},
                   {"messages", AnthropicMessages(turn.history)}, {"tools", std::move(tools)}, {"stream", true}};
    } else if (turn.provider == "google") {
        url = base + "/models/" + UrlEncode(turn.model) + ":streamGenerateContent?alt=sse";
        json declarations = json::array();
        for (const auto& tool : GetPcgToolDefinitions()) declarations.push_back({
            {"name", tool.value("name", "")}, {"description", tool.value("description", "")},
            {"parameters", tool.value("inputSchema", json::object())},
        });
        request = {{"systemInstruction", {{"parts", json::array({{{"text", SystemPrompt()}}})}}},
                   {"contents", GeminiContents(turn.history)},
                   {"tools", json::array({{{"functionDeclarations", std::move(declarations)}}})}};
    } else if (turn.provider == "openai") {
        const bool oauth = credential.value("type", "") == "oauth";
        url = oauth ? "https://chatgpt.com/backend-api/codex/responses" : base + "/responses";
        request = {
            {"model", turn.model}, {"instructions", SystemPrompt()},
            {"input", OpenAiResponseInput(turn.history)}, {"tools", OpenAiResponseTools()},
            {"tool_choice", "auto"}, {"store", false}, {"stream", true},
        };
    } else {
        url = base + "/chat/completions";
        request = {{"model", turn.model}, {"messages", OpenAiMessages(turn.history)},
                   {"tools", AgentTools()}, {"tool_choice", "auto"}, {"stream", true}};
    }
    const std::string protocol = turn.provider == "openai" ? "openai-responses" :
        turn.provider == "kimi-coding" ? "anthropic" : turn.provider;
    CompletionStream stream;
    stream.provider = protocol;
    stream.turn_id = turn.id;
    stream.emit = emit;
    const HttpResult response = HttpStreamCompletion(
        url, CredentialHeaders(turn.provider, credential), request.dump(), stream, &turn.cancelled);
    if (!response.ok()) {
        if (response.error == "cancelled") return ErrorBody("cancelled", "Turn cancelled.");
        const json upstream = json::parse(stream.buffer, nullptr, false);
        const json upstream_error = upstream.is_object() ? upstream.value("error", json::object()) : json::object();
        const std::string upstream_code = upstream_error.is_object()
            ? upstream_error.value("code", upstream_error.value("type", "")) : "";
        if (response.status == 401 || response.status == 403) {
            return ErrorBody("invalid_credential", "Provider rejected the connected credential.");
        }
        if (response.status == 402 || upstream_code == "insufficient_quota" ||
            upstream_code == "billing_error" || upstream_code == "payment_required") {
            return ErrorBody("quota_exhausted", "Provider quota or credit is exhausted.");
        }
        if (response.status == 429) {
            return ErrorBody("rate_limited", "Provider rate limit reached. Try again later.", true);
        }
        if (upstream_code == "model_not_found" || upstream_code == "invalid_model") {
            return ErrorBody("model_unavailable", "The selected model is not available for this account.");
        }
        return ErrorBody("provider_error", "Provider request failed (HTTP " +
            std::to_string(response.status) + ").", response.status >= 500);
    }
    json normalized = {{"text", stream.text}, {"toolCalls", json::array()}, {"streamed", !stream.text.empty()}};
    for (auto& entry : stream.calls) {
        json arguments = json::parse(entry.second.value("argumentsText", "{}"), nullptr, false);
        if (!arguments.is_object()) arguments = json::object();
        normalized["toolCalls"].push_back({
            {"id", entry.second.value("id", entry.first)},
            {"name", entry.second.value("name", "")}, {"arguments", std::move(arguments)},
        });
    }
    if (stream.text.empty() && stream.calls.empty() && !stream.buffer.empty()) {
        const json parsed = json::parse(stream.buffer, nullptr, false);
        if (!parsed.is_object()) return ErrorBody("invalid_provider_response", "Provider returned invalid streaming data.");
        normalized = NormalizeCompletion(protocol, parsed);
        normalized["streamed"] = false;
    }
    normalized["ok"] = true;
    return normalized;
}

void RunTurn(TurnState& turn, const EventEmitter& emit) {
    while (!turn.cancelled.load()) {
        if (++turn.tool_rounds > kMaxToolRounds || turn.tool_calls > kMaxToolCalls ||
            NowMs() - turn.started_at > kTurnTimeoutSeconds * 1000LL) {
            emit("turn.error", ErrorBody("turn_limit", "Agent turn exceeded its safety limit."));
            return;
        }
        json completion = Complete(turn, emit);
        if (!completion.value("ok", false)) {
            emit("turn.error", completion);
            return;
        }
        const std::string text = completion.value("text", "");
        const json calls = completion.value("toolCalls", json::array());
        turn.history.push_back({{"role", "assistant"}, {"text", text}, {"toolCalls", calls}});
        if (!text.empty() && !completion.value("streamed", false) &&
            !emit("message.delta", {{"turnId", turn.id}, {"text", text}})) {
            turn.cancelled.store(true);
            return;
        }
        if (calls.empty()) {
            emit("turn.completed", {{"turnId", turn.id}});
            return;
        }
        turn.tool_calls += static_cast<int>(calls.size());
        if (turn.tool_calls > kMaxToolCalls) continue;
        turn.pending = json::array();
        json approval_calls = json::array();
        for (const auto& call : calls) {
            const std::string name = call.value("name", "");
            const json public_call = {
                {"turnId", turn.id}, {"toolCallId", call.value("id", "")},
                {"name", name}, {"arguments", call.value("arguments", json::object())},
                {"requiresApproval", PcgToolRequiresApproval(name)},
            };
            if (!emit("tool.call", public_call)) {
                turn.cancelled.store(true);
                return;
            }
            if (PcgToolRequiresApproval(name)) {
                turn.pending.push_back(call);
                approval_calls.push_back(public_call);
                continue;
            }
            const json tool_result = CallPcgTool(name, call.value("arguments", json::object()));
            turn.history.push_back({
                {"role", "tool"}, {"toolCallId", call.value("id", "")}, {"name", name},
                {"content", tool_result.dump()}, {"isError", tool_result.value("isError", false)},
            });
            if (!emit("tool.result", {
                {"turnId", turn.id}, {"toolCallId", call.value("id", "")},
                {"name", name}, {"result", tool_result},
            })) {
                turn.cancelled.store(true);
                return;
            }
        }
        if (!turn.pending.empty()) {
            emit("approval.required", {{"turnId", turn.id}, {"calls", std::move(approval_calls)}});
            return;
        }
    }
    emit("turn.error", ErrorBody("cancelled", "Turn cancelled."));
}

bool IsUtf8(const std::string& value) {
    size_t i = 0;
    while (i < value.size()) {
        const unsigned char c = value[i];
        size_t count = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
        if (count == 0 || i + count > value.size()) return false;
        for (size_t j = 1; j < count; ++j) if ((static_cast<unsigned char>(value[i + j]) & 0xC0) != 0x80) return false;
        i += count;
    }
    return true;
}

bool BuildUserMessage(const httplib::Request& req, const json& input, httplib::Response& res, json& message) {
    message = {{"role", "user"}, {"content", json::array()}};
    const std::string text = input.value("message", "");
    if (!text.empty()) message["content"].push_back({{"type", "text"}, {"text", text}});
    const auto range = req.files.equal_range("attachment");
    size_t count = 0;
    size_t total = 0;
    for (auto it = range.first; it != range.second; ++it) {
        const auto& file = it->second;
        ++count;
        total += file.content.size();
        if (count > kMaxAttachments || file.content.size() > kMaxAttachmentBytes || total > kMaxAttachmentTotalBytes) {
            JsonResponse(res, 413, ErrorBody("attachment_too_large", "Attachment limits exceeded."));
            return false;
        }
        const std::string lower = [&] {
            std::string value = file.filename;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return value;
        }();
        const bool declared_image = file.content_type == "image/png" || file.content_type == "image/jpeg" ||
            (lower.size() >= 4 && (lower.rfind(".png") == lower.size() - 4 || lower.rfind(".jpg") == lower.size() - 4)) ||
            (lower.size() >= 5 && lower.rfind(".jpeg") == lower.size() - 5);
        const bool png = file.content.size() >= 8 &&
            static_cast<unsigned char>(file.content[0]) == 0x89 && file.content.compare(1, 3, "PNG") == 0 &&
            static_cast<unsigned char>(file.content[4]) == 0x0d && static_cast<unsigned char>(file.content[5]) == 0x0a &&
            static_cast<unsigned char>(file.content[6]) == 0x1a && static_cast<unsigned char>(file.content[7]) == 0x0a;
        const bool jpeg = file.content.size() >= 3 &&
            static_cast<unsigned char>(file.content[0]) == 0xff &&
            static_cast<unsigned char>(file.content[1]) == 0xd8 &&
            static_cast<unsigned char>(file.content[2]) == 0xff;
        const bool image = declared_image && (png || jpeg);
        const bool text_file = lower.size() >= 4 && (lower.rfind(".txt") == lower.size() - 4 ||
            lower.rfind(".json") == lower.size() - 5 || lower.rfind(".pcg") == lower.size() - 4);
        if (image) {
            message["content"].push_back({
                {"type", "image"}, {"name", file.filename},
                {"mimeType", jpeg ? "image/jpeg" : "image/png"},
                {"data", EncodeBase64(file.content)},
            });
        } else if (text_file && IsUtf8(file.content)) {
            message["content"].push_back({
                {"type", "text"}, {"text", "\n--- attachment: " + file.filename + " ---\n" + file.content + "\n--- end attachment ---"},
            });
        } else {
            JsonResponse(res, 400, ErrorBody("unsupported_attachment", "Only UTF-8 .txt/.json/.pcg and PNG/JPEG attachments are supported."));
            return false;
        }
    }
    if (message["content"].empty()) {
        JsonResponse(res, 400, ErrorBody("empty_message", "Message or attachment is required."));
        return false;
    }
    return true;
}

void FinishOAuth(const std::string& id, const json& credential) {
    std::string provider;
    {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        auto it = state.oauth.find(id);
        if (it == state.oauth.end()) return;
        provider = it->second.provider;
    }
    const json stored_credential = EnrichOAuthCredential(credential, {
        {"access_token", credential.value("access", "")},
        {"id_token", credential.value("idToken", "")},
    });
    if (!CredentialSet(provider, stored_credential)) {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        auto it = state.oauth.find(id);
        if (it != state.oauth.end()) {
            it->second.status = "failed";
            it->second.error = "keychain_write_failed";
        }
        return;
    }
    const json validation = ValidateCredential(provider, stored_credential);
    StoreValidation(provider, validation);
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.oauth.find(id);
    if (it != state.oauth.end()) {
        it->second.status = validation.value("ok", false) ? "connected" : "failed";
        if (!validation.value("ok", false)) it->second.error = validation.dump();
    }
}

void PollDeviceOAuth(OAuthAttempt attempt, const std::string& token_url, const std::string& client_id) {
    int interval = std::max(1, attempt.interval_seconds);
    while (NowMs() < attempt.expires_at) {
        std::this_thread::sleep_for(std::chrono::seconds(interval));
        {
            auto& state = State();
            std::lock_guard<std::mutex> lock(state.mutex);
            const auto live = state.oauth.find(attempt.id);
            if (live == state.oauth.end() || live->second.status != "pending") return;
        }
        const std::string body = "client_id=" + UrlEncode(client_id) +
            "&device_code=" + UrlEncode(attempt.device_code) +
            "&grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code";
        const HttpResult response = Http("POST", token_url,
            {"Accept: application/json", "Content-Type: application/x-www-form-urlencoded"}, body);
        const json parsed = json::parse(response.body, nullptr, false);
        if (response.ok() && parsed.is_object() && parsed.contains("access_token")) {
            FinishOAuth(attempt.id, {
                {"type", "oauth"}, {"access", parsed.value("access_token", "")},
                {"refresh", parsed.value("refresh_token", "")},
                {"idToken", parsed.value("id_token", "")},
                {"expires", NowMs() + parsed.value("expires_in", 3600) * 1000LL},
            });
            return;
        }
        const std::string error = parsed.is_object() ? parsed.value("error", "") : "";
        if (error == "authorization_pending" || response.status == 403) continue;
        if (error == "slow_down") {
            interval += 5;
            continue;
        }
        if (error == "access_denied" || error == "expired_token") {
            auto& state = State();
            std::lock_guard<std::mutex> lock(state.mutex);
            auto it = state.oauth.find(attempt.id);
            if (it != state.oauth.end()) it->second.status = error == "access_denied" ? "denied" : "expired";
            return;
        }
    }
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    auto it = state.oauth.find(attempt.id);
    if (it != state.oauth.end()) it->second.status = "expired";
}

}  // namespace

void ConfigureAgentRuntime(int server_port) {
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    if (server_port > 0 && server_port <= 65535) state.server_port = server_port;
}

void HandleAgentProviders(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    JsonResponse(res, 200, {{"ok", true}, {"providers", PublicProviders()}});
}

void HandleAgentConnectKey(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string provider = Match(req, 1);
    if (!IsSupportedKeyProvider(provider)) {
        JsonResponse(res, 404, ErrorBody("provider_not_found", "Provider does not support API Key authentication."));
        return;
    }
    json body;
    if (!ParseObjectBody(req, res, body)) return;
    const std::string key = body.value("apiKey", "");
    if (key.empty()) {
        JsonResponse(res, 400, ErrorBody("api_key_required", "API Key is required."));
        return;
    }
    if (provider == "openai-compatible") {
        const std::string base = NormalizeBaseUrl(body.value("baseUrl", ""));
        if (base.empty()) {
            JsonResponse(res, 400, ErrorBody("invalid_base_url", "Use HTTPS, or HTTP only for localhost/loopback."));
            return;
        }
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        LoadConfigLocked(state);
        state.custom_base_url = base;
        SaveConfigLocked(state);
    }
    const json credential = {{"type", "api"}, {"key", key}};
    const json validation = ValidateCredential(provider, credential);
    if (!validation.value("ok", false)) {
        JsonResponse(res, 400, validation);
        return;
    }
    if (!CredentialSet(provider, credential)) {
        JsonResponse(res, 500, ErrorBody("keychain_write_failed", "Could not save the credential in macOS Keychain."));
        return;
    }
    StoreValidation(provider, validation);
    JsonResponse(res, 200, {{"ok", true}, {"provider", provider}, {"models", validation["models"]}});
}

void HandleAgentDeleteConnection(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string provider = Match(req, 1);
    if (!CredentialDelete(provider)) {
        JsonResponse(res, 500, ErrorBody("keychain_delete_failed", "Could not remove the Provider credential."));
        return;
    }
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    LoadConfigLocked(state);
    state.provider_meta.erase(provider);
    for (auto it = state.oauth.begin(); it != state.oauth.end();) {
        if (it->second.provider == provider) it = state.oauth.erase(it);
        else ++it;
    }
    if (state.provider_id == provider) {
        state.provider_id.clear();
        state.model_id.clear();
    }
    SaveConfigLocked(state);
    JsonResponse(res, 200, {{"ok", true}});
}

void HandleAgentValidateProvider(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string provider = Match(req, 1);
    json credential;
    if (!CredentialGet(provider, credential)) {
        JsonResponse(res, 404, ErrorBody("not_connected", "Provider is not connected."));
        return;
    }
    json refresh_error;
    if (!EnsureFreshOAuthCredential(provider, credential, &refresh_error)) {
        StoreValidation(provider, refresh_error);
        JsonResponse(res, 400, refresh_error);
        return;
    }
    const json validation = ValidateCredential(provider, credential);
    StoreValidation(provider, validation);
    JsonResponse(res, validation.value("ok", false) ? 200 : 400, validation);
}

void HandleAgentGetSettings(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    LoadConfigLocked(state);
    JsonResponse(res, 200, {{"ok", true}, {"providerId", state.provider_id}, {"modelId", state.model_id}});
}

void HandleAgentPutSettings(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body;
    if (!ParseObjectBody(req, res, body)) return;
    const std::string provider = body.value("providerId", "");
    const std::string model = body.value("modelId", "");
    json credential;
    if (!CredentialGet(provider, credential)) {
        JsonResponse(res, 400, ErrorBody("not_connected", "Connect the selected Provider first."));
        return;
    }
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    LoadConfigLocked(state);
    const json models = state.provider_meta.value(provider, json::object()).value("models", json::array());
    const auto selected = std::find_if(models.begin(), models.end(),
        [&](const json& item) { return item.value("id", "") == model; });
    if (selected == models.end()) {
        JsonResponse(res, 400, ErrorBody("model_not_found", "Select a validated model."));
        return;
    }
    if (!selected->value("capabilities", json::object()).value("toolCall", false)) {
        JsonResponse(res, 400, ErrorBody("model_capability_unsupported", "The selected model does not support tool calling."));
        return;
    }
    state.provider_id = provider;
    state.model_id = model;
    SaveConfigLocked(state);
    JsonResponse(res, 200, {{"ok", true}, {"providerId", provider}, {"modelId", model}});
}

void HandleAgentOAuthStart(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string provider = Match(req, 1);
    const char* env_name = provider == "openai" ? "PCG_OPENAI_OAUTH_CLIENT_ID" :
        provider == "github-copilot" ? "PCG_GITHUB_OAUTH_CLIENT_ID" :
        provider == "xai" ? "PCG_XAI_OAUTH_CLIENT_ID" : nullptr;
    const char* env = env_name ? std::getenv(env_name) : nullptr;
    if (!env || !*env) {
        JsonResponse(res, 400, ErrorBody("oauth_unavailable", "A PCG-owned OAuth Client ID is not configured."));
        return;
    }
    OAuthAttempt attempt;
    attempt.id = RandomId("oauth");
    attempt.provider = provider;
    attempt.expires_at = NowMs() + 5 * 60 * 1000;
    if (provider == "openai") {
        attempt.verifier = RandomUrlToken(48);
        attempt.state = RandomUrlToken();
        const std::string redirect = OAuthRedirectUri(attempt.id);
        attempt.url = "https://auth.openai.com/oauth/authorize?response_type=code&client_id=" + UrlEncode(env) +
            "&redirect_uri=" + UrlEncode(redirect) + "&scope=" + UrlEncode("openid profile email offline_access") +
            "&code_challenge=" + UrlEncode(Sha256Base64Url(attempt.verifier)) + "&code_challenge_method=S256&state=" + UrlEncode(attempt.state);
    } else {
        const std::string device_url = provider == "github-copilot"
            ? "https://github.com/login/device/code"
            : "https://auth.x.ai/oauth2/device/code";
        const std::string scope = provider == "github-copilot"
            ? "read:user" : "openid profile email offline_access grok-cli:access api:access";
        const HttpResult started = Http("POST", device_url,
            {"Accept: application/json", "Content-Type: application/x-www-form-urlencoded"},
            "client_id=" + UrlEncode(env) + "&scope=" + UrlEncode(scope));
        const json parsed = json::parse(started.body, nullptr, false);
        if (!started.ok() || !parsed.is_object() || !parsed.contains("device_code")) {
            JsonResponse(res, 502, ErrorBody("oauth_start_failed", "Provider rejected the device authorization request."));
            return;
        }
        attempt.device_code = parsed.value("device_code", "");
        attempt.user_code = parsed.value("user_code", "");
        attempt.url = parsed.value("verification_uri_complete", parsed.value("verification_uri", ""));
        attempt.interval_seconds = std::max(1, parsed.value("interval", 5));
        attempt.expires_at = NowMs() + parsed.value("expires_in", 300) * 1000LL;
    }
    {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.oauth[attempt.id] = attempt;
    }
    if (provider != "openai") {
        const std::string token_url = provider == "github-copilot"
            ? "https://github.com/login/oauth/access_token"
            : "https://auth.x.ai/oauth2/token";
        std::thread(PollDeviceOAuth, attempt, token_url, std::string(env)).detach();
    }
    JsonResponse(res, 200, {
        {"ok", true}, {"attemptId", attempt.id}, {"providerId", provider},
        {"url", attempt.url}, {"userCode", attempt.user_code},
        {"expiresAt", attempt.expires_at}, {"intervalSeconds", attempt.interval_seconds},
    });
}

void HandleAgentOAuthStatus(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    auto& state = State();
    std::lock_guard<std::mutex> lock(state.mutex);
    const std::string id = Match(req, 1);
    auto it = state.oauth.find(id);
    if (it == state.oauth.end()) {
        JsonResponse(res, 404, ErrorBody("oauth_attempt_not_found", "OAuth attempt was not found."));
        return;
    }
    if (it->second.status == "pending" && NowMs() >= it->second.expires_at) it->second.status = "expired";
    JsonResponse(res, 200, {
        {"ok", true}, {"attemptId", id}, {"providerId", it->second.provider},
        {"status", it->second.status}, {"error", it->second.error},
    });
}

void HandleAgentOAuthCallback(const httplib::Request& req, httplib::Response& res) {
    const std::string provider = Match(req, 1);
    const std::string id = req.has_param("attemptId") ? req.get_param_value("attemptId") : "";
    OAuthAttempt attempt;
    {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        auto it = state.oauth.find(id);
        if (it == state.oauth.end() || it->second.provider != provider) {
            res.status = 400;
            res.set_content("Invalid OAuth attempt.", "text/plain");
            return;
        }
        attempt = it->second;
    }
    const std::string state_value = req.has_param("state") ? req.get_param_value("state") : "";
    const std::string code = req.has_param("code") ? req.get_param_value("code") : "";
    if (state_value != attempt.state || code.empty()) {
        res.status = 400;
        res.set_content("OAuth state validation failed.", "text/plain");
        return;
    }
    const char* client = std::getenv("PCG_OPENAI_OAUTH_CLIENT_ID");
    const std::string redirect = OAuthRedirectUri(id);
    const std::string body = "grant_type=authorization_code&code=" + UrlEncode(code) +
        "&redirect_uri=" + UrlEncode(redirect) + "&client_id=" + UrlEncode(client ? client : "") +
        "&code_verifier=" + UrlEncode(attempt.verifier);
    const HttpResult exchanged = Http("POST", "https://auth.openai.com/oauth/token",
        {"Content-Type: application/x-www-form-urlencoded"}, body);
    const json parsed = json::parse(exchanged.body, nullptr, false);
    if (!exchanged.ok() || !parsed.is_object() || !parsed.contains("access_token")) {
        res.status = 502;
        res.set_content("OAuth token exchange failed.", "text/plain");
        return;
    }
    FinishOAuth(id, {
        {"type", "oauth"}, {"access", parsed.value("access_token", "")},
        {"refresh", parsed.value("refresh_token", "")},
        {"idToken", parsed.value("id_token", "")},
        {"expires", NowMs() + parsed.value("expires_in", 3600) * 1000LL},
    });
    res.status = 200;
    res.set_content("<!doctype html><title>PCG-AI</title><p>Provider connected. You can close this window.</p>", "text/html; charset=utf-8");
}

void HandleAgentTurn(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    if (!req.has_file("request")) {
        JsonResponse(res, 400, ErrorBody("invalid_request", "Multipart field 'request' is required."));
        return;
    }
    const json input = json::parse(req.get_file_value("request").content, nullptr, false);
    if (!input.is_object()) {
        JsonResponse(res, 400, ErrorBody("invalid_request", "Turn request JSON is invalid."));
        return;
    }
    auto turn = std::make_shared<TurnState>();
    turn->id = RandomId("turn");
    turn->session_id = input.value("sessionId", RandomId("session"));
    turn->provider = input.value("providerId", "");
    turn->model = input.value("modelId", "");
    turn->started_at = NowMs();
    json user;
    if (!BuildUserMessage(req, input, res, user)) return;
    {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        LoadConfigLocked(state);
        if (turn->provider.empty()) turn->provider = state.provider_id;
        if (turn->model.empty()) turn->model = state.model_id;
        const json models = state.provider_meta.value(turn->provider, json::object()).value("models", json::array());
        const auto selected = std::find_if(models.begin(), models.end(),
            [&](const json& item) { return item.value("id", "") == turn->model; });
        if (selected == models.end()) {
            JsonResponse(res, 400, ErrorBody("model_not_found", "Select a validated model."));
            return;
        }
        const json capabilities = selected->value("capabilities", json::object());
        if (!capabilities.value("toolCall", false) || !capabilities.value("textInput", false)) {
            JsonResponse(res, 400, ErrorBody("model_capability_unsupported", "The selected model cannot run Agent tool turns."));
            return;
        }
        const bool has_image = std::any_of(user["content"].begin(), user["content"].end(),
            [](const json& part) { return part.value("type", "") == "image"; });
        if (has_image && !capabilities.value("imageInput", false)) {
            JsonResponse(res, 400, ErrorBody("image_input_unsupported", "The selected model does not support image input."));
            return;
        }
        const auto history = state.sessions.find(turn->session_id);
        turn->history = history == state.sessions.end() ? json::array() : history->second;
        if (!turn->history.is_array()) turn->history = json::array();
        turn->history.push_back(std::move(user));
        state.turns[turn->id] = turn;
    }
    res.status = 200;
    res.set_header("Cache-Control", "no-cache");
    res.set_header("X-Accel-Buffering", "no");
    res.set_chunked_content_provider(
        "text/event-stream",
        [turn, started = false](size_t, httplib::DataSink& sink) mutable {
            if (started) return false;
            started = true;
            const EventEmitter emit = [&](const std::string& event, const json& data) {
                const std::string payload = Sse(event, data);
                return sink.is_writable && sink.is_writable() && sink.write(payload.data(), payload.size());
            };
            if (!emit("turn.created", {{"turnId", turn->id}, {"sessionId", turn->session_id}})) {
                turn->cancelled.store(true);
                return false;
            }
            {
                std::lock_guard<std::mutex> turn_lock(turn->mutex);
                RunTurn(*turn, emit);
                auto& state = State();
                std::lock_guard<std::mutex> state_lock(state.mutex);
                state.sessions[turn->session_id] = turn->history;
            }
            sink.done();
            return true;
        },
        [turn](bool success) {
            if (!success) turn->cancelled.store(true);
        });
}

void HandleAgentTurnDecision(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string id = Match(req, 1);
    std::shared_ptr<TurnState> turn;
    {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        auto it = state.turns.find(id);
        if (it != state.turns.end()) turn = it->second;
    }
    if (!turn) {
        JsonResponse(res, 404, ErrorBody("turn_not_found", "Turn was not found."));
        return;
    }
    json body;
    if (!ParseObjectBody(req, res, body)) return;
    const json decisions = body.value("decisions", json::array());
    {
        std::lock_guard<std::mutex> turn_lock(turn->mutex);
        if (turn->pending.empty()) {
            JsonResponse(res, 409, ErrorBody("approval_already_resolved", "This approval has already been resolved."));
            return;
        }
    }
    res.status = 200;
    res.set_header("Cache-Control", "no-cache");
    res.set_header("X-Accel-Buffering", "no");
    res.set_chunked_content_provider(
        "text/event-stream",
        [turn, decisions, id, started = false](size_t, httplib::DataSink& sink) mutable {
            if (started) return false;
            started = true;
            const EventEmitter emit = [&](const std::string& event, const json& data) {
                const std::string payload = Sse(event, data);
                return sink.is_writable && sink.is_writable() && sink.write(payload.data(), payload.size());
            };
            std::lock_guard<std::mutex> turn_lock(turn->mutex);
            if (turn->pending.empty()) {
                emit("turn.error", ErrorBody("approval_already_resolved", "This approval has already been resolved."));
                sink.done();
                return true;
            }
            const json pending = std::move(turn->pending);
            turn->pending = json::array();
            for (const auto& call : pending) {
                const std::string call_id = call.value("id", "");
                bool approved = false;
                for (const auto& decision : decisions) {
                    if (decision.value("toolCallId", "") == call_id) approved = decision.value("decision", "reject") == "approve";
                }
                json result;
                if (!approved) {
                    result = {{"content", json::array({{{"type", "text"}, {"text", "User rejected this write operation."}}})},
                              {"structuredContent", {{"ok", false}, {"error", "user_rejected"}}}, {"isError", true}};
                } else {
                    const json arguments = call.value("arguments", json::object());
                    const std::string expected_hash = arguments.value("ifGraphHash", "");
                    const std::string current_hash = GetEditorContext().value("session", json::object()).value("graphHash", "");
                    if (expected_hash.empty() || expected_hash != current_hash) {
                        result = {{"content", json::array({{{"type", "text"}, {"text", "Graph changed while approval was pending."}}})},
                                  {"structuredContent", {{"ok", false}, {"error", "graph_conflict"}, {"graphHash", current_hash}}}, {"isError", true}};
                    } else {
                        result = CallPcgTool(call.value("name", ""), arguments);
                    }
                }
                turn->history.push_back({
                    {"role", "tool"}, {"toolCallId", call_id}, {"name", call.value("name", "")},
                    {"content", result.dump()}, {"isError", result.value("isError", false)},
                });
                if (!emit("tool.result", {{"turnId", id}, {"toolCallId", call_id}, {"name", call.value("name", "")}, {"result", result}})) {
                    turn->cancelled.store(true);
                    return false;
                }
            }
            RunTurn(*turn, emit);
            {
                auto& state = State();
                std::lock_guard<std::mutex> lock(state.mutex);
                state.sessions[turn->session_id] = turn->history;
            }
            sink.done();
            return true;
        },
        [turn](bool success) {
            if (!success) turn->cancelled.store(true);
        });
}

void HandleAgentTurnCancel(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string id = Match(req, 1);
    std::shared_ptr<TurnState> turn;
    {
        auto& state = State();
        std::lock_guard<std::mutex> lock(state.mutex);
        auto it = state.turns.find(id);
        if (it != state.turns.end()) turn = it->second;
    }
    if (!turn) {
        JsonResponse(res, 404, ErrorBody("turn_not_found", "Turn was not found."));
        return;
    }
    turn->cancelled.store(true);
    JsonResponse(res, 200, {{"ok", true}, {"turnId", id}});
}

}  // namespace pcg_server
