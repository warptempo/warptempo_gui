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
#  parser: map / file-parse core (no GUI, no engine; the shared headers
#  warp_frame_map.h and time_format.h live in src/). No FFT use and no fftw
#  dependency of its own; both products link fftw for the engine regardless.
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
# -ffp-contract=off is repeated here after -ffast-math so this TU's ban on FMA
# contraction is explicit and order-proof: the global flag below already carries
# it, and GCC's -ffast-math does not itself enable contraction, but the intent
# must not rest on either fact.
set_source_files_properties(src/engine/synth_spectrum_trig.cpp
    PROPERTIES COMPILE_OPTIONS "-ffast-math;-ffp-contract=off;-fno-lto")
set(WARPTEMPO_PREPOST_SOURCES
    src/prepost/trimmer.cpp
    src/prepost/peak_limiter.cpp
)

# ============================================================
#  THE GUI'S SOURCE LIST, less the four per-directory lists and less the
#  three files that have one arm per backend. Both GUI targets below draw it,
#  which is what keeps the two builds from drifting into two rosters: a new
#  GUI source is added HERE and both platforms get it.
#
#  THE THREE PER-BACKEND ARMS, each with exactly one arm linked into a given
#  binary and each named at its own site: the platform backend
#  (platform_wayland.cpp / platform_android.cpp, chosen for the consumer by
#  platform.h), the face owner (gui_font_fontconfig.cpp /
#  gui_font_bundled.cpp, gui_font.h) and the playback DEVICE (playback.cpp,
#  the JACK client / playback_aaudio.cpp, the AAudio stream) — the device
#  half alone, the engine both drive being playback_common.cpp in the
#  shared list.
#  main.cpp is in the list on BOTH: its Linux main() is #ifndef __ANDROID__
#  and the body it wraps, gui_main, is the one entry both platforms call.
# ============================================================
set(WARPTEMPO_GUI_SOURCES
    src/gui/main.cpp
    src/gui/app_state.cpp
    src/gui/async_renderer.cpp
    src/gui/waveform_worker.cpp
    src/gui/input_core.cpp
    src/gui/audio.cpp
    # GUI-ONLY: HarfBuzz text shaping is a painting concern and exists only
    # here, so it is deliberately in none of the four shared per-directory
    # source lists — warptempo_cli must never carry it, nor the
    # harfbuzz/cairo-ft link it implies.
    src/gui/text_shape.cpp
    # GUI-ONLY: the in-tree Breeze icon path renderer (an SVG d-string
    # subset over cairo paths) is a painting concern and exists only here,
    # so like text_shape.cpp it is deliberately in none of the four shared
    # per-directory source lists — warptempo_cli must never carry it. It
    # needs no dependency cairo does not already bring.
    src/gui/icons.cpp
    src/gui/render.cpp
    src/gui/warpmarkers.cpp
    src/gui/text_editor.cpp
    src/gui/phaseresetmarkers.cpp
    src/gui/playback_lifecycle.cpp
    # THE PORTABLE HALF of the playback engine (the render body, the
    # predictor, the domain/bind/speed logic). Shared: each backend arm
    # below carries only its device mechanics over this one engine.
    src/gui/playback_common.cpp
    src/gui/render_pipeline.cpp
    src/gui/warp_frame_map_view.cpp
    src/gui/viewport.cpp
    src/gui/selection.cpp
    src/gui/settings_io.cpp
    # THE PER-DEVICE PREFERENCES FILE (gui_scale + audio_player, 2026-08-27).
    # GUI-ONLY by construction: warptempo_cli never read either key, and the
    # file describes the panel in front of the user rather than the piece.
    src/gui/device_config.cpp
    src/gui/project_model.cpp
    src/gui/undo.cpp
    src/gui/phaseresetmarkers_ops.cpp
    src/gui/phase_reset_propagate.cpp
    src/gui/prompt.cpp
    src/gui/warpmarkers_ops.cpp
    src/gui/position_nudge.cpp
    src/gui/marker_drag.cpp
    src/gui/file_loader.cpp
    src/gui/flag_editor.cpp
    src/gui/renders_dir.cpp
    src/gui/save_ops.cpp
    # GUI-ONLY: Synchronize to external storage — the mirror onto the one
    # mounted removable volume and the thread that writes it. warptempo_cli
    # renders and never publishes, so like the history unit above it is
    # deliberately in none of the four shared per-directory source lists. It
    # needs nothing beyond the C++ standard library and POSIX.
    src/gui/external_sync.cpp
    # GUI-ONLY: the GitHub recheck's history model reads the repo's own
    # projects/ corpus through git subprocesses AND owns the product's one
    # mutating git route (the commit act's add/commit/push, behind a separate
    # entry point in the same file). Like text_shape.cpp and icons.cpp it is
    # deliberately in none of the four shared per-directory source lists —
    # warptempo_cli must never carry it. It needs no dependency beyond the C++
    # standard library and POSIX.
    src/gui/history_diff.cpp
    # GUI-ONLY, and for history_diff.cpp's reason: it is the thread that
    # runs that file's one mutating git route off the GUI thread.
    src/gui/history_commit_worker.cpp
    src/gui/history_prefetch.cpp
    src/gui/settings_editor.cpp
    src/gui/active_views.cpp
    src/gui/ab_audition.cpp
    src/gui/target_render.cpp
    src/gui/render_cache.cpp
    src/gui/paint_handler.cpp
    src/gui/waveform_cache.cpp
    src/gui/input_handler.cpp
    src/gui/input_trim.cpp
    src/gui/input_render_dispatch.cpp
    src/gui/input_pointer.cpp
    src/gui/input_key_dispatch.cpp
)

# ============================================================
#  warptempo_gui — the ANDROID library (arm64 NativeActivity, cross-built from
#  the laptop through android/app/configure.sh). It is a SHARED LIBRARY rather
#  than an executable because a NativeActivity's process is started by the
#  system and dlopen's this .so; android_main (platform_android.cpp) is its
#  entry point and calls the same gui_main the Linux main() does.
#
#  NO WAYLAND, NO XKB, NO JACK, NO wayland-scanner is discovered here — none
#  of them exists on the platform. The four dependencies that DO cross are
#  found through the M1 sysroot's own .pc files: android/app/configure.sh
#  passes PKG_CONFIG_EXECUTABLE (the toolchain's wrapper, which hard-forces
#  PKG_CONFIG_LIBDIR at android/prebuilt/<abi>) and FFTW3_THREADS_LIB (whose
#  find_library above cannot search a sysroot the NDK toolchain file has
#  re-rooted), so this branch itself names no path.
#
#  THE LINUX TARGET IS UNTOUCHED BY EVERYTHING IN HERE. The two branches are
#  mutually exclusive on ANDROID, they share the source list and the flag
#  block at the bottom of this file, and neither adds a compile option the
#  other does not.
# ============================================================
if(WARPTEMPO_BUILD_GUI AND ANDROID)
    # Cairo needs its FT backend (there is no fontconfig on the platform, so
    # gui_font_bundled.cpp builds faces from the APK's own font assets) and
    # HarfBuzz shapes on those faces, exactly as on Linux. freetype2 gets its
    # own check here — unlike the Linux branch, which lets cairo-ft and
    # harfbuzz carry it transitively — because the static link needs its
    # archive named explicitly inside the group below.
    pkg_check_modules(WT_ANDROID_GFX REQUIRED cairo cairo-ft harfbuzz freetype2)

    # The NDK's stock android_native_app_glue. It is COMPILED IN PLACE from
    # the NDK rather than vendored: r29's process_input already drains its
    # queue with a while loop, so there is no fork to maintain.
    set(WARPTEMPO_GLUE_DIR ${CMAKE_ANDROID_NDK}/sources/android/native_app_glue)
    if(NOT EXISTS ${WARPTEMPO_GLUE_DIR}/android_native_app_glue.c)
        message(FATAL_ERROR
            "native_app_glue not found under ${WARPTEMPO_GLUE_DIR}")
    endif()
    # The glue is THIRD-PARTY SOURCE this project does not own and must not
    # edit, and it does not compile clean under this project's -Wall -Wextra
    # (two unused `source` parameters in its own poll handlers). The warning is
    # silenced FOR THAT FILE ALONE so the product's own warnings stay the only
    # ones in the log — the same reason -Wfloat-conversion is scoped away from
    # the generated Wayland protocol C on the other target.
    set_source_files_properties(${WARPTEMPO_GLUE_DIR}/android_native_app_glue.c
        PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")

    add_library(warptempo_gui SHARED
        ${WARPTEMPO_GUI_SOURCES}
        src/gui/platform_android.cpp
        src/gui/gui_font_bundled.cpp
        src/gui/playback_aaudio.cpp
        ${WARPTEMPO_GLUE_DIR}/android_native_app_glue.c
        ${WARPTEMPO_AUDIO_IO_SOURCES}
        ${WARPTEMPO_PARSER_SOURCES}
        ${WARPTEMPO_ENGINE_SOURCES}
        ${WARPTEMPO_PREPOST_SOURCES}
    )
    target_include_directories(warptempo_gui PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/parser
        ${CMAKE_SOURCE_DIR}/src/engine
        ${CMAKE_SOURCE_DIR}/src/audio_io
        ${CMAKE_SOURCE_DIR}/src/prepost
        ${CMAKE_SOURCE_DIR}/src/gui
        ${WARPTEMPO_GLUE_DIR}
        ${FFTW3_INCLUDE_DIRS}
        ${WT_ANDROID_GFX_INCLUDE_DIRS}
    )
    # -D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__ IS LOAD-BEARING and is
    # mentioned in no NDK document: without it bionic marks every symbol newer
    # than minSdk `strict`, and a strict-unavailable symbol is a hard BUILD
    # ERROR that no __builtin_available(android N, *) guard can open. With it
    # such a call becomes a weak reference the guard makes safe. Nothing in
    # this target reaches past minSdk 30 today; the define stays because the
    # first thing that does would otherwise fail with an error that reads like
    # a missing library.
    target_compile_definitions(warptempo_gui PRIVATE
        __ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__)
    # --start-group covers the freetype <-> harfbuzz static cycle (cairo pulls
    # freetype, freetype's autohinter reaches back into harfbuzz), which a
    # single-pass static link cannot resolve on its own. --no-undefined makes a
    # missing symbol a BUILD failure rather than something the device's loader
    # discovers at launch.
    target_link_libraries(warptempo_gui PRIVATE
        -Wl,--no-undefined
        -Wl,--start-group
        ${WT_ANDROID_GFX_STATIC_LDFLAGS}
        ${FFTW3_THREADS_LIB}
        ${FFTW3_LIBRARIES}
        -Wl,--end-group
        aaudio
        android
        # libnativewindow, and it is NOT covered by libandroid (2026-08-27).
        # Most of the ANativeWindow_* surface this backend uses — the geometry
        # setter, lock/unlockAndPost, the width and height queries — is
        # re-exported by libandroid, which is why this line was not needed
        # until the 90 Hz frame-rate pin at window adoption:
        # ANativeWindow_setFrameRate (API 30) lives in libnativewindow alone,
        # and --no-undefined turns its absence into the build failure it should
        # be rather than a launch-time surprise.
        nativewindow
        log
        m
        dl
    )
    target_link_directories(warptempo_gui PRIVATE ${FFTW3_LIBRARY_DIRS})
endif()

# ============================================================
#  warptempo_gui — Cairo/Wayland front end (canonical product, default ON).
#  All GUI platform dependency discovery lives inside this guard so a
#  headless build needs none of these packages present.
# ============================================================
if(WARPTEMPO_BUILD_GUI AND NOT ANDROID)
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
        ${WAYLAND_PROTOCOLS_DATADIR}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml
        ${WAYLAND_PROTOCOLS_DATADIR}/unstable/relative-pointer/relative-pointer-unstable-v1.xml
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
    # Proportional text shaping (src/gui/text_shape.cpp): HarfBuzz shapes on
    # the cairo scaled font's own FreeType face, which needs cairo's FT
    # backend headers. freetype2 gets no check of its own — both cairo-ft and
    # harfbuzz carry its include dir transitively — and hb-ft lives in
    # libharfbuzz itself, so there is no separate hb-ft module either.
    pkg_check_modules(HARFBUZZ REQUIRED harfbuzz)
    pkg_check_modules(CAIRO_FT REQUIRED cairo-ft)
    # Playback is a direct libjack client. JACK is required by policy; under
    # PipeWire this is satisfied by pipewire-jack.
    pkg_check_modules(JACK REQUIRED jack)

    add_executable(warptempo_gui
        ${WARPTEMPO_GUI_SOURCES}
        # The three per-backend arms, Linux's: the Wayland platform backend,
        # the fontconfig face owner (gui_font.h) and the JACK playback device.
        # The Android library above compiles platform_android.cpp,
        # gui_font_bundled.cpp and playback_aaudio.cpp in their places; exactly
        # one arm of each is ever linked.
        src/gui/platform_wayland.cpp
        src/gui/gui_font_fontconfig.cpp
        src/gui/playback.cpp
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
        ${FFTW3_INCLUDE_DIRS}
        ${WAYLAND_CLIENT_INCLUDE_DIRS}
        ${WAYLAND_CURSOR_INCLUDE_DIRS}
        ${XKBCOMMON_INCLUDE_DIRS}
        ${CAIRO_INCLUDE_DIRS}
        ${HARFBUZZ_INCLUDE_DIRS}
        ${CAIRO_FT_INCLUDE_DIRS}
        ${JACK_INCLUDE_DIRS}
    )
    target_link_libraries(warptempo_gui PRIVATE
        ${FFTW3_THREADS_LIB}
        ${FFTW3_LIBRARIES}
        ${WAYLAND_CLIENT_LIBRARIES}
        ${WAYLAND_CURSOR_LIBRARIES}
        ${XKBCOMMON_LIBRARIES}
        rt
        ${CAIRO_LIBRARIES}
        ${HARFBUZZ_LIBRARIES}
        ${CAIRO_FT_LIBRARIES}
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
        ${FFTW3_INCLUDE_DIRS}
    )
    target_link_libraries(warptempo_cli PRIVATE
        ${FFTW3_THREADS_LIB}
        ${FFTW3_LIBRARIES}
        pthread
    )
endif()

# --- Compiler Optimizations ---
# Applied identically to every target that exists in this configuration. The
# engine's PGHI hot path and the parser's frame-map math must compile under the
# same -O3 -march=native -ffp-contract=off everywhere. Each target is appended
# only under its own guard.
#
# -ffp-contract=off forbids FMA contraction, measured as the SOLE source of
# output divergence between this engine's two x86-64 GCC codegens: a contracting
# -march=native build differs from a non-contracting generic one in 14% of
# output samples, peaking at -87 dBFS, and with contraction off the two are
# byte-identical. It costs nothing measurable — the vectorization -march=native
# buys is worth ~5% and changes no output bit, while contraction is worth no
# measurable time at all. So -O3 and -march are pure speed knobs: renders are
# byte-reproducible across rebuilds and -march targets under an unchanged
# toolchain and unchanged math libraries. Any future port compiles with
# contraction off as well; clang contracts by default too. Measurement record
# and the claim's exact scope in docs/engineering/perf_campaign_2026_07.md §15.
#
# -march=native IS THE ONE HOST-ONLY FLAG here, and the only conditional one: it
# compiles for whatever CPU is doing the compiling, which a cross build has no
# answer for. An Android build therefore takes the NDK's own armv8-a default and
# adds nothing — the ABI the device already assumes. -O3 and -ffp-contract=off
# stay unconditional on every target and every platform, contraction being off
# by the reproducibility argument above rather than as a speed knob.
set(WARPTEMPO_ARCH_FLAGS -march=native)
if(ANDROID)
    set(WARPTEMPO_ARCH_FLAGS)
endif()

set(WARPTEMPO_OPT_TARGETS "")
if(WARPTEMPO_BUILD_GUI)
    list(APPEND WARPTEMPO_OPT_TARGETS warptempo_gui)
endif()
if(WARPTEMPO_BUILD_CLI)
    list(APPEND WARPTEMPO_OPT_TARGETS warptempo_cli)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(tgt ${WARPTEMPO_OPT_TARGETS})
        target_compile_options(${tgt} PRIVATE -Wall -Wextra -O3 ${WARPTEMPO_ARCH_FLAGS} -ffp-contract=off)
    endforeach()
    # -Wfloat-conversion is a STANDING TRIPWIRE on the GUI's C++ and must report
    # ZERO: every fractional->integer conversion in src/gui is either an
    # explicit static_cast on a std::nearbyint / llrint result (the rounding
    # rule; snap_authored_frame for authored positions) or an audited exact
    # conversion cast explicitly at its site, so a new warning is a rounding
    # off the rule to route through the chokepoint, not a cast to add. Scoped
    # to the GUI target alone and OUTSIDE the shared loop above: the CLI shares
    # only the frozen stages, which pass the flag but are not its subject; and
    # scoped to C++ alone because the GUI target also compiles the generated C
    # protocol stubs, which were never audited under it.
    if(WARPTEMPO_BUILD_GUI)
        target_compile_options(warptempo_gui PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wfloat-conversion>)
    endif()
endif()
