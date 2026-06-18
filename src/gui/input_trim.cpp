#include "input_handler.h"

#include "frame_map_view.h"
#include "time_format.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <utility>

// b / e key handlers. Both share the same shape: the playhead's current
// sample frame is the candidate. Re-press at the same frame as the
// existing trim toggles it off. A candidate equal-frame to the opposite
// trim refuses (would collapse the trim region). A candidate that lands
// past the opposite trim places this bound and disables the opposite
// bound (no swap). Otherwise a simple set. Project-level: reads and writes
// the project trim fields. Each mutation pushes a settings-undo entry so
// b/e/u are reversible.
//
// Side-parameterized to share the body between Begin and End. The
// load-bearing asymmetry is the direction that counts as "past the other":
// Begin is past when the candidate is after trim_end, End is past when the
// candidate is before trim_begin. The past-the-other outcome keeps this
// bound at the candidate and clears the opposite bound (has / seconds /
// selected), rather than swapping the two.
void GuiInputHandler::handle_trim_set_at_playhead(TrimSide side) {
    const int sr = audio.sample_rate();
    if (audio.total_frames() <= 0 || sr <= 0) return;
    const double sr_d = static_cast<double>(sr);
    // Target view: playhead is target-domain; the trim store is
    // source-domain. Inverse-translate at the boundary so the
    // downstream toggle / collision / swap logic compares against the
    // source-frame domain the trim store lives in.
    int64_t cand_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);
    // Snap the candidate at derivation, then recompute cand_frame from the
    // snapped value so the toggle-off compare and the opposite-bound
    // collision compare (both frame-based) match what will be stored.
    const double cand_seconds =
        snap_to_timestamp_grid(static_cast<double>(cand_frame) / sr_d);
    cand_frame = static_cast<int64_t>(std::nearbyint(cand_seconds * sr_d));

    bool&   this_has      = (side == TrimSide::Begin) ? app.trim.has_begin     : app.trim.has_end;
    double& this_seconds  = (side == TrimSide::Begin) ? app.trim.begin_seconds : app.trim.end_seconds;
    bool&   this_sel      = (side == TrimSide::Begin) ? app.trim_begin_selected : app.trim_end_selected;
    bool&   other_has     = (side == TrimSide::Begin) ? app.trim.has_end       : app.trim.has_begin;
    double& other_seconds = (side == TrimSide::Begin) ? app.trim.end_seconds   : app.trim.begin_seconds;
    bool&   other_sel     = (side == TrimSide::Begin) ? app.trim_end_selected  : app.trim_begin_selected;
    const char letter     = (side == TrimSide::Begin) ? 'b' : 'e';

    // Toggle-off: same frame as the existing this-side trim.
    if (this_has) {
        const int64_t cur_frame = static_cast<int64_t>(
            std::nearbyint(this_seconds * sr_d));
        if (cur_frame == cand_frame) {
            SettingsSnapshot pre = capture_current_settings(app);
            this_has     = false;
            this_seconds = 0.0;
            this_sel     = false;
            undo.push_settings_undo(std::move(pre));
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            target_render.trigger();
            return;
        }
    }

    // Equal-frame collision with the opposite-side trim refuses (would
    // collapse the trim region). Past-the-other places this bound at the
    // candidate and disables the opposite bound.
    if (other_has) {
        const int64_t other_frame = static_cast<int64_t>(
            std::nearbyint(other_seconds * sr_d));
        if (other_frame == cand_frame) {
            std::fprintf(stderr,
                "warptempo_gui: %c refused: would collapse trim region\n",
                letter);
            return;
        }
        const bool cand_is_past_other = (side == TrimSide::Begin)
            ? (cand_frame > other_frame)
            : (cand_frame < other_frame);
        if (cand_is_past_other) {
            // Past-the-other no longer swaps. Place this bound at the
            // candidate and disable the opposite bound: setting Begin past
            // End drops End; setting End before Begin drops Begin. The
            // surviving single bound leaves a half-open trim region.
            SettingsSnapshot pre = capture_current_settings(app);
            this_has      = true;
            this_seconds  = cand_seconds;
            other_has     = false;
            other_seconds = 0.0;
            other_sel     = false;
            undo.push_settings_undo(std::move(pre));
            viewport.invalidate_waveform_area();
            viewport.invalidate_timestamp_area();
            target_render.trigger();
            stop_playback_if_scanner_out_of_trim();
            return;
        }
    }

    SettingsSnapshot pre = capture_current_settings(app);
    this_has     = true;
    this_seconds = cand_seconds;
    undo.push_settings_undo(std::move(pre));
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
    stop_playback_if_scanner_out_of_trim();
}

// After a trim set has resolved the new region, stop playback if the
// scanner has been left outside it. The cursor sits on the just-set
// bound, so only the moving scanner can fall out; widening or removing a
// bound can never push it out, so this is called only from the region-
// narrowing outcomes below. No-op when the scanner is inactive (stopped)
// or still in bounds, so the common case keeps playing like a set marker.
void GuiInputHandler::stop_playback_if_scanner_out_of_trim() {
    if (!app.playhead_scanner_active) return;
    const int64_t s = app.playhead_scanner_sample;
    if (s < viewport.trim_begin_sample() ||
        s >= viewport.trim_end_sample()) {
        playback_lifecycle.stop_playback_if_playing();
    }
}

void GuiInputHandler::handle_trim_set_begin_at_playhead() {
    handle_trim_set_at_playhead(TrimSide::Begin);
}

void GuiInputHandler::handle_trim_set_end_at_playhead() {
    handle_trim_set_at_playhead(TrimSide::End);
}

void GuiInputHandler::handle_trim_unset(TrimSide side) {
    bool&   this_has     = (side == TrimSide::Begin) ? app.trim.has_begin     : app.trim.has_end;
    double& this_seconds = (side == TrimSide::Begin) ? app.trim.begin_seconds : app.trim.end_seconds;
    bool&   this_sel     = (side == TrimSide::Begin) ? app.trim_begin_selected : app.trim_end_selected;
    if (!this_has) return;
    SettingsSnapshot pre = capture_current_settings(app);
    this_has     = false;
    this_seconds = 0.0;
    this_sel     = false;  // an unset bound can't stay selected
    undo.push_settings_undo(std::move(pre));
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
    target_render.trigger();
}

void GuiInputHandler::handle_trim_unset_begin() {
    handle_trim_unset(TrimSide::Begin);
}

void GuiInputHandler::handle_trim_unset_end() {
    handle_trim_unset(TrimSide::End);
}

// --- Trim boundary mouse gestures ---------------------------------------

void GuiInputHandler::select_trim_boundary(TrimHit which, bool additive) {
    if (which == TrimHit::None) return;
    bool& this_sel  = (which == TrimHit::Begin) ? app.trim_begin_selected
                                                : app.trim_end_selected;
    bool& other_sel = (which == TrimHit::Begin) ? app.trim_end_selected
                                                : app.trim_begin_selected;
    if (additive) {
        // Toggle this bound's membership; leave the other bound as-is.
        this_sel = !this_sel;
    } else {
        // Single-select within the trim group: this bound on, other off.
        this_sel  = true;
        other_sel = false;
        // A fresh sole selection in the trim group drops marker selection —
        // orthogonal groups, but a single-select in one clears the other
        // (the symmetric counterpart of set_single_selection clearing trim).
        if (!app.selected_markers.empty() || app.last_selected_marker != -1) {
            app.selected_markers.clear();
            app.last_selected_marker = -1;
            viewport.invalidate_top_strip();
        }
    }
    app.last_sel_group = LastSelGroup::Trim;
    viewport.invalidate_waveform_area();
}

bool GuiInputHandler::trim_mouse_x_to_source_seconds(int mouse_x,
                                                     double& out_seconds) {
    const int sr = audio.sample_rate();
    if (sr <= 0 || audio.total_frames() <= 0) return false;
    const double sr_d = static_cast<double>(sr);
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return false;

    int rel = mouse_x - area.x;
    if (rel < 0) rel = 0;
    if (rel >= area.w) rel = area.w - 1;
    const int64_t domain_frame = app.viewport_start_sample +
        static_cast<int64_t>(std::nearbyint(rel * spp));

    // Target view: the cursor column is an active-domain frame; the trim
    // store is source-domain. Inverse-translate at the boundary, mirroring
    // handle_trim_set_at_playhead.
    const int64_t src_frame =
        active_domain_to_source_frame(app, audio, domain_frame);
    out_seconds = static_cast<double>(src_frame) / sr_d;
    return true;
}

void GuiInputHandler::begin_trim_drag(TrimHit which, int mouse_x) {
    if (which == TrimHit::None) return;
    const bool is_begin = (which == TrimHit::Begin);
    if (is_begin ? !app.trim.has_begin : !app.trim.has_end) {
        return;
    }
    app.trim_drag.active       = true;
    app.trim_drag.is_begin     = is_begin;
    app.trim_drag.moved        = false;
    app.trim_drag.orig_seconds = is_begin ? app.trim.begin_seconds
                                          : app.trim.end_seconds;
    // Grab anchor: the press position in source-domain seconds. Motion moves
    // the bound by the cursor's displacement from here, so it tracks the grab
    // point with no snap (mirrors begin_drag's anchor_mouse_time_seconds).
    // A bad conversion leaves anchor_seconds at 0; harmless since the same
    // unusable state makes update_trim_drag early-return too.
    double anchor = 0.0;
    if (trim_mouse_x_to_source_seconds(mouse_x, anchor))
        app.trim_drag.anchor_seconds = anchor;
    app.trim_drag.pre          = capture_current_settings(app);
    app.last_sel_group         = LastSelGroup::Trim;
}

void GuiInputHandler::update_trim_drag(int mouse_x) {
    if (!app.trim_drag.active) return;
    const int sr = audio.sample_rate();
    if (sr <= 0 || audio.total_frames() <= 0) return;
    const double sr_d = static_cast<double>(sr);
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;

    // Anchor-relative motion: the dragged bound moves by the cursor's
    // displacement from the grab point, not to the absolute cursor column.
    // cursor_seconds is converted identically to the begin-drag anchor, so
    // the bound stays the same distance under the cursor for the whole drag.
    double cursor_seconds = 0.0;
    if (!trim_mouse_x_to_source_seconds(mouse_x, cursor_seconds)) return;
    const double delta_seconds = cursor_seconds - app.trim_drag.anchor_seconds;

    const int64_t total = static_cast<int64_t>(audio.total_frames());

    // Single-bound: pre-drag frame plus the anchor-relative delta.
    const int64_t orig_f = static_cast<int64_t>(
        std::nearbyint(app.trim_drag.orig_seconds * sr_d));
    int64_t src_frame = orig_f +
        static_cast<int64_t>(std::nearbyint(delta_seconds * sr_d));
    if (src_frame < 0) src_frame = 0;
    if (src_frame > total) src_frame = total;

    // Clamp against the other bound: the dragged bound
    // stops kMarkerHitHalfPx pixels (at the current zoom) short of the other
    // bound, the same eps the warp drag uses (warpmarkers_ops.cpp ~429), so
    // the b/e stems never reach visual coincidence — matching the tightest
    // gap two regular marker stems can hold. This replaces the former
    // 1-frame clamp, which was sub-pixel at any normal zoom. This is the
    // trim-internal eps (begin vs end only), orthogonal to warp/phase eps.
    const int64_t eps_frames = static_cast<int64_t>(
        std::nearbyint(static_cast<double>(kMarkerHitHalfPx) * spp));
    if (app.trim_drag.is_begin) {
        if (app.trim.has_end) {
            const int64_t end_f = static_cast<int64_t>(
                std::nearbyint(app.trim.end_seconds * sr_d));
            if (src_frame > end_f - eps_frames) src_frame = end_f - eps_frames;
            if (src_frame < 0) src_frame = 0;
        }
    } else {
        if (app.trim.has_begin) {
            const int64_t begin_f = static_cast<int64_t>(
                std::nearbyint(app.trim.begin_seconds * sr_d));
            if (src_frame < begin_f + eps_frames) src_frame = begin_f + eps_frames;
            if (src_frame > total) src_frame = total;
        }
    }

    // Single-bound drag: snap the derived seconds the same way the group
    // branch above does, so the change-detection compare and the store
    // both operate on the grid value the bound will persist at.
    const double new_seconds =
        snap_to_timestamp_grid(static_cast<double>(src_frame) / sr_d);
    double& field = app.trim_drag.is_begin ? app.trim.begin_seconds
                                           : app.trim.end_seconds;
    if (field != new_seconds) {
        const bool first_motion = !app.trim_drag.moved;
        field = new_seconds;
        app.trim_drag.moved = true;
        // First-motion selection collapse: a real drag focuses the whole
        // selection on the dragged bound. Delegated to select_trim_boundary
        // (non-additive) — the same helper a trim click uses — so the rule
        // (select the dragged bound, drop the opposite bound AND any
        // warp/phase-reset marker selection, make Trim the active group)
        // lives in one place. Motion-gated so a Ctrl+click without motion is
        // left to commit_trim_drag's no-motion toggle branch.
        if (first_motion) {
            select_trim_boundary(
                app.trim_drag.is_begin ? TrimHit::Begin : TrimHit::End,
                /*additive=*/false);
        }
        // Track the playhead on the dragged bound for the whole drag,
        // mirroring the warp marker-drag tracking in the motion handler
        // (right after apply_drag_motion): set app.playhead_cursor_sample
        // DIRECTLY rather than via move_playhead_to, so the viewport is
        // deliberately not followed — the user pans manually if the drag
        // runs past the edge. move_playhead_to would scroll when an
        // off-center grab pushes the bound a few pixels past the visible
        // edge; the marker drag never scrolls, and symmetry is the point.
        // Trim is a render-time cut and is NOT in build_target_view_frame_map,
        // so the bound carries no deformation: new_seconds (source-domain)
        // maps straight to the playhead, inverse-translated to target-domain
        // in target view. No predictor resync and no scanner-sample sync,
        // both matching the marker-drag block — trim drag stopped playback at
        // begin, so the scanner is inactive and a play reseeks from the
        // cursor. The invalidate_waveform_area below repaints the playhead
        // columns along with the moved trim shading.
        const int64_t src_sample = static_cast<int64_t>(
            std::nearbyint(new_seconds * sr_d));
        const int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
        app.playhead_cursor_sample = sample;
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
    }
}

void GuiInputHandler::commit_trim_drag() {
    if (!app.trim_drag.active) return;
    if (app.trim_drag.moved) {
        undo.push_settings_undo(std::move(app.trim_drag.pre));
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    } else {
        // Ctrl+press with no motion is a Ctrl+click: toggle the boundary's
        // selection (additive — coexists with marker selection).
        const TrimHit which = app.trim_drag.is_begin ? TrimHit::Begin
                                                      : TrimHit::End;
        select_trim_boundary(which, /*additive=*/true);
    }
    app.trim_drag = TrimDragState{};
}

void GuiInputHandler::delete_selected_trim() {
    if (app.trim_begin_selected && app.trim.has_begin) {
        handle_trim_unset(TrimSide::Begin);
    }
    if (app.trim_end_selected && app.trim.has_end) {
        handle_trim_unset(TrimSide::End);
    }
    app.trim_begin_selected = false;
    app.trim_end_selected   = false;
}

void GuiInputHandler::handle_trim_boundary_press(TrimHit which, bool ctrl,
                                                 bool shift, int mouse_x) {
    if (which == TrimHit::None) return;
    if (ctrl) {
        // Read-only refuses the drag-begin so app.trim_drag.active never
        // enters flight; motion / release / Escape all short-circuit on it.
        if (active_view_state(app).read_only) return;
        begin_trim_drag(which, mouse_x);
        return;
    }
    select_trim_boundary(which, /*additive=*/shift);
    // Mirror the marker flag/stem click: a plain or Shift click on a trim
    // boundary moves the playhead cursor to that boundary, so trim flags
    // navigate exactly like marker flags. The Ctrl branch above is a
    // reposition-drag grab and intentionally does not move the playhead,
    // matching the Ctrl+marker reposition. The hit-test that routed here only
    // fires when the boundary exists, so trim_begin/end_seconds is set.
    const double sec = (which == TrimHit::Begin) ? app.trim.begin_seconds
                                                 : app.trim.end_seconds;
    const int sr = audio.sample_rate();
    const int64_t src_sample =
        static_cast<int64_t>(std::nearbyint(sec * static_cast<double>(sr)));
    const int64_t sample = source_frame_to_active_domain(app, audio, src_sample);
    viewport.move_playhead_to(sample);
}
