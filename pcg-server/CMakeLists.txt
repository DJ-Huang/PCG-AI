cmake_minimum_required(VERSION 3.20)

# Standalone cook HTTP server. Prefer building via:
#   cmake -S pcg-server -B pcg-server/build
# which pulls in ../pcg-core as a subdirectory.

project(pcg-server
    VERSION 0.1.0
    LANGUAGES CXX
)

find_package(CURL REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PCG_SERVER_BUILD_CORE "Add ../pcg-core as a subdirectory" ON)

if(PCG_SERVER_BUILD_CORE)
    if(NOT TARGET PcgCoreStatic)
        set(PCG_CORE_SKIP_TESTS ON CACHE BOOL "" FORCE)
        set(PCG_CORE_ENABLE_FBX_EXPORT ON CACHE BOOL "" FORCE)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../pcg-core" "${CMAKE_BINARY_DIR}/pcg-core")
    endif()
endif()

if(NOT TARGET PcgCoreStatic)
    message(FATAL_ERROR "PcgCoreStatic target not found. Build from pcg-server or after pcg-core.")
endif()

set(PCG_FBX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pcg-fbx-exporter")

add_executable(pcg-server
    src/main.cpp
    src/cook_service.cpp
    src/agent_service.cpp
    src/agent_runtime.cpp
    src/session_service.cpp
    src/mcp_service.cpp
    "${PCG_FBX_DIR}/src/pcg_fbx_exporter.cpp"
    "${PCG_FBX_DIR}/src/geometry_binary_reader.cpp"
)
target_include_directories(pcg-server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
    "${PCG_FBX_DIR}/include"
    "${PCG_FBX_DIR}/src"
)
target_link_libraries(pcg-server PRIVATE
    PcgCoreStatic
    assimp::assimp
    nlohmann_json::nlohmann_json
    CURL::libcurl
)
target_compile_definitions(pcg-server PRIVATE PCG_FBX_EXPORTS)

if(MSVC)
    target_compile_options(pcg-server PRIVATE /W4)
else()
    target_compile_options(pcg-server PRIVATE -Wall -Wextra)
endif()

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(pcg-server PRIVATE Threads::Threads)
endif()

if(APPLE)
    target_link_libraries(pcg-server PRIVATE "-framework Security" "-framework CoreFoundation")
endif()
