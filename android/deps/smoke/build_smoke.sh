#!/usr/bin/env bash
# Compile AND LINK smoke.cpp against the full staging sysroot, twice:
#   * at $WT_API      -- the level the deps were compiled at, i.e. minSdk
#   * at $WT_TARGET_SDK -- the level the app will declare as targetSdk
# and then verify 16 KB page alignment on the produced .so, which is the only
# place the alignment is observable (a static archive has no LOAD segments; the
# property is created at link time and NDK r28+ makes it the default).
#
# The output never runs. A clean link is the claim: five libraries, their
# headers and bionic all agree.
set -euo pipefail
. "$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../toolchain" && pwd)/00_env.sh"

out="$WT_WORK/smoke"
mkdir -p "$out"
src="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/smoke.cpp"

pc="$WT_PKGCONFIG_WRAPPER"
cflags="$("$pc" --cflags cairo cairo-ft harfbuzz freetype2 fftw3)"
# --static: the staging prefix is static-only, so the Libs.private chains
# (cairo -> pixman -> freetype -> m) must come through.
libs="$("$pc" --static --libs cairo cairo-ft harfbuzz freetype2 fftw3)"
# fftw3_threads ships no .pc upstream (Makefile.am installs only fftw3.pc), which
# is exactly why the product finds it with find_library. Name it by hand.
libs="$libs -lfftw3_threads"

fail=0
for api in "$WT_API" "$WT_TARGET_SDK"; do
    cxx="$WT_TCBIN/${WT_TARGET}${api}-clang++"
    [ -x "$cxx" ] || wt_die "no compiler wrapper for API $api"
    so="$out/libwtsmoke-api$api.so"

    wt_say "linking smoke TU at API $api"
    # -static-libstdc++ mirrors the app's ANDROID_STL=c++_static: this product
    # produces exactly ONE .so, which is the case where static libc++ is right.
    "$cxx" -fPIC -shared -O3 -ffp-contract=off -std=c++23 \
        -static-libstdc++ \
        -o "$so" "$src" \
        $cflags -L"$WT_PREFIX/lib" $libs -lm \
        || { wt_warn "link FAILED at API $api"; fail=1; continue; }

    wt_say "ok: $so"
    "$READELF" -h "$so" | grep -E 'Machine|Type:'

    # The research doc's check: expect "align 2**14" on every LOAD segment.
    aligns="$("$OBJDUMP" -p "$so" | grep -A1 'LOAD' | grep -oE 'align 2\*\*[0-9]+' | sort -u)"
    printf '  LOAD alignment: %s\n' "$(echo "$aligns" | tr '\n' ' ')"
    if [ "$aligns" != "align 2**14" ]; then
        wt_warn "NOT 16 KB aligned at API $api (got: $aligns)"
        fail=1
    else
        wt_say "16 KB page alignment confirmed at API $api"
    fi
done

[ "$fail" -eq 0 ] || wt_die "smoke build had failures"
wt_say "smoke compile+link clean at API $WT_API and API $WT_TARGET_SDK"
