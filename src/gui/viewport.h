#pragma once

#include "app_state.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

class GuiAudio;
class GuiPlatform;
class GuiPlayback;

// WHICH ACT ASKS THE LANDING OWNER (Viewport::land_subject) TO LAND ITS
// SUBJECT (architect 2026-09-24). A REQUIRED argument with no default, the
// same discipline as MarkerLandingFrame (app_state.h), so a new caller cannot
// compile without saying which answers it means:
//   * `Walk` — the Tab walk in both audio views, live and `h`, and the
//     marker drop's landing (architect 2026-10-29): centered at
//     the working zoom, as painted, whether on screen or not; at every other
//     zoom, finer or coarser, nothing on screen and paged in off screen (the
//     paired march does not come
//     here: each of its steps centres through bare `c`'s own act, 2026-09-26);
//   * `Restore` — the undo / redo restore: nothing wholly on screen, and an
//     off-screen subject that fits centred at every zoom.
// The full answers, the hold rule and the ruled-out cameras are at the
// definition (viewport.cpp). No `Gui` prefix: a small policy enum like its
// neighbours.
enum class LandingKind { Walk, Restore };

// THE WINDOW-TOP RECT [0, y1) AT THE WINDOW'S WIDTH, SPLIT AT THE VIEWPORT'S
// LANES (2026-10-13): the rows from viewport_lanes_top_y down are damaged
// now, and the rows above it — the toolbar band and its title bar — are OWED
// to every paint target a check
// (GuiUpperRowsRecord::owed_y1, app_state.h), which the pre-paint hook pays
// (GuiPaintHandler::settle_owed_lane_damage): the whole rect where the rows'
// fingerprint has moved past what the target shows or a floating surface
// stands, nothing more where it has not. The ONE road for every rect that ran
// from the window's top to cover the lanes of the viewport, the zoom or the
// cursor: Viewport::invalidate_waveform_area (whose declaration below holds
// the lane inventory), the viewport routes' top-strip damage, and the
// waveform cache's four (the async publish, the synchronous rebuild, the
// deferred basis repair and the flag cache's stage wherever it declares the
// whole area, waveform_cache.cpp).
// THE SPLIT ROW IS CLEAN: nothing a lane paints crosses the band's foot
// (the tab strip's 2026-10-18 to 2026-10-22) — the band ends there and the
// view bar and the modal family's box begin there — so the two halves paint exactly the pixels the one rect
// did (the floating surfaces, which do cross it, take the whole rect).
void damage_window_top_rows(AppState& app, GuiPlatform& gui, int y1);

// Viewport mutators and invalidation helpers. The struct holds references
// to the long-lived state the methods read and write.
struct Viewport {
    AppState&                       app;
    const GuiAudio&                 audio;
    GuiPlatform&                         gui;
    GuiPlayback&                    playback;

    Viewport(AppState&                       app_,
             const GuiAudio&                 audio_,
             GuiPlatform&                         gui_,
             GuiPlayback&                    playback_)
        : app(app_),
          audio(audio_),
          gui(gui_),
          playback(playback_) {}

    // Trim helpers — THE NAVIGATION RANGE: the trim bounds Home/End jump to
    // and the load-time playhead rests at. THE GUI'S OWN PLAYBACK DOES NOT
    // READ THIS since 2026-08-05 (playback_lifecycle.cpp owns the split: the
    // song's end in source view, the bound preview buffer's in target); TWO
    // PLAYBACK CONSUMERS, both the console's — the car's loop window
    // (GuiPlaybackLifecycle::car_toggle_playback, 2026-09-17), which loops
    // exactly the range Home and End land on, always from its begin, and the
    // console's seek (seek_live_session_by, 2026-11-20), which reads it for a
    // once-through play's lower wall and its degenerate-trim silence. The car
    // shares this range with Home and End by reading the same owner, not by
    // composing their acts.
    std::pair<int64_t, int64_t> trim_range() const;
    int64_t                     trim_begin_sample() const;
    int64_t                     trim_end_sample() const;

    // Worker kick: requests an immediate waveform regeneration instead of
    // waiting for the next platform tick. IT IS NO LONGER THE VIEWPORT'S OWN
    // KICK (retold 2026-09-02): viewport changes take the SYNCHRONOUS route,
    // and what is left of this is the fallback named below.
    // Set from main.cpp to paint_handler.maybe_enqueue_waveform_render().
    // Held as a std::function rather than a GuiPaintHandler& so viewport.cpp
    // keeps no compile-time edge to paint_handler.h. kick_waveform_render()
    // is null-safe: if the callback is unset (e.g. before main.cpp wires it),
    // it no-ops. The enqueue is idempotent against the on_tick backstop —
    // both dirty-check the same pending fingerprint, so a redundant call is
    // a cheap no-op. Since 2026-09-02 its ONE call site is kick_waveform_sync's
    // callback-unwired fallback below (follow-scroll, the last explicit caller,
    // took the synchronous route that day); the async path's own inventory is
    // stated once at request_waveform_sync_'s block.
    std::function<void()> request_waveform_render_;
    void kick_waveform_render() {
        if (request_waveform_render_) request_waveform_render_();
    }

    // (There is no separate pan kick. Panning routes through
    // kick_waveform_sync below like every other user-driven viewport change —
    // the incremental shift-and-strip fast-path and its request_waveform_pan_
    // callback were retired 2026-07-26 so that moving and resting plates are
    // produced by one path.)

    // One-shot synchronous rebuild kick: for a discrete viewport/view jump,
    // owe the frame's synchronous plate rebuild, so every layer reflects the
    // new state in the one frame that paints. Set from main.cpp to
    // paint_handler.owe_waveform_rebuild(); the debt is paid at the pre-paint
    // hook, or by an event-time flush where a reader needs the output sooner
    // (THE REBUILD IS OWED, below, owns the contract). Held as a std::function
    // for the same no-compile-time-edge-to-paint_handler.h reason as the kicks
    // above. Null-safe: when unset (before main.cpp wires it) it falls back to
    // the async worker kick, so the path stays correct either way.
    //
    // THE CALLER INVENTORY (grep-derived; this is the ONE authoritative copy —
    // every other site carries only its own class statement plus a pointer here,
    // never a second full list). Two axes.
    //
    // AXIS 1 — WHY the plate must land in one frame with the reclamped geometry
    // below (else the overlays — playhead, markers, flags — jump a frame ahead of
    // a stale plate):
    //  - GENERIC viewport / view jumps: the plate CONTENT is unchanged but the
    //    viewport, zoom, or displayed DOMAIN moved. Viewport's own mutators
    //    (the offscreen-follow shift inside reseat_playhead_to, which
    //    move_playhead_to and the cent steps' re-land both reach,
    //    apply_zoom_change,
    //    apply_zoom_to_start, center_viewport_on_playhead, the discrete move's
    //    other three callers (hold_subject_column_after_nudge,
    //    follow_scroll_if_needed and land_subject),
    //    apply_strip_drag_zoom and the follow scroll through it, follow_scanner),
    //    ALL THREE VIEW SWITCHES, one class since 2026-07-30: the S/T audio-view
    //    switch (GuiInputHandler::switch_active_audio_view_to) and the A/B tab
    //    switch (GuiActiveViews::switch_active_tab_view_to — Ctrl+Tab and the
    //    audition's legs) (both domain flips) and the W/P marker-column switch
    //    (GuiActiveViews::select_active_markers_view) — the column switch moves
    //    neither viewport nor domain, so its plate render is redundant, but
    //    active_markers_view is a flag-cache FINGERPRINT field and this is the
    //    route that lands the flag rebuild in the damaged frame; it joins the
    //    class rather than growing a second flag-only kick (the reason is stated
    //    at its site, active_views.cpp). Bare 1/2/3/4, the ABSOLUTE VIEW
    //    SELECTORS (2026-08-01), add NO fourth site: they run the audio
    //    view's and the column's handlers themselves, so their kicks are those
    //    two entries and this list is unchanged by them.
    //    THE `h` VIEW'S LANE REPUBLISH (GuiInputHandler::republish_history_lane_now,
    //    input_key_dispatch.cpp) is the column switch's class through the
    //    history view's door: its every edge — the entry, the exit and the
    //    `,`/`.` walk's steps — moves flag-cache FINGERPRINT fields (the
    //    history_mode state) and no viewport, and kicks so the arriving lane
    //    lands in the damaged frame (the reason is stated at its site).
    //    The PROPAGATE PASTE'S LANDING TAIL
    //    (land_paste_in_phase_reset_view, phase_reset_propagate.cpp) is the
    //    column-switch case reached by a different door and joined 2026-07-30: it calls
    //    switch_active_markers_view_to('P') directly rather than through the
    //    selector, so it kicks at its own tail — after the changed selection, so the
    //    column and the selection hash rebuild together — its only kick of its
    //    own since it stopped crossing the audio view (architect 2026-10-14,
    //    Shift+S's symmetry); the framing of its changed set (architect
    //    2026-11-17, send U) kicks only through land_subject or
    //    apply_zoom_to_start, listed above, when it moves the camera. SHIFT+S — THE DROP FROM THE WARP COLUMN
    //    (2026-08-28, staying in its audio view since 2026-10-12,
    //    GuiInputHandler::drop_phase_reset_crossing, input_handler.cpp) — is
    //    that same case through a third door and takes the same tail for the
    //    same reason: it calls switch_active_markers_view_to('P') directly
    //    (the writer's selection clear has to land before the drop's
    //    single-select) and kicks after the drop, so the column, the plate
    //    and the new reset's flag land in one frame — its only kick, no
    //    audio switch running ahead of it. Also here: the
    //    settings tab_X_viewport_start commit
    //    (GuiSettingsEditor::commit_gui_setting),
    //    the nav drag's ZOOM-PHASE FORCE-END (finalize_active_drags,
    //    input_pointer.cpp — the force-end finalizer, whose callers are the
    //    window's close, resize and the history prefetch's failed-scan
    //    closer; a moved zoom-phase drag owes one synchronous
    //    rebuild there, having no release coordinates for a final apply; the
    //    strip drag whose terminating-event finalize stood here was deleted
    //    2026-08-15, and Esc is no terminating event, pointer gestures having
    //    no cancel) — and main.cpp's tick backstop for an ASYNC
    //    total change (a preview completion) live here.
    //  - THE PLATE'S OWN GAIN. The gain is the continuous curve derived from
    //    the source at load (GuiAudio::gain_curve), and the plate's gain
    //    field (waveform_gain_fingerprint, warp_frame_map_view.h) has ONE
    //    LIVE INPUT, re-grepped 2026-09-25: the MAGNIFICATION LAMP, whose one
    //    setter (GuiInputHandler::set_show_waveform_magnification, the one
    //    caller of kick_waveform_sync_if_gain_changed) takes the before/after
    //    kick below. The S/T flip leaves the gain field as it stands (the
    //    lamp governs both audio views); its plate re-renders through the
    //    fingerprint's target bit and warp-map hash and its own unconditional
    //    kick_waveform_sync. The markers and the zoom are no inputs: no marker store moves the curve or its fingerprint, so
    //    a marker gesture owes the plate nothing on the gain's account.
    //    A gain change dirties the plate fingerprint
    //    BY FIELD — the tick's async backstop would repaint it a frame
    //    late with no kick of its own, and the lamp takes the kick so the
    //    new gain lands in the frame its press does. A gain-only change is a
    //    plate CONTENT change with no geometry behind it, so the reclamp below
    //    is a pure no-op for it. It touches no audio: the gain is the
    //    picture's.
    //  - THE PLATE'S INKS (2026-10-05): a True Colors flip
    //    (install_true_colors' apply shape, render.h) moves the window's
    //    words of the waveform ink and outline the plate bakes and of the
    //    flag faces the flag cache bakes, and kicks so the new plate
    //    and the new flags land in the act's frame rather than a paint ahead
    //    of the next tick — a CONTENT change with no geometry behind it, the
    //    gain's shape.
    //  - THE PLATE'S FADE START (architect 2026-11-14, send H): a
    //    `waveform_fade_px` commit (GuiInputHandler::apply_waveform_fade_px)
    //    moves the bar width at which the plate's fade band begins
    //    (render.h's THE PLATE'S BARS), a plate fingerprint field, and kicks
    //    so the new row of bars lands in the commit's frame — a CONTENT
    //    change with no viewport behind it, the gain's shape again.
    //  - TARGET-WARP-MAP mutations: a build_warp_frame_map INPUT changed, so the
    //    target-view plate itself re-warps. RE-DERIVED 2026-07-29 when the whole
    //    tempo-image family was deleted (marker_drag.h), which took TWO entries
    //    off this list — the bare Left/Right tempo-image step and the pointer tempo
    //    DRAG. What remains: THE TEMPO WRITES, through their one shared tail
    //    (warp_tempo_write_tail, warpmarkers_ops.cpp) — the bare Up/Down tempo
    //    step (adjust_tempo_cents, singleton AND group) and the VALUE
    //    DRAG's commit (value_drag.cpp, once at the release, the drag having
    //    written the store per motion under the displayed-basis freeze) — the
    //    settings engine-scale commit (GuiSettingsEditor::commit), undo/redo
    //    (Undo::restore_history_entry), and THE LOAD-IN-PLACE
    //    FAMILY, which reaches this through its ONE shared body since 2026-08-24
    //    (apply_recipe_in_place, input_key_dispatch.cpp — called by
    //    load_render_entry_in_place for the render player's load of a batch
    //    cell,
    //    load_history_commit_in_place for the `h` view's commit load, and
    //    load_history_local_entry_in_place for that view's LOCAL walk, which
    //    puts a state of the session's own undo/redo timeline back), and — since
    //    2026-08-25 — THE WARP STATUS/VALUE FAMILY: Ctrl+D
    //    (GuiWarpMarkersOps::toggle_disabled), Ctrl+N (toggle_inherits), Delete
    //    (delete_selected_marker) and the flag editor's payload commit
    //    (GuiFlagEditor::commit_top_flag_edit), admitted in W+target as the
    //    home-view binding's FIFTH ruled exception (architect 2026-08-24, the
    //    split being POSITIONAL vs NOT) with the kick/re-land/trigger tail
    //    contracted once at the head of warpmarkers_ops.cpp.
    //    Each kicks so
    //    displayed == live at the command boundary, leaving no
    //    divergence window for the displayed-basis gestures (phase / trim drags)
    //    to ride out. Warp PLACEMENT edits are what still never call this — the
    //    drop, the marker drag and the position nudge — because THEY are what
    //    the home-view binding gates (architect 2026-07-22: W+target authors
    //    values, never position), so they author in warp's SOURCE home only,
    //    where the source waveform has no map-dependent plate.
    //
    // AXIS 2 — the CADENCE. EVERY MAP-EDIT SITE ABOVE IS NOW A DISCRETE ONE-SHOT
    // per command: the one exception was the TEMPO DRAG, which re-warped LIVE per
    // cent step mid-gesture (the kick a hand-copied list kept dropping), and it
    // died with the gesture on 2026-07-29 — so no map edit runs per pointer frame
    // any more.
    // (The live-per-event kick that remains is apply_strip_drag_zoom — a
    // generic viewport rebuild, not a map edit, a sustained pointer gesture
    // or a following play's scroll (follow_scanner) paying one full rebuild
    // per displayed frame since the rebuild became owed, 2026-10-13, below.)
    //
    // The ASYNC worker path (the request_waveform_sync_ fallback above,
    // kick_waveform_render) is not a map-edit route: it serves the UNDRIVEN
    // changes — a window RESIZE and the launch load — and repaints the
    // plate on preview completion. NONE of those is an explicit kick: they are
    // discovered by maybe_enqueue_waveform_render's dirty-detect on the next
    // tick (main.cpp), so since 2026-09-02 kick_waveform_render has NO caller
    // but the callback-unwired fallback at the tail of kick_waveform_sync
    // below (grep-derived). FOLLOW-SCROLL LEFT THIS LIST that day — it was the
    // last explicit async kick and the one the wiring in main.cpp was written
    // for — because the async page left every surface painting the OLD
    // viewport while the scanner's column sat outside it, so the playhead line
    // vanished for a frame or two at each crossing; its reasoning lives at
    // follow_scroll_if_needed. Panning left the list 2026-07-26 for the same
    // rule: a mover the user sees renders synchronously like zoom. So the
    // playback mover is on the synchronous route, once per frame the follow
    // scroll moves the picture (follow_scanner, send N, 2026-11-15).
    //
    // THE REBUILD IS OWED, NEVER RUN HERE (architect 2026-10-13, P4 approved):
    // the callback marks the synchronous rebuild owed
    // (GuiPaintHandler::owe_waveform_rebuild, which also declares the
    // rebuild's rect so the frame that pays it is scheduled) and the
    // pre-paint hook runs it ONCE, at its tail (main.cpp) — after the
    // scanner advance and Follow's scroll, either of which may kick, and before
    // the owed lane check. The 2026-07-26 ruling that every mover takes the
    // synchronous full rebuild keeps its intent: EVERY FRAME takes one, from
    // the latest state, so no frame paints overlays against a stale plate;
    // the only renders skipped are of states no display ever showed (a
    // drain's 1.76 touch events per frame on the tablet's pinch, measured
    // 2026-10-13, each rendered inline before). It is synchronous per frame
    // on the main thread, NEVER A WORKER: the asynchronous pan fast path
    // stays closed (closed_questions.md). The reclamp below still runs at
    // once, every caller expecting clamped geometry on return.
    // THE FLUSH (GuiPaintHandler::flush_owed_waveform_rebuild) is for an
    // event-time READER of what the rebuild publishes — the plate and its
    // fingerprint (plate_viewport_basis), the flag cache, the staged stash,
    // the `h` lane's list (AppState::HistoryMode::flags). The staged stash is
    // promoted only by on_redraw, so a press resolving against the displayed
    // geometry (displayed_or_live_target_map, item_viewport_basis, the
    // promoted hit rects) reads the same thing either way and needs none.
    // THREE SITES FLUSH, by inventory 2026-10-13 (every caller class above and
    // every event-time reader of the rebuild's output examined; no caller
    // reads that output after its own kick in the same event — the drop, the
    // camera moves and the edit tails all read live or displayed state — and
    // the readers below read it in a LATER event of the same drain):
    //   * GuiInputHandler::run_waveform_lane_playhead_step, ahead of its
    //     plate-basis read: a held arrow's next repeat in the same drain must
    //     read the column the previous step's camera write placed, which the
    //     inline rebuild had published;
    //   * GuiInputHandler::republish_history_lane_now, after its kick: the
    //     history edge's drop and refill of the lane's list stay one swap in
    //     the press, for the keyboard and click readers of that list;
    //   * GuiInputHandler::on_key's bare 1 / 2 / 3 / 4 selector arm, inside the
    //     `h` view alone, after both switches: Tab's cycle, Center and Revert
    //     in a later event of the drain read the lane's list, which must be
    //     the entering column's (the reason at drop_lane_stash_across_history_edge's
    //     view-switch paragraph).
    std::function<void()> request_waveform_sync_;
    void kick_waveform_sync() {
        // Render FINAL clamped geometry: reclamp through the one zoom/viewport
        // chokepoint BEFORE the synchronous rebuild is owed. A target-view map edit
        // (tempo step, engine-scale commit) can change the
        // target total, hence the per-file effective zoom ceiling and the
        // viewport walls; the edit tails call this at the OLD zoom/viewport, so
        // without the reclamp the sync render paints stale geometry and the next
        // tick's live-total backstop (main.cpp) has to re-clamp and render a
        // SECOND time — a structural double synchronous render per press at full
        // zoom-out. clamp_viewport_start clamps the level first (clamp_zoom_level)
        // then snaps/clamps the viewport, and is IDEMPOTENT: for every caller that
        // already clamped before kicking (the zoom paths, move_playhead_to,
        // center-on-playhead, apply_zoom_to_start, apply_strip_drag_zoom, undo, the
        // load-in-place,
        // active_views, the settings editor, the tick backstop) it is a pure no-op.
        // A RE-VALIDATION, SO IT KEEPS THE LATTICE (ViewportLattice::Keep,
        // architect 2026-10-31): it writes no start of its own, so a gesture's
        // rest on its own lattice — every pinch frame reaches this call —
        // stands as the gesture left it (the rule at
        // AppState::viewport_lattice_origin).
        // The tick backstop (main.cpp) remains cheap belt-and-braces insurance
        // for any future total-changing path that skips this reclamp — with
        // every edit tail synchronous now, no NAMED asynchronous case drives
        // it, and the one edit that defers its re-land to a gesture's commit
        // (the value drag) is gated out of that backstop for the gesture's
        // life, so it reaches the tick exactly once, here, through this call.
        clamp_viewport_start(app, audio, ViewportLattice::Keep);
        // Repair the resting playhead and any live region against the (possibly
        // shrunk) live domain, AFTER the zoom/viewport reclamp so it reads the
        // final geometry and its damage is covered by the rebuild's full-width
        // damage below. Idempotent — a no-op when nothing left
        // the domain — so the callers that already clamp their own playhead pay
        // only two compares. The tick backstop mirrors this call.
        clamp_display_state_to_live_domain();
        if (request_waveform_sync_) request_waveform_sync_();
        else                        kick_waveform_render();
    }

    // THE GAIN CATEGORY'S ONE OWNER (the category is inventoried in the caller
    // inventory above): a gain input's writer kicks the synchronous rebuild
    // ONLY WHEN THE PLATE'S GAIN FIELD (waveform_gain_fingerprint, which owns
    // the gate that can answer flat) ACTUALLY CHANGED across it. The caller
    // captures `waveform_gain_hash()` BEFORE its write and hands it to
    // `kick_waveform_sync_if_gain_changed` AFTER; the comparison lives here and
    // nowhere else. A write the picture cannot see must not drain the worker
    // and re-render the whole plate.
    uint64_t waveform_gain_hash() const;
    void     kick_waveform_sync_if_gain_changed(uint64_t prior_hash);

    // THE MARKER DRAG RELEASE'S SEAM (2026-09-14; the rule
    // is at MarkerDragOps::commit_drag's tail), a True Colors flip's
    // that leaves the plate's inks alone (the apply shape at
    // install_true_colors' declaration, render.h), and every same-event
    // rebuild of the stems' ONE-STATE RULE — the selection's publish among
    // them (2026-11-05; the inventory is at
    // GuiPaintHandler::paint_marker_stems, paint_handler.cpp).
    // refresh_flag_cache: the FLAG CACHE ALONE, synchronously — the same
    // fingerprint-guarded GuiPaintHandler::maybe_rebuild_flag_cache the
    // synchronous plate rebuild's tail and the tick run, with no plate render.
    // Wired in main.cpp; a no-op unwired (the tick then catches the flags).
    std::function<void()> refresh_flag_cache_;
    void refresh_flag_cache() {
        if (refresh_flag_cache_) refresh_flag_cache_();
    }
    // THE PLAYHEAD'S COLUMN ON THE PLATE BASIS (architect 2026-11-16, the
    // beats): playhead_pixel_x of the cursor as it stands, on
    // GuiPaintHandler::plate_viewport_basis — the column the cursor's line
    // and its ruler head are PAINTED on, which this struct cannot compute
    // (it sees no paint handler; the playheads' damage rule,
    // playhead_pixel_x, app_state.h). Wired in main.cpp beside the hook
    // above; TWO READERS, the two narrow playhead writes below —
    // tow_playhead_to and drag_playhead_to (through reseat_playhead) — each
    // of which widens to the whole area unwired.
    std::function<int()> plate_playhead_column_;
    // THE SCANNER'S EXACT PLACE ON THE PLATE BASIS (architect 2026-11-16,
    // send Q): scanner_exact_x of the scanner as it stands, on
    // GuiPaintHandler::plate_viewport_basis — the place its line is PAINTED
    // at (paint_scanner), which this struct cannot compute for the same
    // reason (it sees no paint handler; the playheads' damage rule,
    // playhead_pixel_x, app_state.h). The playhead column hook's twin, wired
    // in main.cpp beside it. Meaningful only while
    // app.playhead_scanner_active (scanner_exact_x's contract). ONE READER,
    // invalidate_standing_scanner below, which widens to the whole area
    // unwired.
    std::function<double()> plate_scanner_x_;
    // THE TOOLTIP'S BOX AS ITS PAINTER WILL LAY IT OUT (architect
    // 2026-11-16, the beats): GuiPaintHandler::tooltip_box_for_show — the
    // hull of the standing owner's box, or empty where none would paint —
    // for the show edge's damage (show_tooltip_box, input_pointer.cpp),
    // which cannot size a box whose words only the painter shapes. Wired in
    // main.cpp; unwired, the show falls back to its full-width band.
    std::function<GuiRect()> tooltip_box_for_show_;
    // THE CARDS AS THE PAINTER WILL LAY THE STACK OUT (architect 2026-11-16,
    // the beats): GuiPaintHandler::notification_stack_laid_out, for the
    // stack's damage owner below (invalidate_notification_stack), which
    // cannot size a card whose sentence only the painter shapes and wraps.
    // Wired in main.cpp; unwired, that owner damages the stack's room.
    std::function<void(std::vector<AppState::NotificationPainted>&)>
        notification_stack_laid_out_;

    // Viewport mutators.
    // THE MOVEMENT OWNER — the cursor's live chokepoint, and one of the TWO
    // owners that END THE A/B AUDITION (the other being the land). A command
    // that reaches this function is moving the playhead's position in the
    // music; the rule and its exemptions are stated once at the definition
    // (viewport.cpp). UNCONDITIONAL — a write onto the frame the cursor already
    // holds still ends the act.
    void move_playhead_to(int64_t new_sample);
    // THE MOVEMENT OWNER AT A DRAG'S CADENCE (architect 2026-11-16, the
    // beats): move_playhead_to's act whole — the audition's end, the hold
    // put out, the reseat — with the playhead's own narrow damage where the
    // move scrolled nothing. ONE CALLER, the ruler head's drag
    // (GuiInputHandler::place_playhead_at_click_column); the contract is at
    // the definition (viewport.cpp).
    void drag_playhead_to(int64_t new_sample);
    // A marker drop's seat of the playhead: move_playhead_to, then the
    // landing owner's walk over the seated playhead (the drop lands as Tab
    // does, architect 2026-10-29), keeping a standing hold where the walk did
    // not center (the posture's third exemption; the rule and body are at
    // the definition, viewport.cpp).
    void seat_playhead_on_drop(int64_t new_sample);
    // THE RESEAT — the identical write with NO audition end, for the callers whose write
    // is not a movement (contract at the definition, viewport.cpp). Named rather
    // than spelled as a flag on the mover: "skip the rule this time" would be the
    // old hand-listed inventory in disguise. Its callers are named at the
    // definition; do not add one without an argument for why the cursor is not
    // moving in the music.
    void reseat_playhead_to(int64_t new_sample);
    // The reseat's one body for its two entries (reseat_playhead_to and
    // drag_playhead_to): `own_columns` chooses the no-scroll branch's
    // damage, the whole waveform area or the playhead's own columns on the
    // plate basis (the definition, viewport.cpp). Not an entry of its own.
    void reseat_playhead(int64_t new_sample, bool own_columns);
    // THE TRANSLATION — the reseat's cursor write with NO keep-visible scroll
    // either (architect 2026-09-22): the cursor follows its own musical
    // instant into a rebuilt domain and the camera stays where it stands,
    // however far off screen the cursor lands. Named rather than a flag on the
    // reseat, the movement owners' convention. ONE CALLER, the undo/redo
    // restore's map-change re-land (undo.cpp), with the whole waveform
    // area's damage; the contract is at the definition (viewport.cpp).
    void translate_playhead_to(int64_t new_sample);
    // THE TOW — the translation's write for THE MARKER DRAG'S PLAYHEAD
    // (marker_drag.cpp, architect 2026-09-24: no scroll under a held hand),
    // WITH THE PLAYHEAD'S OWN NARROW DAMAGE (architect 2026-11-16, the
    // beats): the cursor's old and new columns on the plate basis and the
    // clock cell, nothing else. A second entry rather than a flag on the
    // translation, the movement owners' convention again; ONE CALLER,
    // MarkerDragOps::apply_drag_motion. The contract is at the definition
    // (viewport.cpp).
    void tow_playhead_to(int64_t new_sample);
    // The waveform lane's Left / Right step, in the step's own unit
    // (playhead_arrow_step_landing, app_state.h): one millisecond, or on
    // the phase-reset column one hop.
    void move_playhead_by_arrow_step(HorizontalArrowStep step);
    void apply_zoom_change(double new_zoom_level);
    // Strip-drag apply: set the level and place the song anchor (anchor_sample,
    // frames) at anchor_x (its drifted column, window px in fractional pixels) —
    // rather than centering on the playhead the way apply_zoom_change does.
    // THREE callers (re-derived 2026-11-15): the touch-nav body
    // apply_touch_nav_update (which folds its pan into the placement itself —
    // the anchor is the song frame the pan seated, placed at the current
    // fractional finger centroid, 2026-10-22), the zoom step zoom_step
    // (the viewport's centre frame held at the centre column, one final
    // event per press) and the follow scroll (follow_scanner, send N,
    // 2026-11-15: the scanner held at the centre at the standing level, a pan
    // frame the play makes, never final). All pre-clamp the level; this
    // places the anchor at
    // the new level and clamps. For a pure pan
    // (level unchanged) the placement reproduces the caller's post-pan viewport
    // exactly. Never touches the playhead or selection. Repaint dispatch: a
    // mid-gesture event (final=false) with the level AND viewport both unchanged
    // after the clamp is a true NO-OP and returns without repainting; a
    // moved event runs one full synchronous rebuild, whichever axis moved (the
    // incremental pan fast-path that pan-only frames once used was retired
    // 2026-07-26). The terminating event (final=true) always runs the one
    // synchronous rebuild plus the predictor resync so the rest state is exact.
    // `lattice` is the chokepoint's lattice rule for the placement (architect
    // 2026-10-31; the rules at AppState::viewport_lattice_origin): the
    // touch-nav body passes Exact on a frame that changes the level (the
    // pivot's frame then sits at its column to one frame) and Keep on a pan
    // frame; the zoom step passes Exact, a landing; the follow scroll Keep.
    void apply_strip_drag_zoom(double new_zoom_level, double anchor_sample,
                               double anchor_x, bool final,
                               ViewportLattice lattice);
    // THE ZOOM STEP (architect 2026-10-27: bare `=` / `-` and the band's
    // Zoom In / Zoom Out keys one level, since 2026-11-08 the only step):
    // `levels` whole levels from the current
    // one (negative deeper — zoom_step_levels, gui_input.h), saturating at
    // the map's live bounds (zoom_step_level, app_state.h), ZOOMING ABOUT THE
    // VIEWPORT'S CENTRE — the frame at the waveform's centre column stays
    // there, SEATED ONCE PER RUN OF PRESSES and held while the camera rests
    // where the last press left it (architect 2026-10-28,
    // AppState::zoom_step_pivot), so the run accumulates no grid error and IN
    // then OUT is a round trip off the walls and the level bounds; any other
    // camera move ends the run. A
    // silent no-op where the step would move nothing
    // (zoom_step_actionable, whose verdict the keys' faces read too). In
    // every effect a zoom like the Ctrl+drag and the pinch, whose applier it
    // runs (contract at the definition, viewport.cpp).
    void zoom_step(double levels);
    // Zoom-to-span apply: set the level AND the viewport start EXPLICITLY (the
    // start is a framed span's left edge, NOT a playhead recenter — the sole
    // difference from apply_zoom_change), then funnel both through the clamp
    // chokepoints (the lattice rule + range clamp every zoom/viewport write uses)
    // and repaint exactly as apply_zoom_change does. IDEMPOTENT: if the resting
    // (level, start) after clamping equals the current viewport, it is a true
    // no-op — no repaint and no state left behind — so a second identical framing
    // does nothing, while any pan/zoom in between
    // makes the target differ and this re-frames; the test is
    // zoom_to_start_would_move (app_state.h), asked at the head before any
    // write (2026-10-25), the one owner the Zoom to Fit key's face also reads
    // through span_framing_would_act. Its callers: the span
    // framer frame_span_into_view (run_span_framing_command and the
    // cannot-fit arm of land_and_frame_active_markers — the group undo/redo
    // restore and the phase-reset pastes' landing) and bare `0`'s restore of
    // its stamped view
    // (run_overview_command), whose start is the stamp's, not a span's.
    // `parked_origin` names the rule the start rests by, at every call: empty
    // for a framing, which lands Exact; the stamp's own lattice origin for
    // the recall, which rests its start on it (clamp_viewport_start_parked,
    // app_state.h: the view returned to is the view left).
    void apply_zoom_to_start(double new_zoom_level, int64_t new_start,
                             std::optional<int64_t> parked_origin);
    void center_viewport_on_playhead();
    // THE NUDGE HOLDS ITS SUBJECT'S COLUMN while the HOLD posture stands
    // (architect 2026-09-17; chosen by AppState::camera_hold, which an
    // explicit centring arms, since 2026-09-23 — the Ctrl modifier's choice
    // for the day before): a step that moved something places the viewport so the playhead it just
    // landed paints at the exact place the subject held before the step,
    // held between the waveform's first and last columns, never changing the
    // zoom. No centring, no lamp, no view term. The prior place is the
    // subject's PAINTED EXACT x (architect 2026-09-24, strictly as painted;
    // the exact place, not its column, since the no-grid arc, 2026-11-15),
    // each caller deriving it on its subject's painted basis — the marker
    // nudge on the item basis its step anchored on, the playhead step on the
    // plate basis the cursor is drawn with, the undo / redo singleton
    // restore under the hold (a nudge for the camera's purposes, 2026-09-25)
    // on the item basis before the restore — reduced to one held sample
    // offset and written through clamp_viewport_start. The clamp derivation
    // and the three callers are at the definition.
    void hold_subject_column_after_nudge(double prior_x);
    // The changed-path tail the one-shot playhead camera moves share (the two
    // above and the two below).
    void finish_discrete_viewport_move();
    // Follow's page-in (AppState::follow): an offscreen subject lands the
    // edge margin (kViewportEdgeMarginFraction, app_state.h) in from the LEFT
    // edge. It suspends no follow with its own write
    // (AppState::follow_suspended). Its callers at the definition.
    void follow_scroll_if_needed();
    // THE FOLLOW SCROLL (architect 2026-11-15, send N, kept on his glass
    // ("very stable ... even though it's moving"); the rule's one owner at
    // the definition): the picture moves only so that the
    // moving line never passes the waveform area's center — a line left of
    // it runs toward it over a still picture, one right of it is placed on
    // it through the pan's own road (apply_strip_drag_zoom, the Keep
    // lattice, no predictor resync), one off screen to the left is paged in.
    // It suspends no follow with its own write. Its callers: the pre-paint
    // advance during the following play (main.cpp), the launch's
    // LaunchCamera::Follow and the FOLLOW lamp's lit edge during a play.
    void follow_scanner();
    // THE LANDING OWNER (architect 2026-09-24): the one camera that brings a
    // walked or restored subject — the active-domain range [lo, hi], lo == hi
    // for a single marker — on screen, READING THE ZOOM at the discrete act,
    // its answers chosen by `kind`. WALK: at the working zoom, as painted,
    // the subject is CENTRED, on screen or not; at every other zoom, finer or
    // coarser, an on-screen subject moves nothing and an off-screen one is
    // PAGED IN, lo landing the edge margin in from the LEFT edge. RESTORE: a subject WHOLLY ON SCREEN moves
    // nothing at every zoom; an off-screen one that fits is CENTRED on its
    // midpoint at every zoom; a range that CANNOT FIT (its margined window,
    // edge_margined_window_frames in app_state.h, wider than the live painted
    // window) returns FALSE having written nothing. The
    // WALK's centring ARMS THE HOLD POSTURE, so the hold stands only at the
    // working zoom (architect 2026-09-28); a restore's centring never arms
    // it (architect 2026-09-24), and a singleton restore reaches it with the
    // hold dark or across a tab or audio-view switch (under a hold in the view
    // it started in the restore holds the column instead, 2026-09-25). The
    // zoom is never written. Its readers, the one
    // caller of the false verdict and the ruled-out cameras are at the
    // definition (viewport.cpp).
    [[nodiscard]] bool land_subject(int64_t lo, int64_t hi, LandingKind kind);

    // Repair the LIVE display-state fields after a map edit that changed the
    // active-domain total (a target-view tempo cent step, the settings
    // engine-scale commit, undo/redo, the load-in-place — every
    // total-changing warp-map edit; the full grep-derived caller inventory lives
    // at kick_waveform_sync above).
    // Clamps the resting cursor playhead back into [0, live_total - 1] through
    // the shared clamp_playhead_to_live_domain chokepoint. Called from kick_waveform_sync
    // (the one chokepoint every total-changing sync tail funnels through) and
    // mirrored in main.cpp's tick backstop; idempotent and cheap, which is why a
    // held cent step pays nothing when nothing is out of domain. A structural
    // no-op in source view (the source total never changes).
    void clamp_display_state_to_live_domain();

    // Invalidation.
    //
    // THE VIEWPORT'S LANES (2026-10-13, ON SCREEN IS AS PAINTED): the rect
    // runs, at the window's whole width, from the toolbar band's foot
    // (viewport_lanes_top_y, app_state.h; the tab strip's 2026-10-18 to
    // 2026-10-22) through the waveform's bottom, and
    // holds every lane that DRAWS the viewport, the zoom or the cursor — the
    // view bar (paint_trim: the trim window is the
    // span), the ruler (its ticks and digits, the cursor's head), the marker
    // lane (the cues and their labels), the lane's foot line, and the
    // canvas (the plate, the grid, the center lines, the stems, the
    // playheads, the overlays) — with the floating surfaces over them and
    // the folder overlay's band, which starts at the same row. THE ROWS
    // ABOVE IT DRAW NONE OF THOSE: the toolbar band and its title bar paint
    // the legend's battery and clock and the roster's faces (the app's
    // caption painted the window's state and the project's name too, until
    // 2026-10-24). Those FACES may be predicates over the view (Zoom to
    // Fit's reads the zoom level and the viewport start — its plain fork
    // and, since 2026-10-25, its frame term; constant for 2026-10-24 alone),
    // and many more move with the acts that
    // reach this rect beside a viewport change (Undo / Redo and Save after
    // an edit, an undo or a load in place), so the rows are not dropped
    // from the damage but OWED A CHECK (damage_window_top_rows, above the
    // struct): their fingerprint, every input they read enumerated at
    // upper_rows_fingerprint (paint_handler.cpp), is compared before the
    // frame paints, and the rows repaint when it has moved — so every
    // caller of this owner that relied on its rect reaching the window's
    // top is covered by one check rather than by a list.
    void invalidate_waveform_area();
    // THE STATUS BAR'S DAMAGE FOR THE PROCESS LINE (architect 2026-11-19,
    // send Z; the bar under the bottom icon row): status_bar_area whole, the bar's own lane. The bar is
    // STATE — the tick's comparator (refresh_status_bar, main.cpp) holds the
    // composed string against the one last painted and damages this same
    // lane while they differ, so no change of the bar can go stale. These
    // calls are the WRITERS' OWN DECLARATION beside it, for one reason: the
    // comparator runs at the tick, ahead of the pump's worker completions
    // (GuiPlatform::pump's order), so a line a completion clears would
    // reach the screen one beat after the picture that completion also
    // damaged; the writer's own damage puts both in one frame. A field
    // written elsewhere (the selection count) rides the comparator alone.
    //
    // THE CALLER INVENTORY — membership is "this route writes the process
    // line, queue_progress_text", nine sites:
    //   * input_render_dispatch's THREE (the promote, the BATCH'S OWN WRITE at
    //     each cell's dispatch, and finalize_render_run);
    //   * target_render's SIX (the "Updating..." stamp, the run hold's late
    //     clear, the TWO context-ending clears — the target view leaving and
    //     the T->S exit — the render-done failure clear and the completion
    //     tail's guarded one).
    // The `h` WALK LINE has no site of its own here: it rides the mode's
    // edges, which invalidate the whole window. Routes that damage the whole
    // window (the loads, the resize path) cover this lane as a superset.
    void invalidate_status_bar_area();
    // THE ACTIVE TAB'S NAME beside the clock on the bottom icon row (2026-10-22;
    // paint_bottom_row_buttons_and_clock): its one route of change is the tab
    // switch (GuiActiveViews' switch), its one caller. THE RECT IS THE BOTTOM
    // ROW'S LANE WHOLE, deliberately: the name's extent is measured only at
    // paint and published nowhere, so "Tab A" and "Tab B" erase each other
    // only inside the lane; it covers the clock cell as a superset. NONE
    // while the on-screen keyboard stands over the row (the definition).
    void invalidate_tab_name_area();
    // THE MODAL'S SURFACE — THE UNIFIED BOTTOM ROW'S LANE (the modal moved
    // onto the row 2026-08-13, and the row yields to it whole), WITH THE
    // MODAL FAMILY'S WHOLE AREA while a question's page, the progress page
    // or a line editor's value sub-page stands or was last painted
    // (2026-10-14, modal_box.h — the box over the canvas column and its
    // controls row; the definition says why). ITS CALLERS,
    // re-greped 2026-10-14, are the LINE EDITORS' closers plus the
    // RENDER PLAYER's three, the player being the row's third owner (A
    // STANDING EDITOR'S EDITS — typing, the completions, the caret blink, a
    // refusal, the pointer's text drag — take the entry's box alone since
    // 2026-10-14, invalidate_dialog_editor_box below):
    //
    //   * THE CLOSERS — every commit / abandon that deactivates a line
    //     editor (settings_editor's applied / unchanged / exit_no_commit,
    //     and flag_editor's BPM field commit and exit_bpm_mode), and the settings
    //     editor's prefill, part of its open.
    //   * THE RENDER PLAYER'S ROW (2026-08-28), which paints into the same
    //     lane and damages it through the same owner — TWO sites:
    //     GuiRenderPlayer::damage_row for every transport, highlight and
    //     clock/scrub change (render_player.cpp), and the one stop body's
    //     player fork for the play/pause face (playback_lifecycle.cpp). (The
    //     window-activation hook was a third until nothing on the row read
    //     the window's focus any more, 2026-10-02..03.)
    //   * THE PER-TICK COMPARATORS (main.cpp): the player row's enabled-face
    //     drift, and a standing dialog hint whose words the row's paint has
    //     recomposed past the ones the box last drew (2026-09-29, below).
    //
    // The PROMPTS are deliberately absent: every raise and every answer
    // damages the whole window (prompt.cpp), which is what a surface with no
    // published rect before its first paint owes; their CLOSERS that come
    // through here take the family's area by the stash's painted owner. The
    // rect is the lane whole and the family's area, so a CLOSER that
    // runs between paints still erases what the last frame drew and the
    // row's own tenants come back with it.
    //
    // A STANDING DIALOG TOOLTIP RIDES THIS DAMAGE (2026-09-29):
    // a modal button's hint is composed by the row's own paint
    // (paint_modal_dialog publishes it on the stash) and its box hangs OFF
    // the modal's controls row, past this rect — ABOVE ITS BUTTON, the seat
    // on every surface (architect 2026-11-07, session 90; tooltip_box_rect),
    // over whatever the row stands under — so a
    // row repaint alone would recompose
    // the words (the Play/Pause face's "Pause" becoming "Play" at a folder's
    // end or at a pause) and leave the old box's pixels standing.
    // While a tooltip stands on a Dialog owner this owner also damages the
    // box's published rect (erasing the old words, which may measure wider)
    // and the band every seat of the recomputed box lies in
    // (tooltip_damage_band), so the frame that repaints the row repaints the
    // hint from the same composition. Every caller above inherits it, the
    // player's three included; the per-tick comparator in main.cpp calls here
    // for the hint changes no row damage carries (the previous-track window's
    // edge under a playing item, which only the clock's cell damages).
    void invalidate_modal_dialog_area();
    // A STANDING LINE EDITOR'S EDIT (2026-10-14): THE VALUE SUB-PAGE'S
    // ENTRY'S BOX (modal_box.h's modal_page_lines, `entry`; architect
    // 2026-11-19: the entry one line that scrolls sideways; architect
    // 2026-11-20: the entry a fact of the view, never moving or growing
    // with the text, so the rect is the geometry's own and needs no
    // painted stash); never the family's whole area. WHY: under a box the
    // GL canvas's clip is empty and every
    // damage rect uploads whole (GuiGlCanvas::present_frame), so the
    // family's area on every keystroke was ~11.8 MB on the tablet, sustained
    // under key repeat; the entry is 2256 x 56 px there. WHAT AN EDIT CAN
    // CHANGE, re-derived from the painter:
    // the text, its scroll offset, the selection and the caret, all inside
    // the entry (paint_value_page: the text cut at the
    // entry's inner edges, the caret at its padding box). Nothing outside it
    // moves with an edit: the label is the open's; a refusal's or a full
    // field's card damages its own
    // stack (GuiNotifications::notify); the on-screen keyboard's keys and
    // lamps damage their own band.
    // THE OPEN, THE CLOSE, the keyboard's show and hide and every geometry
    // change keep their whole damage. ITS CALLERS
    // (re-greped 2026-11-20): TYPING, the on-screen keyboard's LEFT / RIGHT
    // (the scroll's road) and the completions — route_modal_editor_key's
    // `repaint` argument from the line editor key handler's three kinds
    // (input_key_dispatch) and the settings editor's completers
    // (settings_editor); THE CARET BLINK — main.cpp's per-tick
    // transitions; THE REFUSAL — every refused commit in settings_editor,
    // the trim preset name's (input_key_dispatch) and the BPM field
    // commit's (flag_editor), the text selected whole; THE POINTER'S TEXT
    // DRAG — input_pointer's `g.dialog` sites (the glass's caret drag,
    // click-to-caret, the drag's motion and its release).
    void invalidate_dialog_editor_box();
    // The unified bottom row's CLOCK CELL, and the authoritative inventory of
    // who wants it
    // (2026-08-11, when the timestamp moved off the status line and the one
    // owner split in two — the rects and the split's reasoning are at
    // clock_invalidate_rect, app_state.h; the 2026-08-12 row unification
    // merged the two LANES but kept the two CELL owners). MEMBERSHIP IS "THIS ROUTE CHANGES
    // WHAT THE CLOCK SHOWS" — a write to the playhead or the scanner, the
    // only two values the clock reads, or a flip of WHICH of the two it
    // reads (the playback edges). The list below is re-derived by grep
    // rather than inherited, each member with its own reason:
    //
    //   CLOCK ALONE —
    //   * Viewport's four CURSOR WRITERS: reseat_playhead_to (the live
    //     chokepoint's write half — move_playhead_to and drag_playhead_to
    //     are that same write with the audition's end in front of it, so
    //     all three spellings damage this
    //     cell through the one body), translate_playhead_to (the undo/redo
    //     restore's no-scroll re-land, 2026-09-22), tow_playhead_to (the
    //     marker drag's no-scroll tow, 2026-09-24) and
    //     clamp_display_state_to_live_domain's playhead repair.
    //   (Viewport's three ZOOM APPLIERS — apply_zoom_change,
    //     apply_strip_drag_zoom, apply_zoom_to_start — are NOT members since
    //     2026-10-13: they move the VIEWPORT AND ZOOM ONLY — none contains a
    //     playhead write or a playhead clamp, and the `c` command's centring
    //     READS the resting cursor without moving it — so they owe the cell
    //     a check instead, owe_clock_check below.)
    //   * The playback lifecycle's stop and launch: each flips which value
    //     the clock reads (scanner vs cursor).
    //   * target_render's TRIGGER-FREEZE stop (the target-view edit halt),
    //     the same flip: reachable mid-audition by the W+target cent step —
    //     the one mutator the keyboard stop rule lets through un-stopped —
    //     whose re-land is idempotent and whose stop the tick's catch-up
    //     branch cannot see (both flags already false). The ensure_ready /
    //     rebind_to_source conditional stops are recorded dead-in-practice
    //     backstops behind the S/T toggle's own lifecycle stop
    //     (input_handler.cpp's "before mutating playhead state" call) and
    //     deliberately carry no clock call.
    //   * main.cpp's tick sites: the pre-paint scanner advance (the value
    //     advances), the car's loop wrap (the scanner jumps back to the
    //     trim's start, beside its full-area waveform damage), and the
    //     PAINT-CLOCK KEEPERS, which change nothing the clock shows and are
    //     members because the cell is on screen whenever the bottom icon row
    //     is: the playing heartbeat's OFFSCREEN FALLBACK (when the scanner's
    //     line yields no damage; never under the on-screen keyboard, every
    //     editor's open stopping play, clock_invalidate_rect's record), the
    //     A/B audition's REST (every tick while a rest
    //     stands, so the display's idle timer never fires inside the act,
    //     2026-10-18) and the display keep-alive's brief window after a
    //     fast Stop (the tick's head, one damage per
    //     GuiPlatform::kDisplayKeepAliveMs). (The resize/total-change
    //     repair's geometry-moved arm owes the cell a check since 2026-10-13,
    //     like the zoom appliers — owe_clock_check below; the cursor clamp
    //     that CAN move the value damages the clock itself inside
    //     clamp_display_state_to_live_domain.)
    //   * input_pointer's one direct cursor write:
    //     seat_playhead_on_domain_frame — the direct writer every marker land,
    //     Tab/`c` jump and history diff-flag click rides (history lands call
    //     it directly; seat_playhead_on_source_frame is the source-to-domain
    //     wrapper feeding it for marker lands, whether through
    //     land_playhead_on_marker / land_playhead_on_domain_frame or
    //     reseat_playhead_on_marker). (The region sweep's per-motion cursor
    //     write and the trim commit's park left with 2026-11-07's ruling,
    //     session 88: no trim writer moves the playhead.)
    //
    // ONE ROUTE SPELLS A SECOND SURFACE BESIDE THIS CELL: the A/B tab switch,
    // whose tab name beside the clock takes invalidate_tab_name_area (above),
    // the bottom row's lane whole, a superset of this cell. Routes that damage the
    // WHOLE window (the S/T audio-view toggle's full-window invalidate, the
    // loads, the resize path) cover this cell as a superset and are
    // deliberately unlisted; routes that funnel
    // through a listed owner (the settings editor's active-tab playhead
    // commit through move_playhead_to, every marker land through
    // seat_playhead_on_domain_frame (via seat_playhead_on_source_frame), the nudges through their shared tail)
    // need no row of their own.
    void invalidate_clock_area();
    // THE CLOCK CELL'S CHECK (2026-10-13), the zoom appliers' clock damage —
    // apply_zoom_change, apply_strip_drag_zoom and apply_zoom_to_start —
    // and main.cpp's tick repair's geometry-moved arm: each moves the
    // viewport and the zoom and never the playhead or the scanner (the
    // cursor clamp that can move the value damages the clock itself, inside
    // clamp_display_state_to_live_domain), so the cell is owed a check of
    // its fingerprint (clock_fingerprint, paint_handler.cpp) rather than a
    // repaint, and repaints only where what it shows has moved. Each caller
    // damages the viewport's lanes beside it, so a frame — and its pre-paint
    // check (GuiPaintHandler::settle_owed_lane_damage) — follows.
    void owe_clock_check();
    // Narrow PLAYHEAD damage: the union (or the pair) of the two given
    // COLUMNS' rects (playhead_invalidate_rect; playhead_pixel_x's
    // columns, each holding the line and the head painted at the exact
    // place within half a px of it). The columns must be resolved on the PLATE basis — the
    // playheads' pixels are plate-registered, and damage follows the basis of
    // the pixels it erases (the rule, and the table of which sites take a
    // narrow shape versus a full-area widening, live at playhead_pixel_x in
    // app_state.h). TWO CALLERS (re-derived by grep 2026-11-16), the two
    // per-motion cursor writes, each reaching the plate's column through
    // plate_playhead_column_: the marker drag's tow (tow_playhead_to above)
    // and the ruler head's drag (drag_playhead_to, in reseat_playhead's
    // no-scroll branch); the
    // scanner's sites — the per-frame advance, the launch and the stop —
    // take the scanner's own rect below, and every other playhead write is
    // discrete and widens.
    void invalidate_playhead_columns(int old_px, int new_px);
    // THE SCANNER'S NARROW DAMAGE (architect 2026-11-16, the beats): the
    // scanner's own rect (scanner_invalidate_rect, main.cpp: the hull of its
    // line's span over the canvas's rows, nothing above the canvas) at its
    // old and its new EXACT PLACE, the two united into one rect where they
    // touch (overlap or abut: one region, one redraw) and declared apart
    // otherwise; a place whose line has no part inside the waveform area
    // declares nothing. The places must be resolved on the PLATE basis
    // (scanner_exact_x through GuiPaintHandler::plate_viewport_basis) — the
    // scanner's pixels are plate-registered, and damage follows the basis of
    // the pixels it erases (the rule and the per-site table at
    // playhead_pixel_x, app_state.h). THE CALLERS ARE EXACTLY THE TWO
    // PER-FRAME SCANNER SITES, THE STANDING SCANNER'S DAMAGE AND THE HOVER
    // LINE'S DAMAGE BELOW (re-derived by grep 2026-11-21, send FF): main.cpp's
    // tick heartbeat (the one place twice) and its pre-paint scanner advance
    // (old, then new), both narrow BY NECESSITY at the frame's cadence and
    // both inside a scope that reaches paint_handler;
    // invalidate_standing_scanner (the one place twice), the launch's, the
    // stop's and the console's seek's, reaching the plate basis through
    // plate_scanner_x_; and invalidate_hover_line (its old and new places as
    // painted and derived, or one of them alone). IT SERVES THE
    // SCANNER'S OWN EDGES — the per-frame advance, the launch's seed,
    // the stop's teardown, the seek's jump (its old place, then its new):
    // a move of the scanner's place by a move of the viewport or of the
    // plate's basis carries that mover's own full-area damage — AND THE ONE
    // OTHER CANVAS LINE THAT MOVES BY ITSELF, the waveform's hover line
    // (invalidate_hover_line below, its one road here; its triangle in the
    // ruler, a separate box with a separate compare, takes its own road,
    // invalidate_hover_triangle). A playhead
    // write is no caller of this: the cursor has a head, and its narrow
    // shape is invalidate_playhead_columns above.
    void invalidate_scanner_line(double old_x, double new_x);
    // THE WAVEFORM'S HOVER LINE'S DAMAGE (architect 2026-11-21, send FF;
    // AppState::HoverLine, the state): the line's cells as last painted and
    // its cells at its new place, each the scanner's rect at that place
    // (scanner_invalidate_rect, main.cpp, the one rect owner: the hull of a
    // one-W line's span over the canvas's rows, NOTHING ABOVE THE CANVAS)
    // through invalidate_scanner_line — united where they touch; a side
    // that is none declares nothing (the line not painted, or gone). The
    // rows are the area's whole: where the on-screen keyboard stands over
    // the view's foot (an editor's open, which takes the line away) the
    // rect reaches under it, damage the keyboard's frame repaints anyway.
    // ONE CALLER, GuiInputHandler::set_hover_line, which calls it only where
    // the line's place changed. The line's cells alone: its triangle in the
    // ruler is invalidate_hover_triangle's (below).
    void invalidate_hover_line(std::optional<double> old_x,
                               std::optional<double> new_x);
    // THE HOVER LINE'S TRIANGLE'S DAMAGE (architect 2026-11-21, send GG;
    // AppState::HoverLine's triangle pair, the state; render.h's
    // render_hover_line, the rule): the triangle's box as last painted in
    // the ruler and its box at its new place, each the ruler head's painted
    // box (GuiPaintHandler::ruler_head_box: the head's painted reach round
    // the place's column, cut to the waveform's columns, over the ruler's
    // last cue_chip_h_px rows — 18 x 8 device px on the tablet) — united
    // where they touch, an empty side declaring nothing. THE RULER'S ROWS
    // ALONE, nothing of the canvas, the marker lane or the trim bar: the
    // ruler's painter redraws under that clip alone (paint_ruler_row). A
    // SIBLING of invalidate_hover_line, not an extension of it: the line's
    // rect is the scanner's (scanner_invalidate_rect, the canvas's rows, its
    // callers an enumerated list) and its compare the canvas list's, while
    // the triangle's box is the ruler painter's, on the plate's published
    // width, and its compare its own pair's. ONE CALLER,
    // GuiInputHandler::set_hover_line, where the triangle's place changed.
    void invalidate_hover_triangle(const GuiRect& old_box,
                                   const GuiRect& new_box);
    // THE STANDING SCANNER'S DAMAGE (architect 2026-11-16, send Q: "number
    // 3, narrowing agree"): the scanner's own rect at its place as it stands
    // — invalidate_scanner_line(x, x), x from plate_scanner_x_, the place the
    // last paint drew it at (the seed's place right after a launch's seed) —
    // and, with the hook unwired, the whole waveform area (the tow's and the
    // ruler head's drag's fallback shape). PRECONDITION: the scanner is
    // active; the caller checks (scanner_exact_x's contract). THREE
    // CALLERS (re-counted by grep 2026-11-20), GuiPlaybackLifecycle's three
    // discrete scanner edges: the stop body's teardown (the place before the
    // bit clears), the launch body's success tail (a scanner still standing
    // before the seed, then the seeded place after the camera) and THE
    // CONSOLE'S SEEK, seek_live_session_by (architect 2026-11-20: the place
    // the last paint drew before the publish, then the seeded target).
    void invalidate_standing_scanner();
    // (There is no stem-column invalidator in this struct, and since row 5 no
    // selection-driven stem damage either. invalidate_stem_column computed the
    // old singleton stem's narrow damage on the ITEM basis while the stem
    // painted on the PLATE basis; that damage was widened to
    // invalidate_waveform_area in 2026-07-30 and then deleted outright with
    // Selection's stem subject pair; the selection stem (2026-11-05) rides
    // the flag cache's selection fingerprint, whose rebuild damages each
    // stem it moved at the stem's own exact place, on the basis it was laid
    // out on (maybe_rebuild_flag_cache's tail, 2026-11-16) — run in the
    // selecting event itself by every Selection mutator's publish,
    // Selection::publish_selection_change.)
    void invalidate_top_strip();
    void invalidate_all();
    // (THE WHOLE SURFACE IS THE GUI'S GEOMETRY, the tablet's window the whole
    // panel with no border or title bar, so invalidate_all covers
    // everything the app paints; the pre-paint hook's whole-client damage
    // after a palette install or a gui_scale change is invalidate_all's.)

    // Damage ONE arbitrary rect. The FLOATING SURFACE's entry (the shift
    // tooltip; the Settings dropdown beside it until 2026-10-21): it hangs
    // outside the strip that owns it — under a top icon row key's face, over
    // the well's top lanes, or above any other owner's face (tooltip_box_rect)
    // — so
    // invalidate_top_strip alone would leave its overhang stale. The
    // hide edge damages the surface's own published rect, as painted, and
    // the show edge the rect its painter will lay the box out on
    // (tooltip_box_for_show_; architect 2026-11-16, the beats) — the box and
    // nothing else, in either case; the words comparators and a standing
    // dialog's damage keep the room every seat of the box lies in
    // (tooltip_damage_band). Since row 8 (2026-08-11) it is
    // also the BOTTOM ROW'S button-face damage entry: the transport/arrow
    // faces live in the bottom strip, so every face writer's damage fork
    // passes bottom_row_area here where the top rows call
    // invalidate_top_strip. A zero/negative rect is a no-op.
    void invalidate_rect(const GuiRect& r);
    // THE NOTIFICATION STACK'S DAMAGE (2026-08-29) — THE CARDS A CHANGE
    // CHANGED (architect 2026-11-16, the beats: the card's own rect, appear
    // / dismiss / expire, not the stack's whole room): called AFTER the
    // stack is changed, it holds the cards as last painted
    // (AppState::Notifications::painted — every stack change damages what
    // it changes, so the publication is what the screen shows) against the
    // cards as the painter will now lay them out
    // (notification_stack_laid_out_), and damages ONE RECT, the bound of
    // every card whose id and rect are not in both: a card that left, a
    // card that arrived, and every card the change moved — the rest of the
    // stack below a card that left or under one that arrived. One rect
    // because the changed cards are one run of the stack, each held whole
    // in the one clip rectangle; a card whose rect stands is outside it (a
    // card's words and class never change under its id). A change that
    // moves nothing visible damages nothing. THE ROOM
    // (notification_stack_bound, notifications.h) stays the answer where
    // the layout hook is unwired, and the painter's clip.
    // ITS CALLERS, re-derived by grep 2026-10-01, are GuiNotifications' four
    // stack changes and nothing else: notify (a push), dismiss (a card's
    // lift), dismiss_all (a held lift) and
    // fire_if_due (an expiry). The hover paints nothing since the card's X
    // retired (2026-10-01) and damages nothing, and the painter publishes the
    // rects it drew rather than damaging anything.
    void invalidate_notification_stack();

    // THE MARKER HOVER IS GONE (row 5, 2026-08-01). clear_hover_popup and
    // recompute_hover_at_cursor lived here — one clear reachable from every
    // mutation path, one recompute the motion handler and every viewport
    // mutator called — and both died with the marker-text lane they served
    // (the spell-out popup, the lane's one-run fallback tier, and the
    // pass/ref readout's hover arm). Nothing replaced THEM: a marker's value
    // is written on its flag, and no viewport-side hover state exists to keep
    // in sync. ONE MARKER HOVER SURFACE STANDS SINCE 2026-11-09 (session 100,
    // 00a87821), and it is not the viewport's: with TIPS lit an eligible
    // cue's resolved value — or, since 2026-11-17 (send U), a cut label's
    // whole flag line — hangs in the roster's tooltip box, targeted by the
    // roster's hover walk (recompute_redesign_button_hover,
    // input_pointer.cpp) and published and ended through
    // AppState::RedesignTooltip's Surface::Marker, painted by
    // paint_shift_tooltip — that model's owners, not a revival of these.
};
