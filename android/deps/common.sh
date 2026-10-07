#!/usr/bin/env bash
# Shared bits for the nine dependency builds. Source it, never execute it.
#
# Source tarballs are pinned by SHA-256. Provenance for each pin is recorded in
# android/NOTES.md; eight of the nine are cross-checked against an independent
# publisher (Arch PKGBUILD / Debian .dsc).

set -u
. "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../toolchain" && pwd)/00_env.sh"

# --- pinned sources -------------------------------------------------------
FFTW_VER=3.3.11
FFTW_TAR=fftw-$FFTW_VER.tar.gz
FFTW_URL=https://www.fftw.org/$FFTW_TAR
FFTW_SHA256=5630c24cdeb33b131612f7eb4b1a9934234754f9f388ff8617458d0be6f239a1

FREETYPE_VER=2.14.3
FREETYPE_TAR=freetype-$FREETYPE_VER.tar.xz
FREETYPE_URL=https://download.savannah.gnu.org/releases/freetype/$FREETYPE_TAR
FREETYPE_SHA256=36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f

HARFBUZZ_VER=14.3.1
HARFBUZZ_TAR=harfbuzz-$HARFBUZZ_VER.tar.xz
HARFBUZZ_URL=https://github.com/harfbuzz/harfbuzz/releases/download/$HARFBUZZ_VER/$HARFBUZZ_TAR
HARFBUZZ_SHA256=9dae9538aae2ffdf70cec31f2c27bf68e2aaeeae3112688467697d5faf6194f7

PIXMAN_VER=0.46.4
PIXMAN_TAR=pixman-$PIXMAN_VER.tar.gz
# cairographics.org is unreachable from this host (connect timeout, IPv4, DNS
# resolves) -- the debian pool copy is the pristine upstream .orig tarball and
# its sha256 is published in pixman_0.46.4-1.dsc. See NOTES.md.
PIXMAN_URL=http://deb.debian.org/debian/pool/main/p/pixman/pixman_${PIXMAN_VER}.orig.tar.gz
PIXMAN_SHA256=d09c44ebc3bd5bee7021c79f922fe8fb2fb57f7320f55e97ff9914d2346a591c

CAIRO_VER=1.18.4
CAIRO_TAR=cairo-$CAIRO_VER.tar.xz
CAIRO_URL=http://deb.debian.org/debian/pool/main/c/cairo/cairo_${CAIRO_VER}.orig.tar.xz
CAIRO_SHA256=445ed8208a6e4823de1226a74ca319d3600e83f6369f99b14265006599c32ccb

# The history view's git (src/gui/git_repo.cpp): OpenSSL -> libssh2 -> libgit2,
# each the laptop's own version, so both devices run the same git code.
# OpenSSL's pin equals upstream's .sha256 file AND Arch's PKGBUILD sha256sums.
OPENSSL_VER=3.6.4
OPENSSL_TAR=openssl-$OPENSSL_VER.tar.gz
OPENSSL_URL=https://github.com/openssl/openssl/releases/download/openssl-$OPENSSL_VER/$OPENSSL_TAR
OPENSSL_SHA256=9bffaa1ad1e07b354c21bd3324ec02fa15579f45a7d0494b3e74bc449b7333ef

# libssh2's pin equals Debian's libssh2_1.11.1-6.dsc (.orig.tar.gz, 1093012
# bytes); upstream's .asc could not be checked for want of the signing key.
LIBSSH2_VER=1.11.1
LIBSSH2_TAR=libssh2-$LIBSSH2_VER.tar.gz
LIBSSH2_URL=https://libssh2.org/download/$LIBSSH2_TAR
LIBSSH2_SHA256=d9ec76cbe34db98eec3539fe2c899d26b0c837cb3eb466a56b0f109cabf658f7

# libgit2 publishes no release tarball of its own: this is GitHub's tag
# archive, cached under a versioned name (the URL's own v1.9.7.tar.gz names
# nothing). Arch's PKGBUILD b2sum for the same URL matches the file.
LIBGIT2_VER=1.9.7
LIBGIT2_TAR=libgit2-$LIBGIT2_VER.tar.gz
LIBGIT2_URL=https://github.com/libgit2/libgit2/archive/refs/tags/v$LIBGIT2_VER.tar.gz
LIBGIT2_SHA256=1a4fbe7589e814777ae76b64734ad80f4ecad22cd33a22682a2aaea4ae5375e7

# The icon renderer (src/gui/svg_icon.cpp): resvg's C API, pure Rust, built by
# cargo from GitHub's tag archive, cached under a versioned name as libgit2's
# is. Arch's resvg PKGBUILD sha256sums for the same URL matches the file. THE
# ONE PIN FOR BOTH DEVICES: the laptop's CMakeLists.txt reads RESVG_VER and
# RESVG_SHA256 from these lines, so a version step is an edit here alone. The
# tarball's Cargo.lock pins every crate by its checksum.
RESVG_VER=0.48.1
RESVG_TAR=resvg-$RESVG_VER.tar.gz
RESVG_URL=https://github.com/linebender/resvg/archive/refs/tags/v$RESVG_VER.tar.gz
RESVG_SHA256=40dafea6b4b9d01e9d28b6d49f1e912daf3e9055676ad9179a5a2db6e7386945

# --- helpers --------------------------------------------------------------
WT_SRCDIR="$WT_WORK/src"
WT_BUILDDIR="$WT_WORK/build"
mkdir -p "$WT_SRCDIR" "$WT_BUILDDIR"

# wt_src <tar> <url> <sha256> <expected-dir>  -> echoes the unpacked source dir
wt_src() {
    local tar="$1" url="$2" sha="$3" dir="$4"
    wt_fetch "$url" "$WT_CACHE/$tar" sha256 "$sha" >&2
    if [ ! -f "$WT_SRCDIR/$dir/.wt_unpacked" ]; then
        rm -rf "$WT_SRCDIR/$dir"
        wt_say "unpacking $tar" >&2
        tar -xf "$WT_CACHE/$tar" -C "$WT_SRCDIR"
        [ -d "$WT_SRCDIR/$dir" ] || wt_die "expected $WT_SRCDIR/$dir after unpacking $tar"
        touch "$WT_SRCDIR/$dir/.wt_unpacked"
    fi
    printf '%s\n' "$WT_SRCDIR/$dir"
}

# A fresh build tree every run: cheap here, and it is what makes a re-run after
# a partial failure mean the same thing as a first run.
wt_fresh_build_dir() {
    local name="$1"
    rm -rf "${WT_BUILDDIR:?}/$name"
    mkdir -p "$WT_BUILDDIR/$name"
    printf '%s\n' "$WT_BUILDDIR/$name"
}

# Every meson setup in this tree passes these. --wrap-mode=nofallback is
# load-bearing: without it cairo silently DOWNLOADS AND BUILDS fontconfig (and
# pixman, freetype, ...) instead of failing visibly when pkg-config is wrong.
wt_meson_common=(
    --cross-file "$WT_CROSS_FILE"
    --prefix "$WT_PREFIX"
    --buildtype release
    --default-library static
    --wrap-mode=nofallback
)

# Every CMake configure in this tree (libssh2, libgit2) passes these: the NDK's
# own toolchain file at the sysroot's ABI and API; CMAKE_BUILD_TYPE EMPTY with
# the flags spelled out, so the options are exactly WT_OPT_FLAGS (a Release
# type would add a second -O and -DNDEBUG), as every other dependency here is
# built; and the staging prefix as both the install prefix and the ONE place
# find_* may look (the toolchain file sets the find modes to ONLY, so a path
# outside CMAKE_FIND_ROOT_PATH is never searched, and a host library can never
# be found). PKG_CONFIG_EXECUTABLE is the wrapper, for the same reason meson
# gets it through the cross file.
wt_cmake_common=(
    -G Ninja
    -DCMAKE_TOOLCHAIN_FILE="$WT_NDK/build/cmake/android.toolchain.cmake"
    -DANDROID_ABI="$WT_ABI"
    -DANDROID_PLATFORM="android-$WT_API"
    -DCMAKE_BUILD_TYPE=
    -DCMAKE_C_FLAGS="$WT_OPT_FLAGS"
    -DCMAKE_INSTALL_PREFIX="$WT_PREFIX"
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_PREFIX_PATH="$WT_PREFIX"
    -DCMAKE_FIND_ROOT_PATH="$WT_PREFIX"
    -DPKG_CONFIG_EXECUTABLE="$WT_PKGCONFIG_WRAPPER"
)

wt_check_lib() {  # <libfoo.a> [more...]
    local f out
    for f in "$@"; do
        [ -f "$WT_PREFIX/lib/$f" ] || wt_die "expected $WT_PREFIX/lib/$f after install"
        # No `| grep -q`: grep would close the pipe early and pipefail would
        # then report objdump's SIGPIPE as a build failure.
        out="$("$OBJDUMP" -f "$WT_PREFIX/lib/$f" 2>/dev/null || true)"
        case "$out" in *aarch64*) ;; *) wt_die "$f is not aarch64" ;; esac
    done
    wt_say "installed: $*"
}
