#include "viewport.h"

#include "audio.h"
#include "notifications.h"   // notification_stack_bound (the stack's damage rect)
#include "onscreen_keyboard.h"   // the keyboard's band; modal_box.h through it (the modal's damage)
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

// WHERE A Home / End JUMP WOULD LAND THE CURSOR — the contract, the two arms,
// the clamp's purpose and the four readers are all at the declaration
// (app_state.h): the live Home / End, their shift forms (row 8's START and
// END buttons' plain and shift presses, 2026-10-24) and the mode's absolute
// jumps spell one bound once instead of once per route, and
// playhead_end_jump_actionable just below, the jump acts' "would this form
// change anything" owner, is how the acts' own no-op refusals read this
// landing.
int64_t playhead_skip_landing_frame(const AppState& app, const GuiAudio& audio,
                                    bool forward, bool whole_piece) {
    if (whole_piece || app.history_mode.active) {
        // THE WHOLE-PIECE ARM, ONE ARM WITH TWO ENTRANTS. The `h` history view
        // takes it for every jump (architect 2026-08-05: the view reviews the
        // WHOLE piece, so an End stopping at a trim bound would hide the flags
        // past it), and the SHIFT forms take it anywhere (architect 2026-09-26:
        // frame 0 and the last frame whatever the trim window is; Ctrl until
        // 2026-10-24). The ends
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

// WOULD THIS FORM OF THE JUMP CHANGE ANYTHING — the contract and the reader
// inventory are at the declaration (app_state.h). Each term is an act write
// read from that write's own owner: the stop's (transport_session_live), the
// selection clear's (the live arms' selection-or-focus pair, or the mode
// arm's diff-flag pair through history_mode_revert_subject_standing — the
// same two fields clear_history_mode_focus tests), and the landing compare
// against the resting cursor. (A shown trim overlay was a fourth term until
// 2026-09-22, when the movement owner stopped hiding it.)
bool playhead_end_jump_actionable(const AppState& app, const GuiAudio& audio,
                                  bool forward, bool whole_piece) {
    if (transport_session_live(app)) return true;
    const bool clear_would_act =
        app.history_mode.active
            ? history_mode_revert_subject_standing(app.history_mode)
            : (marker_selection_standing(app) || marker_focus_standing(app));
    if (clear_would_act) return true;
    return playhead_skip_landing_frame(app, audio, forward, whole_piece) !=
           app.playhead_cursor_sample;
}

// Viewport changes repaint the waveform area and the top strip together:
// flag positions depend on the viewport, so any pan/zoom has to refresh
// flags as well as waveform. The scanner's per-frame advance keeps its own
// narrow rect (invalidate_scanner_line below). The rect and the lanes it holds are at the
// declaration (viewport.h); the rows above it are owed a check
// (damage_window_top_rows).
void Viewport::invalidate_waveform_area() {
    const GuiRect a = waveform_area(app);
    damage_window_top_rows(app, gui, a.y + a.h);
}

// THE WINDOW-TOP RECT, SPLIT AT THE VIEWPORT'S LANES (2026-10-13; the rule
// at the declaration, viewport.h): the rows from viewport_lanes_top_y down
// are damaged now, the rows above it owed to every paint target's check.
void damage_window_top_rows(AppState& app, GuiPlatform& gui, int y1) {
    const int top = viewport_lanes_top_y(app);
    // A rect that reaches no viewport lane brings no frame of its own to
    // check at, so it is declared as it stands (unreached by the callers,
    // each of which reaches the waveform's top or the top strip's foot).
    if (y1 <= top || top <= 0) {
        gui.invalidate_region(0, 0, app.width, y1);
        return;
    }
    gui.invalidate_region(0, top, app.width, y1 - top);
    for (int t = 0; t < GuiPlatform::kPaintTargetCount; ++t) {
        int& owed = app.upper_rows_painted[static_cast<std::size_t>(t)].owed_y1;
        owed = std::max(owed, y1);
    }
}

// THE STATE CELL'S DAMAGE — the BOTTOM ROW'S lane whole (the cell is the
// clock's neighbour since 2026-08-29's fold). The caller inventory and the
// reasoning for taking the lane rather than a span of it are at the
// declaration, viewport.h.
void Viewport::invalidate_status_cell_area() {
    const GuiRect t = bottom_row_area(app);
    gui.invalidate_region(t.x, t.y, t.w, t.h);
}

// THE MODAL'S DAMAGE — every rect the modal can paint and every rect its
// close must give back, the unified bottom row's lane among them (the modal
// has stood in the row since 2026-08-13, and the row's own tenants come back
// on a close). No cell arithmetic: the rects below are the modal family's own
// geometry, the lane alone covering only what keeps to the row, which a
// prompt's and a dialog editor's box does not (below) — the stash's owner the
// one published bit read, and only to say whether a box stood last frame.
// A STANDING DIALOG EDITOR'S EDITS do not come through here either: they
// take the box's interior alone (invalidate_dialog_editor_box, below).
// The OPENERS do not come through here — nothing is
// painted before a surface's first paint, so they invalidate the whole
// window. Caller inventory at the declaration.
// A PROMPT, A DIALOG EDITOR OR THE PROGRESS MODAL STANDS IN THE MODAL
// FAMILY'S BOX (2026-10-14, modal_box.h): its question, its line or its
// sentence in the box over the canvas column,
// its buttons on the controls row — so while one stands, OR WHILE THE LAST
// PAINT DREW ONE (the stash's owner, which a closer reads before any paint:
// its surface is gone from the state but not yet from the screen), the
// damage is the lane UNITED WITH THE MODAL FAMILY'S WHOLE AREA without the
// keyboard (modal_area_rect at keyboard height 0 — the view bar's top row to
// the window's foot, which contains the stack's lifted form over a
// standing keyboard too), so a close erases the box and brings the lanes,
// the canvas and row 8 back. A dialog editor's close takes the keyboard with
// it, and the slot's hide edge damages that band (onscreen_keyboard::
// slot_damage_rect, reconcile_slot_standing's). Under the render player and
// the picker the controls row is the lane's own rows, and their
// list's damage is the folder overlay's. (The taskbar dialog, 2026-10-10 to
// 2026-10-14, took the lane united with its overlay to the window's foot.)
void Viewport::invalidate_modal_dialog_area() {
    const GuiRect t = bottom_row_area(app);
    gui.invalidate_region(t.x, t.y, t.w, t.h);
    const AppState::ModalDialogOwner painted = app.modal_dialog.owner;
    if (modal_box_dialog_stands(app) || score_viewer_stands(app) ||
        painted == AppState::ModalDialogOwner::Prompt ||
        painted == AppState::ModalDialogOwner::Editor ||
        painted == AppState::ModalDialogOwner::Progress ||
        painted == AppState::ModalDialogOwner::Score)
        invalidate_rect(modal_area_rect(app, /*keyboard_h=*/0));
    // THE STANDING DIALOG TOOLTIP (the rule is at the declaration): its
    // published rect and the band every seat of it lies in, the show edge's
    // own (tooltip_damage_band), which holds the recomposed box whole.
    const AppState::RedesignTooltip& tip = app.redesign_tooltip;
    if (tip.visible &&
        tip.owner.surface == AppState::RedesignTooltip::Surface::Dialog) {
        invalidate_rect(tip.rect);
        invalidate_rect(tooltip_damage_band(app));
    }
}

// A STANDING DIALOG EDITOR'S EDIT — the box's interior at the keyboard height
// it stands on, the box the editor's line paints in (the rule and the caller
// inventory at the declaration, viewport.h).
void Viewport::invalidate_dialog_editor_box() {
    invalidate_rect(modal_box_interior_rect(
        app, onscreen_keyboard::modal_keyboard_h_px(app)));
}

// THE NOTIFICATION STACK'S DAMAGE — its room, whole; the callers and the
// reasoning are at the declaration, viewport.h.
void Viewport::invalidate_notification_stack() {
    const GuiRect r = notification_stack_bound(app);
    gui.invalidate_region(r.x, r.y, r.w, r.h);
}

void Viewport::invalidate_clock_area() {
    const GuiRect c = clock_invalidate_rect(app);
    gui.invalidate_region(c.x, c.y, c.w, c.h);
}

// The clock cell's check owed to every paint target (the rule and the
// callers at the declaration, viewport.h).
void Viewport::owe_clock_check() {
    for (int t = 0; t < GuiPlatform::kPaintTargetCount; ++t)
        app.clock_painted[static_cast<std::size_t>(t)].owed = true;
}

void Viewport::invalidate_playhead_columns(int old_px, int new_px) {
    const GuiRect area = waveform_area(app);
    const GuiRect r_old = playhead_invalidate_rect(app, area, old_px);
    const GuiRect r_new = playhead_invalidate_rect(app, area, new_px);
    // Union when close (common case: 1px nudges overlap) — one expose.
    if (rects_intersect(r_old, r_new) ||
        std::abs(new_px - old_px) < 4) {
        const GuiRect u = union_rect(r_old, r_new);
        if (u.w > 0 && u.h > 0) {
            gui.invalidate_region(u.x, u.y, u.w, u.h);
        }
    } else {
        if (r_old.w > 0) gui.invalidate_region(r_old.x, r_old.y, r_old.w, r_old.h);
        if (r_new.w > 0) gui.invalidate_region(r_new.x, r_new.y, r_new.w, r_new.h);
    }
}

// The scanner's old and new rects, united where they touch (the rule and
// the two callers at the declaration, viewport.h; the rect's at
// scanner_invalidate_rect, main.cpp). Both stand on the area's rows, so
// they touch exactly when their column intervals overlap or abut.
void Viewport::invalidate_scanner_line(double old_x, double new_x) {
    const GuiRect area  = waveform_area(app);
    const GuiRect r_old = scanner_invalidate_rect(area, old_x);
    const GuiRect r_new = scanner_invalidate_rect(area, new_x);
    if (r_old.w > 0 && r_new.w > 0 &&
        std::max(r_old.x, r_new.x) <=
            std::min(r_old.x + r_old.w, r_new.x + r_new.w)) {
        const GuiRect u = union_rect(r_old, r_new);
        gui.invalidate_region(u.x, u.y, u.w, u.h);
        return;
    }
    if (r_old.w > 0) gui.invalidate_region(r_old.x, r_old.y, r_old.w, r_old.h);
    if (r_new.w > 0) gui.invalidate_region(r_new.x, r_new.y, r_new.w, r_new.h);
}

// move_playhead_to: THE MOVEMENT OWNER — the A/B audition's END in front of the
// reseat below. A PLAYHEAD MOVEMENT ENDS THE A/B AUDITION: what the act
// promises is that the pair of plays it makes on each tab is identical, which
// is exactly a resting cursor that cannot move under it. Reaching this function
// means the playhead's POSITION IN THE MUSIC is changing. THE RULE'S SECOND
// OWNER is the land (land_playhead_on_marker / land_playhead_on_domain_frame,
// input_pointer.cpp), and it has THREE EXEMPTIONS, none of which reaches an
// owner: a TRANSLATION (reseat_playhead_to below, translate_playhead_to — the
// undo/redo restore's re-land, which scrolls nothing — its twin
// tow_playhead_to, the marker drag's tow, and
// reseat_playhead_on_marker — the S/T flip, the map-change re-lands, the
// coincidence auto-select's no-op), a RESTORE (the tab switch's band swap,
// which writes the cursor direct — the act's own two tab switches are
// restores) and THE CAMERA, which writes no playhead at all. The trim family
// writes no cursor at all (architect 2026-11-07, session 88, the rule at the
// head of input_trim.cpp), and a trim write stops playback through the one
// stop body, which ends the act anyway. UNCONDITIONAL, never gated on whether the write moved
// anything. (From 2026-08-19 to 2026-09-22 these owners also hid the trim
// region overlay; that hide was deleted with the overlay's resting form.) This
// is a class statement: the complete clearing-owner inventory is at
// GuiAuditionSequence (app_state.h) and is not to be restated here.
//
// A PLAYHEAD MOVEMENT ALSO PUTS OUT THE HOLD POSTURE (architect 2026-09-23):
// the column an explicit centring asked to keep belonged to the subject where
// it stood, so a movement is one of the posture's three movement-owner clears,
// the two lands being the others — this owner at every call, the lands only
// when they change the cursor (architect 2026-09-26: a land onto the sample
// already held is no movement of the posture's subject). The three exemptions
// are the nudge, which
// keeps the bit across its whole act, the undo/redo singleton restore
// under the hold, which holds the column after its land and re-arms the bit,
// and the marker drop where the walk it lands by does not center
// (seat_playhead_on_drop, below; at the working zoom that walk arms the bit
// itself, architect 2026-10-29); the rule is at AppState::camera_hold.
void Viewport::move_playhead_to(int64_t new_sample) {
    clear_audition_sequence(app);
    app.camera_hold = false;
    reseat_playhead_to(new_sample);
}

// THE DROP'S SEAT AND ITS CAMERA, the one owner for every drop body
// (GuiWarpMarkersOps::drop_marker,
// GuiPhaseResetMarkersOps::drop_phase_reset_at_position).
// THE SEAT is the movement owner's (move_playhead_to), which puts the hold
// out as every movement does.
// THE DROP THEN LANDS AS TAB DOES (architect 2026-10-29, "drop should be
// another action that centers when you're at working zoom … like Tab"): the
// landing owner's walk (land_subject, LandingKind::Walk) over the playhead
// AFTER the seat — the new marker, and on the P column's lead-in drop the
// reset itself, the playhead seated back onto it. AT THE WORKING ZOOM AS
// PAINTED (the walk's own term) the marker is CENTERED, on screen or not, and
// the hold ARMED by the walk; at every other zoom, finer or coarser, an
// on-screen marker moves nothing and an off-screen one is paged in by the
// walk's own rule. Working zoom only, not every zoom (closed_questions.md).
// A single sample always fits, so the verdict is dropped, as the walk's other
// callers drop it.
// THE HOLD POSTURE'S THIRD EXEMPTION (architect 2026-10-12, "if the playhead
// has a hold bit and a new marker is dropped, it inherits the hold bit": a
// drop under the hold is authored on the held column) STANDS WHERE THE WALK
// DID NOT CENTER. The walk asks first and its centering supersedes the
// exemption: at the working zoom the center becomes the held column and the
// walk's own arm the bit, so a bit standing after the walk is that arm (the
// seat having put it out) and the exemption asks nothing — no column is held
// only to be centered over. Off the working zoom the hold stands only on a
// file too short for it (`c`'s clamped level, AppState::camera_hold), the
// walk does not center there, and the exemption runs as ruled: the bit, the
// cursor and the AS-PAINTED item basis are read BEFORE the seat; a seat that
// moved the cursor (the lead-in drop seats the playhead
// kPhaseResetLeadInSamples before where it stood) holds the column the cursor
// painted in before the drop, the undo singleton restore's precedent
// (restore_history_entry, undo.cpp), so the bit stays truthful
// (hold_derived_on_arrival): the playhead paints in the held column; a seat
// that moved nothing (the warp drop, any same-sample re-affirm) only re-arms.
// A dark bit off the working zoom leaves the movement and the walk's answer.
void Viewport::seat_playhead_on_drop(int64_t new_sample) {
    const bool held = app.camera_hold;
    const int64_t cursor_before = app.playhead_cursor_sample;
    const ItemViewportBasis painted = item_viewport_basis(app, audio);
    move_playhead_to(new_sample);
    (void)land_subject(app.playhead_cursor_sample, app.playhead_cursor_sample,
                       LandingKind::Walk);
    if (!held || app.camera_hold) return;
    if (painted.spp > 0.0 && app.playhead_cursor_sample != cursor_before)
        hold_subject_column_after_nudge(displayed_x_at(
            static_cast<double>(cursor_before), painted.vp_start, painted.spp,
            painted.lattice_origin));
    app.camera_hold = true;
}

namespace {
// THE KEEP-VISIBLE EDGE-ALIGN'S PLACEMENT: THE EDGE COLUMN ON THE SUBJECT'S
// OWN FRAME (architect 2026-11-15, the no-grid arc: every landing exact, the
// start in frames with no lattice page) — the start at which `subject` stands
// on the left edge of the edge column on the side it left by: past the LEFT
// edge the view's first column, x = 0, the start the subject's own frame;
// past the RIGHT edge the view's last column, x = w − 1, the start
// nearbyint(subject − (w − 1)·q). The chokepoint rests that start as asked
// (ViewportLattice::Exact), so the subject paints within the whole-frame
// rounding of that place, 0.5 / q px, and is on screen as painted
// (displayed_frame_on_screen: its column is the edge column). BOTH SIDES ARE
// "THE EDGE COLUMN", and the px each shows follows the mark's model, not this
// placement: a mark is the boundary at its column's left edge, so at the left
// its line stands half inside the strip and at the right whole. Unclamped:
// clamp_viewport_start owns the song's two ends, and at the right wall the
// wall wins — a frame in the song's last half-column paints at grid point w
// at every viewport there (the off-edge rule, source_frame_off_right_edge),
// so the edge-align settles on the wall.
int64_t edge_aligned_viewport_start(int64_t subject, double q, int w,
                                    bool past_right) {
    if (!past_right) return subject;
    return static_cast<int64_t>(std::nearbyint(
        static_cast<double>(subject) - static_cast<double>(w - 1) * q));
}
}  // namespace

// reseat_playhead_to: update playhead, keep viewport so playhead stays
// visible. Invalidate only what changed. Clamps to the full audio
// range; trim is purely cosmetic so the playhead is free to sit
// outside the trim window.
//
// VISIBLE IS AS PAINTED (architect 2026-09-28): the keep-visible edge-align
// asks displayed_frame_on_screen (warp_frame_map_view.h) of the new cursor on
// the live viewport and grid, so a cursor landing in the view's last
// half-column — inside the sample span, but painted at grid point w with no
// stem — pages, and one in the half-column left of the start, painted at
// column 0, does not. Off screen, the viewport takes
// edge_aligned_viewport_start above: the cursor in the last column past the
// right edge, in column 0 past the left.
//
// THE WRITE ALONE, WITH NO AUDITION END IN IT, and the callers who want it
// that way are the ones whose write is NOT a movement (2026-08-19). RE-DERIVED BY GREP
// 2026-09-22 — EIGHT, in two families:
//   * THE MAP-CHANGE RE-LANDS, all in a target-view re-warp tail: both arms of
//     the Up/Down tempo cent step (warpmarkers_ops.cpp) and, since 2026-08-25,
//     the WARP STATUS/VALUE FAMILY admitted in W+target with them — Ctrl+D,
//     Ctrl+N and Delete (warpmarkers_ops.cpp) and the flag editor's payload
//     commit (flag_editor.cpp), whose shared contract is stated at the head of
//     warpmarkers_ops.cpp; and since 2026-09-02 THE TWO OTHER
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
//   * THE TRIM FAMILY is NOT here: no trim writer writes the cursor since
//     2026-11-07 (session 88, the rule at the head of input_trim.cpp), so the
//     trim's surfaces take no suppression, they simply do not pass through
//     here.
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
    const int64_t old_origin = app.viewport_lattice_origin;
    const GuiRect area = waveform_area(app);
    const double  q = painter_samples_per_pixel(app, audio, area);

    app.playhead_cursor_sample = new_sample;

    // THE EDGE-ALIGN IS A LANDING, THE STAY A RE-VALIDATION (architect
    // 2026-10-31; the rules at AppState::viewport_lattice_origin): a page
    // rests at the start it asks for (Exact), while a cursor that stays on
    // screen moves no camera, so the clamp keeps the standing lattice under
    // it.
    ViewportLattice lattice = ViewportLattice::Keep;
    if (q > 0.0 &&
        !displayed_frame_on_screen(static_cast<double>(new_sample),
                                   static_cast<double>(old_vp), q,
                                   old_origin, area.w)) {
        // Off screen and at or right of the start is past the right edge
        // (a frame in the half-column left of the start paints at column 0).
        app.viewport_start_sample = edge_aligned_viewport_start(
            new_sample, q, area.w, new_sample >= old_vp);
        lattice = ViewportLattice::Exact;
    }
    // THE KEEP-VISIBLE EDGE-ALIGN SUSPENDS NO FOLLOW (architect 2026-09-23):
    // a camera move onto the playhead is a move onto follow's own subject,
    // not the user looking elsewhere, so the chokepoint's suspension inside
    // the clamp is undone for that one bit (AppState::follow_suspended). HOLD
    // is the movement owners' to clear, and a reseat is not one of them.
    const bool suspended_before = app.follow_suspended;
    clamp_viewport_start(app, audio, lattice);
    app.follow_suspended = suspended_before;
    // The changed path is the settled camera's: at the right wall an
    // edge-align the wall refuses leaves the start where it stood and pays no
    // render (a landing that moved the lattice alone is a change).
    const bool viewport_changed = app.viewport_start_sample != old_vp ||
                                  app.viewport_lattice_origin != old_origin;

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
// ONE CALLER (re-derived by grep 2026-11-16): THE RESTORE'S MAP-CHANGE
// RE-LAND (undo.cpp), the reason above. (The marker drag's tow is
// tow_playhead_to below: the same write under the playhead's own narrow
// damage.)
// The live-domain clamp is the shared ruling (clamp_playhead_to_live_domain);
// the viewport's own domain wall is re-derived through clamp_viewport_start
// because the restore's swap may have shortened the domain under a standing
// viewport — a wall, not a scroll toward the cursor; a re-validation, so it
// keeps the camera's lattice (ViewportLattice::Keep, architect 2026-10-31).
// NO RENDER: the
// restore's tail renders the plate synchronously once for the finished state,
// so this owes only the damage.
void Viewport::translate_playhead_to(int64_t new_sample) {
    if (audio.total_frames() <= 0) return;
    app.playhead_cursor_sample =
        clamp_playhead_to_live_domain(new_sample, app, audio);
    clamp_viewport_start(app, audio, ViewportLattice::Keep);
    // Full waveform-area damage for the no-scroll branch's reason above (the
    // cursor's pixels are plate-registered; rule at playhead_pixel_x).
    invalidate_waveform_area();
    invalidate_clock_area();
    // NO PREDICTOR RESYNC, unlike the reseat: the caller runs with playback
    // stopped (restore_history_entry's head), so there is no play in flight
    // to re-anchor.
}

// tow_playhead_to: THE MARKER DRAG'S PLAYHEAD TOW (MarkerDragOps::
// apply_drag_motion, marker_drag.cpp; architect 2026-09-24) — THE
// TRANSLATION'S WRITE, the cursor to its new instant and the camera where it
// stands. The cursor rides the dragged flag, and a keep-visible edge-align
// there scrolled the viewport under the held hand on a grab whose stem stood
// left of the viewport — a synchronous plate render inside the
// displayed-basis freeze, the basis the gesture reads moving mid-drag. Here
// the playhead may ride offscreen for the drag's life and catches up at the
// release (commit_drag's land, after the freeze lifts). The tow is the ride
// of a movement already taken, not a movement of its own: the press's click
// act landed the playhead (land_playhead_on_marker, the audition's end, and
// the hold's when the press moved the cursor; a press on the marker under
// the playhead leaves the hold to commit_drag's move_playhead_to, which
// clears it at every release) and nothing can re-arm either mid-drag
// (on_key's drag-modal statement: no key arrives mid-gesture). The
// viewport's clamp is the translation's own re-validation and a no-op under
// the drag, which moves neither the domain nor the camera.
//
// ITS DAMAGE IS THE PLAYHEAD'S OWN (architect 2026-11-16, the beats: a
// marker drag's frame repaints what moved and nothing else): the playhead's
// narrow rect (playhead_invalidate_rect, main.cpp — the head's painted reach
// from the viewport's lanes' top row through the waveform's foot, which
// holds the head in the ruler and the line on the canvas and nothing of the
// toolbar band) at the cursor's OLD COLUMN AND ITS
// NEW ONE, BOTH ON THE PLATE BASIS (the playheads' damage rule,
// playhead_pixel_x, app_state.h: the cursor's pixels are plate-registered) —
// the old column derived before the write and the new after it, never
// stashed, the way the scanner's advance derives its places (main.cpp's
// pre-paint hook): the plate basis is what paint_playheads and the ruler's
// head draw the cursor through, so the column it answers for the cursor as
// it still stands is the column the last paint put the playhead on. The
// basis is the paint handler's, reached through plate_playhead_column_
// (viewport.h). SOUND UNDER THE DRAG: the displayed basis is frozen for its
// life (displayed_basis_frozen, app_state.h — no plate is dispatched or
// published and no gesture path reaches a synchronous plate rebuild), and a
// plate that does publish damages the whole area itself. The clock cell,
// which reads the cursor's time, is damaged beside it. What else a motion
// changes — the dragged cue, its stem, the lead-in ring — is the flag
// cache's rebuild's, run by the same event (apply_drag_motion; the damage at
// maybe_rebuild_flag_cache's tail), the stem's rect repainting the cursor's
// line where the stem hides or uncovers it (the coincident lines). THE KEYS
// whose face reads the cursor are the tick's comparator's (main.cpp).
// THE WHOLE AREA STAYS where the narrow shape cannot be trusted: the hook
// unwired, or a clamp that did move the camera.
// NO RENDER and NO PREDICTOR RESYNC: the drag's plate does not move, and the
// drag runs with playback stopped (its arming press, with every launch
// swallowed until the release).
void Viewport::tow_playhead_to(int64_t new_sample) {
    if (audio.total_frames() <= 0) return;
    if (!plate_playhead_column_) {
        translate_playhead_to(new_sample);
        return;
    }
    const int     old_px     = plate_playhead_column_();
    const int64_t old_vp     = app.viewport_start_sample;
    const int64_t old_origin = app.viewport_lattice_origin;
    const double  old_zoom   = app.zoom_level;
    app.playhead_cursor_sample =
        clamp_playhead_to_live_domain(new_sample, app, audio);
    clamp_viewport_start(app, audio, ViewportLattice::Keep);
    if (app.viewport_start_sample != old_vp ||
        app.viewport_lattice_origin != old_origin ||
        app.zoom_level != old_zoom)
        invalidate_waveform_area();
    else
        invalidate_playhead_columns(old_px, plate_playhead_column_());
    invalidate_clock_area();
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
    // own, which a shrinking domain could strand outside [0, live_total - 1].
    // The region IS the trim now, so there is no stored endpoint left to
    // validate. The TRIM's own bounds are SOURCE frames, walled at load and at
    // every gesture, and are not this function's subject.)
}

// WHERE ONE ARROW STEP WOULD LAND THE CURSOR — the contract is at the
// declaration (app_state.h); the arithmetic is move_playhead_by_arrow_step's
// own, which reads this for its landing, the Left / Right buttons' face
// reading the same owner. The degenerate cases — no audio, a dead sample
// rate under the millisecond — answer the resting cursor
// itself, so a step there is the no-op it always was and the face greys.
int64_t playhead_arrow_step_landing(const AppState& app, const GuiAudio& audio,
                                    HorizontalArrowStep step) {
    if (audio.total_frames() <= 0) return app.playhead_cursor_sample;
    // The step in frames of the ACTIVE domain, the cursor's own: a
    // millisecond's frames at the piece's rate (arrow_millisecond_frames —
    // the two views share the one rate), or a hop's kRs. A dead rate has no
    // millisecond and answers the cursor itself, the act's own no-move.
    int64_t unit_frames = kRs;
    if (step.unit == HorizontalArrowStep::Unit::Milliseconds) {
        if (audio.sample_rate() <= 0) return app.playhead_cursor_sample;
        unit_frames = arrow_millisecond_frames(audio.sample_rate());
    }
    // Pre-clamped into the live domain, the clamp move_playhead_to would apply
    // anyway — a landing must be a frame the cursor can occupy, which is what
    // makes the wall compare exact.
    return clamp_playhead_to_live_domain(
        app.playhead_cursor_sample +
            static_cast<int64_t>(step.count) * unit_frames,
        app, audio);
}

void Viewport::move_playhead_by_arrow_step(HorizontalArrowStep step) {
    if (audio.total_frames() <= 0) return;
    // ONE STEP IN THE ACTIVE COLUMN'S UNIT (horizontal_arrow_step,
    // gui_input.h), in pure integer arithmetic on the cursor's own frame —
    // no grid, no map and no rounding: ON THE WARP COLUMN ONE MILLISECOND
    // (architect 2026-11-15, the no-grid arc: "the left-right nudge should be
    // recalibrated to one millisecond left or right ... we would convert 1
    // millisecond to the nearest amount in frames, and that would be the
    // nudge step"; the marker nudge's own unit, position_nudge.h's ONE
    // MILLISECOND A PRESS), arrow_millisecond_frames of the active domain
    // whatever the zoom — 44 frames at 44.1 kHz: about a pixel at the working
    // zoom, a fraction of one above it, under two at the deepest — the line
    // painted at its exact place, so every press shows; ON THE PHASE-RESET
    // COLUMN ONE HOP (architect 2026-09-21), exactly kRs target frames. So a
    // step means the same frames whatever the camera, and Right then Left
    // returns the cursor to its frame off the walls. THE WHOLE ARITHMETIC
    // LIVES AT playhead_arrow_step_landing, the Left / Right buttons' face
    // reading the same landing; move_playhead_to still owns the walls and the
    // keep-visible edge-align, which pages the view exactly where a step
    // carries the cursor off screen (reseat_playhead_to); a step at a wall
    // still reaches move_playhead_to, whose unconditional audition end is the
    // KEY's to keep (the greyed button forgoes it, the skips' own shape).
    move_playhead_to(playhead_arrow_step_landing(app, audio, step));
}

// Apply a zoom change. The numeric target is derived inside; this helper
// handles the playhead-centered viewport recompute its callers share (`c`,
// `0` and the framing acts).
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
    // effective ceiling samples_visible >= total, so
    // clamp_viewport_start's visible >= total branch parks the start at 0
    // (whole song visible) without any mode test.
    const int64_t target = app.playhead_scanner_active
        ? app.playhead_scanner_sample
        : app.playhead_cursor_sample;
    const int64_t visible = samples_visible(app, audio);
    app.viewport_start_sample = target - visible / 2;
    clamp_viewport_start(app, audio, ViewportLattice::Exact);

    invalidate_waveform_area();
    // A CHECK, NOT A REPAINT, OF THE CLOCK: a zoom moves the viewport, never
    // the playhead or the scanner, so the clock's value cannot change here —
    // the record for all three zoom appliers is at owe_clock_check's
    // declaration (viewport.h).
    owe_clock_check();
    // Flags live in the top strip — rect positions change when the viewport
    // scale changes (the selected marker's lane text rides its flag there;
    // row 8's state cell says nothing a zoom can move). The top strip's rows
    // above the viewport's lanes are owed a check (damage_window_top_rows).
    const GuiRect ts = top_strip_area(app);
    damage_window_top_rows(app, gui, ts.y + ts.h);
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
                                     double anchor_x, bool final,
                                     ViewportLattice lattice) {
    if (audio.total_frames() <= 0) return;

    const bool   level_changed = (new_zoom_level != app.zoom_level);
    const int64_t old_vp       = app.viewport_start_sample;
    const int64_t old_origin   = app.viewport_lattice_origin;

    // The whole-song state's clear, the appliers' shared rule (the inventory
    // is at ViewState::whole_song_visible): a drag frame that moves the level
    // has left the ceiling; a pure pan frame has not.
    if (level_changed) active_view_state(app).whole_song_visible = false;

    app.zoom_level = new_zoom_level;

    // Place the song anchor at anchor_x: pick the viewport start that paints
    // anchor_sample at that column, at the new level — on the PAINTED grid, the
    // step the columns are drawn at (painter_samples_per_pixel, the
    // sixteenth-frame q), which every caller's anchor was also seated and
    // rebound on, so the pivot stays under its own column rather than
    // drifting by the q-vs-spp residue across the width. It reads the level
    // just assigned, so this is q(new_level); for a pure pan
    // (level unchanged) it reproduces the caller's post-pan viewport exactly,
    // recovered by nearbyint. At the effective ceiling the anchor cannot pin a
    // column (the whole song is visible); clamp_viewport_start's visible >= total
    // branch parks the start at 0 and the drag is inert.
    //
    // THE PLACEMENT'S LATTICE IS THE CALLER'S RULE (architect 2026-10-31; the
    // rules at AppState::viewport_lattice_origin). Under EXACT (the gesture's
    // zoom frames and the zoom step) the requested start is the rest: its
    // lattice origin is
    // derived from it, so the pivot's frame sits at its held column TO ONE
    // FRAME (the rounding of (k0 + c)·q), not within the half column a
    // frame-0 lattice's snap would leave — a fresh residue every zoom frame,
    // the bubbling peaks measured at the pivot on the tablet the same day.
    // Under KEEP (the gesture's pan frames, FOLLOW's scroll) the start snaps
    // onto the current origin's lattice, so a pan is a pure index shift
    // there. A rest the walls clamp rests at the wall at origin 0 under
    // both.
    const double spp = painter_samples_per_pixel(app, audio, waveform_area(app));
    app.viewport_start_sample = static_cast<int64_t>(std::nearbyint(
        anchor_sample - anchor_x * spp));
    clamp_viewport_start(app, audio, lattice);
    const bool vp_changed = (app.viewport_start_sample != old_vp ||
                             app.viewport_lattice_origin != old_origin);

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
    // out the hold and suspends a following play's scroll
    // (AppState::camera_hold, AppState::follow_suspended); a pan or a zoom
    // has no exemption here, so it suspends the following play's scroll, and
    // the follow scroll itself restores the suspension behind its call
    // (follow_scanner). `level_changed` reports a real move, not a request:
    // the two zooming callers — the
    // touch-nav body (apply_touch_nav_update, the pinch and the one-finger
    // ctrl zoom) and the zoom step (zoom_step) — pre-clamp
    // new_level into the same [effective_min_zoom_level, effective_max_zoom_level]
    // window clamp_viewport_start re-applies, so the pre-assignment compare
    // cannot read a wall-saturated no-op as movement.

    invalidate_waveform_area();
    // A check of the clock, like apply_zoom_change's (the record is at
    // owe_clock_check's declaration, viewport.h).
    owe_clock_check();
    // Flags live in the top strip — rect positions change when the viewport
    // scale or start changes (the rows above the viewport's lanes owed a
    // check, damage_window_top_rows).
    const GuiRect ts = top_strip_area(app);
    damage_window_top_rows(app, gui, ts.y + ts.h);

    // Rest state (final) re-anchors the playback predictor once, like the
    // continuous pan's release; mid-gesture events do NOT resync (the predictor
    // keeps extrapolating smoothly for the drag's duration). Repaint dispatch:
    // EVERY frame of the drag takes one SYNCHRONOUS full rebuild — the cost
    // any discrete zoom pays, capped at once per pointer frame by
    // the platform's motion coalescing. Level-changed and pan-only frames are
    // no longer distinguished: the incremental shift-and-strip fast-path the
    // pan-only frames used was retired 2026-07-26, so the drag renders the same
    // way whichever axis moved.
    if (final && playback.is_playing()) playback.resync_predictor();
    kick_waveform_sync();
}

// THE ZOOM STEP ZOOMS ABOUT THE VIEWPORT'S CENTRE (architect 2026-09-22,
// kept when the pair returned 2026-10-27), AN EXACT LANDING
// (ViewportLattice::Exact, architect 2026-11-15, the no-grid arc — the start
// that puts the pivot at the center is the rest, its held pivot below curing
// the run's accumulation): the frame painted at the
// waveform's centre column before the run's first press (below) is placed at
// that same column after each press of the run, through the strip-drag
// family's applier — the pinch's and the
// Ctrl+drag's own "hold this frame at this column" placement, here with the
// column fixed at the centre and one terminating event per press, the frame
// read on the painted grid the applier places on (painter_samples_per_pixel).
// No playhead and no scanner term: the step is a camera act about what the
// user is looking at. clamp_viewport_start, inside the applier, still wins —
// near the song's ends the centre frame slides off the centre column, and at
// the ceiling the whole song is visible.
//
// IT IS A ZOOM LIKE EVERY OTHER, WITH NO EXEMPTION SPELLED HERE (architect
// 2026-10-27, keeping 2026-09-25's "every zoom clears the camera hold"): the
// applier pays what every zoom pays (the synchronous rebuild, the damage, the
// predictor resync while playing), the chokepoint's compare puts the HOLD
// out and suspends a following play's scroll (AppState::camera_hold,
// follow_suspended), and a level move clears Zoom to Fit's whole-song state
// (ViewState::whole_song_visible), so a step taken while FIT stands drops
// the recall exactly as a Ctrl+drag zoom there does — the next `0` zooms out
// again, stamping the stepped view. A step never writes the recall stamp,
// so steps taken before FIT leave the view FIT returns to as FIT stamped it.
// THE LEADING RETURN IS THE ONE OWNER the two keys' faces read
// (zoom_step_actionable, app_state.h): at its wall the step is a consumed,
// silent no-op and the key greys there.
//
// THE PIVOT IS SEATED ONCE PER RUN OF PRESSES (architect 2026-10-28; the
// record and its reason are at AppState::zoom_step_pivot): the first press
// reads the centre frame off the rest, and each following press reuses that
// same frame while the live camera (start, level, tab, audio view) still
// rests exactly where the previous press left it and the waveform area is
// still the width it had. So each press lands the held frame at the centre
// column to one frame — the start's own whole-frame rounding, never the sum
// of the run's — and IN then OUT returns to the prior start wherever
// neither press met a wall or a level bound (a whole-level step off the
// bounds is exact in the level's double; the origin is that start's own). A pan, Center, FIT, a tab switch, an S/T flip or
// any other camera writer between presses moves the compared fields and
// ends the run; the next press seats afresh. The test is posture identity, so
// a detour that returns the five fields to exactly the step's own copy — a
// Ctrl+Tab to the other tab and back, which restores that tab's saved start
// and level, or FIT followed by its recall — is indistinguishable from the
// run and continues it: the held frame still sits within a frame of
// the centre column, the rest the detour returned to, so the reuse costs
// nothing and keeps IN then OUT reversible across the detour. A window resize
// ends the run too, since the centre column itself moved: the next press
// zooms about the frame centred now. Near a wall the clamp slides the held
// frame off the centre column and the run keeps that same frame, so the run
// is reversible off the walls as the Ctrl+drag's anchor is.
void Viewport::zoom_step(double levels) {
    if (!zoom_step_actionable(app, audio, levels)) return;
    const GuiRect wf_area = waveform_area(app);
    const double spp = painter_samples_per_pixel(app, audio, wf_area);
    if (wf_area.w <= 0 || spp <= 0.0) return;
    const double centre_col = static_cast<double>(wf_area.w) / 2.0;
    const AppState::CameraPostureIdentity live{
        app.viewport_start_sample, app.zoom_level,
        app.active_tab_view, app.active_audio_view,
        app.viewport_lattice_origin};
    const AppState::ZoomStepPivot& held = app.zoom_step_pivot;
    const double pivot_frame =
        (held.valid && held.left == live && held.width == wf_area.w)
            ? held.frame
            : static_cast<double>(app.viewport_start_sample) +
                  centre_col * spp;
    apply_strip_drag_zoom(zoom_step_level(app, audio, levels), pivot_frame,
                          centre_col, /*final=*/true, ViewportLattice::Exact);
    app.zoom_step_pivot = {true, pivot_frame,
                           {app.viewport_start_sample, app.zoom_level,
                            app.active_tab_view, app.active_audio_view,
                            app.viewport_lattice_origin},
                           wf_area.w};
}

// WOULD THE APPLIER BELOW MOVE THE CAMERA — the contract and the two readers
// are at the declaration (app_state.h).
bool zoom_to_start_would_move(const AppState& a, const GuiAudio& audio,
                              double new_zoom_level, int64_t new_start,
                              std::optional<int64_t> parked_origin) {
    if (audio.total_frames() <= 0) return false;
    const double level = clamp_zoom_level(a, audio, new_zoom_level);
    if (std::fabs(level - a.zoom_level) >= 1e-9) return true;
    // The rest under THE APPLIER'S OWN RULE (architect 2026-11-15, the
    // no-grid arc): a framing lands Exact — the start as asked and the origin
    // it derives, or the wall it passed at origin 0 — and a recall rests its
    // stamped start on its stamped origin (resting_parked_viewport_start),
    // so a request whose rest is the live camera, start AND origin, moves
    // nothing.
    const ViewportRest rest =
        parked_origin
            ? resting_parked_viewport_start(a, audio, new_start,
                                            *parked_origin)
            : resting_viewport_start(a, audio, new_start,
                                     ViewportLattice::Exact);
    return rest.start != a.viewport_start_sample ||
           rest.lattice_origin != a.viewport_lattice_origin;
}

void Viewport::apply_zoom_to_start(double new_zoom_level, int64_t new_start,
                                   std::optional<int64_t> parked_origin) {
    // IDEMPOTENT NO-OP (zoom_to_start_would_move, the test's one owner since
    // 2026-10-25, when the Zoom to Fit key's face came to ask it ahead): the
    // resting (level, start) this call would produce equals the current
    // viewport, so nothing would move — return before any write, without a
    // repaint. A pan/zoom since the last framing makes them differ and this
    // proceeds to re-frame. (Until then the test ran after the writes, which
    // put identical values back.) The no-piece return is the predicate's.
    if (!zoom_to_start_would_move(app, audio, new_zoom_level, new_start,
                                  parked_origin))
        return;

    // Pre-clamp the requested level to the per-file window. clamp_viewport_start
    // re-applies the identical clamp as the chokepoint; the whole-song clear
    // below compares the clamped level.
    new_zoom_level = clamp_zoom_level(app, audio, new_zoom_level);

    const double  old_level = app.zoom_level;

    // The whole-song state's clear, the appliers' shared rule (the inventory
    // is at ViewState::whole_song_visible): a framing that moves the level has
    // left the ceiling.
    if (new_zoom_level != old_level)
        active_view_state(app).whole_song_visible = false;

    // Set the level, then the start EXPLICITLY (the span's left edge, not a
    // playhead recenter), and funnel through the two clamp chokepoints.
    app.zoom_level = new_zoom_level;
    app.viewport_start_sample = new_start;
    // A framing lands Exact; a recall comes back on its stamped lattice (the
    // level is written above, so the parked origin is judged at its own
    // step).
    if (parked_origin)
        clamp_viewport_start_parked(app, audio, *parked_origin);
    else
        clamp_viewport_start(app, audio, ViewportLattice::Exact);

    // Past the head's return either the level or the start really moved, so
    // this is a zoom, a pan, or both at once — the trim bar's span-framing
    // double-click, the group undo/redo restore's zoom-out-to-fit arm and bare
    // `0`'s restore being what reach here.

    invalidate_waveform_area();
    // A check of the clock, like apply_zoom_change's (the record is at
    // owe_clock_check's declaration, viewport.h).
    owe_clock_check();
    // Flags live in the top strip — rect positions change with the viewport
    // (the rows above the viewport's lanes owed a check,
    // damage_window_top_rows).
    const GuiRect ts = top_strip_area(app);
    damage_window_top_rows(app, gui, ts.y + ts.h);
    if (playback.is_playing()) playback.resync_predictor();
    // A discrete one-shot viewport jump: render the plate synchronously and
    // publish the displayed fingerprint now so the top-strip flags and the
    // playhead column do not jump a frame ahead of the waveform.
    kick_waveform_sync();
}

// THE TWO LANDING PLACEMENTS, spelled once each for the cameras that share
// them. The start each returns is unclamped: the caller writes it and passes
// clamp_viewport_start under ViewportLattice::Exact, which owns the song's
// two ends and RESTS THE START AS ASKED (architect 2026-11-15, the no-grid
// arc: no snap to a frame-0 lattice, the origin derived from the start) — so
// a centered target stands at x = w / 2 to the whole-frame rounding of
// visible / 2, exactly there at the working zoom's whole q and even w.
//   * centred_viewport_start — `target` at the window's centre: the centring
//     body (center_viewport_on_playhead) and the landing owner's centre
//     answer (land_subject);
//   * paged_in_viewport_start — `target` the edge margin in from the LEFT
//     edge: follow's page-in (follow_scroll_if_needed) and the landing
//     owner's walk page-in at every zoom but the working one, finer or
//     coarser (land_subject).
namespace {
int64_t centred_viewport_start(int64_t target, int64_t visible) {
    return target - visible / 2;
}
int64_t paged_in_viewport_start(int64_t target, int64_t visible) {
    return std::max<int64_t>(0, target - viewport_edge_margin_samples(visible));
}
}  // namespace

// THE HOLD DERIVED ON ARRIVAL (architect 2026-09-29; the contract and its one
// caller at the declaration, app_state.h). A tab switch keeps the hold if
// nothing else that would put it out is done, and each tab keeps its own; the
// shape is DERIVED, never stored: on arrival in a tab the bit is armed iff the
// tab rests EXACTLY where `c` would have left it — the working zoom as
// painted, and the camera THE REST OF THE PLAYHEAD'S CENTRING, so the
// playhead paints in precisely the place `c` would put it in (*"the
// hold holds only if it is precisely the same column as the centre"*). A
// grab-pan that rests even one column off it arrives dark.
//
// THE REST IS THE CENTRING'S OWN (architect 2026-11-15, the no-grid arc:
// every landing exact): C = cursor − visible / 2, the start `c` asks for, at
// the lattice origin that start derives (resting_viewport_start under
// ViewportLattice::Exact, the rule `c` lands by) — the live start AND the
// live origin must both equal it. No frame-0 lattice is asked: any origin
// counts, provided it is the centring's.
//
// NEVER AT A WALL (architect 2026-09-29, the same night): C is the centring's
// start WITHOUT the wall clamp, and it must lie within
// [0, max_viewport_start_grid] as well as equal the live start. A centring
// whose start falls outside the walls — the playhead within half a window of
// either end — is a CLAMPED one, and answers false whatever the viewport: at a
// wall the clamp makes every rest with the playhead inside that half-window
// look like a centring, so a view parked near either end by any road would
// spuriously earn the hold. A centring that lands EXACTLY on a wall's grid
// point is unclamped and counts. So `c` armed at a wall stands while the view
// stays, but a switch away and back arrives dark there; `c` is the repair.
//
// THE ONE-COLUMN TOLERANCE WAS STRUCK THE SAME NIGHT: it had been tried so a
// centred rest would survive the S/T flip, whose anchored start need not be
// the centring's; the S/T switch's
// derivation was removed instead (*"then just remove that"*), the flip putting
// the bit out at the chokepoint as it always had. A tab parked centred while
// the other tab flips has its band shifted by its playhead's delta, which
// keeps the centring's start to the frame only where the playhead moved by
// the same whole delta: where it does not, it arrives dark, the correct
// answer for a band that is no longer centred.
bool hold_derived_on_arrival(const AppState& app, const GuiAudio& audio) {
    const GuiRect area = waveform_area(app);
    if (area.w <= 0 || audio.sample_rate() <= 0) return false;
    const int64_t visible = samples_visible(app, audio);
    if (visible <= 0) return false;
    const double q = painter_samples_per_pixel(app, audio, area);
    if (q <= 0.0) return false;
    // The working zoom AS PAINTED, the landing owner's exact equality
    // (land_subject carries why an exact compare is legitimate).
    if (q != static_cast<double>(audio.working_column())) return false;
    // The centring's start, NOT clamped: outside the walls it is a clamped
    // centring and never arms.
    const int64_t centred =
        centred_viewport_start(app.playhead_cursor_sample, visible);
    if (centred < 0 || centred > max_viewport_start_grid(app, audio))
        return false;
    // The centring's rest, start and origin, under the rule `c` lands by.
    const ViewportRest rest =
        resting_viewport_start(app, audio, centred, ViewportLattice::Exact);
    return app.viewport_start_sample == rest.start &&
           app.viewport_lattice_origin == rest.lattice_origin;
}

void Viewport::center_viewport_on_playhead() {
    if (audio.total_frames() <= 0) return;
    // THE RESTING PLAYHEAD, ALWAYS: every caller centres with playback
    // stopped — the `c` command's live recipe stops on both its arms and the
    // marker jump's Center arm stops ahead of its land, while the `h` view
    // (the command's mode arm and the diff-flag step's Center arm) has no
    // playback at all — so no scanner is live to centre on.
    const int64_t target = app.playhead_cursor_sample;
    const int64_t visible = samples_visible(app, audio);
    const int64_t old_vp = app.viewport_start_sample;
    const int64_t old_origin = app.viewport_lattice_origin;
    app.viewport_start_sample = centred_viewport_start(target, visible);
    clamp_viewport_start(app, audio, ViewportLattice::Exact);
    if (app.viewport_start_sample != old_vp ||
        app.viewport_lattice_origin != old_origin)
        finish_discrete_viewport_move();
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
    damage_window_top_rows(app, gui, ts.y + ts.h);
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
// — the resting cursor the step has just landed — paints at THE EXACT PLACE
// IT HELD BEFORE THE STEP (architect 2026-11-15, the no-grid arc: every
// landing exact — its x, displayed_x_at, not the column that x rounds to),
// held between the waveform's first and last columns' left edges. THE CAMERA IS THE POSTURE'S (nudge_camera, app_state.h, reading
// AppState::camera_hold, which an explicit centring arms): with it dark the
// step never calls this and follows the edge through the movement owner's own
// keep-visible edge-align. (It was the Ctrl modifier's choice from 2026-09-22
// to 2026-09-23, Ctrl+Left / Ctrl+Right and two buttons of their own; those
// chords bind nothing now.) So an onscreen subject keeps its exact screen
// place and the waveform slides under it by the nudge's own delta, and a
// subject that was offscreen lands on the edge column on its side and stays
// there on later nudges, the window walking with it. NOTHING CENTRES: `c` is
// the centring act, this only keeps the place the centring left.
//
// THE PRIOR PLACE IS A PARAMETER because the landing has already run: the
// movement owner's keep-visible edge-align (reseat_playhead_to) may have
// scrolled the viewport before this body is reached, so the place is taken
// against the viewport the subject painted on before the nudge. It arrives as
// THE PAINTED EXACT x, each caller deriving it on the basis its subject's
// pixels were painted with (architect 2026-09-24, strictly as painted), and
// reduces to a SAMPLE OFFSET at the live step: the x clamped into
// [0, w − 1], times q the painter-quantized spp (painter_samples_per_pixel),
// to the whole frame, that offset clamped into [0, floor((w − 1)·q)] — so
// offset 0 paints the subject at the first column's left edge and the
// upper bound paints column w − 1, never one past it. The new start is new
// subject − offset through clamp_viewport_start under ViewportLattice::Exact,
// which rests it as asked, so the subject paints at the held x to that one
// rounding (none at the working zoom after the first held step: the offset
// is then the whole number of frames the subject stood from the start), and
// whose song-end clamp WINS at the song's two ends:
// there the place cannot be held and the subject walks toward the wall
// instead — accepted. A move that changes nothing (a zero delta) repaints
// nothing. THE WRITE IS A LIVE NAVIGATION WRITE either way: the viewport it
// places is the one the next frame paints.
//
// Every caller has stopped playback before its write, so the cursor is the
// subject. FOUR CALLERS, re-derived by grep 2026-10-13 (three on 2026-09-25;
// the drop road joined 2026-10-12), each on its hold arm:
//   * THE MARKER NUDGE'S COMMIT TAIL (finish_position_nudge,
//     position_nudge.cpp) passes the PAINTED EXACT x — the focused marker's
//     pre-write frame through painted_x_of_source_frame_on_basis over
//     the displayed map and item_viewport_basis, the painted basis, so the
//     place held is the one
//     the marker visibly occupied even while a viewport job (a resize
//     re-clamp, a target-map publish) has moved the live viewport ahead of
//     the pixels (architect 2026-09-24, strictly as painted).
//   * THE WAVEFORM-LANE PLAYHEAD STEP (GuiInputHandler::
//     run_waveform_lane_playhead_step) passes the cursor's exact x before
//     the step on the PLATE basis the cursor pass paints it with
//     (GuiPaintHandler::plate_viewport_basis; cold, the item basis, then the
//     live viewport by its own contract), so a viewport job in flight
//     cannot move the place held ahead of the pixels (architect 2026-09-24,
//     strictly as painted).
//   * THE UNDO / REDO SINGLETON RESTORE UNDER THE HOLD
//     (Undo::restore_history_entry's visual tail, undo.cpp; architect
//     2026-09-25), a nudge for the camera's purposes, passes the cursor's
//     PRE-RESTORE frame through displayed_x_at on the ITEM basis
//     (item_viewport_basis, the marker nudge's road — that body does not
//     reach the paint handler's plate basis), both read at the restore's head
//     before any of its writes; it calls this only when the restore moved
//     the cursor, in place of the landing owner's Restore.
//   * THE MARKER DROP'S SEAT (Viewport::seat_playhead_on_drop, above;
//     architect 2026-10-12) calls it when a drop under the hold moved the
//     cursor (the P column's lead-in drop) and the walk the drop lands by did
//     not center it (off the working zoom, a file too short for it; at the
//     working zoom the walk's centering supersedes the held column, architect
//     2026-10-29), passing the exact x the cursor painted at before the drop
//     on the item basis, read before the seat, and then re-arms the bit like
//     the restore.
// A held arrow's repeats and a held arrow button's fires reach the first two
// through the same act bodies, so the hold runs at every step. The write
// below changes the camera, so the chokepoint puts the posture out; the nudge
// keeps it across its whole act at its dispatch (AppState::camera_hold's
// first exemption), which is what makes the next step hold too, and the
// restore and the drop re-arm it after (the second and the third). The P
// column's
// unit is a HOP, so there the held subject slides
// the waveform a hop's width per step. NO VIEW TERM:
// in target view on the warp column the marker nudge is refused upstream
// (active_column_authoring_allowed) and never arrives, while the playhead step
// holds there as anywhere. THE ZOOM IS NEVER CHANGED HERE, and every caller
// reaches this body only under the hold, which stands only at the working
// zoom (architect 2026-09-28, the rule at Viewport::land_subject), where q is
// the whole-frame working column and the held offset is a whole number of
// frames — or, on a file too short for it, at `c`'s clamped level, the one
// level the drop's seat reaches this body at (architect 2026-10-29).
void Viewport::hold_subject_column_after_nudge(double prior_x) {
    if (audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const double q = painter_samples_per_pixel(app, audio, area);
    if (q <= 0.0 || area.w <= 0) return;
    // The held place's own offset at the live step, x·q to the whole frame —
    // the start below is rested as asked (Exact), so the subject paints at x
    // to that rounding, 0.5 / q px — and the clamp keeps the last column's
    // offset inside the window.
    const int64_t last_offset = static_cast<int64_t>(
        std::floor(static_cast<double>(area.w - 1) * q));
    const int64_t offset = std::clamp<int64_t>(
        static_cast<int64_t>(std::nearbyint(
            std::clamp(prior_x, 0.0, static_cast<double>(area.w - 1)) * q)),
        0, last_offset);
    const int64_t old_vp = app.viewport_start_sample;
    const int64_t old_origin = app.viewport_lattice_origin;
    app.viewport_start_sample = app.playhead_cursor_sample - offset;
    clamp_viewport_start(app, audio, ViewportLattice::Exact);
    if (app.viewport_start_sample != old_vp ||
        app.viewport_lattice_origin != old_origin)
        finish_discrete_viewport_move();
}

// FOLLOW'S PAGE-IN (AppState::follow): when the target leaves the viewport,
// scroll so it lands THE EDGE MARGIN in from the new view's LEFT edge
// (kViewportEdgeMarginFraction, app_state.h — 5 % since 2026-09-22, the
// architect finding the old 10 % lead "starts way too late"; the class's
// inventory is at that declaration), leaving the rest of the window ahead.
// THREE CALLERS, the one enumeration (send N, 2026-11-15): the launch of the
// view-end entry with FOLLOW dark and the A/B audition's bounded launch, both
// LaunchCamera::PageIn, and the follow scroll's off-screen-left arm
// (follow_scanner below) — the play's own camera is that scroll.
// OFF SCREEN IS AS PAINTED (architect 2026-09-28, displayed_frame_on_screen,
// warp_frame_map_view.h): the page fires once the scanner's painted column
// leaves [0, w), so the half-column before the right edge, where its column
// would round to grid point w and paint no line, already pages. AT THE RIGHT
// WALL the page settles: in the song's last half-column the scanner paints at
// w at every viewport the clamp allows, the page's write clamps back onto the
// wall, the start compares unchanged and nothing repaints. Called at launch too
// (right after the one launch body's seed — launch_playback_window — sets the
// scanner to the launch position), so the same landing rule places an
// offscreen launch position the same margin in from the left edge.
// THE LAUNCH CALL IS THE CALLER'S WORD SINCE 2026-09-18
// (GuiPlaybackLifecycle::LaunchCamera): every GUI road asks for it with
// FOLLOW dark (lit, the follow scroll, follow_scanner below) and starts on
// screen anyway (Space's cursor launch is the one that a pan can have
// carried out of view; a scrub click is a visible column already), while the
// CAR'S play of the trim — the one launch that begins off screen by design —
// asks for no camera with the follow lamp dark, so the camera stays where
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
    const GuiRect area = waveform_area(app);
    const double  q = painter_samples_per_pixel(app, audio, area);
    if (q <= 0.0) return;
    const int64_t target = app.playhead_scanner_active
        ? app.playhead_scanner_sample
        : app.playhead_cursor_sample;
    if (!displayed_frame_on_screen(
            static_cast<double>(target),
            static_cast<double>(app.viewport_start_sample), q,
            app.viewport_lattice_origin, area.w)) {
        const int64_t old_vp = app.viewport_start_sample;
        const int64_t old_origin = app.viewport_lattice_origin;
        app.viewport_start_sample = paged_in_viewport_start(target, visible);
        const bool suspended_before = app.follow_suspended;
        clamp_viewport_start(app, audio, ViewportLattice::Exact);
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
        // frame; the page-in pays it once per move — and the 2026-08-07 flicker
        // ruling (republish_history_lane_now) put a reported flicker onto this path the
        // same way. The tail was spelled here inline until 2026-09-22, minus
        // the top-strip damage the shared tail carries — which a page owes all
        // the same, the flags moving with the viewport. A landing that moved
        // the lattice alone (architect 2026-10-31) is a move too.
        if (app.viewport_start_sample != old_vp ||
            app.viewport_lattice_origin != old_origin)
            finish_discrete_viewport_move();
    }
}

// THE FOLLOW SCROLL (architect 2026-11-15, send N: FOLLOW built as a
// scrolling follow, KEPT on his glass the same day, "the scroll is actually
// much smoother with follow on ... it's very stable, and the waveform behind
// it looks a lot ... more stable, even though it's moving"; "run to center,
// then scroll"; the scroll in place of the page flip, which "is ... more of a
// Reaper design" — his lean to Live's follow, the line standing at the
// center and the waveform moving under it). THE ONE OWNER OF THE RULE:
// THE PICTURE MOVES ONLY SO THAT THE MOVING LINE NEVER PASSES THE WAVEFORM
// AREA'S CENTER. The target t is the scanner's continuous position
// (playhead_scanner_precise, the place its line is drawn from) while the
// scanner is active, else the cursor's frame; every test is on the LIVE
// viewport — its start, its lattice origin, the painted step q
// (painter_samples_per_pixel) and the waveform area's width w — ON SCREEN
// AS PAINTED:
//   (a) t PAINTS OFF SCREEN TO THE LEFT (its column, displayed_column_at,
//       below 0): THE PAGE-IN as it stands, follow_scroll_if_needed above —
//       t lands the edge margin in from the left edge, then runs toward the
//       center under (c) and (b).
//   (b) ELSE, where THE CENTERED START nearbyint(t − (w / 2)·q) — the start
//       that paints t at x = w / 2, the area's exact middle (a column
//       boundary where w is even) — LIES PAST THE LIVE START: THE PLACEMENT,
//       t at the center, through THE PAN'S OWN ROAD, apply_strip_drag_zoom at
//       the standing level with t anchored at w / 2 under
//       ViewportLattice::Keep, the test and the placement one arithmetic. The
//       start snaps onto the camera's CURRENT lattice (a pan's rule,
//       AppState::viewport_lattice_origin: a pure index shift), so the
//       picture translates by whole device px, a following play keeps the
//       origin the view stood on (after `c`, the centring's own) and the
//       rest after its Stop is that lattice's picture; the line, painted at
//       its exact place as
//       everywhere, stands within half a px of the center plus the
//       whole-frame rounding of the requested start (nearbyint, before the
//       lattice snap), 0.5 / q px more at most, away from the walls. The walls clamp
//       as for any pan: at the song's end the picture stops at the right wall
//       and the line runs on to the edge. A centered start that snaps back
//       onto the standing one moves nothing and damages nothing (the
//       applier's true-no-op skip); every moved frame takes the applier's
//       synchronous rebuild (through the kept bars, WaveformKeptBars,
//       render.h) and its full waveform-area damage, and NO PREDICTOR RESYNC
//       (`final` false): a scroll frame is a pan frame the play makes, never
//       a discrete event. The snap rounds to the nearest lattice point and
//       the live start is one, so a centered start past it never lands left
//       of it: the rule only ever moves the picture forward, except (a).
//   (c) ELSE NOTHING: a line left of the center runs toward it over a still
//       picture.
// THE SCROLL IS FOLLOW'S OWN CAMERA MOVE AND SUSPENDS NOTHING, the page's
// rule (architect 2026-09-23, above): the chokepoint inside the applier
// suspends follow at a changed camera, and the suspension is restored behind
// it (AppState::follow_suspended); the HOLD posture stays out. Degenerate
// geometry (no audio, nothing visible, q or w not positive) moves nothing.
// THREE CALLERS: the pre-paint advance on every frame of THE FOLLOWING PLAY
// (main.cpp's following_play; paused under a live aim, so the first frame
// after the lift places the line at the center in one move), the launch's
// LaunchCamera::Follow (GuiPlaybackLifecycle::launch_playback_window: a
// start right of the center is centered at once) and the FOLLOW lamp's lit
// edge during a play (GuiPlaybackLifecycle::toggle_follow).
void Viewport::follow_scanner() {
    if (audio.total_frames() <= 0) return;
    if (samples_visible(app, audio) <= 0) return;
    const GuiRect area = waveform_area(app);
    if (area.w <= 0) return;
    const double q = painter_samples_per_pixel(app, audio, area);
    if (q <= 0.0) return;
    const double t = app.playhead_scanner_active
        ? app.playhead_scanner_precise
        : static_cast<double>(app.playhead_cursor_sample);
    // (a) Off screen to the left: the page-in.
    if (displayed_column_at(t, static_cast<double>(app.viewport_start_sample),
                            q, app.viewport_lattice_origin) < 0) {
        follow_scroll_if_needed();
        return;
    }
    // (b) Right of the center: t placed at the center, the pan's road.
    const double center_x = static_cast<double>(area.w) / 2.0;
    const int64_t centered_start =
        static_cast<int64_t>(std::nearbyint(t - center_x * q));
    if (centered_start <= app.viewport_start_sample) return;   // (c)
    const bool suspended_before = app.follow_suspended;
    apply_strip_drag_zoom(app.zoom_level, t, center_x, /*final=*/false,
                          ViewportLattice::Keep);
    app.follow_suspended = suspended_before;
}

// THE LANDING OWNER (architect 2026-09-24): ONE camera for every act that
// walks or restores a subject onto the screen, over an ACTIVE-DOMAIN range
// [lo, hi] (lo == hi for a single marker), its answers chosen by the caller's
// LandingKind. It reads the zoom, and that read is legal because it happens at
// a discrete act — a keystroke does not pop the picture the way a
// zoom-derived picture did during a zoom gesture (the rule at nudge_camera's
// neighbour, app_state.h). THE ZOOM IS NEVER WRITTEN HERE.
//
// WALK — the Tab walk and the marker drop's landing (architect 2026-10-29,
// the drop lands as Tab does), always a single frame (the paired march's
// steps came here until 2026-09-26, when each took bare `c`'s act instead):
//   * AT THE WORKING ZOOM, AS PAINTED (architect 2026-09-28: the painted
//     step q EQUALS the working column, audio.working_column(); `c`'s level
//     2.0 paints q = column exactly, and a rest a hair either side of it
//     that rounds to the same sixteenth-frame grid point paints the same
//     picture and walks like it) the subject is CENTRED, ON SCREEN OR NOT, the centring
//     body's own placement: the walk always moves one way and every landing
//     frames alike, so no half of the screen is skipped;
//   * AT EVERY OTHER ZOOM, FINER OR COARSER, an on-screen subject moves
//     NOTHING and an off-screen one is PAGED IN, lo landing the edge margin
//     in from the LEFT edge in both directions — follow's own placement
//     (paged_in_viewport_start). A finer zoom is reached only by accident;
//     `c` is the repair (the working zoom, centred).
// RESTORE — undo / redo, a singleton (lo == hi) or a group's [earliest,
// latest]; a restore is a non-linear jump, and centring is the least
// prejudicial way of framing one:
//   * WHOLLY ON SCREEN (lo and hi each painted in the columns [0, w) of
//     the live viewport, displayed_frame_on_screen — the walk's on-screen
//     test is the same one): NOTHING MOVES, at every zoom;
//   * CANNOT FIT: the range is wider than 1 − 2 × the edge margin of the
//     PAINTED window at the live zoom (samples_visible, W·q on the
//     sixteenth-frame grid — the span the centring below places the subject
//     in and the geometry fit_zoom_level solves in) — the room the span
//     framer's margin arm (frame_span_into_view, input_handler.cpp) leaves a
//     span. One geometry on both sides guarantees the caller's zoom-out never
//     zooms in: a refused range's margined span exceeds W·q = W·n/16, so the
//     framer's fit takes at least n + 1 sixteenths, above the live level's
//     spp (within half a sixteenth of q); and an accepted range keeps the
//     painted margin. RETURNS FALSE HAVING WRITTEN NOTHING: the zoom-out fit
//     is the caller's;
//   * OFF SCREEN AND FITS: CENTRED on its midpoint AT EVERY ZOOM, coarse
//     included; never a page-in.
// THE HOLD POSTURE (AppState::camera_hold) IS ARMED, OF THIS OWNER'S ANSWERS,
// BY THE WALK'S CENTRING ALONE (architect 2026-09-24), after the chokepoint — a
// walked marker centred at the working zoom is expected to hold its column,
// and it arms even where a wall keeps it off the centre. Since the walk
// centres only at the working zoom, every other act that arms (bare `c`, the
// paired march, the A/B audition's centrings, Ctrl+J) writes that zoom, and
// the tab switch's arrival derivation (hold_derived_on_arrival above,
// architect 2026-09-29) arms only at the working zoom as painted, while every zoom
// write that moves the level changes the camera at the chokepoint and puts
// the bit out, THE HOLD STANDS ONLY AT THE WORKING ZOOM (architect
// 2026-09-28). THE RESTORE NEVER ARMS (its singleton centring
// included) and never clears on its own: its centring of an off-screen
// subject clears the bit at the chokepoint like any camera move, and its
// no-move answer leaves it as it stands. (The singleton restore reaches this
// owner with the hold dark, or having crossed the tab or the audio view —
// the tab switch having derived the bit on arrival, the S/T switch having put
// it out (architect 2026-09-29); under a hold in the view it started in it
// takes the nudge's
// hold_subject_column_after_nudge instead and re-arms the bit itself after
// that body, as the nudge's dispatch keeps it — the restore's own act, not
// this owner's. The group restore puts the bit out at its own arm before it
// asks. Both at undo.cpp, architect 2026-09-25.) WHY: an automatic arm on arrival at
// the centre would freeze the viewport under a run of nudges that happened to
// reach the middle; `c` then nudging means "I'm looking for a place to drop a
// marker" and wants the hold, `c` then panning means "I want the working zoom
// but my own viewport" and the pan clears it (no lamp shows which posture
// stands, architect 2026-10-05: AppState::camera_hold). (A tab
// switch is the one arrival that derives it, being a discrete arrival at a
// tab the user last left, not a run of nudges; architect 2026-09-29,
// hold_derived_on_arrival.) A walk's page-in passes the chokepoint, which
// puts the hold out as at any page; a
// no-move answer leaves the posture as it stands. Degenerate geometry (no
// strip width, no sample rate, nothing visible) writes nothing and answers true. clamp_viewport_start
// owns the song's two ends and RESTS THE START AS ASKED (ViewportLattice::
// Exact, architect 2026-11-15, the no-grid arc: the centred midpoint stands
// at the window's exact middle and the paged-in subject exactly the edge
// margin in, each to the start's whole frame); the changed path takes the
// discrete move's tail. AT THE RIGHT WALL a subject in the song's last half-column
// paints at grid point w at every viewport the clamp allows, so it never
// counts as on screen there; its landing clamps onto the wall, and once the
// camera rests there the start compares unchanged and nothing repaints.
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
//   * THE WALK CENTRING AT A ZOOM FINER THAN THE WORKING ONE (2026-09-24 to
//     2026-09-29): it centred, and armed the hold, at every finer zoom; it
//     centres at the working zoom alone, so the hold stands only there
//     (architect 2026-09-28).
//
// ITS READERS, re-grepped 2026-10-29:
//   * WALK: jump_playhead_to_focused_marker's MarkerLandingFrame::Land arm
//     (input_handler.cpp — bare Tab / Shift+Tab through
//     cycle_marker_focus), cycle_history_diff_flag_focus's Land arm
//     (input_key_dispatch.cpp — the `h` view's Tab), and the marker drop's
//     seat (seat_playhead_on_drop, above — every drop body, architect
//     2026-10-29); each lands the cursor it has just seated (lo == hi), so
//     the verdict is dropped;
//   * RESTORE: restore_history_entry's singleton arm (undo.cpp) on the cursor
//     just landed, WITH THE HOLD DARK or ACROSS A TAB OR AUDIO-VIEW SWITCH
//     (otherwise the arm holds the column instead, 2026-09-25; a crossed
//     restore never does, 2026-09-29), and its group arm on the restored
//     markers' [earliest, latest] extent — THE ONE CALLER THAT CAN MEET THE FALSE VERDICT, which
//     runs the span framer's margin arm on it.
// NOT READERS, by ruling: bare `c`, the paired march (live and `h`, each step
// running `c`'s act, 2026-09-26), Ctrl+J and the A/B audition (its three
// centrings `c`'s act too, 2026-09-29), which
// centre unconditionally (center_viewport_on_playhead after the working
// zoom); follow's camera during playback (follow_scanner and its page-in,
// follow_scroll_if_needed, which read the scanner and keep follow's
// suspension their own); the nudge's
// cameras.
bool Viewport::land_subject(int64_t lo, int64_t hi, LandingKind kind) {
    if (hi < lo) std::swap(lo, hi);   // defensive; the callers pass in order
    const int     W  = waveform_area(app).w;
    const int     sr = audio.sample_rate();
    const int64_t visible = samples_visible(app, audio);
    if (W <= 0 || sr <= 0 || visible <= 0) return true;
    const double q = painter_samples_per_pixel(app, audio, waveform_area(app));
    if (q <= 0.0) return true;
    // On screen AS PAINTED, both ends (displayed_frame_on_screen).
    const double vp = static_cast<double>(app.viewport_start_sample);
    const bool on_screen =
        displayed_frame_on_screen(static_cast<double>(lo), vp, q,
                                  app.viewport_lattice_origin, W) &&
        displayed_frame_on_screen(static_cast<double>(hi), vp, q,
                                  app.viewport_lattice_origin, W);
    // The working-zoom read, inline by ruling: the walk's one zoom term. AS
    // PAINTED, q against the working column, not the level against 2.0: a
    // rest a hair either side of level 2 (spp under a 1/32 frame from the
    // column, over or under) paints the working picture exactly and walks
    // like it. AN EXACT EQUALITY IS
    // LEGITIMATE HERE: q is on the sixteenth-frame grid (n/16, exact in a
    // double) and the column is whole frames, so the painted working picture
    // compares equal and every other picture, finer or coarser, does not.
    const bool at_working = q == static_cast<double>(audio.working_column());
    bool centre = false;
    switch (kind) {
        case LandingKind::Walk:
            if (!at_working && on_screen) return true;
            centre = at_working;
            break;
        case LandingKind::Restore:
            if (on_screen) return true;
            centre = true;
            break;
    }
    const double room = (1.0 - 2.0 * kViewportEdgeMarginFraction) *
                        static_cast<double>(visible);
    if (static_cast<double>(hi - lo) > room) return false;
    const int64_t old_vp = app.viewport_start_sample;
    const int64_t old_origin = app.viewport_lattice_origin;
    app.viewport_start_sample = centre
        ? centred_viewport_start(lo + (hi - lo) / 2, visible)
        : paged_in_viewport_start(lo, visible);
    clamp_viewport_start(app, audio, ViewportLattice::Exact);
    if (kind == LandingKind::Walk && centre) app.camera_hold = true;
    if (app.viewport_start_sample != old_vp ||
        app.viewport_lattice_origin != old_origin)
        finish_discrete_viewport_move();
    return true;
}
