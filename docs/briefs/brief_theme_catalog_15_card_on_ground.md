# Brief: the theme catalog, step 15 — the card and the tooltip stand on the ground (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, may build build/, run only read-only git, never stage or commit, never launch the GUI, never build the APK, never use adb, never run scripts/warptempo_sync in any form, never edit CLAUDE.md). HEAD is dc24756d. Nothing frozen is touched. Rulings land as comments at their owners dated (architect 2026-10-03); what retires gets a closed_questions.md line.

## The ruling (architect 2026-10-03, on mock set AY)

"The card should just become ground": at dark the proportional yellow read as "a dingy beige", and a theme's recorded tooltip colours, picked for contrast on its own light ground, do not translate; "I wouldn't want to be recording exceptions — we want a coherent rule set that doesn't need exceptions". THE INFO FACE (paint_popup_chrome's Info face: every notification card AND the tooltip, one face) IS THE LEVEL'S GROUND WITH THE LEVEL'S LABEL, ON BOTH LEVELS, framed by the INFO FRAME as today (its one-line edge is what tells the card from the chrome). The info pair retires from the generated table and the palette (info_ground / info_text; the KDE / CDE fallback to the app's #FFFFE1 / #000000 goes with it); the light level loses Windows' yellow too (one rule for both levels — state it at the owner so a later ruling can restore the light level's recorded pair in one place). Regenerate src/gui/theme_table.h (byte-stable; levels.py --verify passing); tools/palette's tablet.json / crops (`crops.py`) follow; re-render the 97 crops if the crop windows show a card or tooltip (say whether they do). Docs: notifications.h's head, HELP.md if it names the yellow, windows95_deviations.md (the yellow tooltip is Windows' — the card on the ground is a departure: record it), closed_questions.md ("The tooltip and the cards on Windows' COLOR_INFOBK yellow, or on a theme's recorded info pair — retired, 2026-10-03: the info face is the ground and the label").

## Report

Files changed, the table's new shape and checks, the crops, and anything for him to rule on. Commit nothing.
