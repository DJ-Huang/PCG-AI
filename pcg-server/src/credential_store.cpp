#include "credential_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#endif

#ifdef __APPLE__
#include <Security/Security.h>
#endif

namespace pcg_server {
namespace {

using json = nlohmann::json;

// Only storage-location compatibility remains: existing Tripo credentials must
// survive removal of the embedded chat runtime. No old runtime config is read.
const char* Environment(const char* name, const char* legacy_name) {
    const char* value = std::getenv(name);
    if (value && *value) return value;
    value = std::getenv(legacy_name);
    return value && *value ? value : nullptr;
}

std::filesystem::path ConfigDirectory() {
    if (const char* legacy_path = std::getenv("PCG_AGENT_CONFIG_PATH")) {
        if (*legacy_path) return std::filesystem::path(legacy_path).parent_path();
    }
    const char* home = std::getenv("HOME");
    const auto root = home && *home
        ? std::filesystem::path(home)
        : std::filesystem::temp_directory_path();
    return root / "Library" / "Application Support" / "PICG";
}

std::filesystem::path CredentialPath() {
    if (const char* path = Environment("PCG_CREDENTIALS_PATH", "PCG_AGENT_CREDENTIALS_PATH")) {
        return std::filesystem::path(path);
    }
    return ConfigDirectory() / "credentials.json";
}

bool UsesKeychain() {
    const char* store = Environment("PCG_CREDENTIAL_STORE", "PCG_AGENT_CREDENTIAL_STORE");
    return store && std::string(store) == "keychain";
}

std::mutex& StoreMutex() {
    static std::mutex mutex;
    return mutex;
}

// A malformed or unreadable existing file must not be overwritten with an
// empty store. Keep unrelated entries when saving or clearing the Tripo key.
bool ReadCredentialFile(json& credentials) {
    credentials = json::object();
    const auto path = CredentialPath();
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return false;
    if (!exists) return true;
#ifndef _WIN32
    if (chmod(path.c_str(), S_IRUSR | S_IWUSR) != 0) return false;
#endif
    std::ifstream input(path);
    if (!input) return false;
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object()) return false;
    credentials = document.value("credentials", json::object());
    return credentials.is_object();
}

bool WriteCredentialFile(const json& credentials) {
    const auto path = CredentialPath();
    const auto parent = path.has_parent_path() ? path.parent_path() : std::filesystem::current_path();
    std::error_code ec;
    const bool parent_existed = std::filesystem::exists(parent, ec);
    if (ec) return false;
    std::filesystem::create_directories(parent, ec);
    if (ec) return false;
#ifndef _WIN32
    if ((!parent_existed || parent == ConfigDirectory()) && chmod(parent.c_str(), S_IRWXU) != 0) return false;
#endif
    std::random_device random;
    std::ostringstream nonce;
    for (int i = 0; i < 4; ++i) nonce << std::hex << std::setw(8) << std::setfill('0') << random();
    const std::filesystem::path temporary = path.string() + ".tmp." + nonce.str();
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) return false;
#ifndef _WIN32
        // Set permissions before writing any secret bytes.
        if (chmod(temporary.c_str(), S_IRUSR | S_IWUSR) != 0) {
            output.close();
            std::filesystem::remove(temporary, ec);
            return false;
        }
#endif
        output << json{{"version", 1}, {"credentials", credentials}}.dump(2);
        output.close();
        if (!output) {
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
#else
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
#endif
    return true;
}

#ifdef __APPLE__
CFMutableDictionaryRef KeychainQuery(const std::string& name) {
    const char* service_name = Environment("PCG_KEYCHAIN_SERVICE", "PCG_AGENT_KEYCHAIN_SERVICE");
    // Preserve the historical Keychain service label so existing keys remain
    // readable. This is a storage identifier, not an executable runtime.
    if (!service_name) service_name = "PICG Agent";
    CFStringRef service = CFStringCreateWithCString(kCFAllocatorDefault, service_name, kCFStringEncodingUTF8);
    CFStringRef account = CFStringCreateWithBytes(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(name.data()),
        static_cast<CFIndex>(name.size()), kCFStringEncodingUTF8, false);
    if (!service || !account) {
        if (service) CFRelease(service);
        if (account) CFRelease(account);
        return nullptr;
    }
    CFMutableDictionaryRef query = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (query) {
        CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
        CFDictionarySetValue(query, kSecAttrService, service);
        CFDictionarySetValue(query, kSecAttrAccount, account);
    }
    CFRelease(service);
    CFRelease(account);
    return query;
}

bool ReadKeychain(const std::string& name, json& value) {
    CFMutableDictionaryRef query = KeychainQuery(name);
    if (!query) return false;
    CFDictionarySetValue(query, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(query, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef found = nullptr;
    const OSStatus status = SecItemCopyMatching(query, &found);
    CFRelease(query);
    if (status != errSecSuccess || !found || CFGetTypeID(found) != CFDataGetTypeID()) {
        if (found) CFRelease(found);
        return false;
    }
    const auto data = static_cast<CFDataRef>(found);
    const std::string raw(reinterpret_cast<const char*>(CFDataGetBytePtr(data)),
                          static_cast<size_t>(CFDataGetLength(data)));
    CFRelease(found);
    value = json::parse(raw, nullptr, false);
    return value.is_object();
}

bool WriteKeychain(const std::string& name, const json& value) {
    CFMutableDictionaryRef query = KeychainQuery(name);
    if (!query) return false;
    const std::string raw = value.dump();
    CFDataRef data = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(raw.data()),
                                 static_cast<CFIndex>(raw.size()));
    CFMutableDictionaryRef update = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!data || !update) {
        if (data) CFRelease(data);
        if (update) CFRelease(update);
        CFRelease(query);
        return false;
    }
    CFDictionarySetValue(update, kSecValueData, data);
    OSStatus status = SecItemUpdate(query, update);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(query, kSecValueData, data);
        status = SecItemAdd(query, nullptr);
    }
    CFRelease(update);
    CFRelease(data);
    CFRelease(query);
    return status == errSecSuccess;
}

bool RemoveKeychain(const std::string& name) {
    CFMutableDictionaryRef query = KeychainQuery(name);
    if (!query) return false;
    const OSStatus status = SecItemDelete(query);
    CFRelease(query);
    return status == errSecSuccess || status == errSecItemNotFound;
}
#endif

}  // namespace

const char* CredentialStoreName() {
    return UsesKeychain() ? "macos-keychain" : "protected-file";
}

bool LoadProtectedCredential(const std::string& name, json& value) {
    if (name.empty()) return false;
    std::lock_guard<std::mutex> lock(StoreMutex());
    if (UsesKeychain()) {
#ifdef __APPLE__
        return ReadKeychain(name, value);
#else
        return false;
#endif
    }
    json credentials;
    if (!ReadCredentialFile(credentials)) return false;
    const auto entry = credentials.find(name);
    if (entry == credentials.end() || !entry->is_object()) return false;
    value = *entry;
    return true;
}

bool StoreProtectedCredential(const std::string& name, const json& value) {
    if (name.empty() || !value.is_object()) return false;
    std::lock_guard<std::mutex> lock(StoreMutex());
    if (UsesKeychain()) {
#ifdef __APPLE__
        return WriteKeychain(name, value);
#else
        return false;
#endif
    }
    json credentials;
    if (!ReadCredentialFile(credentials)) return false;
    credentials[name] = value;
    return WriteCredentialFile(credentials);
}

bool DeleteProtectedCredential(const std::string& name) {
    if (name.empty()) return false;
    std::lock_guard<std::mutex> lock(StoreMutex());
    if (UsesKeychain()) {
#ifdef __APPLE__
        return RemoveKeychain(name);
#else
        return false;
#endif
    }
    json credentials;
    if (!ReadCredentialFile(credentials)) return false;
    if (!credentials.contains(name)) return true;
    credentials.erase(name);
    return WriteCredentialFile(credentials);
}

}  // namespace pcg_server
