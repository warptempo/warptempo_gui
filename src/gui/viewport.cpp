#include "viewport.h"

#include "audio.h"
#include "playback.h"
#include "render.h"
#include "text_editor.h"
#include "timemap.h"
#include "platform_wayland.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

std::pair<int64_t, int64_t> Viewport::trim_range() const {
    if (audio.total_frames() <= 0) return {0, 0};
    if (app.render_view_enabled) {
        // Render-view has no trim — the loaded audio is already
        // render-domain (trim baked in at render time).
        return {0, audio.total_frames()};
    }
    if (app.active_audio_view == 'T') {
        // Target view: trim is authored source-domain (b/e store
        // source-domain seconds via inverse-translation in
        // handle_trim_set_at_playhead) but Home/End needs to land
        // the playhead in the active target-frame domain. Build
        // the live timemap and forward-translate the source-domain
        // trim boundaries; unset sides fall back to 0 / live total,
        // matching compute_trim_samples' unset-side semantics for
        // S-view.
        const int sr = audio.sample_rate();
        const long total = static_cast<long>(audio.total_frames());
        const ViewState& vs = active_view_state(app);
        const int64_t live_total =
            live_total_frames(app, audio);
        if (!vs.has_trim_begin && !vs.has_trim_end) {
            return {0, live_total};
        }
        const auto tmap = build_target_view_timemap(app, sr, total);
        int64_t begin_tgt = 0;
        int64_t end_tgt   = live_total;
        if (vs.has_trim_begin) {
            const int64_t begin_src = static_cast<int64_t>(
                std::nearbyint(vs.trim_begin_seconds *
                               static_cast<double>(sr)));
            begin_tgt = to_domain_frame(app, begin_src, tmap);
        }
        if (vs.has_trim_end) {
            const int64_t end_src = static_cast<int64_t>(
                std::nearbyint(vs.trim_end_seconds *
                               static_cast<double>(sr)));
            end_tgt = to_domain_frame(app, end_src, tmap);
        }
        if (begin_tgt < 0) begin_tgt = 0;
        if (begin_tgt > live_total) begin_tgt = live_total;
        if (end_tgt > live_total) end_tgt = live_total;
        if (end_tgt < begin_tgt) end_tgt = begin_tgt;
        return {begin_tgt, end_tgt};
    }
    return compute_trim_samples(
        app, audio.sample_rate(), audio.total_frames());
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
    const GuiRect t = timestamp_invalidate_rect(
        app.height, app.width, bottom_strip_wide(app));
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
void Viewport::move_playhead_to(int64_t new_sample) {
    if (audio.total_frames() <= 0) return;
    if (new_sample < 0) new_sample = 0;
    // Live-domain total: source-frame total in source view, target-frame
    // total (cached at `t`-toggle) in target view. The playhead in target
    // view is target-frame, so its clamp must be against the deformed
    // timeline's length.
    const int64_t total = live_total_frames(app, audio);
    if (total > 0 && new_sample >= total) new_sample = total - 1;

    const double old_px = playhead_pixel_x(app, audio);
    const int64_t old_vp = app.viewport_start_sample;
    const int64_t visible = samples_visible(app, audio);

    app.playhead_sample = new_sample;

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
        invalidate_waveform_area();
    } else {
        const double new_px = playhead_pixel_x(app, audio);
        invalidate_playhead_columns(old_px, new_px);
    }
    invalidate_timestamp_area();
    // V.A3b Addendum 3: viewport may have shifted (Home/End or any
    // playhead jump that pushed the viewport). Re-evaluate hover at
    // the cursor's last known coords.
    if (viewport_changed) {
        recompute_hover_at_cursor();
    }
    if (playback.is_playing()) playback.resync_predictor();
}

void Viewport::move_playhead_pixels(int delta_px) {
    if (audio.total_frames() <= 0) return;
    const double spp = current_samples_per_pixel(app, audio);
    const int64_t delta_samples =
        static_cast<int64_t>(std::nearbyint(delta_px * spp));
    move_playhead_to(app.playhead_sample + delta_samples);
}

// Apply a zoom change. The numeric target is derived inside; this helper
// handles the playhead-centered viewport recompute so zoom_in/zoom_out
// share exactly the same logic.
void Viewport::apply_zoom_change(int new_zoom_level) {
    if (audio.total_frames() <= 0) return;
    if (new_zoom_level == app.zoom_level) return;

    app.zoom_level = new_zoom_level;

    if (app.zoom_level == kFitFileLevel) {
        app.viewport_start_sample = 0;
    } else {
        const int64_t visible = samples_visible(app, audio);
        app.viewport_start_sample =
            app.playhead_sample - visible / 2;
        clamp_viewport_start(app, audio);
    }

    invalidate_waveform_area();
    invalidate_timestamp_area();
    // Flags / hover popup live in the top strip — rect positions
    // change when the viewport scale changes.
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
    // V.A3b Addendum 3: rects shifted under the (possibly stationary)
    // cursor — re-evaluate hover.
    recompute_hover_at_cursor();
    if (playback.is_playing()) playback.resync_predictor();
}

void Viewport::zoom_in() {
    const int max_num = max_valid_numeric_level(
        waveform_area(app).w, live_total_frames(app, audio), audio.sample_rate());
    if (max_num < 0) return; // no numeric level valid; only fit-file
    if (app.zoom_level == kFitFileLevel) {
        apply_zoom_change(max_num);
    } else if (app.zoom_level > kMinNumericLevel) {
        apply_zoom_change(app.zoom_level - 1);
    }
    // else at kMinNumericLevel already — no-op.
}

void Viewport::zoom_out() {
    const int max_num = max_valid_numeric_level(
        waveform_area(app).w, live_total_frames(app, audio), audio.sample_rate());
    if (app.zoom_level == kFitFileLevel) return; // already fully out
    if (max_num < 0 || app.zoom_level >= max_num) {
        apply_zoom_change(kFitFileLevel);
    } else {
        apply_zoom_change(app.zoom_level + 1);
    }
}

void Viewport::scroll_viewport(int64_t delta_samples) {
    if (audio.total_frames() <= 0) return;
    const int64_t old_vp = app.viewport_start_sample;
    app.viewport_start_sample += delta_samples;
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) {
        invalidate_waveform_area();
        // Flag positions move with the viewport; the hover popup rides
        // along, so the top strip must repaint too.
        const GuiRect ts = top_strip_area(app);
        gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
        // V.A3b Addendum 3: rects shifted under the (possibly
        // stationary) cursor — re-evaluate hover.
        recompute_hover_at_cursor();
        if (playback.is_playing()) playback.resync_predictor();
    }
}

void Viewport::center_viewport_on_playhead() {
    if (audio.total_frames() <= 0) return;
    const int64_t visible = samples_visible(app, audio);
    const int64_t old_vp = app.viewport_start_sample;
    app.viewport_start_sample = app.playhead_sample - visible / 2;
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) {
        invalidate_waveform_area();
        const GuiRect ts = top_strip_area(app);
        gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
        // V.A3b Addendum 3: rects shifted under the (possibly
        // stationary) cursor — re-evaluate hover.
        recompute_hover_at_cursor();
        if (playback.is_playing()) playback.resync_predictor();
    }
}

void Viewport::invalidate_top_strip() {
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h + 1);
}

void Viewport::invalidate_all() {
    gui.invalidate_region(0, 0, app.width, app.height);
}

// Auto-follow during playback: when the playhead leaves the viewport,
// scroll the viewport so the playhead lands ~10% into the new view, leaving
// room ahead. Only the first move beyond vp_end triggers a scroll.
void Viewport::follow_scroll_if_needed() {
    const int64_t visible = samples_visible(app, audio);
    if (visible <= 0) return;
    const int64_t vp_end = app.viewport_start_sample + visible;
    if (app.playhead_sample < app.viewport_start_sample ||
        app.playhead_sample >= vp_end) {
        const int64_t lead = visible / 10;
        const int64_t old_vp = app.viewport_start_sample;
        app.viewport_start_sample =
            std::max<int64_t>(0, app.playhead_sample - lead);
        clamp_viewport_start(app, audio);
        if (app.viewport_start_sample != old_vp) {
            invalidate_waveform_area();
            if (playback.is_playing()) playback.resync_predictor();
        }
    }
}

// Reset the hover popup state. If the popup was visible, invalidate the
// top strip so the next paint erases it. Safe to call from any path.
void Viewport::clear_hover_popup() {
    const bool was_visible = app.hover_popup.visible;
    app.hover_popup = HoverPopupState{};
    if (was_visible) invalidate_top_strip();
}

// V.A3b Addendum 3: re-evaluate hover at the cursor's last on_motion
// coordinates. Called after viewport mutations (zoom, scroll, center,
// playhead-driven viewport shift) so a stationary cursor's hover state
// tracks the rects that just slid under it. Mirrors the on_motion
// hover-detection branch: same gating, same hit-test, same state
// transitions; the tick handler still drives the dwell-to-visible flip.
void Viewport::recompute_hover_at_cursor() {
    if (app.last_mouse_x < 0 || app.last_mouse_y < 0) return;
    // Dialog / drag / editor / queue still suppress hover in either
    // view. Source-view also requires warp view + iter mode off;
    // render-view bypasses the mode checks because hover always
    // applies against the loaded render's warpmarkers.
    if (app.prompt.active ||
        app.drag.active ||
        app.playhead_drag.active ||
        text_editor::is_active(app.top_flag_editor) ||
        app.queue_running) {
        clear_hover_popup();
        return;
    }
    if (!app.render_view_enabled &&
        (app.active_markers_view != 'W' || app.iteration_mode_enabled)) {
        clear_hover_popup();
        return;
    }
    // Brief 3a: target view's hover popup runs the same way as
    // source view's. hit_test_flag builds the target_timemap
    // internally when active_audio_view == Target so the flag rects it
    // walks match what paint_handler renders at translated columns.
    const int hit = hit_test_flag(app, audio,
                                  app.last_mouse_x, app.last_mouse_y);
    if (hit != app.hover_popup.marker_index) {
        if (app.hover_popup.visible) invalidate_top_strip();
        app.hover_popup.marker_index = hit;
        app.hover_popup.visible      = false;
        app.hover_popup.entry_time   =
            std::chrono::steady_clock::now();
        // Precompute the popup's display text at rect-entry so the
        // delay-completion paint doesn't have to recompute it. Empty
        // when `hit` is not popup-eligible (the redraw branch then
        // skips paint and keeps the strip clean).
        if (app.render_view_enabled) {
            app.hover_popup.cached_text =
                popup_eligible_marker(app, hit)
                    ? compute_hover_popup_text(
                          app.render_view_markers, hit,
                          app.render_view_src_sr)
                    : std::string();
        } else {
            app.hover_popup.cached_text =
                popup_eligible_marker(app, hit)
                    ? compute_hover_popup_text(
                          app.warpmarkers.markers(), hit,
                          audio.sample_rate())
                    : std::string();
        }
    }
}
