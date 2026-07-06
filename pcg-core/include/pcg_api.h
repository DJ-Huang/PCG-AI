#pragma once

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
    PCG_RESULT_KIND_MESH = 2
} PcgResultKind;

/* Mesh binary header: magic 'PCGM', version, vertex_count, index_count (16 bytes),
 * followed by float32 xyz positions and uint32 triangle indices. */
#define PCG_MESH_BINARY_MAGIC 0x4D474350u
#define PCG_MESH_BINARY_VERSION 1u
#define PCG_MESH_BINARY_HEADER_SIZE 16

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

#ifdef __cplusplus
} /* extern "C" */
#endif
