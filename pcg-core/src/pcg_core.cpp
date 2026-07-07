#include "pcg_api.h"

#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_texture_data.hpp"
#include "graph_cook_cache.hpp"
#include "graph_executor.hpp"
#include "graph_parser.hpp"
#include "internal/error_util.hpp"
#include "mesh_runtime.hpp"
#include "texture_runtime.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>
#include <atomic>

namespace {

pcg::internal::GraphCookCache g_cook_cache;
std::atomic<bool> g_cancel_requested{false};

bool is_cancel_requested_now()
{
    return g_cancel_requested.load(std::memory_order_relaxed);
}

PcgResultCode write_execution_result(const pcg::internal::GraphExecutionResult& result,
                                     int* out_kind,
                                     char* out_json,
                                     int out_json_size,
                                     void* out_mesh_buf,
                                     int out_mesh_buf_size,
                                     int* out_vertex_count,
                                     int* out_index_count,
                                     char* err_buf,
                                     int err_buf_size)
{
    if (result.kind == pcg::internal::GraphResultKind::Mesh) {
        if (out_kind)
            *out_kind = PCG_RESULT_KIND_MESH;

        const int vertex_count = static_cast<int>(result.mesh.vertices().size());
        const int index_count = static_cast<int>(result.mesh.triangles().size());
        if (out_vertex_count)
            *out_vertex_count = vertex_count;
        if (out_index_count)
            *out_index_count = index_count;

        if (out_json && out_json_size > 0)
            out_json[0] = '\0';

        if (!out_mesh_buf || out_mesh_buf_size <= 0) {
            pcg::internal::write_error(err_buf, err_buf_size,
                                       "Mesh result requires out_mesh_buf");
            return PCG_ERR_EXECUTION;
        }

        if (!pcg::internal::data::write_mesh_binary(result.mesh, out_mesh_buf, out_mesh_buf_size)) {
            const int required = pcg::internal::data::mesh_binary_size(result.mesh);
            char message[256];
            std::snprintf(message, sizeof(message),
                          "Mesh binary buffer too small (need %d bytes, got %d)",
                          required, out_mesh_buf_size);
            pcg::internal::write_error(err_buf, err_buf_size, message);
            return PCG_ERR_EXECUTION;
        }

        return PCG_OK;
    }

    if (out_kind)
        *out_kind = PCG_RESULT_KIND_JSON;
    if (out_vertex_count)
        *out_vertex_count = 0;
    if (out_index_count)
        *out_index_count = 0;

    if (!out_json || out_json_size <= 0) {
        pcg::internal::write_error(err_buf, err_buf_size, "JSON result requires out_json buffer");
        return PCG_ERR_EXECUTION;
    }

    const std::string serialized = result.json.dump();
    if (static_cast<int>(serialized.size()) >= out_json_size) {
        pcg::internal::write_error(err_buf, err_buf_size, "JSON result exceeds output buffer");
        return PCG_ERR_EXECUTION;
    }

    std::strncpy(out_json, serialized.c_str(), static_cast<size_t>(out_json_size - 1));
    out_json[out_json_size - 1] = '\0';
    return PCG_OK;
}

void build_texture_runtime(const PcgTextureSlot* textures,
                           int texture_count,
                           pcg::internal::TextureRuntime& runtime)
{
    if (!textures || texture_count <= 0)
        return;

    for (int i = 0; i < texture_count; ++i) {
        const PcgTextureSlot& slot = textures[i];
        if (!slot.slot_id || slot.width <= 0 || slot.height <= 0 || !slot.rgba)
            continue;
        const int rgba_count = slot.width * slot.height * 4;
        pcg::internal::data::PcgTextureData tex = pcg::internal::data::PcgTextureData::from_rgba(
            slot.width, slot.height, slot.rgba, rgba_count);
        runtime.add_slot(slot.slot_id, std::move(tex));
    }
}

void build_mesh_runtime(const PcgMeshSlot* meshes, int mesh_count, pcg::internal::MeshRuntime& runtime)
{
    if (!meshes || mesh_count <= 0)
        return;

    for (int i = 0; i < mesh_count; ++i) {
        const PcgMeshSlot& slot = meshes[i];
        if (!slot.slot_id || slot.vertex_count <= 0 || slot.index_count < 3 || !slot.positions ||
            !slot.indices)
            continue;

        pcg::internal::data::PcgMeshData mesh;
        mesh.vertices_mut().reserve(static_cast<size_t>(slot.vertex_count));
        for (int v = 0; v < slot.vertex_count; ++v) {
            const int base = v * 3;
            mesh.vertices_mut().push_back(pcg::internal::data::PcgVertex{
                slot.positions[base],
                slot.positions[base + 1],
                slot.positions[base + 2],
            });
        }
        for (int t = 0; t < slot.index_count; ++t)
            mesh.triangles_mut().push_back(static_cast<int>(slot.indices[t]));

        runtime.add_slot(slot.slot_id, std::move(mesh));
    }
}

PcgResultCode execute_graph_cached(const char* json,
                                   int seed,
                                   const PcgTextureSlot* textures,
                                   int texture_count,
                                   const PcgMeshSlot* meshes,
                                   int mesh_count,
                                   int* out_kind,
                                   char* out_json,
                                   int out_json_size,
                                   void* out_mesh_buf,
                                   int out_mesh_buf_size,
                                   int* out_vertex_count,
                                   int* out_index_count,
                                   PcgCookStats* out_stats,
                                   char* err_buf,
                                   int err_buf_size,
                                   pcg::internal::GraphCookCache* cache)
{
    g_cancel_requested.store(false, std::memory_order_relaxed);
    pcg::internal::write_error(err_buf, err_buf_size, "");
    if (out_kind)
        *out_kind = PCG_RESULT_KIND_NONE;

    const PcgResultCode validate_code = pcg_validate_graph(json, err_buf, err_buf_size);
    if (validate_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return validate_code;
    }

    pcg::internal::Graph graph;
    const PcgResultCode parse_code =
        pcg::internal::parse_graph(json, graph, err_buf, err_buf_size);
    if (parse_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return parse_code;
    }

    pcg::internal::TextureRuntime texture_runtime;
    build_texture_runtime(textures, texture_count, texture_runtime);

    pcg::internal::MeshRuntime mesh_runtime;
    build_mesh_runtime(meshes, mesh_count, mesh_runtime);

    pcg::internal::GraphExecutionResult result;
    const PcgResultCode exec_code = pcg::internal::execute_graph(
        graph, seed, result, err_buf, err_buf_size,
        texture_count > 0 ? &texture_runtime : nullptr,
        mesh_count > 0 ? &mesh_runtime : nullptr, cache, is_cancel_requested_now);
    if (exec_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return exec_code;
    }

    if (out_stats && cache) {
        out_stats->nodes_executed = cache->nodes_executed();
        out_stats->nodes_skipped = cache->nodes_skipped();
    }

    return write_execution_result(result, out_kind, out_json, out_json_size, out_mesh_buf,
                                out_mesh_buf_size, out_vertex_count, out_index_count, err_buf,
                                err_buf_size);
}

} // namespace

const char* pcg_get_version(void)
{
    static char buf[64];
    std::snprintf(buf, sizeof(buf),
                  "pcg-core %d.%d.%d (bmesh-tier1)",
                  PCG_API_VERSION_MAJOR,
                  PCG_API_VERSION_MINOR,
                  PCG_API_VERSION_PATCH);
    return buf;
}

PcgResultCode pcg_validate_graph(const char* json,
                                 char* err_buf,
                                 int err_buf_size)
{
    pcg::internal::write_error(err_buf, err_buf_size, "");

    pcg::internal::Graph graph;
    const PcgResultCode parse_code =
        pcg::internal::parse_graph(json, graph, err_buf, err_buf_size);
    if (parse_code != PCG_OK)
        return parse_code;

    return pcg::internal::validate_graph_structure(graph, err_buf, err_buf_size);
}

PcgResultCode pcg_execute_graph_v2(const char* json,
                                   int seed,
                                   int* out_kind,
                                   char* out_json,
                                   int out_json_size,
                                   void* out_mesh_buf,
                                   int out_mesh_buf_size,
                                   int* out_vertex_count,
                                   int* out_index_count,
                                   char* err_buf,
                                   int err_buf_size)
{
    g_cancel_requested.store(false, std::memory_order_relaxed);
    pcg::internal::write_error(err_buf, err_buf_size, "");
    if (out_kind)
        *out_kind = PCG_RESULT_KIND_NONE;

    const PcgResultCode validate_code = pcg_validate_graph(json, err_buf, err_buf_size);
    if (validate_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return validate_code;
    }

    pcg::internal::Graph graph;
    const PcgResultCode parse_code =
        pcg::internal::parse_graph(json, graph, err_buf, err_buf_size);
    if (parse_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return parse_code;
    }

    pcg::internal::GraphExecutionResult result;
    const PcgResultCode exec_code =
        pcg::internal::execute_graph(
            graph, seed, result, err_buf, err_buf_size, nullptr, nullptr, nullptr, is_cancel_requested_now);
    if (exec_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return exec_code;
    }

    if (result.kind == pcg::internal::GraphResultKind::Mesh) {
        if (out_kind)
            *out_kind = PCG_RESULT_KIND_MESH;

        const int vertex_count = static_cast<int>(result.mesh.vertices().size());
        const int index_count = static_cast<int>(result.mesh.triangles().size());
        if (out_vertex_count)
            *out_vertex_count = vertex_count;
        if (out_index_count)
            *out_index_count = index_count;

        if (out_json && out_json_size > 0)
            out_json[0] = '\0';

        if (!out_mesh_buf || out_mesh_buf_size <= 0) {
            pcg::internal::write_error(err_buf, err_buf_size,
                                       "Mesh result requires out_mesh_buf");
            return PCG_ERR_EXECUTION;
        }

        if (!pcg::internal::data::write_mesh_binary(result.mesh, out_mesh_buf, out_mesh_buf_size)) {
            const int required = pcg::internal::data::mesh_binary_size(result.mesh);
            char message[256];
            std::snprintf(message, sizeof(message),
                          "Mesh binary buffer too small (need %d bytes, got %d)",
                          required, out_mesh_buf_size);
            pcg::internal::write_error(err_buf, err_buf_size, message);
            return PCG_ERR_EXECUTION;
        }

        return PCG_OK;
    }

    if (out_kind)
        *out_kind = PCG_RESULT_KIND_JSON;
    if (out_vertex_count)
        *out_vertex_count = 0;
    if (out_index_count)
        *out_index_count = 0;

    if (!out_json || out_json_size <= 0) {
        pcg::internal::write_error(err_buf, err_buf_size, "JSON result requires out_json buffer");
        return PCG_ERR_EXECUTION;
    }

    const std::string serialized = result.json.dump();
    if (static_cast<int>(serialized.size()) >= out_json_size) {
        pcg::internal::write_error(err_buf, err_buf_size, "JSON result exceeds output buffer");
        return PCG_ERR_EXECUTION;
    }

    std::strncpy(out_json, serialized.c_str(), static_cast<size_t>(out_json_size - 1));
    out_json[out_json_size - 1] = '\0';
    return PCG_OK;
}

PcgResultCode pcg_execute_graph_v3(const char* json,
                                   int seed,
                                   const PcgTextureSlot* textures,
                                   int texture_count,
                                   int* out_kind,
                                   char* out_json,
                                   int out_json_size,
                                   void* out_mesh_buf,
                                   int out_mesh_buf_size,
                                   int* out_vertex_count,
                                   int* out_index_count,
                                   char* err_buf,
                                   int err_buf_size)
{
    g_cancel_requested.store(false, std::memory_order_relaxed);
    pcg::internal::write_error(err_buf, err_buf_size, "");
    if (out_kind)
        *out_kind = PCG_RESULT_KIND_NONE;

    const PcgResultCode validate_code = pcg_validate_graph(json, err_buf, err_buf_size);
    if (validate_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return validate_code;
    }

    pcg::internal::Graph graph;
    const PcgResultCode parse_code =
        pcg::internal::parse_graph(json, graph, err_buf, err_buf_size);
    if (parse_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return parse_code;
    }

    pcg::internal::TextureRuntime runtime;
    if (textures && texture_count > 0) {
        for (int i = 0; i < texture_count; ++i) {
            const PcgTextureSlot& slot = textures[i];
            if (!slot.slot_id || slot.width <= 0 || slot.height <= 0 || !slot.rgba)
                continue;
            const int rgba_count = slot.width * slot.height * 4;
            pcg::internal::data::PcgTextureData tex = pcg::internal::data::PcgTextureData::from_rgba(
                slot.width, slot.height, slot.rgba, rgba_count);
            runtime.add_slot(slot.slot_id, std::move(tex));
        }
    }

    pcg::internal::GraphExecutionResult result;
    const PcgResultCode exec_code = pcg::internal::execute_graph(
        graph, seed, result, err_buf, err_buf_size,
        texture_count > 0 ? &runtime : nullptr, nullptr, nullptr, is_cancel_requested_now);
    if (exec_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return exec_code;
    }

    if (result.kind == pcg::internal::GraphResultKind::Mesh) {
        if (out_kind)
            *out_kind = PCG_RESULT_KIND_MESH;

        const int vertex_count = static_cast<int>(result.mesh.vertices().size());
        const int index_count = static_cast<int>(result.mesh.triangles().size());
        if (out_vertex_count)
            *out_vertex_count = vertex_count;
        if (out_index_count)
            *out_index_count = index_count;

        if (out_json && out_json_size > 0)
            out_json[0] = '\0';

        if (!out_mesh_buf || out_mesh_buf_size <= 0) {
            pcg::internal::write_error(err_buf, err_buf_size,
                                       "Mesh result requires out_mesh_buf");
            return PCG_ERR_EXECUTION;
        }

        if (!pcg::internal::data::write_mesh_binary(result.mesh, out_mesh_buf, out_mesh_buf_size)) {
            const int required = pcg::internal::data::mesh_binary_size(result.mesh);
            char message[256];
            std::snprintf(message, sizeof(message),
                          "Mesh binary buffer too small (need %d bytes, got %d)",
                          required, out_mesh_buf_size);
            pcg::internal::write_error(err_buf, err_buf_size, message);
            return PCG_ERR_EXECUTION;
        }

        return PCG_OK;
    }

    if (out_kind)
        *out_kind = PCG_RESULT_KIND_JSON;
    if (out_vertex_count)
        *out_vertex_count = 0;
    if (out_index_count)
        *out_index_count = 0;

    if (!out_json || out_json_size <= 0) {
        pcg::internal::write_error(err_buf, err_buf_size, "JSON result requires out_json buffer");
        return PCG_ERR_EXECUTION;
    }

    const std::string serialized = result.json.dump();
    if (static_cast<int>(serialized.size()) >= out_json_size) {
        pcg::internal::write_error(err_buf, err_buf_size, "JSON result exceeds output buffer");
        return PCG_ERR_EXECUTION;
    }

    std::strncpy(out_json, serialized.c_str(), static_cast<size_t>(out_json_size - 1));
    out_json[out_json_size - 1] = '\0';
    return PCG_OK;
}

PcgResultCode pcg_execute_graph_v4(const char* json,
                                   int seed,
                                   const PcgTextureSlot* textures,
                                   int texture_count,
                                   const PcgMeshSlot* meshes,
                                   int mesh_count,
                                   int* out_kind,
                                   char* out_json,
                                   int out_json_size,
                                   void* out_mesh_buf,
                                   int out_mesh_buf_size,
                                   int* out_vertex_count,
                                   int* out_index_count,
                                   char* err_buf,
                                   int err_buf_size)
{
    g_cancel_requested.store(false, std::memory_order_relaxed);
    if (!meshes || mesh_count <= 0)
        return pcg_execute_graph_v3(json, seed, textures, texture_count, out_kind, out_json,
                                    out_json_size, out_mesh_buf, out_mesh_buf_size,
                                    out_vertex_count, out_index_count, err_buf, err_buf_size);

    pcg::internal::write_error(err_buf, err_buf_size, "");
    if (out_kind)
        *out_kind = PCG_RESULT_KIND_NONE;

    const PcgResultCode validate_code = pcg_validate_graph(json, err_buf, err_buf_size);
    if (validate_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return validate_code;
    }

    pcg::internal::Graph graph;
    const PcgResultCode parse_code =
        pcg::internal::parse_graph(json, graph, err_buf, err_buf_size);
    if (parse_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return parse_code;
    }

    pcg::internal::TextureRuntime texture_runtime;
    if (textures && texture_count > 0) {
        for (int i = 0; i < texture_count; ++i) {
            const PcgTextureSlot& slot = textures[i];
            if (!slot.slot_id || slot.width <= 0 || slot.height <= 0 || !slot.rgba)
                continue;
            const int rgba_count = slot.width * slot.height * 4;
            pcg::internal::data::PcgTextureData tex = pcg::internal::data::PcgTextureData::from_rgba(
                slot.width, slot.height, slot.rgba, rgba_count);
            texture_runtime.add_slot(slot.slot_id, std::move(tex));
        }
    }

    pcg::internal::MeshRuntime mesh_runtime;
    for (int i = 0; i < mesh_count; ++i) {
        const PcgMeshSlot& slot = meshes[i];
        if (!slot.slot_id || slot.vertex_count <= 0 || slot.index_count < 3 || !slot.positions ||
            !slot.indices)
            continue;

        pcg::internal::data::PcgMeshData mesh;
        mesh.vertices_mut().reserve(static_cast<size_t>(slot.vertex_count));
        for (int v = 0; v < slot.vertex_count; ++v) {
            const int base = v * 3;
            mesh.vertices_mut().push_back(pcg::internal::data::PcgVertex{
                slot.positions[base],
                slot.positions[base + 1],
                slot.positions[base + 2],
            });
        }
        for (int t = 0; t < slot.index_count; ++t)
            mesh.triangles_mut().push_back(static_cast<int>(slot.indices[t]));

        mesh_runtime.add_slot(slot.slot_id, std::move(mesh));
    }

    pcg::internal::GraphExecutionResult result;
    const PcgResultCode exec_code = pcg::internal::execute_graph(
        graph, seed, result, err_buf, err_buf_size,
        texture_count > 0 ? &texture_runtime : nullptr, &mesh_runtime, nullptr, is_cancel_requested_now);
    if (exec_code != PCG_OK) {
        if (out_json && out_json_size > 0)
            out_json[0] = '\0';
        return exec_code;
    }

    if (result.kind == pcg::internal::GraphResultKind::Mesh) {
        if (out_kind)
            *out_kind = PCG_RESULT_KIND_MESH;

        const int vertex_count = static_cast<int>(result.mesh.vertices().size());
        const int index_count = static_cast<int>(result.mesh.triangles().size());
        if (out_vertex_count)
            *out_vertex_count = vertex_count;
        if (out_index_count)
            *out_index_count = index_count;

        if (out_json && out_json_size > 0)
            out_json[0] = '\0';

        if (!out_mesh_buf || out_mesh_buf_size <= 0) {
            pcg::internal::write_error(err_buf, err_buf_size,
                                       "Mesh result requires out_mesh_buf");
            return PCG_ERR_EXECUTION;
        }

        if (!pcg::internal::data::write_mesh_binary(result.mesh, out_mesh_buf, out_mesh_buf_size)) {
            const int required = pcg::internal::data::mesh_binary_size(result.mesh);
            char message[256];
            std::snprintf(message, sizeof(message),
                          "Mesh binary buffer too small (need %d bytes, got %d)",
                          required, out_mesh_buf_size);
            pcg::internal::write_error(err_buf, err_buf_size, message);
            return PCG_ERR_EXECUTION;
        }

        return PCG_OK;
    }

    if (out_kind)
        *out_kind = PCG_RESULT_KIND_JSON;
    if (out_vertex_count)
        *out_vertex_count = 0;
    if (out_index_count)
        *out_index_count = 0;

    if (!out_json || out_json_size <= 0) {
        pcg::internal::write_error(err_buf, err_buf_size, "JSON result requires out_json buffer");
        return PCG_ERR_EXECUTION;
    }

    const std::string serialized = result.json.dump();
    if (static_cast<int>(serialized.size()) >= out_json_size) {
        pcg::internal::write_error(err_buf, err_buf_size, "JSON result exceeds output buffer");
        return PCG_ERR_EXECUTION;
    }

    std::strncpy(out_json, serialized.c_str(), static_cast<size_t>(out_json_size - 1));
    out_json[out_json_size - 1] = '\0';
    return PCG_OK;
}

PcgResultCode pcg_execute_graph(const char* json,
                                int seed,
                                char* out_json,
                                int out_json_size)
{
    int kind = PCG_RESULT_KIND_NONE;
    char local_err[1024] = {};
    const PcgResultCode rc = pcg_execute_graph_v2(
        json, seed, &kind, out_json, out_json_size, nullptr, 0, nullptr, nullptr, local_err,
        sizeof(local_err));
    if (rc != PCG_OK)
        return rc;

    if (kind == PCG_RESULT_KIND_MESH)
        return PCG_ERR_EXECUTION;

    return PCG_OK;
}

void pcg_cook_cache_clear(void)
{
    g_cook_cache.clear();
}

void pcg_request_cancel(void)
{
    g_cancel_requested.store(true, std::memory_order_relaxed);
}

void pcg_clear_cancel(void)
{
    g_cancel_requested.store(false, std::memory_order_relaxed);
}

PcgResultCode pcg_execute_graph_v5(const char* json,
                                   int seed,
                                   const PcgTextureSlot* textures,
                                   int texture_count,
                                   const PcgMeshSlot* meshes,
                                   int mesh_count,
                                   int* out_kind,
                                   char* out_json,
                                   int out_json_size,
                                   void* out_mesh_buf,
                                   int out_mesh_buf_size,
                                   int* out_vertex_count,
                                   int* out_index_count,
                                   PcgCookStats* out_stats,
                                   char* err_buf,
                                   int err_buf_size)
{
    return execute_graph_cached(json, seed, textures, texture_count, meshes, mesh_count, out_kind,
                                out_json, out_json_size, out_mesh_buf, out_mesh_buf_size,
                                out_vertex_count, out_index_count, out_stats, err_buf, err_buf_size,
                                &g_cook_cache);
}

PcgResultCode pcg_mesh_binary_size_for_counts(int vertex_count,
                                              int index_count,
                                              int* out_size)
{
    if (!out_size || vertex_count < 0 || index_count < 0)
        return PCG_ERR_EXECUTION;

    *out_size = pcg::internal::data::kPcgMeshBinaryHeaderSize +
                vertex_count * 3 * static_cast<int>(sizeof(float)) +
                index_count * static_cast<int>(sizeof(uint32_t));
    return PCG_OK;
}
