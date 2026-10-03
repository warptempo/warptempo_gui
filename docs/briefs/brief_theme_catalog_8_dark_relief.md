# Brief: the theme catalog, step 8 — the dark variation's relief scaled from the base's own, the disabled word lightened, and mock set AN (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, run only read-only git, never stage or commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md, never touch src/). THIS BRIEF TOUCHES ONLY tools/palette/ (render.py, README.md, if the disabled word needs a renderer change) and scratch under tmp/colour_arc2/an/. Do not regenerate docs/themes/. HEAD is cc2adf9b (step 7: docs/briefs/brief_theme_catalog_7_windows_darks.md, its generator tmp/colour_arc2/am/make_am.py — the base for this one).

## What he saw in set AM, and why (the planner's diagnosis)

1. THE BUTTONS SHOW ONE LIGHT LINE TOP-LEFT AND TWO DARK LINES BOTTOM-RIGHT. The toolbar button is the SOFT RAISED edge (render.h's relief grammar: outer Hilight / DkShadow, inner 3DLight / Shadow). Windows' dialog rule sets 3DLight = the face, so the inner top-left line vanishes into the ground. (Windows' own derived schemes look exactly so; but Windows 95 Standard RECORDS a distinct 3DLight, #DFDFDF, and its dark variation lost it.)
2. THE TOP-LEFT IS TOO BRIGHT ON THE DARKEST GROUNDS. The dialog rule lifts Hilight halfway to white whatever the face (#353535 -> #9A9A9A), so the darkening reached the face and the shadows but not the light lines.
3. THE EMBOSSED DISABLED WORD IS TOO DARK: its word is drawn in Shadow, which on a dark face sits barely below the ground.

## The rulings (architect 2026-10-03, late: "fix up those issues")

- THE DARK VARIATION'S RELIEF IS THE BASE'S OWN RECORDED QUARTET, DARKENED IN PROPORTION WITH THE FACE (replacing the dialog rule for the variations): for each of Hilight, 3DLight and Shadow, keep that recorded line's HLS hue and saturation and scale its HLS lightness by L(new face) / L(base face); DkShadow stays the base's (black in every Windows-family base). So a base that records a distinct 3DLight keeps two light lines top-left (Windows 95 Standard: #FFFFFF / #DFDFDF / #808080 over #C0C0C0 become three proportional greys over the dark face), and a base whose 3DLight equals its face (most derived schemes) keeps one, as Windows drew it. Print each variation's quartet and its contrast against the face.
- THE DISABLED WORD ON A DARK VARIATION IS LIGHTER THAN THE FACE, the light theme's emboss mirrored: the word in a grey LIGHTER than the new face by the HLS-lightness distance the base's GrayText sits BELOW the base's face (Windows 95 Standard: #808080 below #C0C0C0), hue and saturation of the base's GrayText kept; its echo in the variation's Shadow one Windows px right and down, drawn first. A renderer option if needed (the step-7 `menu.disabled "engraved"` stays as it is).
- THE EDITING FIELD stays the base's recorded Window colour (white), as in set AM.
- Everything else as set AM (ground hue and saturation at relative luminance DARK 0.035 and DIM 0.080, white label, the base's selection, today's program colours, flat flags with the underlined selected state, the playhead head outlined; flags left to right EDITING, UNSELECTED, SELECTED, INVALID).

## Mock set AN (renders only, never committed)

The same eight bases as AM, each DARK then DIM, sixteen mocks into tmp/colour_arc2/an/, `--label`, named mock_AN<nn>_<key>_<dark|dim>.png: windows-95-standard, windows-classic, plus-dangerous-creatures, windows-storm, plus-mystery, plus-space, windows-slate, windows-rainy-day. Look at them before reporting: enlarge a toolbar button's top-left corner and the disabled Edit word on AN01 and AN13. ad2, win95_standard and frozen render byte-identical (compare.py, 0 pixels).

## Report

Files changed, each variation's quartet and contrasts, the disabled word's colours, the AN paths, compare.py results, and anything he might want to rule on. Commit nothing.
