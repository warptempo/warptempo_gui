#!/usr/bin/env bash
# tools/app_icon/gen_app_icon.sh — assets/icons/adwaita-3.22/AppIcon.png -> THE PROGRAM ICON OUTSIDE THE WINDOW
# THE ICON (architect 2026-10-18): the conductor's baton, the icon set's one RASTER own file — his GIMP cutout of
# Gemini Flash's drawing, 2048 x 2048 RGBA with a hard alpha, no ground, no shadow (AppIcon.png, byte-identical to
# Baton.png, the glyph Warp wears; the caption's icon; assets/icons/adwaita-3.22/README.md its provenance and the
# recorded exception to the set's scalable rule) — the BPM Iterations button keeps Adwaita 3.22's audio-x-generic note
# (MusicNote16th.svg), the program icon's drawing from 2026-10-12 to 2026-10-15 — over WHITE, FLAT
# (architect 2026-10-17): #FFFFFF, one color over the whole square, no gradient (Windows Storm's caption start #800080
# from 2026-10-16 until then, the Cool Edit case's 20 face-ramp bands under Midnight's Face from 2026-10-12 before
# it). The ground is the ANDROID background alone, the Linux icon wears none. Every scale is ImageMagick's LANCZOS
# filter from the full 2048 raster, the filter family the GUI scales it by (svg_icon.cpp). It writes, from the repo
# root's point of view:
#   packaging/warptempo_gui.png     the Linux desktop icon (the .desktop's Icon=warptempo_gui; docs/INSTALL.md
#                                   installs it into hicolor's 256x256/apps): 256 x 256, the BARE drawing on
#                                   transparency, no ground (architect 2026-10-12: the freedesktop convention,
#                                   matching the caption's bare icon; Android's adaptive mask demands a full-bleed
#                                   square, Linux desktops take any shape: "two framings is normal") — a PNG, the
#                                   honest form of a raster;
#   android/app/res/mipmap-<density>/ic_launcher_{foreground,background}.png
#                                   the Android adaptive icon's two layers (ic_launcher.xml's head holds the
#                                   per-density table and the safe-zone arithmetic): the foreground the drawing
#                                   resized at 52 dp square (architect 2026-10-16, his pick off a 60 / 56 / 52 /
#                                   48 / 44 / 40 dp series on the glass: the 64 dp art filled ~86 % of the
#                                   launcher's visible tile and nearly touched its corners), centred on the
#                                   transparent 108 dp canvas; the background the 108 dp canvas flat in GROUND,
#                                   which the launcher's mask cuts to its shape.
# A ONE-SHOT, NOT A BUILD STEP: the outputs are committed and build_apk.sh only compiles res/. Re-run after any
# change of the drawing or the ground; never hand-edit an output. Byte-stable: ImageMagick's -strip drops its date
# chunks. Needs ImageMagick 7's magick on PATH.
#
#   bash tools/app_icon/gen_app_icon.sh
set -euo pipefail
[ $# -eq 0 ] || { echo "usage: bash tools/app_icon/gen_app_icon.sh" >&2; exit 2; }
cd "$(dirname "$0")/../.."

SRC=assets/icons/adwaita-3.22/AppIcon.png
OUT_PNG=packaging/warptempo_gui.png
RES=android/app/res
# White, flat (the head).
GROUND=FFFFFF

# The source's shape, the set's raster seat (src/gui/svg_icon.h's parse_png): a square PNG; anything else is a hard
# fail.
read -r FORMAT WIDTH HEIGHT <<< "$(magick identify -format '%m %w %h' "$SRC")"
[ "$FORMAT" = PNG ] && [ "$WIDTH" = "$HEIGHT" ] \
    || { echo "gen_app_icon: $SRC: not a square PNG" >&2; exit 1; }

magick "$SRC" -filter Lanczos -resize 256x256 -strip \
    -define png:compression-level=9 -define png:compression-filter=5 \
    "PNG32:$OUT_PNG"
echo "wrote $OUT_PNG"

# density f -> the 52 dp art A = 52f px and the 108 dp canvas C = 108f px (ic_launcher.xml's table).
for row in mdpi:52:108 hdpi:78:162 xhdpi:104:216 xxhdpi:156:324 xxxhdpi:208:432; do
    IFS=: read -r density art canvas <<< "$row"
    dir="$RES/mipmap-$density"
    magick "$SRC" -filter Lanczos -resize "${art}x${art}" \
        -background none -gravity center -extent "${canvas}x${canvas}" -strip \
        -define png:compression-level=9 -define png:compression-filter=5 \
        "PNG32:$dir/ic_launcher_foreground.png"
    magick -size "${canvas}x${canvas}" "xc:#$GROUND" -strip \
        -define png:compression-level=9 -define png:compression-filter=5 \
        "PNG32:$dir/ic_launcher_background.png"
    echo "wrote $dir: art ${art} px on ${canvas} px"
done
