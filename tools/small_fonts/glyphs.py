# THE RECONSTRUCTED SMALL FONTS DIGITS (architect 2026-10-05): Windows' Small Fonts as Sonic Foundry ACID Pro 3.0
# and Vegas Audio drew their timeline numbers, read PIXEL-EXACT off the architect's own screenshots of ACID Pro 3.0's
# level meter and ruler and Vegas Audio's ruler (Windows, 96 dpi). Only the characters the ruler's labels use are
# recorded: 0-9 '.' ':' (ruler_label_text, src/gui/paint_handler.cpp). Nothing here is interpolated or drawn by hand
# beyond what the captures show.
#
# THE CELL: 7 rows, every row ABOVE the baseline (the bottom row's foot is the baseline; no descent). The digits are
# TABULAR, advance 5 = 4 ink columns + 1 gap ('1' narrow inside its cell); '.' and ':' advance 2. Each row string is
# top to bottom, '#' a lit pixel, '.' an unlit one.
#
# REGENERATE, NEVER HAND-EDIT the face this writes (fonts/small_fonts_digits.otb): gen_small_fonts.py.
GLYPHS = {
    '0': (5, [".##.", "#..#", "#..#", "#..#", "#..#", "#..#", ".##."]),
    '1': (5, ["..#.", ".##.", "..#.", "..#.", "..#.", "..#.", "..#."]),
    '2': (5, [".##.", "#..#", "...#", "..#.", ".#..", "#...", "####"]),
    '3': (5, [".##.", "#..#", "...#", "..#.", "...#", "#..#", ".##."]),
    '4': (5, ["..#.", ".##.", ".##.", "#.#.", "####", "..#.", "..#."]),
    '5': (5, ["####", "#...", "###.", "#..#", "...#", "#..#", ".##."]),
    '6': (5, [".##.", "#..#", "#...", "###.", "#..#", "#..#", ".##."]),
    '7': (5, ["####", "...#", "..#.", "..#.", ".#..", ".#..", ".#.."]),
    '8': (5, [".##.", "#..#", "#..#", ".##.", "#..#", "#..#", ".##."]),
    '9': (5, [".##.", "#..#", "#..#", ".###", "...#", "#..#", ".##."]),
    '.': (2, ["..", "..", "..", "..", "..", "..", "#."]),
    ':': (2, ["..", "..", "..", "#.", "..", "..", "#."]),
}

CELL_ROWS = 7   # the strike's ppem and its ascent; the descent is 0
