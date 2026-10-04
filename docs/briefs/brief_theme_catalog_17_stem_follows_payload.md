# Brief: the theme catalog, step 17 — the selected stem follows the flag box, not the marker (2026-10-04)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, may build build/, run only read-only git, never stage or commit, never launch the GUI, never build the APK, never use adb, never run scripts/warptempo_sync in any form, never edit CLAUDE.md). HEAD is 5d96a6b2 (step 16: docs/briefs/brief_theme_catalog_16_warptempo_theme.md). Nothing frozen is touched.

## The ruling (architect 2026-10-04)

Step 16 made a selected marker's stem take the selected face whenever the marker is selected, whichever cell is addressed (render_flag_boxes_impl's `stem_face`, render.h's THE STATES). He ruled the other way: THE STEM TAKES THE SELECTED FACE ONLY WHEN THE FLAG BOX ITSELF (the payload, the leftmost box) IS THE BRIGHT ONE; when a BOUND CELL is the addressed cell, the stem keeps the marker's resting face (`flag_face`, or `invalid_face` on an invalid marker) — "otherwise it looks disconnected": the stem belongs to the box it leaves from. So the stem reads the payload box's own face (as before step 16, `face.stem`), and the separate `stem_face` goes.

Apply the same rule at every site that resolves a marker's stem from its selection, so they cannot disagree:
- the flag pass's stem crossing and the stem stash (MarkerStem) in render_flag_boxes_impl;
- the in-place editor (render_flag_editor_box): the box is the editor's own and stays the selected face; the stem under it is the selected face when the PAYLOAD field is open, and the marker's resting face when a BOUND-CELL field is open (the payload box is not the addressed one then) — confirm by reading which cell is addressed while a bound field is open and state it at the site;
- the phase-reset lead-in ring (phase_reset_overlay_band's `selected`, phase_reset_stem_color): it wears what the stem wears, so it takes the selected face only when the reset's payload is the bright cell (phase resets carry bound cells? grep; if they carry none, the ring's bit is the marker's selection and nothing changes there — say which);
- the history view's diff flags (no bound cells there; unchanged — confirm).
Rewrite the comments step 16 wrote for the marker-follows rule (render.h's THE STATES and the stem paragraph, render.cpp's THE SELECTION IS ONE CELL'S block, paint_handler.h's stem-stash paragraph if it says "whichever cell") to state the ruling, dated (architect 2026-10-04). Fold the same rule into tools/palette only if its scene can show a selected bound cell; otherwise say it cannot.

## Build and report

`cmake --build build -j$(nproc)` must exit 0. Report the files changed, each stem site and what it now reads, and anything for him to rule on. Commit nothing.
