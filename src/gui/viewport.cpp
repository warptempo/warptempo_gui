#include "viewport.h"

#include "audio.h"
#include "input_handler.h"   // clear_region_highlight (the movement owner's hide)
#include "notifications.h"   // notification_stack_bound (the stack's damage rect)
#include "playback.h"
#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "platform.h"

#include <algorithm>
#include <cmath>

// THE NAVIGATION RANGE (contract at the declaration): Home/End's jump bounds and
// the load-time playhead, and since 2026-08-05 no playback consumer at all.
//
// ITS BODY IS A FREE FUNCTION since 2026-08-15 and the member DELEGATES, so
// there is still exactly ONE arithmetic: playhead_skip_landing_frame (below,
// declared in app_state.h) must answer the same bounds the two Home/End arms
// jump to, and it is a free function itself for the same reason. The member
// keeps its name and its callers; nothing about the range moved.
namespace {
std::pair<int64_t, int64_t> navigation_trim_range(const AppState& app,
                                                  const GuiAudio& audio) {
    if (audio.total_frames() <= 0) return {0, 0};
    if (app.active_audio_view == 'T') {
        // Target view: trim is authored source-domain (both bounds store
        // whole int64 source frames, every writer crossing into that domain at
        // its own site — the inventory is at the head of input_trim.cpp) but
        // Home/End needs to land
        // the playhead in the active target-frame domain. Build
        // the live warp_frame_map and forward-translate the source-domain
        // trim boundaries.
        //
        // THE FULL-WINDOW NORMALIZATION (architect 2026-07-30), the target-view
        // half of the rule compute_trim_samples records in full: a full source
        // pair [0, total-1] is the old unset state, so it returns the whole live
        // domain {0, live_total} — mapping total-1 through the map instead would
        // land an EXCLUSIVE end one target frame short and cost the last frame
        // of the deformed timeline. The recognition is the shared owner
        // trim_window_is_full (settings_file.h), asked on the SOURCE pair
        // against the SOURCE total, exactly as the render orchestrators ask it.
        const int64_t live_total =
            live_total_frames(app, audio);
        if (trim_window_is_full(app.trim.begin_frame, app.trim.end_frame,
                                audio.total_frames())) {
            return {0, live_total};
        }
        int64_t begin_tgt =
            source_frame_to_active_domain(app, audio, app.trim.begin_frame);
        int64_t end_tgt =
            source_frame_to_active_domain(app, audio, app.trim.end_frame);
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
}  // namespace

std::pair<int64_t, int64_t> Viewport::trim_range() const {
    return navigation_trim_range(app, audio);
}

int64_t Viewport::trim_begin_sample() const { return trim_range().first; }
int64_t Viewport::trim_end_sample()   const { return trim_range().second; }

// WHERE A Home / End JUMP WOULD LAND THE CURSOR — the contract, the two arms and
// the clamp's purpose are all at the declaration (app_state.h). TWO READERS:
// the shared jump body run_playhead_end_jump (input_key_dispatch.cpp) plus the
// history view's own pair, so the live bare Home / End, their ctrl forms and
// the mode's absolute jumps spell one bound once instead of once per route
// (the bottom row's two SKIP buttons dispatch bare Home / End like any other
// chrome button and reach it that way); and, since 2026-08-30, THOSE SKIPS'
// FACE (redesign_button_enabled), which greys where the resting cursor
// already sits on this answer. That face reader lived one revision of
// 2026-08-15 and was taken back the same day — a Home / End press is not a
// pure jump (each also stops a live audition, clears the selection and hides
// the trim region overlay, no-op jump included), so the grey promised less
// than the key delivers — and the truthful-buttons ruling, naming the skips
// outright, put it back; the record is at the skips' case in
// redesign_button_enabled (app_state.h).
int64_t playhead_skip_landing_frame(const AppState& app, const GuiAudio& audio,
                                    bool forward, bool whole_piece) {
    if (whole_piece || app.history_mode.active) {
        // THE WHOLE-PIECE ARM, ONE ARM WITH TWO ENTRANTS. The `h` history view
        // takes it for every jump (architect 2026-08-05: the view reviews the
        // WHOLE piece, so an End stopping at a trim bound would hide the flags
        // past it), and the CTRL forms take it anywhere (architect 2026-08-24:
        // "ctrl+home/end should force 0/eof playhead move even if trim does not
        // include the frame"). The ends are the ACTIVE
        // DOMAIN's own: live_total_frames is what the displayed timeline runs to
        // in either audio view. With a full trim window the two arms coincide.
        return clamp_playhead_to_live_domain(
            forward ? live_total_frames(app, audio) - 1 : 0, app, audio);
    }
    const std::pair<int64_t, int64_t> range =
        navigation_trim_range(app, audio);
    return clamp_playhead_to_live_domain(
        forward ? range.second - 1 : range.first, app, audio);
}

// Viewport changes repaint the waveform area and the top strip together:
// flag positions depend on the viewport, so any pan/zoom has to refresh
// flags as well as waveform. Playhead-only moves keep using the narrow
// column invalidation below.
//
// THE OVERVIEW LANE IS INSIDE THIS ONE RECT since the relayout's commit B
// (2026-08-12): the lane moved from the bottom strip into the CENTERED BLOCK
// (top lane 3), and this damage spans the window top through the waveform's
// bottom, so the lane's viewport BOX — which mirrors the viewport/zoom every
// caller here just moved — and its playhead TICK — which mirrors the cursor
// every discrete playhead write behind this shape just landed — are both
// covered by construction. THE DEDICATED RIDER IS DELETED WITH THE MOVE: it was
// a SECOND rect that skipped the unified bottom row between the waveform and
// the lane (to keep pan/zoom frames from paying that row's HarfBuzz label
// shaping), and both the rect and the skip are producer-less now — nothing sits
// between them. Its two inline copies at waveform_cache.cpp's publish sites
// went the same way, each site's note pointing here. The per-frame SCANNER
// sites still do NOT come through here: their overview tick damage is its own
// narrow column pair (the cadence rule at playhead_pixel_x, app_state.h).
void Viewport::invalidate_waveform_area() {
    const GuiRect a = waveform_area(app);
    const int y0 = 0;
    const int y1 = a.y + a.h;
    gui.invalidate_region(0, y0, app.width, y1 - y0);
}

// THE STATE CELL'S DAMAGE — the BOTTOM ROW'S lane whole (the cell is the
// clock's neighbour since 2026-08-29's fold). The caller inventory and the
// reasoning for taking the lane rather than a span of it are at the
// declaration, viewport.h.
void Viewport::invalidate_status_cell_area() {
    const GuiRect t = bottom_row_area(app);
    gui.invalidate_region(t.x, t.y, t.w, t.h);
}

// THE MODAL'S DAMAGE — the unified bottom row's lane whole, which IS the
// modal's surface since it moved onto the row (2026-08-13). No stash rider and
// no cell arithmetic: the row yields whole while a dialog stands, so the lane
// is both the smallest rect that covers the modal and the rect a CLOSER owes
// (it must erase the modal AND bring the row's own tenants back). The OPENERS
// do not come through here — nothing is painted before a surface's first paint,
// so they invalidate the whole window. Caller inventory at the declaration.
void Viewport::invalidate_modal_dialog_area() {
    const GuiRect t = bottom_row_area(app);
    gui.invalidate_region(t.x, t.y, t.w, t.h);
}

// THE NOTIFICATION STACK'S DAMAGE — its bound whole; the callers and the
// reasoning are at the declaration, viewport.h.
void Viewport::invalidate_notification_stack() {
    const GuiRect r = notification_stack_bound(app);
    gui.invalidate_region(r.x, r.y, r.w, r.h);
}

void Viewport::invalidate_clock_area() {
    const GuiRect c = clock_invalidate_rect(app);
    gui.invalidate_region(c.x, c.y, c.w, c.h);
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

// move_playhead_to: THE MOVEMENT OWNER — the reseat below, with the trim region
// overlay's HIDE and the A/B audition's END in front of it. Reaching this
// function means the playhead's POSITION IN THE MUSIC is changing, and that is
// the whole hide rule; the rule, its second owner and its exemptions are stated
// once at clear_region_highlight (input_handler.h). UNCONDITIONAL, never gated
// on whether the write moved anything: a Home pressed on the frame the cursor
// already holds still hides, which is what the bottom row's ungreyed skip
// buttons promise (architect 2026-08-15, the record at their case in
// redesign_button_enabled).
void Viewport::move_playhead_to(int64_t new_sample) {
    clear_region_highlight(app, *this);
    // A PLAYHEAD MOVEMENT ENDS THE A/B AUDITION, and it is the hide rule's own
    // membership: what the act promises is that the pair of plays it makes on
    // each tab is identical, which is exactly a resting cursor that cannot move
    // under it. So the two rules share these movement owners, and a TRANSLATION
    // (reseat_playhead_to, below) or a RESTORE (the tab switch's band swap,
    // which writes the cursor direct) ends neither. This is a class statement:
    // the complete clearing-owner inventory is at GuiAuditionSequence
    // (app_state.h) and is not to be restated here.
    clear_audition_sequence(app);
    reseat_playhead_to(new_sample);
}

// reseat_playhead_to: update playhead, keep viewport so playhead stays
// visible. Invalidate only what changed. Clamps to the full audio
// range; trim is purely cosmetic so the playhead is free to sit
// outside the trim window.
//
// THE WRITE ALONE, WITH NO HIDE IN IT, and the callers who want it that way are
// the ones whose write is NOT a movement (2026-08-19). RE-DERIVED BY GREP —
// seven, in two families:
//   * THE MAP-CHANGE RE-LANDS, all in a target-view re-warp tail: both arms of
//     the Up/Down tempo cent step (warpmarkers_ops.cpp) and, since 2026-08-25,
//     the WARP STATUS/VALUE FAMILY admitted in W+target with them — Ctrl+D,
//     Ctrl+N and Delete (warpmarkers_ops.cpp) and the flag editor's payload
//     commit (flag_editor.cpp), whose shared contract is stated at the head of
//     warpmarkers_ops.cpp; and, since 2026-08-28, THE UNDO/REDO RESTORE
//     (undo.cpp), whose settings-and-marker swap rebuilds the map under a
//     STANDING view, before the restore flips the audio view onto the finished
//     one. In five of the seven the focus does not change and the playhead does
//     not leave it — the marker's IMAGE moved out from under the cursor and the
//     cursor follows it into the new domain. That is the `t` flip's translation
//     in another spelling, and a translation is not a movement. THE DELETE AND
//     THE RESTORE ARE THE OTHER TWO AND THEIR SUBJECT DIFFERS: either can leave
//     no focus at all (the delete clears the selection, and a restore whose
//     touched set comes up empty clears it too), so what they follow into the
//     new domain is the playhead's own musical instant, inverted to a source
//     frame before the write.
//   * THE SWEEP'S OWN PER-MOTION CARRY is NOT here and never was: it writes
//     app.playhead_cursor_sample direct, because a keep-visible edge-align would
//     scroll the viewport out from under a live gesture (input_pointer.cpp). The
//     whole trim family is exempt the same way — park_playhead_at_trim_start
//     writes direct too — so the trim's surfaces take no suppression, they
//     simply do not pass through here.
// EVERY OTHER CALLER GOES THROUGH move_playhead_to and inherits the hide.
//
// The [0, total - 1] live-domain clamp is the shared ruling spelled at
// clamp_playhead_to_live_domain (app_state.h) — this gesture route funnels
// through it exactly like every non-gesture sync route, so they can never
// disagree about the same endpoint.
void Viewport::reseat_playhead_to(int64_t new_sample) {
    if (audio.total_frames() <= 0) return;
    new_sample = clamp_playhead_to_live_domain(new_sample, app, audio);

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
        // NO-SCROLL BRANCH: only the cursor moved. FULL WAVEFORM-AREA DAMAGE
        // (architect 2026-07-30, replacing the narrow old/new column pair
        // computed on the LIVE viewport) — the cursor's pixels are
        // PLATE-registered, and Viewport sees no GuiPaintHandler, so the site
        // takes the widening shape: an async publish still in flight from an
        // earlier follow-scroll, resize or load leaves live and plate on
        // different spans, and narrow live columns then erase pixels the cursor
        // was never drawn at. The cost is bounded — the fastest caller is the
        // compositor-throttled arrow step, and the moved branch above already
        // pays a full synchronous plate RENDER at that same cadence. Rule and
        // per-site shape table at playhead_pixel_x (app_state.h).
        invalidate_waveform_area();
    }
    invalidate_clock_area();
    if (playback.is_playing()) playback.resync_predictor();
}

// Repair the LIVE display-state fields after a total-changing map edit. The
// caller (kick_waveform_sync / the tick backstop) has already reclamped
// zoom/viewport through clamp_viewport_start, so this reads the final geometry.
// In SOURCE view the live total is the source total and never changes, so both
// clamps below are structural no-ops there — no view branch needed. The helper
// is idempotent and cheap (two compares when nothing is out of domain), which is
// what lets a held Up/Down cent step pay nothing per press.
void Viewport::clamp_display_state_to_live_domain() {
    if (audio.total_frames() <= 0) return;

    // PLAYHEAD: keep the resting cursor playhead inside [0, live_total - 1] via
    // the shared chokepoint (clamp_playhead_to_live_domain) — the same ruling
    // every playhead write funnels through. A target-total shrink (e.g. an early
    // slow segment dragged toward 4.00) can strand a parked playhead past the new
    // EOF; this pulls it back to total - 1. On an actual move, damage the
    // waveform area and the timestamp readout the way move_playhead_to's
    // playhead-only branch does — full-area for the same reason stated there
    // (the cursor's pixels are plate-registered and Viewport cannot reach the
    // plate basis; rule at playhead_pixel_x, app_state.h), and rarer still: this
    // fires only when a map edit actually stranded the cursor out of domain.
    // No scanner write: the repair
    // concerns the RESTING cursor only — the scanner is meaningful only while
    // playhead_scanner_active, and every scanner read gates on it (the
    // `? scanner : cursor` ternaries take the cursor this just repaired when the
    // scanner is inactive), while an active scanner is the audio thread's to own;
    // a map edit strands only the cursor.
    const int64_t clamped =
        clamp_playhead_to_live_domain(app.playhead_cursor_sample, app, audio);
    if (clamped != app.playhead_cursor_sample) {
        app.playhead_cursor_sample = clamped;
        invalidate_waveform_area();
        invalidate_clock_area();
    }

    // (THE REGION'S OWN RECLAMP STOOD HERE until 2026-08-18 and is DELETED with
    // the state it validated: a region held two ACTIVE-domain endpoints of its
    // own, which a shrinking domain could strand outside [0, live_total - 1],
    // and the answer was to clear the highlight. The region IS the trim now —
    // the overlay is DERIVED from the trim bounds every frame, through
    // trim_overlay_span, which crosses them into the live domain and clamps
    // there — so there is no stored endpoint left to validate and nothing this
    // pass could correct. The TRIM's own bounds are SOURCE frames, walled at
    // load and at every gesture, and are not this function's subject.)
}

// WHERE ONE PIXEL STEP WOULD LAND — the contract is at the declaration
// (app_state.h); the arithmetic is move_playhead_pixels' own, which reads this
// for its landing since 2026-08-30 (planner decision 60), the Left / Right
// buttons' face being the second reader. The two degenerate cases the act
// used to return on — no audio, no painted grid — answer the resting cursor
// itself, so a step there is the no-op it always was and the face greys.
int64_t playhead_pixel_step_landing(const AppState& app, const GuiAudio& audio,
                                    int delta_px) {
    if (audio.total_frames() <= 0) return app.playhead_cursor_sample;
    const GuiRect area = waveform_area(app);
    const double q = painter_samples_per_pixel(app, audio, area);
    if (q <= 0.0) return app.playhead_cursor_sample;
    const int64_t cur_col = static_cast<int64_t>(std::nearbyint(
        static_cast<double>(app.playhead_cursor_sample - app.viewport_start_sample)
        / q));
    const int64_t target_col = cur_col + static_cast<int64_t>(delta_px);
    // Pre-clamped into the live domain, the clamp move_playhead_to would apply
    // anyway — a landing must be a frame the cursor can occupy, which is what
    // makes the wall compare exact.
    return clamp_playhead_to_live_domain(
        static_cast<int64_t>(std::llrint(displayed_grid_position_at_column(
            app.viewport_start_sample, target_col, q))),
        app, audio);
}

void Viewport::move_playhead_pixels(int delta_px) {
    if (audio.total_frames() <= 0) return;
    // Resolve the playhead's CURRENT painted column —
    // nearbyint((cursor - viewport_start)/q), the painters' own placement — and
    // land on the ADJACENT grid column's frame through the one column->frame
    // owner. So one keypress moves exactly one painted pixel even when q is
    // fractional (a sub-pixel sample step could leave the displayed column
    // unchanged), AND the landed sample is viewport-phase-independent: it is the
    // same lattice the click placement and the marker commits land on, so a step
    // means the same sample whatever pan or zoom preceded it (finer adjustment
    // is a deeper zoom's job). The spp is the PAINTER-quantized q rather than
    // the logical one for the same reason the click placement takes it: it is
    // the grid actually drawn (under the multiple-of-16 width contract the two
    // agree, but the painted grid is the principled input).
    // The recovery nearbyint is the column direction and is this walk's own; the
    // landing is the shared owner's. move_playhead_to still owns the walls, and
    // a playhead parked off-lattice re-snaps onto it at its first step. THE
    // WHOLE ARITHMETIC LIVES AT playhead_pixel_step_landing since 2026-08-30
    // (planner decision 60), the Left / Right buttons' face reading the same
    // landing; a step at a wall still reaches move_playhead_to, whose
    // unconditional overlay hide is the KEY's to keep (the greyed button
    // forgoes it, the skips' own shape).
    move_playhead_to(playhead_pixel_step_landing(app, audio, delta_px));
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
    // Harmless over-damage: a zoom moves the viewport, never the playhead or
    // the scanner, so the clock's value cannot change here — the true-reason
    // record for all three zoom appliers is at invalidate_clock_area's
    // inventory (viewport.h).
    invalidate_clock_area();
    // Flags live in the top strip — rect positions change when the viewport
    // scale changes (the selected marker's lane text rides its flag there;
    // row 8's state cell says nothing a zoom can move).
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
    if (playback.is_playing()) playback.resync_predictor();
    // Reaching here means the zoom level changed (early-return above guards
    // the no-op case), so the waveform fingerprint differs. Zoom is a one-shot
    // discrete jump: render the plate synchronously and publish the displayed
    // fingerprint now so the top-strip flags and the playhead column do not
    // jump a frame ahead of the waveform. The pyramid bounds per-column cost at
    // every level — unconditionally, in both views (the bound and its proof
    // live at GuiAudio::level_for_span) — so a full render is O(area_width) at
    // any zoom; coalesced
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

    // THE STRIP DRAG BYPASSES scroll_viewport (it writes the viewport itself,
    // above), so it carries the same follow suppression here: every user pan
    // suppresses follow for the session (architect 2026-07-30; the funnel copy
    // is in scroll_viewport, the producer inventory at the flag's declaration in
    // app_state.h). Gated on playback being live exactly as the funnel is, and
    // on EITHER STRIP AXIS having moved — not on the viewport alone. The ZOOM
    // axis is a first-class producer here: this zoom is SONG-ANCHORED (the
    // grabbed sample stays pinned at its column), so it carries the view off the
    // scanner just as the pan axis does — unlike the keyboard zoom, which
    // centers ON the scanner during playback and therefore suppresses nothing.
    // The level test is not redundant with the viewport test: a level change can
    // leave viewport_start_sample bit-identical (the anchor pinned at column 0,
    // or the recompute rounding/clamping back onto the same grid point), and
    // while that frame's zoom stands, the next pre-paint's follow_scroll_if_needed
    // pages away from the level the user just dialled in.
    // `level_changed` reports a real move, not a request: ALL THREE callers —
    // the nav drag's zoom phase (apply_nav_zoom_at,
    // which joined 2026-08-14 with the live-ctrl model; the deleted strip
    // drag's own body was the fourth until 2026-08-15), the two-finger
    // touch-nav body
    // (apply_touch_nav_update, which joined 2026-08-11 driving this same
    // chokepoint per touch frame) and the overview lane's edge drags
    // (apply_overview_drag_at's edge arm, since the lane rework 2026-08-12) —
    // pre-clamp new_level into the same
    // [kMinZoom, effective_max_zoom_level] window clamp_viewport_start re-applies
    // below, so the pre-assignment compare cannot read a wall-saturated no-op as
    // movement. A both-unchanged frame suppresses nothing either way — mid-gesture
    // the true-no-op early return above takes it, and the terminating event falls
    // through this gate false.
    if ((level_changed || vp_changed) && playback.is_playing())
        app.follow_overridden_for_session = true;

    invalidate_waveform_area();
    // Harmless over-damage, like apply_zoom_change's (the record is at
    // invalidate_clock_area's inventory, viewport.h).
    invalidate_clock_area();
    // Flags live in the top strip — rect positions change when the viewport
    // scale or start changes.
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);

    // Rest state (final) re-anchors the playback predictor once, like the
    // continuous pan's release; mid-gesture events do NOT resync (the predictor
    // keeps extrapolating smoothly for the drag's duration). Repaint dispatch:
    // EVERY frame of the drag takes one SYNCHRONOUS full rebuild — the exact
    // cost a keyboard zoom pays per press, capped at once per pointer frame by
    // the platform's motion coalescing. Level-changed and pan-only frames are
    // no longer distinguished: the incremental shift-and-strip fast-path the
    // pan-only frames used was retired 2026-07-26, so the drag renders the same
    // way whichever axis moved.
    if (final && playback.is_playing()) playback.resync_predictor();
    kick_waveform_sync();
}

void Viewport::apply_zoom_to_start(double new_zoom_level, int64_t new_start) {
    if (audio.total_frames() <= 0) return;

    // Pre-clamp the requested level to the per-file window. clamp_viewport_start
    // re-applies the identical clamp as the chokepoint; this only sharpens the
    // no-op detection below.
    new_zoom_level = clamp_zoom_level(app, audio, new_zoom_level);

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

    invalidate_waveform_area();
    // Harmless over-damage, like apply_zoom_change's (the record is at
    // invalidate_clock_area's inventory, viewport.h).
    invalidate_clock_area();
    // Flags live in the top strip — rect positions change with the viewport.
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
    if (playback.is_playing()) playback.resync_predictor();
    // A discrete one-shot viewport jump: render the plate synchronously and
    // publish the displayed fingerprint now so the top-strip flags and the
    // playhead column do not jump a frame ahead of the waveform.
    kick_waveform_sync();
}

// ZOOM IN ALWAYS ACTS, which is why its button has no ladder-end face
// (2026-08-30, planner decision 53): at the deepest level the press recentres
// on the playhead instead of stepping, the second arm below.
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
    // THE LEADING RETURN IS ONE OWNER (zoom_out_step_actionable, app_state.h —
    // 2026-08-30, when the ZOOM OUT button's face began reading the same
    // answer): already at the effective ceiling, the step is a consumed no-op.
    if (!zoom_out_step_actionable(app, audio)) return;
    // One whole level shallower, saturating at the effective per-file ceiling
    // (there is nothing beyond it — full zoom-out is whole-song-visible);
    // clamp_zoom_level is the bounds' one owner, and past the return above it
    // hands back a level strictly above the current one.
    apply_zoom_change(clamp_zoom_level(app, audio, app.zoom_level + 1.0));
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
    // effective per-file ceiling. The at-the-ceiling refusal reads its one
    // owner (zoom_out_step_actionable, app_state.h — the same compare
    // zoom_out() and the ZOOM OUT button's face make since 2026-08-30).
    if (!zoom_out_step_actionable(app, audio)) return;
    double target = app.zoom_level - static_cast<double>(in_steps);
    if (target > max_l) target = max_l;
    apply_zoom_change(target);
}

void Viewport::scroll_viewport(int64_t delta_samples, bool continuous) {
    if (audio.total_frames() <= 0) return;
    const int64_t old_vp = app.viewport_start_sample;
    app.viewport_start_sample += delta_samples;
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) {
        // EVERY PAN SUPPRESSES FOLLOW FOR THE SESSION (architect 2026-07-30).
        // This is the pan funnel — PageUp/PageDown, the alt+wheel stepped pan,
        // touchpad scroll
        // and the plain-drag grab-pan all land here (the DRAG plain since
        // 2026-08-12, pan-primary; the WHEEL back on alt since 2026-08-27, the
        // plain form being the waveform magnification) — so one line covers the whole
        // class by construction. Inside the CHANGED guard, because a pan that
        // moved nothing (wall-saturated) suppresses nothing, and gated on
        // playback being live, matching the placement body's own `was_playing`
        // gate: a pan while stopped must not pre-suppress the next session. The
        // producer inventory lives at the flag's declaration (app_state.h); the
        // flag is cleared at every stop edge and by an explicit `f` re-enable,
        // which is what scopes the suppression to the session it was made in.
        if (playback.is_playing()) app.follow_overridden_for_session = true;
        invalidate_waveform_area();
        // Flag positions move with the viewport, so the top strip must
        // repaint too — the flags carry their own text now, so this one
        // invalidation covers every marker pixel the move affects.
        const GuiRect ts = top_strip_area(app);
        gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
        // A discrete pan (the alt+wheel, PageUp/PageDown) re-anchors here -
        // a single snap is invisible. A continuous drag pan passes
        // continuous=true and does NOT resync per motion event: the
        // predictor keeps extrapolating smoothly for the gesture's
        // duration and is re-anchored once when the drag ends.
        if (!continuous && playback.is_playing()) playback.resync_predictor();
        // Viewport actually moved (inside the changed guard). Render the plate
        // synchronously, the same route zoom and every other user-driven
        // viewport change takes: the incremental shift-and-strip fast-path this
        // used to drive was retired 2026-07-26 so a scrolling plate and a
        // resting one come off one code path. Every scroll class lands here —
        // touchpad, the alt+wheel, PageUp/PageDown, the plain-drag grab-pan — and the
        // synchronous render also gives them all the grab-pan's old guarantee:
        // no frame paints overlays against a plate from an older basis.
        kick_waveform_sync();
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
    app.viewport_start_sample = target - visible / 2;
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) {
        invalidate_waveform_area();
        const GuiRect ts = top_strip_area(app);
        gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
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

void Viewport::invalidate_rect(const GuiRect& r) {
    if (r.w <= 0 || r.h <= 0) return;
    gui.invalidate_region(r.x, r.y, r.w, r.h);
}

void Viewport::invalidate_all() {
    gui.invalidate_region(0, 0, app.width, app.height);
}

// Auto-follow during playback: when the scanner leaves the viewport,
// scroll so the scanner lands ~10% into the new view, leaving room
// ahead. Only the first move beyond vp_end triggers a scroll. Called
// at launch too (right after the one launch body's seed — launch_playback_window
// — sets the scanner to the launch position), so the same landing rule
// left-edge-aligns the viewport on the launch position if it was offscreen —
// the scanner always issues forth visible (Space's cursor launch and the A/B
// audition's play, which launches from the same resting cursor, can both be
// offscreen; a scrub click is a visible column already, so the launch call
// no-ops there).
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
