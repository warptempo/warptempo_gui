# WELCOME BACK, CLOUD OPUS PLANNER (written 2026-10-04 by the local Opus planner)

Read CLAUDE.md first (you are THE PLANNER; it holds every process rule), then this file, then
`docs/handoff/local_queue.md` (how you hand work to the laptop). THE PROJECT IS COMPLETE; the work now is the
COLOUR PICKER, a design tool beside the product, and carrying its picks into the product.

## Where things stand (HEAD fe50499c and after)
- `tools/palette/picker/` (its README has every rule) is installed on the tablet at fe50499c: five elements in the
  architect's ORDER OF IMPORTANCE — Chrome (one knob, the ground; every relief line by the Windows 95 rule,
  `tools/palette/colour.py`), Canvas, Ink, Unselected Flag, Selected Flag (labels fixed WHITE = his new standard) —
  over two scenes (waveform, flags); true antialiasing (pixman's own arithmetic, byte-identical to the mock tool);
  one-decimal readouts; a MODEL SWITCH (HSV / HSL / LCh, LCh over the bytes as Display-P3, D65); PRESETS ("Preset N",
  save / load the whole look); the THEME STRIP (the 98 catalog themes by key; open one, adopt a swatch as the active
  element's colour). His verdict: "version zero"; the picker, sliders and antialiasing are excellent.
- HIS PICKS: `tools/palette/picker/presets/` holds the tablet's `presets.json`, `picks.txt` and `state.json`, copied
  verbatim by the local planner each session (the latest look is `state.json`'s colours).
- The product is untouched (tablet APK 8cd49da7). Its defaults (selected flag #CCCCFF, black selected label) stay until
  the preset-to-product helper overrides them; he accepts the transitional state.

## Ruled 2026-10-04 (the architect agreed with all three recommendations)
1. THE ROADMAP ORDER (dark themes first): (1) the PLAYHEAD element (head + stem; always on screen; a scene with the
   playhead on), (2) the PRESET-TO-PRODUCT HELPER (a preset's chrome -> a program-own theme entry `warptempo-<preset>`
   in `tools/theme_catalog/` with the rule applied at generation, so the app derives nothing; canvas / ink / flags /
   playhead -> the twelve device colour keys, `src/gui/device_config.h`'s head; regenerate `theme_table.h`; a product
   rebuild), (3) a pickable LABEL element (text), (4) the OPEN FLAG (selected fill + text), (5) the INVALID (red)
   flags, (6) the clock's own colour (an idea he asked to carry forward for discussion), icon accents later.
2. TEXT is a pickable LABEL element (not a black/white switch, not automatic contrast): the product imports each
   theme's recorded text colour and has no contrast rule; adopting #000000 from a theme strip makes a light variant.
3. GO on step 1, the PLAYHEAD: brief it first (one Opus-high coder; the picker's machinery: a new element pair + a
   scene in `tools/palette/themes/picker.json`, a new export; app code only if truly missing). Then a REQUEST in
   `docs/handoff/local_queue.md` to build, export, install (on his word) and verify.

## THE AUTONOMOUS RUN (architect 2026-10-04: about EIGHT HOURS unattended; neither meter may die)
He leaves the run alone for ~8 hours. Budgets: the cloud's remaining credit (~$239; spend at most ~$50 this run) and
the LOCAL weekly meter (~1-2 % left; the local session must stay tiny). Work in ARCS, each ending at a STOPPING POINT.

AN ARC = one brief -> ONE Opus-high coder round (plus at most one fix-up message to the same coder) -> your review ->
`build_picker.sh --check` (or the product build) -> commit + push -> ONE batched REQUEST in local_queue.md (build,
export, install, verify, read back) -> wait for DONE (poll origin/main with a background shell loop: free while
waiting) -> update THIS FILE's state (what landed, what is next) and commit. That commit is the STOPPING POINT: a
fresh session could resume from this file alone.

THE RUN'S ARCS, in order (all ruled above; stop after the last, or earlier at any hard stop):
1. The PLAYHEAD element (head + stem). Installing the picker while he is away is allowed (his files are kept).
2. The PRESET-TO-PRODUCT HELPER: themes `warptempo-preset-<n>` for his saved presets (and the program keys printed
   for each), the table regenerated, the product built (cloud: GIT=OFF; the local REQUEST builds both devices and
   installs the APK). NEVER change his configured theme or colour keys on either device: the new themes only become
   selectable in Settings; he switches himself.
3. The LABEL element (text), if 1 and 2 are done and the budget below holds.

HARD STOPS (write the state into this file, commit, and stop working):
- Any ruling needed from him (a design choice the rulings above do not cover): put it in "Decisions for you" here,
  skip to the next arc only if it does not depend on it, else stop.
- More than 4 coder rounds, or ~$50 of cloud credit by your best estimate (if you cannot see the meter, count: an arc
  is roughly $5-15), or 8 hours.
- A REQUEST unanswered for 90 minutes (the laptop may be off): stop.
No Fable, no codex, no mocks for judgment (no one is there to judge), no product scope beyond the arcs.

KEEP YOUR OWN SESSION SHORT: every turn re-reads the whole context. Have coders return compact reports; read diffs
by stat and targeted hunks; never paste big files into your context.

## Later (his words, not decisions)
- The picker's NAVIGATION rework (history vs presets; editing a preset). He asked that his thinking-aloud about it NOT
  be recorded as decisions: ask him when the time comes.
- LONG TERM: a protocol so he can transfer themes to the product himself.
- Deferred product coding from before (brief only on his "code"): retire the dirty mark (Save greys when nothing to
  save); the pen plane's enter / exit back as device keys + Settings rows (start 115 / 130).

## What you can and cannot do
- You can: brief Opus coders, review, run `bash tools/palette/picker/build_picker.sh --check` (needs the host's g++,
  cairo, FreeType, python3 + numpy + pycairo; if missing, say so — the local planner runs it), render mocks and the
  export with `tools/palette/render.py`, build the product with `-DWARPTEMPO_GUI_GIT=OFF`, commit with plain git on
  main (CLAUDE.md's message rule: title and body only, NO trailers) and push main.
- You cannot: adb (the tablet), the Android APK builds (no NDK), the codex reviewer, the laptop's libgit2 build. Every
  such act is a REQUEST in `docs/handoff/local_queue.md`.
