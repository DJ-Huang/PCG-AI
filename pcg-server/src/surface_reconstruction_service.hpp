#pragma once

#include <filesystem>

#include <nlohmann/json.hpp>

#include "httplib.h"

namespace pcg_server {

void ConfigureSurfaceReconstructionRoot(const std::filesystem::path& workspace_root);

/** Build manifest-ready OrientedSdfSurface data from a workspace mesh file. */
nlohmann::json BuildOrientedSdfNodeData(const nlohmann::json& request);

void HandleBuildOrientedSdfNodeData(const httplib::Request& req, httplib::Response& res);

/** Serve a workspace-contained, self-contained GLB for the source-rig preservation route. */
void HandlePreservedGltfGet(const httplib::Request& req, httplib::Response& res);

} // namespace pcg_server
