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

    // Trim helpers.
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

    // Incremental-pan kick: for a pure horizontal pan, drive the shift-and-
    // strip fast-path instead of the full worker re-render. Set from main.cpp
    // to paint_handler.pan_waveform_incremental(new_vp_start, synchronous); held
    // as a std::function for the same no-compile-time-edge reason as the kick
    // above. When unset (before main.cpp wires it) kick_waveform_pan falls
    // back to the full worker kick, so the pan path stays correct either way.
    // The incremental path itself falls back to the worker for any non-pure-
    // pan case, and the on_tick backstop catches residual drift.
    //
    // `synchronous` distinguishes the two pan drivers. Async (default): the wheel
    // pan and PageUp/PageDown steps in scroll_viewport — a busy worker or any
    // non-shift case defers to the worker, and the on_tick backstop catches the
    // rest. Synchronous (true): the Alt+drag grab-pan (through scroll_viewport)
    // and the strip drag's pan-only frames (through apply_strip_drag_zoom) — the
    // mid-gesture frame must never paint over a stale-basis plate, so a busy
    // worker is drained rather than deferred to, and a non-shift case falls back
    // to a full synchronous rebuild instead of enqueue-and-return.
    std::function<void(int64_t, bool)> request_waveform_pan_;
    void kick_waveform_pan(int64_t new_vp_start, bool synchronous = false) {
        if (request_waveform_pan_) request_waveform_pan_(new_vp_start, synchronous);
        else                       kick_waveform_render();
    }

    // One-shot synchronous rebuild kick: for a discrete viewport/view jump,
    // render the waveform plate inline and publish the displayed fingerprint in
    // the same handler, so every layer reflects the new state in one frame. Set
    // from main.cpp to paint_handler.force_synchronous_waveform_rebuild(). Held
    // as a std::function for the same no-compile-time-edge-to-paint_handler.h
    // reason as the kicks above. Null-safe: when unset (before main.cpp wires
    // it) it falls back to the async worker kick, so the path stays correct
    // either way.
    //
    // Callers are the discrete, one-shot repositioning events: view swaps
    // (tab / marker navigation), viewport recenters, undo/redo, adopt, and the
    // warp_frame_map edits still reachable in target view — the Alt+Up/Down
    // tempo step (adjust_tempo_cents, the one warp authoring gesture reachable
    // off its source home) and the settings engine-scale commit — each re-warps
    // the map, so the plate and the overlays (playhead, markers, flags) must
    // land in one frame with the reclamped geometry below, or the overlays jump
    // a frame ahead of a stale plate. The warp PLACEMENT edits (drop / delete /
    // marker drag / nudge / Ctrl+N / Ctrl+D / the flag-editor commit) now author
    // in warp's source home view (home-view binding, architect 2026-07-22),
    // where the source waveform has no map-dependent plate — they no longer call
    // this. The ASYNC worker path is not a map-edit route:
    // it serves viewport navigation (pan/follow/resize) and repaints the
    // plate on preview completion, both undriven-by-a-discrete-edit cases the
    // key-repeat-rate argument for staying async never applied to.
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
        // center-on-playhead, apply_zoom_to_start, the strip drag, undo, adopt,
        // active_views, the settings editor, the tick backstop) it is a pure no-op.
        // The tick backstop (main.cpp) remains cheap belt-and-braces insurance
        // for any future total-changing path that skips this reclamp — with
        // every edit tail synchronous now, no NAMED asynchronous case drives it.
        clamp_viewport_start(app, audio);
        // Repair the resting playhead and any live region against the (possibly
        // shrunk) live domain, AFTER the zoom/viewport reclamp so it reads the
        // final geometry and its damage is covered by the rebuild's full-width
        // damage below (codex P2 fix). Idempotent — a no-op when nothing left
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
    // rather than centering on the playhead the way apply_zoom_change does. The
    // caller (apply_strip_drag_at) has already panned the viewport for this
    // event; this places the anchor at the new level and clamps. For a pure pan
    // (level unchanged) the placement reproduces the caller's post-pan viewport
    // exactly. Never touches the playhead or selection. Repaint dispatch: a
    // mid-gesture event (final=false) with the level AND viewport both unchanged
    // after the clamp is a true NO-OP and returns without repainting; a
    // level-changed event runs one full synchronous rebuild; a level-unchanged
    // but viewport-moved event (a pure pan) rides the synchronous incremental pan
    // fast-path. The terminating event (final=true) always runs the one
    // synchronous rebuild plus the predictor resync so the rest state is exact.
    void apply_strip_drag_zoom(double new_zoom_level, double anchor_sample,
                               double anchor_x, bool final);
    // Zoom-to-span apply: set the level AND the viewport start EXPLICITLY (the
    // start is a framed span's left edge, NOT a playhead recenter — the sole
    // difference from apply_zoom_change), then funnel both through the clamp
    // chokepoints (the grid snap + range clamp every zoom/viewport write uses)
    // and repaint exactly as apply_zoom_change does. IDEMPOTENT: if the resting
    // (level, start) after clamping equals the current viewport, it is a true
    // no-op — no repaint, and the scanner ghost-repair stash is left untouched —
    // so a second identical framing does nothing, while any pan/zoom in between
    // makes the target differ and this re-frames. The sole caller is the
    // zoom-strip double-click (run_zoom_double_click_command).
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
    void scroll_viewport(int64_t delta_samples, bool continuous = false,
                         bool synchronous = false);
    void center_viewport_on_playhead();
    void follow_scroll_if_needed();

    // Repair the LIVE display-state fields after a map edit that changed the
    // active-domain total (a target-view tempo step / drag, the settings
    // engine-scale commit, undo/redo — every total-changing warp-map edit).
    // Clamps the resting cursor playhead back into [0, live_total - 1] through
    // the shared clamp_playhead_to_live_domain chokepoint, and CLEARS a live
    // region whose either bound left that domain. Called from kick_waveform_sync
    // (the one chokepoint every total-changing sync tail funnels through) and
    // mirrored in main.cpp's tick backstop; idempotent and cheap so a per-cent-
    // step tempo drag pays nothing when nothing is out of domain. A structural
    // no-op in source view (the source total never changes).
    void clamp_display_state_to_live_domain();

    // Invalidation.
    void invalidate_waveform_area();
    void invalidate_timestamp_area();
    void invalidate_playhead_columns(double old_px, double new_px);
    // Hover-transition stem damage: the SELECTED-marker stem (paint_selected_stem)
    // is a live WAVEFORM overlay whose hover arm appears/disappears as the pointer
    // enters/leaves a marker's flag, but a hover change only damages the top strip
    // / timestamp. Damage the given active-column marker's stem column (waveform
    // height) so a hover moving between markers, or on/off one, repaints it. Since
    // round 4 the stem shows only for the SELECTED singleton when hovered, so this
    // over-damages the non-selected markers' columns — harmless (a bounded blit),
    // and it keeps the appear/disappear on the SELECTED marker's flag covered.
    // (The drag and nudge arms are covered by their own full-waveform damage.)
    // No-op when idx < 0 or the column is offscreen.
    void invalidate_hover_stem_column(int idx, int64_t source_frame);
    void invalidate_top_strip();
    void invalidate_all();

    // Reset the hover popup state. If the popup was visible, invalidate
    // the readout area so the next paint erases it. Safe to call from any
    // path.
    void clear_hover_popup();

    // Recompute the hover state at the cursor's last on_motion position.
    // Called from viewport mutators (so a scroll/zoom updates which
    // marker is under the cursor) and from the platform tick (so the
    // dwell-to-visible flip fires after delay).
    void recompute_hover_at_cursor();
};
