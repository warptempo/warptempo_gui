# Brief: the theme catalog, step 4 — the two-line well back, the stem from the flag's face through a gap, and mock set AJ (the dark and colourful themes) (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, run only read-only git, never stage or commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md, never touch src/). THIS BRIEF TOUCHES ONLY tools/palette/, tools/theme_catalog/ and docs/themes/ (regenerated). HEAD is afcdfe89 (steps 2 and 3: docs/briefs/brief_theme_catalog_2_flags_well.md, brief_theme_catalog_3_labels_tiers.md). Read tools/palette/README.md, tools/theme_catalog/README.md and tmp/colour_arc2/ai/make_ai.py (step 3's mock generator).

## The rulings (architect 2026-10-03, late)

1. THE WELL KEEPS THE APP'S TWO-LINE EDGE, top and bottom only, no sides: the PLAIN SUNKEN field edge the app draws today (render.h's relief grammar at line ~130 and THE WELL'S BORDER at ~1425: top `bevel_shadow` then `bevel_dkshadow` inward; bottom `bevel_light` inward then `bevel_hilight` outward). Step 2's one-line Sound Recorder edge retires from the crops and mocks (the canvas loses the two rows it gained). The flags still stand on the well's first line.
2. THE STEM LEAVES THE FLAG FROM ITS FACE, not its corner: the stem's column is the flag's FIRST FACE COLUMN (inside the one-line outline and the one-line bevel), so the box's left edge lies left of the stem by the outline and the bevel, as the app's flags sat before step 2. The flag's bottom outline line and bottom bevel line get a GAP exactly the stem's width at the stem's column, so the face colour runs unbroken from the face into the stem: a non-antialiased same-colour selection of the face would take the stem with it. The gap holds in every state that has a stem (raised, sunken, invalid; the sunken label nudge does not move the stem or the gap); a disabled flag has no stem and no gap. The stem keeps the face colour and runs through the well's top lines into the canvas as before. The box's width and height do not change.

## Part A — the renderer and the crops

A1. render.py's bevelled flags: ruling 2 (geometry per state; the stem's width is the renderer's line width, one Windows px). Report the columns and rows per state.
A2. crops.py and every mock generator: the well per ruling 1. Regenerate the crops; report the total. ad2, win95_standard and frozen render byte-identical (compare.py, 0 pixels).

## Part B — mock set AJ (renders only, never committed): THE DARK AND COLOURFUL THEMES

One axis: the theme (the chrome), with the program colours held at today's (canvas #141618, ink #96BFDA, flag #8A5EAC with a black label, playhead #8B8B8B / #FCFCFC, invalid #FF0000 / #FFFFFF) and the four flag states left to right (unselected, selected, invalid, disabled). Thirteen full-screen mocks into tmp/colour_arc2/aj/, `--label`, named mock_AJ<nn>_<key>.png in this order: kde3-wedgieweb, cde-northern-sky, kde3-dark-blue, plus-underwater, kde3-digital-cde, plus-mystery, plus-dangerous-creatures, cde-cinnamon, cde-cabernet, cde-neptune, cde-golden, cde-charcoal, cde-urchin. Look at them before reporting. The planner pushes the set to the tablet.

## Report

Files changed, the flag geometry per state (the gap's columns and rows), the crop total, compare.py results, the AJ paths, and anything he might want to rule on. Commit nothing.

## Addition (architect 2026-10-03, "let's try it in the mocks"; sent to the coder mid-brief)

THE PLAYHEAD STEM over the chrome lanes (ruler and marker, above the well) takes the playhead HEAD's colour; in the well and the canvas it keeps its own. A renderer option, default off (the parity records unchanged), ON in set AJ, plus mock_AJ14_warptempo-2026-10-03_paper.png: the warptempo entry with step 3's paper program colours (canvas #FFFFFF, ink #008080, outline #FFFFFF, flag #800080 / #FFFFFF, playhead #808080 / #000000) with the option on.
