#include "pcg_api.h"

#include "data/pcg_mesh_binary.hpp"
#include "data/pcg_texture_data.hpp"
#include "graph_executor.hpp"
#include "graph_parser.hpp"
#include "internal/error_util.hpp"
#include "texture_runtime.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstring>

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
        pcg::internal::execute_graph(graph, seed, result, err_buf, err_buf_size, nullptr);
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
        texture_count > 0 ? &runtime : nullptr);
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
