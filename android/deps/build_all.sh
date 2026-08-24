#!/usr/bin/env bash
# Build the whole dependency sysroot into android/prebuilt/arm64-v8a, in order.
#
# The order is a straight line, not a graph:
#   freetype (harfbuzz DISABLED) -> harfbuzz (freetype enabled)
#   pixman ------------------------------------------------> cairo
#   fftw3 (independent)
# The freetype<->harfbuzz cycle is real, but the freetype pass-2 rebuild only
# improves autohinting for complex scripts (Arabic, Indic). This product paints
# 12pt Latin, so pass 2 is deliberately skipped and the order stays linear.
#
# Safe to re-run: every step wipes its own build tree first, and the fetch step
# is checksum-verified and cached.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"

bash "$here/10_fftw3.sh"
bash "$here/20_freetype.sh"
bash "$here/30_harfbuzz.sh"
bash "$here/40_pixman.sh"
bash "$here/50_cairo.sh"
bash "$here/smoke/build_smoke.sh"

. "$here/common.sh"
wt_say "sysroot complete: $WT_PREFIX"
ls -la "$WT_PREFIX/lib"/*.a
