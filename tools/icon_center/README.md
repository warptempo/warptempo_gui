# icon_center

`gen_icon_shifts.py` writes `src/gui/icon_shifts.inc`: for each glyph of the one set,
Adwaita 3.22 (`assets/icons/adwaita-3.22/`, architect 2026-10-14), in `kIconNames` order,
the translation, in the SVG's 48-unit user space, that puts the glyph's body at the
center of its canvas (architect 2026-10-12). The body is the
shipped file itself: every drop shadow is removed structurally at the set's extraction
(`tools/icon_set_adwaita_322/shadows.tsv`, architect 2026-10-15), so the drawing is
shadow-less by construction; its alpha >= 0.1 bounds are measured at 240 px.

Run: `python3 tools/icon_center/gen_icon_shifts.py`

Needs python3 with numpy and Pillow, and `rsvg-convert`. The SVG files are never modified.
