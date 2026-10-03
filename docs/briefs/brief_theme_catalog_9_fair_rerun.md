# Brief: the theme catalog, step 9 — the fair rerun: KDE and CDE dark variations under the same proportional rule, inverted disabled icons, mock set AO (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, run only read-only git, never stage or commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md, never touch src/). THIS BRIEF TOUCHES ONLY tools/palette/ (render.py, README.md) and scratch under tmp/colour_arc2/ao/. Do not regenerate docs/themes/. HEAD is 7e8b0676 (step 8: docs/briefs/brief_theme_catalog_8_dark_relief.md, generator tmp/colour_arc2/an/make_an.py — the base for this one).

## Why (architect 2026-10-03, late)

Set AL ruled KDE and CDE out of dark variations, but it was not a fair test: each family ran its own toolkit rule on the dark ground (KDE: two light lines at very low contrast; CDE / Motif: two equal lines merging into one bright edge; the Windows family: the dialog rule's single bright line), and only the Windows family then got step 8's proportional rule. He wants the same rule run on the KDE and CDE bases ("yes on both").

## The rules for this set

- THE RELIEF is step 8's: the base's own quartet as the catalog records it at the base's own ground (KDE: KDE 3's rule at that ground, already in the entry's roles; CDE: Motif's top, top, bottom, bottom), each of the FOUR lines darkened in proportion with the face (HLS hue and saturation kept, HLS lightness scaled by new face / base face; black stays black; a 3DLight equal to the base face becomes the new face exactly). Scaling DkShadow too is the uniform form of the rule (step 8 did it for Windows Classic's #404040; CDE's DkShadow is Motif's bottom shadow).
- THE DISABLED WORD is step 8's mirrored emboss (`menu.disabled "shadowed"`): the word lighter than the new face by the HLS-lightness distance the base's disabled text sits below the base face; where the base records no disabled text (the CDE entries), use Windows 95 Standard's distance (#808080 below #C0C0C0).
- THE DISABLED ICONS on a dark variation get the same inversion: a new renderer option for the engraved disabled icons (default off) that draws the glyph's echo in the variation's Shadow one Windows px right and down first, then the glyph in the same lighter disabled colour as the word. In stock Windows both are embossed alike (the glyph in Shadow over a Hilight copy), and this mirrors that for a dark face.
- Everything else as step 8 (ground hue and saturation at relative luminance DARK 0.035 and DIM 0.080, white label, the base's selection, or for CDE its `title_active` with the colour set's own Motif foreground as the text (the catalog's computed foreground for set 1, black or white by Motif's threshold) — not white unconditionally; today's program colours; flat flags with the underlined selected state; the playhead head outlined; visible flags left to right EDITING, UNSELECTED, SELECTED, INVALID).

## Mock set AO (renders only, never committed)

Sixteen full-screen mocks into tmp/colour_arc2/ao/, `--label`, named mock_AO<nn>_<key>_<dark|dim>.png, each base DARK then DIM, in this order: kde3-redmond-2000, kde3-digital-cde, kde3-dark-blue, cde-northern-sky, cde-charcoal, cde-neptune, cde-golden, and last, for reference with the icon inversion on, windows-95-standard. Print each variation's quartet with its contrasts against the face, the disabled colour, and the selection pair. Look at them before reporting: enlarge a toolbar button's top-left corner, the disabled Edit word and a disabled icon on one KDE, one CDE and the Windows reference. ad2, win95_standard and frozen render byte-identical (compare.py, 0 pixels).

## Report

Files changed, the table, the AO paths, compare.py results, and anything he might want to rule on. Commit nothing.
