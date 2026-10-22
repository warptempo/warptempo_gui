#!/usr/bin/env python3
"""THE MATERIALS' PRE-RENDERED COVERS (architect 2026-10-21, the fortepiano chrome;
src/gui/materials.h's head owns the rule).

Each master of assets/tiles/ -- `<Name>.png` or `<Name>.jpg`, the PNG winning where both
stand (the icons' rule) -- is drawn under THE COVER RULE at every size of SIZES: scaled by
one factor s = max(W / w, H / h) through PIL's Lanczos (the mocks' own filter, so the
shipped bytes are what he judged), the overhang cropped evenly from both sides, exactly
W x H, and written beside it as `<Name>-<W>x<H>.png` (lossless). The app bakes such a cover
with no filtering when its client is that size and scales the master itself only when no
cover stands (materials.cpp). The two sizes are the two devices' standard windows: the
tablet's 2304 x 1440 (the only covers the APK carries, android/app/build_apk.sh) and the
laptop's maximized 1920 x 1080.

A MATERIAL'S COVER HEIGHT IS ITS OWN (architect 2026-10-22, "make the brass image a lot
smaller ... cutting the image in half or a quarter vertically, keeping the same length"):
FRACTION, materials.h's kGuiMaterialCoverDivisor as a fraction. The brass is the marker
lane's accent alone, 17 W of the window, so its covers are A QUARTER OF THE WINDOW'S HEIGHT
at the full width -- `Metal-2304x360.png`, `Metal-1920x270.png` -- under THE STRIP RULE: the
master scaled by one factor s = W / w to the window's WIDTH, the strip its CENTER rows (the
overhang cropped evenly top and bottom), the same Lanczos; a master too short for the strip
at that factor is this tool's hard fail, never a stretch (the masters are 16:9, so it never
is). The bake takes the lane's rows from the strip's center rows (materials.cpp's
bake_metal).

RERUN AFTER A DROP: a new master in assets/tiles/ needs its covers rendered again before
the build, and the outputs are committed beside the masters.

Run: python3 -I tools/tiles/render_covers.py
"""
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
TILES = os.path.normpath(os.path.join(HERE, '..', '..', 'assets', 'tiles'))
NAMES = ('Wood', 'Metal', 'Case')          # materials.h's kGuiMaterialNames
SIZES = ((2304, 1440), (1920, 1080))       # the tablet's window, the laptop's maximized one
FRACTION = {'Wood': 1, 'Metal': 4, 'Case': 1}   # the window's height over this (the head)


def master_path(name):
    for ext in ('.png', '.jpg'):
        p = os.path.join(TILES, name + ext)
        if os.path.isfile(p):
            return p
    sys.exit('render_covers: no master %s.png or %s.jpg under %s' % (name, name, TILES))


def cover(im, w_out, h_out):
    w, h = im.size
    s = max(w_out / w, h_out / h)
    sw, sh = max(w_out, round(w * s)), max(h_out, round(h * s))
    scaled = im.resize((sw, sh), Image.LANCZOS)
    x0, y0 = (sw - w_out) // 2, (sh - h_out) // 2
    return scaled.crop((x0, y0, x0 + w_out, y0 + h_out)), (sw, sh, x0, y0)


def strip(im, w_out, h_out):
    w, h = im.size
    s = w_out / w
    sw, sh = max(w_out, round(w * s)), round(h * s)
    if sh < h_out:
        sys.exit('render_covers: %dx%d is too short for a %dx%d strip' % (w, h, w_out, h_out))
    scaled = im.resize((sw, sh), Image.LANCZOS)
    x0, y0 = (sw - w_out) // 2, (sh - h_out) // 2
    return scaled.crop((x0, y0, x0 + w_out, y0 + h_out)), (sw, sh, x0, y0)


def main():
    for name in NAMES:
        src = master_path(name)
        im = Image.open(src).convert('RGB')
        for w_out, h_full in SIZES:
            h_out = h_full // FRACTION[name]
            draw = cover if FRACTION[name] == 1 else strip
            out, (sw, sh, x0, y0) = draw(im, w_out, h_out)
            dst = os.path.join(TILES, '%s-%dx%d.png' % (name, w_out, h_out))
            out.save(dst, optimize=True)
            print('%s: %dx%d -> %dx%d, cut (%d, %d) -> %s' % (
                os.path.basename(src), im.size[0], im.size[1], sw, sh, x0, y0,
                os.path.basename(dst)))


if __name__ == '__main__':
    main()
