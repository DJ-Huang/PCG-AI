#include "third_party_service.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "agent_runtime.hpp"
#include "agent_service.hpp"
#include "kb_service.hpp"

namespace pcg_server {
namespace {

using json = nlohmann::json;

constexpr const char* kTripoCredentialName = "thirdParty.tripo";
constexpr const char* kTripoApiKeyEnv = "PCG_TRIPO_API_KEY";
constexpr const char* kTripoBaseUrlEnv = "PCG_TRIPO_BASE_URL";
constexpr const char* kTripoStubGlbEnv = "PCG_TRIPO_STUB_GLB";
constexpr const char* kTripoDefaultBaseUrl = "https://openapi.tripo3d.ai/v3";
constexpr const char* kTripoChinaBaseUrl = "https://openapi.tripo3d.com/v3";
constexpr long kTripoHttpTimeoutSec = 120;
constexpr int kTripoPollMaxSeconds = 540;

void WriteJson(httplib::Response& res, const json& body, int status = 200) {
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

json ErrorBody(const char* code, const std::string& message) {
    return {
        {"ok", false},
        {"error", {{"code", code}, {"message", message}, {"retryable", false}}},
    };
}

std::string Trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string EnvOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return value && *value ? Trim(value) : "";
}

std::string KeyHint(const std::string& key) {
    if (key.size() < 8) return "";
    return "\xE2\x80\xA6" + key.substr(key.size() - 4);  // "…" + last 4 chars
}

json TripoStatus() {
    const std::string env_key = EnvOrEmpty(kTripoApiKeyEnv);
    const std::string env_base = EnvOrEmpty(kTripoBaseUrlEnv);

    json stored;
    const bool has_stored = LoadProtectedCredential(kTripoCredentialName, stored);
    const std::string stored_key = has_stored ? stored.value("key", "") : "";
    const std::string stored_base = has_stored ? stored.value("baseUrl", "") : "";

    const bool configured = !env_key.empty() || !stored_key.empty();
    const std::string source = !env_key.empty() ? "env" : (!stored_key.empty() ? "store" : "none");
    const std::string& active_key = !env_key.empty() ? env_key : stored_key;

    return {
        {"ok", true},
        {"tripo",
         {
             {"configured", configured},
             {"source", source},
             {"baseUrl", !env_base.empty() ? env_base
                        : !stored_base.empty() ? stored_base
                                               : kTripoDefaultBaseUrl},
             {"keyHint", configured ? KeyHint(active_key) : ""},
             {"credentialStore", AgentCredentialStoreName()},
         }},
    };
}

}  // namespace

void HandleThirdPartyTripoStatus(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    WriteJson(res, TripoStatus());
}

void HandleThirdPartyTripoConfigPut(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body = json::parse(req.body, nullptr, false);
    if (!body.is_object()) {
        WriteJson(res, ErrorBody("invalid_body", "Request body must be a JSON object."), 400);
        return;
    }
    const std::string key = Trim(body.value("apiKey", ""));
    if (key.empty()) {
        WriteJson(res, ErrorBody("api_key_required", "Tripo API Key is required."), 400);
        return;
    }
    if (key.rfind("msy_", 0) == 0) {
        WriteJson(res, ErrorBody(
            "wrong_vendor_key",
            "This looks like a Meshy key (msy_…). Paste a Tripo API key from tripo3d.ai."), 400);
        return;
    }
    const std::string base_url = Trim(body.value("baseUrl", ""));
    const json credential = {
        {"type", "api"},
        {"key", key},
        {"baseUrl", !base_url.empty() ? base_url : kTripoDefaultBaseUrl},
    };
    if (!StoreProtectedCredential(kTripoCredentialName, credential)) {
        WriteJson(res, ErrorBody(
            "credential_store_write_failed", "Could not save the Tripo credential."), 500);
        return;
    }
    WriteJson(res, TripoStatus());
}

void HandleThirdPartyTripoConfigDelete(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    if (!DeleteProtectedCredential(kTripoCredentialName)) {
        WriteJson(res, ErrorBody(
            "credential_store_delete_failed", "Could not delete the Tripo credential."), 500);
        return;
    }
    WriteJson(res, TripoStatus());
}

namespace {

// ── SHA-256 (hex) — self-contained for deterministic cache keys ─────────────

class Sha256 {
public:
    Sha256() { Reset(); }
    void Update(const unsigned char* data, size_t length) {
        for (size_t i = 0; i < length; ++i) {
            buffer_[buffer_len_++] = data[i];
            if (buffer_len_ == 64) {
                Transform();
                bit_length_ += 512;
                buffer_len_ = 0;
            }
        }
    }
    std::string FinalHex() {
        size_t i = buffer_len_;
        buffer_[i++] = 0x80;
        if (i > 56) {
            while (i < 64) buffer_[i++] = 0;
            Transform();
            i = 0;
        }
        while (i < 56) buffer_[i++] = 0;
        bit_length_ += static_cast<uint64_t>(buffer_len_) * 8;
        for (int shift = 56; shift >= 0; shift -= 8)
            buffer_[i++] = static_cast<unsigned char>((bit_length_ >> shift) & 0xFF);
        Transform();
        static const char* hex = "0123456789abcdef";
        std::string out;
        out.reserve(64);
        for (uint32_t word : state_) {
            for (int shift = 28; shift >= 0; shift -= 4)
                out += hex[(word >> shift) & 0xF];
        }
        return out;
    }

private:
    void Reset() {
        buffer_len_ = 0;
        bit_length_ = 0;
        const uint32_t initial[8] = {
            0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
            0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
        };
        std::copy(initial, initial + 8, state_);
    }
    static uint32_t RotateRight(uint32_t value, uint32_t bits) {
        return (value >> bits) | (value << (32 - bits));
    }
    void Transform() {
        static const uint32_t k[64] = {
            0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
            0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
            0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
            0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
            0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
            0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
            0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
            0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
        };
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(buffer_[i * 4]) << 24)
                 | (static_cast<uint32_t>(buffer_[i * 4 + 1]) << 16)
                 | (static_cast<uint32_t>(buffer_[i * 4 + 2]) << 8)
                 | static_cast<uint32_t>(buffer_[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const uint32_t s0 = RotateRight(w[i - 15], 7) ^ RotateRight(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = RotateRight(w[i - 2], 17) ^ RotateRight(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
        for (int i = 0; i < 64; ++i) {
            const uint32_t s1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const uint32_t ch = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            const uint32_t s0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
        state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
    }

    unsigned char buffer_[64]{};
    size_t buffer_len_ = 0;
    uint64_t bit_length_ = 0;
    uint32_t state_[8]{};
};

// ── base64 decode (data URI image payloads) ─────────────────────────────────

bool Base64Decode(const std::string& input, std::string& output) {
    static const std::string alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> table(256, -1);
    for (int i = 0; i < 64; ++i) table[static_cast<unsigned char>(alphabet[i])] = i;
    int value = 0;
    int bits = -8;
    for (const char c : input) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        if (c == '=') break;
        if (table[static_cast<unsigned char>(c)] == -1) return false;
        value = (value << 6) | table[static_cast<unsigned char>(c)];
        bits += 6;
        if (bits >= 0) {
            output += static_cast<char>((value >> bits) & 0xFF);
            bits -= 8;
        }
    }
    return true;
}

// ── HTTP helpers (libcurl) ──────────────────────────────────────────────────

struct HttpResult {
    long status = 0;
    std::string body;
    std::string error;
};

size_t CurlWriteBody(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

HttpResult CurlPerform(CURL* curl, const std::string& url, const std::string& bearer,
                       const char* content_type, curl_mime* mime, const std::string* json_body,
                       long timeout_sec) {
    HttpResult result;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_slist* headers = nullptr;
    std::string auth;
    if (!bearer.empty()) {
        auth = "Authorization: Bearer " + bearer;
        headers = curl_slist_append(headers, auth.c_str());
    }
    if (content_type) headers = curl_slist_append(headers, content_type);
    if (headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (mime) curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    if (json_body) curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_body->c_str());

    const CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        result.error = curl_easy_strerror(code);
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
    if (headers) curl_slist_free_all(headers);
    return result;
}

HttpResult HttpGet(const std::string& url, const std::string& bearer, long timeout_sec) {
    CURL* curl = curl_easy_init();
    HttpResult result = CurlPerform(curl, url, bearer, nullptr, nullptr, nullptr, timeout_sec);
    curl_easy_cleanup(curl);
    return result;
}

HttpResult HttpPostJson(const std::string& url, const std::string& bearer, const json& body) {
    CURL* curl = curl_easy_init();
    const std::string payload = body.dump();
    HttpResult result = CurlPerform(
        curl, url, bearer, "Content-Type: application/json", nullptr, &payload, kTripoHttpTimeoutSec);
    curl_easy_cleanup(curl);
    return result;
}

HttpResult HttpUploadFile(const std::string& url, const std::string& bearer,
                          const std::string& bytes, const std::string& extension) {
    CURL* curl = curl_easy_init();
    curl_mime* mime = curl_mime_init(curl);
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, "file");
    curl_mime_data(part, bytes.data(), bytes.size());
    const std::string filename = "image." + extension;
    curl_mime_filename(part, filename.c_str());
    curl_mime_type(part, extension == "png" ? "image/png" : "image/jpeg");
    HttpResult result = CurlPerform(curl, url, bearer, nullptr, mime, nullptr, kTripoHttpTimeoutSec);
    curl_mime_free(mime);
    curl_easy_cleanup(curl);
    return result;
}

// ── Tripo API flow (mirrors Unity PcgTripoClient) ───────────────────────────

std::string Truncate(const std::string& value, size_t max) {
    return value.size() <= max ? value : value.substr(0, max);
}

std::string FormatTripoHttpError(const char* step, long status, const std::string& body) {
    if (status == 401) {
        return std::string("Tripo ") + step + " HTTP 401: API key rejected. "
            "Check Settings → 3D Generation, or PCG_TRIPO_API_KEY. " + Truncate(body, 400);
    }
    return std::string("Tripo ") + step + " HTTP " + std::to_string(status) + ": " + Truncate(body, 400);
}

// Tripo wraps every payload as {code, message, data}. code != 0 is an API error.
json ParseTripoData(const HttpResult& http, const char* step, std::string& error) {
    if (!http.error.empty()) {
        error = std::string("Tripo ") + step + " request failed: " + http.error;
        return json();
    }
    if (http.status < 200 || http.status >= 300) {
        error = FormatTripoHttpError(step, http.status, http.body);
        return json();
    }
    const json parsed = json::parse(http.body, nullptr, false);
    if (!parsed.is_object()) {
        error = std::string("Tripo ") + step + " response is not a JSON object.";
        return json();
    }
    if (parsed.contains("code") && parsed.value("code", 0) != 0) {
        error = std::string("Tripo ") + step + " API error (code="
            + std::to_string(parsed.value("code", 0)) + "): "
            + Truncate(parsed.value("message", http.body), 400);
        return json();
    }
    const json data = parsed.value("data", json());
    if (!data.is_object()) {
        error = std::string("Tripo ") + step + " response missing data.";
        return json();
    }
    return data;
}

bool IsUnauthorized(const HttpResult& http) { return http.status == 401; }

std::string AlternateBaseUrl(const std::string& base_url) {
    return base_url == kTripoChinaBaseUrl ? kTripoDefaultBaseUrl : kTripoChinaBaseUrl;
}

std::string NormalizeExtension(std::string ext) {
    ext = Trim(ext);
    if (!ext.empty() && ext[0] == '.') ext.erase(ext.begin());
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext.empty()) return "png";
    return ext == "jpeg" ? "jpg" : ext;
}

std::filesystem::path WorkspaceRoot() {
    // ConfigureKbRoot() stores <workspace>/.picg — the workspace is its parent.
    const auto kb_root = GetKbRoot();
    if (!kb_root.empty() && kb_root.has_parent_path()) return kb_root.parent_path();
    return std::filesystem::current_path();
}

std::filesystem::path TripoCacheDir() {
    return WorkspaceRoot() / "library" / "web-cache" / "TripoCache";
}

bool IsInsideRoot(const std::filesystem::path& path, const std::filesystem::path& root) {
    std::error_code ec;
    const auto canonical_path = std::filesystem::weakly_canonical(path, ec);
    const auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec || canonical_path.empty() || canonical_root.empty()) return false;
    const auto relative = canonical_path.lexically_relative(canonical_root);
    return !relative.empty() && relative.native().front() != '.'
        && *relative.begin() != "..";
}

// Resolve a web-editor texture reference to bytes on disk:
//   pcg-resource://textures/x.png → <root>/web/pcg-editor/public/assets/textures/x.png
//   /assets/textures/x.png        → <root>/web/pcg-editor/public/assets/textures/x.png
//   relative path                 → <root>/<path>   (must stay inside the workspace)
bool ResolveImageFromDisk(const std::string& storage, std::string& bytes,
                          std::string& extension, std::string& error) {
    const auto root = WorkspaceRoot();
    std::filesystem::path path;
    if (storage.rfind("pcg-resource://", 0) == 0) {
        std::string key = storage.substr(std::string("pcg-resource://").size());
        while (!key.empty() && key.front() == '/') key.erase(key.begin());
        path = root / "web" / "pcg-editor" / "public" / "assets" / key;
    } else if (storage.rfind("/assets/", 0) == 0) {
        path = root / "web" / "pcg-editor" / "public" / storage.substr(1);
    } else {
        path = root / storage;
    }
    if (!IsInsideRoot(path, root)) {
        error = "Image path escapes the workspace: " + storage;
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "Image file not found: " + path.string();
        return false;
    }
    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        error = "Image file is empty: " + path.string();
        return false;
    }
    extension = NormalizeExtension(path.extension().string());
    if (extension != "png" && extension != "jpg") {
        error = "Unsupported image type '" + extension + "' (Tripo accepts png/jpg).";
        return false;
    }
    return true;
}

std::string ResolveTripoApiKey() {
    const std::string env = EnvOrEmpty(kTripoApiKeyEnv);
    if (!env.empty()) return env;
    json stored;
    if (LoadProtectedCredential(kTripoCredentialName, stored)) return stored.value("key", "");
    return "";
}

std::string ResolveTripoBaseUrl() {
    const std::string env = EnvOrEmpty(kTripoBaseUrlEnv);
    if (!env.empty()) return env;
    json stored;
    if (LoadProtectedCredential(kTripoCredentialName, stored)) {
        const std::string base = stored.value("baseUrl", "");
        if (!base.empty()) return base;
    }
    return kTripoDefaultBaseUrl;
}

// Synchronous upload → create → poll → download. `progress` (0..1) is emitted
// on the request thread; returning false from it aborts (client disconnected).
// Returns a json result with ok=false+error on failure; on success the GLB
// sits in the Tripo cache.
using TripoProgress = std::function<bool(double percent, const std::string& message)>;

json TripoGenerate(const json& request, const TripoProgress& progress) {
    const std::string stub = EnvOrEmpty(kTripoStubGlbEnv);

    const std::string image_url = Trim(request.value("imageUrl", ""));
    const std::string texture_path = Trim(request.value("texturePath", ""));
    const std::string image_base64 = request.value("imageBase64", "");
    std::string model_version = Trim(request.value("modelVersion", ""));
    if (model_version.empty()) model_version = "v3.1-20260211";
    const bool generate_texture = request.value("texture", true);
    const bool enable_pbr = request.value("pbr", false);
    const int face_limit = request.value("faceLimit", 30000);

    // Resolve the source image into bytes (disk/base64) or keep a public URL.
    std::string image_bytes;
    std::string extension = "png";
    if (image_url.empty()) {
        std::string error;
        if (!texture_path.empty()) {
            if (!ResolveImageFromDisk(texture_path, image_bytes, extension, error)) {
                return {{"ok", false}, {"error", {{"code", "image_resolve_failed"}, {"message", error}, {"retryable", false}}}};
            }
        } else if (!image_base64.empty()) {
            std::string payload = image_base64;
            const auto comma = payload.find(',');
            if (payload.rfind("data:", 0) == 0 && comma != std::string::npos) {
                const std::string header = payload.substr(0, comma);
                if (header.find("png") != std::string::npos) extension = "png";
                else if (header.find("jpeg") != std::string::npos || header.find("jpg") != std::string::npos) extension = "jpg";
                payload = payload.substr(comma + 1);
            }
            if (!Base64Decode(payload, image_bytes) || image_bytes.empty()) {
                return {{"ok", false}, {"error", {{"code", "image_decode_failed"}, {"message", "imageBase64 is not valid base64 image data."}, {"retryable", false}}}};
            }
        } else {
            return {{"ok", false}, {"error", {{"code", "image_required"}, {"message", "Provide imageUrl, texturePath, or imageBase64."}, {"retryable", false}}}};
        }
    }

    // Cache key: params + image identity. A hit never touches the network.
    Sha256 sha;
    const std::string fingerprint = model_version + (generate_texture ? "|t1" : "|t0")
        + (enable_pbr ? "|p1" : "|p0") + "|" + std::to_string(face_limit);
    sha.Update(reinterpret_cast<const unsigned char*>(fingerprint.data()), fingerprint.size());
    if (!image_url.empty()) {
        sha.Update(reinterpret_cast<const unsigned char*>(image_url.data()), image_url.size());
    } else {
        sha.Update(reinterpret_cast<const unsigned char*>(image_bytes.data()), image_bytes.size());
    }
    const std::string cache_name = sha.FinalHex() + ".glb";
    const auto cache_dir = TripoCacheDir();
    const auto cache_path = cache_dir / cache_name;
    const std::string relative_path = "library/web-cache/TripoCache/" + cache_name;

    std::error_code ec;
    if (std::filesystem::exists(cache_path, ec) && std::filesystem::file_size(cache_path, ec) > 0) {
        return {
            {"ok", true},
            {"path", relative_path},
            {"cached", true},
            {"bytes", std::filesystem::file_size(cache_path, ec)},
        };
    }

    // Stub mode: copy a local GLB instead of calling Tripo (CI / no-key dev).
    if (!stub.empty()) {
        std::filesystem::create_directories(cache_dir, ec);
        std::filesystem::copy_file(stub, cache_path,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            return {{"ok", false}, {"error", {{"code", "stub_copy_failed"}, {"message", ec.message()}, {"retryable", true}}}};
        }
        return {{"ok", true}, {"path", relative_path}, {"cached", false}, {"stub", true}};
    }

    const std::string api_key = ResolveTripoApiKey();
    if (api_key.empty()) {
        return {{"ok", false}, {"error", {{"code", "api_key_missing"},
            {"message", "Tripo API key is not configured. Open Settings → 3D Generation, or set PCG_TRIPO_API_KEY."},
            {"retryable", false}}}};
    }

    std::string base_url = ResolveTripoBaseUrl();
    std::string error;

    // Step 1: upload the image bytes when no public URL was provided.
    std::string file_token;
    if (image_url.empty()) {
        if (progress && !progress(0.02, "Uploading image to Tripo…")) {
            return {{"ok", false}, {"error", {{"code", "cancelled"}, {"message", "Cancelled by client."}, {"retryable", true}}}};
        }
        HttpResult upload = HttpUploadFile(base_url + "/files", api_key, image_bytes, extension);
        if (IsUnauthorized(upload)) {
            base_url = AlternateBaseUrl(base_url);
            upload = HttpUploadFile(base_url + "/files", api_key, image_bytes, extension);
        }
        const json data = ParseTripoData(upload, "upload", error);
        if (!error.empty()) return {{"ok", false}, {"error", {{"code", "upload_failed"}, {"message", error}, {"retryable", true}}}};
        file_token = data.value("file_token", "");
        if (file_token.empty()) {
            return {{"ok", false}, {"error", {{"code", "upload_failed"}, {"message", "Tripo file upload returned no file_token."}, {"retryable", true}}}};
        }
    }

    // Step 2: create the image_to_model task.
    if (progress && !progress(0.10, "Creating Tripo task…")) {
        return {{"ok", false}, {"error", {{"code", "cancelled"}, {"message", "Cancelled by client."}, {"retryable", true}}}};
    }
    json file = {{"type", extension}};
    if (!file_token.empty()) file["file_token"] = file_token;
    else file["url"] = image_url;
    json create_body = {
        {"type", "image_to_model"},
        {"model_version", model_version},
        {"texture", generate_texture},
        {"pbr", enable_pbr},
        {"file", file},
    };
    if (face_limit > 0) create_body["face_limit"] = face_limit;

    HttpResult create = HttpPostJson(base_url + "/generation/image-to-model", api_key, create_body);
    if (IsUnauthorized(create)) {
        base_url = AlternateBaseUrl(base_url);
        create = HttpPostJson(base_url + "/generation/image-to-model", api_key, create_body);
    }
    const json create_data = ParseTripoData(create, "create", error);
    if (!error.empty()) return {{"ok", false}, {"error", {{"code", "create_failed"}, {"message", error}, {"retryable", true}}}};
    const std::string task_id = create_data.value("task_id", "");
    if (task_id.empty()) {
        return {{"ok", false}, {"error", {{"code", "create_failed"}, {"message", "Tripo create task returned no task_id."}, {"retryable", true}}}};
    }

    // Step 3: poll until the task reaches a terminal state.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kTripoPollMaxSeconds);
    json task_data;
    while (true) {
        if (std::chrono::steady_clock::now() > deadline) {
            return {{"ok", false}, {"error", {{"code", "poll_timeout"},
                {"message", "Tripo task " + task_id + " did not finish within the time budget."},
                {"retryable", true}}, {"taskId", task_id}}};
        }
        const HttpResult poll = HttpGet(base_url + "/tasks/" + task_id, api_key, kTripoHttpTimeoutSec);
        task_data = ParseTripoData(poll, "poll", error);
        if (!error.empty()) {
            return {{"ok", false}, {"error", {{"code", "poll_failed"}, {"message", error}, {"retryable", true}}, {"taskId", task_id}}};
        }
        const std::string status = task_data.value("status", "");
        const int task_progress = task_data.value("progress", 0);
        if (progress && !progress(
                0.15 + static_cast<double>(task_progress) * 0.0075,
                "Tripo " + status + " (" + std::to_string(task_progress) + "%)…")) {
            return {{"ok", false}, {"error", {{"code", "cancelled"}, {"message", "Cancelled by client."}, {"retryable", true}}, {"taskId", task_id}}};
        }
        if (status == "success" || status == "failed" || status == "cancelled") break;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    const std::string status = task_data.value("status", "");
    if (status != "success") {
        const std::string message = task_data.value("message", status);
        return {{"ok", false}, {"error", {{"code", "task_failed"},
            {"message", "Tripo task failed: " + message}, {"retryable", true}}, {"taskId", task_id}}};
    }

    // Step 4: download the GLB.
    if (progress && !progress(0.92, "Downloading GLB…")) {
        return {{"ok", false}, {"error", {{"code", "cancelled"}, {"message", "Cancelled by client."}, {"retryable", true}}, {"taskId", task_id}}};
    }
    const json output = task_data.value("output", json::object());
    std::string model_url;
    if (enable_pbr) model_url = output.value("pbr_model", "");
    if (model_url.empty()) model_url = output.value("model_url", "");
    if (model_url.empty()) model_url = output.value("model", "");
    if (model_url.empty()) model_url = output.value("base_model", "");
    if (model_url.empty()) {
        return {{"ok", false}, {"error", {{"code", "download_failed"},
            {"message", "Tripo task succeeded but output.model_url is missing."},
            {"retryable", true}}, {"taskId", task_id}}};
    }

    const HttpResult download = HttpGet(model_url, "", kTripoHttpTimeoutSec);
    if (!download.error.empty() || download.status < 200 || download.status >= 300 || download.body.empty()) {
        return {{"ok", false}, {"error", {{"code", "download_failed"},
            {"message", "GLB download failed: " + (!download.error.empty() ? download.error : "HTTP " + std::to_string(download.status))},
            {"retryable", true}}, {"taskId", task_id}}};
    }

    std::filesystem::create_directories(cache_dir, ec);
    if (ec) {
        return {{"ok", false}, {"error", {{"code", "cache_write_failed"}, {"message", ec.message()}, {"retryable", true}}}};
    }
    const auto temporary = cache_path.string() + ".tmp";
    {
        std::ofstream output_file(temporary, std::ios::binary | std::ios::trunc);
        output_file.write(download.body.data(), static_cast<std::streamsize>(download.body.size()));
        output_file.close();
        if (!output_file) {
            std::filesystem::remove(temporary, ec);
            return {{"ok", false}, {"error", {{"code", "cache_write_failed"}, {"message", "Could not write the GLB cache file."}, {"retryable", true}}}};
        }
    }
    std::filesystem::rename(temporary, cache_path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return {{"ok", false}, {"error", {{"code", "cache_write_failed"}, {"message", ec.message()}, {"retryable", true}}}};
    }

    if (progress) progress(1.0, "Done");
    return {
        {"ok", true},
        {"path", relative_path},
        {"taskId", task_id},
        {"creditsConsumed", task_data.value("credits_consumed", 0)},
        {"cached", false},
        {"bytes", download.body.size()},
    };
}

std::string SseEvent(const std::string& event, const json& data) {
    return "event: " + event + "\ndata: "
        + data.dump(-1, ' ', false, json::error_handler_t::replace) + "\n\n";
}

}  // namespace

void HandleThirdPartyTripoGenerate(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    json body = json::parse(req.body, nullptr, false);
    if (!body.is_object()) {
        WriteJson(res, ErrorBody("invalid_body", "Request body must be a JSON object."), 400);
        return;
    }
    res.status = 200;
    res.set_header("Cache-Control", "no-cache");
    res.set_header("X-Accel-Buffering", "no");
    res.set_chunked_content_provider(
        "text/event-stream",
        [body, started = false](size_t, httplib::DataSink& sink) mutable {
            if (started) return false;
            started = true;
            const TripoProgress progress = [&sink](double percent, const std::string& message) {
                const std::string payload = SseEvent("progress",
                    {{"percent", percent}, {"message", message}});
                return sink.is_writable() && sink.write(payload.data(), payload.size());
            };
            const json result = TripoGenerate(body, progress);
            const std::string payload = SseEvent("result", result);
            if (sink.is_writable()) sink.write(payload.data(), payload.size());
            sink.done();
            return true;
        },
        [](bool) {});
}

void HandleThirdPartyCacheGet(const httplib::Request& req, httplib::Response& res) {
    if (!CheckAgentAuth(req, res)) return;
    const std::string name = req.matches.size() > 1 ? req.matches[1].str() : "";
    const bool valid_name = !name.empty()
        && name.size() < 128
        && name.find("..") == std::string::npos
        && name.find('/') == std::string::npos
        && name.size() > 4
        && name.compare(name.size() - 4, 4, ".glb") == 0;
    if (!valid_name) {
        WriteJson(res, ErrorBody("invalid_cache_name", "Cache name must be a flat *.glb file name."), 400);
        return;
    }
    const auto path = TripoCacheDir() / name;
    if (!IsInsideRoot(path, TripoCacheDir())) {
        WriteJson(res, ErrorBody("invalid_cache_name", "Cache path escapes the cache directory."), 400);
        return;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        WriteJson(res, ErrorBody("cache_miss", "No cached GLB with that name."), 404);
        return;
    }
    const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    res.status = 200;
    res.set_content(bytes, "model/gltf-binary");
}

}  // namespace pcg_server
