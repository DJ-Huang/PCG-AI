#include "surface_reconstruction_service.hpp"

#include "elements/oriented_sdf_surface.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace pcg_server {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

std::mutex g_root_mutex;
fs::path g_workspace_root;

json Failure(const std::string& code, const std::string& message) {
    return {
        {"ok", false},
        {"error", {{"code", code}, {"message", message}, {"retryable", false}}},
    };
}

bool IsInside(const fs::path& root, const fs::path& candidate) {
    auto root_it = root.begin();
    auto path_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++path_it) {
        if (path_it == candidate.end() || *root_it != *path_it) return false;
    }
    return true;
}

bool ResolveSourcePath(const std::string& value, fs::path& resolved, std::string& error) {
    if (value.empty()) {
        error = "sourcePath is required";
        return false;
    }
    fs::path root;
    {
        std::lock_guard<std::mutex> lock(g_root_mutex);
        root = g_workspace_root;
    }
    if (root.empty()) {
        error = "surface reconstruction workspace root is not configured";
        return false;
    }
    std::error_code ec;
    fs::path candidate(value);
    if (candidate.is_relative()) candidate = root / candidate;
    candidate = fs::weakly_canonical(candidate, ec);
    if (ec || !IsInside(root, candidate)) {
        error = "sourcePath must resolve inside the PCG workspace";
        return false;
    }
    if (!fs::is_regular_file(candidate, ec) || ec) {
        error = "sourcePath is not a readable mesh file";
        return false;
    }
    std::string extension = candidate.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".glb" && extension != ".gltf" &&
        extension != ".obj" && extension != ".fbx") {
        error = "sourcePath must be a .glb, .gltf, .obj, or .fbx mesh";
        return false;
    }
    resolved = std::move(candidate);
    return true;
}

json Vec3Json(const pcg::internal::data::PcgVec3& value) {
    return json::array({value.x, value.y, value.z});
}

std::string EncodeBase64(const std::vector<uint8_t>& input) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    for (size_t offset = 0; offset < input.size(); offset += 3) {
        const uint32_t a = input[offset];
        const uint32_t b = offset + 1 < input.size() ? input[offset + 1] : 0;
        const uint32_t c = offset + 2 < input.size() ? input[offset + 2] : 0;
        const uint32_t value = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(value >> 18) & 63u]);
        output.push_back(alphabet[(value >> 12) & 63u]);
        output.push_back(offset + 1 < input.size() ? alphabet[(value >> 6) & 63u] : '=');
        output.push_back(offset + 2 < input.size() ? alphabet[value & 63u] : '=');
    }
    return output;
}

std::string DataUri(const pcg::internal::elements::EmbeddedTexturePayload& texture) {
    if (texture.empty()) return {};
    return "data:" + texture.mime_type + ";base64," + EncodeBase64(texture.bytes);
}

} // namespace

void ConfigureSurfaceReconstructionRoot(const std::filesystem::path& workspace_root) {
    std::error_code ec;
    fs::path root = fs::weakly_canonical(workspace_root, ec);
    if (ec) root = fs::absolute(workspace_root, ec);
    std::lock_guard<std::mutex> lock(g_root_mutex);
    g_workspace_root = std::move(root);
}

json BuildOrientedSdfNodeData(const json& request) {
    if (!request.is_object()) return Failure("invalid_request", "JSON object required");
    const std::string source_path = request.value("sourcePath", "");
    fs::path resolved;
    std::string error;
    if (!ResolveSourcePath(source_path, resolved, error))
        return Failure("invalid_source_path", error);

    double requested_cell_size = 0.0;
    if (request.contains("cellSize")) {
        if (!request["cellSize"].is_number())
            return Failure("invalid_cell_size", "cellSize must be numeric");
        requested_cell_size = request["cellSize"].get<double>();
        if (!std::isfinite(requested_cell_size) || requested_cell_size < 0.0005)
            return Failure("invalid_cell_size", "cellSize must be finite and at least 0.0005");
    }
    double requested_sample_spacing = request.value(
        "sampleSpacing", requested_cell_size > 0.0 ? requested_cell_size * 0.75 : 0.0);
    if (!std::isfinite(requested_sample_spacing) || requested_sample_spacing < 0.0)
        return Failure("invalid_sample_spacing", "sampleSpacing must be finite and non-negative");

    std::string point_cloud;
    pcg::internal::elements::OrientedPointCloudStats stats;
    if (!pcg::internal::elements::encode_oriented_point_cloud_file(
            resolved.string(), point_cloud, stats, error, requested_sample_spacing)) {
        return Failure("point_cloud_bake_failed", error);
    }
    pcg::internal::elements::EmbeddedPbrTextures textures;
    if (!pcg::internal::elements::extract_embedded_pbr_textures_file(
            resolved.string(), textures, error)) {
        return Failure("embedded_texture_bake_failed", error);
    }

    const double largest_extent = std::max({
        stats.maximum.x - stats.minimum.x,
        stats.maximum.y - stats.minimum.y,
        stats.maximum.z - stats.minimum.z,
    });
    const double default_cell = std::max(0.0005, largest_extent / 333.0);
    const double cell_size = request.value("cellSize", default_cell);
    const double support_radius = request.value("supportRadiusCells", 2.5);
    const double iso_offset = request.value("isoOffset", 0.0);
    const int64_t max_active_cells = request.value("maxActiveCells", int64_t{3000000});
    if (!std::isfinite(cell_size) || cell_size < 0.0005 || cell_size > largest_extent) {
        return Failure("invalid_cell_size", "cellSize must be finite and smaller than the source extent");
    }
    if (!std::isfinite(support_radius) || support_radius < 1.25 || support_radius > 6.0) {
        return Failure("invalid_support_radius", "supportRadiusCells must be in [1.25, 6]");
    }
    if (!std::isfinite(iso_offset) || std::abs(iso_offset) >= cell_size * support_radius * 0.95) {
        return Failure("invalid_iso_offset", "isoOffset must stay inside the supported SDF band");
    }
    if (max_active_cells < 1000 || max_active_cells > 5000000) {
        return Failure("invalid_active_cell_limit", "maxActiveCells must be in [1000, 5000000]");
    }

    const pcg::internal::data::PcgVec3 center{
        (stats.minimum.x + stats.maximum.x) * 0.5,
        (stats.minimum.y + stats.maximum.y) * 0.5,
        (stats.minimum.z + stats.maximum.z) * 0.5,
    };
    const pcg::internal::data::PcgVec3 size{
        stats.maximum.x - stats.minimum.x,
        stats.maximum.y - stats.minimum.y,
        stats.maximum.z - stats.minimum.z,
    };
    std::string source_extension = resolved.extension().string();
    std::transform(source_extension.begin(), source_extension.end(), source_extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    json node_data = {
        {"__nodeTitle", request.value("title", "High-Fidelity SDF Surface")},
        {"pointCloud", std::move(point_cloud)},
        {"cellSize", cell_size},
        {"supportRadiusCells", support_radius},
        {"isoOffset", iso_offset},
        {"maxActiveCells", max_active_cells},
        {"transferColors", request.value("transferColors", true)},
        {"transferUvs", request.value("transferUvs", true)},
        {"flipUvV", request.value("flipUvV",
            source_extension == ".glb" || source_extension == ".gltf")},
        {"sourceReference", source_path},
        {"showEncodedPayload", false},
        {"__semantic", {
            {"componentId", request.value("componentId", "reference.surface")},
            {"bounds", {{"center", Vec3Json(center)}, {"size", Vec3Json(size)}}},
        }},
    };
    return {
        {"ok", true},
        {"algorithm", "dense-oriented-mls-sdf-surface-nets"},
        {"topologyCopied", false},
        {"sourcePointCount", stats.point_count},
        {"sourceVertexCount", stats.source_point_count},
        {"sampleSpacing", stats.sampling_spacing},
        {"payloadBytes", stats.payload_bytes},
        {"hasSourceVertexColors", stats.has_source_colors},
        {"hasSourceUvs", stats.has_source_uvs},
        {"embeddedTextureBytes", {
            {"baseColor", textures.base_color.bytes.size()},
            {"normal", textures.normal.bytes.size()},
            {"orm", textures.orm.bytes.size()},
        }},
        {"bounds", {{"min", Vec3Json(stats.minimum)}, {"max", Vec3Json(stats.maximum)}}},
        {"nodeType", "OrientedSdfSurface"},
        {"nodeData", std::move(node_data)},
        {"suggestedMaterialData", {
            {"__nodeTitle", request.value("materialTitle", "Reconstructed Source Material")},
            {"materialName", request.value("materialName", "ReconstructedSourceMaterial")},
            {"shaderId", "pcg.standard-pbr"},
            {"baseColor", "#ffffff"},
            {"baseColorMap", DataUri(textures.base_color)},
            {"metallic", 1.0},
            {"metallicMap", DataUri(textures.orm)},
            {"roughness", 1.0},
            {"roughnessMap", DataUri(textures.orm)},
            {"normalMap", DataUri(textures.normal)},
            {"normalScale", 1.0},
            {"aoMap", DataUri(textures.orm)},
            {"aoIntensity", 1.0},
            {"emissiveColor", "#000000"},
            {"emissiveMap", ""},
            {"emissiveIntensity", 0.0},
            {"opacity", 1.0},
            {"alphaMode", "opaque"},
            {"alphaCutoff", 0.5},
            {"doubleSided", false},
            {"unityShaderGuid", ""},
            {"unityShaderName", ""},
            {"unityPropertiesJson", "{}"},
        }},
    };
}

void HandleBuildOrientedSdfNodeData(const httplib::Request& req, httplib::Response& res) {
    const json request = json::parse(req.body, nullptr, false);
    if (request.is_discarded()) {
        res.status = 400;
        res.set_content(Failure("invalid_json", "Malformed JSON body").dump(), "application/json");
        return;
    }
    json result = BuildOrientedSdfNodeData(request);
    res.status = result.value("ok", false) ? 200 : 400;
    res.set_content(result.dump(), "application/json");
}

void HandlePreservedGltfGet(const httplib::Request& req, httplib::Response& res) {
    fs::path resolved;
    std::string error;
    const std::string path = req.has_param("path") ? req.get_param_value("path") : "";
    if (!ResolveSourcePath(path, resolved, error)) {
        res.status = 400;
        res.set_content(Failure("invalid_source_path", error).dump(), "application/json");
        return;
    }
    std::string extension = resolved.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".glb") {
        res.status = 400;
        res.set_content(Failure("invalid_rig_source", "Preserved rig source must be a self-contained GLB.").dump(),
                        "application/json");
        return;
    }
    std::ifstream input(resolved, std::ios::binary);
    if (!input) {
        res.status = 404;
        res.set_content(Failure("source_missing", "Preserved rig source could not be opened.").dump(),
                        "application/json");
        return;
    }
    const std::string bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    res.status = 200;
    res.set_header("Cache-Control", "no-cache");
    res.set_content(bytes, "model/gltf-binary");
}

} // namespace pcg_server
