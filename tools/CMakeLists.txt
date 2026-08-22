# Standalone build for the sidecar tools. Like adapters/, this is not wired into
# any product target and has no link path from the GUI or CLI: the root
# CMakeLists never adds this subdirectory. Configure and build it on its own
# when a one-off conversion or a lattice snap is needed:
#
#     cmake -B build-tools -S tools
#     cmake --build build-tools
#
# Both tools COMPILE in-tree product sources — they never edit one. The phase
# reset lattice is defined by the live warp map, so a tool that touches that
# column builds the map through the product's own chain
# (resolve_warp_markers_for_render then build_warp_frame_map) and reads its
# inputs with the product's own parsers, which is also what makes a legacy or
# malformed sibling refuse in the tool exactly as it would refuse at load. The
# sources are listed per target and compiled directly, no library archive —
# object-code duplication accepted deliberately, the same convention the product
# build follows. The frozen directories are compiled here, never modified.
#
# The legacy MM:SS.mmm PARSE helpers stay tool-local as of 2026-07-30 (rationale
# at the head of migrate_sidecar_to_frames.cpp), which is what keeps <regex> out
# of every product translation unit.
cmake_minimum_required(VERSION 3.10)
project(warptempo_tools CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The in-tree sources both tools link: the two marker parsers, the map
# resolver/builder, and the audio probe (with the WAV reader it forwards to and
# that reader's PCM helpers).
set(WARPTEMPO_TOOL_SOURCES
    ../src/parser/warpmarkers_parse.cpp
    ../src/parser/phaseresetmarkers_parse.cpp
    ../src/parser/warp_frame_map_build.cpp
    ../src/audio_io/audio_probe.cpp
    ../src/audio_io/wav_io.cpp
    ../src/audio_io/pcm24.cpp
)

set(WARPTEMPO_TOOL_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/parser
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio_io
)

add_executable(migrate_sidecar_to_frames
    migrate_sidecar_to_frames.cpp
    ${WARPTEMPO_TOOL_SOURCES}
)
target_include_directories(migrate_sidecar_to_frames
    PRIVATE ${WARPTEMPO_TOOL_INCLUDES})

add_executable(snap_sidecar_to_grid
    snap_sidecar_to_grid.cpp
    ${WARPTEMPO_TOOL_SOURCES}
)
target_include_directories(snap_sidecar_to_grid
    PRIVATE ${WARPTEMPO_TOOL_INCLUDES})
