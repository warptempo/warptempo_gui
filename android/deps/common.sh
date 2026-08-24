#!/usr/bin/env bash
# Shared bits for the five dependency builds. Source it, never execute it.
#
# Source tarballs are pinned by SHA-256. Provenance for each pin is recorded in
# android/NOTES.md; four of the five are cross-checked against an independent
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
