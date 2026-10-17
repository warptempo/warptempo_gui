#!/usr/bin/env bash
# tools/app_icon/gen_app_icon.sh — assets/icons/adwaita-3.22/AppIcon.svg -> THE PROGRAM ICON OUTSIDE THE WINDOW
# THE ICON (architect 2026-10-16; re-rendered 2026-10-17): the architect's digital metronome, the icon set's one own
# file (AppIcon.svg, byte-identical to DigitalMetronome.svg, the glyph Warp wears; the caption's icon;
# assets/icons/adwaita-3.22/README.md its provenance and licence) — the BPM Iterations button keeps Adwaita 3.22's
# audio-x-generic note (MusicNote16th.svg), the program icon's drawing from 2026-10-12 until then — over WHITE, FLAT
# (architect 2026-10-17): #FFFFFF, one color over the whole square, no gradient (Windows Storm's caption start #800080
# from 2026-10-16 until then, the Cool Edit case's 20 face-ramp bands under Midnight's Face from 2026-10-12 before
# it). The ground is the ANDROID background alone, the Linux icon wears none. It writes, from the
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
#                                   rasterised ANTIALIASED by rsvg-convert (librsvg) at 52 dp square (architect
#                                   2026-10-16, his pick off a 60 / 56 / 52 / 48 / 44 / 40 dp series on the
#                                   glass: the 64 dp art filled ~86 % of the launcher's visible tile and nearly
#                                   touched its corners), centred on the transparent 108 dp canvas; the background the 108 dp canvas flat in
#                                   GROUND, which the launcher's mask cuts to its shape.
# A ONE-SHOT, NOT A BUILD STEP: the outputs are committed and build_apk.sh only compiles res/. Re-run after any
# change of the drawing or the ground; never hand-edit an output. Byte-stable: rsvg-convert writes no timestamp and
# ImageMagick's -strip drops its date chunks. Needs rsvg-convert and ImageMagick 7's magick on PATH.
#
#   bash tools/app_icon/gen_app_icon.sh
set -euo pipefail
[ $# -eq 0 ] || { echo "usage: bash tools/app_icon/gen_app_icon.sh" >&2; exit 2; }
cd "$(dirname "$0")/../.."

SRC=assets/icons/adwaita-3.22/AppIcon.svg
OUT_SVG=packaging/warptempo_gui.svg
RES=android/app/res
# White, flat (the head).
GROUND=FFFFFF

# The source's shape, the extraction tool's (tools/icon_set_adwaita_322/): no XML declaration, line 1 the root's start
# tag, a 48 x 48 root; anything else is a hard fail.
head -n 1 "$SRC" | grep -q '^<svg[[:space:]]' || { echo "gen_app_icon: $SRC: line 1 is not the root's start tag" >&2; exit 1; }
ROOT=$(tr '\n' ' ' < "$SRC" | grep -o '<svg[[:space:]][^>]*>' | sed -n 1p)
grep -q -E '[[:space:]]width="48(\.0+)?(px)?"' <<< "$ROOT" && grep -q -E '[[:space:]]height="48(\.0+)?(px)?"' <<< "$ROOT" \
    || { echo "gen_app_icon: $SRC: not a 48 x 48 drawing" >&2; exit 1; }

{
    printf '<svg xmlns="http://www.w3.org/2000/svg" width="48" height="48" viewBox="0 0 48 48">\n'
    printf '  <!-- The program icon outside the window, the Linux desktop icon (the .desktop'"'"'s\n'
    printf '       Icon=warptempo_gui): the digital metronome, the caption'"'"'s icon and the glyph\n'
    printf '       Warp wears, the icon set'"'"'s one own file (assets/icons/adwaita-3.22/README.md),\n'
    printf '       bare on transparency (architect 2026-10-12; the metronome since 2026-10-16). GENERATED\n'
    printf '       by tools/app_icon/gen_app_icon.sh from %s,\n' "$SRC"
    printf '       the drawing verbatim: regenerate, never hand-edit. -->\n'
    cat "$SRC"
    [ -z "$(tail -c 1 "$SRC")" ] || printf '\n'
    printf '</svg>\n'
} > "$OUT_SVG"
echo "wrote $OUT_SVG"

# density f -> the 52 dp art A = 52f px and the 108 dp canvas C = 108f px (ic_launcher.xml's table).
for row in mdpi:52:108 hdpi:78:162 xhdpi:104:216 xxhdpi:156:324 xxxhdpi:208:432; do
    IFS=: read -r density art canvas <<< "$row"
    dir="$RES/mipmap-$density"
    rsvg-convert --format=png --width="$art" --height="$art" "$SRC" \
        | magick png:- -background none -gravity center -extent "${canvas}x${canvas}" -strip \
            -define png:compression-level=9 -define png:compression-filter=5 \
            "PNG32:$dir/ic_launcher_foreground.png"
    magick -size "${canvas}x${canvas}" "xc:#$GROUND" -strip \
        -define png:compression-level=9 -define png:compression-filter=5 \
        "PNG32:$dir/ic_launcher_background.png"
    echo "wrote $dir: art ${art} px on ${canvas} px"
done
