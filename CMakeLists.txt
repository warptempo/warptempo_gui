cmake_minimum_required(VERSION 3.10)
project(warptempo_gui VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Dependencies for the DSP Engine ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED sndfile)
pkg_check_modules(FFTW3 REQUIRED fftw3)
find_library(FFTW3_THREADS_LIB fftw3_threads HINTS ${FFTW3_LIBRARY_DIRS} REQUIRED)

# --- GUI platform dependencies (Wayland) ---
pkg_check_modules(WAYLAND_CLIENT    REQUIRED wayland-client)
pkg_check_modules(WAYLAND_CURSOR    REQUIRED wayland-cursor)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(XKBCOMMON         REQUIRED xkbcommon)
pkg_get_variable(WAYLAND_PROTOCOLS_DATADIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER_EXEC NAMES wayland-scanner REQUIRED)

# wayland-scanner: for each protocol XML, generate a client header and a
# private dispatch-table .c. Both files land in ${CMAKE_BINARY_DIR}/wayland-
# generated/ and are added to the warptempo_gui sources below. DEPENDS on
# the source XML so wayland-protocols updates trigger regeneration.
set(WAYLAND_PROTOCOL_SOURCES
    ${WAYLAND_PROTOCOLS_DATADIR}/stable/xdg-shell/xdg-shell.xml
    ${WAYLAND_PROTOCOLS_DATADIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
)
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/wayland-generated)
set(WAYLAND_GENERATED_SOURCES "")
foreach(proto IN LISTS WAYLAND_PROTOCOL_SOURCES)
    get_filename_component(proto_name ${proto} NAME_WE)
    set(proto_h ${CMAKE_BINARY_DIR}/wayland-generated/${proto_name}-client-protocol.h)
    set(proto_c ${CMAKE_BINARY_DIR}/wayland-generated/${proto_name}-protocol.c)
    add_custom_command(
        OUTPUT  ${proto_h}
        COMMAND ${WAYLAND_SCANNER_EXEC} client-header ${proto} ${proto_h}
        DEPENDS ${proto}
        VERBATIM)
    add_custom_command(
        OUTPUT  ${proto_c}
        COMMAND ${WAYLAND_SCANNER_EXEC} private-code ${proto} ${proto_c}
        DEPENDS ${proto}
        VERBATIM)
    list(APPEND WAYLAND_GENERATED_SOURCES ${proto_c} ${proto_h})
endforeach()

# --- Build the GUI (Cairo + selected platform backend) ---
pkg_check_modules(CAIRO REQUIRED cairo)
# JACK is required: miniaudio is initialized with the JACK backend explicitly
# (no auto-fallback). On Arch this is satisfied by pipewire-jack.
pkg_check_modules(JACK REQUIRED jack)

add_executable(warptempo_gui
    src/gui/main.cpp
    src/gui/app_state.cpp
    src/gui/async_renderer.cpp
    src/gui/waveform_worker.cpp
    src/gui/platform_wayland.cpp
    src/gui/audio.cpp
    src/gui/render.cpp
    src/gui/warpmarkers.cpp
    src/gui/text_editor.cpp
    src/gui/text_display.cpp
    src/gui/phase_reset_markers.cpp
    src/gui/playback.cpp
    src/gui/playback_lifecycle.cpp
    src/gui/render_pipeline.cpp
    src/gui/timemap.cpp
    src/gui/timemap_view.cpp
    src/gui/viewport.cpp
    src/gui/selection.cpp
    src/gui/settings_io.cpp
    src/gui/engine_settings_io.cpp
    src/gui/undo.cpp
    src/gui/phase_reset_markers_ops.cpp
    src/gui/phase_reset_propagate.cpp
    src/gui/prompt.cpp
    src/gui/warpmarkers_ops.cpp
    src/gui/file_loader.cpp
    src/gui/flag_editor.cpp
    src/gui/render_view.cpp
    src/gui/save_ops.cpp
    src/gui/settings_editor.cpp
    src/gui/active_views.cpp
    src/gui/target_render.cpp
    src/gui/paint_handler.cpp
    src/gui/input_handler.cpp
    src/gui/miniaudio_impl.cpp
    src/engine/engine.cpp
    src/engine/limiter.cpp
    src/engine/peak_limiter.cpp
    src/engine/synthesis.cpp
    ${WAYLAND_GENERATED_SOURCES}
)
target_include_directories(warptempo_gui PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/engine
    ${CMAKE_SOURCE_DIR}/src/gui
    ${CMAKE_BINARY_DIR}/wayland-generated
    ${WAYLAND_CLIENT_INCLUDE_DIRS}
    ${WAYLAND_CURSOR_INCLUDE_DIRS}
    ${XKBCOMMON_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
    ${SNDFILE_INCLUDE_DIRS}
    ${FFTW3_INCLUDE_DIRS}
    ${JACK_INCLUDE_DIRS}
)
target_link_libraries(warptempo_gui PRIVATE
    ${WAYLAND_CLIENT_LIBRARIES}
    ${WAYLAND_CURSOR_LIBRARIES}
    ${XKBCOMMON_LIBRARIES}
    rt
    ${CAIRO_LIBRARIES}
    ${SNDFILE_LIBRARIES}
    ${FFTW3_THREADS_LIB}
    ${FFTW3_LIBRARIES}
    ${JACK_LIBRARIES}
    pthread
    dl
    m
)

# miniaudio is vendored third-party C. Suppress the warnings that its macro
# soup trips — only on its dedicated implementation TU, so our own code stays
# under -Wall -Wextra.
set_source_files_properties(src/gui/miniaudio_impl.cpp PROPERTIES
    COMPILE_FLAGS "-w")

# --- Compiler Optimizations ---
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(warptempo_gui PRIVATE -Wall -Wextra -O3 -march=native)
endif()
