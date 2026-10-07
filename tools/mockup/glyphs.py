#!/usr/bin/env python3
# tools/mockup/glyphs.py — AN ICON SET'S GLYPHS INTO THE CASES: <Enumerator>.svg rasterised by rsvg-convert at the
# app's own size (the case's glyph seat at gui_scale, whatever the drawing's own cell: the Tango set's 48 units),
# seated at the case's fixed (kIconCaseLeadPx, kIconCaseLeadPx) Windows px plus the lit case's one-line shift
# (icons.h's PLACEMENT, paint_button_box's ButtonBoxFace), over the case's face in the target theme (the checkerboard
# on a lit case). An enabled glyph is the drawing's own picture; a disabled one is ReactOS's saturate (icons.h's
# draw_disabled, svg_icon.cpp's saturated_copy): each pixel's colour its .30 R + .59 G + .11 B luminance, its alpha
# times 192 / 255. rsvg-convert is the reference a set is checked against at its import; the product rasterises through
# resvg (src/gui/svg_icon.h), a median 0.5 levels of 255 from rsvg at 72 px and 1.6 at 33 over the Tango set, its
# thinnest strokes a shade softer at the laptop's size.
import os, subprocess, struct, sys, zlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'palette'))
import pngrw                                            # noqa: E402  (the palette tool's PNG reader)

def _png_rgba(data):
    """An 8-bit RGB or RGBA PNG (rsvg-convert's output) held in bytes -> HxWx4 uint8."""
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit('tools/mockup: rsvg-convert wrote no PNG')
    pos, ihdr, idat = 8, None, b''
    while pos < len(data):
        n = struct.unpack('>I', data[pos:pos + 4])[0]
        name, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        if name == b'IHDR': ihdr = body
        elif name == b'IDAT': idat += body
        pos += 12 + n
    w, h, depth, ctype, _, _, interlace = struct.unpack('>IIBBBBB', ihdr)
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise SystemExit(f'tools/mockup: rsvg-convert wrote a PNG of depth {depth}, colour type {ctype}')
    bpp = 4 if ctype == 6 else 3
    px = pngrw._unfilter(zlib.decompress(idat), h, w, bpp).reshape(h, w, bpp)
    if bpp == 3:
        px = np.concatenate([px, np.full((h, w, 1), 255, np.uint8)], axis=2)
    return px


def _rsvg(svg_text, size, background=None):
    cmd = ['rsvg-convert', '-w', str(size), '-h', str(size)]
    if background:
        cmd += ['-b', background]
    r = subprocess.run(cmd + ['-'], input=svg_text.encode(), capture_output=True, check=True)
    return _png_rgba(r.stdout)


class IconSet:
    def __init__(self, folder, size):
        self.folder, self.size = folder, size
        self._cache = {}

    def path(self, name):
        p = os.path.join(self.folder, name + '.svg')
        if not os.path.isfile(p):
            raise SystemExit(f'tools/mockup: --icons {self.folder}: no {name}.svg')
        return p

    def colour(self, name):
        """(rgb float HxWx3, alpha float HxW) of the drawing in its own inks."""
        k = ('c', name)
        if k not in self._cache:
            a = _rsvg(open(self.path(name)).read(), self.size).astype(np.float64)
            self._cache[k] = (a[..., :3], a[..., 3] / 255.0)
        return self._cache[k]

    def saturated(self, name):
        """(rgb, alpha) of the disabled face: the luminance in every channel, the alpha at 192 / 255."""
        rgb, a = self.colour(name)
        lum = rgb[..., 0] * 0.30 + rgb[..., 1] * 0.59 + rgb[..., 2] * 0.11
        return np.repeat(lum[..., None], 3, axis=2), a * (192.0 / 255.0)


def _over(dst, rgb, alpha):
    """dst (uint8) under a colour of coverage alpha, rounded to the byte."""
    out = dst.astype(np.float64) * (1.0 - alpha[..., None]) + rgb * alpha[..., None]
    return np.clip(np.floor(out + 0.5), 0, 255).astype(np.uint8)


def draw_into_case(out, case, x0, y0, name, icons, U, lead, TT, background):
    """Replace the case's interior at (x0, y0) in `out` by its face and the glyph `name`."""
    ix0, iy0 = x0 + 2 * U, y0 + 2 * U
    bh, bw = background.shape[:2]
    out[iy0:iy0 + bh, ix0:ix0 + bw] = background
    s = U if case.checked else 0
    gx, gy, n = x0 + lead * U + s, y0 + lead * U + s, icons.size
    rgb, a = icons.saturated(name) if case.disabled else icons.colour(name)
    reg = out[gy:gy + n, gx:gx + n]
    reg[:] = _over(reg, rgb[:reg.shape[0], :reg.shape[1]], a[:reg.shape[0], :reg.shape[1]])
