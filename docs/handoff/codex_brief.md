# CODEX BRIEF — Sol round, 2026-10-05 (written by the cloud planner; run by the architect by hand)

You are an EXTERNAL REVIEWER of the Warptempo GUI, repository root = the current directory: a custom C++23
in-process phase-vocoder GUI (Arch / labwc / Wayland / JACK, plus an Android build for one Galaxy Tab S10 FE),
single developer and user. YOU ARE REPORT-ONLY: describe defects and residue; CHANGE NOTHING — no edits, no
staging, no commits, no builds that write outside `tmp/`. Never run `scripts/warptempo_sync`, in any form.

Write your review INCREMENTALLY to `tmp/codex_review.md` (append each finding as you confirm it, so a cut-off
run still leaves a report), then `touch tmp/codex_done` when finished.

## The discipline (read CLAUDE.md first; it is the project's rule book)
- THE CODE COMMENTS ARE THE AUTHORITATIVE RECORD of every rule, at the symbol that owns it.
  `docs/engineering/closed_questions.md` lists everything ruled out: never re-propose a line of it.
- NO BACKSTOPS FOR ADVERSARIAL USE: a state reachable only by hand-edited files, tampered config, a second actor
  racing the app or a perverse gesture sequence gets a hard fail and at most a stderr line. Do NOT report missing
  recovery paths or pre-validation for such states. Before any edge-case finding, ask whether ordinary GUI use can
  construct the state at all; say how it does.
- No automated tests exist and none may be proposed (Wayland has no input injection).
- FROZEN: `src/engine/`, `src/parser/`, `src/audio_io/`, `src/prepost/`; `src/cli/cli_main.cpp` freeze-adjacent.
  A finding there must be a real defect, stated as such.
- COMMENTS describe current behaviour and rationale only; a comment that no longer matches the code is residue
  worth reporting (stale counts, stale lane lists, stale role counts).
- Report per finding: file:line, the defect in one sentence, the concrete scenario (inputs / state -> wrong
  result), severity (BLOCKING / MINOR / RESIDUE), and how sure you are. Verify against the code at HEAD; do not
  speculate about "possibly visible" pixels without reading the painter.

## What to review (in this order)
1. SAVE AS THE DIRTY MARK — 54ba6b6. Save greys unless the undo-tracked dirty flag is set; Ctrl+S is silent when
   grey; the clock's `*` was retired. Trim and the authoring lock never light it (the architect's ACCEPTED
   trade-off — do not report that by itself). HUNT FOR LOST WORK: any authoring act (marker add / delete / move,
   value edits, phase-reset edits, propagate paste, undo / redo across a save, a load in place, a project switch,
   the quit prompt) that changes what Save would write while leaving the flag clear, or a save / revert path that
   clears it while unsaved changes remain. Also: does the quit / close prompt still protect unsaved trim edits?
2. THE TITLE BAR — 6155f29. The app paints its own Windows 95 caption on both devices (18 Windows px; icon, Roboto
   Bold title "<piece> - Warptempo", Minimise / Maximise-Restore / Close), labwc asked for client-side
   decorations, the laptop starts maximised and, restored, draws a 4-px sizing frame outside the app's geometry
   (the backend translating pointer, touch, damage and paint by frame_px_), resizing through xdg_toplevel_resize
   and moving through xdg_toplevel_move; six new theme roles (caption active / inactive start, gradient end,
   text; a start without an end is flat); the caption's gradient quantised to 15-bit colour under an ordered 4x4
   dither in Windows-px cells (`paint_caption_gradient`); the tablet's Minimise is Activity.moveTaskToBack over
   JNI (UNCOMPILED on the cloud host: read it closely). Look for: coordinate-space slips between surface and
   client (pointer, touch, damage, pointer-lock restore hints, cursor shape over the frame), configure-ordering
   bugs (maximised / activated / size / frame changes), a lost release after a compositor move/resize grab,
   lanes or hit tests that did not move down with the caption, the theme-file rule and the generator
   (`tools/theme_catalog/gen_theme_files.py`, `roles.py`; the bundled `assets/themes/`).
3. THE THEME FILES AND THE BUNDLE — 3c3575d, 71acf95, 40623b9 (and the catalog in the app, 7227c38 with
   9885509). Every colour a role of `kGuiThemeRoles` (src/gui/theme_file.h); theme files read once at launch from
   `themes/` beside the device config, first-error hard fail; the retired keys fatal; the bundled files copied in
   at every launch on both devices (the laptop from a compiled-in path, the APK from assets). Look for: a launch
   path that can fail on a state the GUI itself produces, a copy-in that can leave a partial file, a role read
   from the wrong slot, a colour still painted outside the palette.

OUT OF SCOPE: the colour picker (`tools/palette/picker/`) and the mock tool (`tools/palette/`): deferred.

## Prior rounds
The last Sol round reviewed up to the 2026-10-03 glass pass (04a3fe1); its comment residue landed there. Nothing
since has been reviewed.
