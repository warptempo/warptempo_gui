#!/usr/bin/env bash
# tools/app_icon/gen_app_icon.sh — assets/icons/adwaita-3.22/AppIcon.svg -> THE PROGRAM ICON OUTSIDE THE WINDOW
# THE ICON (architect 2026-10-15): Adwaita 3.22's appointment-new clock, its star and shadows removed, the red slice
# kept (AppIcon.svg, byte-identical to AppointmentNew.svg, the glyph Warp wears; the caption's icon;
# assets/icons/adwaita-3.22/README.md the extraction and its changes) — the BPM Iterations button keeps Adwaita 3.22's
# audio-x-generic note (MusicNote16th.svg), the program icon's drawing from 2026-10-12 until then — over THE BUTTON
# CASE'S GRADIENT:
# the Cool Edit case's face ramp at rest, its 20 stops under the default palette's Face (Cool Edit's Midnight,
# #272B6D) by the case's own derivation (src/gui/cool_edit_derive.h's kCaseRamp, the per-row HLS fit). The 20 tones
# below are that program's own output (a one-shot calling case_tone on 0x272B6D), top to bottom. The ramp is spread
# over the whole square the way the app spreads it over the glyph seat (cool_edit_paint.cpp's paint_face_ramp): stop
# i is a flat band of device rows [nearbyint(i·G/20), nearbyint((i+1)·G/20)) of the square's G rows, no blending;
# the case's highlight, shadow and black line are not drawn (the launcher's mask cuts the shape). The bands are the ANDROID background alone, the Linux icon wears none. It writes, from the
# repo root's point of view:
#   packaging/warptempo_gui.svg     the Linux desktop icon (the .desktop's Icon=warptempo_gui; docs/INSTALL.md
#                                   installs it into hicolor's scalable/apps): a 48 x 48 document carrying the
#                                   BARE drawing on transparency, no ground (architect 2026-10-12: the freedesktop
#                                   convention, matching the caption's bare note; Android's adaptive mask demands a
#                                   full-bleed square, Linux desktops take any shape: "two framings is normal"), the
#                                   drawing VERBATIM as a nested <svg> element (the set's files carry no XML
#                                   declaration, so its bytes whole);
#   android/app/res/mipmap-<density>/ic_launcher_{foreground,background}.png
#                                   the Android adaptive icon's two layers (ic_launcher.xml's head holds the
#                                   per-density table and the safe-zone arithmetic): the foreground the drawing
#                                   rasterised ANTIALIASED by rsvg-convert (librsvg) at 64 dp square, centred on
#                                   the transparent 108 dp canvas; the background the 108 dp canvas in the same 20
#                                   bands (G = the canvas), which the launcher's mask cuts to its shape.
# A ONE-SHOT, NOT A BUILD STEP: the outputs are committed and build_apk.sh only compiles res/. Re-run after any
# change of the drawing or the ramp; never hand-edit an output. Byte-stable: rsvg-convert writes no timestamp and
# ImageMagick's -strip drops its date chunks. Needs rsvg-convert and ImageMagick 7's magick on PATH.
#
#   bash tools/app_icon/gen_app_icon.sh
set -euo pipefail
[ $# -eq 0 ] || { echo "usage: bash tools/app_icon/gen_app_icon.sh" >&2; exit 2; }
cd "$(dirname "$0")/../.."

SRC=assets/icons/adwaita-3.22/AppIcon.svg
OUT_SVG=packaging/warptempo_gui.svg
RES=android/app/res
# The 20 case tones on Face 272B6D, top to bottom (the head).
RAMP=(F6F7FD F0F1F8 E9EAF8 E3E4F4 E0E1F1 D8D9E9 D4D4E4 CFD0DF CCCDDB C6C6D3
      BEBFCA B8B9C6 B8B8C4 B1B1BC ABACB7 A6A7B0 A1A2AB 9D9DA7 9898A1 90919A)

# nearbyint(i * G / 20), the recorded rule: ties to even, in integers. An exact half occurs (at the hdpi
# canvas, G = 162, i = 5 is 40.5 and lands on 40; until 2026-10-13 this rounded half up and began that band on
# row 41, the one row the regeneration of that day moved, in mipmap-hdpi's background alone).
edge() {   # edge i G
    local n=$(( 2 * $2 * $1 )) q r
    q=$(( n / 40 )); r=$(( n % 40 ))
    if (( r > 20 || (r == 20 && q % 2 == 1) )); then q=$(( q + 1 )); fi
    echo "$q"
}

# The source's shape, the extraction tool's (tools/icon_set_adwaita_322/): no XML declaration, line 1 the root's start
# tag, a 48 x 48 root; anything else is a hard fail.
head -n 1 "$SRC" | grep -q '^<svg[[:space:]]' || { echo "gen_app_icon: $SRC: line 1 is not the root's start tag" >&2; exit 1; }
ROOT=$(tr '\n' ' ' < "$SRC" | grep -o '<svg[[:space:]][^>]*>' | sed -n 1p)
grep -q -E '[[:space:]]width="48(\.0+)?(px)?"' <<< "$ROOT" && grep -q -E '[[:space:]]height="48(\.0+)?(px)?"' <<< "$ROOT" \
    || { echo "gen_app_icon: $SRC: not a 48 x 48 drawing" >&2; exit 1; }

{
    printf '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 48 48">\n'
    printf '  <!-- The program icon outside the window, the Linux desktop icon (the .desktop'"'"'s\n'
    printf '       Icon=warptempo_gui): Adwaita 3.22'"'"'s appointment-new clock, the caption'"'"'s icon\n'
    printf '       and the glyph Warp wears (assets/icons/adwaita-3.22/README.md),\n'
    printf '       bare on transparency (architect 2026-10-12; the clock since 2026-10-15). GENERATED\n'
    printf '       by tools/app_icon/gen_app_icon.sh from %s,\n' "$SRC"
    printf '       the drawing verbatim: regenerate, never hand-edit. -->\n'
    cat "$SRC"
    [ -z "$(tail -c 1 "$SRC")" ] || printf '\n'
    printf '</svg>\n'
} > "$OUT_SVG"
echo "wrote $OUT_SVG"

# density f -> the 64 dp art A = 64f px and the 108 dp canvas C = 108f px (ic_launcher.xml's table).
for row in mdpi:64:108 hdpi:96:162 xhdpi:128:216 xxhdpi:192:324 xxxhdpi:256:432; do
    IFS=: read -r density art canvas <<< "$row"
    dir="$RES/mipmap-$density"
    rsvg-convert --format=png --width="$art" --height="$art" "$SRC" \
        | magick png:- -background none -gravity center -extent "${canvas}x${canvas}" -strip \
            -define png:compression-level=9 -define png:compression-filter=5 \
            "PNG32:$dir/ic_launcher_foreground.png"
    draw=()
    for i in $(seq 0 19); do
        y0=$(edge "$i" "$canvas"); y1=$(edge $((i + 1)) "$canvas")
        [ "$y1" -gt "$y0" ] && draw+=(-fill "#${RAMP[$i]}" -draw "rectangle 0,$y0 $((canvas - 1)),$((y1 - 1))")
    done
    magick -size "${canvas}x${canvas}" xc:black +antialias "${draw[@]}" -strip \
        -define png:compression-level=9 -define png:compression-filter=5 \
        "PNG32:$dir/ic_launcher_background.png"
    echo "wrote $dir: art ${art} px on ${canvas} px"
done
