#include "viewport.h"

#include "audio.h"
#include "notifications.h"   // notification_stack_bound (the stack's damage rect)
#include "playback.h"
#include "render.h"
#include "text_editor.h"
#include "warp_frame_map_view.h"
#include "platform.h"

#include <algorithm>
#include <cmath>

// THE NAVIGATION RANGE (contract at the declaration): Home/End's jump bounds and
// the load-time playhead; no GUI playback consumer since 2026-08-05, and ONE
// playback consumer since 2026-09-17 — the car's loop window, which is this
// range by ruling (the declaration).
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
// the clamp's purpose are all at the declaration (app_state.h). THREE READERS:
// the shared jump body run_playhead_end_jump (input_key_dispatch.cpp) plus the
// history view's own pair, so the live Home / End and the mode's absolute
// jumps spell one bound once instead of once per route (the bottom row's two
// SKIP buttons dispatch bare Home / End like any other chrome button and
// reach it that way); and playhead_end_jump_actionable just below, the jump
// acts' "would this jump change anything" owner, through which the SKIPS'
// FACE and the acts' own no-op refusals read this landing (the skips' case in
// redesign_button_enabled, app_state.h, states the face).
int64_t playhead_skip_landing_frame(const AppState& app, const GuiAudio& audio,
                                    bool forward) {
    if (app.history_mode.active) {
        // THE PIECE'S-ENDS ARM. The `h` history view takes it for every jump
        // (architect 2026-08-05: the view reviews the WHOLE piece, so an End
        // stopping at a trim bound would hide the flags past it). The ends
        // are the ACTIVE DOMAIN's own: live_total_frames is what the
        // displayed timeline runs to in either audio view. With a full trim
        // window the two arms coincide.
        return clamp_playhead_to_live_domain(
            forward ? live_total_frames(app, audio) - 1 : 0, app, audio);
    }
    const std::pair<int64_t, int64_t> range =
        navigation_trim_range(app, audio);
    return clamp_playhead_to_live_domain(
        forward ? range.second - 1 : range.first, app, audio);
}

// WOULD THIS JUMP CHANGE ANYTHING — the contract and the reader
// inventory are at the declaration (app_state.h). Each term is an act write
// read from that write's own owner: the stop's (transport_session_live), the
// selection clear's (the live arms' selection-or-focus pair, or the mode
// arm's diff-flag pair through history_mode_revert_subject_standing — the
// same two fields clear_history_mode_focus tests), and the landing compare
// against the resting cursor. (A shown trim overlay was a fourth term until
// 2026-09-22, when the movement owner stopped hiding it: the overlay stands
// only while a sweep draws it.)
bool playhead_end_jump_actionable(const AppState& app, const GuiAudio& audio,
                                  bool forward) {
    if (transport_session_live(app)) return true;
    const bool clear_would_act =
        app.history_mode.active
            ? history_mode_revert_subject_standing(app.history_mode)
            : (marker_selection_standing(app) || marker_focus_standing(app));
    if (clear_would_act) return true;
    return playhead_skip_landing_frame(app, audio, forward) !=
           app.playhead_cursor_sample;
}

// Viewport changes repaint the waveform area and the top strip together:
// flag positions depend on the viewport, so any pan/zoom has to refresh
// flags as well as waveform. Playhead-only moves keep using the narrow
// column invalidation below.
//
// ONE RECT, from the window top through the waveform's bottom: every lane
// whose content mirrors the viewport, the zoom or the cursor is inside it by
// construction, so no caller here owes a second rect.
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

// move_playhead_to: THE MOVEMENT OWNER — the A/B audition's END in front of the
// reseat below. A PLAYHEAD MOVEMENT ENDS THE A/B AUDITION: what the act
// promises is that the pair of plays it makes on each tab is identical, which
// is exactly a resting cursor that cannot move under it. Reaching this function
// means the playhead's POSITION IN THE MUSIC is changing. THE RULE'S SECOND
// OWNER is the land (land_playhead_on_marker / land_playhead_on_source_frame,
// input_pointer.cpp), and it has THREE EXEMPTIONS, none of which reaches an
// owner: a TRANSLATION (reseat_playhead_to below, translate_playhead_to — the
// undo/redo restore's re-land, which scrolls nothing — and
// reseat_playhead_on_marker — the S/T flip, the map-change re-lands, the
// coincidence auto-select's no-op), a RESTORE (the tab switch's band swap,
// which writes the cursor direct — the act's own two tab switches are
// restores) and THE CAMERA, which writes no playhead at all. The trim family
// writes the cursor direct too (park_playhead_at_trim_start, the sweep's
// carry), and a trim write stops playback through the one stop body, which
// ends the act anyway. UNCONDITIONAL, never gated on whether the write moved
// anything. (From 2026-08-19 to 2026-09-22 these owners also hid the trim
// region overlay; that hide was deleted with the overlay's resting form.) This
// is a class statement: the complete clearing-owner inventory is at
// GuiAuditionSequence (app_state.h) and is not to be restated here.
//
// A PLAYHEAD MOVEMENT ALSO PUTS OUT THE HOLD POSTURE (architect 2026-09-23):
// the column an explicit centring asked to keep belonged to the subject where
// it stood, so a movement is one of the posture's three movement-owner clears,
// the two lands being the others. The two exemptions are the nudge, which
// keeps the bit across its whole act, and the undo/redo restore, which keeps
// it across its land; the rule is at AppState::camera_hold.
void Viewport::move_playhead_to(int64_t new_sample) {
    clear_audition_sequence(app);
    app.camera_hold = false;
    reseat_playhead_to(new_sample);
}

// reseat_playhead_to: update playhead, keep viewport so playhead stays
// visible. Invalidate only what changed. Clamps to the full audio
// range; trim is purely cosmetic so the playhead is free to sit
// outside the trim window.
//
// THE WRITE ALONE, WITH NO AUDITION END IN IT, and the callers who want it
// that way are the ones whose write is NOT a movement (2026-08-19). RE-DERIVED BY GREP
// 2026-09-22 — EIGHT, in two families:
//   * THE MAP-CHANGE RE-LANDS, all in a target-view re-warp tail: both arms of
//     the Up/Down tempo cent step (warpmarkers_ops.cpp) and, since 2026-08-25,
//     the WARP STATUS/VALUE FAMILY admitted in W+target with them — Ctrl+D,
//     Ctrl+N and Delete (warpmarkers_ops.cpp) and the flag editor's payload
//     commit (flag_editor.cpp), whose shared contract is stated at the head of
//     warpmarkers_ops.cpp; and since 2026-09-02 (the four-tier review's R-17d) THE TWO OTHER
//     WHOLE-MAP REWRITES, which had kept the playhead's NUMBER where the family
//     keeps its INSTANT — the SETTINGS ENGINE COMMIT (settings_editor.cpp; the
//     engine scale is a warp-map input) and the `h` view's WARP REVERT
//     (input_key_dispatch.cpp; its phase arm is carved out, phase resets being
//     no map input). In five of the eight the focus does not change and the
//     playhead does not leave it — the marker's IMAGE moved out from under the
//     cursor and the cursor follows it into the new domain. That is the `t`
//     flip's translation in another spelling, and a translation is not a
//     movement. THE OTHER THREE ARE THE WHOLE-STORE ACTS AND THEIR SUBJECT
//     DIFFERS: the delete, the engine commit and the revert clear the
//     selection outright and so leave no focus at all, and what they follow
//     into the new domain is the playhead's own musical instant, inverted to a
//     source frame before the write.
//   * THE UNDO/REDO RESTORE'S RE-LAND WAS THE NINTH until 2026-09-22 and
//     left for translate_playhead_to below: the keep-visible edge-align here
//     scrolled the camera to an offscreen playhead's old instant, and the
//     restore's camera answers to the markers it restores, never to the
//     playhead (architect 2026-09-22).
//   * THE SWEEP'S OWN PER-MOTION CARRY is NOT here and never was: it writes
//     app.playhead_cursor_sample direct, because a keep-visible edge-align would
//     scroll the viewport out from under a live gesture (input_pointer.cpp). The
//     whole trim family is exempt the same way — park_playhead_at_trim_start
//     writes direct too — so the trim's surfaces take no suppression, they
//     simply do not pass through here.
// EVERY OTHER CALLER GOES THROUGH move_playhead_to and inherits the audition
// end.
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
    // THE KEEP-VISIBLE EDGE-ALIGN SUSPENDS NO FOLLOW (architect 2026-09-23):
    // a camera move onto the playhead is a move onto follow's own subject,
    // not the user looking elsewhere, so the chokepoint's suspension inside
    // the clamp is undone for that one bit (AppState::follow_suspended). HOLD
    // is the movement owners' to clear, and a reseat is not one of them.
    const bool suspended_before = app.follow_suspended;
    clamp_viewport_start(app, audio);
    app.follow_suspended = suspended_before;
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
        // earlier resize or load leaves live and plate on
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

// translate_playhead_to: THE CURSOR FOLLOWS ITS OWN MUSICAL INSTANT INTO A
// REBUILT DOMAIN AND THE CAMERA DOES NOT FOLLOW IT (architect 2026-09-22). The
// reseat above is the same write plus a keep-visible edge-align, and on the
// undo/redo restore that edge-align was the whole defect: in target view the
// restore re-lands the playhead after the map swap, and a playhead that stood
// OFFSCREEN dragged the viewport to wherever its instant now painted — a
// camera move with no subject the user asked about, target view only and so
// seemingly random. The restore's camera answers to the markers it restores
// (the visual tail, undo.cpp), and a translation is not a movement, so this
// write moves neither the camera nor the audition (no audition end, the
// reseat's own exemption).
//
// TWO CALLERS (re-derived by grep 2026-09-24):
//   * THE RESTORE'S MAP-CHANGE RE-LAND (undo.cpp), the reason above.
//   * THE MARKER DRAG'S PLAYHEAD TOW (MarkerDragOps::apply_drag_motion,
//     marker_drag.cpp; architect 2026-09-24). The cursor rides the dragged
//     flag, and a keep-visible edge-align there scrolled the viewport under
//     the held hand on a grab whose stem stood left of the viewport — a
//     synchronous plate render inside the displayed-basis freeze, the basis
//     the gesture reads moving mid-drag. Here the playhead may ride offscreen
//     for the drag's life and catches up at the release (commit_drag's land,
//     after the freeze lifts). The tow is the ride of a movement already
//     taken, not a movement of its own: the press's click act landed the
//     playhead (land_playhead_on_marker, the audition's end and the hold's)
//     and nothing can re-arm either under the drag-modal gate (the argument
//     is at the site).
// The live-domain clamp is the shared ruling (clamp_playhead_to_live_domain);
// the viewport's own domain wall is re-derived through clamp_viewport_start
// because the restore's swap may have shortened the domain under a standing
// viewport — a wall, not a scroll toward the cursor, and a no-op under the
// drag, which moves neither the domain nor the camera. NO RENDER: the
// restore's tail renders the plate synchronously once for the finished state,
// and the drag's plate does not move, so this owes only the damage.
void Viewport::translate_playhead_to(int64_t new_sample) {
    if (audio.total_frames() <= 0) return;
    app.playhead_cursor_sample =
        clamp_playhead_to_live_domain(new_sample, app, audio);
    clamp_viewport_start(app, audio);
    // Full waveform-area damage for the no-scroll branch's reason above (the
    // cursor's pixels are plate-registered; rule at playhead_pixel_x).
    invalidate_waveform_area();
    invalidate_clock_area();
    // NO PREDICTOR RESYNC, unlike the reseat: both callers run with playback
    // stopped (restore_history_entry's head; the drag's arming press, with
    // every launch swallowed until the release), so there is no play in
    // flight to re-anchor.
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
// (app_state.h); the arithmetic is move_playhead_by_arrow_step's own, which
// reads this for its landing since 2026-08-30 (planner decision 60) through
// playhead_arrow_step_landing's Columns arm, the Left / Right buttons' face
// reading the same owner. The two degenerate cases the act
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

int64_t playhead_arrow_step_landing(const AppState& app, const GuiAudio& audio,
                                    HorizontalArrowStep step) {
    if (step.unit == HorizontalArrowStep::Unit::Columns)
        return playhead_pixel_step_landing(app, audio, step.count);
    if (audio.total_frames() <= 0) return app.playhead_cursor_sample;
    return clamp_playhead_to_live_domain(
        app.playhead_cursor_sample + static_cast<int64_t>(step.count) * kRs,
        app, audio);
}

void Viewport::move_playhead_by_arrow_step(HorizontalArrowStep step) {
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
    // the logical one for the same reason the click placement's item basis
    // carries that quantization: it is the grid actually drawn (under the multiple-of-16 width contract the two
    // agree, but the painted grid is the principled input).
    // The recovery nearbyint is the column direction and is this walk's own; the
    // landing is the shared owner's. move_playhead_to still owns the walls, and
    // a playhead parked off-lattice re-snaps onto it at its first step. THE
    // WHOLE ARITHMETIC LIVES AT playhead_pixel_step_landing since 2026-08-30
    // (planner decision 60), the Left / Right buttons' face reading the same
    // landing; a step at a wall still reaches move_playhead_to, whose
    // unconditional audition end is the KEY's to keep (the greyed button
    // forgoes it, the skips' own shape). ON THE PHASE-RESET COLUMN THE UNIT
    // IS A HOP (architect 2026-09-21, horizontal_arrow_step): the same road in
    // its own unit, exactly kRs target frames, no grid and no rounding, the
    // landing owner's Hops arm — everything said above about columns is the
    // Columns arm's.
    move_playhead_to(playhead_arrow_step_landing(app, audio, step));
}

// Apply a zoom change. The numeric target is derived inside; this helper
// handles the playhead-centered viewport recompute its callers share (`c`,
// `0` and the framing acts — the zoom step anchors on the viewport's centre
// instead, apply_zoom_step).
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

    // A ZOOM WRITE THAT MOVES THE LEVEL LEAVES THE CEILING, so bare `0`'s
    // whole-song state goes with it (ViewState::whole_song_visible, whose
    // declaration owns the ruling and names all four clears). It runs BEFORE
    // the clamp below, which pins the level to the ceiling while the bit
    // stands — so this is what lets an explicit request through. `0`'s own
    // zoom-out arm sets the bit after this applier returns.
    active_view_state(app).whole_song_visible = false;

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
    // jump a frame ahead of the waveform — one sync render per discrete zoom.
    // The render's cost bound is stated once, at waveform_cache.cpp's
    // synchronous-render paragraph (the pyramid's per-column bound, and the
    // magnified plate's expander loop that scales with the source seconds
    // per column).
    kick_waveform_sync();
}

void Viewport::apply_strip_drag_zoom(double new_zoom_level, double anchor_sample,
                                     double anchor_x, bool final) {
    if (audio.total_frames() <= 0) return;

    const bool   level_changed = (new_zoom_level != app.zoom_level);
    const int64_t old_vp       = app.viewport_start_sample;

    // The whole-song state's clear, the appliers' shared rule (the inventory
    // is at ViewState::whole_song_visible): a drag frame that moves the level
    // has left the ceiling; a pure pan frame has not.
    if (level_changed) active_view_state(app).whole_song_visible = false;

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

    // FOLLOW IS SUSPENDED AT THE CLAMP ABOVE, NOT HERE (architect
    // 2026-09-23): a camera this applier moves — either axis, the level
    // included, since a song-anchored zoom carries the view off the scanner
    // as a pan does — is a camera change at clamp_viewport_start, which puts
    // out the hold and suspends a following play's paging
    // (AppState::camera_hold, AppState::follow_suspended). The zoom STEP rides
    // this applier and keeps the HOLD posture at its own site
    // (apply_zoom_step); follow has no exemption here, so a zoom during a
    // following play suspends its paging. `level_changed`
    // reports a real move, not a request: ALL THREE callers — the nav drag's
    // zoom phase (apply_nav_zoom_at), the two-finger touch-nav body
    // (apply_touch_nav_update) and the zoom step (apply_zoom_step) —
    // pre-clamp new_level into the same [kMinZoom, effective_max_zoom_level]
    // window clamp_viewport_start re-applies, so the pre-assignment compare
    // cannot read a wall-saturated no-op as movement.

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
    // cost a zoom step pays per press, capped at once per pointer frame by
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

    // The whole-song state's clear, the appliers' shared rule (the inventory
    // is at ViewState::whole_song_visible): a framing that moves the level has
    // left the ceiling.
    if (new_zoom_level != old_level)
        active_view_state(app).whole_song_visible = false;

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

    // Past the return above either the level or the start really moved, so
    // this is a zoom, a pan, or both at once — the trim bar's span-framing
    // double-click, the group undo/redo restore's zoom-out-to-fit arm and bare
    // `0`'s restore being what reach here.

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

// THE ZOOM STEP ZOOMS ABOUT THE VIEWPORT'S CENTRE (architect 2026-09-22): the
// frame under the waveform's centre column before the step is placed at that
// same column after it, through the strip-drag family's applier — the pinch
// and the ctrl-drag's own "hold this frame at this column" placement, here
// with the column fixed at the centre and one terminating event per press.
// No playhead and no scanner term: the step is a camera act about what the
// user is looking at. clamp_viewport_start, inside the applier, still wins —
// near the song's ends the centre frame slides off the centre column, and at
// the ceiling the whole song is visible. The applier pays what every zoom
// pays (the synchronous rebuild, the damage, the predictor resync while
// playing) and clears the whole-song bit on a level move; as a song-anchored
// camera move it also suspends a following play's paging, as the pinch and
// the ctrl-drag do (the chokepoint's compare, AppState::follow_suspended). IT KEEPS THE
// HOLD POSTURE (architect 2026-09-23): the step pivots on the viewport's
// centre, so a subject an explicit centring put there stays there, and the
// nudges that follow keep holding its column — the one zoom that is an
// exemption from the chokepoint's clear, spelled here after the applier
// (AppState::camera_hold). Neither step writes `0`'s recall stamp. The
// caller has already pre-clamped the level (the applier's contract) and
// asked its actionable predicate, so the level moves.
void Viewport::apply_zoom_step(double new_zoom_level) {
    const GuiRect wf_area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (wf_area.w <= 0 || spp <= 0.0) return;
    const double centre_col = static_cast<double>(wf_area.w) / 2.0;
    const double centre_frame =
        static_cast<double>(app.viewport_start_sample) + centre_col * spp;
    const bool hold_before = app.camera_hold;
    apply_strip_drag_zoom(new_zoom_level, centre_frame, centre_col,
                          /*final=*/true);
    app.camera_hold = hold_before;
}

// THE LEADING RETURNS ARE ONE OWNER EACH (zoom_in_step_actionable and
// zoom_out_step_actionable, app_state.h, which the two buttons' faces read
// too): at the floor or the effective ceiling the step is a consumed, silent
// no-op and its button greys.
void Viewport::zoom_in() {
    if (!zoom_in_step_actionable(app, audio)) return;
    // One whole level deeper from the current (possibly fractional) rung,
    // saturating at the floor; clamp_zoom_level is the bounds' one owner, and
    // past the return above it hands back a level strictly below the current.
    apply_zoom_step(clamp_zoom_level(app, audio, app.zoom_level - 1.0));
}

void Viewport::zoom_out() {
    if (!zoom_out_step_actionable(app, audio)) return;
    // One whole level shallower, saturating at the effective per-file ceiling
    // (there is nothing beyond it — full zoom-out is whole-song-visible).
    apply_zoom_step(clamp_zoom_level(app, audio, app.zoom_level + 1.0));
}

void Viewport::scroll_viewport(int64_t delta_samples, bool continuous) {
    if (audio.total_frames() <= 0) return;
    const int64_t old_vp = app.viewport_start_sample;
    app.viewport_start_sample += delta_samples;
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) {
        // EVERY PAN ENDS THE HOLD AND SUSPENDS A FOLLOWING PLAY'S PAGING at
        // the clamp above, the one compare every viewport write passes
        // (AppState::camera_hold, AppState::follow_suspended); a pan that
        // moved nothing (wall-saturated) settles the same camera and ends
        // nothing. This is the pan funnel —
        // PageUp/PageDown, the plain-wheel stepped pan, touchpad scroll and
        // the plain-drag grab-pan all land here.
        invalidate_waveform_area();
        // Flag positions move with the viewport, so the top strip must
        // repaint too — the flags carry their own text now, so this one
        // invalidation covers every marker pixel the move affects.
        const GuiRect ts = top_strip_area(app);
        gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
        // A discrete pan (the plain wheel, PageUp/PageDown) re-anchors here -
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
        // touchpad, the plain wheel, PageUp/PageDown, the plain-drag grab-pan — and the
        // synchronous render also gives them all the grab-pan's old guarantee:
        // no frame paints overlays against a plate from an older basis.
        kick_waveform_sync();
    }
}

// THE TWO LANDING PLACEMENTS, spelled once each for the cameras that share
// them. The start each returns is unclamped: the caller writes it and passes
// clamp_viewport_start, which owns the song's two ends and the grid.
//   * centred_viewport_start — `target` at the window's centre: the centring
//     body (center_viewport_on_playhead) and the landing owner's centre
//     answer (land_subject);
//   * paged_in_viewport_start — `target` the edge margin in from the LEFT
//     edge: follow's page-in (follow_scroll_if_needed) and the landing
//     owner's coarse walk page-in (land_subject).
namespace {
int64_t centred_viewport_start(int64_t target, int64_t visible) {
    return target - visible / 2;
}
int64_t paged_in_viewport_start(int64_t target, int64_t visible) {
    return std::max<int64_t>(0, target - viewport_edge_margin_samples(visible));
}
}  // namespace

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
    app.viewport_start_sample = centred_viewport_start(target, visible);
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) finish_discrete_viewport_move();
}

// THE DISCRETE MOVE'S TAIL, shared by the four one-shot camera moves that land
// the viewport on a playhead or a span — center_viewport_on_playhead,
// hold_subject_column_after_nudge, follow_scroll_if_needed and
// land_subject: waveform and top-strip damage (the flags
// move with the viewport), the predictor re-anchored if playing, and the
// synchronous rebuild so the playhead overlay does not lead the waveform by a
// frame. Callers invoke it on their changed path alone.
void Viewport::finish_discrete_viewport_move() {
    invalidate_waveform_area();
    const GuiRect ts = top_strip_area(app);
    gui.invalidate_region(ts.x, ts.y, ts.w, ts.h);
    if (playback.is_playing()) playback.resync_predictor();
    kick_waveform_sync();
}

uint64_t Viewport::waveform_gain_hash() const {
    return waveform_gain_fingerprint(app);
}

void Viewport::kick_waveform_sync_if_gain_changed(uint64_t prior_hash) {
    if (waveform_gain_hash() == prior_hash) return;
    kick_waveform_sync();
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

// THE NUDGE HOLDS ITS SUBJECT'S COLUMN WHILE THE HOLD POSTURE STANDS
// (architect 2026-09-17 for the hold — "I like to see every delta when
// nudging" — and 2026-09-23 for what chooses it). A Left/Right step that MOVED
// SOMETHING calls this on its changed path, AT EVERY ZOOM, when the press's
// camera is NudgeCamera::HoldColumn, and the viewport is placed so the subject
// — the resting cursor the step has just landed — paints in THE COLUMN IT
// PAINTED IN BEFORE THE STEP, clamped into the waveform's first and last
// columns. THE CAMERA IS THE POSTURE'S (nudge_camera, app_state.h, reading
// AppState::camera_hold, which an explicit centring arms): with it dark the
// step never calls this and follows the edge through the movement owner's own
// keep-visible edge-align. (It was the Ctrl modifier's choice from 2026-09-22
// to 2026-09-23, Ctrl+Left / Ctrl+Right and two buttons of their own; those
// chords bind nothing now.) So an onscreen subject keeps its exact screen
// column and the waveform slides under it by the nudge's own delta, and a
// subject that was offscreen lands on the edge column on its side and stays
// there on later nudges, the window walking with it. NOTHING CENTRES: `c` is
// the centring act, this only keeps the column the centring left.
//
// THE PRIOR PLACE IS A PARAMETER because the landing has already run: the
// movement owner's keep-visible edge-align (reseat_playhead_to) may have
// scrolled the viewport before this body is reached, so the column is taken
// against the viewport the subject painted on before the nudge. It arrives as
// THE PAINTED COLUMN, each caller deriving it on the basis its subject's
// pixels were painted with (architect 2026-09-24, strictly as painted), and
// reduces to a SAMPLE OFFSET on the live grid: the column clamped into
// [0, w − 1], times q the painter-quantized spp (painter_samples_per_pixel),
// that offset clamped into [0, floor((w − 1)·q)] — the painted column is
// nearbyint(offset / q) (displayed_column_at, the playhead's and the stems'
// placement), so offset 0 paints column 0 and the upper bound paints column
// w − 1, never one past it. The new start is new subject − offset through
// clamp_viewport_start, whose grid snap absorbs the sub-sample residue of a
// column-anchored step, and whose song-end clamp WINS at the song's two ends:
// there the column cannot be held and the subject walks toward the wall
// instead — accepted. A move that changes nothing (a zero delta) repaints
// nothing. THE WRITE IS A LIVE NAVIGATION WRITE either way: the viewport it
// places is the one the next frame paints.
//
// Both nudges have stopped playback before their write, so the cursor is the
// subject. TWO CALLERS, re-derived by grep 2026-09-24, each on its
// NudgeCamera::HoldColumn arm:
//   * THE MARKER NUDGE'S COMMIT TAIL (finish_position_nudge,
//     position_nudge.cpp) passes the PAINTED COLUMN — the focused marker's
//     pre-write frame through painted_column_of_source_frame_on_basis over
//     the displayed map and item_viewport_basis, the SAME painted basis its
//     step anchored on (stepped_anchor_frame), so the column held is the one
//     the marker visibly occupied even while a viewport job (a resize
//     re-clamp, a target-map publish) has moved the live viewport ahead of
//     the pixels (architect 2026-09-24, strictly as painted).
//   * THE WAVEFORM-LANE PLAYHEAD STEP (GuiInputHandler::
//     run_waveform_lane_playhead_step) passes the cursor's column before the
//     step on the PLATE basis the cursor pass paints it with
//     (GuiPaintHandler::plate_viewport_basis; cold, the item basis, then the
//     live viewport by its own contract), so a viewport job in flight
//     cannot move the column held ahead of the pixels (architect 2026-09-24,
//     strictly as painted). A held arrow's repeats and a held arrow button's fires
// reach both through the same act bodies, so the hold runs at every step. The
// write below changes the camera, so the chokepoint puts the posture out; the
// nudge keeps it across its whole act at its dispatch (AppState::camera_hold's
// first exemption), which is what makes the next step hold too. The P column's
// unit is a HOP, so there the held subject slides
// the waveform a hop's width per step. NO VIEW TERM:
// in target view on the warp column the marker nudge is refused upstream
// (active_column_authoring_allowed) and never arrives, while the playhead step
// holds there as anywhere. THE ZOOM IS NEVER CHANGED HERE.
void Viewport::hold_subject_column_after_nudge(int prior_column) {
    if (audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const double q = painter_samples_per_pixel(app, audio, area);
    if (q <= 0.0 || area.w <= 0) return;
    // The painted column's own offset on the live grid: nearbyint(offset / q)
    // recovers the column (displayed_column_at), and the clamp keeps the last
    // column's offset inside the window.
    const int64_t last_offset = static_cast<int64_t>(
        std::floor(static_cast<double>(area.w - 1) * q));
    const int64_t offset = std::clamp<int64_t>(
        static_cast<int64_t>(std::nearbyint(
            static_cast<double>(std::clamp(prior_column, 0, area.w - 1)) * q)),
        0, last_offset);
    const int64_t old_vp = app.viewport_start_sample;
    app.viewport_start_sample = app.playhead_cursor_sample - offset;
    clamp_viewport_start(app, audio);
    if (app.viewport_start_sample != old_vp) finish_discrete_viewport_move();
}

// Follow's page during playback (AppState::follow): when the scanner leaves the viewport, scroll
// so the scanner lands THE EDGE MARGIN in from the new view's LEFT edge
// (kViewportEdgeMarginFraction, app_state.h — 5 % since 2026-09-22, the
// architect finding the old 10 % lead "starts way too late"; the class's
// inventory is at that declaration), leaving the rest of the window ahead.
// Only the first move beyond vp_end triggers a scroll. Called at launch too
// (right after the one launch body's seed — launch_playback_window — sets the
// scanner to the launch position), so the same landing rule places an
// offscreen launch position the same margin in from the left edge.
// THE LAUNCH CALL IS THE CALLER'S WORD SINCE 2026-09-18
// (GuiPlaybackLifecycle::LaunchCamera): every GUI road asks for it and starts
// on screen anyway (Space's cursor launch is the one that a pan can have
// carried out of view; a scrub click is a visible column already), while the
// CAR'S play of the trim — the one launch that begins off screen by design —
// asks for it only when the follow lamp is lit, so the camera stays where
// the user left it.
//
// THE PAGE IS FOLLOW'S OWN CAMERA MOVE AND SUSPENDS NOTHING (architect
// 2026-09-23): its write passes the chokepoint, which puts out the hold and
// suspends follow, and the suspension is restored behind it
// (AppState::follow_suspended). The HOLD posture stays out — a page is a
// camera move not on the subject a centring asked to keep.
void Viewport::follow_scroll_if_needed() {
    const int64_t visible = samples_visible(app, audio);
    if (visible <= 0) return;
    const int64_t target = app.playhead_scanner_active
        ? app.playhead_scanner_sample
        : app.playhead_cursor_sample;
    const int64_t vp_end = app.viewport_start_sample + visible;
    if (target < app.viewport_start_sample || target >= vp_end) {
        const int64_t old_vp = app.viewport_start_sample;
        app.viewport_start_sample = paged_in_viewport_start(target, visible);
        const bool suspended_before = app.follow_suspended;
        clamp_viewport_start(app, audio);
        app.follow_suspended = suspended_before;
        // Viewport actually moved — THE PAGE TAKES THE DISCRETE MOVE'S TAIL,
        // and with it THE SYNCHRONOUS REBUILD (architect 2026-09-02), the same
        // body every user-driven pan/zoom frame takes. It kicked the ASYNC
        // worker until that day, and until the worker published, every
        // surface painted the OLD viewport while the scanner's column sat
        // outside it: the playhead line VANISHED for a frame or two at every
        // page (main.cpp's pre-paint documents that offscreen scanner as its
        // own fallback case). The retired centered pin (2026-08-31 to
        // 2026-09-13) proved the synchronous rebuild fits inside a frame EVERY
        // frame; follow pays it once per page — and the 2026-08-07 flicker
        // ruling (github-recheck.md) put a reported flicker onto this path the
        // same way. The tail was spelled here inline until 2026-09-22, minus
        // the top-strip damage the shared tail carries — which a page owes all
        // the same, the flags moving with the viewport.
        if (app.viewport_start_sample != old_vp) finish_discrete_viewport_move();
    }
}

// THE LANDING OWNER (architect 2026-09-24): ONE camera for every act that
// walks or restores a subject onto the screen, over an ACTIVE-DOMAIN range
// [lo, hi] (lo == hi for a single marker), its answers chosen by the caller's
// LandingKind. It reads the zoom, and that read is legal because it happens at
// a discrete act — a keystroke does not pop the picture the way a
// zoom-derived picture did during a zoom gesture (the rule at nudge_camera's
// neighbour, app_state.h). THE ZOOM IS NEVER WRITTEN HERE.
//
// WALK — the Tab walk and the march's steps, always a single frame:
//   * AT THE WORKING ZOOM OR FINER (app.zoom_level <= kWorkingZoomLevel; 2.0
//     exactly is "at working", anything above is coarse) the subject is
//     CENTRED, ON SCREEN OR NOT, the centring body's own placement: the walk
//     always moves one way and every landing frames alike, so no half of the
//     screen is skipped;
//   * COARSER, an on-screen subject moves NOTHING and an off-screen one is
//     PAGED IN, lo landing the edge margin in from the LEFT edge in both
//     directions — follow's own placement (paged_in_viewport_start).
// RESTORE — undo / redo, a singleton (lo == hi) or a group's [earliest,
// latest]; a restore is a non-linear jump, and centring is the least
// prejudicial way of framing one:
//   * WHOLLY ON SCREEN (lo ≥ start and hi < start + visible, in painted
//     samples): NOTHING MOVES, at every zoom;
//   * CANNOT FIT: the range is wider than 1 − 2 × the edge margin of the
//     visible window at the live zoom — the room the span framer's margin arm
//     (frame_span_into_view, input_handler.cpp) leaves a span, taken in the
//     framer's own unrounded domain (spp × W at the live level, its
//     `visible_t`), which guarantees a range refused here solves to a level
//     no finer than the current one there, so the caller's zoom-out never
//     zooms in. RETURNS FALSE HAVING WRITTEN NOTHING: the zoom-out fit is the
//     caller's;
//   * OFF SCREEN AND FITS: CENTRED on its midpoint AT EVERY ZOOM, coarse
//     included; never a page-in.
// THE HOLD POSTURE (AppState::camera_hold) IS ARMED BY THE WALK'S CENTRING
// ALONE (architect 2026-09-24), after the chokepoint — a walked marker centred
// at the working zoom is expected to hold its column, and it arms even where a
// wall keeps it off the centre. THE RESTORE NEVER ARMS (its singleton centring
// included) and never clears on its own: its centring of an off-screen
// subject clears the bit at the chokepoint like any camera move, and its
// no-move answer leaves it as it stands. WHY: an automatic arm on arrival at
// the centre would freeze the viewport under a run of nudges that happened to
// reach the middle; `c` then nudging means "I'm looking for a place to drop a
// marker" and wants the hold, `c` then panning means "I want the working zoom
// but my own viewport" and the pan clears it — and the playhead head's lamp
// (kPlayheadHeadHeld, render.h) now shows which posture stands. A walk's
// page-in passes the chokepoint, which puts the hold out as at any page; a
// no-move answer leaves the posture as it stands. Degenerate geometry (no
// strip width, no sample rate, nothing visible) writes nothing and answers true. clamp_viewport_start
// owns the song's two ends and the grid; the changed path takes the discrete
// move's tail.
//
// RULED OUT, never to be re-proposed (architect 2026-09-24):
//   * LEAST MOVEMENT as a camera (2026-09-22 to 2026-09-24): it "sounds good
//     but it actually doesn't feel good in practice";
//   * CENTRE-ONLY-IF-OFF-SCREEN for the walk (dc8b13a8, one day): with three
//     markers on screen it centred the first, left the next two and centred
//     the fourth — "we've ignored the left half of the screen";
//   * PAGE-IN for undo / redo: a restore is a non-linear jump, and paging it
//     "feels odd even at coarse zooms";
//   * THE AUDIO-VIEW FORK of the walk's camera (2026-09-23, one day): the walk
//     frames alike in both audio views.
//   * AN UNDO / REDO CENTRING ARMING THE HOLD (f25778d1, one morning,
//     2026-09-24): a lamp now shows the posture, so only the walk and `c`
//     arm it.
//
// ITS READERS, re-grepped 2026-09-24:
//   * WALK: jump_playhead_to_focused_marker's MarkerLandingFrame::Land arm
//     (input_handler.cpp — bare Tab / Shift+Tab / IsoLeftTab through
//     cycle_marker_focus, and both steps of the live Ctrl+Shift+Tab march),
//     and cycle_history_diff_flag_focus's Land arm (input_key_dispatch.cpp —
//     the `h` view's Tab and both steps of its march); each lands the cursor
//     it has just seated (lo == hi), so the verdict is dropped;
//   * RESTORE: restore_history_entry's singleton arm (undo.cpp) on the cursor
//     just landed, and its group arm on the restored markers' [earliest,
//     latest] extent — THE ONE CALLER THAT CAN MEET THE FALSE VERDICT, which
//     runs the span framer's margin arm on it.
// NOT READERS, by ruling: bare `c`, Shift+J and the A/B audition, which
// centre unconditionally (center_viewport_on_playhead after the working
// zoom); follow's page-in during playback (follow_scroll_if_needed, which
// reads the scanner and keeps follow's suspension its own); the nudge's
// cameras.
bool Viewport::land_subject(int64_t lo, int64_t hi, LandingKind kind) {
    if (hi < lo) std::swap(lo, hi);   // defensive; the callers pass in order
    const int     W  = waveform_area(app).w;
    const int     sr = audio.sample_rate();
    const int64_t visible = samples_visible(app, audio);
    if (W <= 0 || sr <= 0 || visible <= 0) return true;
    const int64_t vp_end = app.viewport_start_sample + visible;
    const bool on_screen = lo >= app.viewport_start_sample && hi < vp_end;
    // The working-zoom read, inline by ruling: the walk's one zoom term.
    const bool fine = app.zoom_level <= kWorkingZoomLevel;
    bool centre = false;
    switch (kind) {
        case LandingKind::Walk:
            if (!fine && on_screen) return true;
            centre = fine;
            break;
        case LandingKind::Restore:
            if (on_screen) return true;
            centre = true;
            break;
    }
    const double visible_t = samples_per_pixel_at(app.zoom_level, sr) *
                             static_cast<double>(W);
    const double room = (1.0 - 2.0 * kViewportEdgeMarginFraction) * visible_t;
    if (static_cast<double>(hi - lo) > room) return false;
    const int64_t old_vp = app.viewport_start_sample;
    app.viewport_start_sample = centre
        ? centred_viewport_start(lo + (hi - lo) / 2, visible)
        : paged_in_viewport_start(lo, visible);
    clamp_viewport_start(app, audio);
    if (kind == LandingKind::Walk && centre) app.camera_hold = true;
    if (app.viewport_start_sample != old_vp) finish_discrete_viewport_move();
    return true;
}
