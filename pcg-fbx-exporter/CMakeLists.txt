cmake_minimum_required(VERSION 3.20)

project(pcg-fbx-exporter VERSION 1.0.0 LANGUAGES CXX)

option(PCG_FBX_ENABLE_READBACK_TESTS "Build Assimp FBX importer for round-trip tests" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
if(APPLE)
    set(ASSIMP_BUILD_ZLIB OFF CACHE BOOL "" FORCE)
else()
    set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
endif()
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_EXPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ${PCG_FBX_ENABLE_READBACK_TESTS} CACHE BOOL "" FORCE)

include(FetchContent)
include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/PcgAssimp.cmake")
pcg_declare_assimp()
FetchContent_MakeAvailable(assimp)

add_library(PcgFbxExporter SHARED
    src/geometry_binary_reader.cpp
    src/pcg_fbx_exporter.cpp
)
target_include_directories(PcgFbxExporter PUBLIC include PRIVATE src)
target_link_libraries(PcgFbxExporter PRIVATE assimp::assimp)
target_compile_definitions(PcgFbxExporter PRIVATE PCG_FBX_EXPORTS)
set_target_properties(PcgFbxExporter PROPERTIES
    OUTPUT_NAME "PcgFbxExporter"
    POSITION_INDEPENDENT_CODE ON
)

if(MSVC)
    target_compile_options(PcgFbxExporter PRIVATE /W4)
elseif(APPLE)
    target_compile_options(PcgFbxExporter PRIVATE -Wall -Wextra -fvisibility=hidden)
endif()

enable_testing()
add_executable(test_pcg_fbx_exporter
    tests/test_pcg_fbx_exporter.cpp
)
target_include_directories(test_pcg_fbx_exporter PRIVATE include src)
target_link_libraries(test_pcg_fbx_exporter PRIVATE PcgFbxExporter)
if(PCG_FBX_ENABLE_READBACK_TESTS)
    target_compile_definitions(test_pcg_fbx_exporter PRIVATE PCG_FBX_ENABLE_READBACK_TESTS=1)
    target_link_libraries(test_pcg_fbx_exporter PRIVATE assimp::assimp)
endif()
add_test(NAME test_pcg_fbx_exporter COMMAND test_pcg_fbx_exporter)
