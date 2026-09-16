#pragma once

#include <string>
#include <nlohmann/json_fwd.hpp>

namespace pcg_server {

// Shared secret storage for 3D generation services. No provider or chat runtime
// is involved. Values stay on the local server, never in graph documents.
const char* CredentialStoreName();
bool LoadProtectedCredential(const std::string& name, nlohmann::json& value);
bool StoreProtectedCredential(const std::string& name, const nlohmann::json& value);
bool DeleteProtectedCredential(const std::string& name);

}  // namespace pcg_server
