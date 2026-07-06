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

#ifdef __cplusplus
} /* extern "C" */
#endif
