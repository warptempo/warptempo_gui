# Brief: the theme catalog, step 10 — Windows' normal emboss with an unscaled light layer, mock set AP (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, run only read-only git, never stage or commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md, never touch src/). THIS BRIEF TOUCHES ONLY tools/palette/ (render.py, README.md) and scratch under tmp/colour_arc2/ap/. Do not regenerate docs/themes/. HEAD is 80065a44 (step 9: docs/briefs/brief_theme_catalog_9_fair_rerun.md, generator tmp/colour_arc2/ao/make_ao.py — the base for this one).

## The rulings (architect 2026-10-03, late, "yes on both")

- NO MIRRORED DISABLED: the step-8 / step-9 inversion (`menu.disabled "shadowed"`, `buttons.disabled "shadowed"`) is not used; it stays in the renderer for the record.
- DISABLED TEXT AND ICONS USE WINDOWS' NORMAL EMBOSS (DSS_DISABLED): first a copy in the LIGHT colour one Windows px right and down, then the word or glyph in Shadow on top. THE ONE EXCEPTION: on a dim or dark variation, the light copy takes the theme's RECORDED Hilight (the base entry's own `bevel_hilight`, not darkened); the Shadow word darkens with the face like every other line. On a light (original) theme the two coincide, so nothing changes there. Implement it as a role the engraved paths read for the light copy (for example `emboss_hilight`, default `@bevel_hilight`), so ad2, win95_standard and frozen stay byte-identical; the generator sets it to the base's recorded Hilight. (Windows' DSS_DISABLED renders any image, colour or not, as a one-colour emboss of its mask, so the icons' later colour does not change this.)
- Every variation also as ruled: light, dim and dark offered for every family; all four relief lines darkened in proportion with the face (black stays black); white label; the base's selection (CDE: its active colour set with Motif's computed foreground); today's program colours; flat flags with the underlined selected state; the playhead head outlined; visible flags left to right EDITING, UNSELECTED, SELECTED, INVALID.

## Mock set AP (renders only, never committed)

Ten full-screen mocks into tmp/colour_arc2/ap/, `--label`, named mock_AP<nn>_<key>_<level>.png, in this order: windows-95-standard light (the original, for reference), windows-95-standard dim, windows-95-standard dark, windows-rainy-day dark, plus-dangerous-creatures dark, kde3-digital-cde dark (the step-9 hard case), kde3-dark-blue dark, kde3-redmond-2000 dark, cde-northern-sky dark, cde-golden dark. Print each one's emboss pair (light copy, word) with their contrasts against the face. Look at them before reporting: enlarge the disabled Edit word and a disabled toolbar icon on AP03, AP06 and AP10. ad2, win95_standard and frozen render byte-identical (compare.py, 0 pixels).

## Report

Files changed, the emboss table, the AP paths, compare.py results, and anything he might want to rule on. Commit nothing.
