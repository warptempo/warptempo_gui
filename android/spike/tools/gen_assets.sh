#!/usr/bin/env bash
# Populate the spike's asset staging dir -- two outline faces (Nimbus Sans
# Regular and Bold, copied from the repository's fonts/, the product's
# fallback faces since 2026-10-06) and the audition WAV -- under
# android/spike/build/assets/.
#
# It lands under build/ ON PURPOSE: the repo's .gitignore already ignores
# `build*/`, so the generated WAV and the font copies stay out of the repo with
# no .gitignore edit (the tracked originals are fonts/'s).
#
# Idempotent: every output is rewritten from its source each run, and every run
# produces byte-identical bytes (the fonts are copies; the WAV is deterministic).
# build_apk.sh calls this itself, so a clean tree still builds.

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SPIKE="$(cd -- "$HERE/.." && pwd)"
ASSETS="${1:-$SPIKE/build/assets}"

FONT_DIR="$SPIKE/../../fonts"

say() { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m==> ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

mkdir -p "$ASSETS"

for f in NimbusSans-Regular.otf NimbusSans-Bold.otf; do
    [ -f "$FONT_DIR/$f" ] || die "missing $FONT_DIR/$f (the repository's fonts/)"
    cp -f "$FONT_DIR/$f" "$ASSETS/$f"
    say "asset: $f ($(stat -c%s "$ASSETS/$f") bytes)"
done

command -v python3 >/dev/null 2>&1 || die "python3 is needed to generate the spike WAV"
python3 "$HERE/gen_wav.py" "$ASSETS/spike.wav"
say "asset: spike.wav ($(stat -c%s "$ASSETS/spike.wav") bytes, 44100 Hz stereo 16-bit)"
