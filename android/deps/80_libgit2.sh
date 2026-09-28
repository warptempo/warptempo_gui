#!/usr/bin/env bash
# libgit2 -- the history view's git, in process (src/gui/git_repo.cpp), the
# laptop's version over libssh2 (70_libssh2.sh).
#
# THE CHOICES, each measured by the arc 4 survey's probe:
#   USE_SSH=libssh2     the push's one transport: the deploy key and GitHub's
#                       pinned host keys (git_repo.cpp) are SSH's.
#   USE_HTTPS=OFF       the product refuses an http(s) remote before any
#                       connection (it holds no token), so TLS has no caller and
#                       libgit2 needs OpenSSL for nothing. The plain-http
#                       transport and its bundled llhttp still compile (there is
#                       no switch for them); nothing reaches them.
#   USE_SHA256=Builtin  REQUIRED once HTTPS is off: SHA-256 defaults to the
#                       HTTPS backend. SHA-1 is the collision-detecting builtin
#                       (git's own default).
#   USE_BUNDLED_ZLIB=ON the NDK's libz would work too (stable ABI), but it would
#                       add libz.so to the product's DT_NEEDED for nothing; the
#                       bundled copy is compiled in and the recorded NDK set
#                       stays as it is. No clash with freetype's zlib, which is
#                       static inside its ftgzip.
#   USE_THREADS=ON      REQUIRED: git_repo.h's THREADS paragraph -- the main
#                       thread, the prefetch worker and the checkpoint worker
#                       each call libgit2, and the threadsafe build is what
#                       makes its per-thread error state and pack-window mutexes
#                       real. Bionic keeps pthreads in libc, so configure's
#                       "pthread_create in pthreads - not found" is expected.
#   builtin regex / http-parser, no NTLM, no GSSAPI, no iconv: nothing on the
#   device to find, and nothing this product asks needs them.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

src="$(wt_src "$LIBGIT2_TAR" "$LIBGIT2_URL" "$LIBGIT2_SHA256" "libgit2-$LIBGIT2_VER")"
build="$(wt_fresh_build_dir libgit2)"

cmake -S "$src" -B "$build" "${wt_cmake_common[@]}" \
    -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTS=OFF -DBUILD_CLI=OFF -DBUILD_EXAMPLES=OFF \
    -DUSE_SSH=libssh2 -DUSE_HTTPS=OFF \
    -DUSE_SHA1=CollisionDetection -DUSE_SHA256=Builtin \
    -DUSE_HTTP_PARSER=builtin -DREGEX_BACKEND=builtin \
    -DUSE_BUNDLED_ZLIB=ON \
    -DUSE_NTLMCLIENT=OFF -DUSE_GSSAPI=OFF -DUSE_ICONV=OFF \
    -DUSE_THREADS=ON -DUSE_NSEC=ON

cmake --build "$build" -j"$(nproc)"
cmake --install "$build"

wt_check_lib libgit2.a

# The feature set is the build's whole claim; read it back from the header the
# configure generated (it is internal, never installed) rather than trusting
# the flags.
feat="$build/gen_headers/git2_features.h"
[ -f "$feat" ] || wt_die "no $feat to check the feature set against"
for want in GIT_THREADS GIT_SSH GIT_SSH_LIBSSH2 GIT_SHA256_BUILTIN GIT_COMPRESSION_BUILTIN; do
    grep -q "^#define $want 1" "$feat" || wt_die "$want is not set in $feat"
done
if grep -q '^#define GIT_HTTPS 1' "$feat"; then wt_die "GIT_HTTPS is set: HTTPS did not disable"; fi
wt_say "libgit2 features: threads, ssh (libssh2), sha256 builtin, zlib builtin, no https"
"$WT_PKGCONFIG_WRAPPER" --modversion libgit2
