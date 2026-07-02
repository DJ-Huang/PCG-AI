cmake_minimum_required(VERSION 3.20)

project(pcg-core
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Shared library (DLL) — for Unity Editor development ──
add_library(PcgCore SHARED
    src/pcg_core.cpp
)
target_include_directories(PcgCore PUBLIC include)
target_compile_definitions(PcgCore PRIVATE PCG_EXPORTS)
set_target_properties(PcgCore PROPERTIES
    OUTPUT_NAME "PcgCore"
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(PcgCore PRIVATE /W4)
endif()

# ── Static library (LIB) — for IL2CPP Player static linking ──
add_library(PcgCoreStatic STATIC
    src/pcg_core.cpp
)
target_include_directories(PcgCoreStatic PUBLIC include)
target_compile_definitions(PcgCoreStatic PUBLIC PCG_STATIC)
set_target_properties(PcgCoreStatic PROPERTIES
    OUTPUT_NAME "PcgCore"
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(PcgCoreStatic PRIVATE /W4)
endif()

# ── Tests ─────────────────────────────────────────────
enable_testing()

add_executable(test_executor tests/test_executor.cpp)
target_link_libraries(test_executor PRIVATE PcgCoreStatic)

add_test(NAME test_executor COMMAND test_executor)
