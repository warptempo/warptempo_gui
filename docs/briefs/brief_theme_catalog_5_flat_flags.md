# Brief: the theme catalog, step 5 — the flat Acid flag, the editing face, the uniform playhead stem, and mock set AK (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, run only read-only git, never stage or commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md, never touch src/). THIS BRIEF TOUCHES ONLY tools/palette/ (render.py, README.md) and scratch under tmp/colour_arc2/ak/. Do NOT regenerate docs/themes/ and do not change tools/theme_catalog/crops.py: this is an internal iteration (architect: the catalog need not follow every mock round). HEAD is ac97db81 (step 4: docs/briefs/brief_theme_catalog_4_well_stem_darks.md). Read tools/palette/README.md and tmp/colour_arc2/aj/make_aj.py (step 4's generator, the base for Part B).

## The rulings (architect 2026-10-03, late)

- THE BEVELLED FLAG RETIRES as the candidate ("a 3D surface with a cut through it, and when you press it the cut goes the opposite direction"); its renderer style stays for the record. THE FLAT FLAG (ACID's): the face is the flag colour, filled, with the label on it; a ONE-Windows-px OUTLINE round the box in the theme's dark colour (`bevel_dkshadow`); the STEM leaves from the flag's LEFTMOST FACE COLUMN (the first column of fill colour, as the app's flags do today) and runs down CROSSING the bottom outline (drawn over it) through the well's top lines into the canvas. No bevel, no gap logic beyond the stem crossing the outline. Width and height unchanged.
- THE STATES: UNSELECTED as above. SELECTED: the OUTLINE turns WHITE (#FFFFFF); face, label and stem unchanged; no nudge. INVALID: the face a mellow custom red, the app's own #BB575A (the stock #FF0000 does not sit with a mellow flag colour), the label recorded (`flag_label_red`; black #000000 in these mocks, the app's current pair). EDITING (the in-place editor as it opens, the whole text selected): the face the theme's field ground (`field_ground`, Windows' Window colour), the outline black (the window frame), the label's text drawn as SELECTED text (the theme's `selected_fill` behind it, `selected_text` for the glyphs), no growth (the box keeps its width; architect: "the extending part is not necessary"). DISABLED (not mocked this round): as ruled, the ground's face, the engraved label, no stem.
- THE PLAYHEAD STEM IS UNIFORM: one stem colour from head to foot (the step-4 lane option off); its contrast is the user's choice.

## Part A — the renderer

A1. A new `flags.style "flat"` implementing the above, with an `editing` state in `flags.states` (index list like the others) and a theme role for the selected outline (default #FFFFFF). The `editing` face reads `field_ground`, the selection `selected_fill` / `selected_text`, falling back to Windows' Window / Hilight defaults (#FFFFFF, #000080 / #FFFFFF) when a theme lacks them (the CDE entries have no `selected_fill`: say which mocks fall back). README rows. ad2, win95_standard and frozen render byte-identical (compare.py, 0 pixels).

## Part B — mock set AK (renders only, never committed)

One axis: the theme. Program colours held at today's (canvas #141618, ink #96BFDA, flag #8A5EAC with a black label, playhead #8B8B8B head / #FCFCFC stem uniform), the app's two-line well, the flat flags. On each mock the scene's visible flags take, left to right: EDITING (the long first label, its whole text selected), UNSELECTED, SELECTED, INVALID (any further flag unselected). Eighteen full-screen mocks into tmp/colour_arc2/ak/, `--label`, named mock_AK<nn>_<key>.png, in this order: kde3-wedgieweb, cde-northern-sky, kde3-dark-blue, plus-underwater, kde3-digital-cde, plus-mystery, plus-dangerous-creatures, cde-cinnamon, cde-cabernet, cde-neptune, cde-golden, cde-charcoal, cde-urchin, plus-travel, plus-space, cde-sky-red, cde-mustard, and last, for reference, warptempo-2026-10-03. Look at them (zoom into a flag's junction with its stem and into the editing flag) before reporting. The planner pushes the set to the tablet.

## Report

Files changed, the flat flag's geometry per state, which mocks fell back for the editing colours, compare.py results, the AK paths, and anything he might want to rule on. Commit nothing.
