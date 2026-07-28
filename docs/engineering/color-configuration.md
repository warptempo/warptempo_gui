# Color configuration (`~/.config/warptempo_gui/colors.conf`)

Moved out of docs/HELP.md 2026-07-27 (architect): the canonical key block is a maintenance
surface that must mirror the compiled defaults in `src/gui/render.h`, not user-facing reference
prose. HELP keeps a one-line pointer here. `src/gui/render.h`'s palette block and
`src/gui/color_config.cpp`'s key table remain authoritative for provenance and for the
enumeration itself; this file is the human-readable grammar plus the copyable block.

Every color the GUI paints is tunable through one file: `~/.config/warptempo_gui/colors.conf`, read once at startup (edit it, then relaunch — there is no live reload). The color words in this manual describe the shipped defaults, a Plasma 6 Breeze Dark scheme.

The file is strict in the same spirit as the settings sidecars: it must contain exactly one line per color key, in a fixed canonical order, each line spelled `key=#rrggbb` with lowercase hex and no spaces anywhere, nothing else — no comments, no blank lines. A missing file is normal and silently keeps the compiled defaults; any deviation in a present file prints one stderr line naming the offending line and reason, rejects the whole file, and keeps the compiled defaults — never a partial adoption, never a failed launch. The program never writes the file itself; deleting it is always a safe way back to the defaults, and this block IS the canonical file at the shipped defaults — copy it verbatim to start tuning:

```
background=#202326
canvas=#393e43
waveform_ink=#141618
text=#fcfcfc
text_disabled=#606263
line=#686a6c
strip_anchor_stem=#686a6c
playhead_cursor=#7f8c8d
playhead_scanner=#fcfcfc
selected_stem=#7f8c8d
marker=#264a5e
marker_outline=#3895c7
marker_disabled=#164160
marker_disabled_outline=#42464a
accent_red=#59262d
accent_red_outline=#da4453
region_canvas=#42474d
overlay_outline=#7f8c8d
trim_bar=#264a5e
trim_bar_outline=#3895c7
trim_chip=#202326
trim_chip_outline=#686a6c
trim_stem=#686a6c
```

The keys name their surfaces: the two grounds (`background` chrome, `canvas` waveform area) and the ink; text and its disabled shade; the one structural `line` color (strip-row ring and the waveform border) and the strip-drag anchor stem; the two playheads; the selected marker's stem — selection has no color of its own, a selected marker painting exactly the pair an unselected one does, so the stem is the whole cue, and it is its own key because a full-height line carries a color differently than a 1px flag ring; the marker pair, its disabled pair, and the red-flag pair; the region highlight's background lift and the phase overlay's outline (the overlay is a 1px frame around its span — it recolors nothing inside); and the trim family — the bright bar pair, the calm chip pair, and the stems. Every value is opaque — there are no alpha channels: highlights recolor the background under the waveform ink rather than washing over it, and disabled markers use their own opaque pair rather than fading. The CLI renderer never reads this file; colors are display-only.
