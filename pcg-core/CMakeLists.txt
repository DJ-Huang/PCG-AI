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
    src/elements/bevel_diag.cpp
    src/geometry/bmesh.cpp
    src/geometry/group_table.cpp
    src/geometry/spline_geometry.cpp
    src/geometry/sweep_geometry.cpp
    src/geometry/robust_predicates.cpp
    src/geometry/coplanar_partition.cpp
    src/geometry/imesh.cpp
    src/geometry/bvh.cpp
    src/geometry/tri_split.cpp
    src/geometry/arrangement.cpp
    src/geometry/mesh_topology.cpp
    src/geometry/boolean_csg.cpp
    src/geometry/boolean_output.cpp
    src/elements/mesh_elements.cpp
    src/elements/boolean_elements.cpp
    src/elements/mesh_scatter_algorithms.cpp
    src/elements/mesh_scatter_elements.cpp
    src/data/pcg_data_collection.cpp
    src/elements/element_registry.cpp
    src/elements/element_utils.cpp
    src/elements/expression.cpp
    src/elements/attribute_elements.cpp
    src/elements/primitive_elements.cpp
    src/elements/structural_algorithms.cpp
    src/elements/structural_elements.cpp
    src/elements/spline_algorithms.cpp
    src/elements/spline_elements.cpp
    src/elements/spline_mesh_elements.cpp
    src/elements/material_algorithms.cpp
    src/elements/material_elements.cpp
    src/elements/uv_algorithms.cpp
    src/elements/uv_elements.cpp
    src/elements/vehicle_modeling_algorithms.cpp
    src/elements/vehicle_modeling_elements.cpp
    src/elements/building_algorithms.cpp
    src/elements/building_elements.cpp
)

# ── Shared library (DLL) — for Unity Editor development ──
add_library(PcgCore SHARED ${PCG_CORE_SOURCES})
target_include_directories(PcgCore PUBLIC include PRIVATE src third_party/CDT/CDT/include)
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
target_include_directories(PcgCoreStatic PUBLIC include PRIVATE src third_party/CDT/CDT/include)
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

add_executable(test_geometry_export tests/test_geometry_export.cpp)
target_link_libraries(test_geometry_export PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_geometry_export PRIVATE src)

add_test(NAME test_geometry_export COMMAND test_geometry_export WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_bridge_bevel tests/test_bridge_bevel.cpp)
target_link_libraries(test_bridge_bevel PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_bridge_bevel PRIVATE src)

add_test(NAME test_bridge_bevel COMMAND test_bridge_bevel WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_bevel_manifold tests/test_bevel_manifold.cpp)
target_link_libraries(test_bevel_manifold PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_bevel_manifold PRIVATE src)

add_test(NAME test_bevel_manifold COMMAND test_bevel_manifold WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_bevel_polygon tests/test_bevel_polygon.cpp)
target_link_libraries(test_bevel_polygon PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_bevel_polygon PRIVATE src)

add_test(NAME test_bevel_polygon COMMAND test_bevel_polygon WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_split_normals tests/test_split_normals.cpp)
target_link_libraries(test_split_normals PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_split_normals PRIVATE src)

add_test(NAME test_split_normals COMMAND test_split_normals WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_cylinder_revolve tests/test_cylinder_revolve.cpp)
target_link_libraries(test_cylinder_revolve PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_cylinder_revolve PRIVATE src)

add_test(NAME test_cylinder_revolve COMMAND test_cylinder_revolve WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_spiral_spline tests/test_spiral_spline.cpp)
target_link_libraries(test_spiral_spline PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_spiral_spline PRIVATE src)

add_test(NAME test_spiral_spline COMMAND test_spiral_spline WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_new_nodes_graph tests/test_new_nodes_graph.cpp)
target_link_libraries(test_new_nodes_graph PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_new_nodes_graph PRIVATE src)

add_test(NAME test_new_nodes_graph COMMAND test_new_nodes_graph WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_vehicle_modeling tests/test_vehicle_modeling.cpp)
target_link_libraries(test_vehicle_modeling PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_vehicle_modeling PRIVATE src)

add_test(NAME test_vehicle_modeling COMMAND test_vehicle_modeling WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_stone_arch_bridge tests/test_stone_arch_bridge.cpp)
target_link_libraries(test_stone_arch_bridge PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_stone_arch_bridge PRIVATE src)

add_test(NAME test_stone_arch_bridge COMMAND test_stone_arch_bridge WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_subgraph tests/test_subgraph.cpp)
target_link_libraries(test_subgraph PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_subgraph PRIVATE src)

add_test(NAME test_subgraph COMMAND test_subgraph WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_building_nodes tests/test_building_nodes.cpp)
target_link_libraries(test_building_nodes PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_building_nodes PRIVATE src)

add_test(NAME test_building_nodes COMMAND test_building_nodes WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_attribute_nodes tests/test_attribute_nodes.cpp)
target_link_libraries(test_attribute_nodes PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_attribute_nodes PRIVATE src)

add_test(NAME test_attribute_nodes COMMAND test_attribute_nodes WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# ── Phase 4.7: Boolean Mesh ─────────────────────────────

add_executable(test_robust_predicates tests/test_robust_predicates.cpp)
target_link_libraries(test_robust_predicates PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_robust_predicates PRIVATE src)

add_test(NAME test_robust_predicates COMMAND test_robust_predicates WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_tri_intersect tests/test_tri_intersect.cpp)
target_link_libraries(test_tri_intersect PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_tri_intersect PRIVATE src)

add_test(NAME test_tri_intersect COMMAND test_tri_intersect WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_bvh tests/test_bvh.cpp)
target_link_libraries(test_bvh PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_bvh PRIVATE src)

add_test(NAME test_bvh COMMAND test_bvh WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_imesh tests/test_imesh.cpp)
target_link_libraries(test_imesh PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_imesh PRIVATE src)

add_test(NAME test_imesh COMMAND test_imesh WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_arrangement tests/test_arrangement.cpp)
target_link_libraries(test_arrangement PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_arrangement PRIVATE src)

add_test(NAME test_arrangement COMMAND test_arrangement WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_boolean_output tests/test_boolean_output.cpp)
target_link_libraries(test_boolean_output PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_boolean_output PRIVATE src)

add_test(NAME test_boolean_output COMMAND test_boolean_output WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_boolean_binary tests/test_boolean_binary.cpp)
target_link_libraries(test_boolean_binary PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_boolean_binary PRIVATE src)

add_test(NAME test_boolean_binary COMMAND test_boolean_binary WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# ── B4: Boolean element + graph tests ──

add_executable(test_boolean tests/test_boolean.cpp)
target_link_libraries(test_boolean PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_boolean PRIVATE src)

add_test(NAME test_boolean COMMAND test_boolean WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_boolean_graph tests/test_boolean_graph.cpp)
target_link_libraries(test_boolean_graph PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_boolean_graph PRIVATE src)

add_test(NAME test_boolean_graph COMMAND test_boolean_graph WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_executable(test_boolean_bevel_chain tests/test_boolean_bevel_chain.cpp)
target_link_libraries(test_boolean_bevel_chain PRIVATE PcgCoreStatic nlohmann_json::nlohmann_json)
target_include_directories(test_boolean_bevel_chain PRIVATE src)

add_test(NAME test_boolean_bevel_chain COMMAND test_boolean_bevel_chain WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# CTest is intentionally limited to focused functional regressions. Production
# demo graphs belong in manual profiling, not in the build verification loop.
get_property(PCG_FAST_TESTS DIRECTORY PROPERTY TESTS)
set_tests_properties(${PCG_FAST_TESTS} PROPERTIES
    LABELS "fast"
    TIMEOUT 10
)
