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

// THE TWO PASTES' SHARED "NOTHING HAPPENED" SENTENCE (architect 2026-08-30,
// the strictness ruling). Both pastes end on the always-switch to target view
// — a change of scene that looks exactly like a paste — so a run that paired
// no block with any block must say that it wrote nothing, and both say it in
// the same words. Its two readers are paste_apply's matched==0 arm (where the
// destination produced no owned block) and paste_state_apply's pair_count==0
// arm. A CLEAN PARTIAL WALK is not this: it pasted what it had, and a success
// says nothing.
constexpr const char* kNothingMatched = "Nothing matched, so nothing was pasted";

// One named block resolved from a warp-marker walk. `label` is the
// owning marker's label name (empty markers don't produce entries);
// `start` is the owning marker's own absolute source frame and `end` is its
// section's extent under the EFFECTIVE-PARTICIPATION rule stated at
// section_end_index (warpmarkers.h) — the next marker that participates in
// the render,
// else the song end. A disabled marker sitting in between is not a boundary
// and does not close the block.
struct DestBlock {
    std::string label;
    int64_t     start;
    int64_t     end;
};

// (THE SECTION RULE'S EXTENT EXPRESSION LEFT THIS FILE on 2026-08-24, when the
// BPM sweep became its second reader: `section_end_frame` and the
// index-returning walk it is built on, `section_end_index`, live in
// warpmarkers.h beside `effective_disabled`, where the rule and its reasoning
// are stated in full. Both propagate walks below — the copy's selected-run loop
// and walk_named_blocks — still call it, and still call the SAME one, which is
// what the paste's lockstep match depends on: the destination blocks must be
// measured exactly as the clipboard's were.)

// Walk the warp marker list across [from_idx, to_idx_exclusive),
// returning the named blocks in order. A block's extent is section_end_frame
// (warpmarkers.h): its owning marker's time to the next EFFECTIVELY-ENABLED
// marker's
// time, or to the SONG END (song_end_frame, source frames) when no enabled
// marker follows — so the store-final enabled marker owns the section running
// to the song end, and so does a marker trailed only by disabled ones (section
// rule, architect 2026-07-23). Markers without a label name, and
// EFFECTIVELY-DISABLED labeled markers, contribute no block: the copy filters
// effective-disabled selected markers out of the clipboard, so this destination
// walk must filter them identically or a disabled labeled marker opens a
// lockstep gap. Ownership and EXTENT are filtered the same way — a disabled
// marker neither owns a block nor ends one, so no span is left ownerless
// between two enabled markers.
std::vector<DestBlock> walk_named_blocks(
    const std::vector<GuiWarpMarker>& mv,
    int from_idx, int to_idx_exclusive, int64_t song_end_frame) {
    std::vector<DestBlock> out;
    const int n = static_cast<int>(mv.size());
    if (from_idx < 0)        from_idx = 0;
    if (to_idx_exclusive > n) to_idx_exclusive = n;
    for (int i = from_idx; i < to_idx_exclusive; ++i) {
        // The propagate family's ONE membership predicate
        // (warp_marker_propagates, phase_reset_clipboard.h): labeled AND
        // effectively enabled. An effective-disabled labeled marker is not a
        // block owner, and not a boundary either — section_end_frame walks
        // past it (warpmarkers.h states the rule).
        if (!warp_marker_propagates(mv, i)) continue;
        const std::string& name = warp_marker_label_name(mv[i]);
        const int64_t start = mv[i].time_frame;
        const int64_t end   = section_end_frame(mv, i, song_end_frame);
        out.push_back(DestBlock{name, start, end});
    }
    return out;
}

}  // namespace

// The propagate family's stop-message timestamp; the contract is at the
// declaration in phase_reset_propagate.h. It left this file's anonymous
// namespace on 2026-08-20, when the MEASURE propagate became a second caller —
// one spelling of the message register rather than two.
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

void PhaseResetPropagate::copy_from_selection() {
    const auto& mv = app.warpmarkers.markers();
    const auto& tv = app.phaseresetmarkers.markers();
    if (app.selected_markers.empty()) return;

    const int n = static_cast<int>(mv.size());
    const int64_t song_end_frame = target_render.audio.total_frames();

    // Section-based copy (architect 2026-07-23): each selected marker
    // contributes the block IT owns — from its time to the end of the section
    // it renders (section_end_frame: the next effectively-enabled marker's
    // time, or the song end when none follows). The caller
    // has verified the selected set is a CONTIGUOUS run: the
    // paste walks every labeled destination block in strict lockstep, so a
    // disjoint clipboard would diverge at the first gap; contiguity is what
    // keeps the two label sequences aligned (the copy gate mirrors the `m`
    // sweep's). Unlabeled AND effective-disabled markers inside the run still
    // contribute no block, and the paste's destination walk skips both
    // identically (walk_named_blocks filters unnamed and effective-disabled
    // owners), so the sequences stay aligned; a disabled marker is not a
    // boundary on either side, so a captured block reaches across it exactly as
    // the destination block will. Not routed through
    // walk_named_blocks: the copy
    // filters on the SELECTED set and on EFFECTIVE-enabled status (a disabled
    // selected marker contributes no block), neither of which that shared
    // destination walk expresses — a separate loop here, walk_named_blocks
    // stays the paste-destination walk, and section_end_frame is the shared
    // extent both take. std::set is ascending, so the blocks
    // come out in time order.
    std::vector<DestBlock> src_blocks;
    for (int i : app.selected_markers) {
        if (i < 0 || i >= n) continue;
        // The same membership the destination walk takes, through the one
        // predicate rather than a second spelling of it: labeled AND
        // EFFECTIVE-enabled. Unlike the bpm owner predicate (which tests a raw
        // owning marker, where raw == effective), a copy-eligible marker may be
        // a labeled DEF whose enabled state the cascade can reach, so the
        // effective_disabled cascade inside that predicate matters here.
        if (!warp_marker_propagates(mv, i)) continue;
        const std::string& name = warp_marker_label_name(mv[i]);
        const int64_t start = mv[i].time_frame;
        const int64_t end   = section_end_frame(mv, i, song_end_frame);
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
        // exact and unique: an interior block ends at the next EFFECTIVELY-
        // ENABLED marker's time (section_end_frame's rule), and that is still
        // some marker's authored time, which walls at total-1 < total =
        // song_end_frame.
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
    // THE THREE GUARDS ARE SILENT AND STAY SILENT (re-verified 2026-08-30
    // under the strictness ruling): the first two are the Ctrl+Alt+P dispatch
    // arm's own gates, spelled there term for term and CARDED there, so a
    // card here would be the second for one press; the third is a belt
    // against a stale index the selection layer cannot produce. An error arm
    // exists iff a producer exists (validation_topology.md).
    if (app.phase_reset_clipboard.empty()) return;
    if (app.selected_markers.size() != 1) return;
    const int anchor = *app.selected_markers.begin();
    const int n = static_cast<int>(app.warpmarkers.markers().size());
    if (anchor < 0 || anchor >= n) return;

    app.pending_paste_anchor   = anchor;
    // A modal surface is opening: stop playback. Space is swallowed while
    // the prompt is up, so playback cannot restart until it closes.
    playback_lifecycle.stop_playback_if_playing();
    // Button words in the PLAIN spelling (PromptState owns the rule; each
    // button names its key on its tooltip): `y` answers Yes from the keyboard
    // exactly as before. Raised
    // through PromptState::present, the one raise route — which clears the
    // PAINTED bit, so the confirmation cannot be answered before it is on the
    // screen (the rule is at PromptState).
    app.prompt.present(
        "Paste phase resets into matching blocks? "
        "Existing phase resets in matched ranges will be cleared.",
        {'y', '\x1b'},
        {"Yes", "Cancel"},
        DialogTrigger::PASTE_CONFIRM,
        PromptInitialFocus::LastButton);
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
        stop_message = "Stopped at " +
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
        //
        // AND THE SECOND CASE SAYS SO SINCE 2026-08-30 (architect, the
        // strictness ruling): a paste that produced no destination block at
        // all wrote nothing, and the always-switch to target view is a change
        // of scene that looks exactly like a paste — so without a sentence the
        // act reads as having pasted. A CLEAN PARTIAL WALK (one side simply
        // ran out, with blocks matched) stays silent: it pasted what it had,
        // and a success says nothing.
        if (!stop_message.empty()) {
            notifications.notify(AppState::NotificationClass::Normal,
                                 std::move(stop_message));
        } else {
            notifications.notify(AppState::NotificationClass::Normal,
                                 kNothingMatched);
        }
        // Nothing materialized: land in target view with no new selection.
        land_paste_in_target_view({});
        return;
    }

    std::vector<GuiPhaseResetMarker> pre_state =
        app.phaseresetmarkers.markers();

    auto& out = app.phaseresetmarkers.markers_mut();
    // Per-block clear of destination phase resets inside the shifted
    // membership window [start - guard, end - guard). Two matched blocks whose
    // owners are consecutive in the render (nothing effectively enabled between
    // them) tile without gap or overlap: the first block's extent ends at the
    // second's owner, which is the second block's start, and every block's
    // clear window shifts by the same guard (the seconds constant converted
    // once to frames). An enabled UNLABELED marker between them still leaves a
    // deliberate gap — it owns its own section and contributes no block, so its
    // span is nobody's to clear.
    //
    // BEHAVIOR DELTA (2026-08-01, the effective-participation extent): a span
    // lying under a DISABLED warp marker is now inside the preceding enabled
    // block's clear window, so pre-existing destination resets there are
    // cleared and replaced. They previously survived, the old next-in-STORE
    // extent having stopped the block at the disabled marker and left that span
    // owned by nobody. The prompt's own contract is what governs — "existing
    // phase resets in matched ranges will be cleared" — and the span is inside
    // the matched range now that the disabled marker is not a boundary.
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
            // view-independent, and quantizing them to the on-screen
            // column grid would leak incidental view state into authored
            // data — since the 2026-08-22 frame-0 anchoring that grid is
            // viewport-phase-independent, so what would leak today is the
            // ZOOM LEVEL and the PAINTER WIDTH (which set the grid's
            // pitch), no longer the viewport position the original record
            // named. Clamped after the snap to the column's
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
            // NO MEASURE IS CARRIED, and that is by construction rather than by
            // omission: `nm` is a FRESH marker, so its measure field rests
            // empty. A paste materializes new resets rather than duplicating
            // the captured ones, and a note about one marker is not a fact
            // about the copies its section produced elsewhere in the piece.
            const int new_idx =
                app.phaseresetmarkers.insert_marker(std::move(nm));
            for (int& ci : created_indices)
                if (ci >= new_idx) ++ci;
            created_indices.push_back(new_idx);
        }
    }

    // An undo entry represents a state change, not a gesture. The scan
    // compares WHOLE ROWS — the standard the undo restore's row-identity
    // comparators rule ("row identity means the whole struct", undo.cpp),
    // which this scan is a second reader of: time_frame, disabled AND
    // measure, because a paste materializes MEASURELESS replacements (the
    // recorded drop above), so a self-paste onto the copy's own anchor
    // reproduces the destination resets exactly on (frame, disabled)
    // while the cleared originals' measures are destroyed — a real state
    // change that owes its undo entry and its dirty recompute. (The scan
    // compared only time_frame + disabled until 2026-08-22, resting on a
    // "PhaseResetMarker is exactly time_frame + disabled" premise that
    // went stale when the measure field landed 2026-08-19/20: that
    // self-paste read "unchanged" and silently destroyed the span's
    // measures with no undo entry.) A self-paste over a span that
    // carried NO measures is still the designed no-op — every row
    // reproduces whole — while a placement rescaled OUTSIDE the cleared
    // windows (the lead-in / near-end cases) stacks a duplicate next to
    // its surviving occupant — legal in the store
    // (the parser collapses equal-frame enabled resets to one event at
    // render/preview time, one stderr line per collapsed timestamp) — so
    // the store genuinely changes there too.
    // Compare before pre_state is moved into the push. The stop message
    // and the always-switch-to-P rule below still run.
    bool store_changed = out.size() != pre_state.size();
    for (size_t i = 0; !store_changed && i < out.size(); ++i) {
        if (out[i].time_frame != pre_state[i].time_frame ||
            out[i].disabled     != pre_state[i].disabled ||
            out[i].measure      != pre_state[i].measure) {
            store_changed = true;
        }
    }
    if (store_changed) {
        undo.push_undo_phase_reset(std::move(pre_state));
        undo.recompute_dirty();
        viewport.invalidate_waveform_area();
        target_render.trigger();
    }

    // Partial paste that stopped on a divergence: matched prefix is
    // pasted AND the divergence is reported. A clean full paste leaves
    // stop_message empty and shows nothing.
    if (!stop_message.empty()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             std::move(stop_message));
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
            stop_message = "Stopped at " +
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
            stop_message = "Stopped at " +
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
        target_render.trigger();
    }

    // ONE CARD FOR THE PRESS, and only where the run paired nothing at all:
    // pair_count == 0 means one of the two sides produced no block, so the
    // lockstep loop never ran, no flag could flip, and the switch to target
    // view below is the only visible effect — the state a sentence exists for
    // (architect 2026-08-30, the strictness ruling; the literal is its
    // sibling's, kNothingMatched above). A DIVERGENCE or a count mismatch has
    // its own `Stopped at …` report and wins here, and a run that paired
    // blocks and flipped nothing stays silent: it walked what it had, and a
    // clean walk says nothing.
    if (!stop_message.empty()) {
        notifications.notify(AppState::NotificationClass::Normal,
                             std::move(stop_message));
    } else if (pair_count == 0) {
        notifications.notify(AppState::NotificationClass::Normal,
                             kNothingMatched);
    }

    // Land in target view (phase reset's home) at the end of a completed
    // paste-state run, including diverged/mismatched/no-change cases. State
    // paste creates no resets (it only flips disabled flags on existing ones),
    // so there is no selection to set and the tail leaves none: the column swap
    // it runs clears the selection, and nothing restores one.
    land_paste_in_target_view({});
}

// The architect inspects a propagate paste by eye instead of the old
// Ctrl+Z/Ctrl+Shift+Z round-trip: the PHASE RESET propagate starts in the warp
// (source) view and ends in target view, and this tail is shared by its three
// paste actions. IT IS NO LONGER THE ONLY ACT OF THAT SHAPE — SHIFT+S, the
// drop from any view (2026-08-28,
// GuiInputHandler::drop_phase_reset_in_target_view), does the same trip for
// the same reason and takes this tail's own order verbatim, the reasoning
// below reading straight across.
//
// THE CLAIM IS SCOPED TO THIS PROPAGATE (2026-08-20): the MEASURE propagate
// that joined the family that day switches NO view and has no analogue of this
// tail. It has none because it has nowhere to land — a measure is edited
// wherever the flag paints (the home-view binding's fourth ruled exception), so
// there is no home column to carry the reader to and nothing new to select; its
// paste writes a field on markers that are already on screen.
//
// Order — audio-view switch FIRST, then marker-view switch to P, then the
// wholesale region hide, then the selection set (the playhead land rides with
// it, after the swap):
//   * switch_active_audio_view_to is the SAME chokepoint the `t` key runs
//     (validate_target_view_entry, the S<->T re-express of playhead/viewport,
//     the region hide, kick_waveform_sync, and target_render.ensure_ready all
//     fire exactly once). It is the SET-TO spelling, so naming 'T' from a
//     session already in target view is the chokepoint's own no-op and this
//     tail spells no guard of its own.
//   * switch_active_markers_view_to('P') CLEARS the selection (a column switch
//     clears) — so the selection must be set AFTER it or that clear would wipe
//     the new one.
//   * the S/T switch DOES touch the selection since 2026-07-29
//     (it collapses a 2+ selection to its focus, the point-form view-switch
//     rule), but its placement here is still free — SELECTION-NEUTRAL for every
//     reachable paste, and that is GATE-DERIVED rather than incidental: both
//     paste entries refuse unless EXACTLY ONE warp marker is selected, and the
//     confirmation prompt swallows every mutation while it is up, so the
//     selection this call sees is always that singleton and the collapse is a
//     no-op. That same toggle now also LANDS the playhead on a surviving focus
//     (the lane owns the playhead across the domain flip), which here means the
//     paste ANCHOR's target image. Harmless either way: a created-set paste lands
//     again over the top, and a paste that created nothing rests with the anchor's
//     own target image as its cursor — a better resting spot than a generic
//     re-express of wherever the cursor happened to be, and the only playhead cue
//     left, since the swap's clear means no flag claims the position. Running it
//     first keeps the heavier re-express (and its full-window invalidate) ahead of
//     the lightweight mode swap and leaves every side effect (region hide, hover
//     clear, the selection clear) coherent.
//
// Invalidation, and why the tail ends in a SYNCHRONOUS rebuild rather than plain
// damage: on_redraw is blit-only, and the run loop services the frame callback
// BEFORE the timerfd tick that runs the fingerprint-guarded flag-cache rebuild.
// switch_active_markers_view_to('P') moves two flag-cache FINGERPRINT fields —
// the active column always, and the selection hash whenever the created-set arm
// below installs one — so ordinary invalidation alone blits the leaving column's
// W flag pixels (and the pre-paste selection's) under P-live text / stem / overlay
// passes for one frame. kick_waveform_sync commits plate, fingerprint and flag
// cache before that paint, which is exactly what the bare `p` toggle takes for
// the same reason (active_views.cpp).
// IT COVERS BOTH ENTRY CONTEXTS, and it is needed in both:
//   * W+SOURCE entry — the S/T switch runs and kicks, but it
//     kicks BEFORE the column swap below, so its rebuild reads the OLD
//     active_markers_view and the stale-flag frame stands. The tail kick is then
//     the SECOND on this path, and a second kick is harmless: kick_waveform_sync's
//     two clamps (clamp_viewport_start, clamp_display_state_to_live_domain) are
//     idempotent, and force_synchronous_waveform_rebuild re-renders the plate into
//     the same surface, republishes the identical fingerprint (pending_fp_* in
//     lockstep, so no worker re-dispatch), and re-runs the fingerprint-guarded flag
//     rebuild — which is precisely the work that must happen again, the column
//     having changed in between. Cost: one extra full plate render per paste, a
//     discrete rare command.
//   * W+TARGET entry — the audio switch is skipped entirely, so the tail kick is
//     the paste's ONLY synchronous rebuild.
// The ordinary damage stays beside it: the rebuild damages y=0 through the
// waveform's bottom (top strip included), so invalidate_top_strip and
// invalidate_waveform_area are coalesced duplicates. (A THIRD CALL STOOD HERE
// and is deleted with the RESOLVED READOUT it served, 2026-08-29: this route's
// landing selection used to move that readout, which sat one lane below the
// rebuild's rect. No surface displays a resolved value any more, and the
// paste's "Stopped at …" report is a notification card with its own owner.)
void PhaseResetPropagate::land_paste_in_target_view(const std::set<int>& created) {
    if (input) input->switch_active_audio_view_to('T');
    active_views.switch_active_markers_view_to('P');
    // (THE TAIL'S OWN OVERLAY HIDE IS DELETED, 2026-08-19, with the call-site
    // inventory it belonged to.) The CREATED-SET arm below still hides, where
    // the rule puts it: it LANDS the playhead on the first created reset, and
    // the land is one of the rule's two movement owners (clear_region_highlight,
    // input_handler.h). The NO-CREATED arm lands nothing and hides nothing —
    // a paste that materialized no reset moved no playhead and touched no
    // marker, and the overlay it may leave standing derives from the tab's own
    // trim, unchanged by the paste.
    if (!created.empty()) {
        // FIRST created reset as the focus. This is a PROGRAMMATIC group
        // selection, and the product's other one — undo/redo's touched-set
        // restore — focuses the earliest for the same reason: all members are
        // equal, so there is no clicked marker to prefer (the multi-select
        // CLICKS instead focus what the user clicked, and land there).
        //
        // THROUGH THE SELECTION CHOKEPOINT (2026-08-29). The two fields were
        // written directly here until that date — the product's LAST wholesale
        // replace outside a Selection mutator — which left the sticky ctrl
        // (add_to_selection) and the shift-range anchor depending on a
        // NEIGHBOUR to clear them: the column switch two lines up, whose
        // clear_selection puts both clears above its own already-empty early
        // return. THAT ARGUMENT HOLDS TODAY and the change is behaviour-neutral
        // — the paste's two chords are W-gated, so the switch to P always
        // fires — but it rests on the switch not being a no-op, which is a
        // property of the paste's ENTRY GATE rather than of this act, and
        // switch_active_markers_view_to DOES return early when the column is
        // already the target. Riding the chokepoint makes it a property of
        // this line instead; the contract asking for it is at
        // AppState::add_to_selection.
        selection.replace_selection(created, *created.begin());
        // The marker lane owns the playhead (the rule is stated in full at
        // land_playhead_on_marker, input_pointer.cpp): this tail hands the lane a
        // brand-new focus, so it lands on it, and the always-visible cursor then
        // rests on the first created reset where the timestamp readout says it
        // is. That is the WHOLE reason for the land — the earlier
        // suppression-repair rationale (a non-empty selection hiding the cursor
        // while playhead_cursor_sample held a stale pre-paste value) went with the
        // suppression itself, and so did the extent-region write that followed
        // this land: the region IS THE TRIM, which a paste has no business
        // writing (architect 2026-07-30).
        land_playhead_on_marker(app, viewport.audio, viewport, *created.begin());
    }
    // NO CREATED SET — every paste that materialized nothing, not just the state
    // paste (paste_state_apply always lands with {}; a PLACEMENT paste whose run
    // produced no reset reaches here too) — NEEDS NOTHING AT ALL, and that is the
    // whole arm: the column swap above cleared the selection and nothing restored
    // one, so there is no group to collapse, no focus to land on, and no lane. The
    // cursor is the playhead, resting where the S->T re-express put the paste
    // anchor's image, and a paste that did nothing changes nothing. (The collapse
    // and the land that stood here existed only to clean up the P-column selection
    // the swap used to restore, deleted 2026-07-29 with the parked slots.)
    viewport.invalidate_top_strip();
    viewport.invalidate_waveform_area();
    // LAST, after the created selection is installed, so
    // the flag cache rebuilds against the final column AND the final selection
    // hash in one pass (the reasoning, and the two entry contexts it covers, are
    // in the head comment above; the authoritative caller inventory for this kick
    // lives at Viewport::kick_waveform_sync's declaration, viewport.h).
    viewport.kick_waveform_sync();
}
