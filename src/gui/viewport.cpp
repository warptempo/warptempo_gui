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

void Viewport::invalidate_hover_stem_column(int idx, int64_t source_frame) {
    if (idx < 0) return;
    if (audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return;
    // The stem's column on the DISPLAYED item basis — the honest source, since the
    // selected-marker stem paints against the promoted item mirror
    // (displayed_viewport_basis), not the live viewport. Both the displayed MAP
    // and the displayed VIEWPORT/spp: the invariant is damage-follows-the-pixels —
    // this erases the COMMITTED DISPLAYED stem pixels, correct regardless of
    // whether live and displayed currently coincide. It matters during an async
    // publish window, where the live viewport already holds the NEW span while the
    // stem still paints at the OLD displayed column — a live-basis column would
    // then miss the stem entirely (the mixed-basis hazard the pin stamp went
    // full-area to avoid); at a fully settled rest with no publish pending the two
    // bases happen to coincide, but nothing here relies on that. Both consumers
    // inherit this: damage_marker_stem_column's click-appear damage and the hover
    // recompute's appear/disappear transitions (old + new columns).
    const DisplayedViewportBasis basis = displayed_viewport_basis(app, audio);
    const int col = painted_column_of_source_frame_on_basis(
        app, audio, static_cast<double>(source_frame),
        displayed_or_live_target_map(app, audio),
        basis.vp_start, basis.spp);
    if (col < 0 || col >= area.w) return;
    // A narrow full-waveform-height band around the 1px stem (+AA slack).
    constexpr int kStemPad = 2;
    int x0 = area.x + col - kStemPad;
    int x1 = area.x + col + kStemPad + 1;
    if (x0 < area.x)              x0 = area.x;
    if (x1 > area.x + area.w)     x1 = area.x + area.w;
    if (x1 <= x0) return;
    gui.invalidate_region(x0, area.y, x1 - x0, area.h);
}

void Viewport::damage_marker_stem_column(int idx) {
    // Explicit stem-column damage so the marker CLICK's immediately-appearing
    // hover stem renders without relying on an adjacent land/selection repaint
    // (see the declaration). Frame from the active column's store — the index is
    // per-column — and only damage when idx resolves there.
    int64_t frame = 0;
    bool    have  = false;
    if (app.active_markers_view == 'P') {
        const auto& pv = app.phaseresetmarkers.markers();
        if (idx >= 0 && idx < static_cast<int>(pv.size())) {
            frame = pv[idx].time_frame;
            have  = true;
        }
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (idx >= 0 && idx < static_cast<int>(mv.size())) {
            frame = mv[idx].time_frame;
            have  = true;
        }
    }
    if (have) invalidate_hover_stem_column(idx, frame);
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

// Repair the LIVE display-state fields after a total-changing map edit. The
// caller (kick_waveform_sync / the tick backstop) has already reclamped
// zoom/viewport through clamp_viewport_start, so this reads the final geometry.
// In SOURCE view the live total is the source total and never changes, so both
// clamps below are structural no-ops there — no view branch needed. The helper
// is idempotent and cheap (two compares when nothing is out of domain), which it
// must be: kick_waveform_sync runs per cent step during a tempo drag.
void Viewport::clamp_display_state_to_live_domain() {
    if (audio.total_frames() <= 0) return;

    // PLAYHEAD: keep the resting cursor playhead inside [0, live_total - 1] via
    // the shared chokepoint (clamp_playhead_to_live_domain) — the same ruling
    // every playhead write funnels through. A target-total shrink (e.g. an early
    // slow segment dragged toward 4.00) can strand a parked playhead past the new
    // EOF; this pulls it back to total - 1. On an actual move, damage the
    // playhead columns and the timestamp readout the way move_playhead_to's
    // playhead-only branch does (playhead_pixel_x reads app.playhead_cursor_sample,
    // so old_px is captured before the write). No scanner write: the repair
    // concerns the RESTING cursor only — the scanner is meaningful only while
    // playhead_scanner_active, and every scanner read gates on it (the
    // `? scanner : cursor` ternaries take the cursor this just repaired when the
    // scanner is inactive), while an active scanner is the audio thread's to own;
    // a map edit strands only the cursor.
    const int64_t clamped =
        clamp_playhead_to_live_domain(app.playhead_cursor_sample, app, audio);
    if (clamped != app.playhead_cursor_sample) {
        const double old_px = playhead_pixel_x(app, audio);
        app.playhead_cursor_sample = clamped;
        const double new_px = playhead_pixel_x(app, audio);
        invalidate_playhead_columns(old_px, new_px);
        invalidate_timestamp_area();
    }

    // REGION: a live region's endpoints are active-domain frames. If the domain
    // shrank under it and either bound left [0, live_total - 1], CLEAR the
    // highlight (writing app.region directly has precedent in active_views.cpp's
    // S/T clear). CLEAR, not clamp, by design — the domain shifted under the wash,
    // so a clamped span would misrepresent what the user selected; this is the
    // S/T-switch precedent (codex P2 fix). A mid-drag shrink-then-grow that loses
    // the region is accepted — the region is session scratch.
    if (app.region.active) {
        const int64_t total = live_total_frames(app, audio);
        if (app.region.a_frame < 0 || app.region.a_frame >= total ||
            app.region.b_frame < 0 || app.region.b_frame >= total) {
            app.region = RegionState{};
            invalidate_waveform_area();
        }
    }
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
    // (scanner); otherwise tracks the cursor. The scanner is meaningful only
    // while active, so the ternary below takes the cursor at rest. At the
    // effective ceiling samples_visible == total, so
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
    // under review); otherwise center on the cursor. The scanner is
    // meaningful only while active, so the ternary takes the cursor at rest.
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
// at launch too (right after launch_playback_from's seed sets the scanner to
// the launch position), so the same landing rule left-edge-aligns the
// viewport on the launch position if it was offscreen — the scanner always
// issues forth visible (Space's cursor launch can be offscreen; a scrub
// click is a visible column already, so the launch call no-ops there).
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
    // Erase the selected-marker stem's HOVER arm (a live WAVEFORM overlay) when a
    // hovered marker is being cleared — the top-strip damage below does not reach
    // it. Captured before the reset; a no-op when nothing was hovered / offscreen
    // (or the cleared marker was not the selected one — a harmless over-damage).
    const int     old_hover_idx   = app.hover_popup.marker_index;
    const int64_t old_hover_frame = app.hover_popup.source_frame;
    app.hover_popup = HoverPopupState{};
    if (was_visible) {
        invalidate_top_strip();
        invalidate_timestamp_area();
    }
    invalidate_hover_stem_column(old_hover_idx, old_hover_frame);
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
    // Hover runs the same way in target view as in source view. marker_hit_at
    // builds the target_warp_frame_map internally (via hit_test_flag and the
    // lane-run resolver) when active_audio_view == Target so the flag rects and
    // lane run it walks match what paint_handler renders at translated columns.
    // Mutation-sensitive short-circuit: the same hovered marker with both stores
    // unchanged needs no re-read. A store mutation (either column) bumps its
    // generation, so an in-place edit of the hovered marker — tempo step, Ctrl+N,
    // nudge — falls through here and re-reads fields, position, eligibility, and
    // payload from the live store below, even though the hit index is unchanged.
    // The displayed-map generation joins the short-circuit: marker_hit_at
    // resolves identity against the displayed flag / run positions, so a silent
    // map promotion (which advances displayed_map_gen without a store mutation)
    // can move the flag under a stationary cursor. Requiring the map generation
    // to match forces a full re-read after a promotion. The hit is now an index
    // AND a part (flag vs run — the part drives the text-hover expansion), so the
    // short-circuit compares both; the three generation keys still cover every
    // store/map input the run set and the expansion depend on.
    //
    // The hover identity adopts the UNIFIED marker hit (flag shape OR a rendered
    // lane run's rect), the same resolver the press chain reads, so moving the
    // pointer off the flag up into the marker-text lane holds the hover instead
    // of dropping it. STABILITY (no oscillation), TWO-MODE (via
    // current_marker_lane_runs):
    //   ALL-VISIBLE mode — the CAPPED run set is the whole visible set, depending
    //   only on store / viewport(displayed) / map / drag / iteration state, never
    //   on hover; the ONE hover-dependence is the TEXT-HOVER EXPANSION overlaid on
    //   it (the hovered run, if its full text exceeds the budget, expands and is
    //   hit FIRST). That expansion is a STABLE LATCH, not an oscillation: the
    //   expanded rect CONTAINS the marker's capped rect, so a pointer that hit
    //   marker-M (landing inside M's capped rect) is inside M's expanded rect,
    //   which the re-resolve tests first → returns M → fixed point (the
    //   convergence argument below).
    //   FALLBACK mode — the single capped run's rect derives from the marker the
    //   run CURRENTLY shows (hover-else-last-selected). Pointer over hovered-M's
    //   run → the resolver returns M (hover tier), a fixed point; entering over
    //   the last-selected run with NO hover shows that marker, text/frame IDENTICAL
    //   under both tiers, so the rect does not move when the tier flips — one
    //   settle, no flicker. The same expansion latch applies to M's single run.
    // BOTH the marker index AND the part (mh.on_flag) feed hover now — a
    // flag->run move on the same marker is a real transition (it turns the
    // expansion on) — so the short-circuit and the loop compare both.
    const long long warp_gen  = app.warpmarkers.generation();
    const long long phase_gen = app.phaseresetmarkers.generation();
    const long long disp_gen  = app.displayed_map_gen;

    // A store mutation or a silent map promotion invalidates the cached hover
    // run that marker_hit_at resolves against: in FALLBACK mode, while a hover
    // shows, current_marker_lane_runs' hover tier serves hover_popup.lane_text /
    // source_frame, so the first resolve below is judged against the OLD rect;
    // in ALL-VISIBLE mode the capped run set is hover-independent, but the
    // text-hover EXPANSION overlaid on it is hover-dependent (a stale hover could
    // leave the wrong run expanded). Either way the recompose must CONVERGE. On
    // the pure-motion path these three keys still match the cache, the cache is
    // accurate, and one resolve+recompose is the fixed point (unchanged cost).
    // When any key differs the cache is stale, so the recompose must converge
    // rather than latch a hover whose run shrank away from the cursor.
    const bool cache_invalidated =
        warp_gen  != app.hover_popup.warp_gen ||
        phase_gen != app.hover_popup.phase_gen ||
        disp_gen  != app.hover_popup.displayed_gen;

    const MarkerHit mh = marker_hit_at(app, audio,
                                       app.last_mouse_x, app.last_mouse_y);
    const int hit = mh.index;
    // Short-circuit on index AND PART (mh.on_flag): the lane's text-hover
    // expansion keys on hovering the RUN not the flag, so a same-index move from a
    // marker's FLAG up into its own RUN (index unchanged, part flipped) MUST fall
    // through and recompute — else the expansion would never appear. Part is
    // stored into the hover cache below and compared here.
    if (hit == app.hover_popup.marker_index &&
        mh.on_flag == app.hover_popup.on_flag && !cache_invalidated) return;

    const bool was_visible = app.hover_popup.any_visible();
    // Selected-stem hover-transition damage inputs: the OLD hovered marker (whose
    // column may lose the stem's hover arm) captured before apply_hit overwrites
    // the cache. The NEW column is damaged after settling below.
    const int     old_hover_idx   = app.hover_popup.marker_index;
    const int64_t old_hover_frame = app.hover_popup.source_frame;

    // Recompose both surfaces from a hit index INTO the live hover cache.
    // current_marker_lane_runs' fallback hover tier serves exactly this cached
    // run, so applying here is precisely what a re-resolve of marker_hit_at reads
    // back. The LANE shows the hovered marker's own value regardless of
    // eligibility — the canonical flag line for a warp marker (flag_text_iter,
    // the one composer the flag paint, hit-rects, and the Enter editor seed all
    // share, so lane and editor content always agree) or the literal "p" for a
    // phase reset marker. The BOTTOM readout keeps the pass/ref gate
    // (popup_eligible_marker): owners and phase resets have nothing to resolve.
    auto apply_hit = [&](int h, bool on_flag) {
        app.hover_popup.marker_index = h;
        app.hover_popup.on_flag      = on_flag;
        app.hover_popup.source_frame = 0;
        app.hover_popup.lane_text.clear();
        app.hover_popup.readout_text.clear();
        app.hover_popup.copy_payload.clear();
        if (h >= 0) {
            if (app.active_markers_view == 'P') {
                const auto& pv = app.phaseresetmarkers.markers();
                if (h < static_cast<int>(pv.size())) {
                    app.hover_popup.lane_text    = "p";
                    app.hover_popup.source_frame = pv[h].time_frame;
                }
            } else {
                const auto& mv = app.warpmarkers.markers();
                if (h < static_cast<int>(mv.size())) {
                    app.hover_popup.lane_text =
                        flag_text_iter(mv, h, app.iteration_mode_enabled);
                    app.hover_popup.source_frame = mv[h].time_frame;
                }
            }
            if (popup_eligible_marker(app, h)) {
                app.hover_popup.readout_text = compute_hover_popup_text(
                    slice_to_warp_markers(app.warpmarkers.markers()), h,
                    audio.sample_rate(), audio.total_frames(),
                    &app.hover_popup.copy_payload);
            }
        }
    };

    apply_hit(mh.index, mh.on_flag);

    // Converge the resolve-recompose cycle when the cache was stale. The first
    // resolve above ran against the pre-mutation run; re-resolve marker_hit_at
    // against the freshly composed run and, while the resolved identity differs
    // from what we just applied, re-apply — driving to a fixed point.
    //
    // Termination (bounded at 3 passes): the expansion is a self-stabilizing
    // latch, so applying ANY hit H settles on the NEXT re-resolve. After apply_hit
    // sets hover to H (index + part), the re-resolve tests H's EXPANDED rect FIRST
    // (when H's text exceeds the budget and its RUN, not flag, is hovered); H was
    // returned because the pointer sat in H's rect (capped or expanded), and the
    // expanded rect CONTAINS the capped one, so the pointer is in H's expanded
    // rect → the re-resolve returns H → agreement. When H does not expand (short
    // text, or a FLAG hit) the run set is hover-independent for that resolve and
    // the same-rect fixed point holds (fallback also composes the hover and
    // last-selected tiers identically). A mode flip cannot occur inside the loop —
    // the VERDICT depends on store/viewport/map/drag/iteration state, none of
    // which mutate here (only hover_popup, which the verdict ignores; only the
    // expansion reads it). No A->B->A cycle: the capped rects are pairwise
    // disjoint, the pointer sits in at most one, and an applied hover's expanded
    // rect is tested with precedence — so a hit the loop already visited cannot
    // re-appear with a different follow-on. If the hard bound is somehow hit
    // without agreement, clear the hover — never latch a stale one.
    if (cache_invalidated) {
        constexpr int kMaxHoverConvergePasses = 3;
        int passes = 1;  // apply_hit above counts as the first pass
        for (;;) {
            const MarkerHit re = marker_hit_at(app, audio,
                                               app.last_mouse_x,
                                               app.last_mouse_y);
            if (re.index == app.hover_popup.marker_index &&
                re.on_flag == app.hover_popup.on_flag) break;  // settled (index + part)
            if (passes >= kMaxHoverConvergePasses) {
                apply_hit(-1, false);  // bound reached without agreement — drop
                break;
            }
            apply_hit(re.index, re.on_flag);
            ++passes;
        }
    }

    // Stamp the generations that produced this FINAL settled set (both columns,
    // even when marker_index < 0), so the short-circuit and the on_tick refresh
    // settle until the next real store change. The generations are loop-invariant
    // (no store mutates inside this function), so stamping once after convergence
    // matches every intermediate pass.
    app.hover_popup.warp_gen  = warp_gen;
    app.hover_popup.phase_gen = phase_gen;
    app.hover_popup.displayed_gen = disp_gen;

    // The lane renders in the top strip, the readout in the bottom strip; damage
    // both when either surface was showing or will show.
    if (was_visible || app.hover_popup.any_visible()) {
        invalidate_top_strip();
        invalidate_timestamp_area();
    }
    // The selected-marker stem's HOVER arm lives in the WAVEFORM, which the
    // top-strip damage above does not cover. Damage the OLD hovered marker's stem
    // column (it may lose the stem) and the NEW one's (it may gain it) — no-ops
    // when either is absent or offscreen, and a harmless over-damage when the
    // marker is not the selected one. A pure motion within one marker's rect
    // (old == new, both frames equal) redundantly damages the same column once.
    invalidate_hover_stem_column(old_hover_idx, old_hover_frame);
    invalidate_hover_stem_column(app.hover_popup.marker_index,
                                 app.hover_popup.source_frame);
}
