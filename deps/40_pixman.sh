#!/usr/bin/env bash
# pixman -- cairo's image-surface engine, and the only cairo dependency that is
# effectively mandatory.
#
# The cpu-features question is answered by the source: pixman's runtime ARM
# detector (the #elif defined(__ANDROID__) <cpu-features.h> block) compiles only
# under the 32-bit USE_ARM_SIMD/USE_ARM_NEON macros, while USE_ARM_A64_NEON
# needs no detection at all ("neon is a part of aarch64"). So arm64-v8a needs no
# cpu-features path and no patch.
#
# -Da64-neon=enabled turns the one silent failure mode -- a cpu_family typo in
# the cross file giving a scalar pixman -- into a hard configure error.
# Do NOT pass -Dneon / -Darm-simd here: both are gated on cpu_family()=='arm'
# and would hard-error. They self-skip at auto.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

src="$(wt_src "$PIXMAN_TAR" "$PIXMAN_URL" "$PIXMAN_SHA256" "pixman-$PIXMAN_VER")"
build="$(wt_fresh_build_dir pixman)"

meson setup "$build" "$src" "${wt_meson_common[@]}" \
    -Da64-neon=enabled \
    -Dgnu-inline-asm=enabled \
    -Dtls=enabled \
    -Dlibpng=disabled -Dgtk=disabled -Dopenmp=disabled \
    -Dtests=disabled -Ddemos=disabled

ninja -C "$build"
ninja -C "$build" install

wt_check_lib libpixman-1.a
"$WT_PKGCONFIG_WRAPPER" --modversion pixman-1
