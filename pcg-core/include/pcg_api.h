#pragma once

#include <stdint.h>

/* ── DLL export/import macros ────────────────────────── */

#if defined(_WIN32) && !defined(PCG_STATIC)
  #ifdef PCG_EXPORTS
    /* Building the DLL — export symbols */
    #define PCG_API __declspec(dllexport)
  #else
    /* Consuming the DLL — import symbols */
    #define PCG_API __declspec(dllimport)
  #endif
#elif defined(__APPLE__) && !defined(PCG_STATIC)
  #ifdef PCG_EXPORTS
    #define PCG_API __attribute__((visibility("default")))
  #else
    #define PCG_API
  #endif
#else
  /* Static build or other platforms — no decoration */
  #define PCG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ── Version ────────────────────────────────────────── */

#define PCG_API_VERSION_MAJOR 0
#define PCG_API_VERSION_MINOR 1
#define PCG_API_VERSION_PATCH 1

/**
 * Returns a static version string, e.g. "pcg-core 0.1.0".
 * The pointer is valid for the lifetime of the library.
 */
PCG_API const char* pcg_get_version(void);

/* ── Result codes ──────────────────────────────────── */

typedef enum {
    PCG_OK                 = 0,
    PCG_ERR_INVALID_JSON   = 1,
    PCG_ERR_CYCLE_DETECTED = 2,
    PCG_ERR_UNKNOWN_NODE   = 3,
    PCG_ERR_EXECUTION      = 4
} PcgResultCode;

typedef enum {
    PCG_RESULT_KIND_NONE = 0,
    PCG_RESULT_KIND_JSON = 1,
    PCG_RESULT_KIND_MESH = 2,
    PCG_RESULT_KIND_POINTS = 3
} PcgResultKind;

/* Mesh binary header: magic 'PCGM', version, vertex_count, index_count, flags (20 bytes v2).
 * v1 (16 bytes, no flags) is supported for backward-compatible reading only.
 * v2 header is followed by float32 xyz positions, uint32 triangle indices,
 * then optional blocks in order: normals (flags & 0x1, float3*N),
 * colors (flags & 0x2, float4*N), uvs (flags & 0x4, float2*N).
 * Each optional block is independent; any combination may be present. */
#define PCG_MESH_BINARY_MAGIC 0x4D474350u
#define PCG_MESH_BINARY_VERSION 2u
#define PCG_MESH_BINARY_HEADER_SIZE 20

/* Point binary header (v6 data-plane target):
 * [magic|version|point_count|flags] (16 bytes)
 * payload always starts with float32 xyz positions [point_count * 3].
 * optional payload blocks are appended when flags are set.
 */
#define PCG_POINT_BINARY_MAGIC 0x50544750u /* 'PGTP' little-endian */
#define PCG_POINT_BINARY_VERSION 1u
#define PCG_POINT_BINARY_HEADER_SIZE 16

typedef enum {
    PCG_POINT_ATTR_NONE = 0,
    PCG_POINT_ATTR_NORMAL = 1 << 0,  /* float32 nx,ny,nz */
    PCG_POINT_ATTR_UV = 1 << 1,      /* float32 u,v */
    PCG_POINT_ATTR_TRI_INDEX = 1 << 2, /* uint32 triIndex */
    PCG_POINT_ATTR_SCALE = 1 << 3,   /* float32 scale */
    PCG_POINT_ATTR_ROTATION = 1 << 4 /* float32 quaternion xyzw */
} PcgPointAttrFlags;

/* ── Graph API ──────────────────────────────────────── */

/**
 * Validates a Graph JSON string for structural correctness.
 *
 * @param json          Null-terminated Graph JSON v1 string.
 * @param err_buf       Optional buffer for human-readable error message.
 * @param err_buf_size  Size of err_buf in bytes.
 * @return PCG_OK on success, error code on failure.
 */
PCG_API PcgResultCode pcg_validate_graph(const char* json,
                                         char* err_buf,
                                         int err_buf_size);

/**
 * Executes a Graph JSON and returns the result as JSON.
 * Mesh sink graphs return PCG_ERR_EXECUTION — use pcg_execute_graph_v2 instead.
 *
 * @param json          Null-terminated Graph JSON v1 string.
 * @param seed          Random seed for deterministic generation.
 * @param out_json      Output buffer for result JSON.
 * @param out_json_size Size of out_json in bytes.
 * @return PCG_OK on success, error code on failure.
 */
PCG_API PcgResultCode pcg_execute_graph(const char* json,
                                         int seed,
                                         char* out_json,
                                         int out_json_size);

/**
 * Executes a Graph JSON and returns JSON (points/splines) or binary mesh.
 *
 * @param out_kind            PCG_RESULT_KIND_JSON or PCG_RESULT_KIND_MESH.
 * @param out_json            Buffer for JSON results (ignored for mesh).
 * @param out_mesh_buf        Buffer for binary mesh (required for mesh results).
 * @param out_vertex_count    Vertex count when out_kind is mesh.
 * @param out_index_count     Triangle index count when out_kind is mesh.
 */
PCG_API PcgResultCode pcg_execute_graph_v2(const char* json,
                                           int seed,
                                           int* out_kind,
                                           char* out_json,
                                           int out_json_size,
                                           void* out_mesh_buf,
                                           int out_mesh_buf_size,
                                           int* out_vertex_count,
                                           int* out_index_count,
                                           char* err_buf,
                                           int err_buf_size);

/** Computes required bytes for a mesh binary payload with the given counts. */
PCG_API PcgResultCode pcg_mesh_binary_size_for_counts(int vertex_count,
                                                      int index_count,
                                                      int* out_size);

/** Computes required bytes for point binary payload with optional attributes. */
PCG_API PcgResultCode pcg_point_binary_size_for_counts(int point_count,
                                                       uint32_t attr_flags,
                                                       int* out_size);

/**
 * Runtime texture slot uploaded by the host (Unity) before graph execution.
 * slot_id must match the ImageTexture node id in the graph JSON.
 * rgba is width*height*4 floats in [0,1] (linear RGBA).
 */
typedef struct {
    const char* slot_id;
    int width;
    int height;
    const float* rgba;
} PcgTextureSlot;

/**
 * Executes a Graph JSON with optional runtime texture pixel uploads.
 * Falls back to pcg_execute_graph_v2 when textures is null or texture_count is 0.
 */
PCG_API PcgResultCode pcg_execute_graph_v3(const char* json,
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
                                           int err_buf_size);

typedef struct {
    const char* slot_id;
    int vertex_count;
    int index_count;
    const float* positions;
    const uint32_t* indices;
} PcgMeshSlot;

/**
 * Executes a Graph JSON with optional runtime texture and mesh slot uploads.
 * Falls back to v3 when mesh_count is 0; v3 falls back to v2 when texture_count is 0.
 */
PCG_API PcgResultCode pcg_execute_graph_v4(const char* json,
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
                                           int err_buf_size);

typedef struct {
    int nodes_executed;
    int nodes_skipped;
    double graph_execute_ms;
    double binary_write_ms;
} PcgCookStats;

/**
 * Executes a graph with per-node dirty cache (Blender depsgraph-style input_hash memoization).
 * Falls back to v4 when mesh_count is 0; v4 falls back to v3 when texture_count is 0.
 */
PCG_API PcgResultCode pcg_execute_graph_v5(const char* json,
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
                                           int err_buf_size);

/**
 * Executes graph and returns points as binary when sink is point payload.
 * Falls back to v5 semantics for non-point outputs.
 * When sink payload includes spawnMesh, v6 also writes mesh binary to out_mesh_buf
 * and fills out_vertex_count/out_index_count (while out_kind remains POINTS).
 */
PCG_API PcgResultCode pcg_execute_graph_v6(const char* json,
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
                                           void* out_points_buf,
                                           int out_points_buf_size,
                                           int* out_point_count,
                                           uint32_t* out_point_attr_flags,
                                           int* out_vertex_count,
                                           int* out_index_count,
                                           PcgCookStats* out_stats,
                                           char* out_perf_json,
                                           int out_perf_json_size,
                                           char* err_buf,
                                           int err_buf_size);

typedef struct {
    const char* slot_id;
    int spline_count;
    const int* spline_point_counts;
    const float* positions;
    const uint8_t* closed;
} PcgSplineSlot;

/**
 * Executes a graph with optional runtime texture, mesh, and spline slot uploads.
 * Falls back to v6 when spline_count is 0; v6 falls back to v5 when mesh_count is 0.
 */
PCG_API PcgResultCode pcg_execute_graph_v7(const char* json,
                                           int seed,
                                           const PcgTextureSlot* textures,
                                           int texture_count,
                                           const PcgMeshSlot* meshes,
                                           int mesh_count,
                                           const PcgSplineSlot* splines,
                                           int spline_count,
                                           int* out_kind,
                                           char* out_json,
                                           int out_json_size,
                                           void* out_mesh_buf,
                                           int out_mesh_buf_size,
                                           void* out_points_buf,
                                           int out_points_buf_size,
                                           int* out_point_count,
                                           uint32_t* out_point_attr_flags,
                                           int* out_vertex_count,
                                           int* out_index_count,
                                           PcgCookStats* out_stats,
                                           char* out_perf_json,
                                           int out_perf_json_size,
                                           char* err_buf,
                                           int err_buf_size);

/**
 * Same as v7, plus optional best-effort geometry_binary export.
 * out_geometry_buf is filled only when Sink produced native PcgGeometry and the
 * buffer is large enough. On skip/failure *out_geometry_bytes_written stays 0
 * and Mesh/Points cooks still return PCG_OK when otherwise successful.
 */
PCG_API PcgResultCode pcg_execute_graph_v8(const char* json,
                                           int seed,
                                           const PcgTextureSlot* textures,
                                           int texture_count,
                                           const PcgMeshSlot* meshes,
                                           int mesh_count,
                                           const PcgSplineSlot* splines,
                                           int spline_count,
                                           int* out_kind,
                                           char* out_json,
                                           int out_json_size,
                                           void* out_mesh_buf,
                                           int out_mesh_buf_size,
                                           void* out_points_buf,
                                           int out_points_buf_size,
                                           int* out_point_count,
                                           uint32_t* out_point_attr_flags,
                                           int* out_vertex_count,
                                           int* out_index_count,
                                           PcgCookStats* out_stats,
                                           char* out_perf_json,
                                           int out_perf_json_size,
                                           void* out_geometry_buf,
                                           int out_geometry_buf_size,
                                           int* out_geometry_bytes_written,
                                           char* err_buf,
                                           int err_buf_size);

/** Clears the session-scoped per-node cook cache. */
PCG_API void pcg_cook_cache_clear(void);

/** Requests cancellation for the current running execution (best effort). */
PCG_API void pcg_request_cancel(void);

/** Clears the cancellation flag before a new execution starts. */
PCG_API void pcg_clear_cancel(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
