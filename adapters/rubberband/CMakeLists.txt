cmake_minimum_required(VERSION 3.10)
project(rubberband_adapter VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SNDFILE    REQUIRED sndfile)
pkg_check_modules(RUBBERBAND REQUIRED rubberband)

add_executable(rubberband_adapter main.cpp)

target_include_directories(rubberband_adapter PRIVATE
    ${SNDFILE_INCLUDE_DIRS}
    ${RUBBERBAND_INCLUDE_DIRS}
)

target_link_libraries(rubberband_adapter PRIVATE
    ${SNDFILE_LIBRARIES}
    ${RUBBERBAND_LIBRARIES}
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(rubberband_adapter PRIVATE -Wall -Wextra -O3 -march=native)
endif()
