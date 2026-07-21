#include "viewport.h"

#include "audio.h"
#include "playback.h"
#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "platform_wayland.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

std::pair<int64_t, int64_t> Viewport::trim_range() const {
    if (audio.total_frames() <= 0) return {0, 0};
    if (app.active_audio_view == 'T') {
        // Target view: trim is authored source-domain (b/e store
        // whole int64 source frames via inverse-translation in
        // handle_trim_x's set-from-region) but Home/End needs to land
        // the playhead in the active target-frame domain. Build
        // the live warp_frame_map and forward-translate the source-domain
        // trim boundaries; unset sides fall back to 0 / live total,
        // matching compute_trim_samples' unset-side semantics for
        // S-view.
        const int64_t live_total =
            live_total_frames(app, audio);
        if (!app.trim.has_begin && !app.trim.has_end) {
            return {0, live_total};
        }
        int64_t begin_tgt = 0;
        int64_t end_tgt   = live_total;
        if (app.trim.has_begin) {
            const int64_t begin_src = app.trim.begin_frame;
            begin_tgt = source_frame_to_active_domain(app, audio, begin_src);
        }
        if (app.trim.has_end) {
            const int64_t end_src = app.trim.end_frame;
            end_tgt = source_frame_to_active_domain(app, audio, end_src);
        }
        // Per-side clamp to the deformed timeline only — no ordering clamp:
        // inverted bounds pass through as authored, mirroring
        // compute_trim_samples' contract (crossed cannot rest, but
        // mid-gesture crossing is free and this runs per frame, so
        // consumers must not assume begin <= end).
        if (begin_tgt < 0) begin_tgt = 0;
        if (begin_tgt > live_total) begin_tgt = live_total;
        if (end_tgt < 0) end_tgt = 0;
        if (end_tgt > live_total) end_tgt = live_total;
        return {begin_tgt, end_tgt};
    }
    return compute_trim_samples(app, audio.total_frames());
}

int64_t Viewport::trim_begin_sample() const { return trim_range().first; }
int64_t Viewport::trim_end_sample()   const { return trim_range().second; }

// Viewport changes repaint the waveform area and the top strip together:
// flag positions depend on the viewport, so any pan/zoom has to refresh
// flags as well as waveform. Playhead-only moves keep using the narrow
// column invalidation below.
void Viewport::invalidate_waveform_area() {
    const GuiRect a = waveform_area(app);
    const int y0 = 0;
    const int y1 = a.y + a.h;
    gui.invalidate_region(0, y0, app.width, y1 - y0);
}

void Viewport::invalidate_timestamp_area() {
    const GuiRect t = timestamp_invalidate_rect(app);
    gui.invalidate_region(t.x, t.y, t.w, t.h);
}

void Viewport::invalidate_playhead_columns(double old_px, double new_px) {
    const GuiRect area = waveform_area(app);
    const GuiRect r_old = playhead_invalidate_rect(area, old_px);
    const GuiRect r_new = playhead_invalidate_rect(area, new_px);
    // Union when close (common case: 1px nudges overlap) — one expose.
    if (rects_intersect(r_old, r_new) ||
        std::abs(new_px - old_px) < 4.0) {
        const GuiRect u = union_rect(r_old, r_new);
        if (u.w > 0 && u.h > 0) {
            gui.invalidate_region(u.x, u.y, u.w, u.h);
        }
    } else {
        if (r_old.w > 0) gui.invalidate_region(r_old.x, r_old.y, r_old.w, r_old.h);
        if (r_new.w > 0) gui.invalidate_region(r_new.x, r_new.y, r_new.w, r_new.h);
    }
}

// move_playhead_to: update playhead, keep viewport so playhead stays
// visible. Invalidate only what changed. Clamps to the full audio
// range; trim is purely cosmetic so the playhead is free to sit in
// the dim region.
//
// The [0, total - 1] live-domain clamp is the shared ruling spelled at
// clamp_playhead_to_live_domain (app_state.h) — this gesture route funnels
// through it exactly like every non-gesture sync route, so they can never
// disagree about the same endpoint.
void Viewport::move_playhead_to(int64_t new_sample) {
    if (audio.total_frames() <= 0) return;
    new_sample = clamp_playhead_to_live_domain(new_sample, app, audio);

    const double old_px = playhead_pixel_x(app, audio);
    const int64_t old_vp = app.viewport_start_sample;
    const int64_t visible = samples_visible(app, audio);

    app.playhead_cursor_sample = new_sample;
    // Split-playhead invariant: when the scanner is inactive its
    // sample tracks the cursor. Mouse-click-during-playback paths
    // keep scanner_active true and update the scanner via the audio
    // thread's reseek; this branch only fires for the idle / stopped
    // case the gesture callers funnel through here after stop.
    if (!app.playhead_scanner_active) {
        app.playhead_scanner_sample = new_sample;
    }

    const int64_t vp_end = app.viewport_start_sample + visible;
    bool viewport_changed = false;

    if (new_sample < app.viewport_start_sample) {
        app.viewport_start_sample = new_sample;
        viewport_changed = true;
    } else if (new_sample >= vp_end) {
        const double spp = current_samples_per_pixel(app, audio);
        const int64_t one_px = static_cast<int64_t>(std::nearbyint(spp));
        app.viewport_start_sample =
            new_sample - (visible - std::max<int64_t>(one_px, 1));
        viewport_changed = true;
    }
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) viewport_changed = true;

    if (viewport_changed) {
        // One-shot discrete viewport shift (Home / End, navigate-to-marker, or
        // an arrow nudge that pushed the playhead past the edge). Render the
        // plate synchronously so the playhead / marker overlays do not land a
        // frame ahead of the new viewport window. Click-drop callers land their
        // target inside the visible strip and so never reach this branch; the
        // callers that do reach it are all discrete, so a full sync render here
        // is bounded.
        // kick_waveform_sync emits the same waveform-region damage
        // invalidate_waveform_area does, so the explicit call is left as a
        // harmless coalesced duplicate.
        invalidate_waveform_area();
        kick_waveform_sync();
    } else {
        const double new_px = playhead_pixel_x(app, audio);
        invalidate_playhead_columns(old_px, new_px);
    }
    invalidate_timestamp_area();
    // Viewport may have shifted (Home/End or any playhead jump that pushed the
    // viewport). Re-evaluate hover at the cursor's last known coords.
    if (viewport_changed) {
        recompute_hover_at_cursor();
    }
    if (playback.is_playing()) playback.resync_predictor();
}

void Viewport::move_playhead_pixels(int delta_px) {
    if (audio.total_frames() <= 0) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;
    // Snap to the grid the renderer draws on: the playhead column is
    // nearbyint((cursor - viewport_start)/spp). Resolve the current column,
    // step it by delta_px, then map back to a sample at that column. This
    // makes one keypress move exactly one pixel even when spp is fractional
    // (e.g. the 2.4 s zoom), instead of a sub-pixel sample step that can leave
    // the displayed column unchanged. Finer adjustment is the 1.2 s zoom's job.
    const double cur_px =
        static_cast<double>(app.playhead_cursor_sample - app.viewport_start_sample)
        / spp;
    const int64_t cur_col = static_cast<int64_t>(std::nearbyint(cur_px));
    const int64_t target_col = cur_col + static_cast<int64_t>(delta_px);
    const int64_t new_sample = app.viewport_start_sample +
        static_cast<int64_t>(std::nearbyint(static_cast<double>(target_col) * spp));
    move_playhead_to(new_sample);
}

// Apply a zoom change. The numeric target is derived inside; this helper
// handles the playhead-centered viewport recompute so zoom_in/zoom_out
// share exactly the same logic.
void Viewport::apply_zoom_change(double new_zoom_level) {
    if (audio.total_frames() <= 0) return;
    // Pre-clamp the requested level to the per-file window so (a) a c/0 request
    // at a short file's ceiling is a TRUE no-op (equal after clamp → early
    // return) rather than assign-then-revert, and (b) the centering `visible`
    // below is computed at the FINAL level. clamp_viewport_start re-applies the
    // identical clamp as the chokepoint; this only sharpens the no-op detection
    // and the centering math here.
    new_zoom_level = clamp_zoom_level(app, audio, new_zoom_level);
    if (new_zoom_level == app.zoom_level) return;

    // Capture the scanner's pre-reflow pixel-x under the OLD viewport so
    // the next pre-paint can damage the actually-painted column. The
    // recomputed scanner_pixel_x against the post-reflow viewport points
    // at a column the scanner was never painted at, leaving a ghost.
    if (app.playhead_scanner_active) {
        app.playhead_scanner_old_px_stash = scanner_pixel_x(app, audio);
    }

    app.zoom_level = new_zoom_level;

    // Split-playhead: during playback zoom tracks the audio under review
    // (scanner); otherwise tracks the launch point (cursor). The two are equal
    // by invariant when the scanner is inactive, so this only matters during
    // playback. At the effective ceiling samples_visible == total, so
    // clamp_viewport_start's visible >= total branch parks the start at 0
    // (whole song visible) without any mode test.
    const int64_t target = app.playhead_scanner_active
        ? app.playhead_scanner_sample
        : app.playhead_cursor_sample;
    const int64_t visible = samples_visible(app, audio);
    app.viewport_start_sample = target - visible / 2;
    clamp_viewport_start(app, audio);

    invalidate_waveform_area();
    invalidate_timestamp_area();
    // Flags live in the top strip — rect positions change when the viewport
    // scale changes. (The hovered marker's lane text renders in the top strip's
    // marker-text lane, covered by this top-strip invalidation; the pass/ref
    // resolved readout is covered by the bottom-strip invalidation above.)
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
    // Rects shifted under the (possibly stationary)
    // cursor — re-evaluate hover.
    recompute_hover_at_cursor();
    if (playback.is_playing()) playback.resync_predictor();
    // Reaching here means the zoom level changed (early-return above guards
    // the no-op case), so the waveform fingerprint differs. Zoom is a one-shot
    // discrete jump: render the plate synchronously and publish the displayed
    // fingerprint now so the top-strip flags and the playhead column do not
    // jump a frame ahead of the waveform. The pyramid bounds per-column cost at
    // every level, so a full render is O(area_width) at any zoom; coalesced
    // pointer detents resolve to one apply_zoom_change per frame, so this is one
    // sync render per frame, not per detent. zoom_in / zoom_out / zoom_steps
    // delegate here, so they are covered without a separate kick.
    kick_waveform_sync();
}

void Viewport::apply_strip_drag_zoom(double new_zoom_level, double anchor_sample,
                                     double anchor_x, bool final) {
    if (audio.total_frames() <= 0) return;

    const bool   level_changed = (new_zoom_level != app.zoom_level);
    const int64_t old_vp       = app.viewport_start_sample;

    // Capture the scanner's pre-reflow pixel-x under the OLD viewport (as
    // apply_zoom_change does) so the next pre-paint damages the actually-painted
    // column instead of leaving a ghost when the view changes under playback.
    if (app.playhead_scanner_active) {
        app.playhead_scanner_old_px_stash = scanner_pixel_x(app, audio);
    }

    app.zoom_level = new_zoom_level;

    // Place the song anchor at anchor_x: pick the viewport start that paints
    // anchor_sample at that column, at the new level. current_samples_per_pixel
    // reads the level just assigned, so this is spp(new_level); for a pure pan
    // (level unchanged) it reproduces the caller's post-pan viewport exactly,
    // recovered by nearbyint. At the effective ceiling the anchor cannot pin a
    // column (the whole song is visible); clamp_viewport_start's visible >= total
    // branch parks the start at 0 and the drag is inert.
    const double spp = current_samples_per_pixel(app, audio);
    app.viewport_start_sample = static_cast<int64_t>(std::nearbyint(
        anchor_sample - anchor_x * spp));
    clamp_viewport_start(app, audio);
    const bool vp_changed = (app.viewport_start_sample != old_vp);

    // Mid-gesture true NO-OP: when the post-clamp level AND viewport are both
    // unchanged this frame (a wall-saturated pan or zoom), nothing moves — skip
    // the apply and every invalidation rather than repaint an identical frame.
    // The terminating event (final) always proceeds so the rest state re-anchors
    // the predictor and rebuilds the plate exactly.
    if (!final && !level_changed && !vp_changed) return;

    invalidate_waveform_area();
    invalidate_timestamp_area();
    // Flags live in the top strip — rect positions change when the viewport
    // scale or start changes.
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
    // Rects shifted under the (possibly stationary) cursor — re-evaluate hover.
    recompute_hover_at_cursor();

    // Rest state (final) re-anchors the playback predictor once, like the
    // continuous pan's release; mid-gesture events do NOT resync (the predictor
    // keeps extrapolating smoothly for the drag's duration). Repaint dispatch:
    // the terminating event and every level-CHANGED frame rescale the whole plate
    // and take one SYNCHRONOUS full rebuild (the exact cost a keyboard zoom pays
    // per press, capped at once per pointer frame by the platform's motion
    // coalescing); a level-UNCHANGED but viewport-moved frame is a pure pan and
    // rides the synchronous incremental shift-and-strip fast-path instead.
    if (final) {
        if (playback.is_playing()) playback.resync_predictor();
        kick_waveform_sync();
    } else if (level_changed) {
        kick_waveform_sync();
    } else {
        kick_waveform_pan(app.viewport_start_sample, /*synchronous=*/true);
    }
}

void Viewport::apply_zoom_to_start(double new_zoom_level, int64_t new_start) {
    if (audio.total_frames() <= 0) return;

    // Pre-clamp the requested level to the per-file window. clamp_viewport_start
    // re-applies the identical clamp as the chokepoint; this only sharpens the
    // no-op detection below.
    new_zoom_level = clamp_zoom_level(app, audio, new_zoom_level);

    // Capture the scanner's pre-reflow pixel-x under the OLD viewport BEFORE any
    // assignment (as apply_zoom_change does), but publish it to the stash only
    // when the view actually moves — a no-op must leave no dangling ghost-repair
    // state for the next pre-paint to consume.
    const double scanner_old_px = app.playhead_scanner_active
        ? scanner_pixel_x(app, audio)
        : -1.0;

    const double  old_level = app.zoom_level;
    const int64_t old_start = app.viewport_start_sample;

    // Set the level, then the start EXPLICITLY (the span's left edge, not a
    // playhead recenter), and funnel through the two clamp chokepoints.
    app.zoom_level = new_zoom_level;
    app.viewport_start_sample = new_start;
    clamp_viewport_start(app, audio);

    // Idempotent no-op: the resting (level, start) this call would produce equals
    // the current viewport, so nothing moved — return without repaint (the writes
    // above put identical values back). A pan/zoom since the last framing makes
    // them differ and this proceeds to re-frame.
    if (app.viewport_start_sample == old_start &&
        std::fabs(app.zoom_level - old_level) < 1e-9) {
        return;
    }

    // Actually moving: publish the OLD-viewport scanner px so the next pre-paint
    // repairs the ghost, exactly as apply_zoom_change does.
    if (app.playhead_scanner_active)
        app.playhead_scanner_old_px_stash = scanner_old_px;

    invalidate_waveform_area();
    invalidate_timestamp_area();
    // Flags live in the top strip — rect positions change with the viewport.
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
    // Rects shifted under the (possibly stationary) cursor — re-evaluate hover.
    recompute_hover_at_cursor();
    if (playback.is_playing()) playback.resync_predictor();
    // A discrete one-shot viewport jump: render the plate synchronously and
    // publish the displayed fingerprint now so the top-strip flags and the
    // playhead column do not jump a frame ahead of the waveform.
    kick_waveform_sync();
}

void Viewport::zoom_in() {
    if (app.zoom_level > kMinZoom) {
        // One whole level deeper from the current (possibly fractional) rung,
        // clamped constructively at the zoom-in floor.
        double target = app.zoom_level - 1.0;
        if (target < kMinZoom) target = kMinZoom;
        apply_zoom_change(target);
    } else {
        // Already at the deepest zoom-in: recenter on the playhead.
        center_viewport_on_playhead();
    }
}

void Viewport::zoom_out() {
    const double max_l = effective_max_zoom_level(
        waveform_area(app).w, live_total_frames(app, audio), audio.sample_rate());
    if (app.zoom_level >= max_l) return;  // already at the effective ceiling
    // One whole level shallower, saturating at the effective per-file ceiling
    // (there is nothing beyond it — full zoom-out is whole-song-visible).
    double target = app.zoom_level + 1.0;
    if (target > max_l) target = max_l;
    apply_zoom_change(target);
}

void Viewport::zoom_steps(int in_steps) {
    if (in_steps == 0) return;
    if (audio.total_frames() <= 0) return;
    const double max_l = effective_max_zoom_level(
        waveform_area(app).w, live_total_frames(app, audio), audio.sample_rate());

    if (in_steps > 0) {
        // Zoom in by whole steps: subtract one whole level per step, clamped
        // constructively at the zoom-in floor.
        double target = app.zoom_level - static_cast<double>(in_steps);
        if (target < kMinZoom) target = kMinZoom;
        if (target == app.zoom_level) {
            // Net movement saturated with no change. Match zoom_in()'s recenter
            // on the playhead when a deeper zoom is asked at the deepest level.
            if (app.zoom_level == kMinZoom) center_viewport_on_playhead();
            return;
        }
        apply_zoom_change(target);
        return;
    }

    // Zoom out by whole steps: add |in_steps| whole levels, saturating at the
    // effective per-file ceiling.
    if (app.zoom_level >= max_l) return;
    double target = app.zoom_level - static_cast<double>(in_steps);
    if (target > max_l) target = max_l;
    apply_zoom_change(target);
}

void Viewport::scroll_viewport(int64_t delta_samples, bool continuous,
                               bool synchronous) {
    if (audio.total_frames() <= 0) return;
    const int64_t old_vp = app.viewport_start_sample;
    app.viewport_start_sample += delta_samples;
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) {
        invalidate_waveform_area();
        // Flag positions move with the viewport, so the top strip must
        // repaint too. (The hovered marker's lane text renders in the top
        // strip's marker-text lane, covered here; recompute_hover_at_cursor
        // below re-damages both surfaces if the hit changes — the bottom-strip
        // readout sits at a fixed screen position and needs damage only then.)
        const GuiRect ts = top_strip_area(app);
        gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
        // Rects shifted under the (possibly
        // stationary) cursor — re-evaluate hover.
        recompute_hover_at_cursor();
        // A discrete pan (alt+wheel, PageUp/PageDown) re-anchors here -
        // a single snap is invisible. A continuous drag pan passes
        // continuous=true and does NOT resync per motion event: the
        // predictor keeps extrapolating smoothly for the gesture's
        // duration and is re-anchored once when the drag ends.
        if (!continuous && playback.is_playing()) playback.resync_predictor();
        // Viewport actually moved (inside the changed guard). A scroll is a
        // pure horizontal pan, so drive the incremental shift-and-strip
        // fast-path rather than a full worker re-render — this is what keeps
        // fast touchpad scroll continuous instead of leaping. Pass the
        // post-clamp viewport start. `synchronous` selects the pan driver: the
        // Alt+drag grab-pan passes true so a busy worker is DRAINED (never a
        // mid-gesture frame over a stale-basis plate); the discrete pans
        // (Alt+wheel, PageUp/PageDown) keep the default async routing.
        kick_waveform_pan(app.viewport_start_sample, synchronous);
    }
}

void Viewport::center_viewport_on_playhead() {
    if (audio.total_frames() <= 0) return;
    // Split-playhead: during playback center on the scanner (audio
    // under review); otherwise center on the cursor (launch point).
    // The two are equal by invariant when the scanner is inactive.
    const int64_t target = app.playhead_scanner_active
        ? app.playhead_scanner_sample
        : app.playhead_cursor_sample;
    const int64_t visible = samples_visible(app, audio);
    const int64_t old_vp = app.viewport_start_sample;
    // Stash the scanner's last painted pixel-x under the OLD viewport
    // for the next pre-paint; see apply_zoom_change for rationale.
    if (app.playhead_scanner_active) {
        app.playhead_scanner_old_px_stash = scanner_pixel_x(app, audio);
    }
    app.viewport_start_sample = target - visible / 2;
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) {
        invalidate_waveform_area();
        const GuiRect ts = top_strip_area(app);
        gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
        // Rects shifted under the (possibly
        // stationary) cursor — re-evaluate hover.
        recompute_hover_at_cursor();
        if (playback.is_playing()) playback.resync_predictor();
        // Viewport actually moved (inside the changed guard). Center-on-
        // playhead is a one-shot discrete jump (the C key, and the Tab recenter
        // family) — render the plate synchronously so the playhead overlay does
        // not lead the waveform by a frame.
        kick_waveform_sync();
    }
}

void Viewport::invalidate_top_strip() {
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h + 1);
}

void Viewport::invalidate_all() {
    gui.invalidate_region(0, 0, app.width, app.height);
}

// Auto-follow during playback: when the scanner leaves the viewport,
// scroll so the scanner lands ~10% into the new view, leaving room
// ahead. Only the first move beyond vp_end triggers a scroll. Called
// at play press too (when scanner == cursor by invariant), so the same
// landing rule left-edge-aligns the viewport on the cursor if it was
// offscreen — so the scanner always issues forth from a visible
// cursor.
void Viewport::follow_scroll_if_needed() {
    const int64_t visible = samples_visible(app, audio);
    if (visible <= 0) return;
    const int64_t target = app.playhead_scanner_active
        ? app.playhead_scanner_sample
        : app.playhead_cursor_sample;
    const int64_t vp_end = app.viewport_start_sample + visible;
    if (target < app.viewport_start_sample || target >= vp_end) {
        const int64_t lead = visible / kViewportLeadDivisor;
        const int64_t old_vp = app.viewport_start_sample;
        app.viewport_start_sample = std::max<int64_t>(0, target - lead);
        clamp_viewport_start(app, audio);
        if (app.viewport_start_sample != old_vp) {
            invalidate_waveform_area();
            if (playback.is_playing()) playback.resync_predictor();
            // Viewport actually moved — kick the waveform worker now rather
            // than waiting for the next tick.
            kick_waveform_render();
        }
    }
}

// Reset the hover state. If either surface was showing, invalidate both the top
// strip (the marker-text lane) and the bottom strip (the pass/ref resolved
// readout) so the next paint erases whichever was up. Safe to call from any path.
void Viewport::clear_hover_popup() {
    const bool was_visible = app.hover_popup.any_visible();
    app.hover_popup = HoverPopupState{};
    if (was_visible) {
        invalidate_top_strip();
        invalidate_timestamp_area();
    }
}

// Re-evaluate hover at the cursor's last on_motion coordinates. The single
// hover implementation: on_motion's no-gesture path delegates here, and
// viewport mutations (zoom, scroll, center, playhead-driven viewport shift)
// call it so a stationary cursor's hover state tracks the rects that just slid
// under it. Suppression set: prompt, any_pointer_gesture_active (the pointer
// drags), the three text editors, render queue — each clears both surfaces.
// The marker view and iter mode are NOT suppressors: the lane shows every
// hovered marker's own value on BOTH columns, and in iteration mode the warp
// lane text carries the iter bracket exactly as the flag editor's seed does.
void Viewport::recompute_hover_at_cursor() {
    if (app.last_mouse_x < 0 || app.last_mouse_y < 0) return;
    if (app.prompt.active ||
        any_pointer_gesture_active(app) ||
        text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.commit_editor) ||
        text_editor::is_active(app.top_flag_editor) ||
        app.queue_running) {
        clear_hover_popup();
        return;
    }
    // Hover runs the same way in target view as in source view. hit_test_flag
    // builds the target_warp_frame_map internally when active_audio_view ==
    // Target so the flag rects it walks match what paint_handler renders at
    // translated columns.
    // Mutation-sensitive short-circuit: the same hovered marker with both stores
    // unchanged needs no re-read. A store mutation (either column) bumps its
    // generation, so an in-place edit of the hovered marker — tempo step, Ctrl+N,
    // nudge — falls through here and re-reads fields, position, eligibility, and
    // payload from the live store below, even though the hit index is unchanged.
    // The displayed-map generation joins the short-circuit: hit_test_flag
    // resolves identity against the displayed flag positions, so a silent map
    // promotion (which advances displayed_map_gen without a store mutation) can
    // move the flag under a stationary cursor. Requiring the map generation to
    // match forces a full re-read after a promotion.
    const long long warp_gen  = app.warpmarkers.generation();
    const long long phase_gen = app.phaseresetmarkers.generation();
    const long long disp_gen  = app.displayed_map_gen;
    const int hit = hit_test_flag(app, audio,
                                  app.last_mouse_x, app.last_mouse_y);
    if (hit == app.hover_popup.marker_index &&
        warp_gen  == app.hover_popup.warp_gen &&
        phase_gen == app.hover_popup.phase_gen &&
        disp_gen  == app.hover_popup.displayed_gen) return;

    // No dwell: recompute both surfaces once. The LANE shows the hovered
    // marker's own value regardless of eligibility — the canonical flag line
    // for a warp marker (flag_text_iter, the one composer the flag paint,
    // hit-rects, and the Enter editor seed all share, so lane and editor content
    // always agree) or the literal "p" for a phase reset marker. The
    // BOTTOM readout keeps the pass/ref gate (popup_eligible_marker): owners and
    // phase resets have nothing to resolve.
    const bool was_visible = app.hover_popup.any_visible();
    app.hover_popup.marker_index = hit;
    // Stamp the generations that produced this set (both columns, even when
    // hit < 0), so the short-circuit and the on_tick refresh settle until the
    // next real store change.
    app.hover_popup.warp_gen  = warp_gen;
    app.hover_popup.phase_gen = phase_gen;
    app.hover_popup.displayed_gen = disp_gen;
    app.hover_popup.source_frame = 0;
    app.hover_popup.lane_text.clear();
    app.hover_popup.readout_text.clear();
    app.hover_popup.copy_payload.clear();
    if (hit >= 0) {
        if (app.active_markers_view == 'P') {
            const auto& pv = app.phaseresetmarkers.markers();
            if (hit < static_cast<int>(pv.size())) {
                app.hover_popup.lane_text   = "p";
                app.hover_popup.source_frame = pv[hit].time_frame;
            }
        } else {
            const auto& mv = app.warpmarkers.markers();
            if (hit < static_cast<int>(mv.size())) {
                app.hover_popup.lane_text =
                    flag_text_iter(mv, hit, app.iteration_mode_enabled);
                app.hover_popup.source_frame = mv[hit].time_frame;
            }
        }
        if (popup_eligible_marker(app, hit)) {
            app.hover_popup.readout_text = compute_hover_popup_text(
                slice_to_warp_markers(app.warpmarkers.markers()), hit,
                audio.sample_rate(), audio.total_frames(),
                &app.hover_popup.copy_payload);
        }
    }
    // The lane renders in the top strip, the readout in the bottom strip; damage
    // both when either surface was showing or will show.
    if (was_visible || app.hover_popup.any_visible()) {
        invalidate_top_strip();
        invalidate_timestamp_area();
    }
}
