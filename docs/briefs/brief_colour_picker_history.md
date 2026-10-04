# BRIEF: the colour picker's pick history — back / forward through the saved picks (2026-10-04)

Model: Opus, effort high. You are the implementation subagent for warptempo_gui: read CLAUDE.md, then
`tools/palette/picker/README.md` and its sources (you or a predecessor wrote them under
`docs/briefs/brief_colour_picker_app.md`, whose fence applies unchanged: write only under `tools/palette/picker/`,
read-only git, no adb, never the sync script, never stage or commit).

## Why
The architect writes down the inks he likes as he iterates, and wants to backtrack and compare without typing a hex
(the app has no keyboard). His ruling (2026-10-04): an undo / redo over the SAVED PICKS — every close of the panel
already saves one to `picks.txt`; back and forward step through them.

## What to build
1. THE HISTORY is the active layer's committed picks, oldest to newest — exactly the `picks.txt` lines for that
   layer, read at launch (so it survives restarts) and appended to in memory as commits happen. A CURSOR marks the
   entry being shown.
2. TWO BUTTONS on the panel, BACK and FORWARD (arrow glyphs drawn in the panel's chrome, like the − / +), beside the
   hex, and a count "N of M" (N the cursor's entry, M the history's length) in the numbers' face near them.
   - BACK / FORWARD move the cursor one entry and set the colour to that entry: the active layer repaints live, every
     control follows (the wheel, the sliders, the hex, New). Old stays the colour the panel opened with. Each acts at
     the pen's lift, like the − / +.
   - TRUTHFUL BUTTONS: a button whose act would be a no-op is drawn disabled (dimmed glyph) and does nothing — BACK at
     the first entry, FORWARD at the last, both with an empty history.
3. NO SAVED PICK IS EVER THROWN AWAY (unlike an editor's undo, there is no redo branch to lose); an UNSAVED edit is:
   - A close COMMITS (appends to the history's end, to `picks.txt`, the logcat line as today) only when the colour
     differs from the cursor's entry (or the history is empty); the cursor then goes to the new last entry. Closing
     on a colour reached by stepping, unchanged, appends nothing — `state.json` still records it.
   - ONE DELIBERATE SAVE: closing the panel is the only act that commits (architect 2026-10-04: "just throw it
     away. One deliberate save action"). Stepping BACK / FORWARD from an edited, unsaved colour DISCARDS the edit:
     the step goes from the cursor's entry and the edited colour is gone. Leaving the app with the panel open
     (home, the cover) no longer commits either: the panel's unsaved colour is discarded, and the app comes back on
     the last saved state.
4. RELAUNCH: the colour is `state.json`'s as today; the cursor is restored too (record its index in `state.json` —
   your format; the reader stays as strict as today's for what the app writes, and a `state.json` from the previous
   build, without the index, puts the cursor on the newest entry equal to the colour, else at the end).
5. The closed-panel corner label keeps the hex; add " N of M" after it if it fits cleanly — your call, stated.
6. A logcat line per step: `picker: step <layer> N of M #RRGGBB`.
7. Extend the laptop check (`build_picker.sh --check`, `src/host_check.cpp`): a scripted session covering commit,
   back, forward, the disabled ends, a no-op close after a step, an edit-then-step discard, leaving the app with an unsaved edit, and a reload restoring the
   cursor; snapshots of the panel with the new buttons for the planner's eye.
8. README: the history and its rules, in the Use section.

## Report
Files changed; the layout of the new buttons (a snapshot path); the check's results; every judgment call; nothing
claimed about the device.
