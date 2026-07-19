#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>

namespace pcg::internal {

std::filesystem::path resolve_asset_path(const nlohmann::json& node_data);
uint64_t hash_asset_dependency(const std::filesystem::path& path);

} // namespace pcg::internal
