# icon_center

`gen_icon_shifts.py` writes `src/gui/icon_shifts.inc`: for each GNOME 2.28 glyph
(in `kIconNames` order) the translation, in the SVG's 48-unit user space, that puts
the glyph's body at the center of its canvas (architect 2026-10-12). The body is the
drawing with shadows removed structurally (blur-filtered elements, radial gradients
fading to transparent, ids/labels containing "shadow"); its alpha >= 0.1 bounds are
measured at 240 px.

Run: `python3 tools/icon_center/gen_icon_shifts.py`

Needs python3 with numpy and Pillow, and `rsvg-convert`. The SVG files are never modified.
