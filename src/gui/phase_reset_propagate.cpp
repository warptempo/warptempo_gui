#include "phase_reset_propagate.h"

#include "active_views.h"
#include "audio.h"
#include "input_handler.h"
#include "phase_reset_clipboard.h"
#include "phaseresetmarkers.h"
#include "target_render.h"
#include "time_format.h"
#include "warp_frame_map_view.h"
#include "warpmarkers.h"

#include <set>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {

// Boundary guard for near-end bucketing. The CONSTANT stays a seconds
// value (an authoring tolerance — the largest the user ever nudges a
// destination phase reset off its true section boundary, ~2-10 ms
// typically, never more than ~92 ms — deliberately NOT tied to the engine
// N/window size, so a render-setting change cannot shift which section a
// marker counts toward); each use converts it once to frames
// (guard * sample_rate) because block extents and reset positions are
// whole int64 source frames, widened into the double guard-window
// arithmetic. A phase reset within the guard before a section
// end (or before a section start) counts toward the next chronological
// labeled section by shifting every block's membership window backward by
// this amount. Shared membership window across all three propagate
// actions: copy_from_selection, paste_apply, and paste_state_apply.
constexpr double kPhaseResetBoundaryGuardSeconds = 0.100;

// One named block resolved from a warp-marker walk. `label` is the
// owning marker's label name (empty markers don't produce entries);
// `start` and `end` are absolute source frames from the marker and its
// immediate successor.
struct DestBlock {
    std::string label;
    int64_t     start;
    int64_t     end;
};

// Walk the warp marker list across [from_idx, to_idx_exclusive),
// returning the named blocks in order. A block's extent runs from its
// owning marker to the next warp marker in the list; the STORE-FINAL marker
// owns the section running to the SONG END (song_end_frame, source frames),
// so it contributes a block ending there (section rule, architect
// 2026-07-23). Markers without a label name, and EFFECTIVELY-DISABLED labeled
// markers, contribute no block: the copy filters effective-disabled selected
// markers out of the clipboard, so this destination walk must filter them
// identically or a disabled labeled marker opens a lockstep gap (codex
// round-5 finding) — a destination section owned by a disabled marker then
// receives no paste and keeps its resets, consistent with disabled markers
// being excluded from eligibility. Only OWNERSHIP is filtered: a skipped
// marker's TIME still bounds the previous block's extent (the extent stays
// next-in-STORE, so the surviving predecessor block still ends at the next
// store marker's time).
std::vector<DestBlock> walk_named_blocks(
    const std::vector<GuiWarpMarker>& mv,
    int from_idx, int to_idx_exclusive, int64_t song_end_frame) {
    std::vector<DestBlock> out;
    const int n = static_cast<int>(mv.size());
    if (from_idx < 0)        from_idx = 0;
    if (to_idx_exclusive > n) to_idx_exclusive = n;
    for (int i = from_idx; i < to_idx_exclusive; ++i) {
        const std::string& name = warp_marker_label_name(mv[i]);
        if (name.empty()) continue;
        // Effective-disabled labeled marker: not a block owner (see head
        // comment) — but its time still bounds the previous block's extent.
        if (effective_disabled(mv, i)) continue;
        const int64_t start = mv[i].time_frame;
        // Store-final marker: its section runs to the song end. Every other
        // marker's section ends at the next store marker.
        const int64_t end   = (i + 1 < n) ? mv[i + 1].time_frame
                                          : song_end_frame;
        out.push_back(DestBlock{name, start, end});
    }
    return out;
}

// Format a stop-message timestamp in whichever audio domain the user is
// currently in. The input is a source-frame value (warp markers,
// clipboard blocks, and dest_blocks all live in whole source frames,
// widened into this double parameter); the
// timestamp is the display rendering, format_timestamp(frame / sr).
// In source view: identity, labeled " source time". In target view:
// forward-translate to the active domain via source_frame_to_active_domain,
// labeled " target time". A degenerate (empty / failed-build) target-view map
// translates as identity, so the timestamp reads through unchanged but stays
// labeled " target time", consistent with the identity fallback the rest of
// the target-view paint uses on an empty map.
std::string format_domain_timestamp(double source_frame,
                                    const AppState& app,
                                    const GuiAudio& audio) {
    const int sr = audio.sample_rate();
    const long total = static_cast<long>(audio.total_frames());
    const bool in_target = (app.active_audio_view == 'T');
    const double sr_d = static_cast<double>(sr);

    if (sr <= 0 || total <= 0 || !in_target) {
        return format_timestamp(sr > 0 ? source_frame / sr_d : 0.0) +
               (in_target ? " target time" : " source time");
    }

    const int64_t src_frame =
        static_cast<int64_t>(std::nearbyint(source_frame));
    const int64_t dom_frame =
        source_frame_to_active_domain(app, audio, src_frame);
    const double dom_seconds = static_cast<double>(dom_frame) / sr_d;
    return format_timestamp(dom_seconds) + " target time";
}

}  // namespace

void PhaseResetPropagate::copy_from_selection() {
    const auto& mv = app.warpmarkers.markers();
    const auto& tv = app.phaseresetmarkers.markers();
    if (app.selected_markers.empty()) return;

    const int n = static_cast<int>(mv.size());
    const int64_t song_end_frame = target_render.audio.total_frames();

    // Section-based copy (architect 2026-07-23): each selected marker
    // contributes the block IT owns — from its time to the next store
    // marker's time, or to the song end for the store-final marker. The caller
    // has verified the selected set is a CONTIGUOUS run (codex round-4): the
    // paste walks every labeled destination block in strict lockstep, so a
    // disjoint clipboard would diverge at the first gap; contiguity is what
    // keeps the two label sequences aligned (the copy gate mirrors the `m`
    // sweep's). Unlabeled AND effective-disabled markers inside the run still
    // contribute no block, and the paste's destination walk skips both
    // identically (walk_named_blocks filters unnamed and effective-disabled
    // owners), so the sequences stay aligned. Not routed through
    // walk_named_blocks: the copy
    // filters on the SELECTED set and on EFFECTIVE-enabled status (a disabled
    // selected marker contributes no block), neither of which that shared
    // destination walk expresses — a separate loop here, walk_named_blocks
    // stays the paste-destination walk. std::set is ascending, so the blocks
    // come out in time order.
    std::vector<DestBlock> src_blocks;
    for (int i : app.selected_markers) {
        if (i < 0 || i >= n) continue;
        // EFFECTIVE-enabled: unlike the bpm owner predicate (which tests a
        // raw owning marker, where raw == effective), a copy-eligible marker
        // may be a labeled DEF whose enabled state the cascade can reach, so
        // the effective_disabled cascade matters here.
        if (effective_disabled(mv, i)) continue;
        const std::string& name = warp_marker_label_name(mv[i]);
        if (name.empty()) continue;
        const int64_t start = mv[i].time_frame;
        const int64_t end   = (i + 1 < n) ? mv[i + 1].time_frame
                                          : song_end_frame;
        src_blocks.push_back(DestBlock{name, start, end});
    }

    std::vector<ClipboardBlock> clipboard_blocks;
    clipboard_blocks.reserve(src_blocks.size());
    for (const auto& b : src_blocks) {
        ClipboardBlock cb;
        cb.label_name   = b.label;
        cb.source_start_frame = b.start;
        cb.source_end_frame   = b.end;
        const double duration = b.end - b.start;
        if (duration <= 0.0) {
            clipboard_blocks.push_back(std::move(cb));
            continue;
        }
        // The seconds-domain guard constant, converted once to frames —
        // block extents and reset positions are whole int64 source frames
        // widened into the double window math.
        const double guard = kPhaseResetBoundaryGuardSeconds *
            static_cast<double>(target_render.audio.sample_rate());
        // Membership window shifts back by the guard so a lead-in phase
        // reset (authored just before this block's owning marker) is
        // captured as part of this block; the fractional anchor stays at
        // the true marker time, so a lead-in reset gets a small negative
        // fractional_position and round-trips to the same lead-in offset.
        // Song-end block (its extent ends at the song end): keep the shifted
        // LOWER bound (lead-ins before the final marker still belong to it) but
        // use the UNSHIFTED upper bound. The end guard exists to reassign the
        // tail to the NEXT section's owner; at song end there is no next owner,
        // so the guard would orphan the tail instead — the final block owns its
        // section through the last frame. The capture follows the SOURCE
        // block's own extent (a clipboard block captured at song end may later
        // paste onto a non-final destination and vice versa; each side's window
        // follows its own extent). Detected by extent-end == song_end_frame,
        // exact and unique: interior blocks end at the next marker's time,
        // which walls at total-1 < total = song_end_frame.
        const double lo = b.start - guard;
        const double hi = (b.end == song_end_frame)
                              ? static_cast<double>(b.end)
                              : std::max(lo, b.end - guard);
        for (const auto& t : tv) {
            const double t_time = t.time_frame;
            if (t_time < lo)  continue;
            if (t_time >= hi) continue;
            ClipboardPlacement p;
            p.fractional_position = (t_time - b.start) / duration;
            p.source_frame         = t.time_frame;
            p.disabled            = t.disabled;
            cb.placements.push_back(p);
        }
        clipboard_blocks.push_back(std::move(cb));
    }

    app.phase_reset_clipboard.set(std::move(clipboard_blocks));
}

void PhaseResetPropagate::open_paste_confirmation() {
    if (app.phase_reset_clipboard.empty()) return;
    if (app.selected_markers.size() != 1) return;
    const int anchor = *app.selected_markers.begin();
    const int n = static_cast<int>(app.warpmarkers.markers().size());
    if (anchor < 0 || anchor >= n) return;

    app.pending_paste_anchor   = anchor;
    // A modal surface is opening: stop playback. Space is swallowed while
    // the prompt is up, so playback cannot restart until it closes.
    playback_lifecycle.stop_playback_if_playing();
    app.prompt.active          = true;
    app.prompt.text            =
        "paste phase resets into matching blocks? "
        "existing phase resets in matched ranges will be cleared.";
    app.prompt.response_keys   = {'y', '\x1b'};
    app.prompt.response_labels = {"[y]es", "[esc]"};
    app.prompt.trigger         = DialogTrigger::PASTE_CONFIRM;
    viewport.invalidate_all();
}

void PhaseResetPropagate::paste_apply() {
    const int anchor = app.pending_paste_anchor;
    app.pending_paste_anchor = -1;
    if (app.phase_reset_clipboard.empty()) return;
    const auto& mv = app.warpmarkers.markers();
    const int n = static_cast<int>(mv.size());
    if (anchor < 0 || anchor >= n) return;

    const int64_t song_end_frame = target_render.audio.total_frames();
    std::vector<DestBlock> dest_blocks =
        walk_named_blocks(mv, anchor, n, song_end_frame);

    const auto& clip_blocks = app.phase_reset_clipboard.blocks();

    // Lockstep walk; stop on the first name divergence.
    const size_t pair_count = std::min(clip_blocks.size(), dest_blocks.size());
    size_t matched = 0;
    for (; matched < pair_count; ++matched) {
        if (clip_blocks[matched].label_name != dest_blocks[matched].label) break;
    }

    // Distinguish "stopped on divergence" (matched < pair_count: the loop
    // broke at a label mismatch) from "one side ran out" (matched ==
    // pair_count: a clean partial walk — no message, mirroring
    // paste_state's silent partial-run rule).
    std::string stop_message;
    if (matched < pair_count) {
        stop_message = "stopped at " +
            format_domain_timestamp(dest_blocks[matched].start, app,
                                    target_render.audio) +
            " (label name diverged)";
    }

    if (matched == 0) {
        // Nothing to materialize: either a divergence at block 0
        // (stop_message non-empty), or the destination produced zero
        // owned blocks (e.g., no enabled labeled marker at or after the anchor —
        // the store-final marker now DOES own a block, running to the song
        // end, so only unlabeled or effective-disabled destinations reach
        // here), which is a clean partial walk and stays silent. Either way:
        // no undo
        // entry, no waveform / render flush, but the view-switch fires
        // per the always-switch rule.
        if (!stop_message.empty()) {
            app.transient_status_message = std::move(stop_message);
            viewport.invalidate_timestamp_area();
        }
        // Nothing materialized: land in target view with no new selection.
        land_paste_in_target_view({});
        return;
    }

    std::vector<GuiPhaseResetMarker> pre_state =
        app.phaseresetmarkers.markers();

    auto& out = app.phaseresetmarkers.markers_mut();

    // Per-block clear of destination phase resets inside the shifted
    // membership window [start - guard, end - guard). Adjacent matched
    // blocks still tile without gap or overlap because every block's
    // clear window shifts by the same guard (the seconds constant
    // converted once to frames).
    const double guard = kPhaseResetBoundaryGuardSeconds *
        static_cast<double>(target_render.audio.sample_rate());
    for (size_t i = 0; i < matched; ++i) {
        // Song-end destination block: keep the shifted lower bound but use the
        // UNSHIFTED upper bound — the end guard reassigns the tail to the next
        // section's owner, and at song end there is no next owner, so the guard
        // would orphan the final 100 ms instead. The clear window follows the
        // DESTINATION block's own extent (a non-final destination paired with a
        // song-end clipboard block still shifts; each side follows its own).
        const double lo = dest_blocks[i].start - guard;
        const double hi = (dest_blocks[i].end == song_end_frame)
                              ? static_cast<double>(dest_blocks[i].end)
                              : std::max(lo, dest_blocks[i].end - guard);
        out.erase(std::remove_if(out.begin(), out.end(),
            [lo, hi](const GuiPhaseResetMarker& m) {
                return m.time_frame >= lo && m.time_frame < hi;
            }), out.end());
    }

    // Per-block materialization. The fractional anchor and duration stay
    // at the true dst_start / dst_end, so a negative fractional_position
    // (captured from a lead-in placement) lands the marker in the lead-in
    // before dst_start. Clamp to 0 per the universal no-negative-time
    // rule; insert_marker does not clamp.
    //
    // The overwrite semantics live entirely in the per-block membership-
    // window clear above. Materialized placements land wherever rescaling
    // puts them — possibly outside the cleared windows (a lead-in scaled
    // by a longer destination block, a near-end placement scaled by a
    // shorter one), possibly coinciding exactly with surviving
    // pre-existing resets or with each other (a strongly shrunken block,
    // or the zero clamp). Coincident resets simply stack and are legal in
    // the store: the parser normalizes them at render/preview time
    // (equal-frame enabled resets collapse to one event, one stderr line
    // per collapsed timestamp), and insert_marker keeps the in-memory list
    // sorted.
    // Track the exact final index of every reset this paste materializes, so
    // the target-view landing can select precisely them. insert_marker keeps
    // the store time-sorted and returns the insertion index; a later insert at
    // index k shifts every earlier recorded index >= k by one, so adjust as we
    // go rather than re-resolving by time (coincident resets would be
    // ambiguous to match by value).
    std::vector<int> created_indices;
    const int64_t reset_wall = target_render.audio.total_frames() - 1;
    for (size_t i = 0; i < matched; ++i) {
        const double dst_start = dest_blocks[i].start;
        const double dst_dur   = dest_blocks[i].end - dst_start;
        if (dst_dur <= 0.0) continue;
        for (const auto& p : clip_blocks[i].placements) {
            GuiPhaseResetMarker nm;
            // The rescaled position commits through snap_authored_frame —
            // a plain snap to the whole source frame, deliberately WITHOUT
            // the pixel-column anchoring the nudges / drag commits
            // apply: pasted positions are computed and
            // view-independent, and quantizing them to whatever viewport
            // happens to be on screen would leak incidental view state
            // into authored data. Clamped after the snap to the column's
            // absolute range — 0 (the universal no-negative-position rule)
            // and the marker EOF wall, total - 1, the single wall both
            // marker columns share; the walls win. The upper clamp is now
            // load-bearing: the STORE-FINAL destination block ends at the
            // song end (total_frames), so a near-end placement can rescale to
            // total_frames itself — one past the wall — and the clamp pins it
            // to total - 1 (interior blocks end at warp markers, which
            // already wall at total - 1).
            nm.time_frame = std::clamp<int64_t>(snap_authored_frame(
                dst_start + p.fractional_position * dst_dur), 0, reset_wall);
            nm.disabled     = p.disabled;
            const int new_idx =
                app.phaseresetmarkers.insert_marker(std::move(nm));
            for (int& ci : created_indices)
                if (ci >= new_idx) ++ci;
            created_indices.push_back(new_idx);
        }
    }

    // An undo entry represents a state change, not a gesture. Pasting
    // onto the copy's own anchor reproduces the destination resets byte-
    // equal (PhaseResetMarker is exactly time_frame + disabled) — but
    // only when every placement lands inside the cleared membership
    // windows above; that self-paste is a no-op that pushes nothing and
    // touches no dirty/render state. A placement rescaled OUTSIDE the
    // cleared windows (the lead-in / near-end cases) stacks a duplicate
    // next to its surviving occupant on self-paste — legal in the store
    // (the parser collapses equal-frame enabled resets to one event at
    // render/preview time, one stderr line per collapsed timestamp) — so
    // the store genuinely changes and the undo entry is a real state
    // change.
    // Compare before pre_state is moved into the push. The stop message
    // and the always-switch-to-P rule below still run.
    bool store_changed = out.size() != pre_state.size();
    for (size_t i = 0; !store_changed && i < out.size(); ++i) {
        if (out[i].time_frame != pre_state[i].time_frame ||
            out[i].disabled     != pre_state[i].disabled) {
            store_changed = true;
        }
    }
    if (store_changed) {
        undo.push_undo_phase_reset(std::move(pre_state));
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }

    // Partial paste that stopped on a divergence: matched prefix is
    // pasted AND the divergence is reported. A clean full paste leaves
    // stop_message empty and shows nothing.
    if (!stop_message.empty()) {
        app.transient_status_message = std::move(stop_message);
        viewport.invalidate_timestamp_area();
    }

    // Land in target view (phase reset's home) with exactly the newly pasted
    // resets selected, so the architect can inspect the paste by eye. The set
    // was built with insert-time index adjustment above, so it names the final
    // post-insert indices.
    land_paste_in_target_view(
        std::set<int>(created_indices.begin(), created_indices.end()));
}

void PhaseResetPropagate::paste_state_apply() {
    if (app.phase_reset_clipboard.empty()) return;
    if (app.selected_markers.size() != 1) return;
    const int anchor = *app.selected_markers.begin();
    const auto& mv = app.warpmarkers.markers();
    const int n = static_cast<int>(mv.size());
    if (anchor < 0 || anchor >= n) return;

    const int64_t song_end_frame = target_render.audio.total_frames();
    const std::vector<DestBlock> dest_blocks =
        walk_named_blocks(mv, anchor, n, song_end_frame);
    const auto& clip_blocks = app.phase_reset_clipboard.blocks();

    // Boundary guard: the seconds-domain authoring tolerance converted
    // once to frames (phase reset time_frame lives in source frames on
    // both sides).
    const double n_guard = kPhaseResetBoundaryGuardSeconds *
        static_cast<double>(target_render.audio.sample_rate());

    // Flat list of every clipboard placement so we can bucket the
    // clipboard side by absolute source_frame against block windows,
    // mirroring the destination-side bucketing. Capture produces
    // block-ordered, within-block-time-ordered placements, so a flat
    // concatenation is already source_frame-ordered; sort defensively.
    std::vector<const ClipboardPlacement*> all_placements;
    for (const auto& cb : clip_blocks)
        for (const auto& p : cb.placements)
            all_placements.push_back(&p);
    std::sort(all_placements.begin(), all_placements.end(),
        [](const ClipboardPlacement* a, const ClipboardPlacement* b) {
            return a->source_frame < b->source_frame;
        });

    // Snapshot pre-state up front; we commit a single undo entry only
    // if at least one flag actually changes.
    std::vector<GuiPhaseResetMarker> pre_state =
        app.phaseresetmarkers.markers();

    auto& out = app.phaseresetmarkers.markers_mut();

    bool any_change = false;
    std::string stop_message;

    const size_t pair_count = std::min(clip_blocks.size(), dest_blocks.size());
    for (size_t i = 0; i < pair_count; ++i) {
        // Label first, then count — mirrors paste_apply's
        // stop-on-divergence order.
        if (clip_blocks[i].label_name != dest_blocks[i].label) {
            stop_message = "stopped at " +
                format_domain_timestamp(dest_blocks[i].start, app,
                                        target_render.audio) +
                " (label name diverged)";
            break;
        }

        // Shifted membership window [start - N, end - N) on both sides.
        // A near-end marker of the previous interval (within N before
        // this block's start) migrates into this block; a near-end
        // marker of this block (within N before its end) migrates out
        // into the next interval. If the next interval is unlabeled or
        // past the compared range, the marker falls off — symmetrically
        // on both sides. Clamp hi >= lo so a pathologically tiny block
        // produces an empty window (count 0), not an inverted one.
        // Song-end block: the UPPER bound is UNSHIFTED — the end guard
        // reassigns the tail to the next section's owner, and at song end
        // there is no next owner, so the guard would orphan the final 100 ms.
        // Each side follows its OWN extent: the clipboard block and the
        // destination block need not both be song-end (a song-end capture may
        // pair with a non-final destination and vice versa), so dst_hi tests
        // the destination extent and src_hi the source extent independently.
        const double dst_lo = dest_blocks[i].start - n_guard;
        const double dst_hi =
            (dest_blocks[i].end == song_end_frame)
                ? static_cast<double>(dest_blocks[i].end)
                : std::max(dst_lo, dest_blocks[i].end - n_guard);
        const double src_lo = clip_blocks[i].source_start_frame - n_guard;
        const double src_hi =
            (clip_blocks[i].source_end_frame == song_end_frame)
                ? static_cast<double>(clip_blocks[i].source_end_frame)
                : std::max(src_lo, clip_blocks[i].source_end_frame - n_guard);

        // Windowed clipboard placements (migration applied). Globally
        // bucketed by source_frame so a near-end placement originally
        // captured under block i-1 lands in block i's window when i-1
        // and i are adjacent labeled blocks.
        std::vector<const ClipboardPlacement*> windowed_clip;
        for (const auto* p : all_placements) {
            const double t = p->source_frame;
            if (t < src_lo)  continue;
            if (t >= src_hi) continue;
            windowed_clip.push_back(p);
        }

        // Windowed destination markers (markers_ is time-ordered).
        std::vector<int> dest_indices;
        dest_indices.reserve(windowed_clip.size());
        for (size_t k = 0; k < out.size(); ++k) {
            const double t = out[k].time_frame;
            if (t < dst_lo)  continue;
            if (t >= dst_hi) continue;
            dest_indices.push_back(static_cast<int>(k));
        }
        if (dest_indices.size() != windowed_clip.size()) {
            stop_message = "stopped at " +
                format_domain_timestamp(dest_blocks[i].start, app,
                                        target_render.audio) +
                " (marker count mismatch)";
            break;
        }
        for (size_t j = 0; j < dest_indices.size(); ++j) {
            const bool want_disabled = windowed_clip[j]->disabled;
            auto& m = out[dest_indices[j]];
            if (m.disabled != want_disabled) {
                m.disabled = want_disabled;
                any_change = true;
            }
        }
    }

    // An undo entry represents a state change, not a gesture: a paste-
    // state run that flips no flag leaves the store byte-equal, so it
    // pushes nothing and touches no dirty/render state. The stop message
    // and the P-view switch below still fire unconditionally.
    if (any_change) {
        undo.push_undo_phase_reset(std::move(pre_state));
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        target_render.trigger();
    }

    if (!stop_message.empty()) {
        app.transient_status_message = std::move(stop_message);
        viewport.invalidate_timestamp_area();
    }

    // Land in target view (phase reset's home) at the end of a completed
    // paste-state run, including diverged/mismatched/no-change cases. State
    // paste creates no resets (it only flips disabled flags on existing ones),
    // so there is no new selection to set — the empty set leaves the restored
    // P-mode selection in place.
    land_paste_in_target_view({});
}

// The architect inspects a propagate paste by eye instead of the old
// Ctrl+Z/Ctrl+Shift+Z round-trip: propagate is the ONE authoring action that
// starts in the warp (source) view and ends in target view. This tail is
// shared by all three paste actions.
//
// Order — audio-view switch FIRST, then marker-view switch to P, then the
// selection set (the playhead land rides with it, after the swap):
//   * handle_active_audio_view_toggle is the SAME chokepoint the `t` key runs
//     (validate_target_view_entry, the S<->T re-express of playhead/viewport,
//     the region clear, kick_waveform_sync, and target_render.ensure_ready all
//     fire exactly once). It is a TOGGLE, so it is called only when the session
//     is not already in target view.
//   * switch_active_markers_view_to('P') swaps the W/P selection slots, prunes,
//     and clears the hover popup — so the selection must be set AFTER it or the
//     restored P-slot selection would clobber the new one.
//   * handle_active_audio_view_toggle never touches the selection, so its
//     placement is free; running it first keeps the heavier re-express (and its
//     full-window invalidate) ahead of the lightweight mode swap and leaves
//     every side effect (region clear, hover clear, selection slots) coherent.
//
// Invalidation: when the audio-view switch fires (session was in source view),
// handle_active_audio_view_toggle already full-window-invalidates, so the tail
// damage here is a harmless subset. When the session is already in target view
// the audio switch is skipped and switch_active_markers_view_to invalidates
// nothing itself, so the tail damage (top strip flags + waveform stems +
// timestamp readout) is the only repaint covering the mode swap and the new
// selection.
void PhaseResetPropagate::land_paste_in_target_view(const std::set<int>& created) {
    if (input && app.active_audio_view != 'T') {
        input->handle_active_audio_view_toggle();
    }
    active_views.switch_active_markers_view_to('P');
    if (!created.empty()) {
        // The paste replaces the membership with the created resets -> demote a
        // SelectionExtent region to Free (explicit: switch_active_markers_view_to
        // is a no-op when already in P view, so its prune-demote may not have run).
        demote_region_provenance(app.region);
        app.selected_markers     = created;
        // FIRST created reset as the focus. This is a PROGRAMMATIC group
        // selection, and the product's other one — undo/redo's touched-set
        // restore — focuses the earliest for the same reason: all members are
        // equal, so there is no clicked marker to prefer and the earliest is the
        // one the group's playhead sits on anyway (the multi-select CLICKS focus
        // what the user clicked, but they too LAND at the earliest selected).
        app.last_selected_marker = *created.begin();
        // The marker lane owns the playhead (the rule is stated in full at
        // land_playhead_on_marker, input_pointer.cpp): this tail hands the lane a
        // brand-new focus, so it lands on it — otherwise the non-empty selection
        // would suppress the cursor while playhead_cursor_sample still held the
        // pre-paste value re-expressed through the S->T switch, leaving NO
        // playhead painted anywhere. Land only: the paste deliberately sets no
        // extent region (the demote above is its whole region handling), and the
        // land's own dissolve is the region's end here.
        land_playhead_on_marker(app, viewport.audio, viewport, *created.begin());
    }
    viewport.invalidate_top_strip();
    viewport.invalidate_waveform_area();
    viewport.invalidate_timestamp_area();
}
