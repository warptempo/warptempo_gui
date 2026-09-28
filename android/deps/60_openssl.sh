#!/usr/bin/env bash
# OpenSSL -- libssh2's crypto, and nothing else's.
#
# ONLY libcrypto IS LINKED. libgit2 is built with HTTPS OFF (80_libgit2.sh), so
# OpenSSL's TLS half has no caller: libcrypto is what libssh2 signs, verifies
# and key-exchanges with. `build_libs` still produces libssl.a beside it (no
# Configure switch builds libcrypto alone); it lands in the prefix and nothing
# references it, so --no-undefined and the static group never pull it in.
#
# ED25519 IS WHY IT IS OpenSSL rather than a smaller backend: the deploy key is
# an ed25519 OpenSSH key, and libssh2 enables LIBSSH2_ED25519 on OpenSSL >=
# 1.1.1 (its openssl.h). The smoke link checks the symbols.
#
# THE PLAIN BUILD, not a trimmed one (architect 2026-09-27): some thirty more
# no-<alg> switches measured 1.4 MB off the stripped .so, and the list would be
# a maintenance surface for one sideloaded tablet. The switches below drop what
# is outside a static library's job (apps, docs, tests, DSO/engine/module
# loading, the console UI) and the protocols and ciphers nothing should speak.
#
# OpenSSL's OWN ANDROID RECIPE, IN A CLEAN ENVIRONMENT: Configure's android-arm64
# target finds the NDK through ANDROID_NDK_ROOT and the triple-prefixed clang on
# PATH (00_env.sh puts the toolchain bin there) and picks CC, AR and RANLIB
# itself. 00_env.sh's exported CFLAGS/LDFLAGS are NOT inherited: an
# environment CFLAGS REPLACES the target's own flag set in Configure rather than
# adding to it, so the flags travel on the Configure line instead (appended to
# the target's), exactly as the arc 4 survey's probe built it.
# -D__ANDROID_API__ is Configure's documented way to name the API level.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

src="$(wt_src "$OPENSSL_TAR" "$OPENSSL_URL" "$OPENSSL_SHA256" "openssl-$OPENSSL_VER")"
build="$(wt_fresh_build_dir openssl)"

clean_env=(env -u CC -u CXX -u CFLAGS -u CXXFLAGS -u LDFLAGS -u AR -u RANLIB
           -u STRIP -u NM -u LD ANDROID_NDK_ROOT="$WT_NDK")

cd "$build"
"${clean_env[@]}" "$src/Configure" android-arm64 \
    -D__ANDROID_API__="$WT_API" \
    --prefix="$WT_PREFIX" --libdir=lib \
    no-shared no-module no-dso no-engine no-tests no-docs no-apps \
    no-ui-console no-comp no-ssl3 no-weak-ssl-ciphers no-legacy \
    $WT_OPT_FLAGS

"${clean_env[@]}" make -j"$(nproc)" build_libs
# install_dev: headers, the two archives and their .pc files -- no binaries,
# no man pages, no config tree.
"${clean_env[@]}" make install_dev

wt_check_lib libcrypto.a libssl.a
"$WT_PKGCONFIG_WRAPPER" --modversion libcrypto
