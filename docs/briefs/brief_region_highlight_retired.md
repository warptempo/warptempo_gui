# Brief: the sweep's waveform highlight retires (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, run only read-only git (status / diff / log / show), never stage or commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md). Product source under src/gui/ only; nothing frozen is touched (src/engine, src/parser, src/audio_io, src/prepost, src/cli: if you find a reason to touch one, stop and report).

## The ruling (architect 2026-10-03, evening)

"The trim bar is enough": the region highlight on the waveform retires. It served fingers; the pen, and now the finger too, sweep by press-hold-drag, and the trim bar shows the span. The planner's check: the trim bar is drawn at the view's scale (render_trim_flags takes the viewport's samples), and the sweep writes the trim on every motion event (write_trim_from_sweep, input_trim.cpp), so the bar already shows the sweep's span live in the same columns as the highlight did.

WHAT STAYS: the sweep gesture itself, every road to it (the shift+drag former, the touch region hold, the pen's press-hold-drag, the `h` view's carved-out playhead sweep), its per-motion trim writes, commit_region_sweep's commit tail (auto_clear_crossed_trim, the repaints, the target-render trigger, the playhead parked at the committed trim start), and the trim bar.

WHAT GOES: the highlight and everything that exists only for it.
- The visibility bit: `RegionState` (app_state.h) and `AppState::region`, its raise `show_trim_region_overlay` (input_handler.h / .cpp) and its call site on the sweep's motion path, the hide block in `commit_region_sweep` (input_pointer.cpp), the file load's reset (file_loader.cpp).
- `trim_overlay_span` (app_state.h) if the painter was its only consumer.
- The paint path: the two painters in paint_handler.cpp gated on `app.region.shown` (around lines 3900–4090: the region canvas fill and the per-pixel lift of the opaque plate pixels inside the span), their declarations and any damage or clip helpers that serve only them.
- The colours: `kWaveformRegionCanvas` and `region_lift` (render.h) and the comments that describe the region's step at the palette block (around lines 189, 276, 683–713) and at the plate (around 2220).

GREP BEFORE ASSERTING: re-derive the inventory yourself (`region`, `RegionState`, `region_lift`, `kWaveformRegionCanvas`, `trim_overlay_span`, `show_trim_region_overlay`, `overlay` in src/gui and docs/); the list above is the planner's starting point, not the authority.

## Points to settle while you work (report each)

1. DAMAGE: with the highlight gone, confirm what each sweep motion repaints. The trim bar must still repaint on every accepted trim write, mid-gesture, on both platforms' paths. If the waveform-area invalidation in the raise and hide was the only thing repainting something trim-dependent (anything on the canvas that reads the trim), say what and keep that repaint at its rightful owner; do not keep a waveform invalidation that nothing needs.
2. THE PLATE'S GAPS: the waveform plate leaves transparent gaps so a recoloured ground shows through (render.h near 2220, waveform_cache.cpp near 53, paint_handler.cpp near 3832). If the region was the gaps' only reason, do NOT change the plate or its cache (that is perf-campaign territory): correct the comments to the plate's remaining reason, or, if none remains, say so in the comment and in your report and leave the plate as it is.
3. THE TOUCH REGION HOLD (input_core.{h,cpp}, `begin_touch_region`, `kTouchRegionHoldMs`): the hold stays. Correct any comment that justifies it by the highlight.
4. THE CURSOR CUE, the notifications and the `h` view: nothing there should read the bit; confirm.

## Residue (pre-approved)

- Comments describe current behaviour at the owner, with "architect 2026-10-03" where the rule changed; no history narration beyond what the owner block already keeps (RegionState's WHAT WENT ON paragraph moves to whichever comment now owns the sweep's picture, or goes, as you judge: the history is git's).
- `docs/engineering/closed_questions.md`: one new line in the trim / region group: the sweep's waveform highlight (the region overlay) — retired, 2026-10-03 ("the trim bar is enough"): the trim bar, drawn at the view's scale and written per motion, is the sweep's picture. Owner: the sweep's owner comment (name it). Amend the existing lines whose owner symbol no longer exists (`RegionState`, `show_trim_region_overlay`, `trim_overlay_span`) to their new owner.
- docs/HELP.md, docs/INSTALL.md, the tooltips: if anything describes the highlight, correct it.

## Build and report

Build `cmake --build build -j$(nproc)` and the Android build is the planner's. Report the real exit code, the final inventory of what was removed (by file and symbol), the four points above, and any judgment call he might want to rule on.
