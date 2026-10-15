# icon_set_adwaita_322

Builds `assets/icons/adwaita-3.22/` (architect 2026-10-14) from the adwaita-icon-theme 3.22.0 tarball
(https://download.gnome.org/sources/adwaita-icon-theme/3.22/, sha256
`c18bf6e26087d9819a962c77288b291efab25d0419b73d909dd771716a45dcb7`), extracted:

    python3 -I tools/icon_set_adwaita_322/build_set.py <tarball-root> <out-dir>

`<tarball-root>` is the extracted `adwaita-icon-theme-3.22.0/` folder. `mapping.tsv` is the set: one row per
`icons::Icon` enumerator, its freedesktop name, its sheet under `src/fullcolor/`, the 48 slot's rect in the sheet's
user units and its treatment (`-`, `tab-b`, `text-to-outlines`). `shadows.tsv` is the drop-shadow list (architect
2026-10-15, "we'll go with none"; approved icon by icon on a sheet): one row per enumerator, the ids of the seat's drop
shadows (`-` none). `extract.py` prunes the sheet to the slot and removes the listed shadows (`prune_shadows`: each id
must exist; every `<use>` of a removed element goes with it; a reference left to a removed element fails the build)
before pruning the defs, `text_to_paths.py` turns VcsDiff's clock digits into Liberation Sans Bold outlines, `build_set.py` drives both, derives
TabDetachB, copies the licence texts and writes `README_table.md`. Each seat prints the shadow elements removed and
the check of its faithful drawing (shadows kept, written to the scratch folder alone) against the sheet cropped to the
slot and against the theme's shipped 48 PNG. The output is byte-identical to the shipped set; compare with `cmp`.
THE SHADOW PROOF (2026-10-15): every regenerated file rendered by `rsvg-convert` at 192 px is byte-identical (`cmp`
of the PNGs) to the copy the architect approved, made by removing the same ids from the files shipped before.
The README of the set itself (`assets/icons/adwaita-3.22/README.md`) is hand-written around the table.

Needs python3 with numpy, Pillow and fontTools, and `rsvg-convert`.
