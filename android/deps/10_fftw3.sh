#!/usr/bin/env bash
# FFTW3 -- DOUBLE precision, NEON, threads. Autotools (NOT upstream's CMake:
# that build has no NEON option at all).
#
# Three flags carry the whole build:
#   --enable-neon    FFTW NEVER auto-detects NEON on ANY architecture (default
#                    no). On aarch64 it works in DOUBLE precision -- the
#                    opposite of the well-known ARMv7 single-only rule -- so
#                    omitting this silently costs a large factor on the PGHI
#                    inner loops. termux's fftw build omits it; do not copy that.
#   --enable-threads bionic keeps pthreads in libc, so ACX_PTHREAD's "none"
#                    case succeeds and PTHREAD_LIBS is empty. libfftw3_threads
#                    exists only so the engine's fftw_plan_with_nthreads(1)
#                    (a determinism invariant) links -- src/engine/ is frozen,
#                    so we build the library rather than touch the caller.
#   double           NOT --enable-float: there is no fftwf_ call in the tree.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/common.sh"

src="$(wt_src "$FFTW_TAR" "$FFTW_URL" "$FFTW_SHA256" "fftw-$FFTW_VER")"
build="$(wt_fresh_build_dir fftw3)"

cd "$build"
"$src/configure" \
    --host="$WT_TARGET" \
    --prefix="$WT_PREFIX" \
    --enable-neon \
    --enable-threads \
    --disable-fortran \
    --disable-doc \
    --disable-shared --enable-static \
    --with-pic \
    ac_cv_func_clock_gettime=no \
    CC="$CC" AR="$AR" RANLIB="$RANLIB" STRIP="$STRIP" \
    CFLAGS="$CFLAGS" LDFLAGS="$LDFLAGS"

make -j"$(nproc)"
make install

# FFTW has no --disable-tools: `make install` drops aarch64 fftw-wisdom
# binaries and their man pages into the prefix. Nothing links them and they
# cannot run on the host; keep the staging prefix to headers + libs + .pc.
rm -rf "$WT_PREFIX/bin" "$WT_PREFIX/share/man"
# libtool .la files carry absolute host build paths and nothing in this stack
# reads them (the app links with pkg-config + find_library).
rm -f "$WT_PREFIX/lib"/*.la

# HAVE_NEON must actually be on -- the failure mode is silent and expensive.
grep -q '^#define HAVE_NEON 1' config.h || wt_die "HAVE_NEON not set: NEON did not enable"
wt_say "config.h: HAVE_NEON 1"

wt_check_lib libfftw3.a libfftw3_threads.a
# There is no fftw3_threads.pc upstream (Makefile.am installs only fftw3.pc);
# the product already finds it with find_library, and that pattern ports.
[ -f "$WT_PREFIX/lib/pkgconfig/fftw3.pc" ] || wt_die "fftw3.pc missing"
