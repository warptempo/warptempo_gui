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

## THE RUN'S LOG (the cloud planner, 2026-10-04; newest last)
- ARC 1 LANDED (de053b1): Playhead Head + Playhead Stem, the scene `playhead` (no flag selected, the playhead at
  x 293); no picker app code; the old scenes export byte-identical; the laptop check now also runs the repository's
  copy of his tablet files against the export.
- ARC 2 LANDED (the commit after de053b1): `warptempo-preset-1` / `-2` in the catalog (build.py `--presets-only`:
  the cloud cannot reach the theme sources, so the full build.py run is REQUEST 1's to confirm), the table at 100
  entries, `tools/theme_catalog/preset_keys.py [n]` prints each preset's device lines (writes nothing). The product
  builds GIT=OFF here. Presets carry only chrome / canvas / ink so far. NOTE FOR ARC 3: build.py hard-fails on a
  preset key it does not know, so the LABEL element must extend PRESET_PROGRAM_KEYS / the fixed roles with it.
- REQUEST 1 (arcs 1 + 2 batched) -> DONE 1 (eb8c69c, 1 of 8 local wakes): all built, checked, installed; the full
  build.py reproduced the presets-only bytes; his tablet files unchanged; his product config untouched (theme
  kde3-solaris). The playhead on the glass is his to see: choosing an element with an empty history commits its
  colour as a first pick, so the laptop did not touch it. Next: arcs 3-5 here, then REQUEST 2.
- ARC 3 LANDED (the Label commit): the element Label (role `label`, #FFFFFF) over a new scene `label` (the waveform
  scene with row 8's state line on; stems, flags and playhead off so the fixed-white flag labels do not sit beside a
  moving Label); every chrome word, the clock, the state line and the playhead head's outline follow it; the flag
  labels and the selected text stay fixed white. The helper takes a preset's label into the theme's label and field
  text. NOTE FOR ARC 4: the chooser's 8 entries end at y 716, the slider rows start at 720; a NINTH element overruns
  the H row (the check theme already shows it), so arc 4 must fit the chooser (hide a readout the chooser covers, as
  the model list does, or whatever the picker's own style gives).
- THE ARCHITECT ON THE GLASS after DONE 1: the playhead works (head fill, stem); the head's border has no element,
  "ok for now, not a priority" (since arc 3 it follows the Label).
- ARC 4 LANDED (the open-flag commit): Selection (`selected_fill`, #666666) and Selected Text (`selected_text`,
  #FFFFFF) over a new scene `open_flag` (the fourth flag, x 1816, the flags scene's selected one, its editor open and
  its whole text selected; no flag stays in view on both panel halves, this one does with the panel on the left and
  the strip open). THE CHOOSER now runs down over the slider rows, painted over all it covers, holding up to 15
  entries (picker.h kChooserMax; picker_load refuses more): the one picker app-code change of the run. The helper
  takes a preset's selected pair into its theme (the dark level by levels.py's rule).

## Decisions for you (the cloud planner, 2026-10-04)
1. THE FIELD TEXT FOLLOWS THE LABEL (applied in arc 3, easy to undo). The picker's chrome rule makes the field ground
   the Hilight, a ground-family colour, so the field's text is the label's case: a light variant made by adopting
   #000000 as the Label gets black text in its fields too. The alternative is a separate Field Text element (one more
   knob nobody would set apart from the Label). Recommended: keep as applied.

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
He leaves the run alone for ~8 hours. Budgets (raised by him 2026-10-04): the cloud may BURN DOWN MOST OF ITS CREDIT
(~$239) between now and the local weekly reset, TUESDAY 2026-10-06 ~11 pm ET, paced so the work lasts until then: about
$70 per eight-hour run, keeping ~$30 in reserve at the reset. The LOCAL weekly meter (~1-2 % left until that reset) is
the binding limit: the laptop has AT MOST 8 WAKES IN TOTAL until the reset, so BATCH several arcs into one REQUEST
when they can wait (e.g. one install for two picker arcs). Work in ARCS, each ending at a STOPPING POINT.

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
3. The LABEL element (text).
4. The OPEN FLAG (the selected fill + its text), then 5. the INVALID (red) flags — scenes and elements only; his
   picks come later on the glass.
Past these, stop: the rest needs his discussion.

HARD STOPS (write the state into this file, commit, and stop working):
- Any ruling needed from him (a design choice the rulings above do not cover): put it in "Decisions for you" here,
  skip to the next arc only if it does not depend on it, else stop.
- ~$70 of cloud credit this run by your best estimate (if you cannot see the meter, count: an arc is roughly $5-15),
  or 8 hours; never below the ~$30 reserve.
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
