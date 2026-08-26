#!/usr/bin/env bash
# Configure the PRODUCT's Android build: the real GUI, cross-compiled for
# arm64-v8a against the M1 dependency sysroot, into android/app/build-android/.
#
#     bash android/app/configure.sh          # configure (idempotent)
#     cmake --build android/app/build-android -j$(nproc)
#
# build_apk.sh calls this itself, so packaging from a clean tree is one command.
#
# WHAT THIS SCRIPT OWNS, and why it rather than CMakeLists.txt: every path into
# the M1 sysroot. The root CMakeLists' if(ANDROID) branch names no directory at
# all -- it asks pkg-config for cairo/cairo-ft/harfbuzz/freetype2 and reads
# FFTW3_THREADS_LIB -- so the two knobs below are the whole binding between the
# product build and android/prebuilt/<abi>.
#
#   PKG_CONFIG_EXECUTABLE  the toolchain's wrapper, which hard-forces
#                          PKG_CONFIG_LIBDIR at the staging prefix so a host
#                          .pc can never leak into a cross build. It must be
#                          set on the command line because find_package(PkgConfig)
#                          runs at the TOP of CMakeLists.txt, before any branch.
#   FFTW3_THREADS_LIB      the one dependency with no .pc file upstream. The
#                          top-level find_library cannot find it here: the NDK
#                          toolchain file sets CMAKE_FIND_ROOT_PATH_MODE_LIBRARY
#                          to ONLY, which re-roots the HINTS at the NDK sysroot,
#                          so the archive is named outright and find_library
#                          skips its search on an already-set cache entry.

set -euo pipefail

APPDIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
. "$APPDIR/../toolchain/00_env.sh"

BUILD="${WT_ANDROID_BUILD_DIR:-$APPDIR/build-android}"

[ -x "$WT_PKGCONFIG_WRAPPER" ] ||
    wt_die "missing $WT_PKGCONFIG_WRAPPER (run android/toolchain/40_gen_cross_file.sh)"
[ -f "$WT_PREFIX/lib/libfftw3_threads.a" ] ||
    wt_die "missing $WT_PREFIX/lib/libfftw3_threads.a (run android/deps/build_all.sh)"
[ -f "$WT_NDK/build/cmake/android.toolchain.cmake" ] ||
    wt_die "no NDK cmake toolchain file under $WT_NDK"

wt_say "configuring $BUILD (NDK $WT_NDK_RELEASE, $WT_ABI, android-$WT_API)"

# CMAKE_BUILD_TYPE is deliberately EMPTY. The project appends its own
# -O3 -ffp-contract=off to every target (the flag block at the bottom of
# CMakeLists.txt), and a Release build type would add a second -O and an
# -DNDEBUG the Linux default build does not have -- the two products must
# compile the frozen stages under the same options.
#
# WARPTEMPO_BUILD_CLI=OFF: the headless render path is a desktop tool.
cmake -S "$WT_REPO_ROOT" -B "$BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$WT_NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$WT_ABI" \
    -DANDROID_PLATFORM="android-$WT_API" \
    -DCMAKE_BUILD_TYPE= \
    -DWARPTEMPO_BUILD_GUI=ON \
    -DWARPTEMPO_BUILD_CLI=OFF \
    -DPKG_CONFIG_EXECUTABLE="$WT_PKGCONFIG_WRAPPER" \
    -DFFTW3_THREADS_LIB="$WT_PREFIX/lib/libfftw3_threads.a" \
    "$@"

wt_say "configured. Build with: cmake --build $BUILD -j\$(nproc)"
