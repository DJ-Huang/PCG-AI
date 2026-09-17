#pragma once

// Test-only ABI: no QuickJS pointers, JSValues, or C++ allocations cross a DLL.
#if defined(_WIN32) && defined(PCG_SCRIPTING_PROBE_SHARED)
#  if defined(PCG_SCRIPTING_PROBE_EXPORTS)
#    define PCG_SCRIPTING_PROBE_API __declspec(dllexport)
#  else
#    define PCG_SCRIPTING_PROBE_API __declspec(dllimport)
#  endif
#elif defined(PCG_SCRIPTING_PROBE_SHARED)
#  define PCG_SCRIPTING_PROBE_API __attribute__((visibility("default")))
#else
#  define PCG_SCRIPTING_PROBE_API
#endif

extern "C" {
PCG_SCRIPTING_PROBE_API int pcg_scripting_run_probe(const char* name);
PCG_SCRIPTING_PROBE_API int pcg_scripting_benchmark(unsigned samples);
}
