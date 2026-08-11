#pragma once

#include "app_state.h"

#include <cstdint>
#include <functional>
#include <utility>

class GuiAudio;
class GuiPlatform;
class GuiPlayback;

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

    // Trim helpers — THE NAVIGATION RANGE, and since 2026-08-05 nothing else:
    // the trim bounds Home/End jump to and the load-time playhead rests at.
    // PLAYBACK NO LONGER READS THIS (playback_lifecycle.cpp owns the split: the
    // song's end in source view, the bound preview buffer's in target).
    std::pair<int64_t, int64_t> trim_range() const;
    int64_t                     trim_begin_sample() const;
    int64_t                     trim_end_sample() const;

    // Worker kick: requests an immediate waveform regeneration the moment
    // the viewport changes, instead of waiting for the next platform tick.
    // Set from main.cpp to paint_handler.maybe_enqueue_waveform_render().
    // Held as a std::function rather than a GuiPaintHandler& so viewport.cpp
    // keeps no compile-time edge to paint_handler.h. kick_waveform_render()
    // is null-safe: if the callback is unset (e.g. before main.cpp wires it),
    // it no-ops. The enqueue is idempotent against the on_tick backstop —
    // both dirty-check the same pending fingerprint, so a redundant call is
    // a cheap no-op. Callers fire it only inside their actually-changed guard.
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
    // render the waveform plate inline and publish the displayed fingerprint in
    // the same handler, so every layer reflects the new state in one frame. Set
    // from main.cpp to paint_handler.force_synchronous_waveform_rebuild(). Held
    // as a std::function for the same no-compile-time-edge-to-paint_handler.h
    // reason as the kicks above. Null-safe: when unset (before main.cpp wires
    // it) it falls back to the async worker kick, so the path stays correct
    // either way.
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
    //    (move_playhead_to's offscreen-follow shift, apply_zoom_change,
    //    apply_zoom_to_start, center_viewport_on_playhead, apply_strip_drag_zoom,
    //    and scroll_viewport — every pan/scroll class, which joined this route
    //    2026-07-26 when the incremental shift-and-strip path was retired),
    //    ALL THREE VIEW SWITCHES, one class since 2026-07-30: the S/T audio-view
    //    toggle and the Ctrl+Tab A/B tab switch (both domain flips) and the `p`
    //    W/P marker-column toggle — `p` moves neither viewport nor domain, so its
    //    plate render is redundant, but active_markers_view is a flag-cache
    //    FINGERPRINT field and this is the route that lands the flag rebuild in
    //    the damaged frame; it joins the class rather than growing a second
    //    flag-only kick (the reason is stated at its site, active_views.cpp), and
    //    the settings active_markers_view= key rides it through the same
    //    function. Bare 1/2/3, the ABSOLUTE VIEW SELECTORS (2026-08-01), add NO
    //    fourth site: they run the S/T and W/P handlers themselves, so their
    //    kicks are those two entries and this list is unchanged by them.
    //    The PROPAGATE PASTE'S TARGET-VIEW TAIL
    //    (land_paste_in_target_view, phase_reset_propagate.cpp) is the `p` case
    //    reached by a different door and joined 2026-07-30: it calls
    //    switch_active_markers_view_to('P') directly rather than through the
    //    toggle, so it kicks at its own tail — after the created selection, so the
    //    column and the selection hash rebuild together — and on its W+source
    //    entry that is a harmless SECOND kick, the audio-view toggle's own having
    //    run before the column swap. Also here: the
    //    settings tab_X_viewport_start commit, the strip drag's TERMINATING-EVENT
    //    finalize — re-derived 2026-07-29, the true terminating events being
    //    release, button loss, and the force-end finalizer's three callers
    //    (Ctrl+Q, resize, WM close); Esc is NOT one of them any more, pointer
    //    gestures having no cancel — and main.cpp's tick backstop for an ASYNC
    //    total change (a preview completion) live here.
    //  - TARGET-WARP-MAP mutations: a build_warp_frame_map INPUT changed, so the
    //    target-view plate itself re-warps. RE-DERIVED 2026-07-29 when the whole
    //    tempo-image family was deleted (marker_drag.h), which took TWO entries
    //    off this list — the bare Left/Right tempo-image step and the pointer tempo
    //    DRAG. What remains: the bare Up/Down tempo step
    //    (adjust_tempo_cents, singleton AND group — the whole tempo surface now),
    //    the settings engine-scale commit, undo/redo, and ALL THREE LOAD-IN-PLACE
    //    BODIES (re-derived 2026-08-08: load_render_entry_in_place for `'` over a
    //    render entry, load_history_commit_in_place for the `h` view's commit
    //    load, and load_history_local_entry_in_place for that view's LOCAL walk,
    //    which puts a state of the session's own undo/redo timeline back — all
    //    three in input_key_dispatch.cpp).
    //    Each kicks so
    //    displayed == live at the command boundary, leaving no
    //    divergence window for the displayed-basis gestures (phase / trim drags)
    //    to ride out. Warp PLACEMENT edits (drop / delete / marker drag / the
    //    position nudge / Ctrl+N / Ctrl+D / the flag-editor commit) author in
    //    warp's SOURCE home only (home-view binding, architect 2026-07-22 —
    //    W+target authors tempo, never position), where the source waveform has no
    //    map-dependent plate, so they never call this.
    //
    // AXIS 2 — the CADENCE. EVERY MAP-EDIT SITE ABOVE IS NOW A DISCRETE ONE-SHOT
    // per command: the one exception was the TEMPO DRAG, which re-warped LIVE per
    // cent step mid-gesture (the kick a hand-copied list kept dropping), and it
    // died with the gesture on 2026-07-29 — so no map edit runs per pointer frame
    // any more.
    // (The live-per-event kicks that remain are apply_strip_drag_zoom and
    // scroll_viewport — generic viewport rebuilds, not map edits. Both are
    // sustained pointer gestures paying one full rebuild per pointer frame.)
    //
    // The ASYNC worker path (the request_waveform_sync_ fallback above,
    // kick_waveform_render) is not a map-edit route: it serves the UNDRIVEN
    // changes — follow-scroll during playback, resize — and repaints the plate
    // on preview completion. Panning left this list 2026-07-26: it is
    // user-driven, so it renders synchronously like zoom.
    std::function<void()> request_waveform_sync_;
    void kick_waveform_sync() {
        // Render FINAL clamped geometry: reclamp through the one zoom/viewport
        // chokepoint BEFORE the synchronous rebuild. A target-view map edit
        // (tempo step, engine-scale commit) can change the
        // target total, hence the per-file effective zoom ceiling and the
        // viewport walls; the edit tails call this at the OLD zoom/viewport, so
        // without the reclamp the sync render paints stale geometry and the next
        // tick's live-total backstop (main.cpp) has to re-clamp and render a
        // SECOND time — a structural double synchronous render per press at full
        // zoom-out. clamp_viewport_start clamps the level first (clamp_zoom_level)
        // then snaps/clamps the viewport, and is IDEMPOTENT: for every caller that
        // already clamped before kicking (the zoom paths, move_playhead_to,
        // center-on-playhead, apply_zoom_to_start, the strip drag, undo, the
        // load-in-place,
        // active_views, the settings editor, the tick backstop) it is a pure no-op.
        // The tick backstop (main.cpp) remains cheap belt-and-braces insurance
        // for any future total-changing path that skips this reclamp — with
        // every edit tail synchronous now, no NAMED asynchronous case drives it.
        clamp_viewport_start(app, audio);
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

    // Viewport mutators.
    void move_playhead_to(int64_t new_sample);
    void move_playhead_pixels(int delta_px);
    void apply_zoom_change(double new_zoom_level);
    // Strip-drag apply: set the level and place the song anchor (anchor_sample,
    // frames) at anchor_x (its drifted column, window px in fractional pixels) —
    // rather than centering on the playhead the way apply_zoom_change does.
    // TWO callers since 2026-08-11: apply_strip_drag_at (which has already
    // panned the viewport for this event and hands the anchor's drifted,
    // edge-rebindable column) and the two-finger touch-nav body
    // apply_touch_nav_update (which folds its pan into the placement itself —
    // the anchor is the content under the previous finger centroid, placed at
    // the current one). Both pre-clamp the level; this places the anchor at
    // the new level and clamps. For a pure pan
    // (level unchanged) the placement reproduces the caller's post-pan viewport
    // exactly. Never touches the playhead or selection. Repaint dispatch: a
    // mid-gesture event (final=false) with the level AND viewport both unchanged
    // after the clamp is a true NO-OP and returns without repainting; a
    // moved event runs one full synchronous rebuild, whichever axis moved (the
    // incremental pan fast-path that pan-only frames once used was retired
    // 2026-07-26). The terminating event (final=true) always runs the one
    // synchronous rebuild plus the predictor resync so the rest state is exact.
    void apply_strip_drag_zoom(double new_zoom_level, double anchor_sample,
                               double anchor_x, bool final);
    // Zoom-to-span apply: set the level AND the viewport start EXPLICITLY (the
    // start is a framed span's left edge, NOT a playhead recenter — the sole
    // difference from apply_zoom_change), then funnel both through the clamp
    // chokepoints (the grid snap + range clamp every zoom/viewport write uses)
    // and repaint exactly as apply_zoom_change does. IDEMPOTENT: if the resting
    // (level, start) after clamping equals the current viewport, it is a true
    // no-op — no repaint and no state left behind — so a second identical framing
    // does nothing, while any pan/zoom in between
    // makes the target differ and this re-frames. The sole caller is the
    // trim-bar double-click (run_span_framing_command).
    void apply_zoom_to_start(double new_zoom_level, int64_t new_start);
    void zoom_in();
    void zoom_out();
    // Coalesced zoom: apply |in_steps| zoom levels in a single shot.
    // Positive in_steps zooms in, negative zooms out. Equivalent in final
    // state to calling zoom_in()/zoom_out() |in_steps| times, but resolves
    // to one apply_zoom_change so invalidate + worker-kick fire once per
    // pointer frame instead of once per detent. in_steps == +/-1 reproduces
    // zoom_in()/zoom_out() exactly.
    void zoom_steps(int in_steps);
    // `continuous` marks a drag-driven scroll, which suppresses the per-event
    // playback predictor resync (re-anchored once at gesture end). There is no
    // longer a `synchronous` flag: it selected between the two pan drivers, and
    // with the incremental path retired every scroll renders synchronously.
    void scroll_viewport(int64_t delta_samples, bool continuous = false);
    void center_viewport_on_playhead();
    void follow_scroll_if_needed();

    // Repair the LIVE display-state fields after a map edit that changed the
    // active-domain total (a target-view tempo cent step, the settings
    // engine-scale commit, undo/redo, the load-in-place — every
    // total-changing warp-map edit; the full grep-derived caller inventory lives
    // at kick_waveform_sync above).
    // Clamps the resting cursor playhead back into [0, live_total - 1] through
    // the shared clamp_playhead_to_live_domain chokepoint, and CLEARS a live
    // region whose either bound left that domain. Called from kick_waveform_sync
    // (the one chokepoint every total-changing sync tail funnels through) and
    // mirrored in main.cpp's tick backstop; idempotent and cheap, which is why a
    // held cent step pays nothing when nothing is out of domain. A structural
    // no-op in source view (the source total never changes).
    void clamp_display_state_to_live_domain();

    // Invalidation.
    void invalidate_waveform_area();
    // ROW 9, the status lane: section C's whole precedence chain and the
    // critical chip. The bottom strip's high-traffic owner — the editors and
    // their carets, the queue / render / transient strings, the selection
    // readout — and the DEFAULT for anything writing a string down there.
    void invalidate_status_row_area();
    // ROW 8's clock cell, and the authoritative inventory of who wants it
    // (2026-08-11, when the timestamp moved off the status line and the one
    // owner split in two — the rects and the split's reasoning are at
    // clock_invalidate_rect, app_state.h). MEMBERSHIP IS "THIS ROUTE CHANGES
    // WHAT THE CLOCK SHOWS" — a write to the playhead or the scanner, the
    // only two values the clock reads, or a flip of WHICH of the two it
    // reads (the playback edges). The list below is re-derived by grep
    // rather than inherited, each member with its own reason:
    //
    //   CLOCK ALONE —
    //   * Viewport's two CURSOR WRITERS: move_playhead_to (the live
    //     chokepoint) and clamp_display_state_to_live_domain's playhead
    //     repair.
    //   * Viewport's three ZOOM APPLIERS (apply_zoom_change,
    //     apply_strip_drag_zoom, apply_zoom_to_start): HARMLESS OVER-DAMAGE,
    //     kept as truth-over-churn. All three move the VIEWPORT AND ZOOM
    //     ONLY — none contains a playhead write or a playhead clamp (the
    //     earlier "through their playhead clamp" description here was
    //     false), and keyboard zoom's centering ON the scanner READS the
    //     value without moving it — so the clock's value cannot change under
    //     them. The calls are 66d2ec58's rename-in-place of the
    //     whole-status-lane damage these sites carried while the timestamp
    //     lived on that lane; one small always-clean rect per discrete zoom
    //     is cheaper than the churn of removing them.
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
    //   * main.cpp's three tick sites: the pre-paint scanner advance (the
    //     value advances), the playing heartbeat's OFFSCREEN FALLBACK (the
    //     always-onscreen honest rect that keeps the paint clock running
    //     when the scanner column yields no damage), and the
    //     resize/total-change repair's geometry-moved arm (over-damage like
    //     the zoom appliers'; the cursor clamp that CAN move the value
    //     damages the clock itself inside clamp_display_state_to_live_domain).
    //   * input_pointer's two direct cursor writes:
    //     land_playhead_on_source_frame — the land owner every marker land,
    //     Tab/`c` jump and history diff-flag click ride — and on_motion's
    //     sliver release.
    //
    //   BOTH LANES — the routes that land a playhead AND rewrite the readout,
    //   each calling the two owners in turn: the A/B tab switch (active_views,
    //   which restores the entering tab's own playhead), the undo/redo restore
    //   (undo), the position nudges' shared tail (position_nudge), two of the
    //   three load-in-place tails (render entry and history commit — each
    //   applies the loaded sidecar's own band, playhead included; the HISTORY
    //   LOCAL member's tail calls the same two owners but its clock half is
    //   HARMLESS OVER-DAMAGE like the zoom appliers': a timeline state
    //   carries no band, so that tail never writes the live playhead — its
    //   coincidence auto-select fires only where the land is a provable no-op
    //   (auto_select_marker_at_playhead's equality predicate is the land's
    //   own) — and the small always-clean rect is kept as truth-over-churn
    //   beside the tail's real status-lane rewrite), and the TRIM COMMIT
    //   WRITERS (input_trim:
    //   commit_trim_mutation and handle_trim_clear_both damage the waveform +
    //   status lane at their own sites and reach the clock inside
    //   park_playhead_at_trim_start — the trim family's one cursor writer,
    //   where the damage sits beside the write rather than copied per
    //   caller).
    //
    // Every OTHER route on the status lane is a string writer and takes that
    // lane alone; routes that damage the WHOLE window (the S/T audio-view
    // toggle's full-window invalidate, the loads, the resize path) cover the
    // cell as a superset and are deliberately unlisted; routes that funnel
    // through a listed owner (the settings editor's active-tab playhead
    // commit through move_playhead_to, every marker land through
    // land_playhead_on_source_frame, the nudges through their shared tail)
    // need no row of their own. THE `p` MARKER-VIEW TOGGLE IS THE NEAR MISS
    // worth naming, since it sits beside the tab switch and answers
    // differently: it damages the READOUT (its coincidence auto-select can
    // change the selection) and moves no playhead at all, so it takes the
    // status lane alone.
    void invalidate_clock_area();
    // Narrow playhead/scanner damage: the union (or the pair) of the two given
    // COLUMNS' rects. The columns must be resolved on the PLATE basis — the
    // playheads' pixels are plate-registered, and damage follows the basis of
    // the pixels it erases (the rule, and the table of which sites take this
    // narrow shape versus a full-area widening, live at playhead_pixel_x in
    // app_state.h). THE CALLERS ARE EXACTLY THE TWO PER-FRAME SCANNER SITES
    // (re-derived by grep 2026-07-30): main.cpp's tick heartbeat and its
    // pre-paint scanner advance, both narrow BY NECESSITY at 60 Hz and both
    // inside a scope that reaches paint_handler. The Tab/`c` jump's no-scroll
    // branch was the third until it moved onto land_playhead_on_marker and took
    // that owner's full-area shape with it. Every other playhead write is
    // discrete and widens; do not add a narrow caller for one.
    void invalidate_playhead_columns(double old_px, double new_px);
    // (There is no stem-column invalidator any more, and since row 5 no
    // selection-driven stem damage either. invalidate_stem_column computed the
    // old singleton stem's narrow damage on the ITEM basis while the stem
    // painted on the PLATE basis; that damage was widened to
    // invalidate_waveform_area in 2026-07-30 and then deleted outright with
    // Selection's stem subject pair, because stems no longer key on selection
    // at all.)
    void invalidate_top_strip();
    void invalidate_all();

    // Damage ONE arbitrary rect. The FLOATING SURFACES' entry (the shift
    // tooltip and the menu row's dropdown): both hang BELOW the top strip, so
    // invalidate_top_strip alone would leave their overhang stale. Show and hide
    // edges damage the strip AND the surface's own published rect — two cheap
    // calls the platform coalesces — rather than growing a union helper that
    // only these two callers would ever use. Since row 8 (2026-08-11) it is
    // also the TRANSPORT ROW'S damage entry: that row's faces live in the
    // bottom strip, so every face writer's damage fork passes
    // bottom_transport_row_area here where the top rows call
    // invalidate_top_strip. A zero/negative rect is a no-op.
    void invalidate_rect(const GuiRect& r);

    // THE MARKER HOVER IS GONE (row 5, 2026-08-01). clear_hover_popup and
    // recompute_hover_at_cursor lived here — one clear reachable from every
    // mutation path, one recompute the motion handler and every viewport
    // mutator called — and both died with the marker-text lane they served
    // (the spell-out popup, the lane's one-run fallback tier, and the
    // pass/ref readout's hover arm). Nothing replaced them: a marker's value
    // is written on its flag now, so there is no hover-only surface left to
    // keep in sync. The redesigned rows' own button hover
    // (recompute_redesign_button_hover, input_pointer.cpp) is unrelated and
    // untouched.
};
