# Brief: the theme catalog, step 14 — the dim level retires; the held trim cap goes down at the press (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, may build build/ to self-check, run only read-only git, never stage or commit, never launch the GUI, never build the APK, never use adb, never run scripts/warptempo_sync in any form, never edit CLAUDE.md). HEAD is 6b482a6f. Nothing frozen is touched. Rulings land as comments at their owners dated (architect 2026-10-03); what retires gets a closed_questions.md line.

## 1. THE DIM LEVEL RETIRES (architect 2026-10-03)

"Remove the dim category. We don't want to be making even more rules": at dim, the proportional field / card / selection fall into a grey zone (white text on fields that stay pale for themes whose recorded ground is already darkish, 22 pairs on 17 entries below 3 : 1), and fixing that would need another rule. THE LEVELS ARE LIGHT AND DARK. Remove dim from tools/theme_catalog/levels.py and gen_theme_table.py, regenerate src/gui/theme_table.h (two levels per entry; byte-stable, levels.py --verify passing), the `theme_level` device key (light | dark, default dark, unchanged) and its Settings row / refusal, tools/palette (any dim option: keep old saved mocks re-rendering if they only read their saved JSONs, else say what no longer renders), and the docs (HELP.md, INSTALL.md, tools READMEs, docs/themes/CATALOG.md, closed_questions.md: "The dim level (relative luminance 0.080) — retired, 2026-10-03, a grey zone for the proportional pairs"). Grep `dim` / `Dim` / `0.080` across src/gui, tools, docs to be sure nothing is left; a config reading `theme_level=dim` is now the load's first-error hard fail (no migration; both devices are on dark).

## 2. THE HELD TRIM CAP GOES DOWN AT THE PRESS (architect 2026-10-03)

Today the end cap shows Windows' pushed scroll-arrow face (step: brief_theme_catalog_app.md rule 14, render.cpp's held-cap painter) only once the drag STARTS moving, which "looks odd". It must show from the PRESS (pen down / finger down / mouse button down) on the cap until the drag ends, exactly as a push button shows its pressed face from its press. Find where the held state is set (RegionDragState / input_trim.cpp / the touch and pointer press paths) and make the press set it; every end (lift, cancel, focus loss, hard end) clears it as today; a press that never moves still shows it until the lift. Keep the face itself as it is (flat, the one Shadow line ring — already the level's proportional Shadow). Damage: the cap's rect at the press and at the end. Check both pointer and touch roads (input_pointer.cpp, input_core / platform_android's touch).

## Report

Files changed, the table's new shape and checks, the press paths covered, and anything for him to rule on. Commit nothing.
