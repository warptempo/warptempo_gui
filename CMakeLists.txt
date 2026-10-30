cmake_minimum_required(VERSION 3.20)
project(warptempo_gui VERSION 2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# THE BUILD'S SHAPE (architect 2026-10-30: the laptop GUI dropped, the tablet
# the one platform, the GL canvas the one road): NO OPTIONS, ONE PRODUCT PER
# PLATFORM. Cross-built for Android (android/app/configure.sh, the `if(ANDROID)`
# branch below) the configuration's product is warptempo_gui, the tablet's
# NativeActivity library; on the laptop, a plain `cmake -B build -S .`, it is
# warptempo_cli, the headless render the laptop keeps for git pull and render,
# byte-identical to the GUI through the shared stages. The laptop is the APK's
# cross-build host besides, and builds no GUI.

# --- FFTW: needed by the engine sources, which both products compile, so
# discovered top-level for either configuration. ---
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
#  THE GUI'S SOURCE LIST, less the four per-directory lists and the NDK's
#  native_app_glue: the Android library below, the GUI's one target since the
#  laptop GUI's drop (architect 2026-10-30), compiles it. A new GUI source is
#  added HERE.
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
    # GUI-ONLY, like text_shape.cpp: the one face owner (gui_font.h), which
    # builds the product's faces (gui_font.h's kGuiFontFiles: Liberation
    # Sans and its bold) through FreeType from the APK's font assets.
    src/gui/gui_font_bundled.cpp
    # GUI-ONLY: the icons (icons.cpp: the load, the raster cache, the
    # draws) and the wrapper over resvg it reads the bundled drawings through
    # (svg_icon.cpp, the one file that includes <resvg.h>; it also decodes
    # the materials' pictures) are a painting
    # concern and exist only here, so like text_shape.cpp they are
    # deliberately in none of the four shared per-directory source lists —
    # warptempo_cli must never carry them, nor resvg. resvg is the
    # cross-built static one (android/deps/85_resvg.sh), found in the Android
    # branch below.
    src/gui/icons.cpp
    src/gui/svg_icon.cpp
    # GUI-ONLY, for svg_icon.cpp's reason: THE HI-FI SKIN'S GROUNDS
    # (architect 2026-10-23, materials.h) — the procedural brushed panel and
    # marker strip, or a picture dropped beside the config decoded and scaled
    # through svg_icon's road, baked once per window geometry and painted as
    # the chrome's surfaces.
    src/gui/materials.cpp
    src/gui/render.cpp
    # THE PROGRAM'S PAINTERS (2026-10-09): Cool Edit's band, row 8 and its
    # case, the same under every chrome (cool_edit_paint.h).
    src/gui/cool_edit_paint.cpp
    # THE CANVAS SINK (2026-10-13): the canvas interior's one paint road, the
    # frame's draw list (canvas_sink.h).
    src/gui/canvas_sink.cpp
    # THE GL CANVAS (2026-10-13; the one canvas road since the laptop GUI's
    # drop, architect 2026-10-30): its owner GuiGlCanvas (gl_canvas.h) — the
    # programs, the textures, the upload walk and the frame's draw — over
    # EGL / GLES 3.0, the two libraries on the Android branch's link line.
    src/gui/gl_canvas.cpp
    src/gui/warpmarkers.cpp
    src/gui/text_editor.cpp
    src/gui/phaseresetmarkers.cpp
    src/gui/playback_lifecycle.cpp
    # THE PORTABLE HALF of the playback engine (the render body, the
    # predictor, the domain/bind/speed logic); the AAudio device,
    # playback_aaudio.cpp, carries only its device mechanics over this one
    # engine (playback.h the contract).
    src/gui/playback_common.cpp
    src/gui/playback_aaudio.cpp
    src/gui/render_pipeline.cpp
    src/gui/warp_frame_map_view.cpp
    src/gui/viewport.cpp
    src/gui/selection.cpp
    src/gui/settings_io.cpp
    # THE PER-DEVICE PREFERENCES FILE (2026-08-27; its keys are
    # kDeviceConfigKeys in device_config.cpp, the one list). GUI-ONLY
    # by construction: warptempo_cli never read any of them, and the file
    # describes the panel in front of the user rather than the piece.
    src/gui/device_config.cpp
    # THE CHROME'S COLORS (2026-10-04; compiled in since 2026-10-08): the
    # role table and each chrome's compiled theme, no files.
    # GUI-ONLY like the config: the CLI paints nothing.
    src/gui/theme_file.cpp
    # THE WAVEFORM PRESETS (2026-10-07; compiled in alone since 2026-10-14;
    # the caption schemes gone since 2026-10-21): the program's role table
    # (eleven roles since 2026-10-11), the built-in palettes (the program's
    # own twenty-five, the one default `warptempo-phosphor-cyan-and-sky`),
    # and the lookups
    # the device config and the Settings Waveform row read. GUI-ONLY.
    src/gui/palette_file.cpp
    src/gui/project_model.cpp
    src/gui/undo.cpp
    src/gui/phaseresetmarkers_ops.cpp
    src/gui/phase_reset_propagate.cpp
    # THE WAVEFORM GAIN (2026-09-23): the pure derivation of the picture's
    # continuous magnification from the source, in its own translation unit.
    src/gui/waveform_gain.cpp
    src/gui/prompt.cpp
    src/gui/warpmarkers_ops.cpp
    src/gui/position_nudge.cpp
    src/gui/marker_drag.cpp
    # THE FLAG'S VALUE DRAG (2026-09-10): the marker drag's sibling — the same
    # plain flag press-and-drag, turned vertical by the Value Drag lamp — in
    # its own translation unit for marker_drag.cpp's reason, that it is a
    # cross-kind gesture the per-column authoring clusters do not own.
    src/gui/value_drag.cpp
    src/gui/file_loader.cpp
    src/gui/flag_editor.cpp
    src/gui/renders_dir.cpp
    src/gui/save_ops.cpp
    # GUI-ONLY: the GitHub recheck's history model reads the projects
    # repository's corpus AND owns the product's three mutating git routes
    # (the checkpoint act, the GitHub check's fetch, the pull). Like text_shape.cpp and icons.cpp it is
    # deliberately in none of the four shared per-directory source lists —
    # warptempo_cli must never carry it. Its git questions go through
    # git_repo.cpp below.
    src/gui/history_diff.cpp
    # THE ONE GIT ROAD (architect 2026-09-27): libgit2 in process, the history
    # module's only way into a repository, with the fetch's and the push's
    # deploy key and host-key pin. The one file that includes <git2.h>, so the one file that
    # needs libgit2 — the cross-built static one (android/deps/80_libgit2.sh),
    # found in the Android branch below; the APK always carries it.
    src/gui/git_repo.cpp
    # GUI-ONLY, and for history_diff.cpp's reason: it is the thread that
    # runs that file's network git routes (the checkpoint act, the GitHub
    # check) off the GUI thread. warptempo_cli reads no history of any kind.
    src/gui/history_commit_worker.cpp
    src/gui/history_prefetch.cpp
    src/gui/settings_editor.cpp
    src/gui/active_views.cpp
    src/gui/ab_audition.cpp
    # THE NOTIFICATION CARDS (2026-08-29): the stack owner, its clock on the
    # run loop's tick and its hit geometry. GUI-only like every painting
    # concern.
    src/gui/notifications.cpp
    # THE RENDER PLAYER (2026-08-28): the in-app player over the project's
    # output folders, on the one playback engine. GUI-ONLY like every
    # painting and playback concern; it calls the audio_io WAV reader.
    src/gui/render_player.cpp
    # THE SCORE (2026-10-26): the score folder's table and its reader, the
    # score viewer's own file (score/viewer.txt, the five keys), the
    # soundtrack's reader and the page decoder (two worker threads, the
    # 2026-10-26 night player), and the built-in page viewer over them.
    # GUI-only: the CLI reads no score.
    src/gui/score_pages.cpp
    src/gui/score_viewer_file.cpp
    src/gui/score_audio.cpp
    src/gui/score_page_worker.cpp
    src/gui/score_viewer.cpp
    # THE NOISE (2026-10-28): the background sound — the probe of the folder
    # under noise/ beside the device config that the `noise` key names, its
    # loader thread, and the second voice the playback device sums under the
    # engine's (noise.h). GUI-only: the CLI plays nothing.
    src/gui/noise.cpp
    # THE TRIM PRESETS (2026-10-28 night): the piece's named trim windows in
    # a GUI-owned device-local file (trim/presets.txt), the TRIM key's list.
    # GUI-only: the CLI reads no preset, the rendering trim being the
    # sidecar's.
    src/gui/trim_presets_file.cpp
    # THE CAR TRANSPORT (2026-09-17): the head unit's SECOND owner — the
    # cluster that drives the project's own transport while the render player
    # is closed, and publishes the console's three lines from the tab, the
    # views and the trim. GUI-only for the player's reason.
    src/gui/car_transport.cpp
    src/gui/target_render.cpp
    src/gui/render_cache.cpp
    src/gui/paint_handler.cpp
    src/gui/waveform_cache.cpp
    src/gui/input_handler.cpp
    src/gui/input_trim.cpp
    src/gui/input_render_dispatch.cpp
    src/gui/input_pointer.cpp
    src/gui/input_key_dispatch.cpp
    # THE PLATFORM BACKEND (platform.h's one include): GuiPlatform over the
    # NativeActivity, whose android_main is the process's entry and calls
    # main.cpp's gui_main.
    src/gui/platform_android.cpp
)

# ============================================================
#  warptempo_gui — the ANDROID library (arm64 NativeActivity, cross-built from
#  the laptop through android/app/configure.sh). It is a SHARED LIBRARY rather
#  than an executable because a NativeActivity's process is started by the
#  system and dlopen's this .so; android_main (platform_android.cpp) is its
#  entry point and calls gui_main (main.cpp).
#
#  Every dependency (the four graphics libraries, resvg, fftw, and the git
#  stack) is found through the M1 sysroot's own .pc files:
#  android/app/configure.sh passes PKG_CONFIG_EXECUTABLE (the toolchain's
#  wrapper, which hard-forces PKG_CONFIG_LIBDIR at android/prebuilt/<abi>) and
#  FFTW3_THREADS_LIB (whose find_library above cannot search a sysroot the NDK
#  toolchain file has re-rooted), so this branch itself names no path.
# ============================================================
if(ANDROID)
    # Cairo needs its FT backend (gui_font_bundled.cpp builds the product's
    # faces from the APK's own font assets, and this cairo is built with no
    # fontconfig at all) and HarfBuzz shapes on those faces. freetype2 gets
    # its own check because the static link needs its archive named
    # explicitly inside the group below.
    pkg_check_modules(WT_ANDROID_GFX REQUIRED cairo cairo-ft harfbuzz freetype2)
    # The icon renderer (svg_icon.cpp): resvg's C API, static, its resvg.pc
    # written by android/deps/85_resvg.sh (upstream ships none without
    # cargo-c). Its STATIC_LDFLAGS carry the Rust archive's native-static-libs
    # (-llog -lunwind -ldl -lm -lc), all the NDK's own: DT_NEEDED gains
    # nothing the nine do not already name (build_apk.sh's allowlist).
    pkg_check_modules(WT_ANDROID_RESVG REQUIRED resvg)
    # The history view's git (git_repo.cpp): libgit2 with its SSH transport.
    # Static like everything here; libgit2.pc's Requires.private names
    # libssh2, whose own .pc names libcrypto, so --static (the STATIC_LDFLAGS
    # below) carries the whole chain -lgit2 -lssh2 -lcrypto -ldl. zlib is
    # libgit2's bundled copy, so the chain adds nothing to DT_NEEDED
    # (android/deps/80_libgit2.sh owns the build's choices).
    pkg_check_modules(WT_ANDROID_GIT REQUIRED libgit2)

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
    # ones in the log.
    set_source_files_properties(${WARPTEMPO_GLUE_DIR}/android_native_app_glue.c
        PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")

    add_library(warptempo_gui SHARED
        ${WARPTEMPO_GUI_SOURCES}
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
        ${WT_ANDROID_RESVG_INCLUDE_DIRS}
        ${WT_ANDROID_GIT_INCLUDE_DIRS}
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
    # discovers at launch. The git chain sits inside the same group for the
    # same reason (its archives are ordered by pkg-config, not by hand).
    target_link_libraries(warptempo_gui PRIVATE
        -Wl,--no-undefined
        -Wl,--start-group
        ${WT_ANDROID_GFX_STATIC_LDFLAGS}
        ${WT_ANDROID_RESVG_STATIC_LDFLAGS}
        ${WT_ANDROID_GIT_STATIC_LDFLAGS}
        ${FFTW3_THREADS_LIB}
        ${FFTW3_LIBRARIES}
        -Wl,--end-group
        aaudio
        android
        # libnativewindow, and it is NOT covered by libandroid (2026-08-27).
        # Most of the ANativeWindow_* surface this backend uses — the geometry
        # setter and the width and height queries — is re-exported by
        # libandroid, which is why this line was not needed until the 90 Hz
        # frame-rate pin at window adoption: ANativeWindow_setFrameRate (API
        # 30) lives in libnativewindow alone, and --no-undefined turns its
        # absence into the build failure it should be rather than a
        # launch-time surprise.
        nativewindow
        # THE GL CANVAS'S TWO (gl_canvas.h): libEGL and libGLESv3, both NDK
        # stable-ABI libraries, the whole of what the canvas adds to
        # DT_NEEDED (build_apk.sh's allowlist).
        EGL
        GLESv3
        log
        m
        dl
    )
    target_link_directories(warptempo_gui PRIVATE ${FFTW3_LIBRARY_DIRS})
else()
# ============================================================
#  warptempo_cli — the laptop's one product, the headless wav render
#  (architect 2026-10-30: the laptop keeps the CLI to git pull and render).
#  Reads <source-stem>.{warpmarkers,phaseresetmarkers,settings} and writes
#  the warped wav the GUI would render for the same project. Runs the full PGHI
#  engine; renders are wav-only. Compiles the parser and engine sources
#  directly, needing no GUI toolchain: fftw alone.
# ============================================================
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
# Applied to the configuration's one target (WARPTEMPO_OPT_TARGETS below). The
# engine's PGHI hot path and the parser's frame-map math must compile under the
# same -O3 -ffp-contract=off on both products, with -march=native on the
# laptop's.
#
# No CMAKE_BUILD_TYPE is set on either host (android/app/configure.sh empties
# it too), so NDEBUG is never defined: every assert in the tree is a live
# breach check in the shipped binaries, a breach stopping the program rather
# than running on.
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

if(ANDROID)
    set(WARPTEMPO_OPT_TARGETS warptempo_gui)
else()
    set(WARPTEMPO_OPT_TARGETS warptempo_cli)
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
    # scoped to C++ alone because the GUI target's one C file is the NDK's
    # native_app_glue, third-party source never audited under it.
    if(ANDROID)
        target_compile_options(warptempo_gui PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:-Wfloat-conversion>)
    endif()
endif()
