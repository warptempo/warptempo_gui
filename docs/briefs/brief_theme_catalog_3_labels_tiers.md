# Brief: the theme catalog, step 3 — recorded flag labels, the Windows error red, the display tier, and mock set AI (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, run only read-only git, never stage or commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md, never touch src/). THIS BRIEF TOUCHES ONLY tools/palette/, tools/theme_catalog/ and docs/themes/ (regenerated). HEAD is 3d6ba972 (step 2: docs/briefs/brief_theme_catalog_2_flags_well.md). Read tools/palette/README.md and tools/theme_catalog/README.md.

## The rulings (architect 2026-10-03, late)

- THE LUMINANCE RULE RETIRES FOR PROGRAM ELEMENTS: it is WCAG 2.0's 2008 contrast math, not Windows; Windows 95 recorded a text colour beside every face (ButtonFace / ButtonText, Hilight / HilightText). So a bevelled flag's label is a RECORDED colour: `flag_label` for a normal flag's face (the app's default is black, kMarkerFlagLabel), and a new role `flag_label_red` for an invalid flag.
- THE INVALID FLAG IS WINDOWS' ERROR-ICON PAIR: the face VGA bright red #FF0000, the label white #FFFFFF (the Stop icon's white X on red). `flag_fill_red`'s default becomes #FF0000 in the bevelled style's mocks and crops (the app style's parity records keep theirs); its bevel by the family rule as before.
- THE PLAYHEAD IS A PROGRAM ELEMENT, like the flags: its head and stem are program colours (`playhead_head`, `playhead_stem`), not theme roles; the crops and mocks use the app's #8B8B8B head and #FCFCFC stem (the step-2 mapping head <- bevel_shadow and stem <- label retire).
- THE DISPLAY TIER: each catalog entry is tagged by the smallest period colour set holding every colour its roles use: `vga` (the 16 VGA colours), `windows-20` (those plus Windows' four static extras #C0DCC0, #A6CAF0, #FFFBF0, #A0A0A4, always solid on a 256-colour display), else `high-colour`. Today: Windows Storm, Teal and Red, White, and Blue are `vga`; the other 120 are `high-colour` (verify by your own computation). The tier is one line in each entry's CATALOG.md block and a field in catalog.json; the README states the sets.

## Part A — the renderer (tools/palette/render.py)

A1. `flags.style "bevelled"`: the label ink is `flag_label` on a normal face and `flag_label_red` on an invalid face (selected keeps its own face's label; disabled stays engraved). `flag_label_red` defaults to #FFFFFF. Remove the luminance call from the bevelled path; `highlight_text_ink` stays only where the app's own chrome rule still reads it (the app brief retires it there).
A2. README rows. ad2, win95_standard and frozen still render byte-identical (compare.py, 0 pixels).

## Part B — the catalog (tools/theme_catalog/)

B1. The tier field, its computation in build.py with an assert (the three `vga` entries above), the CATALOG.md line, the README.
B2. crops.py: the playhead as program colours (#8B8B8B / #FCFCFC), the invalid flag #FF0000 with the white label, the normal flag's label black. Regenerate the crops; report the total.

## Part C — mock set AI (renders only, never committed): THE PROGRAM COLOURS

One axis: the program's own colours (waveform canvas, ink and the lamp's inner-bar outline; the flag face and label; the playhead head and stem), on two catalog entries over ad2.json's geometry with step 2's well and bevelled flags and the four flag states (left to right: unselected, selected, invalid, disabled). Eight full-screen mocks into tmp/colour_arc2/ai/, `--label`, named mock_AI<n>_<theme>_<variant>.png, n 1–4 on `warptempo-2026-10-03` and 5–8 on `windows-95-standard`, the same four variants in the same order:

| variant | canvas | ink | outline | flag face / label | playhead head / stem |
|---|---|---|---|---|---|
| `today` | #141618 | #96BFDA | #6E8DA1 | #8A5EAC / #000000 | #8B8B8B / #FCFCFC |
| `soundrecorder` (Windows Sound Recorder's green on black, VGA) | #000000 | #00FF00 | #000000 | #FFFF00 / #000000 | #808080 / #FFFFFF |
| `solid` (today's look in Windows' 20 solid colours: #A6CAF0 is the solid nearest today's ink) | #000000 | #A6CAF0 | #000000 | #FF00FF / #000000 | #A0A0A4 / #FFFFFF |
| `paper` (Acid / Vegas: a VGA wave on white) | #FFFFFF | #008080 | #FFFFFF | #800080 / #FFFFFF | #808080 / #000000 |

The invalid flag is #FF0000 / #FFFFFF in every variant. If the scene's waveform does not show the lamp's outline, say so (the outline column then changes nothing). The planner pushes the set to the tablet.

## Discipline and report

Comments at the owner with "architect 2026-10-03". Commit nothing. Report: files changed, the tier counts, the crop total, compare.py results, the AI mock paths, and anything he might want to rule on.
