# Standalone build for the sidecar migration tool. Like adapters/, this is not
# wired into any product target and has no link path from the GUI or CLI: the
# root CMakeLists never adds this subdirectory. Configure and build it on its
# own when the one-off conversion is needed:
#
#     cmake -B build-tools -S tools
#     cmake --build build-tools
#
# It has no dependencies beyond the one in-tree header it reuses
# (src/parser/frame_format.h — the live authored-frame serialization). The
# legacy MM:SS.mmm PARSE helpers are tool-local as of 2026-07-30 (rationale at
# the head of migrate_sidecar_to_frames.cpp), which is what keeps <regex> out of
# every product translation unit.
cmake_minimum_required(VERSION 3.10)
project(warptempo_tools CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(migrate_sidecar_to_frames migrate_sidecar_to_frames.cpp)
