#include "asset_path.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <vector>

namespace pcg::internal {
namespace {

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void hash_bytes(uint64_t& hash, const void* data, size_t size)
{
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
}

void hash_file_metadata(uint64_t& hash, const std::filesystem::path& path)
{
    const std::string normalized = path.lexically_normal().generic_string();
    hash_bytes(hash, normalized.data(), normalized.size());
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        return;
    hash_bytes(hash, &size, sizeof(size));
    const auto write_time = std::filesystem::last_write_time(path, error);
    if (!error) {
        const auto ticks = write_time.time_since_epoch().count();
        hash_bytes(hash, &ticks, sizeof(ticks));
    }
}

std::vector<std::filesystem::path> referenced_asset_files(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    std::vector<std::filesystem::path> result;
    if (extension == ".gltf") {
        try {
            std::ifstream stream(path);
            nlohmann::json document;
            stream >> document;
            for (const char* collection : {"buffers", "images"}) {
                for (const auto& item : document.value(collection, nlohmann::json::array())) {
                    const std::string uri = item.value("uri", std::string{});
                    if (!uri.empty() && uri.rfind("data:", 0) != 0 &&
                        uri.find("://") == std::string::npos)
                        result.push_back(path.parent_path() / uri);
                }
            }
        } catch (...) {
            // ImportMesh reports malformed source files; hashing remains best-effort.
        }
    } else if (extension == ".obj") {
        std::ifstream stream(path);
        std::string line;
        while (std::getline(stream, line)) {
            if (line.rfind("mtllib ", 0) != 0)
                continue;
            std::string value = line.substr(7);
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
                value.pop_back();
            if (!value.empty())
                result.push_back(path.parent_path() / value);
        }
    }
    return result;
}

} // namespace

std::filesystem::path resolve_asset_path(const nlohmann::json& node_data)
{
    const std::filesystem::path value = node_data.value("path", std::string{});
    if (value.empty() || value.is_absolute())
        return value.lexically_normal();
    const std::filesystem::path project_root =
        node_data.value("projectRoot", std::string{});
    if (!project_root.empty())
        return (project_root / value).lexically_normal();
    return std::filesystem::absolute(value).lexically_normal();
}

uint64_t hash_asset_dependency(const std::filesystem::path& path)
{
    uint64_t hash = kFnvOffsetBasis;
    hash_file_metadata(hash, path);
    for (const auto& dependency : referenced_asset_files(path))
        hash_file_metadata(hash, dependency);
    return hash;
}

} // namespace pcg::internal
