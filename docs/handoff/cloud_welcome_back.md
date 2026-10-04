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

## Open decisions (asked 2026-10-04, unanswered — put them in "Decisions for you")
1. The ROADMAP ORDER (dark themes first): (1) the PLAYHEAD element (head + stem; always on screen; a scene with the
   playhead on), (2) the PRESET-TO-PRODUCT HELPER (his go already given: a preset's chrome -> a program-own theme entry
   `warptempo-<preset>` in `tools/theme_catalog/` with the rule applied at generation, so the app derives nothing;
   canvas / ink / flags / playhead -> the twelve device colour keys, `src/gui/device_config.h`'s head; regenerate
   `theme_table.h`; a product rebuild), (3) a pickable LABEL element (text), (4) the OPEN FLAG (selected fill + text),
   (5) the INVALID (red) flags, (6) the clock's own colour (an idea he asked to carry forward for discussion), icon
   accents later. The planner recommended this order.
2. TEXT: a pickable Label element (recommended; the product imports each theme's recorded text colour and has no
   contrast rule, so a free element is the truthful form; adopting #000000 from a theme strip makes a light variant)
   vs a black/white switch vs automatic contrast.
3. Go on step 1 (the playhead)?

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
