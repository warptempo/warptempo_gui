# icon_set_adwaita_322

Builds `assets/icons/adwaita-3.22/` (architect 2026-10-14) from the adwaita-icon-theme 3.22.0 tarball
(https://download.gnome.org/sources/adwaita-icon-theme/3.22/, sha256
`c18bf6e26087d9819a962c77288b291efab25d0419b73d909dd771716a45dcb7`), extracted:

    python3 -I tools/icon_set_adwaita_322/build_set.py <tarball-root> <out-dir>

`<tarball-root>` is the extracted `adwaita-icon-theme-3.22.0/` folder. `mapping.tsv` is the set: one row per
`icons::Icon` enumerator, its freedesktop name, its sheet under `src/fullcolor/`, the 48 slot's rect in the sheet's
user units and its treatment (`-`, `tab-b`, `text-to-outlines`, `own`). THE SET'S ONE OWN FILE (architect 2026-10-16):
`own/DigitalMetronome.svg`, the digital metronome, the architect's own drawing and NOT Adwaita's (the set's README its
provenance and licence) — an `own` row names it in the sheet column (name and slot `-`) and is COPIED, not extracted:
its bytes verbatim but for a dated notice inserted as one XML comment after the root's start tag, no tarball check;
DigitalMetronome and AppIcon (the program icon, `tools/app_icon/`) are both that copy. Its lettering is already
outlines: `text_to_paths.py` turned the approved drawing's one `<text>` ("WARP", Gemini's family list resolved to the
first face the tool has, DejaVu Sans, weight 900 to its Bold, the letter-spacing and the element's own transform
carried) into DejaVu Sans Bold outlines from `fonts/DejaVuSans-Bold.ttf`, measured against rsvg's own text rendering of
the approved file at 0.001/255 mean at 240 px and 0.000 at 60; the defs the cleaned drawing no longer references (its
two drop-shadow filters, the pill highlight, the round and power caps' radials) were pruned with it. `shadows.tsv` is
the drop-shadow list (architect 2026-10-15, "we'll go with none"; approved icon by icon on a sheet): one row per
enumerator, the ids of the seat's drop shadows (`-` none; an `own` row always `-`). `removed.tsv` lists the OTHER
elements the architect rules out of a drawing (optional rows, the same columns, removed with the shadows and
composing with any treatment; no row today). `extract.py` prunes the sheet to the slot and removes both lists' ids
(`prune_shadows`: each id must exist; every `<use>` of a removed element goes with it; a reference left to a removed
element fails the build) before pruning the defs; `text_to_paths.py` turns the history clock's digits
(DocumentOpenRecent, the `text-to-outlines` row; architect 2026-10-16, "give history the clock with backarrow back")
into Liberation Sans Bold outlines from `fonts/LiberationSans-Bold.ttf`, with its own dated notice; `build_set.py`
drives both, derives TabDetachB, copies the own file,
copies the licence texts and writes `README_table.md`. Each seat prints the shadow and other elements removed and the
check of its faithful drawing (nothing removed, written to the scratch folder alone) against the sheet cropped to the
slot and against the theme's shipped 48 PNG. The output is byte-identical to the shipped set; compare with `cmp`.
THE SHADOW PROOF (2026-10-15): every regenerated file rendered by `rsvg-convert` at 192 px is byte-identical (`cmp`
of the PNGs) to the copy the architect approved, made by removing the same ids from the files shipped before.
The README of the set itself (`assets/icons/adwaita-3.22/README.md`) is hand-written around the table.

Needs python3 with numpy, Pillow and fontTools, and `rsvg-convert`.
