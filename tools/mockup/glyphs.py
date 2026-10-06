#!/usr/bin/env python3
# tools/mockup/glyphs.py — AN ICON SET'S GLYPHS INTO THE CASES: <Enumerator>.svg rasterised by rsvg-convert at the
# app's own size (the 16-unit cell at gui_scale: 4 device px a unit at 400 %), seated at the case's fixed
# (kIconCaseLeadPx, kIconCaseLeadPx) Windows px plus the lit case's one-line shift (icons.h's PLACEMENT,
# paint_button_box's ButtonBoxFace), over the case's face in the target theme (the checkerboard on a lit case).
# An enabled glyph is the drawing's own inks; a disabled one is the emboss (icons.cpp's draw_engraved): the disabled
# mask — every ink path opaque, every White or Silver path cleared, in file order — painted in Hilight one relief
# line right and down, then in Shadow at the glyph's own place. The mask is rasterised from the same file with its
# fills rewritten (ink -> black, White / Silver -> white, over white): one minus that picture is the mask, which is
# the CLEAR operator's arithmetic (a later path's coverage c takes the mask to m(1 - c), or m(1 - c) + c for ink).
import os, re, subprocess, struct, sys, zlib
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'palette'))
import pngrw                                            # noqa: E402  (the palette tool's PNG reader)

_BACKGROUND_INKS = ('#ffffff', '#c0c0c0', 'white', 'silver')


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


def _mask_svg(svg):
    def fill(m):
        v = m.group(2).lower()
        return m.group(1) + ('#FFFFFF' if v in _BACKGROUND_INKS else '#000000') + m.group(3)
    return re.sub(r'(fill=")([^"]+)(")', fill, svg)


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

    def mask(self, name):
        """The disabled mask, float HxW in [0, 1]."""
        k = ('m', name)
        if k not in self._cache:
            a = _rsvg(_mask_svg(open(self.path(name)).read()), self.size, background='white').astype(np.float64)
            self._cache[k] = 1.0 - a[..., 0] / 255.0
        return self._cache[k]


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
    if case.disabled:
        m = icons.mask(name)
        for dx, ink in ((U, TT['hilight']), (0, TT['shadow'])):
            reg = out[gy + dx:gy + dx + n, gx + dx:gx + dx + n]
            reg[:] = _over(reg, np.array(ink, np.float64), m[:reg.shape[0], :reg.shape[1]])
    else:
        rgb, a = icons.colour(name)
        reg = out[gy:gy + n, gx:gx + n]
        reg[:] = _over(reg, rgb[:reg.shape[0], :reg.shape[1]], a[:reg.shape[0], :reg.shape[1]])
