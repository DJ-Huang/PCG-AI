include_guard(GLOBAL)
include(FetchContent)

# Update only after repeating docs/scripting/runtime-decision.md's acceptance matrix.
# A full Git object ID, not a movable tag or a branch, is the dependency lock.
set(PCG_QUICKJS_VERSION "0.16.2")
set(PCG_QUICKJS_COMMIT "1ab8676f4b6d6d669baeb5f21790fb9734636a20")

function(pcg_add_quickjs)
    if(TARGET PcgQuickJS)
        return()
    endif()
    find_package(Git REQUIRED)
    find_package(Threads REQUIRED)
    FetchContent_Declare(pcg_quickjs_source
        GIT_REPOSITORY https://github.com/quickjs-ng/quickjs.git
        GIT_TAG "${PCG_QUICKJS_COMMIT}"
        GIT_SUBMODULES ""
        # Do not run upstream's CMake: its default ALL target also builds the CLI,
        # libc, qjsc, and tests. This spike needs only the four engine sources.
        SOURCE_SUBDIR pcg-engine-only-no-upstream-cmake
    )
    FetchContent_MakeAvailable(pcg_quickjs_source)

    # Also validate FetchContent's local-source override. An unverified system
    # QuickJS, a dirty checkout, or a different first-declaration-wins pin must
    # not silently masquerade as the locked dependency.
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${pcg_quickjs_source_SOURCE_DIR}"
        rev-parse HEAD RESULT_VARIABLE git_status OUTPUT_VARIABLE source_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(NOT git_status EQUAL 0 OR NOT source_commit STREQUAL PCG_QUICKJS_COMMIT)
        message(FATAL_ERROR "QuickJS-NG requires a Git checkout at ${PCG_QUICKJS_COMMIT}")
    endif()
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${pcg_quickjs_source_SOURCE_DIR}"
        diff --quiet --ignore-submodules=all HEAD --
        RESULT_VARIABLE git_status)
    if(NOT git_status EQUAL 0)
        message(FATAL_ERROR "QuickJS-NG has tracked modifications; use the clean pinned checkout")
    endif()

    # Mirrors the engine-only source list in the pinned upstream CMakeLists.txt.
    add_library(PcgQuickJS STATIC
        "${pcg_quickjs_source_SOURCE_DIR}/quickjs.c"
        "${pcg_quickjs_source_SOURCE_DIR}/dtoa.c"
        "${pcg_quickjs_source_SOURCE_DIR}/libregexp.c"
        "${pcg_quickjs_source_SOURCE_DIR}/libunicode.c")
    set_target_properties(PcgQuickJS PROPERTIES
        C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS ON
        POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
    target_include_directories(PcgQuickJS SYSTEM PUBLIC "${pcg_quickjs_source_SOURCE_DIR}")
    target_compile_definitions(PcgQuickJS PRIVATE _GNU_SOURCE QUICKJS_NG_BUILD)
    include(CheckCCompilerFlag)
    check_c_compiler_flag("-funsigned-char" PCG_QJS_HAS_UNSIGNED_CHAR_FLAG)
    if(PCG_QJS_HAS_UNSIGNED_CHAR_FLAG)
        target_compile_options(PcgQuickJS PRIVATE -funsigned-char)
    endif()
    target_link_libraries(PcgQuickJS PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
    if(WIN32)
        target_compile_definitions(PcgQuickJS PRIVATE
            WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601 _CRT_SECURE_NO_WARNINGS)
        if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
            include(CheckCCompilerFlag)
            check_c_compiler_flag("/experimental:c11atomics" PCG_QJS_HAS_C11_ATOMICS_FLAG)
            if(PCG_QJS_HAS_C11_ATOMICS_FLAG)
                target_compile_options(PcgQuickJS PRIVATE /experimental:c11atomics)
            endif()
        endif()
    elseif(UNIX)
        find_library(PCG_QJS_M_LIBRARY m)
        if(PCG_QJS_M_LIBRARY)
            target_link_libraries(PcgQuickJS PRIVATE "${PCG_QJS_M_LIBRARY}")
        endif()
    endif()
    # Never compile quickjs-libc.c or define QJS_BUILD_LIBC / QJS_DISABLE_PARSER.
endfunction()
