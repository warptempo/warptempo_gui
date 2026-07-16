cmake_minimum_required(VERSION 3.20)
project(warptempo_gui VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build configuration. The GUI is the canonical product and builds by default;
# warptempo_cli is the headless insurance render path and is opt-in. The GUI's
# Wayland/Cairo/JACK toolchain is optional, so a headless host with none of the
# GUI platform packages can still configure and build warptempo_cli. Turning
# WARPTEMPO_BUILD_GUI OFF skips all GUI dependency discovery and the
# warptempo_gui executable.
option(WARPTEMPO_BUILD_GUI "Build the Cairo/Wayland GUI application" ON)
option(WARPTEMPO_BUILD_CLI "Build the headless warptempo_cli render binary" OFF)

# --- FFTW: needed by the engine sources, so required unconditionally. The GUI
# default compiles the engine sources, and a headless WARPTEMPO_BUILD_CLI build
# does too; keeping discovery top-level lets both configurations find it. ---
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFTW3 REQUIRED fftw3)
find_library(FFTW3_THREADS_LIB fftw3_threads HINTS ${FFTW3_LIBRARY_DIRS} REQUIRED)

# --- Static DSP reference (render-byte reproducibility) ------------------------
# The render-byte reference surface is exactly three libraries: libm (analysis
# window, atan2), libmvec (the vectorized synthesis trig), and fftw (every FFT).
# Dynamically linked, a routine system update to any of them changes the bytes an
# UNTOUCHED binary produces. When ON, both products pin fftw as static archives;
# the CLI additionally links glibc's static libmvec/libm and links -static,
# making it the fully self-contained archival reference renderer whose render
# bytes are a property of the built binary alone, immune to system library
# updates until a deliberate rebuild. The GUI cannot go fully static: glibc
# cannot statically link libm/libmvec into a PIE, because their ifunc objects
# reference the loader-private _dl_x86_cpu_features symbol, so the GUI pins fftw
# only and keeps its glibc math dynamic (-lm pulls libmvec via glibc's linker
# script exactly as in a default build). Within one glibc epoch the GUI and CLI
# render bytes are identical — glibc's libm.a/libmvec.a are numerically identical
# to their shared counterparts (verified) — and a glibc update drifts the GUI's
# output ulp-class until a deliberate rebuild while the fully static CLI holds.
# Everything else the products link (wayland, cairo, jack, libstdc++) never
# touches render bytes and stays dynamic. OFF by default so package-manager-only
# (distro/portable) builds work everywhere; docs/HELP.md's Reproducibility
# section carries the user-facing story.
option(WARPTEMPO_STATIC_DSP
    "Link fftw statically (GUI+CLI) and the CLI fully static to pin render bytes" OFF)
set(WARPTEMPO_FFTW_STATIC_PREFIX "$ENV{HOME}/.warptempo/fftw-3.3.11-static"
    CACHE PATH "Prefix of the static fftw build used by WARPTEMPO_STATIC_DSP")

if(WARPTEMPO_STATIC_DSP)
    # Both products consume these two fftw archives (threads before core) where
    # they consumed the pkg-config fftw libs. Pair the static build's own headers
    # with its archives for a self-consistent header/lib match.
    set(WARPTEMPO_DSP_LIBS
        ${WARPTEMPO_FFTW_STATIC_PREFIX}/lib/libfftw3_threads.a
        ${WARPTEMPO_FFTW_STATIC_PREFIX}/lib/libfftw3.a)
    set(WARPTEMPO_FFTW_INCLUDE_DIRS ${WARPTEMPO_FFTW_STATIC_PREFIX}/include)
    # The CLI additionally links glibc's static math archives (libmvec before
    # libm — libmvec's scalar fallbacks call libm); the GUI links neither, its
    # glibc math staying dynamic under the PIE constraint above.
    if(WARPTEMPO_BUILD_CLI)
        set(WARPTEMPO_CLI_STATIC_MATH_LIBS /usr/lib/libmvec.a /usr/lib/libm.a)
    else()
        set(WARPTEMPO_CLI_STATIC_MATH_LIBS "")
    endif()
    # Existence check: the two fftw archives are needed by every enabled target;
    # the glibc math archives only when the CLI target is built.
    foreach(dsp_archive ${WARPTEMPO_DSP_LIBS} ${WARPTEMPO_CLI_STATIC_MATH_LIBS})
        if(NOT EXISTS ${dsp_archive})
            message(FATAL_ERROR
                "WARPTEMPO_STATIC_DSP: missing static archive ${dsp_archive}. "
                "Build the static fftw once into ${WARPTEMPO_FFTW_STATIC_PREFIX} "
                "(fftw 3.3.11, --enable-sse2 --enable-avx, generic "
                "-march=x86-64 -O2) and ensure glibc's libm.a/libmvec.a are "
                "present, or configure with -DWARPTEMPO_STATIC_DSP=OFF.")
        endif()
    endforeach()
else()
    # Dynamic (default): the libs and includes exactly as pkg-config discovers
    # them — behavior-identical to a build with no static-DSP switch at all.
    set(WARPTEMPO_DSP_LIBS ${FFTW3_THREADS_LIB} ${FFTW3_LIBRARIES})
    set(WARPTEMPO_FFTW_INCLUDE_DIRS ${FFTW3_INCLUDE_DIRS})
    set(WARPTEMPO_CLI_STATIC_MATH_LIBS "")
endif()

# ============================================================
#  Per-directory source lists.
#  Each binary compiles these .cpp lists directly rather than linking a shared
#  archive. The duplicated object code across binaries is accepted deliberately:
#  a single source text per list makes drift between products impossible, which
#  is what matters — not the packaging. The GUI is canonical and warptempo_cli
#  is the insurance render path; both draw the same four lists.
#
#  audio_io: in-tree audio container boundary (WAV-only: probing, reading,
#  and writing all in-tree, no third-party codec).
#  parser: map / file-parse core (no fftw, no GUI, no engine; the shared headers
#  warp_frame_map.h and time_format.h live in src/).
#  engine: PGHI DSP core (needs fftw; reads warp_frame_map.h header-only, no
#  parser dependency). Buffer-out only: analysis, PGHI, synthesis, spectral
#  limiter, map-extent emission.
#  prepost: the pre-/post-engine stages both render orchestrators share — the
#  trimmer (plan_trim / apply_post_trim), the lifted peak limiter, and the
#  post-engine chain (crop -> peak -> encode). Calls audio_io's writers.
# ============================================================
set(WARPTEMPO_AUDIO_IO_SOURCES
    src/audio_io/pcm24.cpp
    src/audio_io/wav_io.cpp
    src/audio_io/audio_probe.cpp
    src/audio_io/audio_reader.cpp
)
set(WARPTEMPO_PARSER_SOURCES
    src/parser/warpmarkers_parse.cpp
    src/parser/phaseresetmarkers_parse.cpp
    src/parser/engine_settings_io.cpp
    src/parser/settings_file.cpp
    src/parser/warp_frame_map_build.cpp
    src/parser/phase_reset_frame_map_build.cpp
    src/parser/marker_store_validate.cpp
    src/parser/source_audio_io.cpp
    src/parser/map_output.cpp
    src/parser/render_output_naming.cpp
)
set(WARPTEMPO_ENGINE_SOURCES
    src/engine/engine.cpp
    src/engine/limiter.cpp
    src/engine/synthesis.cpp
    src/engine/synth_spectrum_trig.cpp
)
# Scoped fast-math: unlocks GCC's libmvec vectorization of the synthesis trig in
# this ONE TU only (appended after the global -O3 -march=native below). A source
# property in this directory scope binds every target that compiles the file, so
# both warptempo_gui and warptempo_cli pick it up. Must not spread to any other
# source — the rest of the engine keeps strict FP semantics. This TU is the
# tree's one fast-math boundary and must stay a normal non-LTO object even under
# an LTO build, so whole-program optimization can never inline or re-form it
# across FP-option domains (-fno-lto is inert in the default non-LTO build).
# -ffast-math must also stay a COMPILE option: on the GCC LINK line it would add
# crtfastmath.o, which sets FTZ/DAZ process-wide at startup — flushing denormals
# in every stage, a silent global reference change, not a scoped one.
set_source_files_properties(src/engine/synth_spectrum_trig.cpp
    PROPERTIES COMPILE_OPTIONS "-ffast-math;-fno-lto")
set(WARPTEMPO_PREPOST_SOURCES
    src/prepost/trimmer.cpp
    src/prepost/peak_limiter.cpp
)

# ============================================================
#  warptempo_gui — Cairo/Wayland front end (canonical product, default ON).
#  All GUI platform dependency discovery lives inside this guard so a
#  headless build needs none of these packages present.
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
    # Playback is a direct libjack client. JACK is required by policy; under
    # PipeWire this is satisfied by pipewire-jack.
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
        src/gui/warp_frame_map_view.cpp
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
        src/gui/renders_dir.cpp
        src/gui/save_ops.cpp
        src/gui/settings_editor.cpp
        src/gui/active_views.cpp
        src/gui/target_render.cpp
        src/gui/render_cache.cpp
        src/gui/paint_handler.cpp
        src/gui/waveform_cache.cpp
        src/gui/input_handler.cpp
        src/gui/input_trim.cpp
        src/gui/input_render_dispatch.cpp
        src/gui/input_pointer.cpp
        src/gui/input_key_dispatch.cpp
        ${WARPTEMPO_AUDIO_IO_SOURCES}
        ${WARPTEMPO_PARSER_SOURCES}
        ${WARPTEMPO_ENGINE_SOURCES}
        ${WARPTEMPO_PREPOST_SOURCES}
        ${WAYLAND_GENERATED_SOURCES}
    )
    target_include_directories(warptempo_gui PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/parser
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/audio_io
        ${CMAKE_SOURCE_DIR}/src/prepost
        ${CMAKE_SOURCE_DIR}/src/gui
        ${CMAKE_BINARY_DIR}/wayland-generated
        ${WARPTEMPO_FFTW_INCLUDE_DIRS}
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${WAYLAND_CURSOR_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
    )
    target_link_libraries(warptempo_gui PRIVATE
        ${WARPTEMPO_DSP_LIBS}
        ${WAYLAND_CLIENT_LIBRARIES}
        ${WAYLAND_CURSOR_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
        rt
        ${CAIRO_LIBRARIES}
        ${JACK_LIBRARIES}
        pthread
        dl
        m
    )
endif()

# ============================================================
#  warptempo_cli — headless wav render binary (insurance render path).
#  Reads <source-stem>.{warpmarkers,phaseresetmarkers,settings} and writes the
#  warped wav the GUI would render for the same project. Runs the full PGHI
#  engine; renders are wav-only. Compiles the parser and engine sources
#  directly, needing no Wayland/Cairo/JACK toolchain.
# ============================================================
if(WARPTEMPO_BUILD_CLI)
    add_executable(warptempo_cli
        src/cli/cli_main.cpp
        ${WARPTEMPO_AUDIO_IO_SOURCES}
        ${WARPTEMPO_PARSER_SOURCES}
        ${WARPTEMPO_ENGINE_SOURCES}
        ${WARPTEMPO_PREPOST_SOURCES}
    )
    target_include_directories(warptempo_cli PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/parser
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/audio_io
        ${CMAKE_SOURCE_DIR}/src/prepost
        ${WARPTEMPO_FFTW_INCLUDE_DIRS}
    )
    # The static-math archives sit right after the fftw archives (empty unless
    # WARPTEMPO_STATIC_DSP is ON). Under WARPTEMPO_STATIC_DSP the CLI is the fully
    # static archival reference renderer, so it also links -static; the GUI never
    # does (a PIE cannot statically link glibc's libm/libmvec — see the option
    # comment above).
    target_link_libraries(warptempo_cli PRIVATE
        ${WARPTEMPO_DSP_LIBS}
        ${WARPTEMPO_CLI_STATIC_MATH_LIBS}
        pthread
    )
    if(WARPTEMPO_STATIC_DSP)
        target_link_options(warptempo_cli PRIVATE -static)
    endif()
endif()

# --- Compiler Optimizations ---
# Applied identically to every target that exists in this configuration. The
# engine's PGHI hot path and the parser's frame-map math must compile under the
# same -O3 -march=native everywhere. Each target is appended only under its own
# guard.
set(WARPTEMPO_OPT_TARGETS "")
if(WARPTEMPO_BUILD_GUI)
    list(APPEND WARPTEMPO_OPT_TARGETS warptempo_gui)
endif()
if(WARPTEMPO_BUILD_CLI)
    list(APPEND WARPTEMPO_OPT_TARGETS warptempo_cli)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(tgt ${WARPTEMPO_OPT_TARGETS})
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -O3 -march=native)
    endforeach()
endif()
