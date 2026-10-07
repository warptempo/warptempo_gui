#!/usr/bin/env bash
# resvg -- the one icon renderer (src/gui/svg_icon.cpp), its C API crate
# (crates/c-api, lib name `resvg`) built as a static archive from the pinned
# source the laptop's CMake builds too (common.sh's RESVG_*), so both devices
# run the same renderer.
#
# THE CHOICES:
#   cargo rustc --crate-type staticlib, no NDK tool
#                       pure Rust: no crate in this feature set compiles C,
#                       and the archive alone is built (the crate's cdylib
#                       half, which nothing ships, is skipped), so cargo links
#                       nothing and needs no NDK linker (nor cargo-ndk). The
#                       NDK enters at the product's link, which resolves the
#                       archive's native-static-libs.
#   --no-default-features --features raster-images
#                       no text (an icon of the period draws no <text>, and
#                       the default pulls a font database and system fonts),
#                       no svgz; raster-images for an embedded <image> (GNOME
#                       2.30's sheets carry PNGs).
#   --profile production  the workspace's own profile: LTO, codegen-units 1
#                       (1.40 MB in the product's stripped .so against 2.93 MB
#                       for --release). Rust never contracts a float multiply
#                       and add into an FMA, so -ffp-contract=off has no
#                       counterpart to pass.
#   --locked            the tarball's Cargo.lock pins every transitive crate
#                       by checksum; the build downloads the crates it compiles
#                       from crates.io into $WT_WORK/cargo, the CARGO_HOME,
#                       kept across runs (never ~/.cargo).
#   resvg.pc            upstream installs none without cargo-c; one is written
#                       here so the product's CMake arm asks pkg-config for
#                       resvg as it does for every other library in the prefix
#                       and names no path. Libs.private is the archive's
#                       native-static-libs on this target, read back below.
# Needs rustup's stable toolchain with the aarch64-linux-android target.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

command -v cargo >/dev/null || wt_die "no cargo: install rustup and its stable toolchain"
rustup target list --installed | grep -qx "$WT_TARGET" ||
    wt_die "the rustup target $WT_TARGET is missing: rustup target add $WT_TARGET"

src="$(wt_src "$RESVG_TAR" "$RESVG_URL" "$RESVG_SHA256" "resvg-$RESVG_VER")"
build="$(wt_fresh_build_dir resvg)"
export CARGO_HOME="$WT_WORK/cargo"

# 00_env.sh exports CC/CXX/AR and the flags for the TARGET; cargo's host build
# scripts must not see them, so the cargo steps run without them.
(
    unset CC CXX AR RANLIB CFLAGS CXXFLAGS LDFLAGS
    cd "$src"
    cargo rustc --locked --profile production --target "$WT_TARGET" \
        -p resvg-capi --no-default-features --features raster-images \
        --crate-type staticlib --target-dir "$build" \
        -- --print native-static-libs
) 2>&1 | tee "$build/rustc.log"

native="$(sed -n 's/^note: native-static-libs: //p' "$build/rustc.log" | tail -n 1)"
[ -n "$native" ] || wt_die "rustc printed no native-static-libs (see $build/rustc.log)"
wt_say "resvg's native-static-libs: $native"

install -D -m644 "$build/$WT_TARGET/production/libresvg.a" "$WT_PREFIX/lib/libresvg.a"
install -D -m644 "$src/crates/c-api/resvg.h" "$WT_PREFIX/include/resvg.h"
install -D -m644 /dev/stdin "$WT_PREFIX/lib/pkgconfig/resvg.pc" <<PC
prefix="$WT_PREFIX"
libdir="$WT_PREFIX/lib"
includedir="$WT_PREFIX/include"

Name: resvg
Description: resvg's C API (crates/c-api), static, --no-default-features --features raster-images
Version: $RESVG_VER
Libs: -L\${libdir} -lresvg
Libs.private: $native
Cflags: -I\${includedir}
PC

wt_check_lib libresvg.a
grep -q "RESVG_VERSION \"$RESVG_VER\"" "$WT_PREFIX/include/resvg.h" ||
    wt_die "resvg.h does not name version $RESVG_VER"
"$WT_PKGCONFIG_WRAPPER" --modversion resvg
