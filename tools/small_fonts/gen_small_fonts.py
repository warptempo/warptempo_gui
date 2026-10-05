#!/usr/bin/env python3
"""Write fonts/small_fonts_digits.otb, the reconstructed Small Fonts digits, from glyphs.py.

A one-strike bitmap OpenType font (OTB) shaped like the repository's Cronyx faces: empty glyf outlines, the pixels in
EBLC / EBDT (index format 3, image format 1: small metrics, byte-aligned rows), one strike at ppem 7, ascent 7,
descent 0. Pure fontTools (`pip install fonttools`). REGENERATE, NEVER HAND-EDIT the output; the glyph table and its
provenance are glyphs.py's.

    python3 tools/small_fonts/gen_small_fonts.py
"""

import io
import os
import sys

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.ttGlyphPen import TTGlyphPen

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from glyphs import CELL_ROWS, GLYPHS  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "fonts", "small_fonts_digits.otb")

UNITS_PER_PX = 128
UPEM = CELL_ROWS * UNITS_PER_PX
NAMES = {'0': "zero", '1': "one", '2': "two", '3': "three", '4': "four", '5': "five", '6': "six", '7': "seven",
         '8': "eight", '9': "nine", '.': "period", ':': "colon"}
NOTDEF_ADVANCE = 5


def ink_box(rows):
    """The lit pixels' bounding box: (first col, first row, width, height), or None for an empty glyph."""
    lit = [(x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c == '#']
    if not lit:
        return None
    x0 = min(x for x, _ in lit)
    x1 = max(x for x, _ in lit)
    y0 = min(y for _, y in lit)
    y1 = max(y for _, y in lit)
    return x0, y0, x1 - x0 + 1, y1 - y0 + 1


def glyph_xml(name, advance, rows):
    box = ink_box(rows) if rows else None
    if box is None:
        w = h = bx = by = 0
        data = ""
    else:
        x0, y0, w, h = box
        bx = x0
        by = CELL_ROWS - y0   # rows above the baseline to the box's top
        out = []
        for r in rows[y0:y0 + h]:
            bits = r[x0:x0 + w]
            v = 0
            for i, c in enumerate(bits):
                if c == '#':
                    v |= 0x80 >> i
            out.append("%02x" % v)
        data = " ".join(out)
    return (f'<ebdt_bitmap_format_1 name="{name}"><SmallGlyphMetrics>'
            f'<height value="{h}"/><width value="{w}"/><BearingX value="{bx}"/>'
            f'<BearingY value="{by}"/><Advance value="{advance}"/></SmallGlyphMetrics>'
            f'<rawimagedata>{data}</rawimagedata></ebdt_bitmap_format_1>')


def line_metrics(direction, width_max):
    return (f'<sbitLineMetrics direction="{direction}"><ascender value="{CELL_ROWS}"/><descender value="0"/>'
            f'<widthMax value="{width_max}"/><caretSlopeNumerator value="0"/><caretSlopeDenominator value="1"/>'
            '<caretOffset value="0"/><minOriginSB value="0"/><minAdvanceSB value="0"/><maxBeforeBL value="0"/>'
            '<minAfterBL value="0"/><pad1 value="0"/><pad2 value="0"/></sbitLineMetrics>')


def main():
    chars = sorted(GLYPHS, key=lambda c: ord(c))
    order = [".notdef"] + [NAMES[c] for c in chars]
    advances = {".notdef": NOTDEF_ADVANCE}
    advances.update({NAMES[c]: GLYPHS[c][0] for c in chars})

    fb = FontBuilder(UPEM, isTTF=True)
    fb.setupGlyphOrder(order)
    fb.setupCharacterMap({ord(c): NAMES[c] for c in chars})
    empty = TTGlyphPen(None).glyph()
    fb.setupGlyf({g: empty for g in order})
    fb.setupHorizontalMetrics({g: (advances[g] * UNITS_PER_PX, 0) for g in order})
    fb.setupHorizontalHeader(ascent=UPEM, descent=0)
    fb.setupNameTable({"familyName": "Small Fonts Digits", "styleName": "Regular",
                       "copyright": "Reconstructed pixel-exact from Windows Small Fonts as drawn by "
                                    "Sonic Foundry ACID Pro 3.0 and Vegas Audio (tools/small_fonts/)"})
    fb.setupOS2(sTypoAscender=UPEM, sTypoDescender=0, usWinAscent=UPEM, usWinDescent=0)
    fb.setupPost()
    font = fb.font

    width_max = max(advances.values())
    xml = ['<?xml version="1.0" encoding="UTF-8"?><ttFont sfntVersion="\\x00\\x01\\x00\\x00">',
           '<EBLC><header version="2.0"/><strike index="0"><bitmapSizeTable>',
           line_metrics("hori", width_max), line_metrics("vert", width_max),
           '<colorRef value="0"/><startGlyphIndex value="0"/>',
           f'<endGlyphIndex value="{len(order) - 1}"/><ppemX value="{CELL_ROWS}"/><ppemY value="{CELL_ROWS}"/>',
           '<bitDepth value="1"/><flags value="1"/></bitmapSizeTable>',
           f'<eblc_index_sub_table_3 imageFormat="1" firstGlyphIndex="0" lastGlyphIndex="{len(order) - 1}">']
    xml += [f'<glyphLoc id="{i}" name="{g}"/>' for i, g in enumerate(order)]
    xml.append('</eblc_index_sub_table_3></strike></EBLC>')
    xml.append('<EBDT><header version="2.0"/><strikedata index="0">')
    xml.append(glyph_xml(".notdef", NOTDEF_ADVANCE, None))
    xml += [glyph_xml(NAMES[c], GLYPHS[c][0], GLYPHS[c][1]) for c in chars]
    xml.append('</strikedata></EBDT></ttFont>')
    font.importXML(io.StringIO("".join(xml)))
    font.save(OUT)
    print(OUT)


if __name__ == "__main__":
    main()
