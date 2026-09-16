#include "cook_service.hpp"

#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "pcg_api.h"
#include "pcg_fbx_api.h"

namespace pcg_server {
namespace {

constexpr uint32_t kResultMagic = 0x52474350u;  // 'PCGR'
constexpr uint32_t kResultVersion = 1u;
constexpr uint32_t kMultiSpawnMagic = 0x534D4350u;  // 'PCMS'

constexpr int kErrBufSize = 1024;
constexpr int kDefaultJsonBuf = 8 * 1024 * 1024;
constexpr int kDefaultMeshBuf = 32 * 1024 * 1024;
constexpr int kDefaultPointsBuf = 16 * 1024 * 1024;
constexpr int kDefaultGeometryBuf = 16 * 1024 * 1024;
constexpr int kDefaultHeightFieldBuf = 8 * 1024 * 1024;
constexpr int kDefaultPerfBuf = 64 * 1024;
constexpr int kMaxBinaryBuf = 256 * 1024 * 1024;
constexpr int kMaxGrowRetries = 3;

std::mutex g_cook_mutex;
std::mutex g_job_state_mutex;
std::string g_active_client_job_id;
std::unordered_set<std::string> g_pending_client_job_ids;
std::unordered_set<std::string> g_cancelled_queued_job_ids;

bool ConsumeQueuedCancellation(const std::string& job_id) {
    if (job_id.empty()) {
        return false;
    }
    const auto it = g_cancelled_queued_job_ids.find(job_id);
    if (it == g_cancelled_queued_job_ids.end()) {
        return false;
    }
    g_cancelled_queued_job_ids.erase(it);
    return true;
}

void RegisterPendingClientJob(const std::string& job_id) {
    if (!job_id.empty()) {
        g_pending_client_job_ids.insert(job_id);
    }
}

void UnregisterPendingClientJob(const std::string& job_id) {
    g_pending_client_job_ids.erase(job_id);
}

void SetActiveClientJobId(const std::string& job_id) {
    g_active_client_job_id = job_id;
}

void ClearActiveClientJobId(const std::string& job_id) {
    if (g_active_client_job_id == job_id) {
        g_active_client_job_id.clear();
    }
}

void AppendU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

void AppendI32(std::vector<uint8_t>& out, int32_t v) {
    AppendU32(out, static_cast<uint32_t>(v));
}

void AppendF64(std::vector<uint8_t>& out, double v) {
    uint64_t bits = 0;
    static_assert(sizeof(double) == sizeof(uint64_t), "unexpected double size");
    std::memcpy(&bits, &v, sizeof(bits));
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<uint8_t>((bits >> (8 * i)) & 0xff));
    }
}

void AppendBlob(std::vector<uint8_t>& out, const void* data, size_t len) {
    AppendU32(out, static_cast<uint32_t>(len));
    if (len == 0 || data == nullptr) {
        return;
    }
    const auto* bytes = static_cast<const uint8_t*>(data);
    out.insert(out.end(), bytes, bytes + len);
}

bool TryParseNeedBytes(const std::string& error, std::string& kind, int& need_bytes) {
    kind.clear();
    need_bytes = 0;
    const auto need_pos = error.find("need ");
    if (need_pos == std::string::npos) {
        return false;
    }
    const auto num_start = need_pos + 5;
    const auto bytes_pos = error.find(" bytes", num_start);
    if (bytes_pos == std::string::npos || bytes_pos <= num_start) {
        return false;
    }
    try {
        need_bytes = std::stoi(error.substr(num_start, bytes_pos - num_start));
    } catch (...) {
        return false;
    }
    if (need_bytes <= 0) {
        return false;
    }
    if (error.find("JSON buffer") != std::string::npos) {
        kind = "json";
    } else if (error.find("Point binary") != std::string::npos) {
        kind = "points";
    } else if (error.find("Mesh binary") != std::string::npos) {
        kind = "mesh";
    } else {
        return false;
    }
    return true;
}

uint32_t ReadU32(const uint8_t* p) {
    uint32_t v = 0;
    std::memcpy(&v, p, 4);
    return v;
}

int32_t ReadI32(const uint8_t* p) {
    return static_cast<int32_t>(ReadU32(p));
}

size_t ComputeMultiSpawnPayloadSize(const std::vector<uint8_t>& mesh_buf) {
    if (mesh_buf.size() < 12) {
        return mesh_buf.size();
    }
    const int count = ReadI32(mesh_buf.data() + 8);
    if (count <= 0) {
        return 12;
    }
    const size_t header = 12 + static_cast<size_t>(count) * 8u;
    if (mesh_buf.size() < header) {
        return mesh_buf.size();
    }
    size_t total = header;
    const size_t size_offset = 12 + static_cast<size_t>(count) * 4u;
    for (int i = 0; i < count; ++i) {
        total += static_cast<size_t>(ReadI32(mesh_buf.data() + size_offset + static_cast<size_t>(i) * 4u));
    }
    return total;
}

size_t ComputeMeshBinaryPayloadSize(
    const std::vector<uint8_t>& mesh_buf,
    int vertex_count,
    int index_count) {
    if (mesh_buf.size() >= 12 && ReadU32(mesh_buf.data()) == kMultiSpawnMagic) {
        return ComputeMultiSpawnPayloadSize(mesh_buf);
    }
    if (mesh_buf.size() < 16 || ReadU32(mesh_buf.data()) != PCG_MESH_BINARY_MAGIC) {
        return 0;
    }

    if (vertex_count <= 0 || index_count <= 0) {
        vertex_count = ReadI32(mesh_buf.data() + 8);
        index_count = ReadI32(mesh_buf.data() + 12);
    }
    if (vertex_count < 0 || index_count < 0) {
        return 0;
    }

    int header_size = 16;
    int normal_size = 0;
    int color_size = 0;
    int uv_size = 0;
    int material_size = 0;
    if (mesh_buf.size() >= 20) {
        const uint32_t version = ReadU32(mesh_buf.data() + 4);
        if (version == 2u || version == 3u) {
            header_size = version == 3u ? 24 : 20;
            const uint32_t flags = ReadU32(mesh_buf.data() + 16);
            if ((flags & 0x1u) != 0) {
                normal_size = vertex_count * 12;
            }
            if ((flags & 0x2u) != 0) {
                color_size = vertex_count * 16;
            }
            if ((flags & 0x4u) != 0) {
                uv_size = vertex_count * 8;
            }
            if (version == 3u && mesh_buf.size() >= 24) {
                material_size = ReadI32(mesh_buf.data() + 20);
            }
        }
    }
    const size_t needed = static_cast<size_t>(header_size) +
                          static_cast<size_t>(vertex_count) * 12u +
                          static_cast<size_t>(index_count) * 4u +
                          static_cast<size_t>(normal_size) +
                          static_cast<size_t>(color_size) +
                          static_cast<size_t>(uv_size) +
                          static_cast<size_t>(material_size);
    return needed <= mesh_buf.size() ? needed : mesh_buf.size();
}

size_t ComputePointsPayloadSize(
    const std::vector<uint8_t>& points_buf,
    int point_count,
    uint32_t attr_flags) {
    if (point_count < 0 || points_buf.size() < 16 ||
        ReadU32(points_buf.data()) != PCG_POINT_BINARY_MAGIC) {
        return 0;
    }
    int needed = 0;
    if (pcg_point_binary_size_for_counts(point_count, attr_flags, &needed) == PCG_OK &&
        needed > 0 && static_cast<size_t>(needed) <= points_buf.size()) {
        return static_cast<size_t>(needed);
    }
    return 0;
}

struct OwnedTexture {
    std::string slot_id;
    int width = 0;
    int height = 0;
    std::vector<float> rgba;
};

struct OwnedMesh {
    std::string slot_id;
    int vertex_count = 0;
    int index_count = 0;
    std::vector<float> positions;
    std::vector<uint32_t> indices;
};

struct OwnedSpline {
    std::string slot_id;
    int spline_count = 0;
    std::vector<int> point_counts;
    std::vector<float> positions;
    std::vector<uint8_t> closed;
};

struct OwnedHeightField {
    std::string slot_id;
    int resolution_x = 0;
    int resolution_z = 0;
    double size_x = 0;
    double size_z = 0;
    double center_x = 0;
    double center_y = 0;
    double center_z = 0;
    int sampling = 0;
    int orientation = 0;
    std::vector<float> height;
    std::vector<float> mask;
};

const httplib::MultipartFormData* FindPart(
    const httplib::MultipartFormDataMap& files,
    const std::string& name) {
    const auto it = files.find(name);
    if (it == files.end()) {
        return nullptr;
    }
    return &it->second;
}

/** Text parts from HttpClient StringContent may land in params, not files. */
std::string GetPartContent(const httplib::Request& req, const std::string& name) {
    if (const auto* part = FindPart(req.files, name)) {
        return part->content;
    }
    return req.get_param_value(name);
}

bool ParseCancelJobId(const std::string& text, std::string& job_id) {
    job_id.clear();
    if (text.empty()) {
        return true;
    }
    try {
        const auto j = nlohmann::json::parse(text);
        if (j.contains("job_id") && j.at("job_id").is_string()) {
            job_id = j.at("job_id").get<std::string>();
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseMetaJson(const std::string& text, int& seed, std::string& job_id, std::string& error) {
    seed = 42;
    job_id.clear();
    try {
        const auto j = nlohmann::json::parse(text.empty() ? "{}" : text);
        if (j.contains("seed")) {
            seed = j.at("seed").get<int>();
        }
        if (j.contains("job_id") && j.at("job_id").is_string()) {
            job_id = j.at("job_id").get<std::string>();
        }
        return true;
    } catch (const std::exception& ex) {
        error = std::string("Invalid meta JSON: ") + ex.what();
        return false;
    }
}

bool LoadTextures(
    const httplib::MultipartFormDataMap& files,
    std::vector<OwnedTexture>& out,
    std::string& error) {
    for (int i = 0; ; ++i) {
        const auto meta_name = "tex_meta_" + std::to_string(i);
        const auto* meta_part = FindPart(files, meta_name);
        if (meta_part == nullptr) {
            break;
        }
        const auto* data_part = FindPart(files, "tex_data_" + std::to_string(i));
        if (data_part == nullptr) {
            error = "Missing tex_data_" + std::to_string(i);
            return false;
        }
        try {
            const auto j = nlohmann::json::parse(meta_part->content);
            OwnedTexture tex;
            tex.slot_id = j.at("slot_id").get<std::string>();
            tex.width = j.at("width").get<int>();
            tex.height = j.at("height").get<int>();
            const size_t expected =
                static_cast<size_t>(tex.width) * static_cast<size_t>(tex.height) * 4u;
            if (data_part->content.size() != expected * sizeof(float)) {
                error = "Texture " + tex.slot_id + " rgba size mismatch";
                return false;
            }
            tex.rgba.resize(expected);
            std::memcpy(tex.rgba.data(), data_part->content.data(), data_part->content.size());
            out.push_back(std::move(tex));
        } catch (const std::exception& ex) {
            error = std::string("Invalid ") + meta_name + ": " + ex.what();
            return false;
        }
    }
    return true;
}

bool LoadMeshes(
    const httplib::MultipartFormDataMap& files,
    std::vector<OwnedMesh>& out,
    std::string& error) {
    for (int i = 0; ; ++i) {
        const auto meta_name = "mesh_meta_" + std::to_string(i);
        const auto* meta_part = FindPart(files, meta_name);
        if (meta_part == nullptr) {
            break;
        }
        const auto* pos_part = FindPart(files, "mesh_pos_" + std::to_string(i));
        const auto* idx_part = FindPart(files, "mesh_idx_" + std::to_string(i));
        if (pos_part == nullptr || idx_part == nullptr) {
            error = "Missing mesh position/index parts for index " + std::to_string(i);
            return false;
        }
        try {
            const auto j = nlohmann::json::parse(meta_part->content);
            OwnedMesh mesh;
            mesh.slot_id = j.at("slot_id").get<std::string>();
            mesh.vertex_count = j.at("vertex_count").get<int>();
            mesh.index_count = j.at("index_count").get<int>();
            const size_t pos_floats = static_cast<size_t>(mesh.vertex_count) * 3u;
            const size_t idx_count = static_cast<size_t>(mesh.index_count);
            if (pos_part->content.size() != pos_floats * sizeof(float) ||
                idx_part->content.size() != idx_count * sizeof(uint32_t)) {
                error = "Mesh " + mesh.slot_id + " buffer size mismatch";
                return false;
            }
            mesh.positions.resize(pos_floats);
            mesh.indices.resize(idx_count);
            std::memcpy(mesh.positions.data(), pos_part->content.data(), pos_part->content.size());
            std::memcpy(mesh.indices.data(), idx_part->content.data(), idx_part->content.size());
            out.push_back(std::move(mesh));
        } catch (const std::exception& ex) {
            error = std::string("Invalid ") + meta_name + ": " + ex.what();
            return false;
        }
    }
    return true;
}

bool LoadSplines(
    const httplib::MultipartFormDataMap& files,
    std::vector<OwnedSpline>& out,
    std::string& error) {
    for (int i = 0; ; ++i) {
        const auto meta_name = "spline_meta_" + std::to_string(i);
        const auto* meta_part = FindPart(files, meta_name);
        if (meta_part == nullptr) {
            break;
        }
        const auto* counts_part = FindPart(files, "spline_counts_" + std::to_string(i));
        const auto* pos_part = FindPart(files, "spline_pos_" + std::to_string(i));
        const auto* closed_part = FindPart(files, "spline_closed_" + std::to_string(i));
        if (counts_part == nullptr || pos_part == nullptr || closed_part == nullptr) {
            error = "Missing spline parts for index " + std::to_string(i);
            return false;
        }
        try {
            const auto j = nlohmann::json::parse(meta_part->content);
            OwnedSpline spline;
            spline.slot_id = j.at("slot_id").get<std::string>();
            spline.spline_count = j.at("spline_count").get<int>();
            if (counts_part->content.size() !=
                    static_cast<size_t>(spline.spline_count) * sizeof(int) ||
                closed_part->content.size() != static_cast<size_t>(spline.spline_count)) {
                error = "Spline " + spline.slot_id + " counts/closed size mismatch";
                return false;
            }
            spline.point_counts.resize(static_cast<size_t>(spline.spline_count));
            spline.closed.resize(static_cast<size_t>(spline.spline_count));
            std::memcpy(spline.point_counts.data(), counts_part->content.data(),
                        counts_part->content.size());
            std::memcpy(spline.closed.data(), closed_part->content.data(),
                        closed_part->content.size());
            size_t total_points = 0;
            for (int c : spline.point_counts) {
                if (c < 0) {
                    error = "Spline " + spline.slot_id + " has negative point count";
                    return false;
                }
                total_points += static_cast<size_t>(c);
            }
            if (pos_part->content.size() != total_points * 3u * sizeof(float)) {
                error = "Spline " + spline.slot_id + " positions size mismatch";
                return false;
            }
            spline.positions.resize(total_points * 3u);
            std::memcpy(spline.positions.data(), pos_part->content.data(), pos_part->content.size());
            out.push_back(std::move(spline));
        } catch (const std::exception& ex) {
            error = std::string("Invalid ") + meta_name + ": " + ex.what();
            return false;
        }
    }
    return true;
}

bool LoadHeightFields(
    const httplib::MultipartFormDataMap& files,
    std::vector<OwnedHeightField>& out,
    std::string& error) {
    for (int i = 0; ; ++i) {
        const auto meta_name = "hf_meta_" + std::to_string(i);
        const auto* meta_part = FindPart(files, meta_name);
        if (meta_part == nullptr) {
            break;
        }
        const auto* height_part = FindPart(files, "hf_height_" + std::to_string(i));
        if (height_part == nullptr) {
            error = "Missing hf_height_" + std::to_string(i);
            return false;
        }
        try {
            const auto j = nlohmann::json::parse(meta_part->content);
            OwnedHeightField hf;
            hf.slot_id = j.at("slot_id").get<std::string>();
            hf.resolution_x = j.at("resolution_x").get<int>();
            hf.resolution_z = j.at("resolution_z").get<int>();
            hf.size_x = j.at("size_x").get<double>();
            hf.size_z = j.at("size_z").get<double>();
            hf.center_x = j.value("center_x", 0.0);
            hf.center_y = j.value("center_y", 0.0);
            hf.center_z = j.value("center_z", 0.0);
            hf.sampling = j.value("sampling", 0);
            hf.orientation = j.value("orientation", 0);
            const size_t samples =
                static_cast<size_t>(hf.resolution_x) * static_cast<size_t>(hf.resolution_z);
            if (height_part->content.size() != samples * sizeof(float)) {
                error = "HeightField " + hf.slot_id + " height size mismatch";
                return false;
            }
            hf.height.resize(samples);
            std::memcpy(hf.height.data(), height_part->content.data(), height_part->content.size());
            if (const auto* mask_part = FindPart(files, "hf_mask_" + std::to_string(i))) {
                if (mask_part->content.size() != samples * sizeof(float)) {
                    error = "HeightField " + hf.slot_id + " mask size mismatch";
                    return false;
                }
                hf.mask.resize(samples);
                std::memcpy(hf.mask.data(), mask_part->content.data(), mask_part->content.size());
            }
            out.push_back(std::move(hf));
        } catch (const std::exception& ex) {
            error = std::string("Invalid ") + meta_name + ": " + ex.what();
            return false;
        }
    }
    return true;
}

std::vector<uint8_t> EncodeResult(
    PcgResultCode code,
    int kind,
    const PcgCookStats& stats,
    int point_count,
    uint32_t point_attr_flags,
    int vertex_count,
    int index_count,
    const std::string& error,
    const char* json,
    size_t json_len,
    const uint8_t* mesh,
    size_t mesh_len,
    const uint8_t* points,
    size_t points_len,
    const uint8_t* geometry,
    size_t geometry_len,
    const uint8_t* heightfield,
    size_t heightfield_len,
    const char* perf,
    size_t perf_len) {
    std::vector<uint8_t> out;
    out.reserve(256 + json_len + mesh_len + points_len + geometry_len + heightfield_len + perf_len);

    AppendU32(out, kResultMagic);
    AppendU32(out, kResultVersion);
    AppendI32(out, static_cast<int32_t>(code));
    AppendU32(out, static_cast<uint32_t>(kind));
    AppendI32(out, stats.nodes_executed);
    AppendI32(out, stats.nodes_skipped);
    AppendF64(out, stats.graph_execute_ms);
    AppendF64(out, stats.binary_write_ms);
    AppendU32(out, static_cast<uint32_t>(point_count));
    AppendU32(out, point_attr_flags);
    AppendI32(out, vertex_count);
    AppendI32(out, index_count);

    AppendBlob(out, error.data(), error.size());
    AppendBlob(out, json, json_len);
    AppendBlob(out, mesh, mesh_len);
    AppendBlob(out, points, points_len);
    AppendBlob(out, geometry, geometry_len);
    AppendBlob(out, heightfield, heightfield_len);
    AppendBlob(out, perf, perf_len);
    return out;
}

}  // namespace

void HandleCancel(const httplib::Request& req, httplib::Response& res) {
    std::string cancel_job_id;
    if (!ParseCancelJobId(req.body, cancel_job_id)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid cancel JSON"})", "application/json");
        return;
    }

    bool canceled = false;
    {
        std::lock_guard<std::mutex> lock(g_job_state_mutex);
        if (!cancel_job_id.empty() && cancel_job_id == g_active_client_job_id) {
            pcg_request_cancel();
            canceled = true;
        } else if (!cancel_job_id.empty() &&
                   g_pending_client_job_ids.count(cancel_job_id) > 0) {
            g_cancelled_queued_job_ids.insert(cancel_job_id);
            canceled = true;
        }
    }

    res.status = 200;
    res.set_content(
        nlohmann::json{{"ok", true}, {"canceled", canceled}}.dump(),
        "application/json");
}

void HandleCook(const httplib::Request& req, httplib::Response& res) {
    if (!req.is_multipart_form_data()) {
        res.status = 400;
        res.set_content(R"({"error":"expected multipart/form-data"})", "application/json");
        return;
    }

    std::string parse_error;
    int seed = 42;
    std::string job_id;
    const std::string meta_content = GetPartContent(req, "meta");
    if (!meta_content.empty()) {
        if (!ParseMetaJson(meta_content, seed, job_id, parse_error)) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", parse_error}}.dump(), "application/json");
            return;
        }
    }

    const std::string graph_json = GetPartContent(req, "graph");
    if (graph_json.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"missing graph part"})", "application/json");
        return;
    }

    std::vector<OwnedTexture> textures;
    std::vector<OwnedMesh> meshes;
    std::vector<OwnedSpline> splines;
    std::vector<OwnedHeightField> heightfields;
    if (!LoadTextures(req.files, textures, parse_error) ||
        !LoadMeshes(req.files, meshes, parse_error) ||
        !LoadSplines(req.files, splines, parse_error) ||
        !LoadHeightFields(req.files, heightfields, parse_error)) {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", parse_error}}.dump(), "application/json");
        return;
    }

    std::vector<PcgTextureSlot> tex_slots(textures.size());
    for (size_t i = 0; i < textures.size(); ++i) {
        tex_slots[i] = PcgTextureSlot{
            textures[i].slot_id.c_str(),
            textures[i].width,
            textures[i].height,
            textures[i].rgba.data(),
        };
    }
    std::vector<PcgMeshSlot> mesh_slots(meshes.size());
    for (size_t i = 0; i < meshes.size(); ++i) {
        mesh_slots[i] = PcgMeshSlot{
            meshes[i].slot_id.c_str(),
            meshes[i].vertex_count,
            meshes[i].index_count,
            meshes[i].positions.data(),
            meshes[i].indices.data(),
        };
    }
    std::vector<PcgSplineSlot> spline_slots(splines.size());
    for (size_t i = 0; i < splines.size(); ++i) {
        spline_slots[i] = PcgSplineSlot{
            splines[i].slot_id.c_str(),
            splines[i].spline_count,
            splines[i].point_counts.data(),
            splines[i].positions.data(),
            splines[i].closed.data(),
        };
    }
    std::vector<PcgHeightFieldSlotV10> hf_slots(heightfields.size());
    for (size_t i = 0; i < heightfields.size(); ++i) {
        hf_slots[i] = PcgHeightFieldSlotV10{
            heightfields[i].slot_id.c_str(),
            heightfields[i].resolution_x,
            heightfields[i].resolution_z,
            heightfields[i].size_x,
            heightfields[i].size_z,
            heightfields[i].center_x,
            heightfields[i].center_y,
            heightfields[i].center_z,
            heightfields[i].sampling,
            heightfields[i].orientation,
            static_cast<int>(heightfields[i].height.size()),
            static_cast<int>(heightfields[i].mask.size()),
            heightfields[i].height.data(),
            heightfields[i].mask.empty() ? nullptr : heightfields[i].mask.data(),
        };
    }

    std::vector<char> json_buf(kDefaultJsonBuf, '\0');
    std::vector<uint8_t> mesh_buf(kDefaultMeshBuf, 0);
    std::vector<uint8_t> points_buf(kDefaultPointsBuf, 0);
    std::vector<uint8_t> geometry_buf(kDefaultGeometryBuf, 0);
    std::vector<uint8_t> heightfield_buf(kDefaultHeightFieldBuf, 0);
    std::vector<char> perf_buf(kDefaultPerfBuf, '\0');
    char err_buf[kErrBufSize];

    int kind = 0;
    int point_count = 0;
    uint32_t point_attr_flags = 0;
    int vertex_count = 0;
    int index_count = 0;
    int geometry_bytes = 0;
    int heightfield_bytes = 0;
    PcgCookStats stats{};
    PcgResultCode rc = PCG_ERR_EXECUTION;
    bool cancelled_before_execute = false;

    {
        std::lock_guard<std::mutex> job_lock(g_job_state_mutex);
        if (ConsumeQueuedCancellation(job_id)) {
            cancelled_before_execute = true;
        } else {
            RegisterPendingClientJob(job_id);
        }
    }

    if (cancelled_before_execute) {
        std::snprintf(err_buf, kErrBufSize, "Cook cancelled");
        rc = PCG_ERR_EXECUTION;
    } else {
        std::lock_guard<std::mutex> cook_lock(g_cook_mutex);

        {
            std::lock_guard<std::mutex> job_lock(g_job_state_mutex);
            UnregisterPendingClientJob(job_id);
            if (ConsumeQueuedCancellation(job_id)) {
                cancelled_before_execute = true;
            } else {
                SetActiveClientJobId(job_id);
            }
        }

        if (cancelled_before_execute) {
            std::snprintf(err_buf, kErrBufSize, "Cook cancelled");
            rc = PCG_ERR_EXECUTION;
        } else {
            pcg_clear_cancel();

            for (int attempt = 0; attempt <= kMaxGrowRetries; ++attempt) {
                std::memset(err_buf, 0, sizeof(err_buf));
                json_buf[0] = '\0';
                perf_buf[0] = '\0';
                if (!points_buf.empty()) {
                    points_buf[0] = '\0';
                }
                geometry_bytes = 0;
                heightfield_bytes = 0;

                rc = pcg_execute_graph_v10(
                    graph_json.c_str(),
                    seed,
                    tex_slots.empty() ? nullptr : tex_slots.data(),
                    static_cast<int>(tex_slots.size()),
                    mesh_slots.empty() ? nullptr : mesh_slots.data(),
                    static_cast<int>(mesh_slots.size()),
                    spline_slots.empty() ? nullptr : spline_slots.data(),
                    static_cast<int>(spline_slots.size()),
                    hf_slots.empty() ? nullptr : hf_slots.data(),
                    static_cast<int>(hf_slots.size()),
                    &kind,
                    json_buf.data(),
                    static_cast<int>(json_buf.size()),
                    mesh_buf.data(),
                    static_cast<int>(mesh_buf.size()),
                    points_buf.data(),
                    static_cast<int>(points_buf.size()),
                    &point_count,
                    &point_attr_flags,
                    &vertex_count,
                    &index_count,
                    &stats,
                    perf_buf.data(),
                    static_cast<int>(perf_buf.size()),
                    geometry_buf.data(),
                    static_cast<int>(geometry_buf.size()),
                    &geometry_bytes,
                    heightfield_buf.data(),
                    static_cast<int>(heightfield_buf.size()),
                    &heightfield_bytes,
                    err_buf,
                    kErrBufSize);

                if (rc == PCG_OK) {
                    if (heightfield_bytes > static_cast<int>(heightfield_buf.size()) &&
                        heightfield_bytes <= kMaxBinaryBuf) {
                        heightfield_buf.assign(static_cast<size_t>(heightfield_bytes), 0);
                        continue;
                    }
                    break;
                }

                std::string err(err_buf);
                std::string need_kind;
                int need_bytes = 0;
                if (!TryParseNeedBytes(err, need_kind, need_bytes) || need_bytes > kMaxBinaryBuf) {
                    break;
                }
                if (need_kind == "points" && static_cast<int>(points_buf.size()) < need_bytes) {
                    points_buf.assign(static_cast<size_t>(need_bytes), 0);
                    continue;
                }
                if (need_kind == "json" && static_cast<int>(json_buf.size()) < need_bytes) {
                    json_buf.assign(static_cast<size_t>(need_bytes), 0);
                    continue;
                }
                if (need_kind == "mesh" && static_cast<int>(mesh_buf.size()) < need_bytes) {
                    mesh_buf.assign(static_cast<size_t>(need_bytes), 0);
                    continue;
                }
                break;
            }
        }

        {
            std::lock_guard<std::mutex> job_lock(g_job_state_mutex);
            ClearActiveClientJobId(job_id);
        }
    }

    // Legacy mis-route: heightfield JSON summaries were left in points_buf while
    // json_buf stayed empty. Recover before sizing outbound blobs.
    if (rc == PCG_OK && json_buf[0] == '\0' && !points_buf.empty() && points_buf[0] == '{') {
        const char* misplaced = reinterpret_cast<const char*>(points_buf.data());
        const size_t text_len = strnlen(misplaced, points_buf.size());
        if (text_len > 0 && text_len + 1 < json_buf.size()) {
            std::memcpy(json_buf.data(), misplaced, text_len + 1);
            std::memset(points_buf.data(), 0, text_len + 1);
        }
    }

    const size_t json_len =
        (json_buf.empty() || json_buf[0] == '\0') ? 0 : strnlen(json_buf.data(), json_buf.size());
    const size_t perf_len =
        (perf_buf.empty() || perf_buf[0] == '\0') ? 0 : strnlen(perf_buf.data(), perf_buf.size());
    if (rc == PCG_OK && mesh_buf.size() >= 16 && ReadU32(mesh_buf.data()) == PCG_MESH_BINARY_MAGIC) {
        vertex_count = ReadI32(mesh_buf.data() + 8);
        index_count = ReadI32(mesh_buf.data() + 12);
    }
    const size_t mesh_len =
        rc == PCG_OK ? ComputeMeshBinaryPayloadSize(mesh_buf, vertex_count, index_count) : 0;
    const size_t points_len =
        rc == PCG_OK ? ComputePointsPayloadSize(points_buf, point_count, point_attr_flags) : 0;
    const size_t geo_len =
        (rc == PCG_OK && geometry_bytes > 0) ? static_cast<size_t>(geometry_bytes) : 0;
    const size_t hf_len =
        (rc == PCG_OK && heightfield_bytes > 0) ? static_cast<size_t>(heightfield_bytes) : 0;

    auto body = EncodeResult(
        rc,
        kind,
        stats,
        point_count,
        point_attr_flags,
        vertex_count,
        index_count,
        rc == PCG_OK ? std::string() : std::string(err_buf),
        json_buf.data(),
        json_len,
        mesh_buf.data(),
        mesh_len,
        points_buf.data(),
        points_len,
        geometry_buf.data(),
        geo_len,
        heightfield_buf.data(),
        hf_len,
        perf_buf.data(),
        perf_len);

    res.status = 200;
    res.set_content(
        std::string(reinterpret_cast<const char*>(body.data()), body.size()),
        "application/x-pcg-cook-result-v1");
}

void HandleValidate(const httplib::Request& req, httplib::Response& res) {
    const std::string graph = req.body.empty() ? GetPartContent(req, "graph") : req.body;
    if (graph.empty()) {
        res.status = 400;
        res.set_content(R"({"ok":false,"error":"missing graph JSON body"})", "application/json");
        return;
    }
    char err_buf[kErrBufSize];
    std::memset(err_buf, 0, sizeof(err_buf));
    const PcgResultCode rc = pcg_validate_graph(graph.c_str(), err_buf, kErrBufSize);
    nlohmann::json body = {
        {"ok", rc == PCG_OK},
        {"code", static_cast<int>(rc)},
        {"error", std::string(err_buf)},
    };
    res.status = 200;
    res.set_content(body.dump(), "application/json");
}

void HandleCacheClear(const httplib::Request& /*req*/, httplib::Response& res) {
    std::lock_guard<std::mutex> lock(g_cook_mutex);
    pcg_cook_cache_clear();
    res.status = 200;
    res.set_content(R"({"ok":true})", "application/json");
}

void HandleExportFbx(const httplib::Request& req, httplib::Response& res) {
    if (!req.is_multipart_form_data()) {
        res.status = 400;
        res.set_content(R"({"error":"expected multipart/form-data"})", "application/json");
        return;
    }

    const auto* geo_part = FindPart(req.files, "geometry");
    if (geo_part == nullptr || geo_part->content.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"missing geometry part"})", "application/json");
        return;
    }

    float scale = 1.0f;
    bool generate_normals = true;
    const std::string meta = GetPartContent(req, "meta");
    if (!meta.empty()) {
        try {
            const auto j = nlohmann::json::parse(meta);
            scale = j.value("scale", 1.0f);
            generate_normals = j.value("generate_normals", true);
        } catch (const std::exception& ex) {
            res.status = 400;
            res.set_content(nlohmann::json{{"error", std::string("Invalid meta: ") + ex.what()}}.dump(),
                            "application/json");
            return;
        }
    }

    namespace fs = std::filesystem;
    const auto tmp_path = fs::temp_directory_path() /
                          ("pcg_fbx_" + std::to_string(
                              static_cast<long long>(
                                  std::chrono::steady_clock::now().time_since_epoch().count())) +
                           ".fbx");

    PcgFbxExportOptions options{};
    options.struct_size = static_cast<uint32_t>(sizeof(PcgFbxExportOptions));
    options.scale = scale;
    options.generate_normals = generate_normals ? 1 : 0;

    char err_buf[2048];
    std::memset(err_buf, 0, sizeof(err_buf));
    const int fbx_rc = pcg_fbx_export_v1(
        geo_part->content.data(),
        static_cast<int>(geo_part->content.size()),
        tmp_path.string().c_str(),
        &options,
        err_buf,
        static_cast<int>(sizeof(err_buf)));

    if (fbx_rc != PCG_FBX_OK) {
        std::error_code ec;
        fs::remove(tmp_path, ec);
        res.status = 500;
        res.set_content(
            nlohmann::json{
                {"error", err_buf[0] ? std::string(err_buf) : ("fbx export failed code " + std::to_string(fbx_rc))},
                {"code", fbx_rc},
                {"fbx_version", pcg_fbx_get_version()},
            }
                .dump(),
            "application/json");
        return;
    }

    std::ifstream in(tmp_path, std::ios::binary);
    std::string fbx_bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::error_code ec;
    fs::remove(tmp_path, ec);

    if (fbx_bytes.empty()) {
        res.status = 500;
        res.set_content(R"({"error":"FBX export produced an empty file"})", "application/json");
        return;
    }

    res.status = 200;
    res.set_header("X-Pcg-Fbx-Version", pcg_fbx_get_version());
    res.set_content(fbx_bytes, "application/octet-stream");
}

}  // namespace pcg_server
