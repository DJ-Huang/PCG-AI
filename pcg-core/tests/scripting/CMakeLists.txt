cmake_minimum_required(VERSION 3.20)
project(pcg-scripting-runtime-spike LANGUAGES C CXX)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# Same CRT policy as pcg-core, including the static-library consumer probes.
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(EMSCRIPTEN OR NOT CMAKE_SYSTEM_NAME MATCHES "^(Windows|Linux|Darwin)$")
    message(FATAL_ERROR "This native spike targets Windows, Linux, and macOS only")
endif()

get_filename_component(PCG_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(PCG_SCRIPTING_HOST "standalone" CACHE STRING "standalone, core, or server")
set_property(CACHE PCG_SCRIPTING_HOST PROPERTY STRINGS standalone core server)
set(PCG_SCRIPTING_SANITIZER "none" CACHE STRING "none, address, or thread")
set_property(CACHE PCG_SCRIPTING_SANITIZER PROPERTY STRINGS none address thread)
if(NOT PCG_SCRIPTING_HOST MATCHES "^(standalone|core|server)$")
    message(FATAL_ERROR "Invalid PCG_SCRIPTING_HOST")
endif()
if(NOT PCG_SCRIPTING_SANITIZER MATCHES "^(none|address|thread)$")
    message(FATAL_ERROR "Invalid PCG_SCRIPTING_SANITIZER")
endif()
if(NOT PCG_SCRIPTING_SANITIZER STREQUAL "none")
    if(WIN32 OR NOT CMAKE_C_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
        message(FATAL_ERROR "These sanitizer presets require GCC or Clang on Unix")
    endif()
    if(PCG_SCRIPTING_SANITIZER STREQUAL "address")
        set(pcg_sanitizer_flag -fsanitize=address,undefined)
    else()
        set(pcg_sanitizer_flag -fsanitize=thread)
    endif()
    add_compile_options(${pcg_sanitizer_flag} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(${pcg_sanitizer_flag} -fno-sanitize-recover=all)
endif()

# Optional integration builds exercise the actual repository targets. The
# default standalone spike avoids Assimp, JSON, CURL, and all production changes.
if(PCG_SCRIPTING_HOST STREQUAL "core")
    set(PCG_CORE_SKIP_TESTS ON CACHE BOOL "" FORCE)
    add_subdirectory("${PCG_ROOT}/pcg-core" "${CMAKE_CURRENT_BINARY_DIR}/core")
elseif(PCG_SCRIPTING_HOST STREQUAL "server")
    add_subdirectory("${PCG_ROOT}/pcg-server" "${CMAKE_CURRENT_BINARY_DIR}/server")
endif()

include("${PCG_ROOT}/cmake/PcgQuickJS.cmake")
pcg_add_quickjs()
find_package(Threads REQUIRED)
enable_testing()

set(probe_cases smoke imports async capabilities exceptions interrupt cancellation
    memory stack teardown concurrency source_limit diagnostics)
foreach(linkage IN ITEMS static shared)
    string(TOUPPER "${linkage}" library_kind)
    add_library(pcg_scripting_probe_${linkage} ${library_kind} runtime_probe.cpp)
    target_link_libraries(pcg_scripting_probe_${linkage} PRIVATE PcgQuickJS Threads::Threads)
    target_compile_definitions(pcg_scripting_probe_${linkage} PRIVATE
        PCG_QUICKJS_VERSION="${PCG_QUICKJS_VERSION}"
        PCG_QUICKJS_COMMIT="${PCG_QUICKJS_COMMIT}"
        PCG_PROBE_COMPILER="${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")
    set_target_properties(pcg_scripting_probe_${linkage} PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
    if(linkage STREQUAL "shared")
        target_compile_definitions(pcg_scripting_probe_${linkage}
            PUBLIC PCG_SCRIPTING_PROBE_SHARED PRIVATE PCG_SCRIPTING_PROBE_EXPORTS)
    endif()
    if(MSVC)
        target_compile_options(pcg_scripting_probe_${linkage} PRIVATE /W4)
    else()
        target_compile_options(pcg_scripting_probe_${linkage} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_executable(test_scripting_${linkage} probe_main.cpp)
    target_link_libraries(test_scripting_${linkage} PRIVATE pcg_scripting_probe_${linkage})
    if(WIN32)
        if(MSVC)
            target_link_options(test_scripting_${linkage} PRIVATE /STACK:8388608)
        elseif(MINGW)
            target_link_options(test_scripting_${linkage} PRIVATE -Wl,--stack,8388608)
        endif()
    endif()
    foreach(probe_case IN LISTS probe_cases)
        add_test(NAME scripting_${linkage}_${probe_case}
            COMMAND test_scripting_${linkage} ${probe_case})
        set_tests_properties(scripting_${linkage}_${probe_case}
            PROPERTIES TIMEOUT 30 LABELS "scripting;runtime-spike")
    endforeach()
    add_custom_command(TARGET test_scripting_${linkage} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:test_scripting_${linkage}>/licenses/quickjs-ng"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${PCG_ROOT}/third_party/quickjs-ng/LICENSE"
            "${PCG_ROOT}/third_party/quickjs-ng/UNICODE-LICENSE"
            "$<TARGET_FILE_DIR:test_scripting_${linkage}>/licenses/quickjs-ng"
        VERBATIM)
endforeach()

if(NOT PCG_SCRIPTING_HOST STREQUAL "standalone")
    foreach(linkage IN ITEMS static shared)
        add_executable(test_scripting_core_${linkage} probe_main.cpp)
        target_compile_definitions(test_scripting_core_${linkage} PRIVATE PCG_SCRIPTING_WITH_CORE)
        target_link_libraries(test_scripting_core_${linkage} PRIVATE pcg_scripting_probe_${linkage})
        if(linkage STREQUAL "static")
            target_link_libraries(test_scripting_core_${linkage} PRIVATE PcgCoreStatic)
        else()
            target_link_libraries(test_scripting_core_${linkage} PRIVATE PcgCore)
            if(WIN32)
                add_custom_command(TARGET test_scripting_core_shared POST_BUILD
                    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:PcgCore>"
                        "$<TARGET_FILE_DIR:test_scripting_core_shared>" VERBATIM)
            endif()
        endif()
        add_test(NAME scripting_core_${linkage}_smoke COMMAND test_scripting_core_${linkage} smoke)
        set_tests_properties(scripting_core_${linkage}_smoke
            PROPERTIES TIMEOUT 30 LABELS "scripting;runtime-spike;core-integration")
    endforeach()
endif()

install(FILES "${PCG_ROOT}/third_party/quickjs-ng/LICENSE"
    "${PCG_ROOT}/third_party/quickjs-ng/UNICODE-LICENSE"
    DESTINATION share/licenses/pcg-scripting-runtime-spike/quickjs-ng)
