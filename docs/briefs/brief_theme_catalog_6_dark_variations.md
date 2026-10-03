# Brief: the theme catalog, step 6 — the underlined selected flag, the playhead head's outline, and mock set AL (dark variations of the themes he likes) (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, run only read-only git, never stage or commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md, never touch src/). THIS BRIEF TOUCHES ONLY tools/palette/ (render.py, README.md) and scratch under tmp/colour_arc2/al/. Do not regenerate docs/themes/ (internal iteration). HEAD is 940907ed (step 5: docs/briefs/brief_theme_catalog_5_flat_flags.md, generator tmp/colour_arc2/ak/make_ak.py). Read tools/palette/README.md and tools/theme_catalog/toolkit_rules.py.

## The rulings (architect 2026-10-03, late)

1. THE SELECTED FLAG IS UNDERLINED: the label's text underlined, nothing else changes (the outline stays the dark one, no nudge, no width or height change). Underline is period: Windows 95 underlined every menu and button accelerator letter. Draw it at Roboto's own underline position and thickness from the face's `post` table (underlinePosition, underlineThickness) at the label's size, rounded to whole device px at the element, in the label's colour, through descenders as Windows did (no skip-ink); it spans the shaped text's advance. Report the rows it lands on and whether it stays inside the face above the bottom outline at every label in the scene. If Roboto's own position would cross the outline, report it rather than move the box.
2. THE PLAYHEAD HEAD gets a ONE-Windows-px OUTLINE in the theme's `label` colour (the chrome's text colour, black on light chrome, white on dark), its fill the program grey #8B8B8B as before; the stem uniform #FCFCFC.
3. THE EDITING FLAG keeps the field strip either side of its selection (Windows' edit-field margin) and its stem in the flag colour. Kept as built in step 5.

## Part A — the renderer

A1. The underline as the flat style's selected state (a new option value or state handling inside `flags.style "flat"`; the white-outline selected state of step 5 stays selectable for the record). A2. The playhead head outline as an option (default off). A3. README rows. ad2, win95_standard and frozen render byte-identical (compare.py, 0 pixels).

## Part B — mock set AL: DARK VARIATIONS (renders only, never committed)

He likes the grey and blue-grey themes (Windows Redmond / Classic, Dangerous Creatures, Digital CDE, Northern Sky, Charcoal, Dark Blue, Mystery, Neptune, Golden, Space) and wants "modern-day dark variations" of them before deciding on a default. A DARK VARIATION of a catalog entry, for these mocks only (these are designs, not catalog entries, and are labelled so):
- THE GROUND keeps the entry's ground hue and saturation (HLS) and takes the lightness that gives a target relative luminance (colour.relative_luminance), two levels: DARK 0.035 (about the app's #303030 and KDE Breeze Dark's window) and DIM 0.080 (about CDE Northern Sky, which he liked as it is).
- THE RELIEF is the entry's OWN FAMILY RULE run on the new ground, as that desktop would have shaded a face of that colour (toolkit_rules: windows-dialog for windows / windows-plus, kde3 at the entry's contrast, motif for cde; the quartet as each rule gives it, Motif's top / top / bottom / bottom).
- THE LABEL is white #FFFFFF (ruler labels, menu, clock, state line follow `label` as in the crops).
- THE SELECTION is the entry's own `selected_fill` / `selected_text`; where the entry records none (CDE), its `title_active` with white text.
- Everything else from the entry as the crops take it (field ground, info, disabled text, ticks = Shadow, flag outline = DkShadow, the app's two-line well).
- Program colours as today (canvas #141618, ink #96BFDA, flag #8A5EAC with a black label, invalid #BB575A with a black label, playhead #8B8B8B with the label outline / #FCFCFC uniform), flat flags with the UNDERLINED selected state; the visible flags left to right: EDITING, UNSELECTED, SELECTED (underlined), INVALID.

The nine bases (the three pure greys collapse to one, so Redmond stands for the greys): `kde3-redmond-2000`, `kde3-digital-cde`, `cde-northern-sky`, `cde-charcoal`, `kde3-dark-blue`, `plus-mystery`, `cde-neptune`, `cde-golden`, `plus-space`. Eighteen full-screen mocks into tmp/colour_arc2/al/, `--label`, named mock_AL<nn>_<key>_<dark|dim>.png, each base's DARK then DIM, in that order. Print each variation's ground, quartet and luminance. Look at them before reporting (zoom into a selected flag's underline and the playhead head).

## Report

Files changed, the underline's rows and any outline crossing, which bases fell back for the selection, each variation's ground and quartet, compare.py results, the AL paths, and anything he might want to rule on. Commit nothing.
