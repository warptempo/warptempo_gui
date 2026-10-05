# tools/small_fonts — the reconstructed Small Fonts digits

The ruler's labels are Windows' Small Fonts as Sonic Foundry ACID Pro 3.0 and Vegas Audio drew their timeline numbers (architect 2026-10-05), read pixel-exact off his screenshots of ACID Pro 3.0's level meter and ruler and Vegas Audio's ruler. `glyphs.py` holds the table and its provenance: 0-9 . : only (what `ruler_label_text`, `src/gui/paint_handler.cpp`, emits), a 7-row cell all above the baseline, the digits tabular at advance 5, '.' and ':' at 2.

`gen_small_fonts.py` writes `fonts/small_fonts_digits.otb`, a one-strike bitmap OpenType font (empty glyf outlines, the pixels in EBLC / EBDT at ppem 7, ascent 7, descent 0), with pure fontTools (`pip install fonttools`). A standalone utility: no link path from any product target.

```
python3 tools/small_fonts/gen_small_fonts.py
```

REGENERATE, NEVER HAND-EDIT the font: a glyph changes in `glyphs.py`, then the generator runs.
