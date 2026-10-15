# icon_set_adwaita_322

Builds `assets/icons/adwaita-3.22/` (architect 2026-10-14) from the adwaita-icon-theme 3.22.0 tarball
(https://download.gnome.org/sources/adwaita-icon-theme/3.22/, sha256
`c18bf6e26087d9819a962c77288b291efab25d0419b73d909dd771716a45dcb7`), extracted:

    python3 -I tools/icon_set_adwaita_322/build_set.py <tarball-root> <out-dir>

`<tarball-root>` is the extracted `adwaita-icon-theme-3.22.0/` folder. `mapping.tsv` is the set: one row per
`icons::Icon` enumerator, its freedesktop name, its sheet under `src/fullcolor/`, the 48 slot's rect in the sheet's
user units and its treatment (`-`, `tab-b`, `text-to-outlines`). `extract.py` prunes the sheet to the slot,
`text_to_paths.py` turns VcsDiff's clock digits into Liberation Sans Bold outlines, `build_set.py` drives both, derives
TabDetachB, copies the licence texts and writes `README_table.md`. Each seat prints its check against the sheet cropped
to the slot and against the theme's shipped 48 PNG. The output is byte-identical to the shipped set; compare with
`cmp`. The README of the set itself (`assets/icons/adwaita-3.22/README.md`) is hand-written around the table.

Needs python3 with numpy, Pillow and fontTools, and `rsvg-convert`.
