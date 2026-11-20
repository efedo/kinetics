# Third-party dependencies for the desktop application, fetched at configure
# time and pinned by version and SHA-256:
#   SDL 3.4.18      - window, input, renderer, native file dialogs
#   Dear ImGui 1.92.8 - user interface
#   ImPlot 1.0      - graphs
#
# To build offline, point FETCHCONTENT_SOURCE_DIR_SDL3 / _IMGUI / _IMPLOT at
# already-extracted copies of the same versions.

include(FetchContent)

# SDL is linked statically so the program is a single executable.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL3
    URL      https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz
    URL_HASH SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    EXCLUDE_FROM_ALL
)

# Dear ImGui and ImPlot have no top-level CMakeLists.txt, so MakeAvailable
# only downloads them and the library target is defined below.
FetchContent_Declare(imgui
    URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.92.8.tar.gz
    URL_HASH SHA256=fecb33d33930e12ff53a34064e9d3a06c8f7c3e04408f14cd36c80e3faac863b
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(implot
    URL      https://github.com/epezent/implot/archive/refs/tags/v1.0.tar.gz
    URL_HASH SHA256=e4a9db64eef7bcc604e2a5ea380af124eb97aa3e8a6a96330079c88add0f7e93
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(SDL3 imgui implot)

if(TARGET SDL3::SDL3-static)
    set(KINETICS_SDL_TARGET SDL3::SDL3-static)
else()
    set(KINETICS_SDL_TARGET SDL3::SDL3)
endif()

add_library(kinetics_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_include_directories(kinetics_imgui SYSTEM PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imgui_SOURCE_DIR}/misc/cpp
    ${implot_SOURCE_DIR}
)
# (ImPlot 1.0 still uses ImDrawList overloads that IMGUI_DISABLE_OBSOLETE_FUNCTIONS
# removes, so that option is left off.)
target_link_libraries(kinetics_imgui PUBLIC ${KINETICS_SDL_TARGET})
