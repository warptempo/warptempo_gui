#include "magnification_level_propagate.h"

#include "active_views.h"
#include "audio.h"
#include "input_handler.h"
#include "magnification_level_clipboard.h"
#include "magnificationlevelmarkers.h"
#include "phase_reset_propagate.h"   // format_domain_timestamp (the family's one register)
#include "warp_frame_map_view.h"     // the playhead's source frame

#include <set>

#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// THE MAGNIFICATION LEVEL PROPAGATE — copy a group of magnification level
// markers and lay the same shape down again from the playhead. The
// whole model, and every way it parts from the phase reset propagate beside
// it, is stated once at the header; the comments below carry only what each
// body does.

void MagnificationLevelPropagate::copy_from_selection() {
    const auto& lv = app.magnificationlevelmarkers.markers();
    const int n = static_cast<int>(lv.size());
    // THE BELT, silent and with no card: the dispatch arm's two gates (the
    // column, and a standing selection) are spelled and carded there, so a
    // card here would be the second for one press, and a stale index is
    // something the selection layer cannot produce. An error arm exists iff a
    // producer exists (validation_topology.md).
    if (app.selected_markers.empty()) return;

    // std::set is ascending and the store is frame-ascending, so the walk
    // comes out in frame order and the FIRST live member is the one every
    // offset is measured from.
    std::vector<MagnificationLevelClipboardPlacement> placements;
    placements.reserve(app.selected_markers.size());
    int64_t origin_frame = 0;
    for (int i : app.selected_markers) {
        if (i < 0 || i >= n) continue;
        const auto& m = lv[static_cast<size_t>(i)];
        if (placements.empty()) origin_frame = m.time_frame;
        MagnificationLevelClipboardPlacement p;
        // A WHOLE-FRAME DISTANCE between two authored positions: the
        // subtraction stays in the integer domain from end to end, so nothing
        // on this road ever rounds and snap_authored_frame (app_state.h, the
        // one double-to-authored conversion chokepoint) has nothing to do
        // here — this act never makes a double.
        p.offset_frames = m.time_frame - origin_frame;
        p.level         = m.level;
        p.disabled      = m.disabled;
        placements.push_back(p);
    }
    if (placements.empty()) return;

    app.magnification_level_clipboard.set(std::move(placements));
}

void MagnificationLevelPropagate::paste_apply() {
    // THE BELT, the copy's for the copy's reason: the dispatch arm carries and
    // cards this chord's two gates (the column, and a non-empty clipboard).
    if (app.magnification_level_clipboard.empty()) return;
    if (audio.sample_rate() <= 0) return;

    // THE SELECTION IS SPENT (architect 2026-09-12, the lamps resolved by use
    // case), the sibling's line for the sibling's reason: past the belts and
    // ahead of the run's own report (selection_consumed, app_state.h, where
    // the class and its inventory live). The write is the LAMP's and not the
    // membership's — which the landing below replaces with what this paste
    // created.
    selection_consumed(app);

    // THE ANCHOR IS THE PLAYHEAD (architect 2026-09-19): the first placement
    // lands on it and every later one at the playhead plus its own offset.
    // The column is source view only, so the cursor IS the source frame and
    // the inverse below is the identity; it is asked unconditionally anyway,
    // the drop body's own shape (drop_magnification_level_at_playhead).
    const int64_t anchor_frame =
        active_domain_to_source_frame(app, audio, app.playhead_cursor_sample);

    // THE WALL, total - 1 — the marker EOF wall all three columns share. Every
    // offset is >= 0 and the playhead rests in [0, total-1] by every writer's
    // clamp, so only the upper wall can ever be met; the run STOPS at the
    // first landing past it rather than clamping, because a clamped marker
    // would silently change the shape the copy captured.
    const int64_t marker_wall = audio.total_frames() - 1;
    const auto& placements = app.magnification_level_clipboard.placements();

    std::vector<GuiMagnificationLevelMarker> pre_state =
        app.magnificationlevelmarkers.markers();

    // Track the exact final index of every marker this paste materializes so
    // the landing can select precisely them: a later insert at index k shifts
    // every earlier recorded index >= k by one, so adjust as we go rather than
    // re-resolving by time (coincident markers are legal here and would be
    // ambiguous by value — see the landing rule below).
    std::vector<int> created_indices;
    std::string stop_message;
    for (const auto& p : placements) {
        const int64_t frame = anchor_frame + p.offset_frames;
        if (frame > marker_wall) {
            // PAST THE SONG END: paste what fits and report where it stopped
            // (architect 2026-09-19), in the family's own register
            // (format_domain_timestamp, phase_reset_propagate.h — one spelling
            // for both propagates' stop reports).
            stop_message = "Stopped at " +
                format_domain_timestamp(static_cast<double>(frame), app, audio) +
                " (past the end of the piece)";
            break;
        }
        GuiMagnificationLevelMarker nm;
        nm.time_frame = frame;
        nm.level      = p.level;
        nm.disabled   = p.disabled;
        const int new_idx =
            app.magnificationlevelmarkers.insert_marker(std::move(nm));
        for (int& ci : created_indices)
            if (ci >= new_idx) ++ci;
        created_indices.push_back(new_idx);
    }

    // THE FIRST PLACEMENT ALWAYS LANDS, so this act has no produced-nothing
    // arm and the belt is an assert rather than a card (an error arm exists
    // iff a producer exists, validation_topology.md). Every link: the
    // clipboard has ONE writer, copy_from_selection above, which measures
    // every offset from the FIRST captured marker, so placements[0]
    // .offset_frames is 0 by construction and placements is non-empty past
    // the belt at the head; the column lives in source view alone
    // (switch_active_markers_view_to refuses 'M' anywhere else), so the
    // context is the identity and anchor_frame IS the playhead;
    // clamp_playhead_to_live_domain holds the playhead inside
    // [0, total - 1] on every writer's road; and a zero-frame source is
    // refused at the wav layout owner, so total - 1 is a real frame. The
    // first landing is therefore the in-domain playhead, which is at or
    // under the wall — only a LATER placement can break the loop, and by
    // then something is already inserted.
    assert(!created_indices.empty());

    // A COINCIDENT LANDING SIMPLY PASTES (architect 2026-09-19): nothing
    // refuses and nothing de-duplicates, the standing coincidence posture.
    // insert_marker keeps the list sorted, and a run of 2+ ENABLED rows at one
    // frame reads as the neutral level 0 (magnificationlevelmarkers.h), which
    // the red cue then says.
    //
    // THE IDENTITY HINT ON THE LIVE SIDE (the contract and the producer
    // enumeration are at restore_touched_indices, app_state.h), the sibling's
    // hint for a reason this act makes MORE load-bearing: created_indices
    // names every materialized marker's FINAL index, so a REDO re-selects
    // exactly what the paste selected, coincident groups being ambiguous by
    // frame. The SNAPSHOT side stays empty — this act removes no row, so an
    // undo puts none back.
    std::vector<int> touched_live = created_indices;
    undo.push_undo_magnification_level(std::move(pre_state),
                                       /*touched_snapshot=*/{},
                                       std::move(touched_live));
    undo.recompute_dirty();
    viewport.invalidate_waveform_area();
    // NO PREVIEW (the header's rule: a level reaches no render input); the
    // flags are repainted by the landing's unconditional synchronous rebuild
    // below, which this arm always reaches.

    // Partial paste: what fitted is pasted AND the stop is reported. A run
    // where everything fitted leaves stop_message empty and shows nothing.
    if (!stop_message.empty()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             std::move(stop_message));
    }

    land_paste_in_source_view(
        std::set<int>(created_indices.begin(), created_indices.end()));
}

// The order is the sibling's (PhaseResetPropagate::land_paste_in_target_view,
// whose head comment argues it): audio-view switch first, then the column
// writer, then the selection set with its land, then the SYNCHRONOUS rebuild
// last. Both switches are no-ops on every live road — the act is an S+M act at
// the press — and they stand because this tail is where the paste NAMES the
// view it ends in. THE KICK IS THE FLAG CACHE'S REPAINT: the store this paste
// wrote moves no pixel of the waveform (the header's rule).
void MagnificationLevelPropagate::land_paste_in_source_view(
        const std::set<int>& created) {
    // The precondition the header states, held by the paste: this tail
    // focuses and lands on the first created marker, so an empty set would
    // have nothing to name.
    assert(!created.empty());
    if (input) input->switch_active_audio_view_to('S');
    active_views.switch_active_markers_view_to('M');
    // FIRST created marker as the focus — a PROGRAMMATIC group selection, the
    // earliest focused as undo/redo's touched-set restore focuses it — through
    // the selection chokepoint, and then the lane's own land (the marker lane
    // owns the playhead; land_playhead_on_marker's rule). FOR THE PASTE THE
    // LAND MOVES NOTHING, the playhead already standing on the first created
    // marker's frame, and it runs anyway because it is the lane's movement
    // owner and this act has always gone through it.
    selection.replace_selection(created, *created.begin());
    land_playhead_on_marker(app, viewport.audio, viewport, *created.begin());
    viewport.invalidate_top_strip();
    viewport.invalidate_waveform_area();
    // LAST, after the created selection is installed, so the flag cache
    // rebuilds against the final column AND the final selection hash in one
    // pass.
    viewport.kick_waveform_sync();
}
