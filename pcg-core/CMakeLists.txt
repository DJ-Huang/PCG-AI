cmake_minimum_required(VERSION 3.20)

project(pcg-core
    VERSION 0.1.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Match Unity IL2CPP Windows linker CRT (/MT) for static lib consumption.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(json)

set(PCG_CORE_SOURCES
    src/pcg_core.cpp
    src/graph_parser.cpp
    src/graph_executor.cpp
    src/cook_hash.cpp
    src/graph_cook_cache.cpp
    src/data/pcg_metadata.cpp
    src/data/pcg_point_data.cpp
    src/data/pcg_param_data.cpp
    src/data/pcg_spline_data.cpp
    src/data/pcg_mesh_data.cpp
    src/data/pcg_geometry.cpp
    src/data/pcg_geometry_binary.cpp
    src/data/pcg_mesh_binary.cpp
    src/data/pcg_point_binary.cpp
    src/data/pcg_texture_data.cpp
    src/texture_runtime.cpp
    src/mesh_runtime.cpp
    src/spline_runtime.cpp
    src/elements/mesh_algorithms.cpp
    src/elements/geometry_algorithms.cpp
    src/elements/geometry_elements.cpp
    src/elements/bevel_blender.cpp
    src/geometry/bmesh.cpp
    src/geometry/group_table.cpp
    src/geometry/spline_geometry.cpp
    src/geometry/sweep_geometry.cpp
    src/elements/mesh_elements.cpp
    src/elements/mesh_scatter_algorithms.cpp
    src/elements/mesh_scatter_elements.cpp
    src/data/pcg_data_collection.cpp
    src/elements/element_registry.cpp
    src/elements/element_utils.cpp
    src/elements/primitive_elements.cpp
    src/elements/structural_algorithms.cpp
    src/elements/structural_elements.cpp
    src/elements/spline_algorithms.cpp
    src/elements/spline_elements.cpp
    src/elements/spline_mesh_elements.cpp
)

# ── Shared library (DLL) — for Unity Editor development ──
add_library(PcgCore SHARED ${PCG_CORE_SOURCES})
target_include_directories(PcgCore PUBLIC include PRIVATE src)
target_link_libraries(PcgCore PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(PcgCore PRIVATE PCG_EXPORTS)
set_target_properties(PcgCore PROPERTIES
    OUTPUT_NAME "PcgCore"
    POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
    target_compile_options(PcgCore PRIVATE /W4)
elseif(APPLE)
    target_compile_options(PcgCore PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()

# ── Static library (LIB) — for IL2CPP Player static linking ──
add_library(PcgCoreStatic STATIC ${PCG_CORE_SOURCES})
target_include_directories(PcgCoreStatic PUBLIC include PRIVATE src)
target_link_libraries(PcgCoreStatic PRIVATE nlohmann_json::nlohmann_json)
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

add_executable(test_data tests/test_data.cpp)
target_link_libraries(test_data PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_data PRIVATE src)

add_executable(test_phase41 tests/test_phase41.cpp)
target_link_libraries(test_phase41 PRIVATE PcgCoreStatic)

add_executable(test_phase42 tests/test_phase42.cpp)
target_link_libraries(test_phase42 PRIVATE PcgCoreStatic)

add_executable(test_phase43 tests/test_phase43.cpp)
target_link_libraries(test_phase43 PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_phase43 PRIVATE src)

add_test(NAME test_executor COMMAND test_executor WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME test_data COMMAND test_data)
add_test(NAME test_phase41 COMMAND test_phase41 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME test_phase42 COMMAND test_phase42 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME test_phase43 COMMAND test_phase43 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_phase44 tests/test_phase44.cpp)
target_link_libraries(test_phase44 PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_phase44 PRIVATE src)

add_test(NAME test_phase44 COMMAND test_phase44 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_cook_cache tests/test_cook_cache.cpp)
target_link_libraries(test_cook_cache PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_cook_cache PRIVATE src)

add_test(NAME test_cook_cache COMMAND test_cook_cache WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_spawn_mesh_binary tests/test_spawn_mesh_binary.cpp)
target_link_libraries(test_spawn_mesh_binary PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_spawn_mesh_binary PRIVATE src)

add_test(NAME test_spawn_mesh_binary COMMAND test_spawn_mesh_binary WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_phase45_spline tests/test_phase45_spline.cpp)
target_link_libraries(test_phase45_spline PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_phase45_spline PRIVATE src)

add_test(NAME test_phase45_spline COMMAND test_phase45_spline WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_phase46 tests/test_phase46.cpp)
target_link_libraries(test_phase46 PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_phase46 PRIVATE src)

add_test(NAME test_phase46 COMMAND test_phase46 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_bridge_bevel tests/test_bridge_bevel.cpp)
target_link_libraries(test_bridge_bevel PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_bridge_bevel PRIVATE src)

add_test(NAME test_bridge_bevel COMMAND test_bridge_bevel WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_stone_arch_bridge tests/test_stone_arch_bridge.cpp)
target_link_libraries(test_stone_arch_bridge PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_stone_arch_bridge PRIVATE src)

add_test(NAME test_stone_arch_bridge COMMAND test_stone_arch_bridge WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

