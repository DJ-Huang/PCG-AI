#pragma once

#include <stdint.h>

#if defined(_WIN32)
#  if defined(PCG_FBX_EXPORTS)
#    define PCG_FBX_API __declspec(dllexport)
#  else
#    define PCG_FBX_API __declspec(dllimport)
#  endif
#else
#  define PCG_FBX_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum PcgFbxResult {
    PCG_FBX_OK = 0,
    PCG_FBX_INVALID_ARGUMENT = 1,
    PCG_FBX_INVALID_GEOMETRY = 2,
    PCG_FBX_EXPORT_FAILED = 3,
};

typedef struct PcgFbxExportOptions {
    uint32_t struct_size;
    float scale;
    int32_t generate_normals;
} PcgFbxExportOptions;

/// Export a PcgGeometry binary v2 payload to FBX.
/// The function is synchronous and never takes ownership of the input buffer.
PCG_FBX_API int pcg_fbx_export_v1(
    const void* geometry_binary,
    int geometry_binary_size,
    const char* output_path_utf8,
    const PcgFbxExportOptions* options,
    char* error_buffer,
    int error_buffer_size);

PCG_FBX_API const char* pcg_fbx_get_version(void);

#ifdef __cplusplus
}
#endif
