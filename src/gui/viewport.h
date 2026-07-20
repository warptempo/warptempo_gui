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
    // rest. Synchronous (true): the Alt+drag grab-pan (through scroll_viewport) —
    // the mid-gesture frame must never paint over a stale-basis plate, so a busy
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
    // (tab / marker navigation), viewport recenters,
    // undo/redo, and the structural target-view marker ops (drop / delete /
    // commit_drag) whose warp_frame_map re-warp shifts the whole plate. Without the
    // inline rebuild the overlays (playhead, markers, flags) land a frame ahead
    // of the waveform, flashing. In-place fine-tune edits (nudge / jump /
    // toggle-disabled / adjust_tempo_cents) deliberately omit it: they don't move the
    // viewport, the async invalidate keeps pace, and a synchronous rebuild per
    // keystroke would tax the drag-time torrent the async path exists to absorb.
    std::function<void()> request_waveform_sync_;
    void kick_waveform_sync() {
        if (request_waveform_sync_) request_waveform_sync_();
        else                        kick_waveform_render();
    }

    // Viewport mutators.
    void move_playhead_to(int64_t new_sample);
    void move_playhead_pixels(int delta_px);
    void apply_zoom_change(double new_zoom_level);
    // Zoom-strip drag zoom: set the level and place the song position
    // (anchor_sample, frames) at anchor_x (the fixed press column, window px, in
    // fractional pixels) — rather than centering on the playhead the way
    // apply_zoom_change does. The row is zoom-only (no pan), so the anchor stays
    // pinned to its press column and the zoom pivots around that song position.
    // Never touches the playhead or selection. A mid-gesture event (final=false)
    // that does NOT change the level is a wall-saturated NO-OP and returns at the
    // top without repainting; a level-changed event runs one full synchronous
    // rebuild for this frame (affordable because the platform coalesces captured
    // motion to at most one event per pointer frame). The terminating event
    // (final=true) runs the one synchronous rebuild plus the predictor resync so
    // the rest state is exact.
    void apply_strip_drag_zoom(double new_zoom_level, double anchor_sample,
                               double anchor_x, bool final);
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

    // Invalidation.
    void invalidate_waveform_area();
    void invalidate_timestamp_area();
    void invalidate_playhead_columns(double old_px, double new_px);
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
