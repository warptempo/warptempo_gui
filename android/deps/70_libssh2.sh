#!/usr/bin/env bash
# libssh2 -- libgit2's SSH transport, over OpenSSL's libcrypto (60_openssl.sh).
#
# ED25519 MUST BE IN: the deploy key is an ed25519 OpenSSH key. It is not a
# switch -- libssh2's openssl.h turns LIBSSH2_ED25519 on for OpenSSL >= 1.1.1 --
# so the check below reads the built archive for the two entry points.
#
# THE ZLIB SWITCH IS LOAD-BEARING. ENABLE_ZLIB_COMPRESSION=OFF alone is not
# enough: libssh2's OpenSSL arm calls find_package(ZLIB) unconditionally, finds
# the NDK sysroot's libz, and writes `-L<NDK sysroot> -lz` into libssh2.pc
# although no libssh2 object references zlib. CMAKE_DISABLE_FIND_PACKAGE_ZLIB
# keeps the .pc honest (`Libs: -L${libdir} -lssh2 -lcrypto`), so the product's
# DT_NEEDED stays the NDK stable set with no libz.so in it.
#
# No OPENSSL_USE_STATIC_LIBS: the prefix holds only static archives
# (60_openssl.sh builds no-shared), so FindOpenSSL can find nothing else.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

src="$(wt_src "$LIBSSH2_TAR" "$LIBSSH2_URL" "$LIBSSH2_SHA256" "libssh2-$LIBSSH2_VER")"
build="$(wt_fresh_build_dir libssh2)"

cmake -S "$src" -B "$build" "${wt_cmake_common[@]}" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
    -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DLINT=OFF \
    -DCRYPTO_BACKEND=OpenSSL -DOPENSSL_ROOT_DIR="$WT_PREFIX" \
    -DENABLE_ZLIB_COMPRESSION=OFF -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON \
    -DENABLE_DEBUG_LOGGING=OFF

cmake --build "$build" -j"$(nproc)"
cmake --install "$build"
# libssh2's install has no switch for its man pages and docs; nothing reads
# them from a cross prefix. Keep it to headers + libs + .pc, as 10_fftw3.sh does.
rm -rf "$WT_PREFIX/share/man" "$WT_PREFIX/share/doc/libssh2"
rmdir "$WT_PREFIX/share/doc" 2>/dev/null || true

wt_check_lib libssh2.a

# The two failure modes are silent, so both are hard errors here.
syms="$("$NM" -g --defined-only "$WT_PREFIX/lib/libssh2.a" 2>/dev/null || true)"
for s in _libssh2_ed25519_sign _libssh2_curve25519_new; do
    case "$syms" in *" $s"*) ;; *) wt_die "libssh2.a defines no $s: ED25519 did not enable" ;; esac
done
wt_say "libssh2.a: ed25519 in"
pc_libs="$("$WT_PKGCONFIG_WRAPPER" --static --libs libssh2)"
case " $pc_libs " in *" -lz "*) wt_die "libssh2.pc names -lz ($pc_libs)" ;; esac
wt_say "libssh2.pc: $pc_libs"
