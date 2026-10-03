# Brief: the theme catalog, step 12 — the proportional field, card and selection at dim / dark; the white-outlined selected flag; mock set AW, the held trim cap's line (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, may build build/ to self-check, run only read-only git, never stage or commit, never launch the GUI, never build the APK, never run scripts/warptempo_sync in any form, never edit CLAUDE.md). HEAD is 175c9900. Nothing frozen is touched. The app's theme work: docs/briefs/brief_theme_catalog_app.md; the last mock step: docs/briefs/brief_theme_catalog_11_fields_selection.md (its generators tmp/colour_arc2/{ar,as,at,av}/make_*.py over tmp/colour_arc2/step11_common.py are the reference arithmetic). Rulings land as comments at their owners with the date (architect 2026-10-03); what they retire gets a closed_questions.md line.

## Part 1 — the levels: field, card and selection darken in proportion (tools/theme_catalog/levels.py, gen_theme_table.py, src/gui/theme_table.h regenerated)

THE RULING (architect 2026-10-03, on mock sets AS and AT: AS02 picked for dark, "darkened in proportion" for dim as well, and "proportionally darken" the highlight): on DIM and DARK, for EVERY entry, three grounds darken with the face by the ground's own ratio —
- the FIELD ground, the INFO (card / tooltip) ground and the SELECTED fill each move to relative luminance = their recorded luminance × L(the level's ground) / L(the entry's recorded ground), HUE AND HSV SATURATION KEPT (step11_common.py's `at_luminance_hsv`: HLS saturation turned #FFFFE1 olive; the ground itself keeps its ruled HLS rule, unchanged);
- the TEXT on each of the three takes the level's label (#FFFFFF), the dark level's rule as mock AS02 showed it; one rule on both levels (planner's call for symmetry, the mocks having shown dim only with black text: state it at the owner so he can overrule on the glass).
- LIGHT is unchanged (the entry as recorded). The CDE selected pair (title_active with its Motif foreground) darkens like any other selected fill. The emboss light copy, the relief and the label are unchanged.
Verify: AS02's bytes (field #494949, card #494940) come out of the table for windows-95-standard dark; the generator stays byte-stable (two runs, cmp); levels.py --verify still passes. Report windows-95-standard's three pairs at dim and dark with contrasts, and list any entry whose darkened selected fill falls below 1.1 : 1 against its own level's ground (information only; he accepts "a little off").

## Part 2 — the selected flag is a WHITE OUTLINE (src/gui/render.{h,cpp} and the flag painters)

THE RULING (architect 2026-10-03, mock AR02): a SELECTED flag (and a selected run's ADDRESSED cell, the one the underline marked) shows its one-Windows-px outline ring in WHITE #FFFFFF in place of the DkShadow ring; face, label, stem, geometry unchanged — the red invalid frame of the in-place editor is its sibling. THE UNDERLINE RETIRES (the white outline replaces it); delete text_shape's underline metrics if nothing else reads them. White is a constant beside the palette (a settled design), not a key; say at the owner that on a light theme it stands on a light ground. Cover the bound cells' shared seam columns (the white ring must be whole on four sides, as the red frame is), the history view's flags, a selected disabled flag, and the damage rects. closed_questions: the underlined selected flag (retired, 2026-10-03, after a day on the glass), the navy-filled selected flag (AR03 / AR04: the navy face vanishes on the dark ground).

## Part 3 — mock set AW, the held trim cap's line (tools/palette/render.py options only; renders under tmp/colour_arc2/aw/)

THE RULING (architect 2026-10-03): the held cap STAYS FLAT (Windows' pushed scroll arrow: the sunken face "violates the trough" — a button pressed further into the trough it sits in); only its one-line ring is too faint at dark (#232323 on #353535). Mock ways to bring the ring out, windows-95-standard DARK, the begin cap held, everything else as mock AV01, full-screen with `--label`, named mock_AW<nn>_<what>.png:
AW01 today: the ring in Shadow (the reference).
AW02 the ring in DkShadow (#000000).
AW03 the ring in Hilight (the light line, #464646).
AW04 the ring in the emboss light copy (the dark level's dialog-rule Hilight).
AW05 the ring in the label white (the selected flag's white outline, mirrored).
Print each ring's contrast against the face and against the trough's dither colours. Any option the renderer lacks is added default-off (ad2, win95_standard, frozen byte-identical, compare.py 0 px). The app's held cap is NOT changed.

## Verification and report

build/ to exit 0, no new warnings; the generator checks above; grep that no reader of a retired name remains. Report: files changed, the pairs table, the low-contrast list, the white-outline sites, the AW paths with contrasts, and anything for him to rule on. Commit nothing.
