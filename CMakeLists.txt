cmake_minimum_required(VERSION 3.20)
project(warptempo_gui VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build configuration. The two static archives (warptempo_parser,
# warptempo_engine) always build and need only sndfile / fftw3. The GUI and
# its Wayland/Cairo/JACK toolchain are optional, so the libraries can be built
# on a headless host that has none of the GUI platform packages installed.
# Turning this OFF skips all GUI dependency discovery and the warptempo_gui
# executable while still building the two static archives and the headless
# CLI executables.
option(WARPTEMPO_BUILD_GUI "Build the Cairo/Wayland GUI application" ON)

# --- Dependencies shared by the libraries (always required) ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE REQUIRED sndfile)
pkg_check_modules(FFTW3 REQUIRED fftw3)
find_library(FFTW3_THREADS_LIB fftw3_threads HINTS ${FFTW3_LIBRARY_DIRS} REQUIRED)

# ============================================================
#  warptempo_parser — map / file-parse core.
#  Depends on sndfile for the trimmed-WAV writer and warptempo_audio_io for
#  source reads. No fftw, no GUI, no engine. frame_map.h / time_format.h are
#  shared headers in src/.
# ============================================================
add_library(warptempo_parser_lib STATIC
    src/parser/warpmarkers_parse.cpp
    src/parser/phaseresetmarkers_parse.cpp
    src/parser/engine_settings_io.cpp
    src/parser/settings_trim.cpp
    src/parser/frame_map_build.cpp
    src/parser/source_audio_io.cpp
    src/parser/map_output.cpp
)
target_include_directories(warptempo_parser_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/parser
    PRIVATE
        ${SNDFILE_INCLUDE_DIRS}
)
set_target_properties(warptempo_parser_lib PROPERTIES OUTPUT_NAME warptempo_parser)
target_link_libraries(warptempo_parser_lib PUBLIC ${SNDFILE_LIBRARIES})

# ============================================================
#  warptempo_engine — PGHI DSP core.
#  Depends on sndfile + fftw3. Reads frame_map.h (header-only) from
#  src/; no link edge to the parser archive.
# ============================================================
add_library(warptempo_engine_lib STATIC
    src/engine/engine.cpp
    src/engine/limiter.cpp
    src/engine/peak_limiter.cpp
    src/engine/synthesis.cpp
)
target_include_directories(warptempo_engine_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/engine
    PRIVATE
        ${SNDFILE_INCLUDE_DIRS}
        ${FFTW3_INCLUDE_DIRS}
)
set_target_properties(warptempo_engine_lib PROPERTIES OUTPUT_NAME warptempo_engine)
target_link_libraries(warptempo_engine_lib PUBLIC
    ${SNDFILE_LIBRARIES}
    ${FFTW3_THREADS_LIB}
    ${FFTW3_LIBRARIES}
    pthread
)

# ============================================================
#  warptempo_audio_io — in-tree audio container/codec boundary.
#  No external link dependencies; miniaudio is vendored here and used by the
#  FLAC reader plus the GUI playback TU that includes its public header.
# ============================================================
add_library(warptempo_audio_io_lib STATIC
    src/audio_io/miniaudio_impl.cpp
    src/audio_io/pcm24.cpp
    src/audio_io/wav_io.cpp
    src/audio_io/flac_io.cpp
    src/audio_io/audio_probe.cpp
    src/audio_io/audio_reader.cpp
)
target_include_directories(warptempo_audio_io_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/src/audio_io
)
set_target_properties(warptempo_audio_io_lib PROPERTIES OUTPUT_NAME warptempo_audio_io)

# miniaudio is vendored third-party C. Suppress the warnings that its macro
# soup trips — only on its dedicated implementation TU, so our own code stays
# under -Wall -Wextra.
set_source_files_properties(src/audio_io/miniaudio_impl.cpp PROPERTIES
    COMPILE_FLAGS "-w")

add_executable(warptempo_audio_io_test
    src/audio_io/test_main.cpp
)
target_include_directories(warptempo_audio_io_test PRIVATE
    ${SNDFILE_INCLUDE_DIRS}
)
# Temporary libsndfile linkage: the comparison modes measure the in-tree codec
# against the incumbent reader/writer while both are still available.
target_link_libraries(warptempo_audio_io_test PRIVATE
    warptempo_audio_io_lib
    ${SNDFILE_LIBRARIES}
)

target_link_libraries(warptempo_parser_lib PUBLIC warptempo_audio_io_lib)

# ============================================================
#  warptempo_gui — Cairo/Wayland front end (optional).
#  All GUI platform dependency discovery lives inside this guard so a
#  libraries-only build needs none of these packages present.
# ============================================================
if(WARPTEMPO_BUILD_GUI)
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
        src/gui/phaseresetmarkers.cpp
        src/gui/playback.cpp
        src/gui/playback_lifecycle.cpp
        src/gui/render_pipeline.cpp
        src/gui/frame_map_view.cpp
        src/gui/viewport.cpp
        src/gui/selection.cpp
        src/gui/settings_io.cpp
        src/gui/undo.cpp
        src/gui/phaseresetmarkers_ops.cpp
        src/gui/phase_reset_propagate.cpp
        src/gui/prompt.cpp
        src/gui/warpmarkers_ops.cpp
        src/gui/marker_drag.cpp
        src/gui/file_loader.cpp
        src/gui/flag_editor.cpp
        src/gui/render_view.cpp
        src/gui/save_ops.cpp
        src/gui/settings_editor.cpp
        src/gui/active_views.cpp
        src/gui/target_render.cpp
        src/gui/render_cache.cpp
        src/gui/source_sample_cache.cpp
        src/gui/paint_handler.cpp
        src/gui/waveform_cache.cpp
        src/gui/input_handler.cpp
        src/gui/input_trim.cpp
        src/gui/input_render_view.cpp
        src/gui/input_render_dispatch.cpp
        src/gui/input_pointer.cpp
        src/gui/input_key_dispatch.cpp
        ${WAYLAND_GENERATED_SOURCES}
    )
    target_include_directories(warptempo_gui PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/parser
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/audio_io
        ${CMAKE_SOURCE_DIR}/src/gui
        ${CMAKE_BINARY_DIR}/wayland-generated
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${WAYLAND_CURSOR_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${SNDFILE_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
    )
    target_link_libraries(warptempo_gui PRIVATE
        warptempo_parser_lib
        warptempo_engine_lib
        warptempo_audio_io_lib
        ${WAYLAND_CLIENT_LIBRARIES}
        ${WAYLAND_CURSOR_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
        rt
        ${CAIRO_LIBRARIES}
        ${SNDFILE_LIBRARIES}
        ${JACK_LIBRARIES}
        pthread
        dl
        m
    )
endif()

# ============================================================
#  warptempo_parser — headless map emitter (parser-only).
#  Reads <source-stem>.{warpmarkers,settings} and writes the framemap /
#  tempomap artifact the GUI would for the same project. No engine, no audio.
# ============================================================
option(WARPTEMPO_BUILD_PARSER
       "Build the standalone warptempo_parser binary (the parser library always builds)" OFF)
if(WARPTEMPO_BUILD_PARSER)
    add_executable(warptempo_parser src/cli/parser_main.cpp)
    target_link_libraries(warptempo_parser PRIVATE
        warptempo_parser_lib
        warptempo_audio_io_lib
    )
endif()

# ============================================================
#  warptempo_cli — headless wav render CLI (parser + engine).
#  Reads <source-stem>.{warpmarkers,phaseresetmarkers,settings} and writes the
#  warped wav the GUI would render for the same project. Runs the full PGHI
#  engine; output_format must be wav (warptempo_parser covers framemap/tempomap).
# ============================================================
option(WARPTEMPO_BUILD_CLI "Build the headless warptempo_cli render binary" OFF)
if(WARPTEMPO_BUILD_CLI)
    add_executable(warptempo_cli src/cli/cli_main.cpp)
    target_link_libraries(warptempo_cli PRIVATE
        warptempo_parser_lib
        warptempo_engine_lib
        warptempo_audio_io_lib
    )
endif()

# ============================================================
#  warptempo_engine — engine-only synthesis driver.
#  Reads a prebuilt framemap [+ resetmap] + source wav and writes the warped
#  wav by running the PGHI engine directly. Links libwarptempo_engine ALONE —
#  no parser archive: the map readers are header-only (frame_map.h) and the
#  canonical phase-reset displacement is inlined. Trim is not an engine concern.
# ============================================================
option(WARPTEMPO_BUILD_ENGINE
       "Build the standalone warptempo_engine binary (the engine library always builds)" OFF)
if(WARPTEMPO_BUILD_ENGINE)
    add_executable(warptempo_engine src/cli/engine_main.cpp)
    target_link_libraries(warptempo_engine PRIVATE
        warptempo_engine_lib
        warptempo_audio_io_lib
    )
endif()

# --- Compiler Optimizations ---
# Applied identically to every target that exists in this configuration. The
# engine's PGHI hot path and the parser's frame-map math must compile under the
# same -O3 -march=native the monolith used. warptempo_gui is appended only when
# the GUI is being built.
set(WARPTEMPO_OPT_TARGETS
    warptempo_parser_lib
    warptempo_engine_lib
    warptempo_audio_io_lib
    warptempo_audio_io_test
)
if(WARPTEMPO_BUILD_GUI)
    list(APPEND WARPTEMPO_OPT_TARGETS warptempo_gui)
endif()
if(WARPTEMPO_BUILD_PARSER)
    list(APPEND WARPTEMPO_OPT_TARGETS warptempo_parser)
endif()
if(WARPTEMPO_BUILD_CLI)
    list(APPEND WARPTEMPO_OPT_TARGETS warptempo_cli)
endif()
if(WARPTEMPO_BUILD_ENGINE)
    list(APPEND WARPTEMPO_OPT_TARGETS warptempo_engine)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(tgt ${WARPTEMPO_OPT_TARGETS})
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -O3 -march=native)
    endforeach()
endif()
