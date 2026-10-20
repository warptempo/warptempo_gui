# icon_set_adwaita_322

Builds the Adwaita 3.22 drawings of `assets/icons/` (architect 2026-10-14; one flat folder, each file named by its
act, since 2026-10-19 — `src/gui/icons.h`'s `kIconNames`) from the adwaita-icon-theme 3.22.0 tarball
(https://download.gnome.org/sources/adwaita-icon-theme/3.22/, sha256
`c18bf6e26087d9819a962c77288b291efab25d0419b73d909dd771716a45dcb7`), extracted:

    python3 -I tools/icon_set_adwaita_322/build_set.py <tarball-root> <out-dir>

`<tarball-root>` is the extracted `adwaita-icon-theme-3.22.0/` folder. `mapping.tsv` is the set: one row per
extracted file, keyed by its `icons::Icon` enumerator (the act's name, the file's stem), its freedesktop name, its sheet
under `src/fullcolor/`, the 48 slot's rect in the sheet's user units and its treatment (`-`, `text-to-outlines`).
THE TOOL WRITES SVGs AND THE LICENCE TEXTS ONLY: never a PNG — the architect's own drawings are dropped into
`assets/icons/` by hand (the drop road, architect 2026-10-19, `src/gui/icons.h`'s head; the `own` treatment that
copied Warp's baton retired with it) — and never a symlink: a drawing several acts wear is ONE row, the canonical
file, and the folder carries the others as links (`AppIcon.png` -> `Warp.png`, `BpmIterations.svg` -> `WavRow.svg`,
`CardCritical.svg` -> `Cancel.svg`, `UpAFolder.svg` -> `FolderRow.svg`, `TransportPlay.svg` -> `Play.svg`). `text-to-outlines` is the history clock's
slot, `document-open-recent`, with its digits turned into Liberation Sans Bold outlines (HistoryView, history's again
since 2026-10-18; its dated notice names the file as it was named when written, `DocumentOpenRecent`, kept so the
bytes stand). (`tab-b`, which derived TabDetachB by element order for Switch Tab's tab-B face, left with that file
2026-10-18: Switch Tab wears its one drawing in both tabs.)
`shadows.tsv` is
the drop-shadow list (architect 2026-10-15, "we'll go with none"; approved icon by icon on a sheet): one row per
extracted file, the ids of the seat's drop shadows (`-` none). `removed.tsv` lists the OTHER
elements the architect rules out of a drawing (optional rows, the same columns, removed with the shadows and
composing with any treatment; one row today: ValueDrag's arrow group `g7348`, the pen alone, 2026-10-18). `extract.py` prunes the sheet to the slot and removes both lists' ids
(`prune_shadows`: each id must exist; every `<use>` of a removed element goes with it; a reference left to a removed
element fails the build) before pruning the defs; `text_to_paths.py` turns the clock's digits
(the `text-to-outlines` row) into Liberation Sans Bold outlines from `fonts/LiberationSans-Bold.ttf`;
`body_shift.py` measures each written SVG's body-centering shift as the 2026-10-12 table did (rsvg-convert at 240
px, the alpha >= 0.1 bounds, rounded to 0.1 unit) and inserts it on the root tag as `data-body-shift="dx dy"`, nothing
else in the file touched — the product reads it instead of rendering at launch (architect 2026-10-19; resvg ignores
it); `build_set.py` drives all three, writes the clock's dated notice, copies the
licence texts and writes `README_table.md` (the shift its own column), which the folder's README carries as its table. Each seat prints the shadow and other elements removed and the
check of its faithful drawing (nothing removed, written to the scratch folder alone) against the sheet cropped to the
slot and against the theme's shipped 48 PNG. Every SVG of the output is byte-identical to the folder's file of the
same name; compare with `cmp` — the folder's PNGs and links are not
the tool's.
THE SHADOW PROOF (2026-10-15): every regenerated file rendered by `rsvg-convert` at 192 px is byte-identical (`cmp`
of the PNGs) to the copy the architect approved, made by removing the same ids from the files shipped before.
The folder's README (`assets/icons/README.md`) is hand-written around the table.

Needs python3 with numpy, Pillow and fontTools, and `rsvg-convert`.
