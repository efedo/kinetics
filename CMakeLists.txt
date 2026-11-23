cmake_minimum_required(VERSION 3.24)

project(Kinetics
    VERSION 4.0.0
    DESCRIPTION "Enzyme kinetics curve fitting (modern port of SB's Kinetics Program 3.51)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(KINETICS_BUILD_APP   "Build the SDL3 + Dear ImGui desktop application" ON)
option(KINETICS_BUILD_TESTS "Build the core library tests" ON)
option(KINETICS_COVERAGE    "Instrument the core library and tests for gcov-style coverage (Clang or GCC, including Emscripten)" OFF)

if(MSVC)
    set(KINETICS_WARNINGS /W4 /permissive- /utf-8)
else()
    set(KINETICS_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Core: models, fitting, statistics, import and report formatting.
# No GUI dependencies, so it can be tested (and reused) on its own.
# ---------------------------------------------------------------------------
add_library(kinetics_core STATIC
    source/remake/core/models.cpp
    source/remake/core/linalg.cpp
    source/remake/core/fit.cpp
    source/remake/core/stats.cpp
    source/remake/core/guess.cpp
    source/remake/core/analysis.cpp
    source/remake/core/batch.cpp
    source/remake/core/plots.cpp
    source/remake/core/import.cpp
    source/remake/core/report.cpp
    source/remake/core/settings.cpp
)
target_include_directories(kinetics_core PUBLIC source/remake)
target_compile_options(kinetics_core PRIVATE ${KINETICS_WARNINGS})
target_compile_definitions(kinetics_core PUBLIC
    KINETICS_VERSION="${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# Desktop application
# ---------------------------------------------------------------------------
if(KINETICS_BUILD_APP)
    include(cmake/Dependencies.cmake)

    # The classic (v3.51) interface: an emulated text-mode PC drawn with SDL,
    # no ImGui. The foundation is in source/remake/classic, the program's
    # modules in its edit, cd, file and view subfolders.
    file(GLOB KINETICS_CLASSIC_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_SOURCE_DIR}/source/remake/classic/*.cpp
        ${CMAKE_SOURCE_DIR}/source/remake/classic/*/*.cpp)
    # It is a port of C-style code (strcpy, sprintf into fixed buffers).
    set_source_files_properties(${KINETICS_CLASSIC_SOURCES} PROPERTIES COMPILE_DEFINITIONS _CRT_SECURE_NO_WARNINGS)

    add_executable(kinetics WIN32
        source/remake/app/main.cpp
        source/remake/app/app.cpp
        source/remake/app/ui_data.cpp
        source/remake/app/ui_results.cpp
        source/remake/app/ui_plot.cpp
        source/remake/app/ui_batch.cpp
        source/remake/app/ui_dialogs.cpp
        source/remake/app/ui_frame.cpp
        source/remake/app/help_text.cpp
        source/remake/app/platform.cpp
        source/remake/app/theme.cpp
        source/remake/app/classic_session.cpp
        ${KINETICS_CLASSIC_SOURCES}
    )
    target_link_libraries(kinetics PRIVATE kinetics_core kinetics_imgui)
    target_compile_options(kinetics PRIVATE ${KINETICS_WARNINGS})

    if(EMSCRIPTEN)
        # Browser build: index.html + index.js + index.wasm, with the example
        # data and the fonts embedded in the module's virtual file system.
        set_target_properties(kinetics PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
        target_link_options(kinetics PRIVATE
            "--shell-file=${CMAKE_SOURCE_DIR}/web/shell.html"
            "--embed-file=${CMAKE_SOURCE_DIR}/examples@/examples"
            "--embed-file=${imgui_SOURCE_DIR}/misc/fonts/Roboto-Medium.ttf@/fonts/Roboto-Medium.ttf"
            "--embed-file=${imgui_SOURCE_DIR}/misc/fonts/Cousine-Regular.ttf@/fonts/Cousine-Regular.ttf"
            "-sALLOW_MEMORY_GROWTH=1"
            "-sSTACK_SIZE=1048576"
            "-sENVIRONMENT=web"
            "-sEXPORTED_FUNCTIONS=_main,_malloc,_free")
        set_property(TARGET kinetics APPEND PROPERTY LINK_DEPENDS "${CMAKE_SOURCE_DIR}/web/shell.html")
    else()
        set_target_properties(kinetics PROPERTIES OUTPUT_NAME "Kinetics")
        # Example data next to the executable, for File > Open example
        add_custom_command(TARGET kinetics POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                    "${CMAKE_SOURCE_DIR}/examples" "$<TARGET_FILE_DIR:kinetics>/examples")
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(KINETICS_BUILD_TESTS)
    enable_testing()
    # Every tests/*.cpp is part of the suite. To build a subset while working
    # on it, set KINETICS_TEST_GLOBS, e.g. "tests/main.cpp;tests/test_fit*.cpp".
    set(KINETICS_TEST_GLOBS "tests/*.cpp" CACHE STRING "Test sources to build (globs relative to the source folder)")
    set(_test_globs)
    foreach(g IN LISTS KINETICS_TEST_GLOBS)
        list(APPEND _test_globs "${CMAKE_SOURCE_DIR}/${g}")
    endforeach()
    file(GLOB KINETICS_TEST_SOURCES CONFIGURE_DEPENDS ${_test_globs})
    add_executable(kinetics_tests ${KINETICS_TEST_SOURCES})
    target_link_libraries(kinetics_tests PRIVATE kinetics_core)
    target_compile_options(kinetics_tests PRIVATE ${KINETICS_WARNINGS})
    target_compile_definitions(kinetics_tests PRIVATE
        KINETICS_EXAMPLES_DIR="${CMAKE_SOURCE_DIR}/examples")
    if(EMSCRIPTEN)
        # Run under Node.js with direct access to the host file system.
        target_link_options(kinetics_tests PRIVATE "-sNODERAWFS=1" "-sENVIRONMENT=node")
    endif()
    add_test(NAME kinetics_core COMMAND kinetics_tests)
endif()

# ---------------------------------------------------------------------------
# Coverage (tools/coverage.py reads the results)
# ---------------------------------------------------------------------------
if(KINETICS_COVERAGE)
    if(MSVC)
        message(FATAL_ERROR "KINETICS_COVERAGE needs Clang or GCC; use the wasm-coverage preset on Windows.")
    endif()
    target_compile_options(kinetics_core PRIVATE --coverage -O0)
    if(TARGET kinetics_tests)
        target_compile_options(kinetics_tests PRIVATE --coverage -O0)
        target_link_options(kinetics_tests PRIVATE --coverage)
        if(EMSCRIPTEN)
            # The counters are written by an exit handler, which Emscripten
            # skips unless the runtime exits.
            target_link_options(kinetics_tests PRIVATE "-sEXIT_RUNTIME=1")
        endif()
    endif()
endif()
