# Standalone build for the sidecar migration tool. Like adapters/, this is not
# wired into any product target and has no link path from the GUI or CLI: the
# root CMakeLists never adds this subdirectory. Configure and build it on its
# own when the one-off conversion is needed:
#
#     cmake -B build-tools -S tools
#     cmake --build build-tools
#
# It has no dependencies beyond the two in-tree headers it reuses
# (src/time_format.h, src/parser/frame_format.h).
cmake_minimum_required(VERSION 3.10)
project(warptempo_tools CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(migrate_sidecar_to_frames migrate_sidecar_to_frames.cpp)
