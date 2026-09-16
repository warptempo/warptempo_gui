#include "magnification_level_propagate.h"

#include "active_views.h"
#include "audio.h"
#include "input_handler.h"
#include "magnification_level_clipboard.h"
#include "magnificationlevelmarkers.h"
#include "phase_reset_propagate.h"   // format_domain_timestamp (the family's one register)
#include "propagate_blocks.h"
#include "warpmarkers.h"

#include <set>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// THE MAGNIFICATION LEVEL PROPAGATE — the phase reset propagate's three bodies
// over the third store, with this family's deltas stated once at the header:
// the FRAME IS THE ANCHOR (no conversion in front of any window or fraction),
// the STATE IS THE LEVEL AND THE DISABLED BIT, no GuiTargetRender, and the
// landing in T+M. The shared owners (the guard, the block, the window, the
// destination walk, the nothing-matched sentence) live in propagate_blocks.h;
// the comments below carry only what is this column's, and point at the
// sibling for every clause they take verbatim.

void MagnificationLevelPropagate::copy_from_selection() {
    const auto& mv = app.warpmarkers.markers();
    const auto& lv = app.magnificationlevelmarkers.markers();
    if (app.selected_markers.empty()) return;

    const int n = static_cast<int>(mv.size());
    const int64_t song_end_frame = audio.total_frames();

    // Section-based copy, the sibling's selected-run loop: each selected marker
    // contributes the block IT owns, through the family's one membership
    // predicate and the one extent owner (the reasoning is at
    // PhaseResetPropagate::copy_from_selection and at walk_named_blocks,
    // propagate_blocks.h). std::set is ascending, so the blocks come out in
    // time order.
    std::vector<PropagateBlock> src_blocks;
    for (int i : app.selected_markers) {
        if (i < 0 || i >= n) continue;
        if (!warp_marker_propagates(mv, i)) continue;
        const std::string& name = warp_marker_label_name(mv[i]);
        const int64_t start = mv[i].time_frame;
        const int64_t end   = section_end_frame(mv, i, song_end_frame);
        src_blocks.push_back(PropagateBlock{name, start, end});
    }

    // The seconds-domain guard constant, converted once to frames — block
    // extents and marker frames are whole int64 source frames widened into the
    // double window math.
    const double guard = kPropagateBoundaryGuardSeconds *
        static_cast<double>(audio.sample_rate());

    std::vector<MagnificationLevelClipboardBlock> clipboard_blocks;
    clipboard_blocks.reserve(src_blocks.size());
    for (const auto& b : src_blocks) {
        MagnificationLevelClipboardBlock cb;
        cb.label_name         = b.label;
        cb.source_start_frame = b.start;
        cb.source_end_frame   = b.end;
        const double duration = static_cast<double>(b.end - b.start);
        if (duration <= 0.0) {
            clipboard_blocks.push_back(std::move(cb));
            continue;
        }
        // The membership window, asked of each marker's OWN FRAME (the
        // header's first delta): a level marker dropped on this block's owning
        // marker stands on it, fraction 0, and is captured here by
        // construction, while the guard still catches a frame a hand nudge
        // left just short of the boundary. The song-end rule and the
        // each-side-follows-its-own-extent rule are the window owner's
        // (propagate_membership_window).
        const auto [lo, hi] =
            propagate_membership_window(b.start, b.end, guard, song_end_frame);
        for (const auto& m : lv) {
            const double frame = static_cast<double>(m.time_frame);
            if (frame < lo)  continue;
            if (frame >= hi) continue;
            MagnificationLevelClipboardPlacement p;
            // The frame's fraction, unclamped at both ends (the sibling's
            // "let it run past" read across: a boundary is not a wall on a
            // marker the hand nudged over it).
            p.fractional_position = (frame - static_cast<double>(b.start)) /
                                    duration;
            p.source_frame        = frame;
            p.level               = m.level;
            p.disabled            = m.disabled;
            cb.placements.push_back(p);
        }
        clipboard_blocks.push_back(std::move(cb));
    }

    app.magnification_level_clipboard.set(std::move(clipboard_blocks));
}

void MagnificationLevelPropagate::open_paste_confirmation() {
    // THE THREE GUARDS ARE SILENT AND STAY SILENT, the sibling's finding: the
    // first two are the Ctrl+Alt+M dispatch arm's own gates, spelled there term
    // for term and CARDED there, so a card here would be the second for one
    // press; the third is a belt against a stale index the selection layer
    // cannot produce. An error arm exists iff a producer exists
    // (validation_topology.md).
    if (app.magnification_level_clipboard.empty()) return;
    if (app.selected_markers.size() != 1) return;
    const int anchor = *app.selected_markers.begin();
    const int n = static_cast<int>(app.warpmarkers.markers().size());
    if (anchor < 0 || anchor >= n) return;

    app.pending_paste_anchor = anchor;
    // THE SUBJECT TAG: one PASTE_CONFIRM prompt body serves both families
    // (GuiPrompt::activate_response forks its `y` on this), and the opener is
    // the one writer of which family is pending.
    app.pending_paste_column = 'M';
    // A modal surface is opening: stop playback. Space is swallowed while the
    // prompt is up, so playback cannot restart until it closes.
    playback_lifecycle.stop_playback_if_playing();
    // The sibling's prompt, word for word in its shape — `y` answers Yes,
    // raised through PromptState::present (the one raise route, which clears
    // the PAINTED bit), ONE QUESTION with the consequence riding inside it.
    app.prompt.present(
        "Paste magnification levels into matching blocks, "
        "clearing the ones already there?",
        {'y', '\x1b'},
        {"Yes", "Cancel"},
        DialogTrigger::PASTE_CONFIRM,
        PromptInitialFocus::LastButton);
    viewport.invalidate_all();
}

void MagnificationLevelPropagate::paste_apply() {
    const int anchor = app.pending_paste_anchor;
    app.pending_paste_anchor = -1;
    if (app.magnification_level_clipboard.empty()) return;
    const auto& mv = app.warpmarkers.markers();
    const int n = static_cast<int>(mv.size());
    if (anchor < 0 || anchor >= n) return;

    // THE SELECTION IS SPENT (architect 2026-09-12, the lamps resolved by use
    // case), the sibling's line for the sibling's reason: past the three belts
    // above and ahead of the walk's own matched==0 report (selection_consumed,
    // app_state.h, where the class and its inventory live).
    selection_consumed(app);

    const int64_t song_end_frame = audio.total_frames();
    std::vector<PropagateBlock> dest_blocks =
        walk_named_blocks(mv, anchor, n, song_end_frame);

    const auto& clip_blocks = app.magnification_level_clipboard.blocks();

    // Lockstep walk; stop on the first name divergence.
    const size_t pair_count = std::min(clip_blocks.size(), dest_blocks.size());
    size_t matched = 0;
    for (; matched < pair_count; ++matched) {
        if (clip_blocks[matched].label_name != dest_blocks[matched].label) break;
    }

    // "Stopped on divergence" (matched < pair_count) against "one side ran
    // out" (matched == pair_count: a clean partial walk — no message).
    std::string stop_message;
    if (matched < pair_count) {
        stop_message = "Stopped at " +
            format_domain_timestamp(
                static_cast<double>(dest_blocks[matched].start), app, audio) +
            " (label name diverged)";
    }

    if (matched == 0) {
        // Nothing to materialize, AND IT SAYS SO WITHOUT MOVING — the
        // sibling's produced-nothing arm, its two cases and its stillness
        // (the ruling is stated in full at PhaseResetPropagate::paste_apply's
        // matched==0 arm): no undo entry, no damage, no landing.
        if (!stop_message.empty()) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 std::move(stop_message));
        } else {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kNothingMatched);
        }
        return;
    }

    std::vector<GuiMagnificationLevelMarker> pre_state =
        app.magnificationlevelmarkers.markers();

    auto& out = app.magnificationlevelmarkers.markers_mut();
    // Per-block clear of destination markers inside the shifted membership
    // window — the prompt's own contract ("clearing the ones already there"),
    // the sibling's tiling argument holding unchanged (consecutive blocks tile
    // without gap or overlap; an enabled unlabeled marker between them leaves
    // a deliberate gap). THE WINDOW IS ASKED OF THE DESTINATION MARKER'S OWN
    // FRAME, the capture's rule read across.
    const double guard = kPropagateBoundaryGuardSeconds *
        static_cast<double>(audio.sample_rate());
    for (size_t i = 0; i < matched; ++i) {
        const auto [lo, hi] = propagate_membership_window(
            dest_blocks[i].start, dest_blocks[i].end, guard, song_end_frame);
        out.erase(std::remove_if(out.begin(), out.end(),
            [lo, hi](const GuiMagnificationLevelMarker& m) {
                const double frame = static_cast<double>(m.time_frame);
                return frame >= lo && frame < hi;
            }), out.end());
    }
    // Per-block materialization IN ONE STEP (the header's first delta): the
    // fraction is scaled at the true dst_start / dst_end straight into the
    // destination FRAME — there is no anchor to re-derive a marker from. A
    // fraction outside [0, 1] is scaled as it stands, the sibling's ruling.
    //
    // The overwrite semantics live entirely in the per-block clear above.
    // Materialized placements land wherever the rescaled fraction puts them —
    // possibly outside the cleared window (a frame nudged past its block's
    // edge), possibly coinciding exactly with surviving pre-existing markers
    // or with each other (a strongly shrunken block, or the zero clamp).
    // Coincident markers are legal in the store (a run of 2+ ENABLED rows at
    // one frame reads as the neutral level 0, magnificationlevelmarkers.h —
    // so the ORDER an insert gives an equal-frame run cannot move the picture)
    // and insert_marker keeps the list sorted.
    // Track the exact final index of every marker this paste materializes so
    // the landing can select precisely them: a later insert at index k shifts
    // every earlier recorded index >= k by one, so adjust as we go rather than
    // re-resolving by time (coincident markers would be ambiguous by value).
    std::vector<int> created_indices;
    const int64_t marker_wall = audio.total_frames() - 1;
    for (size_t i = 0; i < matched; ++i) {
        const double dst_start = static_cast<double>(dest_blocks[i].start);
        const double dst_dur   =
            static_cast<double>(dest_blocks[i].end) - dst_start;
        if (dst_dur <= 0.0) continue;
        for (const auto& p : clip_blocks[i].placements) {
            GuiMagnificationLevelMarker nm;
            // The rescaled position commits through snap_authored_frame — a
            // plain snap to the whole source frame, deliberately WITHOUT the
            // pixel-column anchoring the nudges / drag commits apply (the
            // sibling's reasoning: a pasted position is computed and
            // view-independent). Clamped after the snap to the column's
            // absolute range — 0 and the marker EOF wall, total - 1, the
            // single wall all three columns share; the walls win. The upper
            // clamp is load-bearing under the unclamped fraction: the
            // STORE-FINAL destination block ends at the song end
            // (total_frames), so a fraction past 1.0 there can scale to a
            // frame the wall has to pin.
            nm.time_frame = std::clamp<int64_t>(
                snap_authored_frame(dst_start + p.fractional_position * dst_dur),
                0, marker_wall);
            nm.level    = p.level;
            nm.disabled = p.disabled;
            const int new_idx =
                app.magnificationlevelmarkers.insert_marker(std::move(nm));
            for (int& ci : created_indices)
                if (ci >= new_idx) ++ci;
            created_indices.push_back(new_idx);
        }
    }

    // An undo entry represents a state change, not a gesture. The scan
    // compares WHOLE ROWS through the column's own row equality
    // (magnification_level_rows_equal — time_frame, level and disabled, the
    // whole of a row's serialized content): a self-paste onto the copy's own
    // anchor reproduces every row whole and is the designed no-op, while a
    // placement rescaled OUTSIDE the cleared windows stacks a duplicate next
    // to its surviving occupant, so the store genuinely changes there.
    // Compare before pre_state is moved into the push. The stop message and
    // the landing below still run (this arm has matched blocks, so it is past
    // the produced-nothing path that keeps the view still).
    const bool store_changed = !magnification_level_rows_equal(out, pre_state);
    if (store_changed) {
        // THE IDENTITY HINT ON THE LIVE SIDE (the contract and the producer
        // enumeration are at restore_touched_indices, app_state.h), the
        // sibling's hint for the sibling's reason: created_indices names every
        // materialized marker's FINAL index, so a REDO re-selects exactly what
        // the paste selected — coincident groups being ambiguous by frame.
        // The SNAPSHOT side stays empty (the rows an undo puts back are the
        // ones the per-block clear destroyed, which this act does not
        // enumerate).
        std::vector<int> touched_live = created_indices;
        undo.push_undo_magnification_level(std::move(pre_state),
                                           /*touched_snapshot=*/{},
                                           std::move(touched_live));
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        // NO PREVIEW (the header's rule: a level reaches no render input) and
        // NO GAIN KICK OF ITS OWN: the picture is repainted by the landing's
        // unconditional synchronous rebuild below, which this arm always
        // reaches.
    }

    // Partial paste that stopped on a divergence: the matched prefix is
    // pasted AND the divergence is reported. A clean full paste leaves
    // stop_message empty and shows nothing.
    if (!stop_message.empty()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             std::move(stop_message));
    }

    // Land in T+M with exactly the newly pasted markers selected, so the
    // paste can be inspected by eye. The set names the final post-insert
    // indices (the insert-time adjustment above).
    land_paste_in_target_view(
        std::set<int>(created_indices.begin(), created_indices.end()));
}

void MagnificationLevelPropagate::paste_state_apply() {
    if (app.magnification_level_clipboard.empty()) return;
    if (app.selected_markers.size() != 1) return;
    const int anchor = *app.selected_markers.begin();
    const auto& mv = app.warpmarkers.markers();
    const int n = static_cast<int>(mv.size());
    if (anchor < 0 || anchor >= n) return;

    // THE SELECTION IS SPENT, the sibling's line for the sibling's reason:
    // past the belts, ahead of the run's own report and of the `any_change`
    // test (selection_consumed, app_state.h). The write is the LAMP's and not
    // the selection's — the members stay selected.
    selection_consumed(app);

    const int64_t song_end_frame = audio.total_frames();
    const std::vector<PropagateBlock> dest_blocks =
        walk_named_blocks(mv, anchor, n, song_end_frame);
    const auto& clip_blocks = app.magnification_level_clipboard.blocks();

    // Boundary guard: the seconds-domain authoring tolerance converted once to
    // frames (marker frames live in source frames on both sides).
    const double guard = kPropagateBoundaryGuardSeconds *
        static_cast<double>(audio.sample_rate());

    // Flat list of every clipboard placement so the clipboard side is bucketed
    // by absolute FRAME against block windows, mirroring the destination-side
    // bucketing. THE BUCKETING KEY IS THE FRAME ON BOTH SIDES (the header's
    // first delta; the sibling buckets by the anchor on both sides for the
    // same reason — the CAPTURE decided membership by it, so the state paste
    // must bucket by the same quantity or the two acts disagree about which
    // block a marker belongs to). THE CONCATENATION IS ALREADY FRAME-ORDERED
    // AND IS LEFT ALONE: capture walks the named blocks along the timeline, so
    // the blocks are frame-ascending and disjoint, and each block's placements
    // come out frame-ordered within it. The defensive std::sort that used to
    // stand here was deleted 2026-09-16 — it could only ever permute EQUAL
    // frames (std::sort is not stable), which is the one thing it must not do:
    // the pairing below walks this list against the destination indices
    // positionally, so a tie reshuffle would hand a coincident run's states to
    // the wrong rows.
    std::vector<const MagnificationLevelClipboardPlacement*> all_placements;
    for (const auto& cb : clip_blocks)
        for (const auto& p : cb.placements)
            all_placements.push_back(&p);

    // Snapshot pre-state up front; a single undo entry is committed only if at
    // least one field actually changes.
    std::vector<GuiMagnificationLevelMarker> pre_state =
        app.magnificationlevelmarkers.markers();

    auto& out = app.magnificationlevelmarkers.markers_mut();

    bool any_change = false;
    std::string stop_message;
    // BLOCKS THAT COMPLETED THEIR PASS — the produced-nothing test below, NOT
    // the same question as any_change: a block that paired, walked and found
    // every field already right counts here, while a block the walk never
    // reached does not. Bumped at the bottom of the body, so BOTH breaks leave
    // it holding the number of blocks written before the stop.
    size_t applied_pairs = 0;

    const size_t pair_count = std::min(clip_blocks.size(), dest_blocks.size());
    for (size_t i = 0; i < pair_count; ++i) {
        // Label first, then count — paste_apply's stop-on-divergence order.
        if (clip_blocks[i].label_name != dest_blocks[i].label) {
            stop_message = "Stopped at " +
                format_domain_timestamp(
                    static_cast<double>(dest_blocks[i].start), app, audio) +
                " (label name diverged)";
            break;
        }

        // Shifted membership window on both sides, asked of each marker's OWN
        // FRAME, each side following its OWN extent (the window owner,
        // propagate_membership_window, carries the song-end rule and the
        // migration reasoning is the sibling's: a near-end marker of the
        // previous interval migrates into this block, a near-end marker of
        // this block migrates out into the next).
        const auto [dst_lo, dst_hi] = propagate_membership_window(
            dest_blocks[i].start, dest_blocks[i].end, guard, song_end_frame);
        const auto [src_lo, src_hi] = propagate_membership_window(
            clip_blocks[i].source_start_frame, clip_blocks[i].source_end_frame,
            guard, song_end_frame);

        // Windowed clipboard placements (migration applied), globally bucketed
        // by frame.
        std::vector<const MagnificationLevelClipboardPlacement*> windowed_clip;
        for (const auto* p : all_placements) {
            const double t = p->source_frame;
            if (t < src_lo)  continue;
            if (t >= src_hi) continue;
            windowed_clip.push_back(p);
        }

        // Windowed destination markers (markers_ is time-ordered, so these
        // indices come out ascending and the pairing below walks both sides in
        // order).
        std::vector<int> dest_indices;
        dest_indices.reserve(windowed_clip.size());
        for (size_t k = 0; k < out.size(); ++k) {
            const double t = static_cast<double>(out[k].time_frame);
            if (t < dst_lo)  continue;
            if (t >= dst_hi) continue;
            dest_indices.push_back(static_cast<int>(k));
        }
        if (dest_indices.size() != windowed_clip.size()) {
            stop_message = "Stopped at " +
                format_domain_timestamp(
                    static_cast<double>(dest_blocks[i].start), app, audio) +
                " (marker count mismatch)";
            break;
        }
        // THE STATE IS BOTH FIELDS (the header's second delta): the level and
        // the disabled bit, each written only where it differs, either one
        // making the run a change.
        for (size_t j = 0; j < dest_indices.size(); ++j) {
            const uint8_t want_level    = windowed_clip[j]->level;
            const bool    want_disabled = windowed_clip[j]->disabled;
            auto& m = out[dest_indices[j]];
            if (m.level != want_level) {
                m.level = want_level;
                any_change = true;
            }
            if (m.disabled != want_disabled) {
                m.disabled = want_disabled;
                any_change = true;
            }
        }
        ++applied_pairs;
    }

    // An undo entry represents a state change, not a gesture: a run that
    // rewrites no field leaves the store byte-equal, so it pushes nothing and
    // touches no dirty state. The stop message still fires unconditionally;
    // the landing fires for every run that WROTE a block — byte-equal or not
    // — a run that wrote none keeping the view still (the sibling's ruling
    // and its two halves, at PhaseResetPropagate::paste_state_apply).
    if (any_change) {
        undo.push_undo_magnification_level(std::move(pre_state));
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        // No preview and no gain kick of its own — the landing below repaints
        // the picture (the header's rule), and any_change implies
        // applied_pairs >= 1, so the landing is always reached from here.
    }

    // ONE CARD FOR THE PRESS, and only where the run paired nothing at all —
    // a divergence or a count mismatch has its own `Stopped at …` report and
    // wins here; a run that paired blocks and changed nothing stays silent.
    if (!stop_message.empty()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             std::move(stop_message));
    } else if (pair_count == 0) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kNothingMatched);
    }

    // AND A RUN THAT WROTE NO BLOCK DOES NOT MOVE — applied_pairs is the whole
    // produced-nothing family in one compare (pair_count == 0, a label
    // divergence at block 0, a count mismatch at block 0), while a run that
    // paired and left the store byte-equal STILL LANDS (the sibling's two
    // rulings, stated in full at its own tail).
    if (applied_pairs == 0) return;

    // State paste creates no markers (it rewrites fields on existing ones), so
    // there is no selection to set and the tail leaves none: the column swap
    // it runs clears the selection, and nothing restores one.
    land_paste_in_target_view({});
}

// The sibling's tail (PhaseResetPropagate::land_paste_in_target_view, whose
// head comment argues the ORDER — audio-view switch first, then the column
// writer, then the selection set with its land, then the SYNCHRONOUS rebuild
// last — and why the rebuild is needed in both entry contexts) over the M
// column. It is also drop_magnification_level_in_target_view's shape
// (input_handler.cpp), the column's own crossing. THE KICK IS THE PICTURE'S
// REPAINT here as well as the flag cache's: the store this paste wrote re-keys
// the gain profile by generation, and this unconditional rebuild is what reads
// it (the header's rule).
void MagnificationLevelPropagate::land_paste_in_target_view(
        const std::set<int>& created) {
    if (input) input->switch_active_audio_view_to('T');
    active_views.switch_active_markers_view_to('M');
    if (!created.empty()) {
        // FIRST created marker as the focus — a PROGRAMMATIC group selection,
        // the earliest focused as undo/redo's touched-set restore focuses it —
        // through the selection chokepoint, and then the lane's own land (the
        // marker lane owns the playhead; land_playhead_on_marker's rule),
        // which is one of the overlay-hide rule's two movement owners.
        selection.replace_selection(created, *created.begin());
        land_playhead_on_marker(app, viewport.audio, viewport, *created.begin());
    }
    // NO CREATED SET — the state paste, and a placement paste whose run
    // produced no marker — needs nothing at all: the column swap above cleared
    // the selection and nothing restored one.
    viewport.invalidate_top_strip();
    viewport.invalidate_waveform_area();
    // LAST, after the created selection is installed, so the flag cache
    // rebuilds against the final column AND the final selection hash in one
    // pass — and the plate against the store this paste wrote.
    viewport.kick_waveform_sync();
}
